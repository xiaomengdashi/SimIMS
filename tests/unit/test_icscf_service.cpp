#include "mocks/mock_hss_client.hpp"

#define private public
#include "i-cscf/icscf_service.hpp"
#undef private

#include "sip/message.hpp"
#include "sip/transaction.hpp"
#include "sip/transport.hpp"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

namespace {

using ::testing::_;
using ::testing::Return;
using ::testing::Truly;

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
        return ims::sip::Endpoint{.address = "127.0.0.1", .port = 5061, .transport = "udp"};
    }

    MessageCallback callback;
    std::vector<ims::sip::SipMessage> sent_messages;
    std::vector<ims::sip::Endpoint> sent_destinations;
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

class IcscfServiceTest : public ::testing::Test {
protected:
    void SetUp() override {
        config.listen_addr = "127.0.0.1";
        config.listen_port = 0;
        config.hss.realm = "ims.example.com";
        config.local_scscf = {
            .address = "10.10.10.10",
            .port = 6060,
            .transport = "udp",
        };

        service = std::make_unique<ims::icscf::IcscfService>(config, io, hss);

        capturing_transport = std::make_shared<CapturingTransport>();
        service->sip_stack_ = std::make_unique<FakeSipStack>(io, "127.0.0.1", 0, capturing_transport);
    }

    auto make_request(const std::string& method,
                      const std::string& call_id,
                      uint32_t cseq,
                      const std::string& request_uri = "sip:user@ims.example.com") -> ims::sip::SipMessage {
        auto request = ims::sip::createRequest(method, request_uri);
        EXPECT_TRUE(request.has_value()) << request.error().message;

        request->setFromHeader("<sip:caller@ims.example.com>;tag=caller-tag");
        request->setToHeader("<sip:user@ims.example.com>;tag=callee-tag");
        request->setCallId(call_id);
        request->setCSeq(cseq, method);
        request->addVia("SIP/2.0/UDP 127.0.0.1:5090;branch=z9hG4bK-icstest");

        auto raw = request->toString();
        EXPECT_TRUE(raw.has_value()) << raw.error().message;
        auto parsed = ims::sip::SipMessage::parse(*raw);
        EXPECT_TRUE(parsed.has_value()) << parsed.error().message;
        return std::move(*parsed);
    }

    boost::asio::io_context io;
    ims::IcscfConfig config;
    std::shared_ptr<ims::test::MockHssClient> hss = std::make_shared<ims::test::MockHssClient>();
    std::unique_ptr<ims::icscf::IcscfService> service;
    std::shared_ptr<CapturingTransport> capturing_transport;
};

TEST_F(IcscfServiceTest, LocalScscfEndpointUsesConfiguredValue) {
    auto endpoint = service->localScscfEndpoint();
    EXPECT_EQ(endpoint.address, config.local_scscf.address);
    EXPECT_EQ(endpoint.port, config.local_scscf.port);
    EXPECT_EQ(endpoint.transport, config.local_scscf.transport);
}

TEST_F(IcscfServiceTest, LocalScscfEndpointFallsBackLoopbackWhenAddressIsWildcard) {
    config.local_scscf.address = "0.0.0.0";
    service = std::make_unique<ims::icscf::IcscfService>(config, io, hss);
    capturing_transport = std::make_shared<CapturingTransport>();
    service->sip_stack_ = std::make_unique<FakeSipStack>(io, "127.0.0.1", 0, capturing_transport);

    auto endpoint = service->localScscfEndpoint();
    EXPECT_EQ(endpoint.address, "127.0.0.1");
    EXPECT_EQ(endpoint.port, config.local_scscf.port);
}

TEST_F(IcscfServiceTest, AckUsesConfiguredLocalScscfDestination) {
    auto ack = make_request("ACK", "ack-call", 2);

    service->onAck(ack);

    ASSERT_EQ(capturing_transport->sent_destinations.size(), 1u);
    EXPECT_EQ(capturing_transport->sent_destinations[0].address, config.local_scscf.address);
    EXPECT_EQ(capturing_transport->sent_destinations[0].port, config.local_scscf.port);
    EXPECT_EQ(capturing_transport->sent_destinations[0].transport, config.local_scscf.transport);
}

