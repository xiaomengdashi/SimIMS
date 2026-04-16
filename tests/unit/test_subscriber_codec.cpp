#include "db/subscriber_codec.hpp"

#include <bson/bson.h>
#include <gtest/gtest.h>

#include <cstdint>
#include <optional>
#include <string>

namespace {

struct AuthFields {
    std::string operator_code_type = "opc";
    std::optional<std::string> opc = "cd63cb71954a9f4e48a5994e37a02baf";
    std::optional<std::string> op = "";
    bool include_sqn = true;
    bool sqn_is_string = false;
    int64_t sqn_value = 0x1234;
};

auto make_subscriber_document(const AuthFields& auth_fields = {},
                             bool include_associated_impus = true,
                             std::optional<std::string> legacy_tel = std::nullopt) -> bson_t {
    bson_t document;
    bson_init(&document);

    if (legacy_tel) {
        BSON_APPEND_UTF8(&document, "tel", legacy_tel->c_str());
    }

    bson_t identities;
    BSON_APPEND_DOCUMENT_BEGIN(&document, "identities", &identities);
    BSON_APPEND_UTF8(&identities, "impi", "001010000000001@ims.mnc001.mcc001.3gppnetwork.org");
    BSON_APPEND_UTF8(&identities, "canonical_impu", "sip:alice@ims.mnc001.mcc001.3gppnetwork.org");

    if (include_associated_impus) {
        bson_t associated_impus;
        BSON_APPEND_ARRAY_BEGIN(&identities, "associated_impus", &associated_impus);
        BSON_APPEND_UTF8(&associated_impus, "0", "sip:alice@ims.mnc001.mcc001.3gppnetwork.org");
        BSON_APPEND_UTF8(&associated_impus, "1", "tel:+8613800000001");
        bson_append_array_end(&identities, &associated_impus);
    }

    BSON_APPEND_UTF8(&identities, "realm", "ims.mnc001.mcc001.3gppnetwork.org");
    bson_append_document_end(&document, &identities);

    bson_t auth;
    BSON_APPEND_DOCUMENT_BEGIN(&document, "auth", &auth);
    BSON_APPEND_UTF8(&auth, "password", "testpass");
    BSON_APPEND_UTF8(&auth, "ki", "465b5ce8b199b49faa5f0a2ee238a6bc");
    BSON_APPEND_UTF8(&auth, "operator_code_type", auth_fields.operator_code_type.c_str());
    if (auth_fields.opc) {
        BSON_APPEND_UTF8(&auth, "opc", auth_fields.opc->c_str());
    }
    if (auth_fields.op) {
        BSON_APPEND_UTF8(&auth, "op", auth_fields.op->c_str());
    }
    if (auth_fields.include_sqn) {
        if (auth_fields.sqn_is_string) {
            BSON_APPEND_UTF8(&auth, "sqn", "not-an-integer");
        } else {
            BSON_APPEND_INT64(&auth, "sqn", auth_fields.sqn_value);
        }
    }
    BSON_APPEND_UTF8(&auth, "amf", "8000");
    bson_append_document_end(&document, &auth);

    bson_t serving;
    BSON_APPEND_DOCUMENT_BEGIN(&document, "serving", &serving);
    BSON_APPEND_UTF8(
        &serving, "assigned_scscf", "sip:scscf1.ims.mnc001.mcc001.3gppnetwork.org:5062;transport=udp");
    bson_append_document_end(&document, &serving);

    return document;
}

} // namespace

TEST(SubscriberCodecTest, DecodeSubscriberDocumentMapsIdentityAndAuthFields) {
    auto document = make_subscriber_document();

    auto decoded = ims::db::decodeSubscriber(document);

    ASSERT_TRUE(decoded.has_value()) << decoded.error().message;
    EXPECT_EQ(decoded->identities.impi, "001010000000001@ims.mnc001.mcc001.3gppnetwork.org");
    EXPECT_EQ(decoded->identities.canonical_impu, "sip:alice@ims.mnc001.mcc001.3gppnetwork.org");
    ASSERT_EQ(decoded->identities.associated_impus.size(), 2u);
    EXPECT_EQ(decoded->identities.associated_impus[0], "sip:alice@ims.mnc001.mcc001.3gppnetwork.org");
    EXPECT_EQ(decoded->identities.associated_impus[1], "tel:+8613800000001");
    EXPECT_EQ(decoded->identities.realm, "ims.mnc001.mcc001.3gppnetwork.org");

    EXPECT_EQ(decoded->auth.ki, "465b5ce8b199b49faa5f0a2ee238a6bc");
    EXPECT_EQ(decoded->auth.operator_code_type, "opc");
    EXPECT_EQ(decoded->auth.opc, "cd63cb71954a9f4e48a5994e37a02baf");
    EXPECT_EQ(decoded->auth.op, "");
    EXPECT_EQ(decoded->auth.sqn, 0x1234ULL);
    EXPECT_EQ(decoded->auth.amf, "8000");

    EXPECT_EQ(decoded->serving.assigned_scscf,
              "sip:scscf1.ims.mnc001.mcc001.3gppnetwork.org:5062;transport=udp");
    EXPECT_TRUE(decoded->profile.ifcs.empty());

    bson_destroy(&document);
}

