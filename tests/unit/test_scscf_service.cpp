#include "../mocks/mock_hss_client.hpp"
#include "../mocks/mock_registration_store.hpp"
#define private public
#include "s-cscf/scscf_service.hpp"
#undef private

#include "sip/stack.hpp"
#include "sip/transaction.hpp"
#include "sip/transport.hpp"

#include <chrono>
#include <expected>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <optional>

namespace ims::scscf {

class ScscfServiceTestPeer {
public:
    static void sendInitialNotify(ScscfService& service,
                                  const ims::sip::SipMessage& subscribe,
                                  const std::string& to_tag) {
        service.sendInitialNotify(subscribe, to_tag);
    }

    static void onSubscribe(ScscfService& service,
                            std::shared_ptr<ims::sip::ServerTransaction> txn,
                            ims::sip::SipMessage& request) {
        service.onSubscribe(std::move(txn), request);
    }

    static void onInvite(ScscfService& service,
                         std::shared_ptr<ims::sip::ServerTransaction> txn,
                         ims::sip::SipMessage& request) {
        service.onInvite(std::move(txn), request);
    }

    static void onPrack(ScscfService& service,
                        std::shared_ptr<ims::sip::ServerTransaction> txn,
                        ims::sip::SipMessage& request) {
        service.onPrack(std::move(txn), request);
    }

    static void replaceSipStack(ScscfService& service,
                                std::unique_ptr<ims::sip::SipStack> sip_stack) {
        service.sip_stack_ = std::move(sip_stack);
    }

    static void rewireSessionRouter(ScscfService& service) {
        std::optional<ims::sip::Endpoint> peer_icscf;
        if (service.config_.peer_icscf) {
            peer_icscf = ims::sip::Endpoint{
                .address = service.config_.peer_icscf->address,
                .port = service.config_.peer_icscf->port,
                .transport = service.config_.peer_icscf->transport.empty()
                    ? "udp"
                    : service.config_.peer_icscf->transport,
            };
        }

        service.session_router_ = std::make_unique<SessionRouter>(
            service.store_, *service.sip_stack_, peer_icscf);
    }

    static void scheduleRegistrationCleanup(ScscfService& service) {
        service.scheduleRegistrationCleanup();
    }
};

} // namespace ims::scscf

namespace {

using ::testing::Return;

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

class RecordingRegEventNotifier final : public ims::sip::IRegEventNotifier {
public:
    auto start() -> ims::VoidResult override {
        started = true;
        return {};
    }

    auto sendInitialNotify(const ims::sip::InitialRegNotifyContext& context) -> ims::VoidResult override {
        contexts.push_back(context);
        return {};
    }

    void shutdown() override {
        shutdown_called = true;
    }

    bool started = false;
    bool shutdown_called = false;
    std::vector<ims::sip::InitialRegNotifyContext> contexts;
};

class FakeSipStack final : public ims::sip::SipStack {
public:
    FakeSipStack(boost::asio::io_context& io,
                 const std::string& bind_addr,
                 ims::Port port,
                 std::shared_ptr<ims::sip::ITransport> transport)
        : ims::sip::SipStack(io, bind_addr, port)
        , injected_transport_(std::move(transport)) {
        transport_ = injected_transport_;
        txn_layer_ = std::make_unique<ims::sip::TransactionLayer>(io, injected_transport_);
    }

private:
    std::shared_ptr<ims::sip::ITransport> injected_transport_;
};

class ScscfServiceTest : public ::testing::Test {
protected:
    void SetUp() override {
        config.listen_addr = "127.0.0.1";
        config.listen_port = 0;
        config.domain = "ims.example.com";
        config.exosip.enabled = false;

        auto notifier = std::make_unique<RecordingRegEventNotifier>();
        notifier_ptr = notifier.get();
        service = std::make_unique<ims::scscf::ScscfService>(config, io, hss, store, std::move(notifier));
    }

