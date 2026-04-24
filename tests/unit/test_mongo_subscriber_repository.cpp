#include "db/mongo_subscriber_repository.hpp"

#include <bson/bson.h>
#include <gtest/gtest.h>

#include <mongoc/mongoc.h>

#include <memory>

namespace {

constexpr auto kMongoUri = "mongodb://127.0.0.1:27017";
constexpr auto kMongoDb = "simims";
constexpr auto kMongoCollection = "subscribers_repository_test";

class MongoGlobalInit {
public:
    MongoGlobalInit() {
        mongoc_init();
    }

    ~MongoGlobalInit() {
        mongoc_cleanup();
    }
};

auto global_init() -> MongoGlobalInit& {
    static MongoGlobalInit init;
    return init;
}

auto make_test_repository() -> std::shared_ptr<ims::db::MongoSubscriberRepository> {
    (void)global_init();

    ims::HssAdapterConfig config;
    config.mongo_uri = kMongoUri;
    config.mongo_db = kMongoDb;
    config.mongo_collection = kMongoCollection;

    auto repository = ims::db::MongoSubscriberRepository::create(config);
    if (!repository) {
        return {};
    }
    return *repository;
}

auto make_subscriber_document_without_username() -> bson_t {
    bson_t document;
    bson_init(&document);
    BSON_APPEND_UTF8(&document, "imsi", "alice");
    BSON_APPEND_UTF8(&document, "tel", "+8613800000001");

    bson_t identities;
    BSON_APPEND_DOCUMENT_BEGIN(&document, "identities", &identities);
    BSON_APPEND_UTF8(&identities, "impi", "alice@ims.example.com");
    BSON_APPEND_UTF8(&identities, "canonical_impu", "sip:alice@ims.example.com");

    bson_t associated_impus;
    BSON_APPEND_ARRAY_BEGIN(&identities, "associated_impus", &associated_impus);
    BSON_APPEND_UTF8(&associated_impus, "0", "sip:alice@ims.example.com");
    bson_append_array_end(&identities, &associated_impus);

    BSON_APPEND_UTF8(&identities, "realm", "ims.example.com");
    bson_append_document_end(&document, &identities);

    bson_t auth;
    BSON_APPEND_DOCUMENT_BEGIN(&document, "auth", &auth);
    BSON_APPEND_UTF8(&auth, "password", "testpass");
    BSON_APPEND_UTF8(&auth, "ki", "465b5ce8b199b49faa5f0a2ee238a6bc");
    BSON_APPEND_UTF8(&auth, "operator_code_type", "opc");
    BSON_APPEND_UTF8(&auth, "opc", "cd63cb71954a9f4e48a5994e37a02baf");
    BSON_APPEND_UTF8(&auth, "op", "");
    BSON_APPEND_INT64(&auth, "sqn", 0x1234);
    BSON_APPEND_UTF8(&auth, "amf", "8000");
    bson_append_document_end(&document, &auth);

    return document;
}

auto make_subscriber_document_with_aliases() -> bson_t {
    bson_t document;
    bson_init(&document);
    BSON_APPEND_UTF8(&document, "imsi", "alice");
    BSON_APPEND_UTF8(&document, "tel", "+8613800000001");

    bson_t identities;
    BSON_APPEND_DOCUMENT_BEGIN(&document, "identities", &identities);
    BSON_APPEND_UTF8(&identities, "impi", "alice@ims.example.com");
    BSON_APPEND_UTF8(&identities, "canonical_impu", "sip:alice@ims.example.com");

    bson_t associated_impus;
    BSON_APPEND_ARRAY_BEGIN(&identities, "associated_impus", &associated_impus);
    BSON_APPEND_UTF8(&associated_impus, "0", "sip:alice@ims.example.com");
    BSON_APPEND_UTF8(&associated_impus, "1", "sip:+8613800000001@ims.example.com");
    BSON_APPEND_UTF8(&associated_impus, "2", "tel:+8613800000001");
    bson_append_array_end(&identities, &associated_impus);

    BSON_APPEND_UTF8(&identities, "realm", "ims.example.com");
    bson_append_document_end(&document, &identities);

    bson_t auth;
    BSON_APPEND_DOCUMENT_BEGIN(&document, "auth", &auth);
    BSON_APPEND_UTF8(&auth, "password", "testpass");
    BSON_APPEND_UTF8(&auth, "ki", "465b5ce8b199b49faa5f0a2ee238a6bc");
    BSON_APPEND_UTF8(&auth, "operator_code_type", "opc");
    BSON_APPEND_UTF8(&auth, "opc", "cd63cb71954a9f4e48a5994e37a02baf");
    BSON_APPEND_UTF8(&auth, "op", "");
    BSON_APPEND_INT64(&auth, "sqn", 0x1234);
    BSON_APPEND_UTF8(&auth, "amf", "8000");
    bson_append_document_end(&document, &auth);

    return document;
}

void reset_collection(mongoc_collection_t* collection) {
    bson_error_t error;
    mongoc_collection_drop(collection, &error);
}

} // namespace

