#include <any>
#include <sstream>

#define private public
#include "p-cscf/pcscf_service.hpp"
#undef private

#include "mocks/mock_pcf_client.hpp"
#include "mocks/mock_rtpengine_client.hpp"
#include "mocks/mock_transport.hpp"
#include "sip/message.hpp"
#include "sip/transaction.hpp"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

using ::testing::_;
using ::testing::AnyNumber;
using ::testing::Invoke;
using ::testing::StrictMock;
using ::testing::Truly;

auto makeInviteResponse(int status, std::string_view reason, std::string_view call_id,
                        std::string_view from_tag, std::string_view to_tag,
                        const std::optional<std::string>& sdp) -> ims::sip::SipMessage {
    auto parsed = ims::sip::SipMessage::parse(
        std::string("SIP/2.0 ") + std::to_string(status) + " " + std::string(reason) + "\r\n"
        "Via: SIP/2.0/UDP 127.0.0.1:5060;branch=z9hG4bK-test\r\n"
        "To: <sip:callee@ims.local>;tag=" + std::string(to_tag) + "\r\n"
        "From: <sip:caller@ims.local>;tag=" + std::string(from_tag) + "\r\n"
        "Call-ID: " + std::string(call_id) + "\r\n"
        "CSeq: 1 INVITE\r\n"
        "Content-Length: 0\r\n"
        "\r\n");
    EXPECT_TRUE(parsed.has_value()) << parsed.error().message;
    if (!parsed) {
        throw std::runtime_error("Failed to parse SIP response in test fixture");
    }

    if (sdp) {
        parsed->setBody(*sdp, "application/sdp");
    }
    return std::move(*parsed);
}

class PcscfServiceTest : public ::testing::Test {
protected:
    void SetUp() override {
        ims::PcscfConfig cfg;
        cfg.listen_addr = "127.0.0.1";
        cfg.listen_port = 0;
        cfg.core_entry = {
            .address = "127.0.0.1",
            .port = 5062,
            .transport = "udp",
        };

        pcf_ = std::make_shared<StrictMock<ims::test::MockPcfClient>>();
        rtpengine_ = std::make_shared<StrictMock<ims::test::MockRtpengineClient>>();
        service_ = std::make_unique<ims::pcscf::PcscfService>(
            cfg, io_, pcf_, rtpengine_, cfg.core_entry.address, cfg.core_entry.port);
    }

    boost::asio::io_context io_;
    std::shared_ptr<StrictMock<ims::test::MockPcfClient>> pcf_;
    std::shared_ptr<StrictMock<ims::test::MockRtpengineClient>> rtpengine_;
    std::unique_ptr<ims::pcscf::PcscfService> service_;
};

TEST_F(PcscfServiceTest, Invite183WithSdpTriggersRtpengineAnswerAndRewritesSdp) {
    static const std::string kCallId = "call-183";
    static const std::string kFromTag = "from-a";
    static const std::string kToTag = "to-b";
    static const std::string kOriginalSdp = "v=0\r\no=- 1 1 IN IP4 10.0.0.1\r\ns=-\r\n";
    static const std::string kRewrittenSdp = "v=0\r\no=- 2 2 IN IP4 127.0.0.1\r\ns=-\r\n";

    service_->media_sessions_.createSession(kCallId, kFromTag);
    auto response = makeInviteResponse(183, "Session Progress", kCallId, kFromTag, kToTag, kOriginalSdp);

    EXPECT_CALL(
        *rtpengine_,
        answer(Truly([](const ims::media::MediaSession& session) {
                   return session.call_id == "call-183" &&
                          session.from_tag == "from-a" &&
                          session.to_tag == "to-b";
               }),
               kOriginalSdp, _))
        .WillOnce([](const ims::media::MediaSession&, const std::string&, const ims::media::RtpengineFlags&) {
            return ims::media::RtpengineResult{.sdp = "v=0\r\no=- 2 2 IN IP4 127.0.0.1\r\ns=-\r\n"};
        });

    service_->onInviteResponse(response);

    auto body = response.body();
    ASSERT_TRUE(body.has_value());
    EXPECT_EQ(*body, kRewrittenSdp);

    auto state = service_->media_sessions_.getSession(kCallId);
    ASSERT_TRUE(state.has_value());
    EXPECT_EQ(state->session.to_tag, kToTag);
    EXPECT_EQ(state->callee_sdp, kOriginalSdp);
}

TEST_F(PcscfServiceTest, Invite486DoesNotCallRtpengineAnswer) {
    static const std::string kCallId = "call-486";
    static const std::string kFromTag = "from-a";
    static const std::string kOriginalSdp = "v=0\r\ns=-\r\n";

    service_->media_sessions_.createSession(kCallId, kFromTag);
    auto response = makeInviteResponse(486, "Busy Here", kCallId, kFromTag, "to-b", kOriginalSdp);

    EXPECT_CALL(*rtpengine_, answer(_, _, _)).Times(0);

    service_->onInviteResponse(response);

    auto body = response.body();
    ASSERT_TRUE(body.has_value());
    EXPECT_EQ(*body, kOriginalSdp);
}

