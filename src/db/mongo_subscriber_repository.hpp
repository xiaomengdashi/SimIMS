#pragma once

#include "common/config.hpp"
#include "db/mongo_client.hpp"
#include "db/subscriber_repository.hpp"

#include <memory>
#include <string_view>

namespace ims::db {

class MongoSubscriberRepository final : public ISubscriberRepository {
public:
    static auto create(const HssAdapterConfig& config)
        -> Result<std::shared_ptr<MongoSubscriberRepository>>;

    explicit MongoSubscriberRepository(std::shared_ptr<MongoClient> client);

    auto findByImpiOrImpu(std::string_view impi,
                          std::string_view impu) const
        -> Result<std::optional<SubscriberRecord>> override;

    auto findByIdentity(std::string_view identity) const
        -> Result<std::optional<SubscriberRecord>> override;

    auto findByUsernameRealm(std::string_view username,
                             std::string_view realm) const
        -> Result<std::optional<SubscriberRecord>> override;

    auto setSqn(std::string_view impi, uint64_t sqn) -> VoidResult override;

    auto incrementSqn(std::string_view impi,
                      uint64_t step,
                      uint64_t mask) -> VoidResult override;

    auto advanceSqn(std::string_view impi,
                    uint64_t step,
                    uint64_t mask) -> Result<uint64_t> override;

    auto setServingScscf(std::string_view impi,
                         std::string_view scscf_uri) -> VoidResult override;

private:
    auto find_one_by_field(std::string_view field, std::string_view value) const
        -> Result<std::optional<SubscriberRecord>>;

    auto find_one_with_filter(const bson_t& filter) const
        -> Result<std::optional<SubscriberRecord>>;

    auto update_one_with_filter(const bson_t& filter, const bson_t& update) -> VoidResult;

    std::shared_ptr<MongoClient> client_;
};

} // namespace ims::db