TEST(MongoSubscriberRepositoryTest, FindByUsernameRealmUsesTopLevelImsi) {
    auto repository = make_test_repository();
    if (!repository) {
        GTEST_SKIP() << "Mongo unavailable in test environment";
    }

    auto seed_client = std::unique_ptr<mongoc_client_t, decltype(&mongoc_client_destroy)>(
        mongoc_client_new(kMongoUri), mongoc_client_destroy);
    ASSERT_NE(seed_client.get(), nullptr);

    auto seed_collection = std::unique_ptr<mongoc_collection_t, decltype(&mongoc_collection_destroy)>(
        mongoc_client_get_collection(seed_client.get(), kMongoDb, kMongoCollection), mongoc_collection_destroy);
    ASSERT_NE(seed_collection.get(), nullptr);

    reset_collection(seed_collection.get());

    auto document = make_subscriber_document_without_username();
    bson_error_t insert_error;
    ASSERT_TRUE(mongoc_collection_insert_one(seed_collection.get(), &document, nullptr, nullptr, &insert_error))
        << insert_error.message;
    bson_destroy(&document);

    auto result = repository->findByUsernameRealm("alice", "ims.example.com");

    ASSERT_TRUE(result.has_value()) << result.error().message;
    ASSERT_TRUE(result->has_value());
    EXPECT_EQ(result->value().identities.impi, "alice@ims.example.com");

    reset_collection(seed_collection.get());
}

TEST(MongoSubscriberRepositoryTest, FindByImpiOrImpuPrefersImsi) {
    auto repository = make_test_repository();
    if (!repository) {
        GTEST_SKIP() << "Mongo unavailable in test environment";
    }

    auto seed_client = std::unique_ptr<mongoc_client_t, decltype(&mongoc_client_destroy)>(
        mongoc_client_new(kMongoUri), mongoc_client_destroy);
    ASSERT_NE(seed_client.get(), nullptr);

    auto seed_collection = std::unique_ptr<mongoc_collection_t, decltype(&mongoc_collection_destroy)>(
        mongoc_client_get_collection(seed_client.get(), kMongoDb, kMongoCollection), mongoc_collection_destroy);
    ASSERT_NE(seed_collection.get(), nullptr);

    reset_collection(seed_collection.get());

    auto document = make_subscriber_document_with_aliases();
    bson_error_t insert_error;
    ASSERT_TRUE(mongoc_collection_insert_one(seed_collection.get(), &document, nullptr, nullptr, &insert_error))
        << insert_error.message;
    bson_destroy(&document);

    auto result = repository->findByImpiOrImpu("alice@ims.example.com", "sip:other@ims.example.com");

    ASSERT_TRUE(result.has_value()) << result.error().message;
    ASSERT_TRUE(result->has_value());
    EXPECT_EQ(result->value().imsi, "alice");

    reset_collection(seed_collection.get());
}

