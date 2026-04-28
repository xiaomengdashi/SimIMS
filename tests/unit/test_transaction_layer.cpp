#include "sip/transaction.hpp"
#include "sip/message.hpp"
#include "../mocks/mock_transport.hpp"

#include <boost/asio/io_context.hpp>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

using namespace ims;
using namespace ims::sip;
using namespace ims::test;
using ::testing::_;
using ::testing::Return;

namespace {

static constexpr auto kRegisterMsg =
    "REGISTER sip:ims.example.com SIP/2.0\r\n"
    "Via: SIP/2.0/UDP 10.0.0.1:5060;branch=z9hG4bK-reg-1\r\n"
    "Max-Forwards: 70\r\n"
    "To: <sip:user@ims.example.com>\r\n"
    "From: <sip:user@ims.example.com>;tag=1928301774\r\n"
    "Call-ID: reg-call-id@10.0.0.1\r\n"
    "CSeq: 1 REGISTER\r\n"
    "Contact: <sip:user@10.0.0.1:5060>\r\n"
    "Content-Length: 0\r\n"
    "\r\n";

static constexpr auto kInviteMsg =
    "INVITE sip:callee@ims.example.com SIP/2.0\r\n"
    "Via: SIP/2.0/UDP 10.0.0.1:5060;branch=z9hG4bK-invite-1\r\n"
    "Max-Forwards: 70\r\n"
    "To: <sip:callee@ims.example.com>\r\n"
    "From: <sip:caller@ims.example.com>;tag=8735290\r\n"
    "Call-ID: invite-call-id@10.0.0.1\r\n"
    "CSeq: 1 INVITE\r\n"
    "Contact: <sip:caller@10.0.0.1:5060>\r\n"
    "Content-Length: 0\r\n"
    "\r\n";

class TransactionLayerTest : public ::testing::Test {
protected:
    boost::asio::io_context io;
    std::shared_ptr<MockTransport> transport = std::make_shared<MockTransport>();
    Endpoint dest{.address = "127.0.0.1", .port = 5060, .transport = "udp"};
    Endpoint source{.address = "10.0.0.1", .port = 5060, .transport = "udp"};

    void SetUp() override {
        ON_CALL(*transport, start()).WillByDefault(Return(VoidResult{}));
        ON_CALL(*transport, localEndpoint()).WillByDefault(Return(dest));
        ON_CALL(*transport, setMessageCallback(_)).WillByDefault(Return());
        ON_CALL(*transport, send(_, _)).WillByDefault(Return(VoidResult{}));
    }

    auto parseMessage(const std::string& raw) -> SipMessage {
        auto msg = SipMessage::parse(raw);
        EXPECT_TRUE(msg.has_value()) << msg.error().message;
        return std::move(*msg);
    }
};

TEST_F(TransactionLayerTest, DuplicateClientTransactionKeyReturnsError) {
    TransactionLayer layer(io, transport);
    auto first = parseMessage(kRegisterMsg);
    auto second = parseMessage(kRegisterMsg);

    EXPECT_CALL(*transport, send(_, _)).Times(1).WillOnce(Return(VoidResult{}));

    auto first_result = layer.sendRequest(std::move(first), dest, {});
    ASSERT_TRUE(first_result.has_value()) << first_result.error().message;

    auto second_result = layer.sendRequest(std::move(second), dest, {});
    ASSERT_FALSE(second_result.has_value());
    EXPECT_EQ(second_result.error().code, ErrorCode::kSipTransactionFailed);
    EXPECT_NE(second_result.error().message.find("Duplicate client transaction key"), std::string::npos);
}

TEST_F(TransactionLayerTest, Invite2xxResponseRemovesClientTransactionImmediately) {
    TransactionLayer layer(io, transport);
    auto invite = parseMessage(kInviteMsg);
    auto response = createResponse(invite, 200, "OK");
    ASSERT_TRUE(response.has_value()) << response.error().message;

    EXPECT_CALL(*transport, send(_, _)).Times(2).WillRepeatedly(Return(VoidResult{}));

    auto sent = layer.sendRequest(std::move(invite), dest, {});
    ASSERT_TRUE(sent.has_value()) << sent.error().message;

    layer.processMessage(std::move(*response), source);

    auto duplicate = parseMessage(kInviteMsg);
    auto duplicate_result = layer.sendRequest(std::move(duplicate), dest, {});
    EXPECT_TRUE(duplicate_result.has_value()) << duplicate_result.error().message;
}

TEST_F(TransactionLayerTest, ServerInvite2xxTerminationCallbackFiresOnce) {
    auto invite = parseMessage(kInviteMsg);
    auto response = createResponse(invite, 200, "OK");
    ASSERT_TRUE(response.has_value()) << response.error().message;

    auto txn = std::make_shared<ServerTransaction>(std::move(invite), transport, source, io);
    int terminated_count = 0;
    txn->onTerminated([&terminated_count]() { ++terminated_count; });

    EXPECT_CALL(*transport, send(_, _)).Times(2).WillRepeatedly(Return(VoidResult{}));

    auto send_result = txn->sendResponse(std::move(*response));
    ASSERT_TRUE(send_result.has_value()) << send_result.error().message;
    EXPECT_EQ(txn->state(), TransactionState::kTerminated);
    EXPECT_EQ(terminated_count, 1);

    auto retransmit_result = txn->retransmitLastResponse();
    ASSERT_TRUE(retransmit_result.has_value()) << retransmit_result.error().message;
    EXPECT_EQ(terminated_count, 1);
}

TEST_F(TransactionLayerTest, ClientTransactionSendFailureInvokesTimeoutCallbackSafely) {
    auto invite = parseMessage(kInviteMsg);
    auto txn = std::make_shared<ClientTransaction>(std::move(invite), transport, dest, io);
    int timeout_count = 0;

    EXPECT_CALL(*transport, send(_, _))
        .WillOnce(Return(std::unexpected(ErrorInfo{ErrorCode::kSipTransportError, "send failed"})));

    txn->onTimeout([&timeout_count]() { ++timeout_count; });
    txn->start();

    EXPECT_EQ(txn->state(), TransactionState::kTerminated);
    EXPECT_EQ(timeout_count, 1);
}

} // namespace
