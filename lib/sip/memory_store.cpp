#include "memory_store.hpp"
#include "core/logger.hpp"
#include "sip/uri_utils.hpp"

#include <algorithm>

namespace ims::registration {

auto MemoryRegistrationStore::normalizeContactUri(std::string_view contact_uri) -> std::string {
    return ims::sip::extract_uri_from_name_addr(std::string(contact_uri));
}

auto MemoryRegistrationStore::matchesSelector(const ContactBinding& contact,
                                              const ContactBindingSelector& selector) -> bool
{
    if (selector.uses_instance_and_reg_id()) {
        return contact.instance_id == selector.instance_id
               && contact.reg_id == selector.reg_id;
    }

    return normalizeContactUri(contact.contact_uri) == selector.normalized_contact_uri;
}

auto MemoryRegistrationStore::pruneExpiredLocked(std::string_view impu)
    -> std::unordered_map<std::string, RegistrationBinding>::iterator
{
    auto it = bindings_.find(std::string(impu));
    if (it == bindings_.end()) {
        return it;
    }

    auto now = std::chrono::steady_clock::now();
    auto& binding = it->second;
    auto old_size = binding.contacts.size();
    std::erase_if(binding.contacts, [now](const ContactBinding& contact) {
        return contact.expires <= now;
    });

    if (old_size != binding.contacts.size()) {
        IMS_LOG_DEBUG("Pruned {} expired contacts for IMPU={}",
                      old_size - binding.contacts.size(),
                      binding.impu);
    }

    if (binding.contacts.empty()) {
        IMS_LOG_DEBUG("Removing expired registration for IMPU={}", binding.impu);
        return bindings_.erase(it);
    }

    return it;
}

auto MemoryRegistrationStore::store(const RegistrationBinding& binding) -> VoidResult {
    std::lock_guard lock(mutex_);
    IMS_LOG_DEBUG("Storing registration for IMPU={}", binding.impu);
    bindings_[binding.impu] = binding;
    return {};
}

auto MemoryRegistrationStore::lookup(std::string_view impu)
    -> Result<RegistrationBinding>
{
    std::lock_guard lock(mutex_);
    auto it = pruneExpiredLocked(impu);
    if (it == bindings_.end()) {
        return std::unexpected(ErrorInfo{
            ErrorCode::kRegistrationNotFound,
            std::string("Registration not found for IMPU: ") + std::string(impu)
        });
    }
    return it->second;
}

auto MemoryRegistrationStore::upsertContactLocked(
    std::unordered_map<std::string, RegistrationBinding>& bindings,
    std::string_view impu,
    const ContactBindingSelector& selector,
    const ContactBinding& contact,
    std::string_view impi,
    std::string_view scscf_uri,
    RegistrationBinding::State state,
    bool require_existing_match,
    bool reject_older_cseq) -> bool
{
    auto key = std::string(impu);
    auto it = bindings.find(key);

    if (it == bindings.end()) {
        if (require_existing_match) {
            return false;
        }

        RegistrationBinding binding{
            .impu = key,
            .impi = std::string(impi),
            .scscf_uri = std::string(scscf_uri),
            .contacts = {},
            .state = state,
        };
        auto [inserted_it, inserted] = bindings.emplace(key, std::move(binding));
        (void)inserted;
        it = inserted_it;
    }

    auto& binding = it->second;
    if (!impi.empty()) {
        binding.impi = std::string(impi);
    }
    if (!scscf_uri.empty()) {
        binding.scscf_uri = std::string(scscf_uri);
    }
    binding.state = state;

    auto existing = std::find_if(binding.contacts.begin(), binding.contacts.end(),
                                 [&](const ContactBinding& candidate) {
                                     return matchesSelector(candidate, selector);
                                 });

    if (existing == binding.contacts.end()) {
        if (require_existing_match) {
            return false;
        }
        binding.contacts.push_back(contact);
        return true;
    }

    if (reject_older_cseq && existing->call_id == contact.call_id && contact.cseq < existing->cseq) {
        IMS_LOG_DEBUG("Ignoring stale REGISTER update for IMPU={} call-id={} cseq={} < {}",
                      binding.impu, contact.call_id, contact.cseq, existing->cseq);
        return true;
    }

    *existing = contact;
    return true;
}

auto MemoryRegistrationStore::removeContactLocked(
    std::unordered_map<std::string, RegistrationBinding>& bindings,
    std::string_view impu,
    const ContactBindingSelector& selector,
    std::string_view call_id,
    uint32_t cseq,
    bool reject_older_cseq) -> bool
{
    auto it = bindings.find(std::string(impu));
    if (it == bindings.end()) {
        return false;
    }

    auto& binding = it->second;
    auto old_size = binding.contacts.size();
    std::erase_if(binding.contacts, [&](const ContactBinding& candidate) {
        if (!matchesSelector(candidate, selector)) {
            return false;
        }
        if (reject_older_cseq && candidate.call_id == call_id && cseq < candidate.cseq) {
            IMS_LOG_DEBUG("Ignoring stale REGISTER removal for IMPU={} call-id={} cseq={} < {}",
                          binding.impu, call_id, cseq, candidate.cseq);
            return false;
        }
        return true;
    });

    if (old_size == binding.contacts.size()) {
        return false;
    }

    if (binding.contacts.empty()) {
        bindings.erase(it);
    } else {
        binding.state = RegistrationBinding::State::kRegistered;
    }

    return true;
}

auto MemoryRegistrationStore::upsertContact(std::string_view impu,
                                            const ContactBindingSelector& selector,
                                            const ContactBinding& contact,
                                            std::string_view impi,
                                            std::string_view scscf_uri,
                                            RegistrationBinding::State state,
                                            bool require_existing_match,
                                            bool reject_older_cseq) -> Result<bool>
{
    std::lock_guard lock(mutex_);
    pruneExpiredLocked(impu);
    return upsertContactLocked(bindings_, impu, selector, contact, impi, scscf_uri, state,
                               require_existing_match, reject_older_cseq);
}

auto MemoryRegistrationStore::removeContact(std::string_view impu,
                                            const ContactBindingSelector& selector) -> Result<bool>
{
    std::lock_guard lock(mutex_);
    pruneExpiredLocked(impu);
    return removeContactLocked(bindings_, impu, selector, {}, 0, false);
}

auto MemoryRegistrationStore::upsertContacts(const ContactBatchUpsert& batch) -> Result<size_t>
{
    std::lock_guard lock(mutex_);
    for (const auto& impu : batch.impus) {
        pruneExpiredLocked(impu);
    }

    if (batch.require_existing_match) {
        for (const auto& impu : batch.impus) {
            auto it = bindings_.find(impu);
            if (it == bindings_.end()) {
                return size_t{0};
            }
            const auto& contacts = it->second.contacts;
            const auto existing = std::find_if(contacts.begin(), contacts.end(),
                                               [&](const ContactBinding& candidate) {
                                                   return matchesSelector(candidate, batch.selector);
                                               });
            if (existing == contacts.end()) {
                return size_t{0};
            }
        }
    }

    size_t changed = 0;
    for (const auto& impu : batch.impus) {
        if (!upsertContactLocked(bindings_, impu, batch.selector, batch.contact, batch.impi,
                                 batch.scscf_uri, batch.state, batch.require_existing_match,
                                 batch.reject_older_cseq)) {
            return size_t{0};
        }
        ++changed;
    }

    return changed;
}

auto MemoryRegistrationStore::removeContacts(const ContactBatchRemove& batch) -> Result<size_t>
{
    std::lock_guard lock(mutex_);
    for (const auto& impu : batch.impus) {
        pruneExpiredLocked(impu);
    }

    size_t removed = 0;
    for (const auto& impu : batch.impus) {
        if (removeContactLocked(bindings_, impu, batch.selector, batch.call_id, batch.cseq,
                                batch.reject_older_cseq)) {
            ++removed;
        }
    }

    return removed;
}

auto MemoryRegistrationStore::removeBindings(const std::unordered_set<std::string>& impus) -> Result<size_t>
{
    std::lock_guard lock(mutex_);
    size_t removed = 0;
    for (const auto& impu : impus) {
        if (bindings_.erase(impu) > 0) {
            ++removed;
        }
    }
    return removed;
}

auto MemoryRegistrationStore::remove(std::string_view impu) -> VoidResult {
    std::lock_guard lock(mutex_);
    auto it = bindings_.find(std::string(impu));
    if (it != bindings_.end()) {
        IMS_LOG_DEBUG("Removing registration for IMPU={}", std::string(impu));
        bindings_.erase(it);
    }
    return {};
}

auto MemoryRegistrationStore::purgeExpired() -> Result<size_t> {
    std::lock_guard lock(mutex_);
    auto now = std::chrono::steady_clock::now();
    size_t removed = 0;

    for (auto it = bindings_.begin(); it != bindings_.end();) {
        auto& contacts = it->second.contacts;

        // Remove expired contacts
        auto old_size = contacts.size();
        std::erase_if(contacts, [now](const ContactBinding& c) {
            return c.expires <= now;
        });
        removed += old_size - contacts.size();

        // Remove entire binding if no contacts remain
        if (contacts.empty()) {
            IMS_LOG_DEBUG("Purging empty registration for IMPU={}", it->first);
            it = bindings_.erase(it);
        } else {
            ++it;
        }
    }

    IMS_LOG_DEBUG("Purged {} expired contacts", removed);
    return removed;
}

auto MemoryRegistrationStore::isRegistered(std::string_view impu) -> Result<bool> {
    std::lock_guard lock(mutex_);
    auto it = pruneExpiredLocked(impu);
    if (it == bindings_.end()) {
        return false;
    }
    return it->second.state == RegistrationBinding::State::kRegistered
           && !it->second.contacts.empty();
}

} // namespace ims::registration
