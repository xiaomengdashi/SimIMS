#include "ims_aka_auth_provider.hpp"

#include "auth_manager.hpp"
#include "sip/uri_utils.hpp"

#include <algorithm>
#include <format>

namespace ims::scscf {
namespace {

constexpr auto kPendingAuthTtl = std::chrono::seconds(120);

} // namespace

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
    auto auth_header = request.getHeader("Authorization");
    if (!auth_header) {
        return false;
    }

    auto params = AuthManager::parseAuthorization(*auth_header);
    if (!params) {
        return false;
    }

    {
        std::lock_guard lock(auth_mutex_);
        purgeExpiredLocked(std::chrono::steady_clock::now());
        if (findPendingForAuthorizationLocked(request.callId(), params->username) != pending_auth_.end()) {
            return true;
        }
    }

    return params->algorithm.empty()
        || params->algorithm == "AKAv1-MD5"
        || params->algorithm == "Digest-AKAv1-MD5";
}

auto ImsAkaAuthProvider::createChallenge(const ims::sip::SipMessage& request) -> Result<AuthChallenge> {
    auto impi = extractImpi(request);
    auto impu = extractImpu(request);
    auto call_id = request.callId();
    auto key = makePendingKey(call_id, impi, impu);

    auto build_challenge = [&](const PendingAuth& pending) {
        return AuthChallenge{
            .session_key = call_id,
            .impi = pending.impi,
            .impu = pending.impu,
            .scheme = pending.scheme,
            .realm = domain_,
            .www_authenticate = AuthManager::buildChallenge(pending.vector, domain_, pending.scheme),
        };
    };

    {
        std::lock_guard lock(auth_mutex_);
        auto now = std::chrono::steady_clock::now();
        purgeExpiredLocked(now);
        if (auto it = pending_auth_.find(key); it != pending_auth_.end()) {
            return build_challenge(it->second);
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

    std::lock_guard lock(auth_mutex_);
    auto now = std::chrono::steady_clock::now();
    purgeExpiredLocked(now);
    if (auto it = pending_auth_.find(key); it != pending_auth_.end()) {
        return build_challenge(it->second);
    }

    auto pending = PendingAuth{
        .vector = maa->auth_vector,
        .impi = std::move(impi),
        .impu = std::move(impu),
        .scheme = maa->sip_auth_scheme,
        .expires_at = now + kPendingAuthTtl,
    };
    auto challenge = build_challenge(pending);
    pending_auth_[key] = std::move(pending);
    return challenge;
}

auto ImsAkaAuthProvider::verifyAuthorization(const ims::sip::SipMessage& request)
    -> Result<AuthVerificationResult> {
    auto auth_header = request.getHeader("Authorization");
    if (!auth_header) {
        return std::unexpected(ErrorInfo{ErrorCode::kDiameterAuthFailed, "missing Authorization header"});
    }

    auto call_id = request.callId();
    auto params = AuthManager::parseAuthorization(*auth_header);
    if (!params) {
        return std::unexpected(params.error());
    }

    PendingAuth pending;
    std::string key;
    {
        std::lock_guard lock(auth_mutex_);
        purgeExpiredLocked(std::chrono::steady_clock::now());
        auto it = findPendingForAuthorizationLocked(call_id, params->username);
        if (it == pending_auth_.end()) {
            return std::unexpected(ErrorInfo{ErrorCode::kDiameterAuthFailed, "no pending auth state"});
        }
        key = it->first;
        pending = it->second;
    }

    if (!AuthManager::verifyResponse(*auth_header, pending.vector, request.method(), pending.scheme)) {
        return std::unexpected(ErrorInfo{ErrorCode::kDiameterAuthFailed, "authorization verification failed"});
    }

    {
        std::lock_guard lock(auth_mutex_);
        pending_auth_.erase(key);
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

auto ImsAkaAuthProvider::makePendingKey(std::string_view call_id,
                                        std::string_view impi,
                                        std::string_view impu) const -> std::string {
    return std::format("{}\x1f{}\x1f{}",
                       call_id,
                       ims::sip::normalize_impu_uri(std::string(impi)),
                       ims::sip::normalize_impu_uri(std::string(impu)));
}

auto ImsAkaAuthProvider::findPendingForAuthorizationLocked(std::string_view call_id,
                                                           std::string_view impi) const
    -> std::unordered_map<std::string, PendingAuth>::const_iterator {
    auto normalized_impi = normalizeAuthorizationImpi(impi);
    return std::find_if(pending_auth_.begin(), pending_auth_.end(), [&](const auto& entry) {
        return entry.second.impi == normalized_impi
            && entry.first.rfind(std::string(call_id) + "\x1f", 0) == 0;
    });
}

auto ImsAkaAuthProvider::normalizeAuthorizationImpi(std::string_view username) const -> std::string {
    auto normalized = ims::sip::normalize_impu_uri(std::string(username));
    if (normalized.find('@') == std::string::npos
        && normalized.rfind("tel:", 0) != 0
        && normalized.rfind("sip:", 0) != 0
        && normalized.rfind("sips:", 0) != 0) {
        return std::format("{}@{}", normalized, domain_);
    }
    return normalized;
}

void ImsAkaAuthProvider::purgeExpiredLocked(std::chrono::steady_clock::time_point now) const {
    std::erase_if(pending_auth_, [&](const auto& entry) {
        return entry.second.expires_at <= now;
    });
}

} // namespace ims::scscf
