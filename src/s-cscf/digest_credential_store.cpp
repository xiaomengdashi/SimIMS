#include "digest_credential_store.hpp"

#include "sip/uri_utils.hpp"

#include <format>

namespace ims::scscf {

namespace {

auto makeImpi(const ims::HssSubscriberConfig& subscriber) -> std::string {
    return std::format("{}@{}", subscriber.imsi, subscriber.realm);
}

auto makeCanonicalImpu(const ims::HssSubscriberConfig& subscriber) -> std::string {
    return std::format("sip:{}@{}", subscriber.imsi, subscriber.realm);
}

auto makeAssociatedImpus(const ims::HssSubscriberConfig& subscriber) -> std::vector<std::string> {
    return {
        std::format("tel:{}", subscriber.tel),
        std::format("sip:{}@{}", subscriber.tel, subscriber.realm),
        makeCanonicalImpu(subscriber),
    };
}

auto makeUsernameRealmKey(std::string_view username, std::string_view realm) -> std::string {
    return std::format("{}|{}", username, realm);
}

void addUsernameAlias(std::unordered_map<std::string, std::size_t>& by_username_realm,
                      std::string_view username,
                      std::string_view realm,
                      std::size_t index) {
    if (username.empty() || realm.empty()) {
        return;
    }
    by_username_realm.emplace(makeUsernameRealmKey(username, realm), index);
}

} // namespace

LocalDigestCredentialStore::LocalDigestCredentialStore(const ims::HssAdapterConfig& config) {
    credentials_.reserve(config.subscribers.size());

    for (const auto& subscriber : config.subscribers) {
        auto impi = makeImpi(subscriber);
        auto impu = makeCanonicalImpu(subscriber);
        auto associated_impus = makeAssociatedImpus(subscriber);

        DigestCredential credential{
            .username = impi,
            .realm = subscriber.realm,
            .password = subscriber.password,
            .impi = impi,
            .impu = impu,
            .associated_impus = associated_impus,
        };

        auto index = credentials_.size();
        credentials_.push_back(credential);

        addUsernameAlias(by_username_realm_, credential.username, credential.realm, index);
        addUsernameAlias(by_username_realm_, subscriber.imsi, credential.realm, index);
        addUsernameAlias(by_username_realm_, subscriber.tel, credential.realm, index);
        addUsernameAlias(by_username_realm_, std::format("{}@{}", subscriber.tel, subscriber.realm),
                         credential.realm, index);
        by_identity_.emplace(ims::sip::normalize_impu_uri(credential.impi), index);
        by_identity_.emplace(ims::sip::normalize_impu_uri(credential.impu), index);
        for (const auto& identity : associated_impus) {
            by_identity_.emplace(ims::sip::normalize_impu_uri(identity), index);
        }
    }
}

auto LocalDigestCredentialStore::findByUsername(std::string_view username,
                                                std::string_view realm) const
    -> Result<std::optional<DigestCredential>> {
    auto it = by_username_realm_.find(makeUsernameRealmKey(username, realm));
    if (it == by_username_realm_.end()) {
        return std::optional<DigestCredential>{};
    }

    return lookupByIndex(it->second);
}

auto LocalDigestCredentialStore::findByIdentity(std::string_view identity) const
    -> Result<std::optional<DigestCredential>> {
    auto normalized = ims::sip::normalize_impu_uri(std::string(identity));
    auto it = by_identity_.find(normalized);
    if (it == by_identity_.end()) {
        return std::optional<DigestCredential>{};
    }

    return lookupByIndex(it->second);
}

auto LocalDigestCredentialStore::lookupByIndex(std::size_t index) const -> std::optional<DigestCredential> {
    if (index >= credentials_.size()) {
        return std::nullopt;
    }
    return credentials_[index];
}

auto make_local_digest_credential_store(const ims::HssAdapterConfig& config)
    -> std::shared_ptr<IDigestCredentialStore> {
    return std::make_shared<LocalDigestCredentialStore>(config);
}

} // namespace ims::scscf