TEST_F(PcscfServiceTest, Invite200WithoutSdpDoesNotCallRtpengineAnswer) {
    static const std::string kCallId = "call-200-no-sdp";
    static const std::string kFromTag = "from-a";

    service_->media_sessions_.createSession(kCallId, kFromTag);
    auto response = makeInviteResponse(200, "OK", kCallId, kFromTag, "to-b", std::nullopt);

    EXPECT_CALL(*rtpengine_, answer(_, _, _)).Times(0);

    service_->onInviteResponse(response);

    EXPECT_FALSE(response.body().has_value());
}

TEST_F(PcscfServiceTest, Invite183WithoutTrackedSessionDoesNotCallRtpengineAnswer) {
    auto response = makeInviteResponse(
        183, "Session Progress", "missing-call", "from-a", "to-b", std::string("v=0\r\ns=-\r\n"));

    EXPECT_CALL(*rtpengine_, answer(_, _, _)).Times(0);

    service_->onInviteResponse(response);
}

TEST_F(PcscfServiceTest, ExtractTopologyTokenFromTopRoute) {
    auto parsed = ims::sip::SipMessage::parse(
        "BYE sip:alice@ims.local SIP/2.0\r\n"
        "Via: SIP/2.0/UDP 10.0.0.9:5099;branch=z9hG4bKabc;rport\r\n"
        "Route: <sip:10.0.0.1:5060;lr;th=th001122>\r\n"
        "From: <sip:bob@ims.local>;tag=f1\r\n"
        "To: <sip:alice@ims.local>;tag=t1\r\n"
        "Call-ID: token-test\r\n"
        "CSeq: 2 BYE\r\n"
        "Content-Length: 0\r\n\r\n");
    ASSERT_TRUE(parsed.has_value()) << parsed.error().message;

    auto token = service_->extractTopologyToken(*parsed);
    ASSERT_TRUE(token.has_value());
    EXPECT_EQ(*token, "th001122");
}

TEST_F(PcscfServiceTest, ResolveCoreDestinationUsesStoredTopologyToken) {
    ims::sip::Endpoint expected{
        .address = "10.10.10.10",
        .port = 6060,
        .transport = "udp",
    };
    service_->rememberTopologyRoute("thsaved", expected);

    auto parsed = ims::sip::SipMessage::parse(
        "BYE sip:alice@ims.local SIP/2.0\r\n"
        "Via: SIP/2.0/UDP 10.0.0.9:5099;branch=z9hG4bKabc;rport\r\n"
        "Route: <sip:127.0.0.1:5060;lr;th=thsaved>\r\n"
        "From: <sip:bob@ims.local>;tag=f1\r\n"
        "To: <sip:alice@ims.local>;tag=t1\r\n"
        "Call-ID: token-test2\r\n"
        "CSeq: 3 BYE\r\n"
        "Content-Length: 0\r\n\r\n");
    ASSERT_TRUE(parsed.has_value()) << parsed.error().message;

    auto resolved = service_->resolveCoreDestination(*parsed);
    EXPECT_EQ(resolved.address, expected.address);
    EXPECT_EQ(resolved.port, expected.port);
    EXPECT_EQ(resolved.transport, expected.transport);
}

TEST_F(PcscfServiceTest, ResolveCoreDestinationFallsBackToConfiguredCoreEntryWhenNoToken) {
    auto parsed = ims::sip::SipMessage::parse(
        "BYE sip:alice@ims.local SIP/2.0\r\n"
        "Via: SIP/2.0/UDP 10.0.0.9:5099;branch=z9hG4bKabc;rport\r\n"
        "From: <sip:bob@ims.local>;tag=f1\r\n"
        "To: <sip:alice@ims.local>;tag=t1\r\n"
        "Call-ID: token-fallback\r\n"
        "CSeq: 3 BYE\r\n"
        "Content-Length: 0\r\n\r\n");
    ASSERT_TRUE(parsed.has_value()) << parsed.error().message;

    auto resolved = service_->resolveCoreDestination(*parsed);
    EXPECT_EQ(resolved.address, "127.0.0.1");
    EXPECT_EQ(resolved.port, 5062);
    EXPECT_EQ(resolved.transport, "udp");
}

