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

auto makeContactSelector(const ims::sip::SipMessage& request,
                         const std::optional<std::string>& contact)
    -> ims::registration::ContactBindingSelector
{
    ims::registration::ContactBindingSelector selector{
        .normalized_contact_uri = contact ? ims::sip::extract_uri_from_name_addr(*contact) : std::string{},
        .instance_id = request.contact_param("+sip.instance").value_or(""),
        .reg_id = request.contact_param("reg-id").value_or(""),
    };

    if (!selector.uses_instance_and_reg_id()) {
        selector.instance_id.clear();
        selector.reg_id.clear();
    }

    return selector;
}

auto makeContactBinding(const ims::sip::SipMessage& request,
                        const std::optional<std::string>& contact,
                        uint32_t expires,
                        const ims::registration::ContactBinding* previous = nullptr)
    -> ims::registration::ContactBinding
{
    auto path_headers = request.getHeaders("Path");
    auto path = path_headers.empty()
                    ? (previous ? previous->path : std::string{})
                    : path_headers.front();
    auto user_agent = request.getHeader("User-Agent")
                          .value_or(previous ? previous->user_agent : std::string{});
    auto instance_id = request.contact_param("+sip.instance")
                           .value_or(previous ? previous->instance_id : std::string{});
    auto reg_id = request.contact_param("reg-id")
                      .value_or(previous ? previous->reg_id : std::string{});

    return {
        .contact_uri = contact.value_or(previous ? previous->contact_uri : std::string{}),
        .instance_id = instance_id,
        .reg_id = reg_id,
        .path = path,
        .expires = std::chrono::steady_clock::now() + std::chrono::seconds(expires),
        .user_agent = user_agent,
        .call_id = request.callId(),
        .cseq = request.cseq(),
    };
}

} // namespace

Registrar::Registrar(std::shared_ptr<ims::registration::IRegistrationStore> store,
                     std::vector<std::shared_ptr<IAuthProvider>> auth_providers,
                     std::shared_ptr<ims::diameter::IHssClient> hss,
                     const std::string& domain)
    : store_(std::move(store))
    , auth_providers_(std::move(auth_providers))
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
    auto provider = selectProviderForInitialRegister(request);
    if (!provider) {
        IMS_LOG_ERROR("Challenge creation failed for {}: no auth provider available",
                      extractImpi(request));
        auto resp = ims::sip::createResponse(request, 501, "Not Implemented");
        if (resp) {
            (void)txn->sendResponse(std::move(*resp));
        }
        return;
    }

    auto challenge = provider->createChallenge(request);
    if (!challenge) {
        IMS_LOG_ERROR("Challenge creation failed for {}: {}",
                      extractImpi(request), challenge.error().message);
        auto resp = ims::sip::createResponse(request, 501, "Not Implemented");
        if (resp) {
            (void)txn->sendResponse(std::move(*resp));
        }
        return;
    }

    // Build 401 response with WWW-Authenticate
    auto resp = ims::sip::createResponse(request, 401, "Unauthorized");
    if (!resp) {
        IMS_LOG_ERROR("Failed to create 401 response");
        return;
    }

    resp->addHeader("WWW-Authenticate", challenge->www_authenticate);
    addSecurityAgreementHeaders(request, *resp, false);
    resp->addHeader("Date", formatGmtDate());

    IMS_LOG_DEBUG("Sending 401 challenge to {}", challenge->impi);
    (void)txn->sendResponse(std::move(*resp));
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

    auto selector = makeContactSelector(request, contact);
    auto existing = std::find_if(binding.contacts.begin(), binding.contacts.end(),
                                 [&](const ims::registration::ContactBinding& candidate) {
                                     if (candidate.expires <= std::chrono::steady_clock::now()) {
                                         return false;
                                     }
                                     if (selector.uses_instance_and_reg_id()) {
                                         return candidate.instance_id == selector.instance_id
                                                && candidate.reg_id == selector.reg_id;
                                     }
                                     return ims::sip::extract_uri_from_name_addr(candidate.contact_uri)
                                            == selector.normalized_contact_uri;
                                 });
    if (existing == binding.contacts.end()) {
        return false;
    }

    auto expires = determineRegisterExpires(request);
    auto refreshed_contact = makeContactBinding(request, contact, expires, &*existing);

    auto store_result = store_->upsertContact(binding.impu,
                                              selector,
                                              refreshed_contact,
                                              binding.impi,
                                              binding.scscf_uri,
                                              ims::registration::RegistrationBinding::State::kRegistered,
                                              true,
                                              true);
    if (!store_result) {
        IMS_LOG_ERROR("Failed to update re-registration for {}: {}", impu, store_result.error().message);
        auto resp = ims::sip::createResponse(request, 500, "Internal Server Error");
        if (resp) {
            (void)txn->sendResponse(std::move(*resp));
        }
        return true;
    }
    if (!*store_result) {
        return false;
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

        auto alias_store_result = store_->upsertContact(identity,
                                                        selector,
                                                        refreshed_contact,
                                                        binding.impi,
                                                        binding.scscf_uri,
                                                        ims::registration::RegistrationBinding::State::kRegistered,
                                                        false,
                                                        true);
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

    if (!associated_impus.empty()) {
        for (const auto& associated : associated_impus) {
            resp->addHeader("P-Associated-URI", std::format("<{}>", associated));
        }
    } else {
        resp->addHeader("P-Associated-URI", std::format("<{}>", extractImpu(request)));
    }

    (void)txn->sendResponse(std::move(*resp));
}

