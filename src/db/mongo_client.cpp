#include "db/mongo_client.hpp"

#include <bson/bson.h>

namespace ims::db {

std::mutex MongoClient::lifecycle_mutex_;
size_t MongoClient::lifecycle_ref_count_ = 0;

MongoClient::MongoClient(mongoc_client_t* client, mongoc_collection_t* collection)
    : client_(client)
    , collection_(collection) {}

MongoClient::~MongoClient() {
    if (collection_ != nullptr) {
        mongoc_collection_destroy(collection_);
        collection_ = nullptr;
    }
    if (client_ != nullptr) {
        mongoc_client_destroy(client_);
        client_ = nullptr;
    }
    release_driver_lifecycle();
}

auto MongoClient::create(const HssAdapterConfig& config) -> Result<std::shared_ptr<MongoClient>> {
    if (config.mongo_uri.empty() || config.mongo_db.empty() || config.mongo_collection.empty()) {
        return std::unexpected(ErrorInfo{
            ErrorCode::kConfigInvalidValue,
            "Mongo config must not be empty",
            "mongo_uri/mongo_db/mongo_collection",
        });
    }

    acquire_driver_lifecycle();

    auto* client = mongoc_client_new(config.mongo_uri.c_str());
    if (client == nullptr) {
        release_driver_lifecycle();
        return std::unexpected(ErrorInfo{
            ErrorCode::kInternalError,
            "Failed to create Mongo client",
            config.mongo_uri,
        });
    }

    bson_t ping_command;
    bson_t ping_reply;
    bson_error_t ping_error;
    bson_init(&ping_command);
    bson_init(&ping_reply);
    BSON_APPEND_INT32(&ping_command, "ping", 1);

    const bool ping_ok = mongoc_client_command_simple(
        client,
        config.mongo_db.c_str(),
        &ping_command,
        nullptr,
        &ping_reply,
        &ping_error);
    bson_destroy(&ping_reply);
    bson_destroy(&ping_command);

    if (!ping_ok) {
        mongoc_client_destroy(client);
        release_driver_lifecycle();
        return std::unexpected(ErrorInfo{
            ErrorCode::kInternalError,
            "Failed to connect Mongo server",
            ping_error.message,
        });
    }

    auto* collection =
        mongoc_client_get_collection(client, config.mongo_db.c_str(), config.mongo_collection.c_str());
    if (collection == nullptr) {
        mongoc_client_destroy(client);
        release_driver_lifecycle();
        return std::unexpected(ErrorInfo{
            ErrorCode::kConfigInvalidValue,
            "Failed to get Mongo collection",
            config.mongo_db + "." + config.mongo_collection,
        });
    }

    return std::shared_ptr<MongoClient>(new MongoClient(client, collection));
}

void MongoClient::acquire_driver_lifecycle() {
    std::lock_guard<std::mutex> lock(lifecycle_mutex_);
    if (lifecycle_ref_count_++ == 0) {
        mongoc_init();
    }
}

void MongoClient::release_driver_lifecycle() {
    std::lock_guard<std::mutex> lock(lifecycle_mutex_);
    if (lifecycle_ref_count_ == 0) {
        return;
    }
    if (--lifecycle_ref_count_ == 0) {
        mongoc_cleanup();
    }
}

auto MongoClient::client() const -> mongoc_client_t* {
    return client_;
}

auto MongoClient::collection() const -> mongoc_collection_t* {
    return collection_;
}

} // namespace ims::db
