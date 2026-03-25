#include "icscf_service.hpp"
#include "common/logger.hpp"
#include "sip/uri_utils.hpp"

#include <algorithm>
#include <cctype>
#include <format>
#include <optional>

namespace ims::icscf {

namespace {

auto normalizeImpu(std::string uri) -> std::string {
    if (uri.empty()) {
        return uri;
    }
    if (uri.find("sip:") == std::string::npos) {
        uri = "sip:" + uri;
    }
    return uri;
}

} // namespace

IcscfService::IcscfService(const ims::IcscfConfig& config,
                           boost::asio::io_context& io,
                           std::shared_ptr<ims::diameter::IHssClient> hss)
    : config_(config)
    , sip_stack_(std::make_unique<ims::sip::SipStack>(
          io, config.listen_addr, config.listen_port))
    , selector_(std::make_unique<ScscfSelector>(std::move(hss)))
    , proxy_(config.listen_addr, config.listen_port)
{
}

auto IcscfService::start() -> VoidResult {
    IMS_LOG_INFO("Starting I-CSCF on {}:{}", config_.listen_addr, config_.listen_port);

    sip_stack_->onRequest("REGISTER", [this](auto txn, auto& req) {
        onRegister(txn, req);
    });
    sip_stack_->onRequest("INVITE", [this](auto txn, auto& req) {
        onInvite(txn, req);
    });
    sip_stack_->onRequest("SUBSCRIBE", [this](auto txn, auto& req) {
        onSubscribe(txn, req);
    });

    return sip_stack_->start();
}

void IcscfService::stop() {
    IMS_LOG_INFO("Stopping I-CSCF");
    sip_stack_->stop();
}

void IcscfService::onRegister(std::shared_ptr<ims::sip::ServerTransaction> txn,
                               ims::sip::SipMessage& request)
{
    IMS_LOG_DEBUG("I-CSCF received REGISTER via_count={} top_via={}",
                  request.viaCount(), request.topVia());

    auto impu = request.impu_from_to().value_or("");
    auto impi = request.impi_from_authorization_or_from().value_or("");

    // Query HSS for S-CSCF assignment
    auto scscf_result = selector_->selectForRegistration(impi, impu, config_.hss.realm);
    if (!scscf_result) {
        IMS_LOG_ERROR("S-CSCF selection failed: {}", scscf_result.error().message);
        auto resp = ims::sip::createResponse(request, 500, "Internal Server Error");
        if (resp) txn->sendResponse(std::move(*resp));
        return;
    }

    auto endpoint = ims::sip::parse_endpoint_from_uri(*scscf_result);
    if (!endpoint) {
        IMS_LOG_ERROR("Invalid S-CSCF URI from UAA: {}", *scscf_result);
        auto resp = ims::sip::createResponse(request, 500, "Internal Server Error");
        if (resp) txn->sendResponse(std::move(*resp));
        return;
    }
    forwardStateful(std::move(txn), request, *endpoint);
}

void IcscfService::onInvite(std::shared_ptr<ims::sip::ServerTransaction> txn,
                             ims::sip::SipMessage& request)
{
    IMS_LOG_DEBUG("I-CSCF received INVITE (MT routing)");

    // Extract callee IMPU from Request-URI
    auto callee_uri = request.requestUri();
    std::string impu = callee_uri;
    if (impu.find("sip:") == std::string::npos) {
        impu = "sip:" + impu;
    }

    // Query HSS for serving S-CSCF
    auto scscf_result = selector_->selectForRouting(impu);
    if (!scscf_result) {
        IMS_LOG_WARN("No serving S-CSCF for {}", impu);
        auto resp = ims::sip::createResponse(request, 404, "Not Found");
        if (resp) txn->sendResponse(std::move(*resp));
        return;
    }

    auto endpoint = ims::sip::parse_endpoint_from_uri(*scscf_result);
    if (!endpoint) {
        IMS_LOG_ERROR("Invalid serving S-CSCF URI from LIA: {}", *scscf_result);
        auto resp = ims::sip::createResponse(request, 500, "Internal Server Error");
        if (resp) txn->sendResponse(std::move(*resp));
        return;
    }
    forwardStateful(std::move(txn), request, *endpoint, true);
}

void IcscfService::onSubscribe(std::shared_ptr<ims::sip::ServerTransaction> txn,
                                ims::sip::SipMessage& request)
{
    IMS_LOG_DEBUG("I-CSCF received SUBSCRIBE");

    auto impu = request.requestUri();
    if (impu.find("sip:") == std::string::npos) {
        impu = "sip:" + impu;
    }

    auto scscf_result = selector_->selectForRouting(impu);
    if (!scscf_result) {
        IMS_LOG_WARN("No serving S-CSCF for SUBSCRIBE target {}", impu);
        auto resp = ims::sip::createResponse(request, 404, "Not Found");
        if (resp) txn->sendResponse(std::move(*resp));
        return;
    }

    auto endpoint = ims::sip::parse_endpoint_from_uri(*scscf_result);
    if (!endpoint) {
        IMS_LOG_ERROR("Invalid serving S-CSCF URI from LIA: {}", *scscf_result);
        auto resp = ims::sip::createResponse(request, 500, "Internal Server Error");
        if (resp) txn->sendResponse(std::move(*resp));
        return;
    }
    forwardStateful(std::move(txn), request, *endpoint, true);
}

void IcscfService::forwardStateful(std::shared_ptr<ims::sip::ServerTransaction> txn,
                                   ims::sip::SipMessage& request,
                                   const ims::sip::Endpoint& dest,
                                   bool add_record_route)
{
    auto result = proxy_.forwardStateful(request, dest, txn, *sip_stack_, {
        .add_record_route = add_record_route,
    });
    if (!result) {
        IMS_LOG_ERROR("Failed to forward request statefully: {}", result.error().message);
    }
}

} // namespace ims::icscf