TEST_F(PcscfServiceTest, AddTopologyRecordRouteCarriesToken) {
    auto parsed = ims::sip::SipMessage::parse(
        "INVITE sip:alice@ims.local SIP/2.0\r\n"
        "Via: SIP/2.0/UDP 10.0.0.9:5099;branch=z9hG4bKabc;rport\r\n"
        "From: <sip:bob@ims.local>;tag=f1\r\n"
        "To: <sip:alice@ims.local>\r\n"
        "Call-ID: rr-token-test\r\n"
        "CSeq: 1 INVITE\r\n"
        "Content-Length: 0\r\n\r\n");
    ASSERT_TRUE(parsed.has_value()) << parsed.error().message;

    service_->addTopologyRecordRoute(*parsed, "thabc123");

    auto rr = parsed->getHeaders("Record-Route");
    ASSERT_EQ(rr.size(), 1u);
    EXPECT_NE(rr[0].find(";th=thabc123"), std::string::npos);
}

TEST_F(PcscfServiceTest, CoreFacingRequestAcceptsConfiguredCoreEntrySource) {
    auto req = ims::sip::SipMessage::parse(
        "INVITE sip:alice@ims.local SIP/2.0\r\n"
        "Via: SIP/2.0/UDP 127.0.0.1:5062;branch=z9hG4bKcore;rport\r\n"
        "From: <sip:bob@ims.local>;tag=f1\r\n"
        "To: <sip:alice@ims.local>\r\n"
        "Call-ID: core-facing-test\r\n"
        "CSeq: 1 INVITE\r\n"
        "Content-Length: 0\r\n\r\n");
    ASSERT_TRUE(req.has_value()) << req.error().message;

    auto transport = std::make_shared<StrictMock<ims::test::MockTransport>>();
    auto txn = std::make_shared<ims::sip::ServerTransaction>(
        std::move(*req),
        transport,
        ims::sip::Endpoint{.address = "127.0.0.1", .port = 5062, .transport = "udp"},
        io_);

    EXPECT_TRUE(service_->isCoreFacingRequest(*txn));
}

TEST_F(PcscfServiceTest, CoreFacingRequestAcceptsExplicitCorePeer) {
    ims::PcscfConfig cfg;
    cfg.listen_addr = "127.0.0.1";
    cfg.listen_port = 0;
    cfg.core_entry = {
        .address = "127.0.0.1",
        .port = 5062,
        .transport = "udp",
    };
    cfg.core_peers = {
        ims::SipEndpointConfig{
            .address = "10.0.0.2",
            .port = 5061,
            .transport = "udp",
        },
    };

    auto peer_service = std::make_unique<ims::pcscf::PcscfService>(
        cfg, io_, pcf_, rtpengine_, cfg.core_entry.address, cfg.core_entry.port);

    auto req = ims::sip::SipMessage::parse(
        "INVITE sip:alice@ims.local SIP/2.0\r\n"
        "Via: SIP/2.0/UDP 10.0.0.2:5061;branch=z9hG4bKcore-peer;rport\r\n"
        "From: <sip:bob@ims.local>;tag=f1\r\n"
        "To: <sip:alice@ims.local>\r\n"
        "Call-ID: core-peer-test\r\n"
        "CSeq: 1 INVITE\r\n"
        "Content-Length: 0\r\n\r\n");
    ASSERT_TRUE(req.has_value()) << req.error().message;

    auto transport = std::make_shared<StrictMock<ims::test::MockTransport>>();
    auto txn = std::make_shared<ims::sip::ServerTransaction>(
        std::move(*req),
        transport,
        ims::sip::Endpoint{.address = "10.0.0.2", .port = 5061, .transport = "udp"},
        io_);

    EXPECT_TRUE(peer_service->isCoreFacingRequest(*txn));
}

TEST_F(PcscfServiceTest, CoreFacingRequestRejectsUnexpectedCorePeerPort) {
    ims::PcscfConfig cfg;
    cfg.listen_addr = "127.0.0.1";
    cfg.listen_port = 0;
    cfg.core_entry = {
        .address = "127.0.0.1",
        .port = 5062,
        .transport = "udp",
    };
    cfg.core_peers = {
        ims::SipEndpointConfig{
            .address = "10.0.0.2",
            .port = 5061,
            .transport = "udp",
        },
    };

    auto peer_service = std::make_unique<ims::pcscf::PcscfService>(
        cfg, io_, pcf_, rtpengine_, cfg.core_entry.address, cfg.core_entry.port);

    auto req = ims::sip::SipMessage::parse(
        "INVITE sip:alice@ims.local SIP/2.0\r\n"
        "Via: SIP/2.0/UDP 10.0.0.2:9999;branch=z9hG4bKcore-peer-bad-port;rport\r\n"
        "From: <sip:bob@ims.local>;tag=f1\r\n"
        "To: <sip:alice@ims.local>\r\n"
        "Call-ID: core-peer-bad-port\r\n"
        "CSeq: 1 INVITE\r\n"
        "Content-Length: 0\r\n\r\n");
    ASSERT_TRUE(req.has_value()) << req.error().message;

    auto transport = std::make_shared<StrictMock<ims::test::MockTransport>>();
    auto txn = std::make_shared<ims::sip::ServerTransaction>(
        std::move(*req),
        transport,
        ims::sip::Endpoint{.address = "10.0.0.2", .port = 9999, .transport = "udp"},
        io_);

    EXPECT_FALSE(peer_service->isCoreFacingRequest(*txn));
}

