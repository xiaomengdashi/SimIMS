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
    auto remove(std::string_view impu) -> VoidResult override;
    auto purgeExpired() -> Result<size_t> override;
    auto isRegistered(std::string_view impu) -> Result<bool> override;

private:
    mutable std::mutex mutex_;
    std::unordered_map<std::string, RegistrationBinding> bindings_;
};

} // namespace ims::registration
