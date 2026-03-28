#include "s-cscf/auth_manager.hpp"

#include <boost/uuid/detail/md5.hpp>
#include <gtest/gtest.h>
#include <iomanip>
#include <sstream>

using namespace ims::scscf;

namespace {

auto md5Hex(const std::string& input) -> std::string {
    boost::uuids::detail::md5 hash;
    boost::uuids::detail::md5::digest_type digest{};

    hash.process_bytes(input.data(), input.size());
    hash.get_digest(digest);

    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    auto* bytes = reinterpret_cast<const unsigned char*>(digest);
    for (std::size_t i = 0; i < sizeof(digest); ++i) {
        oss << std::setw(2) << static_cast<unsigned int>(bytes[i]);
    }
    return oss.str();
}

auto bytesToHex(const std::vector<uint8_t>& data) -> std::string {
    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    for (auto byte : data) {
        oss << std::setw(2) << static_cast<unsigned int>(byte);
    }
    return oss.str();
}

auto computeDigestResponse(const std::string& username,
                           const std::string& realm,
                           const std::string& secret,
                           const std::string& method,
                           const std::string& uri,
                           const std::string& nonce,
                           const std::string& nc,
                           const std::string& cnonce,
                           const std::string& qop) -> std::string {
    auto ha1 = md5Hex(username + ":" + realm + ":" + secret);
    auto ha2 = md5Hex(method + ":" + uri);
    return md5Hex(ha1 + ":" + nonce + ":" + nc + ":" + cnonce + ":" + qop + ":" + ha2);
}

} // namespace

TEST(AuthManagerTest, ParseAuthorizationWithQopFields) {
    auto parsed = AuthManager::parseAuthorization(
        "Digest username=\"user@ims.example.com\", realm=\"ims.example.com\", "
        "nonce=\"abc\", uri=\"sip:ims.example.com\", response=\"deadbeef\", "
        "algorithm=AKAv1-MD5, qop=auth, nc=00000001, cnonce=\"cafebabe\"");

    ASSERT_TRUE(parsed.has_value());
    EXPECT_EQ(parsed->username, "user@ims.example.com");
    EXPECT_EQ(parsed->realm, "ims.example.com");
    EXPECT_EQ(parsed->qop, "auth");
    EXPECT_EQ(parsed->nc, "00000001");
    EXPECT_EQ(parsed->cnonce, "cafebabe");
}

TEST(AuthManagerTest, VerifyAkaDigestResponseSuccess) {
    ims::diameter::AuthVector av{
        .rand = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
                 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10},
        .autn = {0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18,
                 0x19, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E, 0x1F, 0x20},
        .xres = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF, 0x00, 0x11},
    };

    auto nonce = "AQIDBAUGBwgJCgsMDQ4PEBESExQVFhcYGRobHB0eHyA=";
    auto response = computeDigestResponse(
        "460112024122023@ims.mnc011.mcc460.3gppnetwork.org",
        "ims.mnc011.mcc460.3gppnetwork.org",
        bytesToHex(av.xres),
        "REGISTER",
        "sip:ims.mnc011.mcc460.3gppnetwork.org",
        nonce,
        "00000001",
        "deadbeef",
        "auth");

    auto header =
        "Digest username=\"460112024122023@ims.mnc011.mcc460.3gppnetwork.org\", "
        "realm=\"ims.mnc011.mcc460.3gppnetwork.org\", "
        "nonce=\"" + std::string(nonce) + "\", "
        "uri=\"sip:ims.mnc011.mcc460.3gppnetwork.org\", "
        "response=\"" + response + "\", "
        "algorithm=AKAv1-MD5, qop=auth, nc=00000001, cnonce=\"deadbeef\"";

    EXPECT_TRUE(AuthManager::verifyResponse(header, av, "REGISTER", "Digest-AKAv1-MD5"));
}

TEST(AuthManagerTest, VerifyAkaDigestResponseRejectsNonceMismatch) {
    ims::diameter::AuthVector av{
        .rand = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
                 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10},
        .autn = {0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18,
                 0x19, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E, 0x1F, 0x20},
        .xres = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF, 0x00, 0x11},
    };

    auto header =
        "Digest username=\"460112024122023@ims.mnc011.mcc460.3gppnetwork.org\", "
        "realm=\"ims.mnc011.mcc460.3gppnetwork.org\", "
        "nonce=\"wrong_nonce\", "
        "uri=\"sip:ims.mnc011.mcc460.3gppnetwork.org\", "
        "response=\"425f6e5096624975af0e7695ba7712a8\", "
        "algorithm=AKAv1-MD5, qop=auth, nc=00000001, cnonce=\"deadbeef\"";

    EXPECT_FALSE(AuthManager::verifyResponse(header, av, "REGISTER", "Digest-AKAv1-MD5"));
}

TEST(AuthManagerTest, BuildMd5ChallengeAndVerifyResponseSuccess) {
    ims::diameter::AuthVector av{
        .rand = {0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37,
                 0x38, 0x39, 0x61, 0x62, 0x63, 0x64, 0x65, 0x66},
        .xres = {'t', 'e', 's', 't', 'p', 'a', 's', 's'},
    };

    auto challenge = AuthManager::buildChallenge(av, "ims.example.com", "Digest-MD5");
    EXPECT_NE(challenge.find("algorithm=MD5"), std::string::npos);

    auto nonce = "MDEyMzQ1Njc4OWFiY2RlZg==";
    auto response = computeDigestResponse(
        "testuser",
        "ims.example.com",
        "testpass",
        "REGISTER",
        "sip:ims.example.com",
        nonce,
        "00000001",
        "deadbeef",
        "auth");

    auto header =
        "Digest username=\"testuser\", realm=\"ims.example.com\", "
        "nonce=\"" + std::string(nonce) + "\", uri=\"sip:ims.example.com\", "
        "response=\"" + response + "\", "
        "algorithm=MD5, qop=auth, nc=00000001, cnonce=\"deadbeef\"";

    EXPECT_TRUE(AuthManager::verifyResponse(header, av, "REGISTER", "Digest-MD5"));
}

TEST(AuthManagerTest, VerifyDigestPasswordResponseSuccess) {
    auto response = computeDigestResponse(
        "testuser",
        "ims.example.com",
        "testpass",
        "REGISTER",
        "sip:ims.example.com",
        "nonce-123",
        "00000001",
        "deadbeef",
        "auth");

    auto header =
        "Digest username=\"testuser\", realm=\"ims.example.com\", "
        "nonce=\"nonce-123\", uri=\"sip:ims.example.com\", "
        "response=\"" + response + "\", "
        "algorithm=MD5, qop=auth, nc=00000001, cnonce=\"deadbeef\"";

    EXPECT_TRUE(AuthManager::verifyDigestPassword(
        header, "testpass", "REGISTER", "nonce-123"));
}

TEST(AuthManagerTest, VerifyDigestPasswordRejectsWrongPassword) {
    auto response = computeDigestResponse(
        "testuser",
        "ims.example.com",
        "testpass",
        "REGISTER",
        "sip:ims.example.com",
        "nonce-123",
        "00000001",
        "deadbeef",
        "auth");

    auto header =
        "Digest username=\"testuser\", realm=\"ims.example.com\", "
        "nonce=\"nonce-123\", uri=\"sip:ims.example.com\", "
        "response=\"" + response + "\", "
        "algorithm=MD5, qop=auth, nc=00000001, cnonce=\"deadbeef\"";

    EXPECT_FALSE(AuthManager::verifyDigestPassword(
        header, "wrongpass", "REGISTER", "nonce-123"));
}
