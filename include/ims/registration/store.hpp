#pragma once

#include "ims/common/types.hpp"

#include <chrono>
#include <optional>
#include <string>
#include <vector>

namespace ims::registration {

/// Contact binding from a REGISTER request
struct ContactBinding {
    std::string contact_uri;     // Contact header URI
    std::string instance_id;     // +sip.instance feature tag
    std::string reg_id;          // reg-id parameter
    std::string path;            // Path header value (P-CSCF route)
    TimePoint expires;           // Expiration time
    uint16_t q_value = 1000;     // q-value * 1000 (priority)
    std::string user_agent;      // User-Agent header
    std::string call_id;         // Call-ID of REGISTER
    uint32_t cseq = 0;          // CSeq of REGISTER
};

/// Full registration record for an IMPU
struct RegistrationBinding {
    std::string impu;            // IMS Public User Identity (sip:user@domain)
    std::string impi;            // IMS Private User Identity
    std::string scscf_uri;       // Serving S-CSCF URI
    std::vector<ContactBinding> contacts;  // Active contact bindings

    enum class State {
        kRegistered,
        kUnregistered,
        kNotRegistered,
    } state = State::kNotRegistered;

    /// Get non-expired contacts
    auto active_contacts() const -> std::vector<const ContactBinding*> {
        auto now = std::chrono::steady_clock::now();
        std::vector<const ContactBinding*> result;
        for (const auto& c : contacts) {
            if (c.expires > now) {
                result.push_back(&c);
            }
        }
        return result;
    }
};

/// Abstract registration storage interface
///
/// Supports in-memory (development) and Redis (production) backends.
/// Used by S-CSCF Registrar to manage UE registration bindings.
struct IRegistrationStore {
    virtual ~IRegistrationStore() = default;

    /// Store or update a registration binding
    virtual auto store(const RegistrationBinding& binding) -> VoidResult = 0;

    /// Look up registration by IMPU
    virtual auto lookup(std::string_view impu) -> Result<RegistrationBinding> = 0;

    /// Remove registration binding
    virtual auto remove(std::string_view impu) -> VoidResult = 0;

    /// Remove expired contacts, return count of removed entries
    virtual auto purgeExpired() -> Result<size_t> = 0;

    /// Check if an IMPU is currently registered
    virtual auto isRegistered(std::string_view impu) -> Result<bool> = 0;
};

} // namespace ims::registration
