#include "db/mongo_client.hpp"

#include <bson/bson.h>

namespace ims::db {

std::mutex MongoClient::lifecycle_mutex_;
size_t MongoClient::lifecycle_ref_count_ = 0;

MongoClient::MongoClient(mongoc_client_pool_t* pool, std::string db_name, std::string collection_name)
    : pool_(pool)
    , db_name_(std::move(db_name))
    , collection_name_(std::move(collection_name)) {}

MongoClient::~MongoClient() {
    if (pool_ != nullptr) {
        mongoc_client_pool_destroy(pool_);
        pool_ = nullptr;
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

    mongoc_uri_t* uri = mongoc_uri_new_with_error(config.mongo_uri.c_str(), nullptr);
    if (!uri) {
        release_driver_lifecycle();
        return std::unexpected(ErrorInfo{
            ErrorCode::kInternalError,
            "Failed to parse Mongo URI",
            config.mongo_uri,
        });
    }

    mongoc_client_pool_t* pool = mongoc_client_pool_new(uri);
    mongoc_uri_destroy(uri);
    
    if (pool == nullptr) {
        release_driver_lifecycle();
        return std::unexpected(ErrorInfo{
            ErrorCode::kInternalError,
            "Failed to create Mongo client pool",
            config.mongo_uri,
        });
    }

    // Ping check using a client from the pool
    mongoc_client_t* client = mongoc_client_pool_pop(pool);

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

    mongoc_client_pool_push(pool, client);

    if (!ping_ok) {
        mongoc_client_pool_destroy(pool);
        release_driver_lifecycle();
        return std::unexpected(ErrorInfo{
            ErrorCode::kInternalError,
            "Failed to connect Mongo server",
            ping_error.message,
        });
    }

    return std::shared_ptr<MongoClient>(new MongoClient(pool, config.mongo_db, config.mongo_collection));
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

auto MongoClient::pop_client() const -> mongoc_client_t* {
    return mongoc_client_pool_pop(pool_);
}

void MongoClient::push_client(mongoc_client_t* client) const {
    mongoc_client_pool_push(pool_, client);
}

auto MongoClient::collection_name() const -> const std::string& {
    return collection_name_;
}

auto MongoClient::db_name() const -> const std::string& {
    return db_name_;
}

} // namespace ims::db
