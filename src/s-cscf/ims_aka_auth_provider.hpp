#pragma once

#include "diameter/ihss_client.hpp"
#include "diameter/types.hpp"
#include "i_auth_provider.hpp"

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
    };

    auto extractImpi(const ims::sip::SipMessage& msg) const -> std::string;
    auto extractImpu(const ims::sip::SipMessage& msg) const -> std::string;

    std::shared_ptr<ims::diameter::IHssClient> hss_;
    std::string domain_;
    std::unordered_map<std::string, PendingAuth> pending_auth_;
    mutable std::mutex auth_mutex_;
};

} // namespace ims::scscf