TEST_F(IcscfServiceTest, InDialogStatefulUsesConfiguredLocalScscfDestinationForPrack) {
    auto prack = make_request("PRACK", "prack-call", 3);
    auto prack_for_txn = prack.clone();
    ASSERT_TRUE(prack_for_txn.has_value()) << prack_for_txn.error().message;

    ims::sip::Endpoint source{.address = "127.0.0.1", .port = 5090, .transport = "udp"};
    auto txn = std::make_shared<ims::sip::ServerTransaction>(std::move(*prack_for_txn), capturing_transport, source, io);

    service->onInDialogStateful(txn, prack, "PRACK");

    ASSERT_EQ(capturing_transport->sent_destinations.size(), 1u);
    EXPECT_EQ(capturing_transport->sent_destinations[0].address, config.local_scscf.address);
    EXPECT_EQ(capturing_transport->sent_destinations[0].port, config.local_scscf.port);
    EXPECT_EQ(capturing_transport->sent_destinations[0].transport, config.local_scscf.transport);
}

TEST_F(IcscfServiceTest, InviteStillUsesSelectorResultForRouting) {
    auto invite = make_request("INVITE", "invite-call", 1);
    auto invite_for_txn = invite.clone();
    ASSERT_TRUE(invite_for_txn.has_value()) << invite_for_txn.error().message;

    ims::sip::Endpoint source{.address = "127.0.0.1", .port = 5090, .transport = "udp"};
    auto txn = std::make_shared<ims::sip::ServerTransaction>(std::move(*invite_for_txn), capturing_transport, source, io);

    EXPECT_CALL(*hss, locationInfo(_))
        .WillOnce(Return(ims::Result<ims::diameter::LiaResult>{ims::diameter::LiaResult{
            .result_code = 2001,
            .assigned_scscf = "sip:192.168.100.20:5099;transport=udp",
        }}));

    service->onInvite(txn, invite);

    ASSERT_EQ(capturing_transport->sent_destinations.size(), 1u);
    EXPECT_EQ(capturing_transport->sent_destinations[0].address, "192.168.100.20");
    EXPECT_EQ(capturing_transport->sent_destinations[0].port, 5099);
    EXPECT_EQ(capturing_transport->sent_destinations[0].transport, "udp");
}

TEST_F(IcscfServiceTest, InviteNormalizesTelRequestUriBeforeLocationLookup) {
    auto invite = make_request("INVITE",
                               "invite-tel-call",
                               1,
                               "tel:+8613800000001;phone-context=IMS.EXAMPLE.COM");
    auto invite_for_txn = invite.clone();
    ASSERT_TRUE(invite_for_txn.has_value()) << invite_for_txn.error().message;

    ims::sip::Endpoint source{.address = "127.0.0.1", .port = 5090, .transport = "udp"};
    auto txn = std::make_shared<ims::sip::ServerTransaction>(std::move(*invite_for_txn), capturing_transport, source, io);

    EXPECT_CALL(*hss, locationInfo(Truly([](const ims::diameter::LirParams& params) {
        return params.impu == "tel:+8613800000001";
    })))
        .WillOnce(Return(ims::Result<ims::diameter::LiaResult>{ims::diameter::LiaResult{
            .result_code = 2001,
            .assigned_scscf = "sip:192.168.100.30:5100;transport=udp",
        }}));

    service->onInvite(txn, invite);

    ASSERT_EQ(capturing_transport->sent_destinations.size(), 1u);
    EXPECT_EQ(capturing_transport->sent_destinations[0].address, "192.168.100.30");
    EXPECT_EQ(capturing_transport->sent_destinations[0].port, 5100);
    EXPECT_EQ(capturing_transport->sent_destinations[0].transport, "udp");
}

} // namespace
