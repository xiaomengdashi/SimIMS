#include "sip/transport.hpp"
#define private public
#include "s-cscf/session_router.hpp"
#include "sip/stack.hpp"
#undef private

#include <boost/asio/io_context.hpp>
#include <gtest/gtest.h>

namespace {

class CapturingTransport final : public ims::sip::ITransport {
public:
    auto send(const ims::sip::SipMessage& msg, const ims::sip::Endpoint& dest) -> ims::VoidResult override {
        auto clone = msg.clone();
        if (!clone) {
            return std::unexpected(clone.error());
        }
        sent_messages.push_back(std::move(*clone));
        sent_destinations.push_back(dest);
        return {};
    }

    void setMessageCallback(MessageCallback cb) override {
        callback = std::move(cb);
    }

    auto start() -> ims::VoidResult override { return {}; }
    void stop() override {}

    auto localEndpoint() const -> ims::sip::Endpoint override {
        return ims::sip::Endpoint{.address = "127.0.0.1", .port = 5062, .transport = "udp"};
    }

    MessageCallback callback;
    std::vector<ims::sip::SipMessage> sent_messages;
    std::vector<ims::sip::Endpoint> sent_destinations;
};

class FakeSipStack final : public ims::sip::SipStack {
public:
    FakeSipStack(boost::asio::io_context& io,
                 const std::shared_ptr<ims::sip::ITransport>& transport)
        : ims::sip::SipStack(io, "127.0.0.1", 0) {
        transport_ = transport;
        txn_layer_ = std::make_unique<ims::sip::TransactionLayer>(io, transport_);
    }
};

class NoopRegistrationStore final : public ims::registration::IRegistrationStore {
public:
    auto store(const ims::registration::RegistrationBinding&) -> ims::VoidResult override { return {}; }
    auto lookup(std::string_view) -> ims::Result<ims::registration::RegistrationBinding> override {
        return std::unexpected(ims::ErrorInfo{ims::ErrorCode::kRegistrationNotFound, "not found"});
    }
    auto upsertContact(std::string_view,
                       const ims::registration::ContactBindingSelector&,
                       const ims::registration::ContactBinding&,
                       std::string_view,
                       std::string_view,
                       ims::registration::RegistrationBinding::State,
                       bool,
                       bool) -> ims::Result<bool> override { return false; }
    auto removeContact(std::string_view,
                       const ims::registration::ContactBindingSelector&) -> ims::Result<bool> override { return false; }
    auto remove(std::string_view) -> ims::VoidResult override { return {}; }
    auto purgeExpired() -> ims::Result<size_t> override { return size_t{0}; }
    auto isRegistered(std::string_view) -> ims::Result<bool> override { return false; }
};

auto makeRequest(const std::string& method,
                 const std::string& call_id,
                 const std::string& from_tag,
                 const std::string& to_tag) -> ims::sip::SipMessage {
    auto raw = std::format(
        "{} sip:bob@ims.example.com SIP/2.0\r\n"
        "Via: SIP/2.0/UDP 127.0.0.1:5090;branch=z9hG4bK-{}-{}-{}\r\n"
        "From: <sip:alice@ims.example.com>;tag={}\r\n"
        "To: <sip:bob@ims.example.com>;tag={}\r\n"
        "Call-ID: {}\r\n"
        "CSeq: 1 {}\r\n"
        "Contact: <sip:alice@127.0.0.1:5090>\r\n"
        "Content-Length: 0\r\n\r\n",
        method, method, from_tag, to_tag, from_tag, to_tag, call_id, method);
    auto parsed = ims::sip::SipMessage::parse(raw);
    EXPECT_TRUE(parsed.has_value()) << parsed.error().message;
    return std::move(*parsed);
}

auto makeTxn(const ims::sip::SipMessage& request,
             const std::shared_ptr<CapturingTransport>& transport,
             boost::asio::io_context& io) -> std::shared_ptr<ims::sip::ServerTransaction> {
    auto clone = request.clone();
    EXPECT_TRUE(clone.has_value()) << clone.error().message;
    ims::sip::Endpoint source{.address = "127.0.0.1", .port = 5090, .transport = "udp"};
    return std::make_shared<ims::sip::ServerTransaction>(std::move(*clone), transport, source, io);
}

auto makeSession(const std::string& call_id,
                 const std::string& caller_tag,
                 const std::string& callee_tag,
                 uint16_t callee_port) -> ims::scscf::SessionRouter::SessionInfo {
    return ims::scscf::SessionRouter::SessionInfo{
        .call_id = call_id,
        .caller_tag = caller_tag,
        .callee_tag = callee_tag,
        .caller_impu = "sip:alice@ims.example.com",
        .callee_impu = "sip:bob@ims.example.com",
        .caller_endpoint = ims::sip::Endpoint{.address = "127.0.0.1", .port = 5090, .transport = "udp"},
        .callee_endpoint = ims::sip::Endpoint{.address = "127.0.0.1", .port = callee_port, .transport = "udp"},
        .callee_invite_branch = "z9hG4bK-invite",
    };
}

class SessionRouterTest : public ::testing::Test {
protected:
    void SetUp() override {
        stack = std::make_unique<FakeSipStack>(io, transport);
        router = std::make_unique<ims::scscf::SessionRouter>(store, *stack);
    }

