#include "registrar.hpp"
#include "auth_manager.hpp"
#include "ims/common/logger.hpp"

#include <chrono>
#include <format>

namespace ims::scscf {

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
        };
    }

    // Build 401 response with WWW-Authenticate
    auto resp = ims::sip::createResponse(request, 401, "Unauthorized");
    if (!resp) {
        IMS_LOG_ERROR("Failed to create 401 response");
        return;
    }

    auto challenge = AuthManager::buildChallenge(maa->auth_vector, domain_);
    resp->addHeader("WWW-Authenticate", challenge);

    IMS_LOG_DEBUG("Sending 401 challenge to {}", impi);
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
    if (!AuthManager::verifyResponse(*auth_header, pending.vector)) {
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
    auto expires_header = request.getHeader("Expires");
    uint32_t expires = 3600;
    if (expires_header) {
        expires = std::stoul(*expires_header);
    }

    ims::registration::RegistrationBinding binding{
        .impu = pending.impu,
        .impi = pending.impi,
        .scscf_uri = std::format("sip:scscf.{}", domain_),
        .contacts = {{
            .contact_uri = contact.value_or(""),
            .expires = std::chrono::steady_clock::now() + std::chrono::seconds(expires),
            .call_id = call_id,
            .cseq = request.cseq(),
        }},
        .state = ims::registration::RegistrationBinding::State::kRegistered,
    };

    auto store_result = store_->store(binding);
    if (!store_result) {
        IMS_LOG_ERROR("Failed to store registration: {}", store_result.error().message);
    }

    // Send 200 OK
    auto resp = ims::sip::createResponse(request, 200, "OK");
    if (!resp) return;

    if (contact) {
        resp->setContact(*contact);
    }
    resp->addHeader("Expires", std::to_string(expires));

    // Add Service-Route for subsequent requests
    auto service_route = std::format("<sip:scscf.{};lr>", domain_);
    resp->addHeader("Service-Route", service_route);

    // Add P-Associated-URI
    resp->addHeader("P-Associated-URI", std::format("<{}>", pending.impu));

    IMS_LOG_INFO("Registration complete for {} (expires={}s)", pending.impu, expires);
    txn->sendResponse(std::move(*resp));
}

void Registrar::handleDeregister(ims::sip::SipMessage& request,
                                  std::shared_ptr<ims::sip::ServerTransaction> txn)
{
    auto impi = extractImpi(request);
    auto impu = extractImpu(request);

    IMS_LOG_INFO("De-registration for IMPU={}", impu);

    // Inform HSS
    ims::diameter::SarParams sar{
        .impi = impi,
        .impu = impu,
        .server_name = std::format("sip:scscf.{}", domain_),
        .assignment_type = ims::diameter::SarParams::AssignmentType::kUserDeregistration,
    };
    hss_->serverAssignment(sar);  // Best effort

    // Remove binding
    store_->remove(impu);

    auto resp = ims::sip::createResponse(request, 200, "OK");
    if (resp) txn->sendResponse(std::move(*resp));
}

auto Registrar::extractImpi(const ims::sip::SipMessage& msg) const -> std::string {
    // IMPI is in Authorization header username, or derived from From URI
    auto auth = msg.getHeader("Authorization");
    if (auth) {
        auto params = AuthManager::parseAuthorization(*auth);
        if (params) return params->username;
    }
    // Fallback: extract from From header
    auto from = msg.fromHeader();
    // Strip "<sip:" prefix and ">" suffix
    auto start = from.find("sip:");
    if (start != std::string::npos) {
        auto end = from.find('>', start);
        if (end != std::string::npos) {
            return from.substr(start + 4, end - start - 4);
        }
        return from.substr(start + 4);
    }
    return from;
}

auto Registrar::extractImpu(const ims::sip::SipMessage& msg) const -> std::string {
    // IMPU is in the To header URI
    auto to_hdr = msg.toHeader();
    // Extract URI from <sip:user@domain>
    auto start = to_hdr.find('<');
    auto end = to_hdr.find('>');
    if (start != std::string::npos && end != std::string::npos) {
        return to_hdr.substr(start + 1, end - start - 1);
    }
    return to_hdr;
}

bool Registrar::isDeregister(const ims::sip::SipMessage& msg) const {
    auto expires = msg.getHeader("Expires");
    if (expires && *expires == "0") return true;

    // Also check Contact: * with Expires: 0
    auto contact = msg.contact();
    if (contact && *contact == "*") return true;

    return false;
}

bool Registrar::hasAuthorization(const ims::sip::SipMessage& msg) const {
    return msg.getHeader("Authorization").has_value();
}

} // namespace ims::scscf
