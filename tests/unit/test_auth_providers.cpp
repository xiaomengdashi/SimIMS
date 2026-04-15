#include "../mocks/mock_hss_client.hpp"
#include "../mocks/mock_subscriber_repository.hpp"
#include "s-cscf/auth_manager.hpp"
#include "s-cscf/digest_auth_provider.hpp"
#include "s-cscf/digest_credential_store.hpp"
#include "s-cscf/ims_aka_auth_provider.hpp"
#include "s-cscf/mongo_digest_credential_store.hpp"
#include "sip/message.hpp"

#include <boost/uuid/detail/md5.hpp>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <iomanip>
#include <optional>
#include <sstream>

namespace {

using ::testing::_;
using ::testing::Return;
using ::testing::StrEq;

auto makeRegister(const std::string& request_uri,
                  const std::string& from,
                  const std::string& to,
                  const std::string& call_id) -> ims::sip::SipMessage {
    auto request = ims::sip::createRequest("REGISTER", request_uri);
    EXPECT_TRUE(request.has_value()) << request.error().message;

    request->setFromHeader(from);
    request->setToHeader(to);
    request->setCallId(call_id);
    request->setCSeq(1, "REGISTER");
    request->addVia("SIP/2.0/UDP 127.0.0.1:5060;branch=z9hG4bK-test");
    request->setContact("<sip:user@127.0.0.1:5060>");
    return std::move(*request);
}

auto makeRegisterWithAuthorization(const std::string& request_uri,
                                   const std::string& from,
                                   const std::string& to,
                                   const std::string& call_id,
                                   const std::string& authorization) -> ims::sip::SipMessage {
    auto raw = std::string{}
        + "REGISTER " + request_uri + " SIP/2.0\r\n"
        + "Via: SIP/2.0/UDP 127.0.0.1:5060;branch=z9hG4bK-test\r\n"
        + "From: " + from + "\r\n"
        + "To: " + to + "\r\n"
        + "Call-ID: " + call_id + "\r\n"
        + "CSeq: 1 REGISTER\r\n"
        + "Contact: <sip:user@127.0.0.1:5060>\r\n"
        + "Authorization: " + authorization + "\r\n"
        + "Content-Length: 0\r\n"
        + "\r\n";

    auto parsed = ims::sip::SipMessage::parse(raw);
    EXPECT_TRUE(parsed.has_value()) << parsed.error().message;
    return std::move(*parsed);
}

auto extractNonce(std::string_view challenge) -> std::string {
    auto start = challenge.find("nonce=\"");
    EXPECT_NE(start, std::string_view::npos);
    start += 7;
    auto end = challenge.find('"', start);
    EXPECT_NE(end, std::string_view::npos);
    return std::string(challenge.substr(start, end - start));
}

auto md5Hex(const std::string& input) -> std::string {
    boost::uuids::detail::md5 hash;
    boost::uuids::detail::md5::digest_type digest{};

    hash.process_bytes(input.data(), input.size());
    hash.get_digest(digest);

    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    for (auto word : digest) {
        for (int shift = 24; shift >= 0; shift -= 8) {
            oss << std::setw(2) << ((word >> shift) & 0xFF);
        }
    }
    return oss.str();
}

auto computeDigestResponse(const std::string& username,
                           const std::string& realm,
                           const std::string& password,
                           const std::string& method,
                           const std::string& uri,
                           const std::string& nonce,
                           const std::string& nc,
                           const std::string& cnonce,
                           const std::string& qop) -> std::string {
    auto ha1 = md5Hex(username + ":" + realm + ":" + password);
    auto ha2 = md5Hex(method + ":" + uri);
    return md5Hex(ha1 + ":" + nonce + ":" + nc + ":" + cnonce + ":" + qop + ":" + ha2);
}

TEST(DigestAuthProviderTest, ChallengeAndVerifyAuthorizationSuccess) {
    auto repository = std::make_shared<ims::test::MockSubscriberRepository>();
    ims::db::SubscriberRecord record;
    record.identities.impi = "460112024122023@ims.example.com";
    record.identities.canonical_impu = "sip:460112024122023@ims.example.com";
    record.identities.associated_impus = {
        "sip:460112024122023@ims.example.com",
        "tel:13824122023",
    };
    record.identities.realm = "ims.example.com";
    record.auth.password = "testpass";

    EXPECT_CALL(*repository, findByIdentity(StrEq("460112024122023@ims.example.com")))
        .WillRepeatedly(Return(ims::Result<std::optional<ims::db::SubscriberRecord>>{record}));
    EXPECT_CALL(*repository,
                findByUsernameRealm(StrEq("460112024122023@ims.example.com"),
                                    StrEq("ims.example.com")))
        .WillRepeatedly(Return(ims::Result<std::optional<ims::db::SubscriberRecord>>{record}));

    auto credential_store = std::make_shared<ims::scscf::MongoDigestCredentialStore>(repository);
    ims::scscf::DigestAuthProvider provider(credential_store, "ims.example.com");

    auto first_register = makeRegister("sip:ims.example.com",
                                       "<sip:460112024122023@ims.example.com>;tag=from-1",
                                       "<sip:460112024122023@ims.example.com>",
                                       "call-digest-1");

    ASSERT_TRUE(provider.canHandleInitialRegister(first_register));
    auto challenge = provider.createChallenge(first_register);
    ASSERT_TRUE(challenge.has_value()) << challenge.error().message;
    EXPECT_EQ(challenge->scheme, "Digest-MD5");

    auto nonce = extractNonce(challenge->www_authenticate);
    auto second_register = makeRegister("sip:ims.example.com",
                                        "<sip:460112024122023@ims.example.com>;tag=from-1",
                                        "<sip:460112024122023@ims.example.com>",
                                        "call-digest-1");
    auto final_response = computeDigestResponse("460112024122023@ims.example.com",
                                                "ims.example.com",
                                                "testpass",
                                                "REGISTER",
                                                "sip:ims.example.com",
                                                nonce,
                                                "00000001",
                                                "deadbeef",
                                                "auth");
    second_register = makeRegisterWithAuthorization(
        "sip:ims.example.com",
        "<sip:460112024122023@ims.example.com>;tag=from-1",
        "<sip:460112024122023@ims.example.com>",
        "call-digest-1",
        "Digest username=\"460112024122023@ims.example.com\", realm=\"ims.example.com\", "
        "nonce=\"" + nonce + "\", uri=\"sip:ims.example.com\", "
        "response=\"" + final_response + "\", algorithm=MD5, qop=auth, nc=00000001, cnonce=\"deadbeef\"");

    EXPECT_TRUE(provider.canHandleAuthorization(second_register));
    auto verified = provider.verifyAuthorization(second_register);
    ASSERT_TRUE(verified.has_value()) << verified.error().message;
    EXPECT_EQ(verified->impi, "460112024122023@ims.example.com");
    EXPECT_EQ(verified->impu, "sip:460112024122023@ims.example.com");
}

TEST(ImsAkaAuthProviderTest, PendingStateKeepsDigestMd5RegistrationFlow) {
    auto hss = std::make_shared<ims::test::MockHssClient>();
    ims::scscf::ImsAkaAuthProvider provider(hss, "ims.example.com");

    ims::diameter::AuthVector av{
        .rand = {0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37,
                 0x38, 0x39, 0x61, 0x62, 0x63, 0x64, 0x65, 0x66},
        .xres = {'t', 'e', 's', 't', 'p', 'a', 's', 's'},
    };

    EXPECT_CALL(*hss, multimediaAuth(_))
        .WillOnce(Return(ims::Result<ims::diameter::MaaResult>{
            ims::diameter::MaaResult{
                .result_code = 2001,
                .sip_auth_scheme = "Digest-MD5",
                .auth_vector = av,
            }}));

    auto first_register = makeRegister("sip:ims.example.com",
                                       "<sip:testuser@ims.example.com>;tag=from-1",
                                       "<sip:testuser@ims.example.com>",
                                       "call-aka-1");

    auto challenge = provider.createChallenge(first_register);
    ASSERT_TRUE(challenge.has_value()) << challenge.error().message;
    EXPECT_NE(challenge->www_authenticate.find("algorithm=MD5"), std::string::npos);

    auto response = computeDigestResponse("testuser",
                                          "ims.example.com",
                                          "testpass",
                                          "REGISTER",
                                          "sip:ims.example.com",
                                          "MDEyMzQ1Njc4OWFiY2RlZg==",
                                          "00000001",
                                          "deadbeef",
                                          "auth");
    auto second_register = makeRegisterWithAuthorization(
        "sip:ims.example.com",
        "<sip:testuser@ims.example.com>;tag=from-1",
        "<sip:testuser@ims.example.com>",
        "call-aka-1",
        "Digest username=\"testuser\", realm=\"ims.example.com\", "
        "nonce=\"MDEyMzQ1Njc4OWFiY2RlZg==\", uri=\"sip:ims.example.com\", "
        "response=\"" + response + "\", "
        "algorithm=MD5, qop=auth, nc=00000001, cnonce=\"deadbeef\"");

    EXPECT_TRUE(provider.canHandleAuthorization(second_register));
    auto verified = provider.verifyAuthorization(second_register);
    ASSERT_TRUE(verified.has_value()) << verified.error().message;
    EXPECT_EQ(verified->scheme, "Digest-MD5");
    EXPECT_EQ(verified->impu, "sip:testuser@ims.example.com");
}

TEST(ImsAkaAuthProviderTest, AcceptsDigestAkaSchemeAliasInAuthorization) {
    auto hss = std::make_shared<ims::test::MockHssClient>();
    ims::scscf::ImsAkaAuthProvider provider(hss, "ims.example.com");

    ims::diameter::AuthVector av{
        .rand = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
                 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10},
        .autn = {0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18,
                 0x19, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E, 0x1F, 0x20},
        .xres = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF, 0x00, 0x11},
    };

    EXPECT_CALL(*hss, multimediaAuth(_))
        .WillOnce(Return(ims::Result<ims::diameter::MaaResult>{
            ims::diameter::MaaResult{
                .result_code = 2001,
                .sip_auth_scheme = "Digest-AKAv1-MD5",
                .auth_vector = av,
            }}));

    auto first_register = makeRegister("sip:ims.example.com",
                                       "<sip:testuser@ims.example.com>;tag=from-1",
                                       "<sip:testuser@ims.example.com>",
                                       "call-aka-2");

    auto challenge = provider.createChallenge(first_register);
    ASSERT_TRUE(challenge.has_value()) << challenge.error().message;
    EXPECT_NE(challenge->www_authenticate.find("algorithm=AKAv1-MD5"), std::string::npos);

    auto nonce = extractNonce(challenge->www_authenticate);
    auto response = computeDigestResponse("testuser",
                                          "ims.example.com",
                                          std::string(av.xres.begin(), av.xres.end()),
                                          "REGISTER",
                                          "sip:ims.example.com",
                                          nonce,
                                          "00000001",
                                          "deadbeef",
                                          "auth");
    auto second_register = makeRegisterWithAuthorization(
        "sip:ims.example.com",
        "<sip:testuser@ims.example.com>;tag=from-1",
        "<sip:testuser@ims.example.com>",
        "call-aka-2",
        "Digest username=\"testuser\", realm=\"ims.example.com\", "
        "nonce=\"" + nonce + "\", uri=\"sip:ims.example.com\", "
        "response=\"" + response + "\", "
        "algorithm=AKAv1-MD5, qop=auth, nc=00000001, cnonce=\"deadbeef\"");

    EXPECT_TRUE(provider.canHandleAuthorization(second_register));
    auto verified = provider.verifyAuthorization(second_register);
    ASSERT_TRUE(verified.has_value()) << verified.error().message;
    EXPECT_EQ(verified->scheme, "Digest-AKAv1-MD5");
}

} // namespace
