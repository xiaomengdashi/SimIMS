#pragma once

#include "common/types.hpp"

#include <chrono>
#include <optional>
#include <string>
#include <unordered_set>
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

struct ContactBindingSelector {
    std::string normalized_contact_uri;
    std::string instance_id;
    std::string reg_id;

    [[nodiscard]] auto uses_instance_and_reg_id() const -> bool {
        return !instance_id.empty() && !reg_id.empty();
    }
};

struct ContactBatchUpsert {
    std::unordered_set<std::string> impus;
    ContactBindingSelector selector;
    ContactBinding contact;
    std::string impi;
    std::string scscf_uri;
    RegistrationBinding::State state = RegistrationBinding::State::kRegistered;
    bool require_existing_match = false;
    bool reject_older_cseq = false;
};

struct ContactBatchRemove {
    std::unordered_set<std::string> impus;
    ContactBindingSelector selector;
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

    /// Atomically upsert or refresh a single contact within an IMPU binding.
    virtual auto upsertContact(std::string_view impu,
                               const ContactBindingSelector& selector,
                               const ContactBinding& contact,
                               std::string_view impi,
                               std::string_view scscf_uri,
                               RegistrationBinding::State state,
                               bool require_existing_match = false,
                               bool reject_older_cseq = false) -> Result<bool> = 0;

    /// Atomically remove a single contact within an IMPU binding.
    virtual auto removeContact(std::string_view impu,
                               const ContactBindingSelector& selector) -> Result<bool> = 0;

    /// Atomically upsert or refresh the same contact across associated IMPU bindings.
    virtual auto upsertContacts(const ContactBatchUpsert& batch) -> Result<size_t> = 0;

    /// Atomically remove one contact across associated IMPU bindings.
    virtual auto removeContacts(const ContactBatchRemove& batch) -> Result<size_t> = 0;

    /// Atomically remove associated IMPU bindings.
    virtual auto removeBindings(const std::unordered_set<std::string>& impus) -> Result<size_t> = 0;

    /// Remove registration binding
    virtual auto remove(std::string_view impu) -> VoidResult = 0;

    /// Remove expired contacts, return count of removed entries
    virtual auto purgeExpired() -> Result<size_t> = 0;

    /// Check if an IMPU is currently registered
    virtual auto isRegistered(std::string_view impu) -> Result<bool> = 0;
};

} // namespace ims::registration