void Registrar::verifyAndRegister(ims::sip::SipMessage& request,
                                   std::shared_ptr<ims::sip::ServerTransaction> txn)
{
    auto call_id = request.callId();
    auto provider = selectProviderForAuthorization(request);
    if (!provider) {
        IMS_LOG_WARN("No auth provider can verify Authorization for Call-ID={}", call_id);
        auto resp = ims::sip::createResponse(request, 400, "Bad Request");
        if (resp) {
            (void)txn->sendResponse(std::move(*resp));
        }
        return;
    }

    auto verification = provider->verifyAuthorization(request);
    if (!verification) {
        IMS_LOG_WARN("Auth verification failed for Call-ID={} reason={}",
                     call_id, verification.error().message);
        if (verification.error().message == "no pending auth state") {
            sendChallenge(request, txn);
            return;
        }

        auto resp = ims::sip::createResponse(request, 403, "Forbidden");
        if (resp) {
            (void)txn->sendResponse(std::move(*resp));
        }
        return;
    }

    IMS_LOG_INFO("Auth verified for {}", verification->impi);

    // Inform HSS via SAR
    ims::diameter::SarParams sar{
        .impi = verification->impi,
        .impu = verification->impu,
        .server_name = std::format("sip:scscf.{}", domain_),
        .assignment_type = ims::diameter::SarParams::AssignmentType::kRegistration,
    };

    auto saa = hss_->serverAssignment(sar);
    if (!saa) {
        IMS_LOG_ERROR("SAR failed for {}: {}", verification->impi, saa.error().message);
        auto resp = ims::sip::createResponse(request, 500, "Internal Server Error");
        if (resp) {
            (void)txn->sendResponse(std::move(*resp));
        }
        return;
    }

    // Store registration binding
    auto contact = request.contact();
    auto expires = determineRegisterExpires(request);
    auto selector = makeContactSelector(request, contact);
    auto contact_binding = makeContactBinding(request, contact, expires);
    auto scscf_uri = std::format("sip:scscf.{}", domain_);

    std::unordered_set<std::string> all_impus;
    all_impus.insert(verification->impu);
    for (const auto& associated : saa->user_profile.associated_impus) {
        all_impus.insert(associated);
    }

    for (const auto& identity : all_impus) {
        auto store_result = store_->upsertContact(identity,
                                                  selector,
                                                  contact_binding,
                                                  verification->impi,
                                                  scscf_uri,
                                                  ims::registration::RegistrationBinding::State::kRegistered,
                                                  false,
                                                  true);
        if (!store_result) {
            IMS_LOG_ERROR("Failed to store registration for {}: {}",
                          identity, store_result.error().message);
        }
    }

    IMS_LOG_INFO("Registration complete for {} (expires={}s)", verification->impu, expires);
    auto associated_impus = saa->user_profile.associated_impus;
    if (associated_impus.empty()) {
        associated_impus.push_back(verification->impu);
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
    auto selector = has_specific_contact
                        ? makeContactSelector(request, contact)
                        : ims::registration::ContactBindingSelector{};

    for (const auto& identity : target_impus) {
        if (wildcard_deregister || !has_specific_contact) {
            auto remove_result = store_->remove(identity);
            if (!remove_result) {
                IMS_LOG_WARN("Failed to remove registration for {}: {}",
                             identity, remove_result.error().message);
            }
            continue;
        }

        auto remove_contact_result = store_->removeContact(identity, selector);
        if (!remove_contact_result) {
            IMS_LOG_WARN("Failed to persist partial de-registration for {}: {}",
                         identity, remove_contact_result.error().message);
        }
    }

    auto resp = ims::sip::createResponse(request, 200, "OK");
    if (resp) {
        resp->addHeader("Date", formatGmtDate());
        (void)txn->sendResponse(std::move(*resp));
    }
}

auto Registrar::extractImpi(const ims::sip::SipMessage& msg) const -> std::string {
    if (auto impi = msg.impi_from_authorization_or_from()) {
        return ims::sip::normalize_impu_uri(*impi);
    }
    return ims::sip::normalize_impu_uri(msg.fromHeader());
}

auto Registrar::extractImpu(const ims::sip::SipMessage& msg) const -> std::string {
    if (auto impu = msg.impu_from_to()) {
        return ims::sip::normalize_impu_uri(*impu);
    }
    return ims::sip::normalize_impu_uri(msg.toHeader());
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

auto Registrar::selectProviderForInitialRegister(const ims::sip::SipMessage& request) const
    -> std::shared_ptr<IAuthProvider> {
    auto it = std::find_if(auth_providers_.begin(), auth_providers_.end(),
                           [&](const auto& provider) {
                               return provider && provider->canHandleInitialRegister(request);
                           });
    return it == auth_providers_.end() ? nullptr : *it;
}

auto Registrar::selectProviderForAuthorization(const ims::sip::SipMessage& request) const
    -> std::shared_ptr<IAuthProvider> {
    auto it = std::find_if(auth_providers_.begin(), auth_providers_.end(),
                           [&](const auto& provider) {
                               return provider && provider->canHandleAuthorization(request);
                           });
    return it == auth_providers_.end() ? nullptr : *it;
}

} // namespace ims::scscf
