#include "registrar.hpp"
#include "auth_manager.hpp"
#include "common/logger.hpp"
#include "sip/uri_utils.hpp"

#include <algorithm>
#include <chrono>
#include <format>
#include <ctime>
#include <unordered_set>

namespace ims::scscf {

namespace {

auto normalizeUri(std::string uri) -> std::string {
    if (uri.empty() || uri.find("sip:") != std::string::npos) {
        return uri;
    }
    return "sip:" + uri;
}

auto formatGmtDate() -> std::string {
    std::time_t now = std::time(nullptr);
    std::tm gm{};
    gmtime_r(&now, &gm);
    char buf[64]{0};
    std::strftime(buf, sizeof(buf), "%a, %d %b %Y %H:%M:%S GMT", &gm);
    return buf;
}

void addSecurityAgreementHeaders(const ims::sip::SipMessage& request,
                                 ims::sip::SipMessage& response,
                                 bool final_register_response) {
    auto security_client = request.getHeaders("Security-Client");
    if (security_client.empty()) return;

    // Minimal sec-agree support: echo selected mechanism list.
    for (const auto& mechanism : security_client) {
        response.addHeader("Security-Server", mechanism);
        if (final_register_response) {
            response.addHeader("Security-Verify", mechanism);
        }
    }
}

auto determineRegisterExpires(const ims::sip::SipMessage& request) -> uint32_t {
    if (auto contact_expires = request.contact_expires()) {
        return *contact_expires;
    }
    if (auto expires = request.expires_value()) {
        return *expires;
    }
    return 3600;
}

} // namespace

Registrar::Registrar(std::shared_ptr<ims::registration::IRegistrationStore> store,
                     std::shared_ptr<ims::diameter::IHssClient> hss,
                     const std::string& domain)
    : store_(std::move(store))
    , hss_(std::move(hss))
    , domain_(domain)
{
}

void Registrar::handleRegister(ims::sip::SipMessage& request,
                                std::shared_ptr<ims::sip::ServerTransaction> txn)
{
    auto impu = extractImpu(request);
    auto impi = extractImpi(request);
    IMS_LOG_INFO("REGISTER from IMPI={} IMPU={}", impi, impu);

    if (isDeregister(request)) {
        handleDeregister(request, txn);
        return;
    }

    if (tryHandleReregister(request, txn)) {
        return;
    }

    if (hasAuthorization(request)) {
        verifyAndRegister(request, txn);
    } else {
        sendChallenge(request, txn);
    }
}

void Registrar::sendChallenge(ims::sip::SipMessage& request,
                               std::shared_ptr<ims::sip::ServerTransaction> txn)
{
    auto impi = extractImpi(request);
    auto impu = extractImpu(request);
    auto call_id = request.callId();

    // Fetch auth vector from HSS via MAR
    ims::diameter::MarParams mar{
        .impi = impi,
        .impu = impu,
        .sip_auth_scheme = "Digest-AKAv1-MD5",
        .server_name = std::format("sip:scscf.{}", domain_),
    };

    auto maa = hss_->multimediaAuth(mar);
    if (!maa) {
        IMS_LOG_ERROR("MAR failed for {}: {}", impi, maa.error().message);
        auto resp = ims::sip::createResponse(request, 500, "Internal Server Error");
        if (resp) txn->sendResponse(std::move(*resp));
        return;
    }

    // Store auth vector for verification on next REGISTER
    {
        std::lock_guard lock(auth_mutex_);
        pending_auth_[call_id] = PendingAuth{
            .vector = maa->auth_vector,
            .impi = impi,
            .impu = impu,
            .scheme = maa->sip_auth_scheme,
        };
    }

    // Build 401 response with WWW-Authenticate
    auto resp = ims::sip::createResponse(request, 401, "Unauthorized");
    if (!resp) {
        IMS_LOG_ERROR("Failed to create 401 response");
        return;
    }

    auto challenge = AuthManager::buildChallenge(maa->auth_vector, domain_, maa->sip_auth_scheme);
    resp->addHeader("WWW-Authenticate", challenge);
    addSecurityAgreementHeaders(request, *resp, false);
    resp->addHeader("Date", formatGmtDate());

    IMS_LOG_DEBUG("Sending 401 challenge to {}", impi);
    txn->sendResponse(std::move(*resp));
}

bool Registrar::tryHandleReregister(ims::sip::SipMessage& request,
                                    std::shared_ptr<ims::sip::ServerTransaction> txn)
{
    auto impu = extractImpu(request);
    auto lookup = store_->lookup(impu);
    if (!lookup) {
        return false;
    }

    auto binding = *lookup;
    if (binding.state != ims::registration::RegistrationBinding::State::kRegistered) {
        return false;
    }

    auto contact = request.contact();
    if (!contact) {
        return false;
    }

    auto requested_contact_uri = ims::sip::extract_uri_from_name_addr(*contact);
    auto requested_instance_id = request.contact_param("+sip.instance").value_or("");
    auto requested_reg_id = request.contact_param("reg-id").value_or("");
    auto now = std::chrono::steady_clock::now();

    auto matches_contact = [&](const ims::registration::ContactBinding& candidate) -> bool {
        if (candidate.expires <= now) {
            return false;
        }
        if (!requested_instance_id.empty() && candidate.instance_id != requested_instance_id) {
            return false;
        }
        if (!requested_reg_id.empty() && candidate.reg_id != requested_reg_id) {
            return false;
        }

        // If flow identifiers are present, they are the primary matching keys.
        if (!requested_instance_id.empty() || !requested_reg_id.empty()) {
            return true;
        }

        return ims::sip::extract_uri_from_name_addr(candidate.contact_uri) == requested_contact_uri;
    };

    auto active_contacts = binding.active_contacts();
    if (active_contacts.empty()) {
        return false;
    }

    auto it = std::find_if(binding.contacts.begin(), binding.contacts.end(), matches_contact);
    if (it == binding.contacts.end()) {
        return false;
    }

    auto expires = determineRegisterExpires(request);
    auto path_headers = request.getHeaders("Path");
    auto user_agent = request.getHeader("User-Agent").value_or(it->user_agent);

    it->contact_uri = *contact;
    if (!requested_instance_id.empty()) {
        it->instance_id = requested_instance_id;
    }
    if (!requested_reg_id.empty()) {
        it->reg_id = requested_reg_id;
    }
    if (!path_headers.empty()) {
        it->path = path_headers.front();
    }
    it->expires = now + std::chrono::seconds(expires);
    it->user_agent = user_agent;
    it->call_id = request.callId();
    it->cseq = request.cseq();

    auto store_result = store_->store(binding);
    if (!store_result) {
        IMS_LOG_ERROR("Failed to update re-registration for {}: {}", impu, store_result.error().message);
        auto resp = ims::sip::createResponse(request, 500, "Internal Server Error");
        if (resp) txn->sendResponse(std::move(*resp));
        return true;
    }

    // Re-registration can refresh user profile; do not fail REGISTER if HSS refresh fails.
    std::vector<std::string> associated_impus{binding.impu};
    ims::diameter::SarParams sar{
        .impi = binding.impi,
        .impu = binding.impu,
        .server_name = std::format("sip:scscf.{}", domain_),
        .assignment_type = ims::diameter::SarParams::AssignmentType::kReRegistration,
    };
    auto saa = hss_->serverAssignment(sar);
    if (saa && !saa->user_profile.associated_impus.empty()) {
        associated_impus = saa->user_profile.associated_impus;
    } else if (!saa) {
        IMS_LOG_WARN("SAR re-registration refresh failed for {}: {}", impu, saa.error().message);
    }

    std::unordered_set<std::string> all_impus(associated_impus.begin(), associated_impus.end());
    all_impus.insert(binding.impu);
    for (const auto& identity : all_impus) {
        if (identity == binding.impu) {
            continue;
        }

        auto alias_binding = binding;
        alias_binding.impu = identity;
        auto alias_store_result = store_->store(alias_binding);
        if (!alias_store_result) {
            IMS_LOG_WARN("Failed to update alias binding {} during re-registration: {}",
                         identity, alias_store_result.error().message);
        }
    }

    IMS_LOG_INFO("Re-registration refreshed for {} (expires={}s)", impu, expires);
    sendRegisterOk(request, txn, expires, contact, associated_impus);
    return true;
}

void Registrar::sendRegisterOk(ims::sip::SipMessage& request,
                               std::shared_ptr<ims::sip::ServerTransaction> txn,
                               uint32_t expires,
                               const std::optional<std::string>& contact,
                               const std::vector<std::string>& associated_impus)
{
    auto resp = ims::sip::createResponse(request, 200, "OK");
    if (!resp) {
        IMS_LOG_ERROR("Failed to create 200 response for REGISTER");
        return;
    }

    if (contact) {
        resp->setContact(*contact);
    }
    resp->addHeader("Expires", std::to_string(expires));
    resp->addHeader("Date", formatGmtDate());
    addSecurityAgreementHeaders(request, *resp, true);

    auto service_route = std::format("<sip:scscf.{};lr>", domain_);
    resp->addHeader("Service-Route", service_route);

    if (!associated_impus.empty()) {
        for (const auto& associated : associated_impus) {
            resp->addHeader("P-Associated-URI", std::format("<{}>", associated));
        }
    } else {
        resp->addHeader("P-Associated-URI", std::format("<{}>", extractImpu(request)));
    }

    txn->sendResponse(std::move(*resp));
}

void Registrar::verifyAndRegister(ims::sip::SipMessage& request,
                                   std::shared_ptr<ims::sip::ServerTransaction> txn)
{
    auto call_id = request.callId();
    auto auth_header = request.getHeader("Authorization");

    if (!auth_header) {
        IMS_LOG_WARN("Missing Authorization header in second REGISTER");
        auto resp = ims::sip::createResponse(request, 400, "Bad Request");
        if (resp) txn->sendResponse(std::move(*resp));
        return;
    }

    // Look up pending auth
    PendingAuth pending;
    {
        std::lock_guard lock(auth_mutex_);
        auto it = pending_auth_.find(call_id);
        if (it == pending_auth_.end()) {
            IMS_LOG_WARN("No pending auth for Call-ID={}", call_id);
            // Re-challenge
            sendChallenge(request, txn);
            return;
        }
        pending = it->second;
        pending_auth_.erase(it);
    }

    // Verify the response
    if (!AuthManager::verifyResponse(*auth_header, pending.vector, request.method(), pending.scheme)) {
        IMS_LOG_WARN("Auth verification failed for {}", pending.impi);
        auto resp = ims::sip::createResponse(request, 403, "Forbidden");
        if (resp) txn->sendResponse(std::move(*resp));
        return;
    }

    IMS_LOG_INFO("Auth verified for {}", pending.impi);

    // Inform HSS via SAR
    ims::diameter::SarParams sar{
        .impi = pending.impi,
        .impu = pending.impu,
        .server_name = std::format("sip:scscf.{}", domain_),
        .assignment_type = ims::diameter::SarParams::AssignmentType::kRegistration,
    };

    auto saa = hss_->serverAssignment(sar);
    if (!saa) {
        IMS_LOG_ERROR("SAR failed for {}: {}", pending.impi, saa.error().message);
        auto resp = ims::sip::createResponse(request, 500, "Internal Server Error");
        if (resp) txn->sendResponse(std::move(*resp));
        return;
    }

    // Store registration binding
    auto contact = request.contact();
    auto expires = determineRegisterExpires(request);

    auto path_headers = request.getHeaders("Path");
    auto path = path_headers.empty() ? std::string{} : path_headers.front();
    auto user_agent = request.getHeader("User-Agent").value_or("");
    auto instance_id = request.contact_param("+sip.instance").value_or("");
    auto reg_id = request.contact_param("reg-id").value_or("");

    ims::registration::RegistrationBinding binding{
        .impu = pending.impu,
        .impi = pending.impi,
        .scscf_uri = std::format("sip:scscf.{}", domain_),
        .contacts = {{
            .contact_uri = contact.value_or(""),
            .instance_id = instance_id,
            .reg_id = reg_id,
            .path = path,
            .expires = std::chrono::steady_clock::now() + std::chrono::seconds(expires),
            .user_agent = user_agent,
            .call_id = call_id,
            .cseq = request.cseq(),
        }},
        .state = ims::registration::RegistrationBinding::State::kRegistered,
    };

    std::unordered_set<std::string> all_impus;
    all_impus.insert(pending.impu);
    for (const auto& associated : saa->user_profile.associated_impus) {
        all_impus.insert(associated);
    }

    for (const auto& identity : all_impus) {
        auto identity_binding = binding;
        identity_binding.impu = identity;

        auto store_result = store_->store(identity_binding);
        if (!store_result) {
            IMS_LOG_ERROR("Failed to store registration for {}: {}",
                          identity, store_result.error().message);
        }
    }

    IMS_LOG_INFO("Registration complete for {} (expires={}s)", pending.impu, expires);
    auto associated_impus = saa->user_profile.associated_impus;
    if (associated_impus.empty()) {
        associated_impus.push_back(pending.impu);
    }
    sendRegisterOk(request, txn, expires, contact, associated_impus);
}

void Registrar::handleDeregister(ims::sip::SipMessage& request,
                                  std::shared_ptr<ims::sip::ServerTransaction> txn)
{
    auto impi = extractImpi(request);
    auto impu = extractImpu(request);
    auto contact = request.contact();

    IMS_LOG_INFO("De-registration for IMPU={}", impu);

    // Inform HSS
    ims::diameter::SarParams sar{
        .impi = impi,
        .impu = impu,
        .server_name = std::format("sip:scscf.{}", domain_),
        .assignment_type = ims::diameter::SarParams::AssignmentType::kUserDeregistration,
    };
    auto saa = hss_->serverAssignment(sar);  // Best effort

    std::unordered_set<std::string> target_impus;
    target_impus.insert(impu);
    if (saa && !saa->user_profile.associated_impus.empty()) {
        target_impus.insert(saa->user_profile.associated_impus.begin(),
                            saa->user_profile.associated_impus.end());
    }

    auto wildcard_deregister = contact && *contact == "*";
    auto has_specific_contact = contact.has_value() && !wildcard_deregister;
    auto requested_contact_uri = has_specific_contact ? ims::sip::extract_uri_from_name_addr(*contact) : "";
    auto requested_instance_id = has_specific_contact ? request.contact_param("+sip.instance").value_or("") : "";
    auto requested_reg_id = has_specific_contact ? request.contact_param("reg-id").value_or("") : "";

    for (const auto& identity : target_impus) {
        auto lookup = store_->lookup(identity);
        if (!lookup) {
            continue;
        }

        auto binding = *lookup;
        if (wildcard_deregister || !has_specific_contact) {
            store_->remove(identity);
            continue;
        }

        auto matches_contact = [&](const ims::registration::ContactBinding& candidate) -> bool {
            if (!requested_instance_id.empty() && candidate.instance_id != requested_instance_id) {
                return false;
            }
            if (!requested_reg_id.empty() && candidate.reg_id != requested_reg_id) {
                return false;
            }

            if (!requested_instance_id.empty() || !requested_reg_id.empty()) {
                return true;
            }

            return ims::sip::extract_uri_from_name_addr(candidate.contact_uri) == requested_contact_uri;
        };

        auto old_size = binding.contacts.size();
        std::erase_if(binding.contacts, matches_contact);
        if (binding.contacts.size() == old_size) {
            continue;
        }

        if (binding.contacts.empty()) {
            store_->remove(identity);
            continue;
        }

        binding.state = ims::registration::RegistrationBinding::State::kRegistered;
        auto store_result = store_->store(binding);
        if (!store_result) {
            IMS_LOG_WARN("Failed to persist partial de-registration for {}: {}",
                         identity, store_result.error().message);
        }
    }

    auto resp = ims::sip::createResponse(request, 200, "OK");
    if (resp) {
        resp->addHeader("Date", formatGmtDate());
        txn->sendResponse(std::move(*resp));
    }
}

auto Registrar::extractImpi(const ims::sip::SipMessage& msg) const -> std::string {
    if (auto impi = msg.impi_from_authorization_or_from()) {
        return normalizeUri(*impi);
    }
    return normalizeUri(msg.fromHeader());
}

auto Registrar::extractImpu(const ims::sip::SipMessage& msg) const -> std::string {
    if (auto impu = msg.impu_from_to()) {
        return normalizeUri(*impu);
    }
    return normalizeUri(msg.toHeader());
}

bool Registrar::isDeregister(const ims::sip::SipMessage& msg) const {
    if (msg.expires_value() == 0) return true;
    if (msg.is_wildcard_contact()) return true;
    return msg.contact_expires() == 0;
}

bool Registrar::hasAuthorization(const ims::sip::SipMessage& msg) const {
    auto header = msg.getHeader("Authorization");
    if (!header) {
        IMS_LOG_DEBUG("REGISTER has no Authorization header");
        return false;
    }

    IMS_LOG_DEBUG("REGISTER Authorization header: {}", *header);
    auto params = AuthManager::parseAuthorization(*header);
    if (!params) {
        IMS_LOG_WARN("Failed to parse Authorization header: {} raw={}",
                     params.error().message, *header);
        return false;
    }

    IMS_LOG_DEBUG("Parsed Authorization username={} nonce_len={} response_len={} algorithm={} qop={} nc={} cnonce_len={}",
                  params->username, params->nonce.size(), params->response.size(),
                  params->algorithm, params->qop, params->nc, params->cnonce.size());

    // Some UE sends placeholder Authorization in initial REGISTER with empty nonce/response.
    return !params->nonce.empty() && !params->response.empty();
}

} // namespace ims::scscf