    void reinitializeService() {
        auto notifier = std::make_unique<RecordingRegEventNotifier>();
        notifier_ptr = notifier.get();
        service = std::make_unique<ims::scscf::ScscfService>(config, io, hss, store, std::move(notifier));
    }

    auto make_client_txn_transport() -> std::shared_ptr<CapturingTransport> {
        auto transport = std::make_shared<CapturingTransport>();
        ims::scscf::ScscfServiceTestPeer::replaceSipStack(
            *service,
            std::make_unique<FakeSipStack>(io, "127.0.0.1", 0, transport));
        ims::scscf::ScscfServiceTestPeer::rewireSessionRouter(*service);
        return transport;
    }

    auto make_subscribe() -> ims::sip::SipMessage {
        auto request = ims::sip::createRequest("SUBSCRIBE", "sip:user@ims.example.com");
        EXPECT_TRUE(request.has_value()) << request.error().message;

        request->setFromHeader("<sip:watcher@ims.example.com>;tag=from-tag");
        request->setToHeader("<sip:user@ims.example.com>");
        request->setCallId("sub-call-id");
        request->setCSeq(41, "SUBSCRIBE");
        request->addVia("SIP/2.0/UDP 127.0.0.1:5090;branch=z9hG4bK-sub-1");
        request->setContact("<sip:watcher@127.0.0.1:5090>");
        request->addHeader("Event", "reg");
        request->addHeader("Expires", "180");
        request->addRecordRoute("<sip:edge1.ims.example.com;lr>");
        request->addRecordRoute("<sip:edge2.ims.example.com;lr>");

        auto raw = request->toString();
        EXPECT_TRUE(raw.has_value()) << raw.error().message;
        auto parsed = ims::sip::SipMessage::parse(*raw);
        EXPECT_TRUE(parsed.has_value()) << parsed.error().message;
        return std::move(*parsed);
    }

