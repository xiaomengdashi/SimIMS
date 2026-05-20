#include "sms/constants.hpp"
#include "sms/hex_utils.hpp"
#include "sms/rp_message.hpp"
#include "sms/samples.hpp"
#include "sms/sms_validator.hpp"
#include "sms/tpdu.hpp"
#include "sms/user_data.hpp"

#include <gtest/gtest.h>
#include <span>

namespace {

auto sample_rp_data_bytes() -> std::string {
    auto decoded = ims::sms::decode_hex(ims::sms::kSampleRpDataHex);
    EXPECT_TRUE(decoded.has_value()) << decoded.error().message;
    return std::string(decoded->begin(), decoded->end());
}

TEST(SmsOverImsTest, ParsesBlogRpDataExample) {
    const auto bytes = sample_rp_data_bytes();
    auto rp = ims::sms::parse_rp_message(std::span<const uint8_t>{
        reinterpret_cast<const uint8_t*>(bytes.data()), bytes.size()});
    ASSERT_TRUE(rp.has_value()) << rp.error().message;

    const auto* data = std::get_if<ims::sms::RpDataMessage>(&*rp);
    ASSERT_NE(data, nullptr);
    EXPECT_EQ(data->message_reference, 0x00);
    ASSERT_TRUE(data->destination.has_value());
    EXPECT_EQ(data->destination->digit_length, 0x0B);
    EXPECT_EQ(ims::sms::decode_bcd_msisdn(*data->destination).size(), data->destination->digit_length);
    EXPECT_EQ(data->user_data.size(), 0x0C);

    auto tpdu = ims::sms::parse_tpdu(data->user_data);
    ASSERT_TRUE(tpdu.has_value()) << tpdu.error().message;
    EXPECT_EQ(tpdu->type, ims::sms::TpduType::kSubmit);
}

TEST(SmsOverImsTest, Validates3gppSmsContentType) {
    const auto bytes = sample_rp_data_bytes();
    EXPECT_TRUE(ims::sms::validate_sip_message_body(ims::sms::kContentType3gppSms, bytes).has_value());
}

TEST(SmsOverImsTest, RejectsTextPlainForSmsOverIms) {
    EXPECT_FALSE(ims::sms::validate_sip_message_body(ims::sms::kContentTypeTextPlain, "hello").has_value());
}

TEST(SmsOverImsTest, BuildsRpAck) {
    auto ack = ims::sms::decode_hex(ims::sms::kSampleRpAckHex);
    ASSERT_TRUE(ack.has_value()) << ack.error().message;
    auto rp = ims::sms::parse_rp_message(*ack);
    ASSERT_TRUE(rp.has_value()) << rp.error().message;
    ASSERT_TRUE(std::holds_alternative<ims::sms::RpAckMessage>(*rp));
}

TEST(SmsOverImsTest, RoundTripRpDataEncoding) {
    const auto bytes = sample_rp_data_bytes();
    auto parsed = ims::sms::parse_rp_message(std::span<const uint8_t>{
        reinterpret_cast<const uint8_t*>(bytes.data()), bytes.size()});
    ASSERT_TRUE(parsed.has_value()) << parsed.error().message;

    auto encoded = ims::sms::encode_rp_message(*parsed);
    ASSERT_TRUE(encoded.has_value()) << encoded.error().message;
    EXPECT_EQ(*encoded, std::vector<uint8_t>(bytes.begin(), bytes.end()));
}

TEST(SmsOverImsTest, SubmitToDeliverTpduConversion) {
    const auto bytes = sample_rp_data_bytes();
    auto rp = ims::sms::parse_rp_message(std::span<const uint8_t>{
        reinterpret_cast<const uint8_t*>(bytes.data()), bytes.size()});
    ASSERT_TRUE(rp.has_value()) << rp.error().message;
    const auto* mo = std::get_if<ims::sms::RpDataMessage>(&*rp);
    ASSERT_NE(mo, nullptr);

    auto originator = ims::sms::encode_bcd_msisdn("460112024122023", 0x91);
    ASSERT_TRUE(originator.has_value()) << originator.error().message;

    auto deliver = ims::sms::submit_to_deliver_tpdu(mo->user_data, *originator);
    ASSERT_TRUE(deliver.has_value()) << deliver.error().message;

    auto tpdu = ims::sms::parse_tpdu(*deliver);
    ASSERT_TRUE(tpdu.has_value()) << tpdu.error().message;
    EXPECT_EQ(tpdu->type, ims::sms::TpduType::kDeliver);
    EXPECT_EQ(tpdu->first_octet & 0x03U, 0x00U);
}

TEST(SmsOverImsTest, BuildMtRpDataFromMoUsesDeliverTpdu) {
    const auto bytes = sample_rp_data_bytes();
    auto rp = ims::sms::parse_rp_message(std::span<const uint8_t>{
        reinterpret_cast<const uint8_t*>(bytes.data()), bytes.size()});
    ASSERT_TRUE(rp.has_value()) << rp.error().message;
    const auto* mo = std::get_if<ims::sms::RpDataMessage>(&*rp);
    ASSERT_NE(mo, nullptr);

    auto originator = ims::sms::encode_bcd_msisdn("460112024122023", 0x91);
    ASSERT_TRUE(originator.has_value()) << originator.error().message;

    auto mt = ims::sms::build_mt_rp_data_from_mo(*mo, *originator);
    ASSERT_TRUE(mt.has_value()) << mt.error().message;
    EXPECT_TRUE(!mt->destination.has_value() || mt->destination->digit_length == 0);
    ASSERT_TRUE(mt->originator.has_value());
    EXPECT_GT(mt->originator->digit_length, 0U);

    auto tpdu = ims::sms::parse_tpdu(mt->user_data);
    ASSERT_TRUE(tpdu.has_value()) << tpdu.error().message;
    EXPECT_EQ(tpdu->type, ims::sms::TpduType::kDeliver);

    auto encoded = ims::sms::encode_rp_message(*mt);
    ASSERT_TRUE(encoded.has_value()) << encoded.error().message;
    EXPECT_NE(*encoded, std::vector<uint8_t>(bytes.begin(), bytes.end()));
}

TEST(SmsOverImsTest, SampleWithoutTpSrrDoesNotRequestStatusReport) {
    auto decoded = ims::sms::decode_hex(ims::sms::kSampleRpDataNoSrrHex);
    ASSERT_TRUE(decoded.has_value()) << decoded.error().message;
    auto rp = ims::sms::parse_rp_message(*decoded);
    ASSERT_TRUE(rp.has_value()) << rp.error().message;
    const auto* mo = std::get_if<ims::sms::RpDataMessage>(&*rp);
    ASSERT_NE(mo, nullptr);
    auto tpdu = ims::sms::parse_tpdu(mo->user_data);
    ASSERT_TRUE(tpdu.has_value()) << tpdu.error().message;
    EXPECT_FALSE(ims::sms::submit_requests_status_report(tpdu->first_octet));
}

TEST(SmsOverImsTest, DecodesUcs2SubmitUserData) {
    constexpr std::string_view kTpduHex =
        "01000000080A6D4B8BD500206D88606F";
    auto tpdu_bytes = ims::sms::decode_hex(kTpduHex);
    ASSERT_TRUE(tpdu_bytes.has_value()) << tpdu_bytes.error().message;

    auto submit = ims::sms::parse_submit_tpdu(*tpdu_bytes);
    ASSERT_TRUE(submit.has_value()) << submit.error().message;
    EXPECT_EQ(submit->dcs, 0x08);
    EXPECT_EQ(submit->user_data_length, 0x0A);

    auto decoded = ims::sms::decode_submit_user_data(*submit);
    ASSERT_TRUE(decoded.has_value()) << decoded.error().message;
    EXPECT_EQ(decoded->alphabet, ims::sms::UserDataAlphabet::kUcs2);
    EXPECT_EQ(decoded->text, "测试 消息");
}

TEST(SmsOverImsTest, DecodesGsm7BitUserData) {
    const std::vector<uint8_t> packed{0xC8, 0x32, 0x9B, 0xFD, 0x06};
    auto decoded = ims::sms::decode_user_data(0x00, packed, 5);
    ASSERT_TRUE(decoded.has_value()) << decoded.error().message;
    EXPECT_EQ(decoded->alphabet, ims::sms::UserDataAlphabet::kGsm7BitDefault);
    EXPECT_EQ(decoded->text, "Hello");
}

TEST(SmsOverImsTest, ClassifyDcsAlphabets) {
    auto gsm7 = ims::sms::classify_dcs(0x00);
    ASSERT_TRUE(gsm7.has_value());
    EXPECT_EQ(*gsm7, ims::sms::UserDataAlphabet::kGsm7BitDefault);

    auto eight_bit = ims::sms::classify_dcs(0x04);
    ASSERT_TRUE(eight_bit.has_value());
    EXPECT_EQ(*eight_bit, ims::sms::UserDataAlphabet::kEightBit);

    auto ucs2 = ims::sms::classify_dcs(0x08);
    ASSERT_TRUE(ucs2.has_value());
    EXPECT_EQ(*ucs2, ims::sms::UserDataAlphabet::kUcs2);
}

TEST(SmsOverImsTest, ParsesDeliverTpduFields) {
    auto originator = ims::sms::encode_bcd_msisdn("86138012345", 0x91);
    ASSERT_TRUE(originator.has_value()) << originator.error().message;

    const std::vector<uint8_t> user_data{0x6D, 0x4B, 0x8B, 0xD5, 0x00, 0x20, 0x6D, 0x88, 0x60, 0x6F};
    const ims::sms::ServiceCentreTimeStamp scts{
        0x25, 0x05, 0x21, 0x12, 0x34, 0x56, 0x08,
    };

    auto encoded = ims::sms::encode_deliver_tpdu(0x04, *originator, 0x00, 0x08, user_data);
    ASSERT_TRUE(encoded.has_value()) << encoded.error().message;

    // first(1) + OA(8) + PID(1) + DCS(1) = 11
    constexpr std::size_t kSctsOffset = 11;
    std::copy(scts.begin(), scts.end(), encoded->begin() + static_cast<std::ptrdiff_t>(kSctsOffset));

    auto deliver = ims::sms::parse_deliver_tpdu(*encoded);
    ASSERT_TRUE(deliver.has_value()) << deliver.error().message;
    EXPECT_EQ(deliver->first_octet, 0x04);
    EXPECT_EQ(deliver->pid, 0x00);
    EXPECT_EQ(deliver->dcs, 0x08);
    EXPECT_EQ(deliver->user_data_length, user_data.size());
    EXPECT_EQ(deliver->user_data, user_data);
    EXPECT_EQ(deliver->service_centre_time_stamp, scts);
    ASSERT_GT(deliver->originator.digit_length, 0U);
    EXPECT_EQ(ims::sms::decode_bcd_msisdn(deliver->originator), "86138012345");
    EXPECT_FALSE(ims::sms::deliver_has_user_data_header(deliver->first_octet));
    EXPECT_FALSE(ims::sms::deliver_has_more_messages(deliver->first_octet));

    auto decoded = ims::sms::decode_deliver_user_data(*deliver);
    ASSERT_TRUE(decoded.has_value()) << decoded.error().message;
    EXPECT_EQ(decoded->text, "测试 消息");
}

TEST(SmsOverImsTest, ParsesDeliverTpduFromSubmitConversion) {
    const auto bytes = sample_rp_data_bytes();
    auto rp = ims::sms::parse_rp_message(std::span<const uint8_t>{
        reinterpret_cast<const uint8_t*>(bytes.data()), bytes.size()});
    ASSERT_TRUE(rp.has_value()) << rp.error().message;
    const auto* mo = std::get_if<ims::sms::RpDataMessage>(&*rp);
    ASSERT_NE(mo, nullptr);

    auto originator = ims::sms::encode_bcd_msisdn("460112024122023", 0x91);
    ASSERT_TRUE(originator.has_value()) << originator.error().message;

    auto deliver_bytes = ims::sms::submit_to_deliver_tpdu(mo->user_data, *originator);
    ASSERT_TRUE(deliver_bytes.has_value()) << deliver_bytes.error().message;

    auto deliver = ims::sms::parse_deliver_tpdu(*deliver_bytes);
    ASSERT_TRUE(deliver.has_value()) << deliver.error().message;
    EXPECT_EQ(deliver->originator.digit_length, originator->digit_length);
    EXPECT_EQ(ims::sms::decode_bcd_msisdn(deliver->originator), "460112024122023");
    EXPECT_EQ(deliver->service_centre_time_stamp, ims::sms::ServiceCentreTimeStamp{});
}

} // namespace
