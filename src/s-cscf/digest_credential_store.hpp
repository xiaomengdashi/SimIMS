#pragma once

#include "common/config.hpp"
#include "common/types.hpp"

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace ims::scscf {

struct DigestCredential {
    std::string username;
    std::string realm;
    std::string password;
    std::string impi;
    std::string impu;
    std::vector<std::string> associated_impus;
};

class IDigestCredentialStore {
public:
    virtual ~IDigestCredentialStore() = default;

    virtual auto findByUsername(std::string_view username,
                                std::string_view realm) const
        -> Result<std::optional<DigestCredential>> = 0;

    virtual auto findByIdentity(std::string_view identity) const
        -> Result<std::optional<DigestCredential>> = 0;
};

class LocalDigestCredentialStore final : public IDigestCredentialStore {
public:
    explicit LocalDigestCredentialStore(const ims::HssAdapterConfig& config);

    auto findByUsername(std::string_view username,
                        std::string_view realm) const
        -> Result<std::optional<DigestCredential>> override;

    auto findByIdentity(std::string_view identity) const
        -> Result<std::optional<DigestCredential>> override;

private:
    auto lookupByIndex(std::size_t index) const -> std::optional<DigestCredential>;

    std::vector<DigestCredential> credentials_;
    std::unordered_map<std::string, std::size_t> by_username_realm_;
    std::unordered_map<std::string, std::size_t> by_identity_;
};

auto make_local_digest_credential_store(const ims::HssAdapterConfig& config)
    -> std::shared_ptr<IDigestCredentialStore>;

} // namespace ims::scscf
