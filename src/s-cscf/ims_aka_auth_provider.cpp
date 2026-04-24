#include "ims_aka_auth_provider.hpp"

#include "auth_manager.hpp"
#include "sip/uri_utils.hpp"

#include <format>

namespace ims::scscf {

ImsAkaAuthProvider::ImsAkaAuthProvider(std::shared_ptr<ims::diameter::IHssClient> hss,
                                       std::string domain)
    : hss_(std::move(hss))
    , domain_(std::move(domain))
{
}

auto ImsAkaAuthProvider::canHandleInitialRegister(const ims::sip::SipMessage& request) const -> bool {
    return request.method() == "REGISTER";
}

auto ImsAkaAuthProvider::canHandleAuthorization(const ims::sip::SipMessage& request) const -> bool {
    {
        std::lock_guard lock(auth_mutex_);
        if (pending_auth_.contains(request.callId())) {
            return true;
        }
    }

    auto auth_header = request.getHeader("Authorization");
    if (!auth_header) {
        return false;
    }

    auto params = AuthManager::parseAuthorization(*auth_header);
    if (!params) {
        return false;
    }

    return params->algorithm.empty()
        || params->algorithm == "AKAv1-MD5"
        || params->algorithm == "Digest-AKAv1-MD5";
}

auto ImsAkaAuthProvider::createChallenge(const ims::sip::SipMessage& request) -> Result<AuthChallenge> {
    auto impi = extractImpi(request);
    auto impu = extractImpu(request);
    auto call_id = request.callId();

    {
        std::lock_guard lock(auth_mutex_);
        auto it = pending_auth_.find(call_id);
        if (it != pending_auth_.end()) {
            return AuthChallenge{
                .session_key = call_id,
                .impi = it->second.impi,
                .impu = it->second.impu,
                .scheme = it->second.scheme,
                .realm = domain_,
                .www_authenticate = AuthManager::buildChallenge(
                    it->second.vector, domain_, it->second.scheme),
            };
        }
    }

    ims::diameter::MarParams mar{
        .impi = impi,
        .impu = impu,
        .sip_auth_scheme = "Digest-AKAv1-MD5",
        .server_name = std::format("sip:scscf.{}", domain_),
    };

    auto maa = hss_->multimediaAuth(mar);
    if (!maa) {
        return std::unexpected(maa.error());
    }

    {
        std::lock_guard lock(auth_mutex_);
        pending_auth_[call_id] = PendingAuth{
            .vector = maa->auth_vector,
            .impi = impi,
            .impu = impu,
            .scheme = maa->sip_auth_scheme,
        };
    }

    return AuthChallenge{
        .session_key = call_id,
        .impi = impi,
        .impu = impu,
        .scheme = maa->sip_auth_scheme,
        .realm = domain_,
        .www_authenticate = AuthManager::buildChallenge(maa->auth_vector, domain_, maa->sip_auth_scheme),
    };
}

auto ImsAkaAuthProvider::verifyAuthorization(const ims::sip::SipMessage& request)
    -> Result<AuthVerificationResult> {
    auto auth_header = request.getHeader("Authorization");
    if (!auth_header) {
        return std::unexpected(ErrorInfo{ErrorCode::kDiameterAuthFailed, "missing Authorization header"});
    }

    auto call_id = request.callId();

    PendingAuth pending;
    {
        std::lock_guard lock(auth_mutex_);
        auto it = pending_auth_.find(call_id);
        if (it == pending_auth_.end()) {
            return std::unexpected(ErrorInfo{ErrorCode::kDiameterAuthFailed, "no pending auth state"});
        }
        pending = it->second;
        pending_auth_.erase(it);
    }

    if (!AuthManager::verifyResponse(*auth_header, pending.vector, request.method(), pending.scheme)) {
        return std::unexpected(ErrorInfo{ErrorCode::kDiameterAuthFailed, "authorization verification failed"});
    }

    return AuthVerificationResult{
        .session_key = call_id,
        .impi = pending.impi,
        .impu = pending.impu,
        .scheme = pending.scheme,
    };
}

auto ImsAkaAuthProvider::extractImpi(const ims::sip::SipMessage& msg) const -> std::string {
    if (auto impi = msg.impi_from_authorization_or_from()) {
        return ims::sip::normalize_impu_uri(*impi);
    }
    return ims::sip::normalize_impu_uri(msg.fromHeader());
}

auto ImsAkaAuthProvider::extractImpu(const ims::sip::SipMessage& msg) const -> std::string {
    if (auto impu = msg.impu_from_to()) {
        return ims::sip::normalize_impu_uri(*impu);
    }
    return ims::sip::normalize_impu_uri(msg.toHeader());
}

} // namespace ims::scscf
