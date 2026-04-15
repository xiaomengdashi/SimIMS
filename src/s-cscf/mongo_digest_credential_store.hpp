#pragma once

#include "common/config.hpp"
#include "db/subscriber_repository.hpp"
#include "s-cscf/digest_credential_store.hpp"

#include <memory>
#include <optional>
#include <string_view>

namespace ims::scscf {

class MongoDigestCredentialStore final : public IDigestCredentialStore {
public:
    explicit MongoDigestCredentialStore(std::shared_ptr<db::ISubscriberRepository> repository);

    auto findByUsername(std::string_view username,
                        std::string_view realm) const
        -> Result<std::optional<DigestCredential>> override;

    auto findByIdentity(std::string_view identity) const
        -> Result<std::optional<DigestCredential>> override;

private:
    static auto to_digest_credential(const db::SubscriberRecord& record) -> DigestCredential;

    std::shared_ptr<db::ISubscriberRepository> repository_;
};

auto make_mongo_digest_credential_store(const ims::HssAdapterConfig& config)
    -> Result<std::shared_ptr<IDigestCredentialStore>>;

} // namespace ims::scscf
