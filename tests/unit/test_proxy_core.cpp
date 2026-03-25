#include "sip/message.hpp"
#include "sip/proxy_core.hpp"
#include "sip/stack.hpp"
#include "sip/transport.hpp"
#include <gtest/gtest.h>

using namespace ims::sip;

namespace {

static constexpr auto kRegisterMsg =
    "REGISTER sip:ims.example.com SIP/2.0\r\n"
    "Via: SIP/2.0/UDP 10.0.0.1:5060;branch=z9hG4bK776asdhds\r\n"
    "Max-Forwards: 70\r\n"
    "To: <sip:user@ims.example.com>\r\n"
    "From: <sip:user@ims.example.com>;tag=1928301774\r\n"
    "Call-ID: a84b4c76e66710@10.0.0.1\r\n"
    "CSeq: 1 REGISTER\r\n"
    "Contact: <sip:user@10.0.0.1:5060>\r\n"
    "Expires: 3600\r\n"
    "Content-Length: 0\r\n"
    "\r\n";

class CapturingTransport final : public ITransport {
public:
    auto send(const SipMessage&, const Endpoint& dest) -> ims::VoidResult override {
        ++send_count;
        destinations.push_back(dest);
        return {};
    }

    void setMessageCallback(MessageCallback cb) override {
        callback = std::move(cb);
    }

    auto start() -> ims::VoidResult override { return {}; }
    void stop() override {}

    auto localEndpoint() const -> Endpoint override {
        return Endpoint{.address = "127.0.0.1", .port = 5060, .transport = "udp"};
    }

