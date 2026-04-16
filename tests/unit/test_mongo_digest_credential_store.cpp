#include "s-cscf/mongo_digest_credential_store.hpp"

#include "mocks/mock_subscriber_repository.hpp"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

using namespace ims;
using namespace ims::scscf;
using namespace ims::test;
using ::testing::Return;
using ::testing::StrEq;

namespace {

auto make_record() -> db::SubscriberRecord {
    db::SubscriberRecord record;
    record.identities.impi = "alice@ims.example.com";
    record.identities.canonical_impu = "sip:alice@ims.example.com";
    record.identities.associated_impus = {
        "sip:alice@ims.example.com",
        "tel:+8613800000001",
    };
    record.identities.realm = "ims.example.com";

    record.auth.password = "testpass";
    record.auth.ki = "465b5ce8b199b49faa5f0a2ee238a6bc";
    record.auth.operator_code_type = "opc";
    record.auth.opc = "cd63cb71954a9f4e48a5994e37a02baf";
    record.auth.op = "";
    record.auth.sqn = 0x1234;
    record.auth.amf = "8000";

    return record;
}

} // namespace

TEST(MongoDigestCredentialStoreTest, FindByUsernameMapsSubscriberToDigestCredential) {
    auto repository = std::make_shared<MockSubscriberRepository>();
    auto record = make_record();

    EXPECT_CALL(*repository, findByUsernameRealm(StrEq("alice"), StrEq("ims.example.com")))
        .WillOnce(Return(Result<std::optional<db::SubscriberRecord>>{record}));

    MongoDigestCredentialStore store(repository);
    auto result = store.findByUsername("alice", "ims.example.com");

    ASSERT_TRUE(result.has_value()) << result.error().message;
    ASSERT_TRUE(result->has_value());
    EXPECT_EQ(result->value().username, "alice");
    EXPECT_EQ(result->value().realm, "ims.example.com");
    EXPECT_EQ(result->value().password, "testpass");
    EXPECT_EQ(result->value().impi, "alice@ims.example.com");
    EXPECT_EQ(result->value().impu, "sip:alice@ims.example.com");
    EXPECT_EQ(result->value().associated_impus.size(), 2u);
}

TEST(MongoDigestCredentialStoreTest, FindByIdentityNormalizesAndMapsCredential) {
    auto repository = std::make_shared<MockSubscriberRepository>();
    auto record = make_record();

    EXPECT_CALL(*repository, findByIdentity(StrEq("alice@ims.example.com")))
        .WillOnce(Return(Result<std::optional<db::SubscriberRecord>>{record}));

    MongoDigestCredentialStore store(repository);
    auto result = store.findByIdentity("Alice@ims.example.com");

    ASSERT_TRUE(result.has_value()) << result.error().message;
    ASSERT_TRUE(result->has_value());
    EXPECT_EQ(result->value().username, "alice");
    EXPECT_EQ(result->value().password, "testpass");
}

TEST(MongoDigestCredentialStoreTest, FindByIdentityNormalizesSipUserPhoneIdentity) {
    auto repository = std::make_shared<MockSubscriberRepository>();
    auto record = make_record();

    EXPECT_CALL(*repository, findByIdentity(StrEq("sip:+8613800000001@ims.example.com")))
        .WillOnce(Return(Result<std::optional<db::SubscriberRecord>>{record}));

    MongoDigestCredentialStore store(repository);
    auto result = store.findByIdentity("<sip:+8613800000001@IMS.EXAMPLE.COM;user=phone>");

    ASSERT_TRUE(result.has_value()) << result.error().message;
    ASSERT_TRUE(result->has_value());
    EXPECT_EQ(result->value().username, "alice");
    EXPECT_EQ(result->value().password, "testpass");
}

TEST(MongoDigestCredentialStoreTest, FindByUsernameReturnsEmptyWhenNotFound) {
    auto repository = std::make_shared<MockSubscriberRepository>();

    EXPECT_CALL(*repository, findByUsernameRealm(StrEq("unknown"), StrEq("ims.example.com")))
        .WillOnce(Return(Result<std::optional<db::SubscriberRecord>>{std::nullopt}));

    MongoDigestCredentialStore store(repository);
    auto result = store.findByUsername("unknown", "ims.example.com");

    ASSERT_TRUE(result.has_value()) << result.error().message;
    EXPECT_FALSE(result->has_value());
}

TEST(MongoDigestCredentialStoreTest, FindByUsernameFallsBackToTelAliasesWhenPrimaryLookupMisses) {
    auto repository = std::make_shared<MockSubscriberRepository>();
    auto record = make_record();

    EXPECT_CALL(*repository, findByUsernameRealm(StrEq("+8613800000001"), StrEq("ims.example.com")))
        .WillOnce(Return(Result<std::optional<db::SubscriberRecord>>{std::nullopt}));
    EXPECT_CALL(*repository, findByUsernameRealm(StrEq("tel:+8613800000001"), StrEq("ims.example.com")))
        .WillOnce(Return(Result<std::optional<db::SubscriberRecord>>{record}));

    MongoDigestCredentialStore store(repository);
    auto result = store.findByUsername("+8613800000001", "ims.example.com");

    ASSERT_TRUE(result.has_value()) << result.error().message;
    ASSERT_TRUE(result->has_value());
    EXPECT_EQ(result->value().username, "alice");
    EXPECT_EQ(result->value().realm, "ims.example.com");
}

TEST(MongoDigestCredentialStoreTest, FindByIdentityPropagatesRepositoryError) {
    auto repository = std::make_shared<MockSubscriberRepository>();

    EXPECT_CALL(*repository, findByIdentity(StrEq("sip:alice@ims.example.com")))
        .WillOnce(Return(std::unexpected(ErrorInfo{
            ErrorCode::kInternalError,
            "db failure",
            "mongo",
        })));

    MongoDigestCredentialStore store(repository);
    auto result = store.findByIdentity("sip:alice@ims.example.com");

    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, ErrorCode::kInternalError);
}

TEST(MongoDigestCredentialStoreFactoryTest, ReturnsErrorWhenMongoUnavailable) {
    HssAdapterConfig config;
    config.mongo_uri = "mongodb://127.0.0.1:1";
    config.mongo_db = "simims";
    config.mongo_collection = "subscribers";

    auto store = make_mongo_digest_credential_store(config);

    ASSERT_FALSE(store.has_value());
}
