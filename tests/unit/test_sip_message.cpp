#include "ims/sip/message.hpp"
#include <gtest/gtest.h>

using namespace ims::sip;

class SipMessageTest : public ::testing::Test {
protected:
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

    static constexpr auto kInviteMsg =
        "INVITE sip:callee@ims.example.com SIP/2.0\r\n"
        "Via: SIP/2.0/UDP 10.0.0.1:5060;branch=z9hG4bKnashds7\r\n"
        "Max-Forwards: 70\r\n"
        "To: <sip:callee@ims.example.com>\r\n"
        "From: <sip:caller@ims.example.com>;tag=8735290\r\n"
        "Call-ID: 3848276298220188511@10.0.0.1\r\n"
        "CSeq: 1 INVITE\r\n"
        "Contact: <sip:caller@10.0.0.1:5060>\r\n"
        "Content-Type: application/sdp\r\n"
        "Content-Length: 131\r\n"
        "\r\n"
        "v=0\r\n"
        "o=caller 2890844526 2890844526 IN IP4 10.0.0.1\r\n"
        "s=-\r\n"
        "c=IN IP4 10.0.0.1\r\n"
        "t=0 0\r\n"
        "m=audio 49170 RTP/AVP 0\r\n"
        "a=rtpmap:0 PCMU/8000\r\n";
};

TEST_F(SipMessageTest, ParseRegister) {
    auto result = SipMessage::parse(kRegisterMsg);
    ASSERT_TRUE(result.has_value()) << result.error().message;

    auto& msg = *result;
    EXPECT_TRUE(msg.isRequest());
    EXPECT_FALSE(msg.isResponse());
    EXPECT_EQ(msg.method(), "REGISTER");
    EXPECT_EQ(msg.callId(), "a84b4c76e66710@10.0.0.1");
    EXPECT_EQ(msg.cseq(), 1u);
    EXPECT_EQ(msg.cseqMethod(), "REGISTER");
    EXPECT_EQ(msg.maxForwards(), 70);
    EXPECT_EQ(msg.fromTag(), "1928301774");
    EXPECT_EQ(msg.viaCount(), 1);
}

TEST_F(SipMessageTest, ParseInviteWithSdp) {
    auto result = SipMessage::parse(kInviteMsg);
    ASSERT_TRUE(result.has_value()) << result.error().message;

    auto& msg = *result;
    EXPECT_TRUE(msg.isRequest());
    EXPECT_EQ(msg.method(), "INVITE");

    auto body = msg.body();
    ASSERT_TRUE(body.has_value());
    EXPECT_NE(body->find("m=audio"), std::string::npos);
}

TEST_F(SipMessageTest, CreateResponse) {
    auto req_result = SipMessage::parse(kRegisterMsg);
    ASSERT_TRUE(req_result.has_value());

    auto resp_result = createResponse(*req_result, 200, "OK");
    ASSERT_TRUE(resp_result.has_value()) << resp_result.error().message;

    auto& resp = *resp_result;
    EXPECT_TRUE(resp.isResponse());
    EXPECT_EQ(resp.statusCode(), 200);
    EXPECT_EQ(resp.callId(), "a84b4c76e66710@10.0.0.1");
    EXPECT_EQ(resp.fromTag(), "1928301774");
    EXPECT_EQ(resp.cseq(), 1u);
    EXPECT_EQ(resp.cseqMethod(), "REGISTER");
    EXPECT_FALSE(resp.toTag().empty());
}

TEST_F(SipMessageTest, CreateResponsePreservesViaOrder) {
    auto req_result = SipMessage::parse(kRegisterMsg);
    ASSERT_TRUE(req_result.has_value());

    auto& req = *req_result;
    req.addVia("SIP/2.0/UDP proxy1.example.com:5060;branch=z9hG4bK-proxy-1");

    auto resp_result = createResponse(req, 200, "OK");
    ASSERT_TRUE(resp_result.has_value());

    auto& resp = *resp_result;
    auto top_via = resp.topVia();
    EXPECT_NE(top_via.find("proxy1.example.com"), std::string::npos);
}

TEST_F(SipMessageTest, SerializeRoundTrip) {
    auto result = SipMessage::parse(kRegisterMsg);
    ASSERT_TRUE(result.has_value());

    auto str_result = result->toString();
    ASSERT_TRUE(str_result.has_value());

    // Re-parse the serialized message
    auto result2 = SipMessage::parse(*str_result);
    ASSERT_TRUE(result2.has_value());
    EXPECT_EQ(result2->method(), "REGISTER");
    EXPECT_EQ(result2->callId(), result->callId());
}

TEST_F(SipMessageTest, ViaOperations) {
    auto result = SipMessage::parse(kRegisterMsg);
    ASSERT_TRUE(result.has_value());

    auto& msg = *result;
    EXPECT_EQ(msg.viaCount(), 1);

    // Add a new Via
    msg.addVia("SIP/2.0/UDP proxy.example.com:5060;branch=z9hG4bK-proxy-1");
    EXPECT_EQ(msg.viaCount(), 2);

    // Top Via should be the newly added one
    auto top_via = msg.topVia();
    EXPECT_NE(top_via.find("proxy.example.com"), std::string::npos);

    // Remove top Via
    msg.removeTopVia();
    EXPECT_EQ(msg.viaCount(), 1);
}

TEST_F(SipMessageTest, MaxForwardsDecrement) {
    auto result = SipMessage::parse(kRegisterMsg);
    ASSERT_TRUE(result.has_value());

    auto& msg = *result;
    EXPECT_EQ(msg.maxForwards(), 70);

    msg.decrementMaxForwards();
    EXPECT_EQ(msg.maxForwards(), 69);
}

TEST_F(SipMessageTest, GenerateBranch) {
    auto branch1 = generateBranch();
    auto branch2 = generateBranch();

    // Must start with magic cookie
    EXPECT_EQ(branch1.substr(0, 7), "z9hG4bK");
    EXPECT_EQ(branch2.substr(0, 7), "z9hG4bK");

    // Must be unique
    EXPECT_NE(branch1, branch2);
}

TEST_F(SipMessageTest, GenerateTag) {
    auto tag1 = generateTag();
    auto tag2 = generateTag();

    EXPECT_FALSE(tag1.empty());
    EXPECT_NE(tag1, tag2);
}

TEST_F(SipMessageTest, ParseInvalidMessage) {
    auto result = SipMessage::parse("not a valid SIP message");
    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, ims::ErrorCode::kSipParseError);
}

TEST_F(SipMessageTest, BodyOperations) {
    auto result = SipMessage::create();
    ASSERT_TRUE(result.has_value());

    auto& msg = *result;
    EXPECT_FALSE(msg.body().has_value());

    std::string sdp = "v=0\r\no=- 0 0 IN IP4 0.0.0.0\r\ns=-\r\nt=0 0\r\n";
    msg.setBody(sdp, "application/sdp");

    auto body = msg.body();
    ASSERT_TRUE(body.has_value());
    EXPECT_EQ(*body, sdp);
}
