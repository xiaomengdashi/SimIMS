#pragma once

#include "digest_credential_store.hpp"
#include "i_auth_provider.hpp"

#include <chrono>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

namespace ims::scscf {

class DigestAuthProvider final : public IAuthProvider {
public:
    DigestAuthProvider(std::shared_ptr<IDigestCredentialStore> credential_store,
                       std::string realm);

    auto canHandleInitialRegister(const ims::sip::SipMessage& request) const -> bool override;
    auto canHandleAuthorization(const ims::sip::SipMessage& request) const -> bool override;

    auto createChallenge(const ims::sip::SipMessage& request) -> Result<AuthChallenge> override;
    auto verifyAuthorization(const ims::sip::SipMessage& request)
        -> Result<AuthVerificationResult> override;

private:
    struct PendingAuth {
        std::string nonce;
        DigestCredential credential;
        std::chrono::steady_clock::time_point expires_at;
    };

    auto extractIdentity(const ims::sip::SipMessage& request) const -> std::string;
    auto makePendingKey(std::string_view call_id, std::string_view impi) const -> std::string;
    void purgeExpiredLocked(std::chrono::steady_clock::time_point now) const;
    auto makeNonce() const -> std::string;

    std::shared_ptr<IDigestCredentialStore> credential_store_;
    std::string realm_;
    mutable std::unordered_map<std::string, PendingAuth> pending_auth_;
    mutable std::mutex mutex_;
};

} // namespace ims::scscf
