#include "digest_auth_provider.hpp"

#include "auth_manager.hpp"
#include "sip/uri_utils.hpp"

#include <format>
#include <random>

namespace ims::scscf {
namespace {

constexpr auto kPendingAuthTtl = std::chrono::seconds(120);

} // namespace

DigestAuthProvider::DigestAuthProvider(std::shared_ptr<IDigestCredentialStore> credential_store,
                                       std::string realm)
    : credential_store_(std::move(credential_store))
    , realm_(std::move(realm))
{
}

auto DigestAuthProvider::canHandleInitialRegister(const ims::sip::SipMessage& request) const -> bool {
    if (!credential_store_ || request.method() != "REGISTER") {
        return false;
    }

    auto identity = extractIdentity(request);
    if (identity.empty()) {
        return false;
    }

    auto credential = credential_store_->findByIdentity(identity);
    return credential.has_value() && credential->has_value();
}

auto DigestAuthProvider::canHandleAuthorization(const ims::sip::SipMessage& request) const -> bool {
    if (!credential_store_) {
        return false;
    }

    auto auth_header = request.getHeader("Authorization");
    if (!auth_header) {
        return false;
    }

    auto params = AuthManager::parseAuthorization(*auth_header);
    if (!params) {
        return false;
    }

    if (!params->algorithm.empty() && params->algorithm != "MD5") {
        return false;
    }

    auto credential = credential_store_->findByUsername(params->username, params->realm);
    if (!credential.has_value() || !credential->has_value()) {
        return false;
    }

    {
        std::lock_guard lock(mutex_);
        purgeExpiredLocked(std::chrono::steady_clock::now());
        auto key = makePendingKey(request.callId(), (*credential)->impi);
        if (pending_auth_.contains(key)) {
            return true;
        }
    }

    return true;
}

auto DigestAuthProvider::createChallenge(const ims::sip::SipMessage& request) -> Result<AuthChallenge> {
    auto identity = extractIdentity(request);
    auto credential = credential_store_->findByIdentity(identity);
    if (!credential) {
        return std::unexpected(credential.error());
    }
    if (!*credential) {
        return std::unexpected(ErrorInfo{ErrorCode::kDiameterUserNotFound, "digest credential not found"});
    }

    auto call_id = request.callId();
    auto key = makePendingKey(call_id, (*credential)->impi);
    {
        std::lock_guard lock(mutex_);
        purgeExpiredLocked(std::chrono::steady_clock::now());
        auto it = pending_auth_.find(key);
        if (it != pending_auth_.end()) {
            return AuthChallenge{
                .session_key = call_id,
                .impi = it->second.credential.impi,
                .impu = it->second.credential.impu,
                .scheme = "Digest-MD5",
                .realm = realm_,
                .www_authenticate = AuthManager::buildDigestChallenge(realm_, it->second.nonce),
            };
        }
    }

    auto nonce = makeNonce();

    {
        std::lock_guard lock(mutex_);
        purgeExpiredLocked(std::chrono::steady_clock::now());
        if (auto it = pending_auth_.find(key); it != pending_auth_.end()) {
            return AuthChallenge{
                .session_key = call_id,
                .impi = it->second.credential.impi,
                .impu = it->second.credential.impu,
                .scheme = "Digest-MD5",
                .realm = realm_,
                .www_authenticate = AuthManager::buildDigestChallenge(realm_, it->second.nonce),
            };
        }
        pending_auth_[key] = PendingAuth{
            .nonce = nonce,
            .credential = **credential,
            .expires_at = std::chrono::steady_clock::now() + kPendingAuthTtl,
        };
    }

    return AuthChallenge{
        .session_key = call_id,
        .impi = (*credential)->impi,
        .impu = (*credential)->impu,
        .scheme = "Digest-MD5",
        .realm = realm_,
        .www_authenticate = AuthManager::buildDigestChallenge(realm_, nonce),
    };
}

auto DigestAuthProvider::verifyAuthorization(const ims::sip::SipMessage& request)
    -> Result<AuthVerificationResult> {
    auto auth_header = request.getHeader("Authorization");
    if (!auth_header) {
        return std::unexpected(ErrorInfo{ErrorCode::kDiameterAuthFailed, "missing Authorization header"});
    }

    PendingAuth pending;
    auto call_id = request.callId();
    auto params = AuthManager::parseAuthorization(*auth_header);
    if (!params) {
        return std::unexpected(params.error());
    }

    auto credential = credential_store_->findByUsername(params->username, params->realm);
    if (!credential) {
        return std::unexpected(credential.error());
    }
    if (!*credential) {
        return std::unexpected(ErrorInfo{ErrorCode::kDiameterAuthFailed, "credential not found"});
    }
    auto key = makePendingKey(call_id, (*credential)->impi);

    {
        std::lock_guard lock(mutex_);
        purgeExpiredLocked(std::chrono::steady_clock::now());
        auto it = pending_auth_.find(key);
        if (it == pending_auth_.end()) {
            return std::unexpected(ErrorInfo{ErrorCode::kDiameterAuthFailed, "no pending auth state"});
        }
        pending = it->second;
    }

    if (!AuthManager::verifyDigestPassword(*auth_header,
                                           pending.credential.password,
                                           request.method(),
                                           pending.nonce)) {
        return std::unexpected(ErrorInfo{ErrorCode::kDiameterAuthFailed, "authorization verification failed"});
    }

    {
        std::lock_guard lock(mutex_);
        pending_auth_.erase(key);
    }

    return AuthVerificationResult{
        .session_key = call_id,
        .impi = pending.credential.impi,
        .impu = pending.credential.impu,
        .scheme = "Digest-MD5",
    };
}

auto DigestAuthProvider::extractIdentity(const ims::sip::SipMessage& request) const -> std::string {
    if (auto impi = request.impi_from_authorization_or_from()) {
        return ims::sip::normalize_impu_uri(*impi);
    }
    if (auto impu = request.impu_from_to()) {
        return ims::sip::normalize_impu_uri(*impu);
    }
    return ims::sip::normalize_impu_uri(request.toHeader());
}

auto DigestAuthProvider::makePendingKey(std::string_view call_id, std::string_view impi) const -> std::string {
    return std::format("{}\x1f{}", call_id, ims::sip::normalize_impu_uri(std::string(impi)));
}

void DigestAuthProvider::purgeExpiredLocked(std::chrono::steady_clock::time_point now) const {
    std::erase_if(pending_auth_, [&](const auto& entry) {
        return entry.second.expires_at <= now;
    });
}

auto DigestAuthProvider::makeNonce() const -> std::string {
    static thread_local std::mt19937_64 rng{std::random_device{}()};
    std::uniform_int_distribution<uint64_t> dist;
    return std::format("{:016x}{:016x}", dist(rng), dist(rng));
}

} // namespace ims::scscf