TEST(MongoSubscriberRepositoryTest, AdvanceSqnReturnsNotFoundForMissingSubscriber) {
    auto repository = make_test_repository();
    if (!repository) {
        GTEST_SKIP() << "Mongo unavailable in test environment";
    }

    auto result = repository->advanceSqn("non-existent-impi@ims.local", 32, 0x0000FFFFFFFFFFFFULL);

    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, ims::ErrorCode::kDiameterUserNotFound);
}

TEST(MongoSubscriberRepositoryTest, SetServingScscfReturnsNotFoundForMissingSubscriber) {
    auto repository = make_test_repository();
    if (!repository) {
        GTEST_SKIP() << "Mongo unavailable in test environment";
    }

    auto result = repository->setServingScscf("non-existent-impi@ims.local", "sip:scscf.ims.local");

    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, ims::ErrorCode::kDiameterUserNotFound);
}

TEST(MongoSubscriberRepositoryTest, FindByIdentityNormalizesSipUserPhoneAlias) {
    auto repository = make_test_repository();
    if (!repository) {
        GTEST_SKIP() << "Mongo unavailable in test environment";
    }

    auto seed_client = std::unique_ptr<mongoc_client_t, decltype(&mongoc_client_destroy)>(
        mongoc_client_new(kMongoUri), mongoc_client_destroy);
    ASSERT_NE(seed_client.get(), nullptr);

    auto seed_collection = std::unique_ptr<mongoc_collection_t, decltype(&mongoc_collection_destroy)>(
        mongoc_client_get_collection(seed_client.get(), kMongoDb, kMongoCollection), mongoc_collection_destroy);
    ASSERT_NE(seed_collection.get(), nullptr);

    reset_collection(seed_collection.get());

    auto document = make_subscriber_document_with_aliases();
    bson_error_t insert_error;
    ASSERT_TRUE(mongoc_collection_insert_one(seed_collection.get(), &document, nullptr, nullptr, &insert_error))
        << insert_error.message;
    bson_destroy(&document);

    auto result = repository->findByIdentity("<sip:+8613800000001@IMS.EXAMPLE.COM;user=phone>");

    ASSERT_TRUE(result.has_value()) << result.error().message;
    ASSERT_TRUE(result->has_value());
    EXPECT_EQ(result->value().tel, "+8613800000001");
    EXPECT_EQ(result->value().identities.impi, "alice@ims.example.com");

    reset_collection(seed_collection.get());
}

TEST(MongoSubscriberRepositoryTest, FindByIdentityNormalizesTelWithPhoneContext) {
    auto repository = make_test_repository();
    if (!repository) {
        GTEST_SKIP() << "Mongo unavailable in test environment";
    }

    auto seed_client = std::unique_ptr<mongoc_client_t, decltype(&mongoc_client_destroy)>(
        mongoc_client_new(kMongoUri), mongoc_client_destroy);
    ASSERT_NE(seed_client.get(), nullptr);

    auto seed_collection = std::unique_ptr<mongoc_collection_t, decltype(&mongoc_collection_destroy)>(
        mongoc_client_get_collection(seed_client.get(), kMongoDb, kMongoCollection), mongoc_collection_destroy);
    ASSERT_NE(seed_collection.get(), nullptr);

    reset_collection(seed_collection.get());

    auto document = make_subscriber_document_with_aliases();
    bson_error_t insert_error;
    ASSERT_TRUE(mongoc_collection_insert_one(seed_collection.get(), &document, nullptr, nullptr, &insert_error))
        << insert_error.message;
    bson_destroy(&document);

    auto result = repository->findByIdentity("tel:+8613800000001;phone-context=ims.example.com");

    ASSERT_TRUE(result.has_value()) << result.error().message;
    ASSERT_TRUE(result->has_value());
    EXPECT_EQ(result->value().tel, "+8613800000001");
    EXPECT_EQ(result->value().identities.impi, "alice@ims.example.com");

    reset_collection(seed_collection.get());
}
