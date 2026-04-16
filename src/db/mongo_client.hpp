#pragma once

#include "common/config.hpp"
#include "common/types.hpp"

#include <mongoc/mongoc.h>

#include <memory>
#include <mutex>
#include <string>

namespace ims::db {

class MongoClient {
public:
    static auto create(const HssAdapterConfig& config) -> Result<std::shared_ptr<MongoClient>>;

    MongoClient(const MongoClient&) = delete;
    auto operator=(const MongoClient&) -> MongoClient& = delete;

    ~MongoClient();

    // Gets a client from the pool. Caller MUST call mongoc_client_pool_push when done.
    [[nodiscard]] auto pop_client() const -> mongoc_client_t*;
    
    // Returns a client to the pool.
    void push_client(mongoc_client_t* client) const;

    [[nodiscard]] auto collection_name() const -> const std::string&;
    [[nodiscard]] auto db_name() const -> const std::string&;

private:
    MongoClient(mongoc_client_pool_t* pool, std::string db_name, std::string collection_name);

    static void acquire_driver_lifecycle();
    static void release_driver_lifecycle();

    mongoc_client_pool_t* pool_ = nullptr;
    std::string db_name_;
    std::string collection_name_;

    static std::mutex lifecycle_mutex_;
    static size_t lifecycle_ref_count_;
};

// RAII helper for client checkout from pool
class MongoClientGuard {
public:
    explicit MongoClientGuard(std::shared_ptr<MongoClient> pool)
        : pool_(std::move(pool))
        , client_(pool_->pop_client()) {}

    ~MongoClientGuard() {
        if (client_ && pool_) {
            pool_->push_client(client_);
        }
    }

    auto client() const -> mongoc_client_t* { return client_; }
    auto collection() const -> mongoc_collection_t* {
        // Create collection handle on the fly using the checked-out client
        return mongoc_client_get_collection(client_, pool_->db_name().c_str(), pool_->collection_name().c_str());
    }

private:
    std::shared_ptr<MongoClient> pool_;
    mongoc_client_t* client_;
};

} // namespace ims::db
