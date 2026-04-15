#include "pcscf_service.hpp"
#include "common/logger.hpp"
#include "sip/uri_utils.hpp"

#include <algorithm>
#include <cctype>
#include <format>
#include <optional>
#include <random>
#include <sstream>
#include <string>
#include <osipparser2/osip_message.h>
#include <osipparser2/osip_parser.h>

namespace ims::pcscf {

namespace {

auto to_lower(std::string value) -> std::string {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return value;
}

auto resolve_proxy_advertised_address(const ims::PcscfConfig& config) -> std::string {
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

} // namespace

PcscfService::PcscfService(const ims::PcscfConfig& config,
                           boost::asio::io_context& io,
                           std::shared_ptr<ims::diameter::IPcfClient> pcf,
                           std::shared_ptr<ims::media::IRtpengineClient> rtpengine,
                           const std::string& core_entry_addr,
                           ims::Port core_entry_port)
    : config_(config)
    , sip_stack_(std::make_unique<ims::sip::SipStack>(
          io, config.listen_addr, config.listen_port))
    , proxy_(resolve_proxy_advertised_address(config), config.listen_port)
    , pcf_(std::move(pcf))
    , rtpengine_(std::move(rtpengine))
    , core_entry_addr_(core_entry_addr)
    , core_entry_port_(core_entry_port)
    , proxy_public_addr_(resolve_proxy_advertised_address(config))
{
}

auto PcscfService::start() -> VoidResult {
    IMS_LOG_INFO("Starting P-CSCF on {}:{}", config_.listen_addr, config_.listen_port);

    sip_stack_->onRequest("REGISTER", [this](auto txn, auto& req) { onRegister(txn, req); });
    sip_stack_->onRequest("INVITE", [this](auto txn, auto& req) { onInvite(txn, req); });
    sip_stack_->onRequest("BYE", [this](auto txn, auto& req) { onBye(txn, req); });
    sip_stack_->onRequest("ACK", [this](auto txn, auto& req) { onAck(txn, req); });
    sip_stack_->onRequest("CANCEL", [this](auto txn, auto& req) { onCancel(txn, req); });
    sip_stack_->onRequest("PRACK", [this](auto txn, auto& req) { onPrack(txn, req); });
    sip_stack_->onRequest("SUBSCRIBE", [this](auto txn, auto& req) { onSubscribe(txn, req); });

    return sip_stack_->start();
}

void PcscfService::stop() {
    IMS_LOG_INFO("Stopping P-CSCF");
    sip_stack_->stop();
}

void PcscfService::onRegister(std::shared_ptr<ims::sip::ServerTransaction> txn,
                               ims::sip::SipMessage& request)
{
    IMS_LOG_DEBUG("P-CSCF received REGISTER from UE via_count={} top_via={}",
                  request.viaCount(), request.topVia());

    proxy_.addPathHeader(request);
    forwardStatefulToCore(std::move(txn), request);
}

void PcscfService::onInvite(std::shared_ptr<ims::sip::ServerTransaction> txn,
                              ims::sip::SipMessage& request)
{
    IMS_LOG_DEBUG("P-CSCF received INVITE");

    if (isCoreFacingRequest(*txn)) {
        forwardStatefulToUe(std::move(txn), request, true);
        return;
    }

    auto call_id = request.callId();
    auto from_tag = request.fromTag();

    // Create media session tracking
    auto media_session = media_sessions_.createSession(call_id, from_tag);

    // Process SDP through rtpengine (offer)
    auto sdp = request.body();
    if (sdp && rtpengine_) {
        ims::media::RtpengineFlags flags{
            .direction_from = "internal",
            .direction_to = "internal",
            .ice_remove = true,
        };

        auto offer_result = rtpengine_->offer(media_session, *sdp, flags);
        if (offer_result) {
            media_sessions_.updateCallerSdp(call_id, *sdp);
            if (!offer_result->sdp.empty()) {
                // Replace SDP with rtpengine's version
                request.setBody(offer_result->sdp, "application/sdp");
                IMS_LOG_DEBUG("SDP rewritten by rtpengine for call={}", call_id);
            } else {
                IMS_LOG_WARN("rtpengine offer returned empty SDP for call={}, keeping original SDP", call_id);
            }
        } else {
            IMS_LOG_WARN("rtpengine offer failed: {}", offer_result.error().message);
        }
    }

    forwardStatefulToCore(std::move(txn), request, true, true);
}

void PcscfService::onBye(std::shared_ptr<ims::sip::ServerTransaction> txn,
                           ims::sip::SipMessage& request)
{
    auto call_id = request.callId();
    IMS_LOG_DEBUG("P-CSCF received BYE for call={}", call_id);

    if (isCoreFacingRequest(*txn)) {
        forwardStatefulToUe(std::move(txn), request);
        return;
    }

    // Clean up rtpengine session
    auto session_state = media_sessions_.getSession(call_id);
    if (session_state && rtpengine_) {
        rtpengine_->deleteSession(session_state->session);
    }

    // Release QoS via Rx STR
    if (session_state && !session_state->rx_session_id.empty() && pcf_) {
        ims::diameter::StrParams str{
            .session_id = session_state->rx_session_id,
            .termination_cause = 1,  // DIAMETER_LOGOUT
        };
        pcf_->terminateSession(str);
    }

    media_sessions_.removeSession(call_id);

    // Forward BYE along signaling path
    forwardStatefulToCore(std::move(txn), request);
}

void PcscfService::onAck(std::shared_ptr<ims::sip::ServerTransaction> txn,
                          ims::sip::SipMessage& request)
{
    IMS_LOG_DEBUG("P-CSCF received ACK for call={}", request.callId());
    // ACK has no response; forward statelessly.
    if (isCoreFacingRequest(*txn)) {
        forwardStatelessToUe(request);
        return;
    }
    forwardStatelessToCore(request);
}

void PcscfService::onCancel(std::shared_ptr<ims::sip::ServerTransaction> txn,
                             ims::sip::SipMessage& request)
{
    auto call_id = request.callId();
    IMS_LOG_DEBUG("P-CSCF received CANCEL for call={}", call_id);

    if (isCoreFacingRequest(*txn)) {
        forwardStatefulToUe(std::move(txn), request);
        return;
    }

    // Clean up media session
    auto session_state = media_sessions_.getSession(call_id);
    if (session_state && rtpengine_) {
        rtpengine_->deleteSession(session_state->session);
    }
    media_sessions_.removeSession(call_id);

    forwardStatefulToCore(std::move(txn), request);
}

void PcscfService::onPrack(std::shared_ptr<ims::sip::ServerTransaction> txn,
                            ims::sip::SipMessage& request)
{
    IMS_LOG_DEBUG("P-CSCF received PRACK for call={}", request.callId());
    if (isCoreFacingRequest(*txn)) {
        forwardStatefulToUe(std::move(txn), request);
        return;
    }
    forwardStatefulToCore(std::move(txn), request);
}

void PcscfService::onSubscribe(std::shared_ptr<ims::sip::ServerTransaction> txn,
                                ims::sip::SipMessage& request)
{
    IMS_LOG_DEBUG("P-CSCF received SUBSCRIBE for {}", request.requestUri());
    if (isCoreFacingRequest(*txn)) {
        forwardStatefulToUe(std::move(txn), request, true);
        return;
    }
    forwardStatefulToCore(std::move(txn), request, true);
}

void PcscfService::onInviteResponse(ims::sip::SipMessage& response) {
    if (!response.isResponse()) {
        return;
    }
    if (response.cseqMethod() != "INVITE") {
        return;
    }

    sanitizeForUeEgress(response);

    const int status = response.statusCode();
    if (status != 183 && (status < 200 || status >= 300)) {
        return;
    }

    auto sdp = response.body();
    if (!sdp || !rtpengine_) {
        return;
    }

    const auto call_id = response.callId();
    auto session_state = media_sessions_.getSession(call_id);
    if (!session_state) {
        IMS_LOG_DEBUG("No media session found while handling INVITE response call={}", call_id);
        return;
    }

    auto to_tag = response.toTag();
    if (!to_tag.empty()) {
        media_sessions_.updateToTag(call_id, to_tag);
        session_state->session.to_tag = std::move(to_tag);
    }

    ims::media::RtpengineFlags flags{
        .direction_from = "internal",
        .direction_to = "internal",
        .ice_remove = true,
    };

    auto answer_result = rtpengine_->answer(session_state->session, *sdp, flags);
    if (!answer_result) {
        IMS_LOG_WARN("rtpengine answer failed for call={}: {}", call_id, answer_result.error().message);
        return;
    }

    media_sessions_.updateCalleeSdp(call_id, *sdp);
    if (!answer_result->sdp.empty()) {
        response.setBody(answer_result->sdp, "application/sdp");
        IMS_LOG_DEBUG("SDP answer rewritten by rtpengine for call={}", call_id);
        return;
    }

    IMS_LOG_WARN("rtpengine answer returned empty SDP for call={}, keeping original SDP", call_id);
}

auto PcscfService::isCoreFacingRequest(const ims::sip::ServerTransaction& txn) const -> bool {
    const auto source = txn.source();
    const auto source_addr = to_lower(source.address);
    const auto source_transport = to_lower(source.transport.empty() ? "udp" : source.transport);

    auto endpoint_matches = [&](const std::string& configured_addr,
                                ims::Port configured_port,
                                const std::string& configured_transport) {
        const auto target_addr = to_lower(configured_addr);
        const auto target_transport = to_lower(configured_transport.empty() ? "udp" : configured_transport);
        const auto listen_addr = to_lower(config_.listen_addr);
        const bool is_loopback_source = source_addr == "127.0.0.1" || source_addr == "::1";
        const bool addr_matches =
            source_addr == target_addr ||
            source_addr == to_lower(proxy_public_addr_) ||
            (!listen_addr.empty() && listen_addr != "0.0.0.0" && listen_addr != "::" &&
             source_addr == listen_addr) ||
            (((target_addr == "0.0.0.0" || target_addr == "::") ||
              (listen_addr == "0.0.0.0" || listen_addr == "::")) &&
             is_loopback_source);
        return addr_matches && source.port == configured_port && source_transport == target_transport;
    };

    if (config_.core_peers.empty()) {
        return endpoint_matches(core_entry_addr_, core_entry_port_, config_.core_entry.transport);
    }

    for (const auto& peer : config_.core_peers) {
        if (endpoint_matches(peer.address, peer.port, peer.transport)) {
            return true;
        }
    }

    return false;
}

auto PcscfService::resolveUeDestination(const ims::sip::SipMessage& request) const
    -> std::optional<ims::sip::Endpoint> {
    auto parsed_request_uri = ims::sip::parse_endpoint_from_uri(request.requestUri());
    if (parsed_request_uri && !parsed_request_uri->address.empty() &&
        parsed_request_uri->address != "ims.local") {
        if (parsed_request_uri->transport.empty()) {
            parsed_request_uri->transport = "udp";
        }
        return parsed_request_uri;
    }

    auto contact_uri = request.contact_uri();
    if (contact_uri) {
        auto parsed_contact = ims::sip::parse_endpoint_from_uri(*contact_uri);
        if (parsed_contact) {
            if (parsed_contact->transport.empty()) {
                parsed_contact->transport = "udp";
            }
            return parsed_contact;
        }
    }

    osip_via_t* bottom_via = nullptr;
    auto idx = request.viaCount() - 1;
    if (idx >= 0 && osip_message_get_via(request.raw(), idx, &bottom_via) == 0 && bottom_via && bottom_via->host) {
        ims::Port port = 5060;
        osip_generic_param_t* rport = nullptr;
        osip_via_param_get_byname(bottom_via, const_cast<char*>("rport"), &rport);
        if (rport && rport->gvalue && std::string(rport->gvalue) != "") {
            port = static_cast<ims::Port>(std::atoi(rport->gvalue));
        } else if (bottom_via->port) {
            port = static_cast<ims::Port>(std::atoi(bottom_via->port));
        }

        std::string transport = "udp";
        if (bottom_via->protocol) {
            auto protocol = to_lower(bottom_via->protocol);
            if (protocol == "tcp") {
                transport = "tcp";
            }
        }

        return ims::sip::Endpoint{
            .address = bottom_via->host,
            .port = port,
            .transport = transport,
        };
    }

    return std::nullopt;
}

auto PcscfService::resolveCoreDestination(const ims::sip::SipMessage& request) const -> ims::sip::Endpoint {
    if (auto token = extractTopologyToken(request)) {
        std::lock_guard<std::mutex> lock(topology_mutex_);
        auto it = topology_routes_.find(*token);
        if (it != topology_routes_.end()) {
            return it->second;
        }
    }
    return ims::sip::Endpoint{
        .address = core_entry_addr_,
        .port = core_entry_port_,
        .transport = config_.core_entry.transport.empty() ? "udp" : config_.core_entry.transport,
    };
}

auto PcscfService::extractTopologyToken(const ims::sip::SipMessage& request) const -> std::optional<std::string> {
    auto routes = request.routes();
    if (routes.empty()) {
        return std::nullopt;
    }

    const auto& top = routes.front();
    auto key_pos = top.find("th=");
    if (key_pos == std::string::npos) {
        return std::nullopt;
    }

    key_pos += 3;
    auto end_pos = top.find_first_of(";>", key_pos);
    if (end_pos == std::string::npos) {
        end_pos = top.size();
    }
    if (end_pos <= key_pos) {
        return std::nullopt;
    }

    return top.substr(key_pos, end_pos - key_pos);
}

auto PcscfService::createTopologyToken() -> std::string {
    return "th" + random_hex(20);
}

void PcscfService::rememberTopologyRoute(const std::string& token, const ims::sip::Endpoint& endpoint) {
    std::lock_guard<std::mutex> lock(topology_mutex_);
    topology_routes_[token] = endpoint;
}

void PcscfService::addTopologyRecordRoute(ims::sip::SipMessage& request, const std::string& token) const {
    request.removeHeader("Record-Route");
    auto rr = std::format("<sip:{}:{};lr;th={}>", proxy_public_addr_, config_.listen_port, token);
    request.addRecordRoute(rr);
}

void PcscfService::sanitizeForUeEgress(ims::sip::SipMessage& request) {
    auto via_count = request.viaCount();
    while (via_count > 1) {
        request.removeTopVia();
        via_count = request.viaCount();
    }
}

void PcscfService::forwardStatefulToCore(std::shared_ptr<ims::sip::ServerTransaction> txn,
                                          ims::sip::SipMessage& request,
                                          bool add_record_route,
                                          bool process_invite_response_media)
{
    auto dest = resolveCoreDestination(request);

    ims::sip::ForwardOptions options{
        .add_record_route = add_record_route,
    };
    if (process_invite_response_media) {
        options.on_response = [this](ims::sip::SipMessage& response) {
            onInviteResponse(response);
        };
    }

    auto result = proxy_.forwardStateful(request, dest, txn, *sip_stack_, options);
    if (!result) {
        IMS_LOG_ERROR("Failed to send request to core entry: {}", result.error().message);
    }
}

void PcscfService::forwardStatefulToUe(std::shared_ptr<ims::sip::ServerTransaction> txn,
                                       ims::sip::SipMessage& request,
                                       bool add_record_route) {
    auto ue = resolveUeDestination(request);
    if (!ue) {
        IMS_LOG_WARN("Failed to resolve UE destination from Via chain");
        auto resp = ims::sip::createResponse(request, 500, "Internal Server Error");
        if (resp) {
            txn->sendResponse(std::move(*resp));
        }
        return;
    }

    auto token = createTopologyToken();
    rememberTopologyRoute(token, resolveCoreDestination(request));

    request.removeHeader("Route");
    if (add_record_route) {
        addTopologyRecordRoute(request, token);
    }

    ims::sip::ForwardOptions options{
        .add_record_route = false,
        .process_route_headers = false,
        .detect_loop = false,
    };

    auto result = proxy_.forwardStateful(request, *ue, txn, *sip_stack_, options);
    if (!result) {
        IMS_LOG_ERROR("Failed to send request to UE: {}", result.error().message);
    }
}

void PcscfService::forwardStatelessToCore(ims::sip::SipMessage& request,
                                           bool add_record_route)
{
    if (proxy_.isLoopDetected(request)) {
        IMS_LOG_WARN("Dropping request due to loop detection");
        return;
    }

    proxy_.processRouteHeaders(request);
    if (add_record_route) {
        proxy_.addRecordRoute(request);
    }

    auto dest = resolveCoreDestination(request);

    auto result = proxy_.forwardRequest(request, dest, sip_stack_->transport());
    if (!result) {
        IMS_LOG_ERROR("Failed to forward stateless request: {}", result.error().message);
    }
}

void PcscfService::forwardStatelessToUe(ims::sip::SipMessage& request,
                                        bool add_record_route) {
    auto ue = resolveUeDestination(request);
    if (!ue) {
        IMS_LOG_WARN("Failed to resolve UE destination from Via chain");
        return;
    }

    auto token = createTopologyToken();
    rememberTopologyRoute(token, resolveCoreDestination(request));

    sanitizeForUeEgress(request);
    if (add_record_route) {
        addTopologyRecordRoute(request, token);
    }

    auto result = proxy_.forwardRequest(request, *ue, sip_stack_->transport());
    if (!result) {
        IMS_LOG_ERROR("Failed to forward stateless request to UE: {}", result.error().message);
    }
}

} // namespace ims::pcscf
