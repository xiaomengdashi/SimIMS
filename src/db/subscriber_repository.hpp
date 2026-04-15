#pragma once

#include "common/types.hpp"
#include "diameter/types.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ims::db {

struct SubscriberIdentities {
    std::string impi;
    std::string canonical_impu;
    std::vector<std::string> associated_impus;
    std::string username;
    std::string realm;
};

struct SubscriberAuth {
    std::string password;
    std::string ki;
    std::string operator_code_type;
    std::string opc;
    std::string op;
    uint64_t sqn = 0;
    std::string amf = "8000";
};

struct SubscriberProfile {
    std::vector<ims::diameter::InitialFilterCriteria> ifcs;
};

struct SubscriberServing {
    std::string assigned_scscf;
};

struct SubscriberRecord {
    SubscriberIdentities identities;
    SubscriberAuth auth;
    SubscriberProfile profile;
    SubscriberServing serving;
};

struct ISubscriberRepository {
    virtual ~ISubscriberRepository() = default;

    virtual auto findByImpiOrImpu(std::string_view impi,
                                  std::string_view impu) const
        -> Result<std::optional<SubscriberRecord>> = 0;

    virtual auto findByIdentity(std::string_view identity) const
        -> Result<std::optional<SubscriberRecord>> = 0;

    virtual auto findByUsernameRealm(std::string_view username,
                                     std::string_view realm) const
        -> Result<std::optional<SubscriberRecord>> = 0;

    virtual auto setSqn(std::string_view impi, uint64_t sqn) -> VoidResult = 0;

    virtual auto incrementSqn(std::string_view impi,
                              uint64_t step,
                              uint64_t mask) -> VoidResult = 0;

    // Atomically updates auth.sqn to (old + step) & mask and returns old auth.sqn.
    virtual auto advanceSqn(std::string_view impi,
                            uint64_t step,
                            uint64_t mask) -> Result<uint64_t> = 0;

    virtual auto setServingScscf(std::string_view impi,
                                 std::string_view scscf_uri) -> VoidResult = 0;
};

} // namespace ims::db
