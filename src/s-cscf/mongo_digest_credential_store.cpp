#include "s-cscf/mongo_digest_credential_store.hpp"

#include "db/mongo_subscriber_repository.hpp"
#include "sip/uri_utils.hpp"

#include <algorithm>
#include <format>

namespace ims::scscf {

MongoDigestCredentialStore::MongoDigestCredentialStore(std::shared_ptr<db::ISubscriberRepository> repository)
    : repository_(std::move(repository)) {}

auto MongoDigestCredentialStore::findByUsername(std::string_view username,
                                                std::string_view realm) const
    -> Result<std::optional<DigestCredential>> {
    if (!repository_) {
        return std::unexpected(ErrorInfo{
            ErrorCode::kInternalError,
            "digest credential repository is not initialized",
            "MongoDigestCredentialStore",
        });
    }

    auto try_lookup = [&](std::string_view candidate) -> Result<std::optional<DigestCredential>> {
        auto record = repository_->findByUsernameRealm(candidate, realm);
        if (!record) {
            return std::unexpected(record.error());
        }
        if (!*record) {
            return std::optional<DigestCredential>{};
        }
        return std::optional<DigestCredential>{to_digest_credential(**record)};
    };

    auto credential = try_lookup(username);
    if (!credential) {
        return std::unexpected(credential.error());
    }
    if (*credential) {
        return credential;
    }

    auto username_str = std::string(username);
    if (username_str.find('@') != std::string::npos || username_str.empty() || username_str.front() != '+') {
        return credential;
    }

    return try_lookup(std::format("tel:{}", username));
}

auto MongoDigestCredentialStore::findByIdentity(std::string_view identity) const
    -> Result<std::optional<DigestCredential>> {
    if (!repository_) {
        return std::unexpected(ErrorInfo{
            ErrorCode::kInternalError,
            "digest credential repository is not initialized",
            "MongoDigestCredentialStore",
        });
    }

    auto normalized = ims::sip::normalize_impu_uri(std::string(identity));
    auto record = repository_->findByIdentity(normalized);
    if (!record) {
        return std::unexpected(record.error());
    }
    if (!*record) {
        return std::optional<DigestCredential>{};
    }

    return std::optional<DigestCredential>{to_digest_credential(**record)};
}

auto MongoDigestCredentialStore::to_digest_credential(const db::SubscriberRecord& record) -> DigestCredential {
    DigestCredential credential;
    credential.impi = record.identities.impi;
    credential.impu = record.identities.canonical_impu;
    credential.realm = record.identities.realm;
    credential.associated_impus = record.identities.associated_impus;

    auto username = record.identities.impi;
    if (auto at = username.find('@'); at != std::string::npos) {
        username = username.substr(0, at);
    }
    credential.username = std::move(username);

    credential.password = record.auth.password;
    return credential;
}

auto make_mongo_digest_credential_store(const ims::HssAdapterConfig& config)
    -> Result<std::shared_ptr<IDigestCredentialStore>> {
    auto repository = db::MongoSubscriberRepository::create(config);
    if (!repository) {
        return std::unexpected(repository.error());
    }

    return std::static_pointer_cast<IDigestCredentialStore>(
        std::make_shared<MongoDigestCredentialStore>(*repository));
}

} // namespace ims::scscf
