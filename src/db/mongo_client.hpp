#pragma once

#include "common/config.hpp"
#include "common/types.hpp"

#include <mongoc/mongoc.h>

#include <memory>
#include <mutex>

namespace ims::db {

class MongoClient {
public:
    static auto create(const HssAdapterConfig& config) -> Result<std::shared_ptr<MongoClient>>;

    MongoClient(const MongoClient&) = delete;
    auto operator=(const MongoClient&) -> MongoClient& = delete;

    ~MongoClient();

    [[nodiscard]] auto client() const -> mongoc_client_t*;
    [[nodiscard]] auto collection() const -> mongoc_collection_t*;

private:
    MongoClient(mongoc_client_t* client, mongoc_collection_t* collection);

    static void acquire_driver_lifecycle();
    static void release_driver_lifecycle();

    mongoc_client_t* client_ = nullptr;
    mongoc_collection_t* collection_ = nullptr;

    static std::mutex lifecycle_mutex_;
    static size_t lifecycle_ref_count_;
};

} // namespace ims::db
