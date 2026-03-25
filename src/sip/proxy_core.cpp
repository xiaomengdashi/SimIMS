#include "proxy_core.hpp"
#include "stack.hpp"
#include "common/logger.hpp"
#include <osipparser2/osip_parser.h>

#include <algorithm>
#include <cctype>
#include <format>

namespace ims::sip {

ProxyCore::ProxyCore(const std::string& local_address, Port local_port)
    : local_address_(local_address)
    , local_port_(local_port) {}

auto ProxyCore::prepareRequestForForward(SipMessage& msg,
                                         std::string_view transport) -> VoidResult {
    // Decrement Max-Forwards
    int mf = msg.maxForwards();
    if (mf < 0) {
        msg.setMaxForwards(70);
        mf = 70;
    }
    if (mf == 0) {
        return std::unexpected(ErrorInfo{
            ErrorCode::kSipTransactionFailed, "Max-Forwards is zero, cannot forward"});
    }
    msg.decrementMaxForwards();

    // Add our Via header
    std::string transport_token(transport);
    if (transport_token.empty()) {
        transport_token = "udp";
    }
    std::transform(transport_token.begin(), transport_token.end(), transport_token.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::toupper(ch)); });

    auto branch = generateBranch();
    auto via = std::format("SIP/2.0/{} {}:{};branch={};rport",
        transport_token, local_address_, local_port_, branch);
    msg.addVia(via);

    return {};
}

auto ProxyCore::forwardRequest(SipMessage& msg, const Endpoint& dest,
                               std::shared_ptr<ITransport> transport) -> VoidResult {
    auto prep = prepareRequestForForward(msg, dest.transport);
    if (!prep) {
        return prep;
    }

    // Send via transport
    return transport->send(msg, dest);
}

