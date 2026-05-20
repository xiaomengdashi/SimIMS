#include "ims_test_harness.hpp"

#include "sms/hex_utils.hpp"
#include "sms/rp_message.hpp"
#include "sms/samples.hpp"
#include "sms/tpdu.hpp"

#include <gtest/gtest.h>

namespace {

using ims::integration::ImsTestHarness;
using ims::integration::Node;
using ims::integration::kCalleeImpi;
using ims::integration::kCalleeImpu;
using ims::integration::kCalleeUePort;
using ims::integration::kCallerImpi;
using ims::integration::kCallerImpu;
using ims::integration::kCallerUePort;
using ims::integration::invite_raw;
using ims::integration::message_raw;
using ims::integration::message_rp_ack_raw;

class FullFlowIntegrationTest : public ::testing::Test {
protected:
    void SetUp() override {
        auto started = harness_.start();
        ASSERT_TRUE(started.has_value()) << started.error().message;
    }

    void TearDown() override {
        harness_.stop();
    }

    void registerBothUes() {
        harness_.registerUe(Node::kCallerUe, kCallerImpi, kCallerImpu, "integration-caller-reg", kCallerUePort);
        harness_.registerUe(Node::kCalleeUe, kCalleeImpi, kCalleeImpu, "integration-callee-reg", kCalleeUePort);
    }

    struct MtDeliverMessage {
        const ims::sip::SipMessage* request = nullptr;
        std::string body;
    };

    auto findMtDeliverMessage(Node callee) -> std::optional<MtDeliverMessage> {
        for (const auto& msg : harness_.ueMessages(callee)) {
            if (!msg.isRequest() || msg.method() != "MESSAGE") {
                continue;
            }
            const auto body = msg.body();
            if (!body || body->empty()) {
                continue;
            }

            const auto bytes = std::span<const uint8_t>{
                reinterpret_cast<const uint8_t*>(body->data()), body->size()};
            auto rp = ims::sms::parse_rp_message(bytes);
            if (!rp) {
                continue;
            }
            const auto* data = std::get_if<ims::sms::RpDataMessage>(&*rp);
            if (!data) {
                continue;
            }
            auto tpdu = ims::sms::parse_tpdu(data->user_data);
            if (!tpdu || tpdu->type != ims::sms::TpduType::kDeliver) {
                continue;
            }
            return MtDeliverMessage{.request = &msg, .body = *body};
        }
        return std::nullopt;
    }

    ImsTestHarness harness_;
};

TEST_F(FullFlowIntegrationTest, RegisterThenSmsMoMtViaSmsc) {
    constexpr auto kInviteCallId = "integration-invite-call";
    constexpr auto kMoCallId = "integration-sms-mo";
    constexpr auto kMtRpAckCallId = "integration-sms-mt-rpack";

    registerBothUes();

    harness_.sendUeRaw(Node::kCallerUe, invite_raw(kCallerImpu, kCalleeImpu, kInviteCallId, kCallerUePort));
    EXPECT_TRUE(harness_.hasRequestTo(Node::kCalleeUe, "INVITE", kInviteCallId));

    const auto mo_body = ims::sms::decode_hex(ims::sms::kSampleRpDataHex);
    ASSERT_TRUE(mo_body.has_value()) << mo_body.error().message;

    harness_.sendUeRaw(Node::kCallerUe, message_raw(kCallerImpu, kCalleeImpu, kMoCallId, kCallerUePort));

    ASSERT_NE(harness_.findResponse(Node::kCallerUe, kMoCallId, 202), nullptr);

    const auto* mo_rp_ack = harness_.findMessageWithRpAck(Node::kCallerUe);
    ASSERT_NE(mo_rp_ack, nullptr);
    harness_.sendUeResponse(Node::kCallerUe, *mo_rp_ack, 200, "OK");

    const auto mt_message = findMtDeliverMessage(Node::kCalleeUe);
    ASSERT_TRUE(mt_message.has_value());
    EXPECT_NE(mt_message->body, std::string(mo_body->begin(), mo_body->end()));

    const auto mt_bytes = std::span<const uint8_t>{
        reinterpret_cast<const uint8_t*>(mt_message->body.data()), mt_message->body.size()};
    auto mt_rp = ims::sms::parse_rp_message(mt_bytes);
    ASSERT_TRUE(mt_rp.has_value()) << mt_rp.error().message;
    const auto* mt_data = std::get_if<ims::sms::RpDataMessage>(&*mt_rp);
    ASSERT_NE(mt_data, nullptr);
    ASSERT_TRUE(mt_data->originator.has_value());
    EXPECT_GT(mt_data->originator->digit_length, 0U);

    auto mt_tpdu = ims::sms::parse_tpdu(mt_data->user_data);
    ASSERT_TRUE(mt_tpdu.has_value()) << mt_tpdu.error().message;
    EXPECT_EQ(mt_tpdu->type, ims::sms::TpduType::kDeliver);

    harness_.sendUeResponse(Node::kCalleeUe, *mt_message->request, 202, "Accepted");
    harness_.sendUeRaw(Node::kCalleeUe,
                       message_rp_ack_raw(kCalleeImpu, kMtRpAckCallId, kCalleeUePort, 2));
    ASSERT_NE(harness_.findResponse(Node::kCalleeUe, kMtRpAckCallId, 200), nullptr);
}

TEST_F(FullFlowIntegrationTest, MoWithoutTpSrrSkipsMoRpAck) {
    constexpr auto kMoCallId = "integration-sms-mo-no-srr";
    constexpr auto kMtRpAckCallId = "integration-sms-mt-no-srr-rpack";

    registerBothUes();

    harness_.sendUeRaw(Node::kCallerUe,
                       message_raw(kCallerImpu,
                                   kCalleeImpu,
                                   kMoCallId,
                                   kCallerUePort,
                                   ims::sms::kSampleRpDataNoSrrHex));

    ASSERT_NE(harness_.findResponse(Node::kCallerUe, kMoCallId, 202), nullptr);
    EXPECT_EQ(harness_.findMessageWithRpAck(Node::kCallerUe), nullptr);

    const auto mt_message = findMtDeliverMessage(Node::kCalleeUe);
    ASSERT_TRUE(mt_message.has_value());

    const auto mt_bytes = std::span<const uint8_t>{
        reinterpret_cast<const uint8_t*>(mt_message->body.data()), mt_message->body.size()};
    auto mt_rp = ims::sms::parse_rp_message(mt_bytes);
    ASSERT_TRUE(mt_rp.has_value()) << mt_rp.error().message;
    const auto* mt_data = std::get_if<ims::sms::RpDataMessage>(&*mt_rp);
    ASSERT_NE(mt_data, nullptr);
    auto mt_tpdu = ims::sms::parse_tpdu(mt_data->user_data);
    ASSERT_TRUE(mt_tpdu.has_value()) << mt_tpdu.error().message;
    EXPECT_EQ(mt_tpdu->type, ims::sms::TpduType::kDeliver);

    harness_.sendUeResponse(Node::kCalleeUe, *mt_message->request, 202, "Accepted");
    harness_.sendUeRaw(Node::kCalleeUe,
                       message_rp_ack_raw(kCalleeImpu, kMtRpAckCallId, kCalleeUePort, 2));
    ASSERT_NE(harness_.findResponse(Node::kCalleeUe, kMtRpAckCallId, 200), nullptr);
}

} // namespace
