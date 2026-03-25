#include "sip/message.hpp"
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

TEST_F(SipMessageTest, CreateResponseRoundTripPreservesAllVias) {
    auto req_result = SipMessage::parse(kRegisterMsg);
    ASSERT_TRUE(req_result.has_value());

    req_result->addVia("SIP/2.0/UDP proxy1.example.com:5060;branch=z9hG4bK-proxy-1");
    req_result->addVia("SIP/2.0/UDP proxy2.example.com:5060;branch=z9hG4bK-proxy-2");
    ASSERT_EQ(req_result->viaCount(), 3);

    auto resp_result = createResponse(*req_result, 401, "Unauthorized");
    ASSERT_TRUE(resp_result.has_value()) << resp_result.error().message;
    EXPECT_EQ(resp_result->viaCount(), 3);

    auto raw = resp_result->toString();
    ASSERT_TRUE(raw.has_value()) << raw.error().message;

    auto reparsed = SipMessage::parse(*raw);
    ASSERT_TRUE(reparsed.has_value()) << reparsed.error().message;
    EXPECT_EQ(reparsed->viaCount(), 3);
    EXPECT_EQ(reparsed->viaBranch(), "z9hG4bK-proxy-2");
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

TEST_F(SipMessageTest, ParseResponseWithMultipleVias) {
    auto result = SipMessage::parse(
        "SIP/2.0 401 Unauthorized\r\n"
        "Via: SIP/2.0/UDP 127.0.0.1:5061;branch=z9hG4bK-icscf\r\n"
        "Via: SIP/2.0/UDP 127.0.0.1:5060;branch=z9hG4bK-pcscf\r\n"
        "Via: SIP/2.0/UDP ue.example.com:5090;branch=z9hG4bK-ue\r\n"
        "To: <sip:user@ims.example.com>;tag=to-tag\r\n"
        "From: <sip:user@ims.example.com>;tag=from-tag\r\n"
        "Call-ID: response-call-id\r\n"
        "CSeq: 1 REGISTER\r\n"
        "Content-Length: 0\r\n"
        "\r\n");
    ASSERT_TRUE(result.has_value()) << result.error().message;
    EXPECT_EQ(result->viaCount(), 3);
    EXPECT_EQ(result->viaBranch(), "z9hG4bK-icscf");
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

TEST_F(SipMessageTest, RemoveHeaderRemovesSpecializedContact) {
    auto result = SipMessage::parse(kRegisterMsg);
    ASSERT_TRUE(result.has_value()) << result.error().message;

    auto& msg = *result;
    ASSERT_TRUE(msg.contact().has_value());

    msg.removeHeader("contact");

    EXPECT_FALSE(msg.contact().has_value());
    auto serialized = msg.toString();
    ASSERT_TRUE(serialized.has_value()) << serialized.error().message;
    EXPECT_EQ(serialized->find("Contact:"), std::string::npos);
}

TEST_F(SipMessageTest, RemoveHeaderRemovesRecordRoute) {
    auto result = SipMessage::parse(kRegisterMsg);
    ASSERT_TRUE(result.has_value()) << result.error().message;

    result->addRecordRoute("<sip:edge1.ims.example.com;lr>");
    result->addRecordRoute("<sip:edge2.ims.example.com;lr>");

    auto raw = result->toString();
    ASSERT_TRUE(raw.has_value()) << raw.error().message;

    auto reparsed = SipMessage::parse(*raw);
    ASSERT_TRUE(reparsed.has_value()) << reparsed.error().message;
    ASSERT_FALSE(reparsed->getHeaders("Record-Route").empty());

    reparsed->removeHeader("Record-Route");

    EXPECT_TRUE(reparsed->getHeaders("Record-Route").empty());
    auto serialized = reparsed->toString();
    ASSERT_TRUE(serialized.has_value()) << serialized.error().message;
    EXPECT_EQ(serialized->find("Record-Route:"), std::string::npos);
}

TEST_F(SipMessageTest, GetHeaderReturnsSpecializedContact) {
    auto result = SipMessage::parse(kRegisterMsg);
    ASSERT_TRUE(result.has_value()) << result.error().message;

    EXPECT_EQ(result->getHeader("Contact"), std::optional<std::string>{"<sip:user@10.0.0.1:5060>"});
    EXPECT_EQ(result->getHeaders("Contact"), std::vector<std::string>({"<sip:user@10.0.0.1:5060>"}));
}

TEST_F(SipMessageTest, GetHeadersReturnsSpecializedRoutes) {
    auto result = SipMessage::parse(kRegisterMsg);
    ASSERT_TRUE(result.has_value()) << result.error().message;

    result->addRoute("<sip:edge1.ims.example.com;lr>");
    result->addRoute("<sip:edge2.ims.example.com;lr>");

    auto routes = result->getHeaders("Route");
    ASSERT_EQ(routes.size(), 2u);
    EXPECT_NE(routes[0].find("edge1.ims.example.com"), std::string::npos);
    EXPECT_NE(routes[1].find("edge2.ims.example.com"), std::string::npos);
}

TEST_F(SipMessageTest, GetHeaderReturnsSpecializedAuthorization) {
    static constexpr auto kMsg =
        "REGISTER sip:ims.example.com SIP/2.0\r\n"
        "Via: SIP/2.0/UDP 10.0.0.1:5060;branch=z9hG4bK776asdhds\r\n"
        "Max-Forwards: 70\r\n"
        "To: <sip:user@ims.example.com>\r\n"
        "From: <sip:user@ims.example.com>;tag=1928301774\r\n"
        "Call-ID: a84b4c76e66710@10.0.0.1\r\n"
        "CSeq: 2 REGISTER\r\n"
        "Authorization: Digest username=\"user\", realm=\"ims.example.com\", nonce=\"abc\", uri=\"sip:ims.example.com\", response=\"deadbeef\"\r\n"
        "Content-Length: 0\r\n"
        "\r\n";

    auto result = SipMessage::parse(kMsg);
    ASSERT_TRUE(result.has_value()) << result.error().message;

    auto auth = result->getHeader("Authorization");
    ASSERT_TRUE(auth.has_value());
    EXPECT_NE(auth->find("username=\"user\""), std::string::npos);
}

TEST_F(SipMessageTest, RemoveHeaderRemovesSpecializedRoutes) {
    auto result = SipMessage::parse(kRegisterMsg);
    ASSERT_TRUE(result.has_value()) << result.error().message;

    result->addRoute("<sip:edge1.ims.example.com;lr>");
    result->addRoute("<sip:edge2.ims.example.com;lr>");
    ASSERT_EQ(result->routes().size(), 2u);

    result->removeHeader("Route");

    EXPECT_TRUE(result->routes().empty());
    EXPECT_TRUE(result->getHeaders("Route").empty());
    auto serialized = result->toString();
    ASSERT_TRUE(serialized.has_value()) << serialized.error().message;
    EXPECT_EQ(serialized->find("Route:"), std::string::npos);
}

TEST_F(SipMessageTest, RemoveTopRouteKeepsRemainingRoutes) {
    auto result = SipMessage::parse(kRegisterMsg);
    ASSERT_TRUE(result.has_value()) << result.error().message;

    result->addRoute("<sip:edge1.ims.example.com;lr>");
    result->addRoute("<sip:edge2.ims.example.com;lr>");

    result->removeTopRoute();

    auto routes = result->routes();
    ASSERT_EQ(routes.size(), 1u);
    EXPECT_NE(routes.front().find("edge2.ims.example.com"), std::string::npos);
}

TEST_F(SipMessageTest, TypedAccessorsExtractUrisAndExpires) {
    static constexpr auto kMsg =
        "REGISTER sip:ims.example.com SIP/2.0\r\n"
        "Via: SIP/2.0/UDP 10.0.0.1:5060;branch=z9hG4bK776asdhds\r\n"
        "Max-Forwards: 70\r\n"
        "To: <sip:user@ims.example.com>\r\n"
        "From: <sip:user@ims.example.com>;tag=1928301774\r\n"
        "Call-ID: a84b4c76e66710@10.0.0.1\r\n"
        "CSeq: 1 REGISTER\r\n"
        "Contact: <sip:user@10.0.0.1:5060>;expires=120;+sip.instance=\"urn:uuid:00000000-0000-0000-0000-000000000001\";reg-id=3\r\n"
        "Expires: 3600\r\n"
        "Authorization: Digest username=\"user@ims.example.com\", realm=\"ims.example.com\", nonce=\"abc\", uri=\"sip:ims.example.com\", response=\"deadbeef\"\r\n"
        "Content-Length: 0\r\n"
        "\r\n";

    auto result = SipMessage::parse(kMsg);
    ASSERT_TRUE(result.has_value()) << result.error().message;

    EXPECT_EQ(result->from_uri(), std::optional<std::string>{"sip:user@ims.example.com"});
    EXPECT_EQ(result->to_uri(), std::optional<std::string>{"sip:user@ims.example.com"});
    EXPECT_EQ(result->contact_uri(), std::optional<std::string>{"sip:user@10.0.0.1:5060"});
    EXPECT_EQ(result->contact_param("+sip.instance"),
              std::optional<std::string>{"urn:uuid:00000000-0000-0000-0000-000000000001"});
    EXPECT_EQ(result->contact_param("reg-id"), std::optional<std::string>{"3"});
    EXPECT_EQ(result->contact_expires(), std::optional<uint32_t>{120});
    EXPECT_EQ(result->expires_value(), std::optional<uint32_t>{3600});
    EXPECT_EQ(result->impu_from_to(), std::optional<std::string>{"sip:user@ims.example.com"});
    EXPECT_EQ(result->impi_from_authorization_or_from(),
              std::optional<std::string>{"user@ims.example.com"});
}

TEST_F(SipMessageTest, WildcardContactIsDetected) {
    static constexpr auto kMsg =
        "REGISTER sip:ims.example.com SIP/2.0\r\n"
        "Via: SIP/2.0/UDP 10.0.0.1:5060;branch=z9hG4bK776asdhds\r\n"
        "Max-Forwards: 70\r\n"
        "To: <sip:user@ims.example.com>\r\n"
        "From: <sip:user@ims.example.com>;tag=1928301774\r\n"
        "Call-ID: a84b4c76e66710@10.0.0.1\r\n"
        "CSeq: 2 REGISTER\r\n"
        "Contact: *\r\n"
        "Expires: 0\r\n"
        "Content-Length: 0\r\n"
        "\r\n";

    auto result = SipMessage::parse(kMsg);
    ASSERT_TRUE(result.has_value()) << result.error().message;

    EXPECT_TRUE(result->is_wildcard_contact());
    EXPECT_EQ(result->contact_uri(), std::optional<std::string>{"*"});
    EXPECT_EQ(result->contact_expires(), std::nullopt);
}

