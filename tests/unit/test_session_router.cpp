#include "sip/transport.hpp"
#define private public
#include "s-cscf/session_router.hpp"
#include "sip/stack.hpp"
#undef private

#include "sms/hex_utils.hpp"
#include "sms/samples.hpp"

#include <boost/asio/io_context.hpp>
#include <gtest/gtest.h>

#include <unordered_set>

namespace {

class CapturingTransport final : public ims::sip::ITransport {
public:
    auto send(const ims::sip::SipMessage& msg, const ims::sip::Endpoint& dest) -> ims::VoidResult override {
        if (fail_send_count > 0) {
            --fail_send_count;
            return std::unexpected(ims::ErrorInfo{
                ims::ErrorCode::kSipTransportError,
                "synthetic send failure",
            });
        }
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
    int fail_send_count = 0;
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
        if (binding) {
            return *binding;
        }
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
    auto upsertContacts(const ims::registration::ContactBatchUpsert& batch) -> ims::Result<size_t> override {
        return batch.impus.size();
    }
    auto removeContacts(const ims::registration::ContactBatchRemove&) -> ims::Result<size_t> override { return size_t{0}; }
    auto removeBindings(const std::unordered_set<std::string>&) -> ims::Result<size_t> override { return size_t{0}; }
    auto remove(std::string_view) -> ims::VoidResult override { return {}; }
    auto purgeExpired() -> ims::Result<size_t> override { return size_t{0}; }
    auto isRegistered(std::string_view impu) -> ims::Result<bool> override {
        return registered_impus.contains(std::string(impu));
    }

    std::optional<ims::registration::RegistrationBinding> binding;
    std::unordered_set<std::string> registered_impus;
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

auto makeInviteResponse(int status_code,
                        const std::string& reason,
                        const std::string& call_id,
                        const std::string& from_tag,
                        const std::string& to_tag) -> ims::sip::SipMessage {
    auto raw = std::format(
        "SIP/2.0 {} {}\r\n"
        "Via: SIP/2.0/UDP 127.0.0.1:5090;branch=z9hG4bK-invite\r\n"
        "From: <sip:alice@ims.example.com>;tag={}\r\n"
        "To: <sip:bob@ims.example.com>;tag={}\r\n"
        "Call-ID: {}\r\n"
        "CSeq: 1 INVITE\r\n"
        "Content-Length: 0\r\n\r\n",
        status_code, reason, from_tag, to_tag, call_id);
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

    void seedRegisteredMessageUsers() {
        store->registered_impus.insert("sip:alice@ims.example.com");
        store->binding = ims::registration::RegistrationBinding{
            .impu = "sip:bob@ims.example.com",
            .impi = "bob-private@ims.example.com",
            .scscf_uri = "sip:scscf.ims.example.com",
            .contacts = {
                ims::registration::ContactBinding{
                    .contact_uri = "<sip:bob@10.0.0.23:5092;transport=udp>",
                    .path = "<sip:pcscf.ims.example.com:5060;lr>",
                    .expires = std::chrono::steady_clock::now() + std::chrono::minutes{5},
                },
            },
            .state = ims::registration::RegistrationBinding::State::kRegistered,
        };
    }

    auto makeMessage(const std::string& call_id = "sms-message-call",
                     std::string_view extra_headers = "",
                     std::string_view body_hex = ims::sms::kSampleRpDataHex) -> ims::sip::SipMessage {
        auto body_bytes = ims::sms::decode_hex(body_hex);
        EXPECT_TRUE(body_bytes.has_value()) << body_bytes.error().message;
        const std::string body(body_bytes->begin(), body_bytes->end());

        auto raw = std::format(
            "MESSAGE sip:bob@ims.example.com SIP/2.0\r\n"
            "Via: SIP/2.0/UDP 127.0.0.1:5090;branch=z9hG4bK-{}\r\n"
            "From: <sip:alice@ims.example.com>;tag=alice-msg\r\n"
            "To: <sip:bob@ims.example.com>\r\n"
            "Call-ID: {}\r\n"
            "CSeq: 1 MESSAGE\r\n"
            "{}"
            "Content-Type: application/vnd.3gpp.sms\r\n"
            "Content-Length: {}\r\n\r\n",
            call_id,
            call_id,
            extra_headers,
            body.size());
        raw.append(body);
        auto parsed = ims::sip::SipMessage::parse(raw);
        EXPECT_TRUE(parsed.has_value()) << parsed.error().message;
        return std::move(*parsed);
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

TEST_F(SessionRouterTest, CancelKeepsInitialSessionUntilAckRoutes) {
    router->sessions_.emplace(key("call-cancel", "caller", ""),
                              makeSession("call-cancel", "caller", "", 5070));

    auto cancel = makeRequest("CANCEL", "call-cancel", "caller", "");
    router->handleCancel(cancel, makeTxn(cancel, transport, io));

    ASSERT_TRUE(router->sessions_.contains(key("call-cancel", "caller", "")));
    EXPECT_TRUE(router->sessions_.at(key("call-cancel", "caller", "")).cancel_seen);
    ASSERT_EQ(transport->sent_destinations.size(), 2u);
    EXPECT_EQ(transport->sent_messages[0].statusCode(), 200);
    EXPECT_EQ(transport->sent_destinations[1].port, 5070);

    auto ack = makeRequest("ACK", "call-cancel", "caller", "");
    router->handleAck(ack, makeTxn(ack, transport, io));

    ASSERT_EQ(transport->sent_destinations.size(), 3u);
    EXPECT_EQ(transport->sent_destinations[2].port, 5070);
    EXPECT_FALSE(router->sessions_.contains(key("call-cancel", "caller", "")));
}

TEST_F(SessionRouterTest, CancelAckWithToTagClearsInitialAndDialogSessions) {
    auto initial = makeSession("call-cancel", "caller", "", 5070);
    initial.cancel_seen = true;
    auto dialog = makeSession("call-cancel", "caller", "callee", 5070);
    dialog.cancel_seen = true;
    router->sessions_.emplace(key("call-cancel", "caller", ""), initial);
    router->sessions_.emplace(key("call-cancel", "caller", "callee"), dialog);

    auto ack = makeRequest("ACK", "call-cancel", "caller", "callee");
    router->handleAck(ack, makeTxn(ack, transport, io));

    ASSERT_EQ(transport->sent_destinations.size(), 1u);
    EXPECT_EQ(transport->sent_destinations[0].port, 5070);
    EXPECT_FALSE(router->sessions_.contains(key("call-cancel", "caller", "")));
    EXPECT_FALSE(router->sessions_.contains(key("call-cancel", "caller", "callee")));
}

TEST_F(SessionRouterTest, CancelAckWithMissingDialogStillRoutesAndClearsInitialSession) {
    auto initial = makeSession("call-cancel", "caller", "", 5070);
    initial.cancel_seen = true;
    router->sessions_.emplace(key("call-cancel", "caller", ""), initial);

    auto ack = makeRequest("ACK", "call-cancel", "caller", "callee");
    router->handleAck(ack, makeTxn(ack, transport, io));

    ASSERT_EQ(transport->sent_destinations.size(), 1u);
    EXPECT_EQ(transport->sent_destinations[0].port, 5070);
    EXPECT_FALSE(router->sessions_.contains(key("call-cancel", "caller", "")));
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

TEST_F(SessionRouterTest, InviteFinalFailureRemovesInitialAndEarlySessions) {
    router->sessions_.emplace(key("call-fail", "caller", ""),
                              makeSession("call-fail", "caller", "", 5070));
    router->recordInviteResponseDialog("call-fail", "caller", "early-a");
    router->recordInviteResponseDialog("call-fail", "caller", "early-b");

    auto failure = makeInviteResponse(486, "Busy Here", "call-fail", "caller", "early-a");
    router->handleInviteResponseState("call-fail", "caller", failure);

    EXPECT_FALSE(router->sessions_.contains(key("call-fail", "caller", "")));
    EXPECT_FALSE(router->sessions_.contains(key("call-fail", "caller", "early-a")));
    EXPECT_FALSE(router->sessions_.contains(key("call-fail", "caller", "early-b")));
}

TEST_F(SessionRouterTest, InviteSuccessCreatesDialogAndClearsInitialSession) {
    router->sessions_.emplace(key("call-ok", "caller", ""),
                              makeSession("call-ok", "caller", "", 5070));

    auto success = makeInviteResponse(200, "OK", "call-ok", "caller", "callee");
    router->handleInviteResponseState("call-ok", "caller", success);

    EXPECT_FALSE(router->sessions_.contains(key("call-ok", "caller", "")));
    ASSERT_TRUE(router->sessions_.contains(key("call-ok", "caller", "callee")));
    EXPECT_TRUE(router->sessions_.at(key("call-ok", "caller", "callee")).established);
}

TEST_F(SessionRouterTest, InviteFinalFailureKeepsEstablishedDialog) {
    auto established = makeSession("call-mixed", "caller", "callee", 5070);
    established.established = true;
    router->sessions_.emplace(key("call-mixed", "caller", "callee"), established);
    router->sessions_.emplace(key("call-mixed", "caller", ""),
                              makeSession("call-mixed", "caller", "", 5080));
    router->recordInviteResponseDialog("call-mixed", "caller", "early");

    auto failure = makeInviteResponse(480, "Temporarily Unavailable", "call-mixed", "caller", "early");
    router->handleInviteResponseState("call-mixed", "caller", failure);

    ASSERT_TRUE(router->sessions_.contains(key("call-mixed", "caller", "callee")));
    EXPECT_FALSE(router->sessions_.contains(key("call-mixed", "caller", "")));
    EXPECT_FALSE(router->sessions_.contains(key("call-mixed", "caller", "early")));
}

TEST_F(SessionRouterTest, CancelCanStillUseInitialSessionBeforeFinalFailureCleanup) {
    router->sessions_.emplace(key("call-cancel-before-fail", "caller", ""),
                              makeSession("call-cancel-before-fail", "caller", "", 5070));

    auto cancel = makeRequest("CANCEL", "call-cancel-before-fail", "caller", "");
    router->handleCancel(cancel, makeTxn(cancel, transport, io));

    ASSERT_TRUE(router->sessions_.contains(key("call-cancel-before-fail", "caller", "")));
    EXPECT_TRUE(router->sessions_.at(key("call-cancel-before-fail", "caller", "")).cancel_seen);
    ASSERT_EQ(transport->sent_destinations.size(), 2u);
    EXPECT_EQ(transport->sent_destinations[1].port, 5070);
}

TEST_F(SessionRouterTest, MessageToRegisteredCalleeRoutesToRegisteredContact) {
    seedRegisteredMessageUsers();
    auto message = makeMessage();

    router->handleMessage(message, makeTxn(message, transport, io));

    ASSERT_EQ(transport->sent_destinations.size(), 1u);
    EXPECT_EQ(transport->sent_destinations[0].address, "pcscf.ims.example.com");
    EXPECT_EQ(transport->sent_destinations[0].port, 5060);
    ASSERT_EQ(transport->sent_messages.size(), 1u);
    EXPECT_EQ(transport->sent_messages[0].method(), "MESSAGE");
    EXPECT_EQ(transport->sent_messages[0].requestUri(), "sip:bob@10.0.0.23:5092;transport=udp");
    ASSERT_TRUE(transport->sent_messages[0].body().has_value());
    EXPECT_EQ(*transport->sent_messages[0].body(), message.body().value_or(""));
    EXPECT_EQ(transport->sent_messages[0].contentType().value_or(""), "application/vnd.3gpp.sms");
}

TEST_F(SessionRouterTest, MessageToUnknownCalleeReturns404WhenNoPeerIcscfConfigured) {
    store->registered_impus.insert("sip:alice@ims.example.com");
    auto message = makeMessage("sms-missing-call");
    message.setRequestUri("sip:missing@ims.example.com");

    router->handleMessage(message, makeTxn(message, transport, io));

    ASSERT_EQ(transport->sent_messages.size(), 1u);
    EXPECT_EQ(transport->sent_messages[0].statusCode(), 404);
}

TEST_F(SessionRouterTest, MessageFromUnregisteredSenderReturns403) {
    seedRegisteredMessageUsers();
    store->registered_impus.clear();
    auto message = makeMessage("sms-unregistered-sender");

    router->handleMessage(message, makeTxn(message, transport, io));

    ASSERT_EQ(transport->sent_messages.size(), 1u);
    EXPECT_EQ(transport->sent_messages[0].statusCode(), 403);
    ASSERT_EQ(transport->sent_destinations.size(), 1u);
    EXPECT_EQ(transport->sent_destinations[0].port, 5090);
}

TEST_F(SessionRouterTest, MessageWithZeroMaxForwardsReturns483) {
    seedRegisteredMessageUsers();
    auto message = makeMessage("sms-max-forwards-zero", "Max-Forwards: 0\r\n");

    router->handleMessage(message, makeTxn(message, transport, io));

    ASSERT_EQ(transport->sent_messages.size(), 1u);
    EXPECT_EQ(transport->sent_messages[0].statusCode(), 483);
    ASSERT_EQ(transport->sent_destinations.size(), 1u);
    EXPECT_EQ(transport->sent_destinations[0].port, 5090);
}

TEST_F(SessionRouterTest, MessageSendFailureReturns500Upstream) {
    seedRegisteredMessageUsers();
    auto message = makeMessage("sms-send-failure");
    transport->fail_send_count = 1;

    router->handleMessage(message, makeTxn(message, transport, io));

    ASSERT_EQ(transport->sent_messages.size(), 1u);
    EXPECT_EQ(transport->sent_messages[0].statusCode(), 500);
    ASSERT_EQ(transport->sent_destinations.size(), 1u);
    EXPECT_EQ(transport->sent_destinations[0].port, 5090);
}

TEST_F(SessionRouterTest, MessageDownstreamResponseIsForwardedUpstream) {
    seedRegisteredMessageUsers();
    auto message = makeMessage("sms-response-forwarding");

    router->handleMessage(message, makeTxn(message, transport, io));
    ASSERT_EQ(transport->sent_messages.size(), 1u);

    auto downstream_response = ims::sip::createResponse(transport->sent_messages[0], 200, "OK");
    ASSERT_TRUE(downstream_response.has_value()) << downstream_response.error().message;
    stack->transactionLayer().processMessage(std::move(*downstream_response), transport->sent_destinations[0]);

    ASSERT_EQ(transport->sent_messages.size(), 2u);
    EXPECT_EQ(transport->sent_messages[1].statusCode(), 200);
    ASSERT_EQ(transport->sent_destinations.size(), 2u);
    EXPECT_EQ(transport->sent_destinations[1].port, 5090);
    EXPECT_EQ(transport->sent_messages[1].viaCount(), 1);
    EXPECT_NE(transport->sent_messages[1].topVia().find("127.0.0.1:5090"), std::string::npos);
}

} // namespace