TEST_F(PcscfServiceTest, ResolveUeDestinationFallsBackToRequestUriWhenViaMissing) {
    auto req = ims::sip::SipMessage::parse(
        "INVITE sip:alice@10.1.20.6:39545 SIP/2.0\r\n"
        "From: <sip:bob@ims.local>;tag=f1\r\n"
        "To: <sip:alice@ims.local>\r\n"
        "Call-ID: ue-dest-ruri-fallback\r\n"
        "CSeq: 1 INVITE\r\n"
        "Content-Length: 0\r\n\r\n");
    ASSERT_TRUE(req.has_value()) << req.error().message;

    auto ue = service_->resolveUeDestination(*req);
    ASSERT_TRUE(ue.has_value());
    EXPECT_EQ(ue->address, "10.1.20.6");
    EXPECT_EQ(ue->port, 39545);
    EXPECT_EQ(ue->transport, "udp");
}

TEST_F(PcscfServiceTest, ResolveUeDestinationFallsBackToContactWhenViaAndRequestUriMissingEndpoint) {
    auto req = ims::sip::SipMessage::parse(
        "INVITE sip:alice@ims.local SIP/2.0\r\n"
        "From: <sip:bob@ims.local>;tag=f1\r\n"
        "To: <sip:alice@ims.local>\r\n"
        "Contact: <sip:alice@10.1.20.6:39545>\r\n"
        "Call-ID: ue-dest-contact-fallback\r\n"
        "CSeq: 1 INVITE\r\n"
        "Content-Length: 0\r\n\r\n");
    ASSERT_TRUE(req.has_value()) << req.error().message;

    auto ue = service_->resolveUeDestination(*req);
    ASSERT_TRUE(ue.has_value());
    EXPECT_EQ(ue->address, "10.1.20.6");
    EXPECT_EQ(ue->port, 39545);
    EXPECT_EQ(ue->transport, "udp");
}

TEST_F(PcscfServiceTest, ResolveUeDestinationPrefersRequestUriOverContactAndVia) {
    auto req = ims::sip::SipMessage::parse(
        "INVITE sip:alice@10.1.20.8:40600 SIP/2.0\r\n"
        "Via: SIP/2.0/UDP 10.1.20.7:5099;branch=z9hG4bKcore-to-ue;rport\r\n"
        "From: <sip:bob@ims.local>;tag=f1\r\n"
        "To: <sip:alice@ims.local>\r\n"
        "Contact: <sip:alice@10.1.20.6:39545>\r\n"
        "Call-ID: ue-dest-ruri-priority\r\n"
        "CSeq: 1 INVITE\r\n"
        "Content-Length: 0\r\n\r\n");
    ASSERT_TRUE(req.has_value()) << req.error().message;

    auto ue = service_->resolveUeDestination(*req);
    ASSERT_TRUE(ue.has_value());
    EXPECT_EQ(ue->address, "10.1.20.8");
    EXPECT_EQ(ue->port, 40600);
}

TEST_F(PcscfServiceTest, SanitizeForUeEgressPreservesRouteButCollapsesViaChain) {
    auto req = ims::sip::SipMessage::parse(
        "ACK sip:alice@10.1.20.6:39545 SIP/2.0\r\n"
        "Via: SIP/2.0/UDP 127.0.0.1:5060;branch=z9hG4bKtop\r\n"
        "Via: SIP/2.0/UDP 192.168.31.40:5062;branch=z9hG4bKmid\r\n"
        "Via: SIP/2.0/UDP 192.168.31.48:61836;rport;branch=z9hG4bKbottom\r\n"
        "Route: <sip:192.168.31.40:5060;lr;th=th123>\r\n"
        "From: <sip:bob@ims.local>;tag=caller\r\n"
        "To: <sip:alice@ims.local>;tag=callee\r\n"
        "Call-ID: ack-ue-egress\r\n"
        "CSeq: 1 ACK\r\n"
        "Content-Length: 0\r\n\r\n");
    ASSERT_TRUE(req.has_value()) << req.error().message;

    service_->sanitizeForUeEgress(*req);

    EXPECT_EQ(req->viaCount(), 1);
    EXPECT_NE(req->topVia().find("192.168.31.48:61836"), std::string::npos);
    auto routes = req->routes();
    ASSERT_EQ(routes.size(), 1u);
    EXPECT_NE(routes.front().find("th=th123"), std::string::npos);
}

} // namespace