auto ProxyCore::forwardResponse(SipMessage& msg,
                                std::shared_ptr<ITransport> transport) -> VoidResult {
    // Remove our top Via (we added it when forwarding the request)
    if (msg.viaCount() < 1) {
        return std::unexpected(ErrorInfo{
            ErrorCode::kSipTransportError, "No Via header in response"});
    }

    msg.removeTopVia();

    if (msg.viaCount() < 1) {
        return std::unexpected(ErrorInfo{
            ErrorCode::kSipTransportError, "No remaining Via header after removal"});
    }

    // Extract destination from the new top Via
    osip_via_t* top_via = nullptr;
    if (osip_message_get_via(msg.raw(), 0, &top_via) != 0 || !top_via) {
        return std::unexpected(ErrorInfo{
            ErrorCode::kSipTransportError, "Failed to get top Via for forwarding"});
    }

    // Get host and port from Via
    std::string host;
    Port port = 5060;

    // Check for received parameter first (NAT traversal)
    osip_generic_param_t* received = nullptr;
    osip_via_param_get_byname(top_via, const_cast<char*>("received"), &received);
    if (received && received->gvalue) {
        host = received->gvalue;
    } else if (top_via->host) {
        host = top_via->host;
    }

    // Check for rport parameter
    osip_generic_param_t* rport = nullptr;
    osip_via_param_get_byname(top_via, const_cast<char*>("rport"), &rport);
    if (rport && rport->gvalue && std::string(rport->gvalue) != "") {
        port = static_cast<Port>(std::atoi(rport->gvalue));
    } else if (top_via->port) {
        port = static_cast<Port>(std::atoi(top_via->port));
    }

    std::string transport_name = "udp";
    if (top_via->protocol) {
        auto protocol = std::string(top_via->protocol);
        std::transform(protocol.begin(), protocol.end(), protocol.begin(),
                       [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
        if (protocol == "tcp") {
            transport_name = "tcp";
        }
    }

    Endpoint dest{
        .address = host,
        .port = port,
        .transport = transport_name
    };

    IMS_LOG_DEBUG("Forwarding response {} to {}:{}", msg.statusCode(), dest.address, dest.port);
    return transport->send(msg, dest);
}

auto ProxyCore::forwardResponseUpstream(const SipMessage& response,
                                        const std::shared_ptr<ServerTransaction>& txn) -> VoidResult {
    IMS_LOG_DEBUG("Forwarding response upstream status={} original_via_count={} top_via={}",
                  response.statusCode(), response.viaCount(), response.topVia());
    auto upstream = response.clone();
    if (!upstream) {
        return std::unexpected(upstream.error());
    }
    upstream->removeTopVia();
    IMS_LOG_DEBUG("Forwarding response upstream after removeTopVia via_count={} top_via={}",
                  upstream->viaCount(), upstream->topVia());
    return txn->sendResponse(std::move(*upstream));
}

auto ProxyCore::forwardStateful(SipMessage& request,
                                const Endpoint& dest,
                                const std::shared_ptr<ServerTransaction>& upstream_txn,
                                SipStack& sip_stack,
                                const ForwardOptions& options) -> VoidResult {
    if (options.detect_loop && isLoopDetected(request)) {
        auto resp = createResponse(request, 482, "Loop Detected");
        if (resp) {
            upstream_txn->sendResponse(std::move(*resp));
        }
        return {};
    }

    if (options.process_route_headers) {
        processRouteHeaders(request);
    }
    if (options.add_record_route) {
        addRecordRoute(request);
    }

    auto prep = prepareRequestForForward(request, dest.transport);
    if (!prep) {
        int code = (request.maxForwards() == 0) ? 483 : 500;
        const char* reason = (code == 483) ? "Too Many Hops" : "Internal Server Error";
        auto resp = createResponse(request, code, reason);
        if (resp) {
            upstream_txn->sendResponse(std::move(*resp));
        }
        return std::unexpected(prep.error());
    }

    auto send_result = sip_stack.sendRequest(std::move(request), dest,
        [this, upstream_txn](const ims::sip::SipMessage& response) {
            auto forwarded = forwardResponseUpstream(response, upstream_txn);
            if (!forwarded) {
                IMS_LOG_WARN("Failed to forward response upstream: {}", forwarded.error().message);
            }
        });
    if (!send_result) {
        auto resp = createResponse(upstream_txn->request(), 500, "Internal Server Error");
        if (resp) {
            upstream_txn->sendResponse(std::move(*resp));
        }
        return std::unexpected(send_result.error());
    }

    return {};
}

auto ProxyCore::buildPathHeader() const -> std::string {
    return std::format("<sip:{}:{};lr>", local_address_, local_port_);
}

auto ProxyCore::buildForwardedCancel(const SipMessage& incoming_cancel,
                                     const CancelForwardContext& context) -> Result<SipMessage> {
    auto forwarded = incoming_cancel.clone();
    if (!forwarded) {
        return std::unexpected(forwarded.error());
    }

    if (context.process_route_headers) {
        processRouteHeaders(*forwarded);
    }

    while (forwarded->viaCount() > 0) {
        forwarded->removeTopVia();
    }

    if (context.decrement_max_forwards) {
        if (forwarded->maxForwards() < 0) {
            forwarded->setMaxForwards(70);
        } else {
            forwarded->decrementMaxForwards();
        }
    }

    std::string transport = context.transport.empty() ? "udp" : context.transport;
    std::transform(transport.begin(), transport.end(), transport.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::toupper(ch)); });
    auto branch = context.invite_branch.empty() ? generateBranch() : context.invite_branch;
    auto via = std::format("SIP/2.0/{} {}:{};branch={};rport",
                           transport, context.local_address, context.local_port, branch);
    forwarded->addVia(via);
    return forwarded;
}

void ProxyCore::addPathHeader(SipMessage& msg) {
    msg.addHeader("Path", buildPathHeader());
}

void ProxyCore::addRecordRoute(SipMessage& msg) {
    auto rr = std::format("<sip:{}:{};lr>", local_address_, local_port_);
    msg.addRecordRoute(rr);
    IMS_LOG_DEBUG("Added Record-Route: {}", rr);
}

bool ProxyCore::processRouteHeaders(SipMessage& msg) {
    auto route_list = msg.routes();
    if (route_list.empty()) return false;

    // Check if the top Route matches our address
    const auto& top_route = route_list[0];
    auto our_uri = std::format("sip:{}:{}", local_address_, local_port_);

    if (top_route.find(our_uri) != std::string::npos) {
        msg.removeTopRoute();
        IMS_LOG_DEBUG("Removed top Route matching our address");
        return true;
    }

    return false;
}

bool ProxyCore::isLoopDetected(const SipMessage& msg) const {
    int count = msg.viaCount();
    auto our_addr = std::format("{}:{}", local_address_, local_port_);

    for (int i = 0; i < count; ++i) {
        osip_via_t* via = nullptr;
        if (osip_message_get_via(msg.raw(), i, &via) == 0 && via) {
            std::string via_host;
            std::string via_port = "5060";

            if (via->host) via_host = via->host;
            if (via->port) via_port = via->port;

            auto via_addr = via_host + ":" + via_port;
            if (via_addr == our_addr) {
                IMS_LOG_WARN("Loop detected: Via contains our address {}", our_addr);
                return true;
            }
        }
    }

    return false;
}

} // namespace ims::sip