    auto key(const std::string& call_id,
             const std::string& caller_tag,
             const std::string& callee_tag) -> ims::scscf::SessionRouter::DialogKey {
        return ims::scscf::SessionRouter::DialogKey{
            .call_id = call_id,
            .caller_tag = caller_tag,
            .callee_tag = callee_tag,
        };
    }

    boost::asio::io_context io;
    std::shared_ptr<CapturingTransport> transport = std::make_shared<CapturingTransport>();
    std::shared_ptr<NoopRegistrationStore> store = std::make_shared<NoopRegistrationStore>();
    std::unique_ptr<FakeSipStack> stack;
    std::unique_ptr<ims::scscf::SessionRouter> router;
};

TEST_F(SessionRouterTest, SameCallIdDifferentToTagsRouteByeToMatchingDialog) {
    router->sessions_.emplace(key("call-1", "caller", "callee-a"),
                              makeSession("call-1", "caller", "callee-a", 5070));
    router->sessions_.emplace(key("call-1", "caller", "callee-b"),
                              makeSession("call-1", "caller", "callee-b", 5080));

    auto bye = makeRequest("BYE", "call-1", "caller", "callee-b");
    router->handleBye(bye, makeTxn(bye, transport, io));

    ASSERT_EQ(transport->sent_destinations.size(), 1u);
    EXPECT_EQ(transport->sent_destinations[0].port, 5080);
}

TEST_F(SessionRouterTest, ReverseDirectionByeRoutesToCallerEndpoint) {
    router->sessions_.emplace(key("call-1", "caller", "callee-a"),
                              makeSession("call-1", "caller", "callee-a", 5070));

    auto bye = makeRequest("BYE", "call-1", "callee-a", "caller");
    router->handleBye(bye, makeTxn(bye, transport, io));

    ASSERT_EQ(transport->sent_destinations.size(), 1u);
    EXPECT_EQ(transport->sent_destinations[0].port, 5090);
}

TEST_F(SessionRouterTest, UnknownDialogByeReturns481) {
    router->sessions_.emplace(key("call-1", "caller", "callee-a"),
                              makeSession("call-1", "caller", "callee-a", 5070));

    auto bye = makeRequest("BYE", "call-1", "caller", "callee-missing");
    router->handleBye(bye, makeTxn(bye, transport, io));

    ASSERT_EQ(transport->sent_messages.size(), 1u);
    EXPECT_EQ(transport->sent_messages[0].statusCode(), 481);
}

TEST_F(SessionRouterTest, InviteResponseCreatesSeparateDialogWithoutReplacingExistingOne) {
    router->sessions_.emplace(key("call-1", "caller", "callee-a"),
                              makeSession("call-1", "caller", "callee-a", 5070));
    router->sessions_.emplace(key("call-1", "caller", ""),
                              makeSession("call-1", "caller", "", 5080));

    router->recordInviteResponseDialog("call-1", "caller", "callee-b");

    ASSERT_TRUE(router->sessions_.contains(key("call-1", "caller", "callee-a")));
    ASSERT_TRUE(router->sessions_.contains(key("call-1", "caller", "callee-b")));
    EXPECT_EQ(router->sessions_.at(key("call-1", "caller", "callee-a")).callee_endpoint.port, 5070);
    EXPECT_EQ(router->sessions_.at(key("call-1", "caller", "callee-b")).callee_endpoint.port, 5080);
}

} // namespace
