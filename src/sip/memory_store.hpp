#pragma once

#include "sip/store.hpp"

#include <mutex>
#include <unordered_map>

namespace ims::registration {

/// In-memory registration store for development and testing.
/// Not suitable for production (no persistence, no clustering).
class MemoryRegistrationStore : public IRegistrationStore {
public:
    MemoryRegistrationStore() = default;

    auto store(const RegistrationBinding& binding) -> VoidResult override;
    auto lookup(std::string_view impu) -> Result<RegistrationBinding> override;
    auto upsertContact(std::string_view impu,
                       const ContactBindingSelector& selector,
                       const ContactBinding& contact,
                       std::string_view impi,
                       std::string_view scscf_uri,
                       RegistrationBinding::State state,
                       bool require_existing_match = false,
                       bool reject_older_cseq = false) -> Result<bool> override;
    auto removeContact(std::string_view impu,
                       const ContactBindingSelector& selector) -> Result<bool> override;
    auto remove(std::string_view impu) -> VoidResult override;
    auto purgeExpired() -> Result<size_t> override;
    auto isRegistered(std::string_view impu) -> Result<bool> override;

private:
    static auto normalizeContactUri(std::string_view contact_uri) -> std::string;
    static auto matchesSelector(const ContactBinding& contact,
                                const ContactBindingSelector& selector) -> bool;

    auto pruneExpiredLocked(std::string_view impu)
        -> std::unordered_map<std::string, RegistrationBinding>::iterator;

    mutable std::mutex mutex_;
    std::unordered_map<std::string, RegistrationBinding> bindings_;
};

} // namespace ims::registration
