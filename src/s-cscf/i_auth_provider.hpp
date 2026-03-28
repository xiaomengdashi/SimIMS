#pragma once

#include "common/types.hpp"
#include "sip/message.hpp"

#include <string>
#include <vector>

namespace ims::scscf {

struct AuthChallenge {
    std::string session_key;
    std::string impi;
    std::string impu;
    std::string scheme;
    std::string realm;
    std::string www_authenticate;
};

struct AuthVerificationResult {
    std::string session_key;
    std::string impi;
    std::string impu;
    std::string scheme;
};

class IAuthProvider {
public:
    virtual ~IAuthProvider() = default;

    virtual auto canHandleInitialRegister(const ims::sip::SipMessage& request) const -> bool = 0;
    virtual auto canHandleAuthorization(const ims::sip::SipMessage& request) const -> bool = 0;

    virtual auto createChallenge(const ims::sip::SipMessage& request) -> Result<AuthChallenge> = 0;
    virtual auto verifyAuthorization(const ims::sip::SipMessage& request)
        -> Result<AuthVerificationResult> = 0;
};

} // namespace ims::scscf
