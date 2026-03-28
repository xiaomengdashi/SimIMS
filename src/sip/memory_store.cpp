#include "memory_store.hpp"
#include "common/logger.hpp"

#include <algorithm>

namespace ims::registration {

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
