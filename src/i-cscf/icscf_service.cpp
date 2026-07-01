#include "icscf_service.hpp"
#include "core/logger.hpp"
#include "sip/uri_utils.hpp"
#include "sms/sms_validator.hpp"

#include <algorithm>
#include <cctype>
#include <format>
#include <optional>
#include <random>
#include <sstream>
#include <string>

namespace ims::icscf {

namespace {

auto resolve_proxy_advertised_address(const ims::IcscfConfig& config) -> std::string {
    if (!config.advertised_addr.empty()) {
        return config.advertised_addr;
    }
    if (!config.listen_addr.empty() && config.listen_addr != "0.0.0.0") {
        return config.listen_addr;
    }
    return "127.0.0.1";
}

auto random_hex(int length) -> std::string {
    static thread_local std::mt19937 rng{std::random_device{}()};
    std::uniform_int_distribution<int> dist(0, 15);
    std::ostringstream oss;
    for (int i = 0; i < length; ++i) {
        oss << std::hex << dist(rng);
    }
    return oss.str();
}

auto to_lower(std::string value) -> std::string {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return value;
}

constexpr auto kTopologyRouteTtl = std::chrono::minutes{30};

} // namespace

IcscfService::IcscfService(const ims::IcscfConfig& config,
                           boost::asio::io_context& io,
                           std::shared_ptr<ims::diameter::IHssClient> hss)
    : config_(config)
    , sip_stack_(std::make_unique<ims::sip::SipStack>(
          io, config.listen_addr, config.listen_port))
    , selector_(std::make_unique<ScscfSelector>(std::move(hss)))
    , proxy_(resolve_proxy_advertised_address(config), config.listen_port)
    , proxy_public_addr_(resolve_proxy_advertised_address(config))
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
    sip_stack_->onRequest("ACK", [this](auto /*txn*/, auto& req) {
        onAck(req);
    });
    sip_stack_->onRequest("BYE", [this](auto txn, auto& req) {
        onInDialogStateful(txn, req, "BYE");
    });
    sip_stack_->onRequest("CANCEL", [this](auto txn, auto& req) {
        onInDialogStateful(txn, req, "CANCEL");
    });
    sip_stack_->onRequest("PRACK", [this](auto txn, auto& req) {
        onInDialogStateful(txn, req, "PRACK");
    });
    sip_stack_->onRequest("MESSAGE", [this](auto txn, auto& req) {
        onMessage(txn, req);
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
    auto impu = ims::sip::normalize_impu_uri(request.requestUri());

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
    forwardStateful(std::move(txn), request, *endpoint, false);
}

void IcscfService::onMessage(std::shared_ptr<ims::sip::ServerTransaction> txn,
                              ims::sip::SipMessage& request)
{
    IMS_LOG_DEBUG("I-CSCF received MESSAGE (MT SMS routing)");

    const auto body = request.body().value_or(std::string{});
    const auto content_type = request.contentType().value_or(std::string{});
    if (auto sms_valid = ims::sms::validate_sip_message_body(content_type, body); !sms_valid) {
        IMS_LOG_WARN("I-CSCF rejected invalid SMS MESSAGE for {}: {}",
                     request.requestUri(), sms_valid.error().message);
        const auto status = sms_valid.error().code == ims::ErrorCode::kSmsInvalidPayload ? 415 : 400;
        const auto reason = status == 415 ? "Unsupported Media Type" : "Bad Request";
        auto resp = ims::sip::createResponse(request, status, reason);
        if (resp) {
            txn->sendResponse(std::move(*resp));
        }
        return;
    }

    auto impu = ims::sip::normalize_impu_uri(request.requestUri());

    auto scscf_result = selector_->selectForRouting(impu);
    if (!scscf_result) {
        IMS_LOG_WARN("No serving S-CSCF for MESSAGE target {}", impu);
        auto resp = ims::sip::createResponse(request, 404, "Not Found");
        if (resp) txn->sendResponse(std::move(*resp));
        return;
    }

    auto endpoint = ims::sip::parse_endpoint_from_uri(*scscf_result);
    if (!endpoint) {
        IMS_LOG_ERROR("Invalid serving S-CSCF URI from LIA for MESSAGE: {}", *scscf_result);
        auto resp = ims::sip::createResponse(request, 500, "Internal Server Error");
        if (resp) txn->sendResponse(std::move(*resp));
        return;
    }
    forwardStateful(std::move(txn), request, *endpoint, false);
}

void IcscfService::onSubscribe(std::shared_ptr<ims::sip::ServerTransaction> txn,
                                ims::sip::SipMessage& request)
{
    IMS_LOG_DEBUG("I-CSCF received SUBSCRIBE");

    auto impu = ims::sip::normalize_impu_uri(request.requestUri());

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
    forwardStateful(std::move(txn), request, *endpoint, false);
}

auto IcscfService::localScscfEndpoint() const -> ims::sip::Endpoint {
    std::string address = config_.local_scscf.address;
    if (address.empty() || address == "0.0.0.0") {
        address = "127.0.0.1";
    }
    return ims::sip::Endpoint{
        .address = address,
        .port = config_.local_scscf.port,
        .transport = config_.local_scscf.transport.empty() ? "udp" : config_.local_scscf.transport,
    };
}

auto IcscfService::resolveScscfDestination(const ims::sip::SipMessage& request) const -> ims::sip::Endpoint {
    if (auto token = extractTopologyToken(request)) {
        std::lock_guard<std::mutex> lock(topology_mutex_);
        purgeExpiredTopologyRoutesLocked();
        auto it = topology_routes_.find(*token);
        if (it != topology_routes_.end()) {
            return it->second.endpoint;
        }
    }
    return localScscfEndpoint();
}

auto IcscfService::extractTopologyToken(const ims::sip::SipMessage& request) const -> std::optional<std::string> {
    std::vector<std::string> routes = request.routes();
    auto service_routes = request.getHeaders("Service-Route");
    routes.insert(routes.end(), service_routes.begin(), service_routes.end());
    auto record_routes = request.getHeaders("Record-Route");
    routes.insert(routes.end(), record_routes.begin(), record_routes.end());

    for (const auto& route : routes) {
        auto key_pos = route.find("th=");
        if (key_pos == std::string::npos) {
            continue;
        }

        key_pos += 3;
        auto end_pos = route.find_first_of(";>", key_pos);
        if (end_pos == std::string::npos) {
            end_pos = route.size();
        }
        if (end_pos > key_pos) {
            return route.substr(key_pos, end_pos - key_pos);
        }
    }

    return std::nullopt;
}

auto IcscfService::createTopologyToken() -> std::string {
    return "th" + random_hex(20);
}

void IcscfService::purgeExpiredTopologyRoutesLocked() const {
    auto now = std::chrono::steady_clock::now();
    std::erase_if(topology_routes_, [now](const auto& item) {
        return item.second.expires_at <= now;
    });
}

auto IcscfService::topologyRoutesForRequest(const ims::sip::SipMessage& request) const -> std::vector<std::string> {
    if (auto token = extractTopologyToken(request)) {
        std::lock_guard<std::mutex> lock(topology_mutex_);
        purgeExpiredTopologyRoutesLocked();
        auto it = topology_routes_.find(*token);
        if (it != topology_routes_.end()) {
            return it->second.routes;
        }
    }
    return {};
}

void IcscfService::rememberTopologyRoute(const std::string& token,
                                         const ims::sip::Endpoint& endpoint,
                                         std::vector<std::string> routes) {
    std::lock_guard<std::mutex> lock(topology_mutex_);
    purgeExpiredTopologyRoutesLocked();
    topology_routes_[token] = TopologyRouteEntry{
        .endpoint = endpoint,
        .routes = std::move(routes),
        .expires_at = std::chrono::steady_clock::now() + kTopologyRouteTtl,
    };
}

auto IcscfService::topologyRouteForToken(const std::string& token) const -> std::string {
    return std::format("<sip:{}:{};lr;th={}>", proxy_public_addr_, config_.listen_port, token);
}

void IcscfService::hideHeaderRoutesForExternal(ims::sip::SipMessage& message, const std::string& header_name) {
    const auto routes = message.getHeaders(header_name);
    if (routes.empty()) {
        return;
    }

    ims::sip::Endpoint local_endpoint{
        .address = proxy_public_addr_,
        .port = config_.listen_port,
        .transport = "udp",
    };
    if (routes.front().find("th=") != std::string::npos &&
        ims::sip::route_points_to_endpoint(routes.front(), local_endpoint)) {
        return;
    }

    auto endpoint = ims::sip::parse_endpoint_from_uri(routes.front()).value_or(localScscfEndpoint());
    if (endpoint.transport.empty()) {
        endpoint.transport = config_.local_scscf.transport.empty() ? "udp" : config_.local_scscf.transport;
    }

    auto token = createTopologyToken();
    rememberTopologyRoute(token, endpoint, routes);
    auto hidden_route = topologyRouteForToken(token);

    message.removeHeader(header_name);
    if (to_lower(header_name) == "record-route") {
        message.addRecordRoute(hidden_route);
    } else {
        message.addHeader(header_name, hidden_route);
    }
}

void IcscfService::restoreTopologyRouteForScscf(ims::sip::SipMessage& request) {
    auto routes = topologyRoutesForRequest(request);
    proxy_.processRouteHeaders(request);
    for (const auto& route : routes) {
        if (!route.empty()) {
            request.addRoute(route);
        }
    }
}

void IcscfService::sanitizeForExternalEgress(ims::sip::SipMessage& message) {
    hideHeaderRoutesForExternal(message, "Service-Route");
    hideHeaderRoutesForExternal(message, "Record-Route");
}

void IcscfService::onAck(ims::sip::SipMessage& request) {
    IMS_LOG_DEBUG("I-CSCF received ACK");
    auto dest = resolveScscfDestination(request);
    restoreTopologyRouteForScscf(request);
    auto result = proxy_.forwardRequest(request, dest, sip_stack_->transport());
    if (!result) {
        IMS_LOG_ERROR("Failed to forward ACK statelessly: {}", result.error().message);
    }
}

void IcscfService::onInDialogStateful(std::shared_ptr<ims::sip::ServerTransaction> txn,
                                      ims::sip::SipMessage& request,
                                      const char* method_name) {
    IMS_LOG_DEBUG("I-CSCF received {}", method_name);
    auto dest = resolveScscfDestination(request);
    restoreTopologyRouteForScscf(request);
    auto result = proxy_.forwardStateful(request, dest, txn, *sip_stack_, {
        .add_record_route = false,
        .process_route_headers = false,
        .on_response = [this](ims::sip::SipMessage& response) {
            sanitizeForExternalEgress(response);
        },
    });
    if (!result) {
        IMS_LOG_ERROR("Failed to forward {} statefully: {}", method_name, result.error().message);
    }
}

void IcscfService::forwardStateful(std::shared_ptr<ims::sip::ServerTransaction> txn,
                                   ims::sip::SipMessage& request,
                                   const ims::sip::Endpoint& dest,
                                   bool add_record_route)
{
    auto result = proxy_.forwardStateful(request, dest, txn, *sip_stack_, {
        .add_record_route = add_record_route,
        .on_response = [this](ims::sip::SipMessage& response) {
            sanitizeForExternalEgress(response);
        },
    });
    if (!result) {
        IMS_LOG_ERROR("Failed to forward request statefully: {}", result.error().message);
    }
}

} // namespace ims::icscf