    boost::asio::io_context io;
    ims::ScscfConfig config;
    std::shared_ptr<ims::test::MockHssClient> hss = std::make_shared<ims::test::MockHssClient>();
    std::shared_ptr<ims::test::MockRegistrationStore> store = std::make_shared<ims::test::MockRegistrationStore>();
    std::unique_ptr<ims::scscf::ScscfService> service;
    RecordingRegEventNotifier* notifier_ptr = nullptr;
};

TEST_F(ScscfServiceTest, SendInitialNotifyBuildsRegEventContext) {
    auto subscribe = make_subscribe();

    ims::scscf::ScscfServiceTestPeer::sendInitialNotify(*service, subscribe, "generated-to-tag");

    ASSERT_EQ(notifier_ptr->contexts.size(), 1u);
    const auto& context = notifier_ptr->contexts.front();
    EXPECT_EQ(context.request_uri, "sip:watcher@127.0.0.1:5090");
    EXPECT_EQ(context.from_header, "<sip:user@ims.example.com>;tag=generated-to-tag");
    EXPECT_EQ(context.to_header, "<sip:watcher@ims.example.com>;tag=from-tag");
    EXPECT_EQ(context.call_id, "sub-call-id");
    EXPECT_EQ(context.cseq, 42u);
    EXPECT_EQ(context.event, "reg");
    EXPECT_EQ(context.subscription_state, "active;expires=180");
    ASSERT_EQ(context.route_set.size(), 2u);
    EXPECT_NE(context.route_set[0].find("edge2.ims.example.com"), std::string::npos);
    EXPECT_NE(context.route_set[1].find("edge1.ims.example.com"), std::string::npos);
    EXPECT_EQ(context.contact, "<sip:ims.example.com:5072>");
    EXPECT_EQ(context.content_type, "application/reginfo+xml");
    EXPECT_NE(context.body.find("aor=\"sip:user@ims.example.com\""), std::string::npos);
}

TEST_F(ScscfServiceTest, OnSubscribeSends200OkAndTriggersInitialNotify) {
    auto subscribe = make_subscribe();
    auto subscribe_for_txn = subscribe.clone();
    ASSERT_TRUE(subscribe_for_txn.has_value()) << subscribe_for_txn.error().message;

    auto transport = make_client_txn_transport();
    ims::sip::Endpoint source{.address = "127.0.0.1", .port = 5090, .transport = "udp"};
    auto txn = std::make_shared<ims::sip::ServerTransaction>(std::move(*subscribe_for_txn), transport, source, io);

    ims::registration::RegistrationBinding binding;
    binding.impu = "sip:user@ims.example.com";
    binding.state = ims::registration::RegistrationBinding::State::kRegistered;
    binding.contacts.push_back({
        .contact_uri = "sip:user@127.0.0.1:5060",
        .expires = std::chrono::steady_clock::now() + std::chrono::hours(1),
    });

    EXPECT_CALL(*store, lookup("sip:user@ims.example.com"))
        .WillOnce(Return(ims::Result<ims::registration::RegistrationBinding>{binding}));

    ims::scscf::ScscfServiceTestPeer::onSubscribe(*service, txn, subscribe);

    ASSERT_EQ(transport->sent_messages.size(), 1u);
    const auto& response = transport->sent_messages.front();
    EXPECT_TRUE(response.isResponse());
    EXPECT_EQ(response.statusCode(), 200);
    EXPECT_EQ(response.reasonPhrase(), "OK");
    EXPECT_EQ(response.getHeader("Expires"), std::optional<std::string>{"180"});

    auto response_contact = response.contact();
    ASSERT_TRUE(response_contact.has_value());
    EXPECT_EQ(*response_contact, "<sip:127.0.0.1:0>");

    ASSERT_EQ(notifier_ptr->contexts.size(), 1u);
    const auto& context = notifier_ptr->contexts.front();
    EXPECT_EQ(context.request_uri, "sip:watcher@127.0.0.1:5090");
    EXPECT_EQ(context.to_header, "<sip:watcher@ims.example.com>;tag=from-tag");
    EXPECT_NE(context.from_header.find("<sip:user@ims.example.com>"), std::string::npos);
    EXPECT_NE(context.from_header.find(";tag="), std::string::npos);
}

TEST_F(ScscfServiceTest, OnSubscribeRejectsNonRegEventWith489) {
    auto subscribe = make_subscribe();
    subscribe.removeHeader("Event");
    subscribe.addHeader("Event", "presence");

    auto subscribe_for_txn = subscribe.clone();
    ASSERT_TRUE(subscribe_for_txn.has_value()) << subscribe_for_txn.error().message;

    auto transport = make_client_txn_transport();
    ims::sip::Endpoint source{.address = "127.0.0.1", .port = 5090, .transport = "udp"};
    auto txn = std::make_shared<ims::sip::ServerTransaction>(std::move(*subscribe_for_txn), transport, source, io);

    EXPECT_CALL(*store, lookup).Times(0);

    ims::scscf::ScscfServiceTestPeer::onSubscribe(*service, txn, subscribe);

    ASSERT_EQ(transport->sent_messages.size(), 1u);
    const auto& response = transport->sent_messages.front();
    EXPECT_TRUE(response.isResponse());
    EXPECT_EQ(response.statusCode(), 489);
    EXPECT_EQ(response.reasonPhrase(), "Bad Event");
    EXPECT_TRUE(notifier_ptr->contexts.empty());
}

TEST_F(ScscfServiceTest, OnSubscribeReturns404WhenBindingMissing) {
    auto subscribe = make_subscribe();
    auto subscribe_for_txn = subscribe.clone();
    ASSERT_TRUE(subscribe_for_txn.has_value()) << subscribe_for_txn.error().message;

    auto transport = make_client_txn_transport();
    ims::sip::Endpoint source{.address = "127.0.0.1", .port = 5090, .transport = "udp"};
    auto txn = std::make_shared<ims::sip::ServerTransaction>(std::move(*subscribe_for_txn), transport, source, io);

    EXPECT_CALL(*store, lookup("sip:user@ims.example.com"))
        .WillOnce(Return(ims::Result<ims::registration::RegistrationBinding>{
            std::unexpected(ims::ErrorInfo{ims::ErrorCode::kRegistrationNotFound, "Not found"})}));

    ims::scscf::ScscfServiceTestPeer::onSubscribe(*service, txn, subscribe);

    ASSERT_EQ(transport->sent_messages.size(), 1u);
    const auto& response = transport->sent_messages.front();
    EXPECT_TRUE(response.isResponse());
    EXPECT_EQ(response.statusCode(), 404);
    EXPECT_EQ(response.reasonPhrase(), "Not Found");
    EXPECT_TRUE(notifier_ptr->contexts.empty());
}

TEST_F(ScscfServiceTest, SendInitialNotifySkipsWhenContactMissing) {
    auto subscribe = make_subscribe();
    auto raw = subscribe.toString();
    ASSERT_TRUE(raw.has_value()) << raw.error().message;

    auto contact_pos = raw->find("Contact:");
    ASSERT_NE(contact_pos, std::string::npos);
    auto line_end = raw->find("\r\n", contact_pos);
    ASSERT_NE(line_end, std::string::npos);
    raw->erase(contact_pos, line_end - contact_pos + 2);

    auto parsed = ims::sip::SipMessage::parse(*raw);
    ASSERT_TRUE(parsed.has_value()) << parsed.error().message;

    ims::scscf::ScscfServiceTestPeer::sendInitialNotify(*service, *parsed, "generated-to-tag");

    EXPECT_TRUE(notifier_ptr->contexts.empty());
}

TEST_F(ScscfServiceTest, OnPrackReturns481WhenNoSessionExists) {
    auto prack = ims::sip::createRequest("PRACK", "sip:user@ims.example.com");
    ASSERT_TRUE(prack.has_value()) << prack.error().message;

    prack->setFromHeader("<sip:caller@ims.example.com>;tag=caller-tag");
    prack->setToHeader("<sip:user@ims.example.com>;tag=callee-tag");
    prack->setCallId("prack-call-id");
    prack->setCSeq(2, "PRACK");
    prack->addVia("SIP/2.0/UDP 127.0.0.1:5090;branch=z9hG4bK-prack-1");
    prack->addHeader("RAck", "1 1 INVITE");

    auto prack_for_txn = prack->clone();
    ASSERT_TRUE(prack_for_txn.has_value()) << prack_for_txn.error().message;

    auto transport = make_client_txn_transport();
    ims::sip::Endpoint source{.address = "127.0.0.1", .port = 5090, .transport = "udp"};
    auto txn = std::make_shared<ims::sip::ServerTransaction>(std::move(*prack_for_txn), transport, source, io);

    ims::scscf::ScscfServiceTestPeer::onPrack(*service, txn, *prack);

    ASSERT_EQ(transport->sent_messages.size(), 1u);
    const auto& response = transport->sent_messages.front();
    EXPECT_TRUE(response.isResponse());
    EXPECT_EQ(response.statusCode(), 481);
    EXPECT_EQ(response.reasonPhrase(), "Call/Transaction Does Not Exist");
}

TEST_F(ScscfServiceTest, OnInviteFallsBackToPeerIcscfWhenBindingMissing) {
    config.peer_icscf = ims::SipEndpointConfig{
        .address = "10.20.30.40",
        .port = 5071,
        .transport = "udp",
    };
    reinitializeService();

    auto invite = ims::sip::createRequest("INVITE", "sip:callee@ims.example.com");
    ASSERT_TRUE(invite.has_value()) << invite.error().message;
    invite->setFromHeader("<sip:caller@ims.example.com>;tag=caller-tag");
    invite->setToHeader("<sip:callee@ims.example.com>");
    invite->setCallId("invite-fallback-call");
    invite->setCSeq(1, "INVITE");
    invite->addVia("SIP/2.0/UDP 127.0.0.1:5090;branch=z9hG4bK-invite-fallback");

    auto invite_for_txn = invite->clone();
    ASSERT_TRUE(invite_for_txn.has_value()) << invite_for_txn.error().message;

    auto transport = make_client_txn_transport();
    ims::sip::Endpoint source{.address = "127.0.0.1", .port = 5090, .transport = "udp"};
    auto txn = std::make_shared<ims::sip::ServerTransaction>(std::move(*invite_for_txn), transport, source, io);

    EXPECT_CALL(*store, lookup("sip:callee@ims.example.com"))
        .WillOnce(Return(ims::Result<ims::registration::RegistrationBinding>{
            std::unexpected(ims::ErrorInfo{ims::ErrorCode::kRegistrationNotFound, "Not found"})}));

    ims::scscf::ScscfServiceTestPeer::onInvite(*service, txn, *invite);

    ASSERT_EQ(transport->sent_destinations.size(), 2u);
    EXPECT_EQ(transport->sent_destinations[0].address, "127.0.0.1");
    EXPECT_EQ(transport->sent_destinations[0].port, 5090);
    EXPECT_EQ(transport->sent_destinations[0].transport, "udp");
    EXPECT_EQ(transport->sent_destinations[1].address, "10.20.30.40");
    EXPECT_EQ(transport->sent_destinations[1].port, 5071);
    EXPECT_EQ(transport->sent_destinations[1].transport, "udp");
}

TEST_F(ScscfServiceTest, OnInviteReturns404WhenBindingMissingAndNoPeerIcscfConfigured) {
    config.peer_icscf.reset();
    reinitializeService();

    auto invite = ims::sip::createRequest("INVITE", "sip:callee@ims.example.com");
    ASSERT_TRUE(invite.has_value()) << invite.error().message;
    invite->setFromHeader("<sip:caller@ims.example.com>;tag=caller-tag");
    invite->setToHeader("<sip:callee@ims.example.com>");
    invite->setCallId("invite-no-peer-call");
    invite->setCSeq(1, "INVITE");
    invite->addVia("SIP/2.0/UDP 127.0.0.1:5090;branch=z9hG4bK-invite-no-peer");

    auto invite_for_txn = invite->clone();
    ASSERT_TRUE(invite_for_txn.has_value()) << invite_for_txn.error().message;

    auto transport = make_client_txn_transport();
    ims::sip::Endpoint source{.address = "127.0.0.1", .port = 5090, .transport = "udp"};
    auto txn = std::make_shared<ims::sip::ServerTransaction>(std::move(*invite_for_txn), transport, source, io);

    EXPECT_CALL(*store, lookup("sip:callee@ims.example.com"))
        .WillOnce(Return(ims::Result<ims::registration::RegistrationBinding>{
            std::unexpected(ims::ErrorInfo{ims::ErrorCode::kRegistrationNotFound, "Not found"})}));

    ims::scscf::ScscfServiceTestPeer::onInvite(*service, txn, *invite);

    ASSERT_EQ(transport->sent_messages.size(), 2u);
    const auto& response = transport->sent_messages.back();
    EXPECT_TRUE(response.isResponse());
    EXPECT_EQ(response.statusCode(), 404);
    EXPECT_EQ(response.reasonPhrase(), "Not Found");
}

TEST_F(ScscfServiceTest, StartSchedulesPeriodicRegistrationCleanup) {
    config.registration_cleanup_interval_ms = 1;
    reinitializeService();

    EXPECT_CALL(*store, purgeExpired())
        .WillOnce(Return(ims::Result<size_t>{1}))
        .WillRepeatedly(Return(ims::Result<size_t>{0}));

    ims::scscf::ScscfServiceTestPeer::scheduleRegistrationCleanup(*service);
    io.run_for(std::chrono::milliseconds(20));
    service->stop();
}

} // namespace
