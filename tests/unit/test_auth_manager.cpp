#include "s-cscf/auth_manager.hpp"

#include <gtest/gtest.h>

using namespace ims::scscf;

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

    auto header =
        "Digest username=\"460112024122023@ims.mnc011.mcc460.3gppnetwork.org\", "
        "realm=\"ims.mnc011.mcc460.3gppnetwork.org\", "
        "nonce=\"AQIDBAUGBwgJCgsMDQ4PEBESExQVFhcYGRobHB0eHyA=\", "
        "uri=\"sip:ims.mnc011.mcc460.3gppnetwork.org\", "
        "response=\"425f6e5096624975af0e7695ba7712a8\", "
        "algorithm=AKAv1-MD5, qop=auth, nc=00000001, cnonce=\"deadbeef\"";

    EXPECT_TRUE(AuthManager::verifyResponse(header, av, "REGISTER"));
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

    EXPECT_FALSE(AuthManager::verifyResponse(header, av, "REGISTER"));
}