    MessageCallback callback;
    int send_count = 0;
    std::vector<Endpoint> destinations;
};

class ProxyCoreTest : public ::testing::Test {};

TEST_F(ProxyCoreTest, ProcessRouteHeadersRemovesMatchingTopRoute) {
    auto result = SipMessage::parse(kRegisterMsg);
    ASSERT_TRUE(result.has_value()) << result.error().message;

    ProxyCore proxy("edge1.ims.example.com", 5060);
    result->addRoute("<sip:edge1.ims.example.com:5060;lr>");
    result->addRoute("<sip:edge2.ims.example.com:5060;lr>");

    EXPECT_TRUE(proxy.processRouteHeaders(*result));

    auto routes = result->routes();
    ASSERT_EQ(routes.size(), 1u);
    EXPECT_NE(routes.front().find("edge2.ims.example.com"), std::string::npos);
}

TEST_F(ProxyCoreTest, BuildAndAddPathHeader) {
    auto result = SipMessage::parse(kRegisterMsg);
    ASSERT_TRUE(result.has_value()) << result.error().message;

    ProxyCore proxy("pcscf.example.com", 5060);
    EXPECT_EQ(proxy.buildPathHeader(), "<sip:pcscf.example.com:5060;lr>");

    proxy.addPathHeader(*result);
    EXPECT_EQ(result->getHeader("Path"), std::optional<std::string>{"<sip:pcscf.example.com:5060;lr>"});
}

TEST_F(ProxyCoreTest, BuildForwardedCancelReusesInviteBranchAndResetsVia) {
    auto cancel = SipMessage::parse(
        "CANCEL sip:callee@ims.example.com SIP/2.0\r\n"
        "Via: SIP/2.0/UDP pcscf.example.com:5060;branch=z9hG4bK-upstream\r\n"
        "Via: SIP/2.0/UDP 10.0.0.1:5060;branch=z9hG4bK-client\r\n"
        "Max-Forwards: 70\r\n"
        "Route: <sip:scscf.example.com:5062;lr>\r\n"
        "To: <sip:callee@ims.example.com>\r\n"
        "From: <sip:caller@ims.example.com>;tag=8735290\r\n"
        "Call-ID: cancel-call-id\r\n"
        "CSeq: 1 CANCEL\r\n"
        "Content-Length: 0\r\n"
        "\r\n");
    ASSERT_TRUE(cancel.has_value()) << cancel.error().message;

    ProxyCore proxy("scscf.example.com", 5062);
    auto forwarded = proxy.buildForwardedCancel(*cancel, {
        .transport = "udp",
        .invite_branch = "z9hG4bK-invite-branch",
        .local_address = "scscf.example.com",
        .local_port = 5062,
    });
    ASSERT_TRUE(forwarded.has_value()) << forwarded.error().message;

    EXPECT_EQ(forwarded->viaCount(), 1);
    EXPECT_NE(forwarded->topVia().find("scscf.example.com:5062"), std::string::npos);
    EXPECT_NE(forwarded->topVia().find("z9hG4bK-invite-branch"), std::string::npos);
    EXPECT_EQ(forwarded->maxForwards(), 69);
    EXPECT_TRUE(forwarded->routes().empty());
}

TEST_F(ProxyCoreTest, ProcessRouteHeadersKeepsNonMatchingTopRoute) {
    auto result = SipMessage::parse(kRegisterMsg);
    ASSERT_TRUE(result.has_value()) << result.error().message;

    ProxyCore proxy("edge1.ims.example.com", 5060);
    result->addRoute("<sip:edge2.ims.example.com:5060;lr>");
    result->addRoute("<sip:edge3.ims.example.com:5060;lr>");

    EXPECT_FALSE(proxy.processRouteHeaders(*result));

    auto routes = result->routes();
    ASSERT_EQ(routes.size(), 2u);
    EXPECT_NE(routes[0].find("edge2.ims.example.com"), std::string::npos);
    EXPECT_NE(routes[1].find("edge3.ims.example.com"), std::string::npos);
}

TEST_F(ProxyCoreTest, ProcessRouteHeadersReturnsFalseWithoutRoutes) {
    auto result = SipMessage::parse(kRegisterMsg);
    ASSERT_TRUE(result.has_value()) << result.error().message;

    ProxyCore proxy("edge1.ims.example.com", 5060);

    EXPECT_FALSE(proxy.processRouteHeaders(*result));
    EXPECT_TRUE(result->routes().empty());
}

TEST_F(ProxyCoreTest, ForwardResponseUsesReceivedAndRport) {
    auto result = SipMessage::parse(
        "SIP/2.0 200 OK\r\n"
        "Via: SIP/2.0/UDP proxy.local:5060;branch=z9hG4bK-proxy\r\n"
        "Via: SIP/2.0/TCP client.example.com:5070;received=198.51.100.10;rport=5088;branch=z9hG4bK-client\r\n"
        "To: <sip:user@ims.example.com>;tag=to-tag\r\n"
        "From: <sip:user@ims.example.com>;tag=from-tag\r\n"
        "Call-ID: response-call-id\r\n"
        "CSeq: 1 REGISTER\r\n"
        "Content-Length: 0\r\n"
        "\r\n");
    ASSERT_TRUE(result.has_value()) << result.error().message;

    ProxyCore proxy("proxy.local", 5060);
    auto transport = std::make_shared<CapturingTransport>();

    auto forward = proxy.forwardResponse(*result, transport);
    ASSERT_TRUE(forward.has_value()) << forward.error().message;
    ASSERT_EQ(transport->send_count, 1);
    ASSERT_EQ(transport->destinations.size(), 1u);
    EXPECT_EQ(transport->destinations[0].address, "198.51.100.10");
    EXPECT_EQ(transport->destinations[0].port, 5088);
    EXPECT_EQ(transport->destinations[0].transport, "tcp");
    EXPECT_EQ(result->viaCount(), 1);
    EXPECT_NE(result->topVia().find("client.example.com:5070"), std::string::npos);
}

TEST_F(ProxyCoreTest, ForwardResponseRemovesOnlyCurrentProxyVia) {
    auto result = SipMessage::parse(
        "SIP/2.0 401 Unauthorized\r\n"
        "Via: SIP/2.0/UDP 127.0.0.1:5061;branch=z9hG4bK-icscf\r\n"
        "Via: SIP/2.0/UDP 127.0.0.1:5060;branch=z9hG4bK-pcscf\r\n"
        "Via: SIP/2.0/UDP ue.example.com:5090;received=198.51.100.10;rport=5090;branch=z9hG4bK-ue\r\n"
        "To: <sip:user@ims.example.com>;tag=to-tag\r\n"
        "From: <sip:user@ims.example.com>;tag=from-tag\r\n"
        "Call-ID: response-call-id\r\n"
        "CSeq: 1 REGISTER\r\n"
        "Content-Length: 0\r\n"
        "\r\n");
    ASSERT_TRUE(result.has_value()) << result.error().message;

    ProxyCore proxy("127.0.0.1", 5061);
    auto transport = std::make_shared<CapturingTransport>();

    auto forward = proxy.forwardResponse(*result, transport);
    ASSERT_TRUE(forward.has_value()) << forward.error().message;
    ASSERT_EQ(transport->send_count, 1);
    ASSERT_EQ(transport->destinations.size(), 1u);
    EXPECT_EQ(transport->destinations[0].address, "127.0.0.1");
    EXPECT_EQ(transport->destinations[0].port, 5060);
    EXPECT_EQ(result->viaCount(), 2);
    EXPECT_NE(result->topVia().find("127.0.0.1:5060"), std::string::npos);
}

TEST_F(ProxyCoreTest, ForwardResponseFallsBackToViaHostAndPort) {
    auto result = SipMessage::parse(
        "SIP/2.0 200 OK\r\n"
        "Via: SIP/2.0/UDP proxy.local:5060;branch=z9hG4bK-proxy\r\n"
        "Via: SIP/2.0/UDP client.example.com:5071;branch=z9hG4bK-client\r\n"
        "To: <sip:user@ims.example.com>;tag=to-tag\r\n"
        "From: <sip:user@ims.example.com>;tag=from-tag\r\n"
        "Call-ID: response-call-id\r\n"
        "CSeq: 1 REGISTER\r\n"
        "Content-Length: 0\r\n"
        "\r\n");
    ASSERT_TRUE(result.has_value()) << result.error().message;

    ProxyCore proxy("proxy.local", 5060);
    auto transport = std::make_shared<CapturingTransport>();

    auto forward = proxy.forwardResponse(*result, transport);
    ASSERT_TRUE(forward.has_value()) << forward.error().message;
    ASSERT_EQ(transport->send_count, 1);
    ASSERT_EQ(transport->destinations.size(), 1u);
    EXPECT_EQ(transport->destinations[0].address, "client.example.com");
    EXPECT_EQ(transport->destinations[0].port, 5071);
    EXPECT_EQ(transport->destinations[0].transport, "udp");
}

TEST_F(ProxyCoreTest, ForwardResponseRejectsWhenNoRemainingViaExists) {
    auto result = SipMessage::parse(
        "SIP/2.0 200 OK\r\n"
        "Via: SIP/2.0/UDP proxy.local:5060;branch=z9hG4bK-proxy\r\n"
        "To: <sip:user@ims.example.com>;tag=to-tag\r\n"
        "From: <sip:user@ims.example.com>;tag=from-tag\r\n"
        "Call-ID: response-call-id\r\n"
        "CSeq: 1 REGISTER\r\n"
        "Content-Length: 0\r\n"
        "\r\n");
    ASSERT_TRUE(result.has_value()) << result.error().message;

    ProxyCore proxy("proxy.local", 5060);
    auto transport = std::make_shared<CapturingTransport>();

    auto forward = proxy.forwardResponse(*result, transport);
    ASSERT_FALSE(forward.has_value());
    EXPECT_EQ(forward.error().code, ims::ErrorCode::kSipTransportError);
    EXPECT_EQ(transport->send_count, 0);
}

TEST_F(ProxyCoreTest, ForwardResponseUpstreamRemovesTopVia) {
    auto response = SipMessage::parse(
        "SIP/2.0 200 OK\r\n"
        "Via: SIP/2.0/UDP proxy.local:5060;branch=z9hG4bK-proxy\r\n"
        "Via: SIP/2.0/UDP client.example.com:5070;branch=z9hG4bK-client\r\n"
        "To: <sip:user@ims.example.com>;tag=to-tag\r\n"
        "From: <sip:user@ims.example.com>;tag=from-tag\r\n"
        "Call-ID: response-call-id\r\n"
        "CSeq: 1 REGISTER\r\n"
        "Content-Length: 0\r\n"
        "\r\n");
    ASSERT_TRUE(response.has_value()) << response.error().message;

    auto request = SipMessage::parse(kRegisterMsg);
    ASSERT_TRUE(request.has_value()) << request.error().message;

    auto transport = std::make_shared<CapturingTransport>();
    boost::asio::io_context io;
    auto txn = std::make_shared<ServerTransaction>(std::move(*request), transport,
                                                   Endpoint{.address = "198.51.100.10", .port = 5060, .transport = "udp"},
                                                   io);

    ProxyCore proxy("proxy.local", 5060);
    auto forwarded = proxy.forwardResponseUpstream(*response, txn);
    ASSERT_TRUE(forwarded.has_value()) << forwarded.error().message;
    ASSERT_EQ(transport->send_count, 1);
    ASSERT_EQ(transport->destinations.size(), 1u);
    EXPECT_EQ(transport->destinations[0].address, "198.51.100.10");
}

TEST_F(ProxyCoreTest, PrepareRequestForForwardDecrementsMaxForwardsAndAddsVia) {
    auto result = SipMessage::parse(kRegisterMsg);
    ASSERT_TRUE(result.has_value()) << result.error().message;

    ProxyCore proxy("proxy.local", 5060);

    auto prepare = proxy.prepareRequestForForward(*result, "tcp");
    ASSERT_TRUE(prepare.has_value()) << prepare.error().message;
    EXPECT_EQ(result->maxForwards(), 69);
    EXPECT_EQ(result->viaCount(), 2);
    EXPECT_NE(result->topVia().find("SIP/2.0/TCP proxy.local:5060"), std::string::npos);
    EXPECT_NE(result->topVia().find(";rport"), std::string::npos);
    EXPECT_NE(result->viaBranch().find("z9hG4bK"), std::string::npos);
}

TEST_F(ProxyCoreTest, PrepareRequestForForwardSetsDefaultMaxForwardsWhenMissing) {
    auto result = SipMessage::parse(
        "REGISTER sip:ims.example.com SIP/2.0\r\n"
        "Via: SIP/2.0/UDP 10.0.0.1:5060;branch=z9hG4bK776asdhds\r\n"
        "To: <sip:user@ims.example.com>\r\n"
        "From: <sip:user@ims.example.com>;tag=1928301774\r\n"
        "Call-ID: a84b4c76e66710@10.0.0.1\r\n"
        "CSeq: 1 REGISTER\r\n"
        "Contact: <sip:user@10.0.0.1:5060>\r\n"
        "Expires: 3600\r\n"
        "Content-Length: 0\r\n"
        "\r\n");
    ASSERT_TRUE(result.has_value()) << result.error().message;
    EXPECT_EQ(result->maxForwards(), -1);

    ProxyCore proxy("proxy.local", 5060);

    auto prepare = proxy.prepareRequestForForward(*result);
    ASSERT_TRUE(prepare.has_value()) << prepare.error().message;
    EXPECT_EQ(result->maxForwards(), 69);
    EXPECT_NE(result->topVia().find("SIP/2.0/UDP proxy.local:5060"), std::string::npos);
}

TEST_F(ProxyCoreTest, PrepareRequestForForwardRejectsZeroMaxForwards) {
    auto result = SipMessage::parse(kRegisterMsg);
    ASSERT_TRUE(result.has_value()) << result.error().message;
    result->setMaxForwards(0);

    ProxyCore proxy("proxy.local", 5060);

    auto prepare = proxy.prepareRequestForForward(*result);
    ASSERT_FALSE(prepare.has_value());
    EXPECT_EQ(prepare.error().code, ims::ErrorCode::kSipTransactionFailed);
    EXPECT_EQ(result->maxForwards(), 0);
    EXPECT_EQ(result->viaCount(), 1);
}

TEST_F(ProxyCoreTest, ForwardRequestPreparesAndSendsToDestination) {
    auto result = SipMessage::parse(kRegisterMsg);
    ASSERT_TRUE(result.has_value()) << result.error().message;

    ProxyCore proxy("proxy.local", 5060);
    auto transport = std::make_shared<CapturingTransport>();
    Endpoint dest{.address = "198.51.100.20", .port = 5090, .transport = "tcp"};

    auto forward = proxy.forwardRequest(*result, dest, transport);
    ASSERT_TRUE(forward.has_value()) << forward.error().message;
    ASSERT_EQ(transport->send_count, 1);
    ASSERT_EQ(transport->destinations.size(), 1u);
    EXPECT_EQ(transport->destinations[0].address, "198.51.100.20");
    EXPECT_EQ(transport->destinations[0].port, 5090);
    EXPECT_EQ(transport->destinations[0].transport, "tcp");
    EXPECT_EQ(result->maxForwards(), 69);
    EXPECT_NE(result->topVia().find("SIP/2.0/TCP proxy.local:5060"), std::string::npos);
}

TEST_F(ProxyCoreTest, IsLoopDetectedReturnsTrueWhenViaMatchesLocalAddress) {
    auto result = SipMessage::parse(kRegisterMsg);
    ASSERT_TRUE(result.has_value()) << result.error().message;
    result->addVia("SIP/2.0/UDP proxy.local:5060;branch=z9hG4bK-loop");

    ProxyCore proxy("proxy.local", 5060);

    EXPECT_TRUE(proxy.isLoopDetected(*result));
}

TEST_F(ProxyCoreTest, IsLoopDetectedReturnsFalseWhenViaDoesNotMatchLocalAddress) {
    auto result = SipMessage::parse(kRegisterMsg);
    ASSERT_TRUE(result.has_value()) << result.error().message;
    result->addVia("SIP/2.0/UDP proxy.local:5070;branch=z9hG4bK-other-port");

    ProxyCore proxy("proxy.local", 5060);

    EXPECT_FALSE(proxy.isLoopDetected(*result));
}

TEST_F(ProxyCoreTest, IsLoopDetectedTreatsMissingViaPortAs5060) {
    auto result = SipMessage::parse(kRegisterMsg);
    ASSERT_TRUE(result.has_value()) << result.error().message;
    result->addVia("SIP/2.0/UDP proxy.local;branch=z9hG4bK-default-port");

    ProxyCore proxy("proxy.local", 5060);

    EXPECT_TRUE(proxy.isLoopDetected(*result));
}

} // namespace
