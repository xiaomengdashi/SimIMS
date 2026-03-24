#include "icscf_service.hpp"
#include "common/logger.hpp"

#include <algorithm>
#include <cctype>
#include <format>
#include <optional>

namespace ims::icscf {

namespace {

auto parseEndpointFromSipUri(const std::string& sip_uri) -> std::optional<ims::sip::Endpoint> {
    std::string uri = sip_uri;
    auto normalized = uri;
    std::transform(normalized.begin(), normalized.end(), normalized.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });

    std::string transport = "udp";
    auto transport_pos = normalized.find(";transport=");
    if (transport_pos != std::string::npos) {
        auto value_start = transport_pos + std::string(";transport=").size();
        auto value_end = normalized.find(';', value_start);
        transport = normalized.substr(value_start, value_end - value_start);
    }

    if (!uri.empty() && uri.front() == '<') {
        uri.erase(0, 1);
    }
    if (!uri.empty() && uri.back() == '>') {
        uri.pop_back();
    }

    auto sip_pos = uri.find("sip:");
    if (sip_pos != std::string::npos) {
        uri = uri.substr(sip_pos + 4);
    }
    auto param_pos = uri.find(';');
    if (param_pos != std::string::npos) {
        uri = uri.substr(0, param_pos);
    }

    std::string host;
    ims::Port port = 5060;

    if (!uri.empty() && uri.front() == '[') {
        auto close = uri.find(']');
        if (close == std::string::npos) {
            return std::nullopt;
        }
        host = uri.substr(1, close - 1);
        if (close + 1 < uri.size() && uri[close + 1] == ':') {
            try {
                port = static_cast<ims::Port>(std::stoi(uri.substr(close + 2)));
            } catch (...) {
                return std::nullopt;
            }
        }
    } else {
        auto colon = uri.find(':');
        if (colon != std::string::npos) {
            host = uri.substr(0, colon);
            try {
                port = static_cast<ims::Port>(std::stoi(uri.substr(colon + 1)));
            } catch (...) {
                return std::nullopt;
            }
        } else {
            host = uri;
        }
    }

    if (host.empty()) {
        return std::nullopt;
    }
    return ims::sip::Endpoint{
        .address = host,
        .port = port,
        .transport = transport
    };
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
    IMS_LOG_DEBUG("I-CSCF received REGISTER");

    // Extract IMPI/IMPU from the REGISTER
    auto from = request.fromHeader();
    auto to = request.toHeader();

    // Simplified: extract user@domain from To header
    std::string impu;
    auto start = to.find("sip:");
    if (start != std::string::npos) {
        auto end = to.find('>', start);
        impu = (end != std::string::npos)
            ? to.substr(start, end - start)
            : to.substr(start);
    }

    std::string impi;
    start = from.find("sip:");
    if (start != std::string::npos) {
        auto end = from.find('>', start);
        auto uri = (end != std::string::npos)
            ? from.substr(start + 4, end - start - 4)
            : from.substr(start + 4);
        impi = uri;
    }

    // Query HSS for S-CSCF assignment
    auto scscf_result = selector_->selectForRegistration(impi, impu, config_.hss.realm);
    if (!scscf_result) {
        IMS_LOG_ERROR("S-CSCF selection failed: {}", scscf_result.error().message);
        auto resp = ims::sip::createResponse(request, 500, "Internal Server Error");
        if (resp) txn->sendResponse(std::move(*resp));
        return;
    }

    auto endpoint = parseEndpointFromSipUri(*scscf_result);
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

    auto endpoint = parseEndpointFromSipUri(*scscf_result);
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

    auto endpoint = parseEndpointFromSipUri(*scscf_result);
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
    if (proxy_.isLoopDetected(request)) {
        auto resp = ims::sip::createResponse(request, 482, "Loop Detected");
        if (resp) txn->sendResponse(std::move(*resp));
        return;
    }

    proxy_.processRouteHeaders(request);
    if (add_record_route) {
        proxy_.addRecordRoute(request);
    }

    auto prep = proxy_.prepareRequestForForward(request, dest.transport);
    if (!prep) {
        int code = (request.maxForwards() == 0) ? 483 : 500;
        const char* reason = (code == 483) ? "Too Many Hops" : "Internal Server Error";
        IMS_LOG_ERROR("Failed to prepare request for forwarding: {}", prep.error().message);
        auto resp = ims::sip::createResponse(request, code, reason);
        if (resp) txn->sendResponse(std::move(*resp));
        return;
    }

    auto send_result = sip_stack_->sendRequest(std::move(request), dest,
        [txn](const ims::sip::SipMessage& response) {
            auto upstream = response.clone();
            if (!upstream) return;
            upstream->removeTopVia();
            txn->sendResponse(std::move(*upstream));
        });

    if (!send_result) {
        IMS_LOG_ERROR("Failed to forward request statefully: {}", send_result.error().message);
        auto resp = ims::sip::createResponse(txn->request(), 500, "Internal Server Error");
        if (resp) txn->sendResponse(std::move(*resp));
    }
}

} // namespace ims::icscf
