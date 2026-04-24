#pragma once

#include "diameter/ihss_client.hpp"
#include "diameter/types.hpp"
#include "i_auth_provider.hpp"

#include <chrono>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

namespace ims::scscf {

class ImsAkaAuthProvider final : public IAuthProvider {
public:
    ImsAkaAuthProvider(std::shared_ptr<ims::diameter::IHssClient> hss,
                       std::string domain);

    auto canHandleInitialRegister(const ims::sip::SipMessage& request) const -> bool override;
    auto canHandleAuthorization(const ims::sip::SipMessage& request) const -> bool override;

    auto createChallenge(const ims::sip::SipMessage& request) -> Result<AuthChallenge> override;
    auto verifyAuthorization(const ims::sip::SipMessage& request)
        -> Result<AuthVerificationResult> override;

private:
    struct PendingAuth {
        ims::diameter::AuthVector vector;
        std::string impi;
        std::string impu;
        std::string scheme;
        std::chrono::steady_clock::time_point expires_at;
    };

    auto extractImpi(const ims::sip::SipMessage& msg) const -> std::string;
    auto extractImpu(const ims::sip::SipMessage& msg) const -> std::string;
    auto makePendingKey(std::string_view call_id,
                        std::string_view impi,
                        std::string_view impu) const -> std::string;
    auto findPendingForAuthorizationLocked(std::string_view call_id,
                                           std::string_view impi) const
        -> std::unordered_map<std::string, PendingAuth>::const_iterator;
    auto normalizeAuthorizationImpi(std::string_view username) const -> std::string;
    void purgeExpiredLocked(std::chrono::steady_clock::time_point now) const;

    std::shared_ptr<ims::diameter::IHssClient> hss_;
    std::string domain_;
    mutable std::unordered_map<std::string, PendingAuth> pending_auth_;
    mutable std::mutex auth_mutex_;
};

} // namespace ims::scscf
