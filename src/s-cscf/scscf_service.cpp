#include "scscf_service.hpp"
#include "common/logger.hpp"
#include "digest_auth_provider.hpp"
#include "ims_aka_auth_provider.hpp"
#include "sip/uri_utils.hpp"

#include <format>
#include <vector>

namespace ims::scscf {

namespace {

auto buildAuthProviders(const ims::ScscfConfig& config,
                        const std::shared_ptr<ims::diameter::IHssClient>& hss,
                        const std::shared_ptr<IDigestCredentialStore>& digest_store)
    -> std::vector<std::shared_ptr<IAuthProvider>> {
    std::vector<std::shared_ptr<IAuthProvider>> auth_providers;

    auto add_ims = [&] {
        auth_providers.push_back(std::make_shared<ImsAkaAuthProvider>(hss, config.domain));
    };
    auto add_digest = [&] {
        if (digest_store) {
            auth_providers.push_back(std::make_shared<DigestAuthProvider>(digest_store, config.domain));
        } else {
            IMS_LOG_WARN("S-CSCF auth_mode={} requested Digest auth, but no credential store is configured",
                         config.auth_mode);
        }
    };

    if (config.auth_mode == "digest_only") {
        add_digest();
        return auth_providers;
    }

    if (config.auth_mode == "hybrid_fallback") {
        add_ims();
        add_digest();
        return auth_providers;
    }

    if (config.auth_mode != "ims_only") {
        IMS_LOG_WARN("Unknown S-CSCF auth_mode={}, falling back to ims_only", config.auth_mode);
    }
    add_ims();
    return auth_providers;
}

} // namespace

ScscfService::ScscfService(const ims::ScscfConfig& config,
                           boost::asio::io_context& io,
                           std::shared_ptr<ims::diameter::IHssClient> hss,
                           std::shared_ptr<ims::registration::IRegistrationStore> store,
                           std::unique_ptr<ims::sip::IRegEventNotifier> reg_event_notifier)
    : ScscfService(config, io, std::move(hss), std::move(store), nullptr,
                   std::move(reg_event_notifier))
{
}

ScscfService::ScscfService(const ims::ScscfConfig& config,
                           boost::asio::io_context& io,
                           std::shared_ptr<ims::diameter::IHssClient> hss,
                           std::shared_ptr<ims::registration::IRegistrationStore> store,
                           std::shared_ptr<IDigestCredentialStore> digest_store,
                           std::unique_ptr<ims::sip::IRegEventNotifier> reg_event_notifier)
    : config_(config)
    , sip_stack_(std::make_unique<ims::sip::SipStack>(
          io, config.listen_addr, config.listen_port))
    , reg_event_notifier_(std::move(reg_event_notifier))
    , hss_(std::move(hss))
    , store_(std::move(store))
    , digest_store_(std::move(digest_store))
    , registration_cleanup_timer_(io)
{
    if (!reg_event_notifier_) {
        reg_event_notifier_ = std::make_unique<ims::sip::ExosipRegEventNotifier>(config.exosip);
    }
    auto auth_providers = buildAuthProviders(config_, hss_, digest_store_);
    registrar_ = std::make_unique<Registrar>(store_, std::move(auth_providers), hss_, config.domain);
    session_router_ = std::make_unique<SessionRouter>(store_, *sip_stack_);
}

auto ScscfService::start() -> VoidResult {
    IMS_LOG_INFO("Starting S-CSCF on {}:{} domain={} auth_mode={} cleanup_interval_ms={}",
                 config_.listen_addr,
                 config_.listen_port,
                 config_.domain,
                 config_.auth_mode,
                 config_.registration_cleanup_interval_ms);

    if (reg_event_notifier_) {
        auto notifier_start = reg_event_notifier_->start();
        if (!notifier_start) {
            return std::unexpected(notifier_start.error());
        }
    }

    sip_stack_->onRequest("REGISTER", [this](auto txn, auto& req) {
        onRegister(txn, req);
    });
    sip_stack_->onRequest("INVITE", [this](auto txn, auto& req) {
        onInvite(txn, req);
    });
    sip_stack_->onRequest("BYE", [this](auto txn, auto& req) {
        onBye(txn, req);
    });
    sip_stack_->onRequest("ACK", [this](auto txn, auto& req) {
        session_router_->handleAck(req, txn);
    });
    sip_stack_->onRequest("CANCEL", [this](auto txn, auto& req) {
        onCancel(txn, req);
    });
    sip_stack_->onRequest("PRACK", [this](auto txn, auto& req) {
        onPrack(txn, req);
    });
    sip_stack_->onRequest("SUBSCRIBE", [this](auto txn, auto& req) {
        onSubscribe(txn, req);
    });

    auto start_result = sip_stack_->start();
    if (!start_result) {
        return start_result;
    }

    scheduleRegistrationCleanup();
    return {};
}

void ScscfService::stop() {
    IMS_LOG_INFO("Stopping S-CSCF");
    registration_cleanup_timer_.cancel();
    if (reg_event_notifier_) {
        reg_event_notifier_->shutdown();
    }
    sip_stack_->stop();
}

void ScscfService::scheduleRegistrationCleanup() {
    if (config_.registration_cleanup_interval_ms == 0) {
        IMS_LOG_INFO("Registration cleanup scheduler disabled");
        return;
    }

    registration_cleanup_timer_.expires_after(
        std::chrono::milliseconds(config_.registration_cleanup_interval_ms));
    registration_cleanup_timer_.async_wait([this](const boost::system::error_code& ec) {
        if (ec == boost::asio::error::operation_aborted) {
            return;
        }
        if (ec) {
            IMS_LOG_WARN("Registration cleanup timer error: {}", ec.message());
            scheduleRegistrationCleanup();
            return;
        }
        runRegistrationCleanup();
        scheduleRegistrationCleanup();
    });
}

void ScscfService::runRegistrationCleanup() {
    auto purge_result = store_->purgeExpired();
    if (!purge_result) {
        IMS_LOG_WARN("Failed to purge expired registrations: {}", purge_result.error().message);
        return;
    }

    if (*purge_result > 0) {
        IMS_LOG_INFO("Purged {} expired registration contact(s)", *purge_result);
    }
}

void ScscfService::onRegister(std::shared_ptr<ims::sip::ServerTransaction> txn,
                               ims::sip::SipMessage& request)
{
    IMS_LOG_DEBUG("S-CSCF received REGISTER via_count={} top_via={}",
                  request.viaCount(), request.topVia());
    registrar_->handleRegister(request, txn);
}

void ScscfService::onInvite(std::shared_ptr<ims::sip::ServerTransaction> txn,
                             ims::sip::SipMessage& request)
{
    IMS_LOG_DEBUG("S-CSCF received INVITE");
    session_router_->handleInvite(request, txn);
}

void ScscfService::onBye(std::shared_ptr<ims::sip::ServerTransaction> txn,
                          ims::sip::SipMessage& request)
{
    IMS_LOG_DEBUG("S-CSCF received BYE");
    session_router_->handleBye(request, txn);
}

void ScscfService::onCancel(std::shared_ptr<ims::sip::ServerTransaction> txn,
                             ims::sip::SipMessage& request)
{
    IMS_LOG_DEBUG("S-CSCF received CANCEL");
    session_router_->handleCancel(request, txn);
}

void ScscfService::onPrack(std::shared_ptr<ims::sip::ServerTransaction> txn,
                            ims::sip::SipMessage& request)
{
    IMS_LOG_DEBUG("S-CSCF received PRACK");
    session_router_->handlePrack(request, txn);
}

void ScscfService::onSubscribe(std::shared_ptr<ims::sip::ServerTransaction> txn,
                                ims::sip::SipMessage& request)
{
    IMS_LOG_DEBUG("S-CSCF received SUBSCRIBE event={}",
                  request.getHeader("Event").value_or("<none>"));

    auto event = request.getHeader("Event");
    if (!event || *event != "reg") {
        auto resp = ims::sip::createResponse(request, 489, "Bad Event");
        if (resp) txn->sendResponse(std::move(*resp));
        return;
    }

    auto impu = ims::sip::normalize_impu_uri(request.requestUri());

    auto lookup = store_->lookup(impu);
    if (!lookup) {
        auto resp = ims::sip::createResponse(request, 404, "Not Found");
        if (resp) txn->sendResponse(std::move(*resp));
        return;
    }

    auto resp = ims::sip::createResponse(request, 200, "OK");
    if (!resp) return;

    auto expires = request.getHeader("Expires").value_or("3600");
    resp->addHeader("Expires", expires);
    resp->setContact(std::format("<sip:{}:{}>", config_.listen_addr, config_.listen_port));
    for (const auto& rr : request.getHeaders("Record-Route")) {
        resp->addRecordRoute(rr);
    }

    auto to_tag = resp->toTag();
    txn->sendResponse(std::move(*resp));

    sendInitialNotify(request, to_tag);
}

void ScscfService::sendInitialNotify(const ims::sip::SipMessage& subscribe,
                                     const std::string& to_tag)
{
    auto contact = subscribe.contact();
    if (!contact) {
        IMS_LOG_WARN("SUBSCRIBE missing Contact header, cannot send initial NOTIFY");
        return;
    }

    auto target_uri = ims::sip::extract_uri_from_name_addr(*contact);
    auto record_routes = subscribe.getHeaders("Record-Route");
    std::vector<std::string> route_set(record_routes.rbegin(), record_routes.rend());

    auto from = subscribe.toHeader();
    if (from.find(";tag=") == std::string::npos && !to_tag.empty()) {
        from += ";tag=" + to_tag;
    }

    auto event = subscribe.getHeader("Event").value_or("reg");
    auto expires = subscribe.getHeader("Expires").value_or("3600");

    auto reg_aor = ims::sip::extract_uri_from_name_addr(subscribe.toHeader());
    auto body = std::format(
        "<?xml version=\"1.0\"?>\r\n"
        "<reginfo xmlns=\"urn:ietf:params:xml:ns:reginfo\" version=\"0\" state=\"full\">\r\n"
        "  <registration aor=\"{}\" id=\"1\" state=\"active\">\r\n"
        "    <contact id=\"1\" state=\"active\" event=\"registered\" />\r\n"
        "  </registration>\r\n"
        "</reginfo>\r\n",
        reg_aor);

    ims::sip::InitialRegNotifyContext notify_context{
        .request_uri = target_uri,
        .from_header = from,
        .to_header = subscribe.fromHeader(),
        .call_id = subscribe.callId(),
        .cseq = subscribe.cseq() + 1,
        .event = event,
        .subscription_state = std::format("active;expires={}", expires),
        .route_set = route_set,
        .contact = std::format("<sip:{}:{}>", config_.domain, config_.exosip.listen_port),
        .body = body,
        .content_type = "application/reginfo+xml"
    };

    if (!reg_event_notifier_) {
        IMS_LOG_WARN("No reg-event notifier configured, skip initial NOTIFY");
        return;
    }

    auto send_result = reg_event_notifier_->sendInitialNotify(notify_context);
    if (!send_result) {
        IMS_LOG_ERROR("Failed to send initial NOTIFY via eXosip2: {} ({})",
                      send_result.error().message,
                      send_result.error().detail);
    }
}

} // namespace ims::scscf