TEST(SubscriberCodecTest, DecodeSubscriberAllowsOpTypeWithoutOpc) {
    auto document = make_subscriber_document(AuthFields{
        .operator_code_type = "op",
        .opc = std::nullopt,
        .op = "cdc202d5123e20f62b6d676ac72cb318",
    });

    auto decoded = ims::db::decodeSubscriber(document);

    ASSERT_TRUE(decoded.has_value()) << decoded.error().message;
    EXPECT_EQ(decoded->auth.operator_code_type, "op");
    EXPECT_EQ(decoded->auth.opc, "");
    EXPECT_EQ(decoded->auth.op, "cdc202d5123e20f62b6d676ac72cb318");

    bson_destroy(&document);
}

TEST(SubscriberCodecTest, DecodeSubscriberAllowsOpcTypeWithoutOpField) {
    auto document = make_subscriber_document(AuthFields{
        .operator_code_type = "opc",
        .opc = "cd63cb71954a9f4e48a5994e37a02baf",
        .op = std::nullopt,
    });

    auto decoded = ims::db::decodeSubscriber(document);

    ASSERT_TRUE(decoded.has_value()) << decoded.error().message;
    EXPECT_EQ(decoded->auth.operator_code_type, "opc");
    EXPECT_EQ(decoded->auth.opc, "cd63cb71954a9f4e48a5994e37a02baf");
    EXPECT_EQ(decoded->auth.op, "");

    bson_destroy(&document);
}

TEST(SubscriberCodecTest, DecodeSubscriberRejectsOpcTypeWhenOpcMissing) {
    auto document = make_subscriber_document(AuthFields{
        .operator_code_type = "opc",
        .opc = std::nullopt,
        .op = std::nullopt,
    });

    auto decoded = ims::db::decodeSubscriber(document);

    ASSERT_FALSE(decoded.has_value());
    EXPECT_EQ(decoded.error().code, ims::ErrorCode::kConfigInvalidValue);

    bson_destroy(&document);
}

TEST(SubscriberCodecTest, DecodeSubscriberRejectsOpTypeWhenOpMissing) {
    auto document = make_subscriber_document(AuthFields{
        .operator_code_type = "op",
        .opc = std::nullopt,
        .op = std::nullopt,
    });

    auto decoded = ims::db::decodeSubscriber(document);

    ASSERT_FALSE(decoded.has_value());
    EXPECT_EQ(decoded.error().code, ims::ErrorCode::kConfigInvalidValue);

    bson_destroy(&document);
}

TEST(SubscriberCodecTest, DecodeSubscriberRejectsNonIntegerSqn) {
    auto document = make_subscriber_document(AuthFields{
        .operator_code_type = "opc",
        .sqn_is_string = true,
    });

    auto decoded = ims::db::decodeSubscriber(document);

    ASSERT_FALSE(decoded.has_value());
    EXPECT_EQ(decoded.error().code, ims::ErrorCode::kConfigInvalidValue);

    bson_destroy(&document);
}

TEST(SubscriberCodecTest, DecodeSubscriberRejectsMissingSqn) {
    auto document = make_subscriber_document(AuthFields{
        .operator_code_type = "opc",
        .include_sqn = false,
    });

    auto decoded = ims::db::decodeSubscriber(document);

    ASSERT_FALSE(decoded.has_value());
    EXPECT_EQ(decoded.error().code, ims::ErrorCode::kConfigInvalidValue);

    bson_destroy(&document);
}

TEST(SubscriberCodecTest, DecodeSubscriberRejectsNegativeSqn) {
    auto document = make_subscriber_document(AuthFields{
        .operator_code_type = "opc",
        .sqn_value = -1,
    });

    auto decoded = ims::db::decodeSubscriber(document);

    ASSERT_FALSE(decoded.has_value());
    EXPECT_EQ(decoded.error().code, ims::ErrorCode::kConfigInvalidValue);

    bson_destroy(&document);
}

TEST(SubscriberCodecTest, NextSqnApplies48BitMask) {
    constexpr uint64_t current = 0x0000FFFFFFFFFFF0ULL;
    constexpr uint64_t step = 0x40ULL;
    constexpr uint64_t mask = 0x0000FFFFFFFFFFFFULL;

    auto next = ims::db::nextSqn(current, step, mask);

    EXPECT_EQ(next, 0x30ULL);
}

TEST(SubscriberCodecTest, DecodeSubscriberBuildsAssociatedImpusFromLegacyTelWhenMissing) {
    auto document = make_subscriber_document(AuthFields{}, false, std::string("+8613800000001"));

    auto decoded = ims::db::decodeSubscriber(document);

    ASSERT_TRUE(decoded.has_value()) << decoded.error().message;
    EXPECT_EQ(decoded->identities.canonical_impu, "sip:alice@ims.mnc001.mcc001.3gppnetwork.org");
    ASSERT_EQ(decoded->identities.associated_impus.size(), 3u);
    EXPECT_EQ(decoded->identities.associated_impus[0], "tel:+8613800000001");
    EXPECT_EQ(decoded->identities.associated_impus[1], "sip:+8613800000001@ims.mnc001.mcc001.3gppnetwork.org");
    EXPECT_EQ(decoded->identities.associated_impus[2], "sip:alice@ims.mnc001.mcc001.3gppnetwork.org");

    bson_destroy(&document);
}
