#include "session_router.hpp"
#include "common/logger.hpp"

#include <algorithm>
#include <cctype>
#include <format>
#include <optional>

namespace ims::scscf {

namespace {

auto endpointEquals(const ims::sip::Endpoint& lhs, const ims::sip::Endpoint& rhs) -> bool {
    return lhs.address == rhs.address && lhs.port == rhs.port;
}

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

    auto at_pos = uri.rfind('@');
    if (at_pos != std::string::npos) {
        uri = uri.substr(at_pos + 1);
    }

    auto param_pos = uri.find(';');
    if (param_pos != std::string::npos) {
        uri = uri.substr(0, param_pos);
    }
    auto angle_pos = uri.find('>');
    if (angle_pos != std::string::npos) {
        uri = uri.substr(0, angle_pos);
    }

    std::string host;
    ims::Port port = 5060;

    if (!uri.empty() && uri.front() == '[') {
        auto close = uri.find(']');
        if (close == std::string::npos) return std::nullopt;
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

    if (host.empty()) return std::nullopt;
    return ims::sip::Endpoint{
        .address = host,
        .port = port,
        .transport = transport
    };
}

} // namespace

SessionRouter::SessionRouter(std::shared_ptr<ims::registration::IRegistrationStore> store,
                             ims::sip::SipStack& sip_stack)
    : store_(std::move(store))
    , sip_stack_(sip_stack)
    , proxy_(sip_stack.localAddress(), sip_stack.localPort()) {
    IMS_LOG_INFO("SessionRouter initialized");
}

void SessionRouter::handleInvite(const ims::sip::SipMessage& request,
                                 std::shared_ptr<ims::sip::ServerTransaction> txn) {
    auto call_id = request.callId();
    auto from = request.fromHeader();
    auto request_uri = request.requestUri();

    IMS_LOG_INFO("INVITE received, Call-ID={}, From={}, To={}", call_id, from, request_uri);

    // Send 100 Trying immediately
    auto trying = ims::sip::createResponse(request, 100, "Trying");
    if (trying) {
        txn->sendResponse(std::move(*trying));
    }

    // Look up callee registration
    auto binding = lookupCallee(request_uri);
    if (!binding) {
        IMS_LOG_WARN("Callee not found: {}", request_uri);
        auto resp = ims::sip::createResponse(request, 404, "Not Found");
        if (resp) txn->sendResponse(std::move(*resp));
        return;
    }

    // Get active contacts for the callee
    auto contacts = binding->active_contacts();
    if (contacts.empty()) {
        IMS_LOG_WARN("No active contacts for callee: {}", request_uri);
        auto resp = ims::sip::createResponse(request, 480, "Temporarily Unavailable");
        if (resp) txn->sendResponse(std::move(*resp));
        return;
    }

    // Clone the request for forwarding
    auto fwd_request = request.clone();
    if (!fwd_request) {
        IMS_LOG_ERROR("Failed to clone INVITE for forwarding");
        auto resp = ims::sip::createResponse(request, 500, "Server Internal Error");
        if (resp) txn->sendResponse(std::move(*resp));
        return;
    }

    // Set Request-URI to the callee's contact
    fwd_request->setRequestUri(contacts[0]->contact_uri);
    proxy_.processRouteHeaders(*fwd_request);

    // Add Record-Route so subsequent requests pass through us
    proxy_.addRecordRoute(*fwd_request);

    // Determine destination from contact's Path or contact URI
    ims::sip::Endpoint dest;
    if (!contacts[0]->path.empty()) {
        // Path header contains P-CSCF route URI.
        auto parsed = parseEndpointFromSipUri(contacts[0]->path);
        if (parsed) {
            dest = *parsed;
        }
    } else {
        auto parsed = parseEndpointFromSipUri(contacts[0]->contact_uri);
        if (parsed) {
            dest = *parsed;
        }
    }

    if (dest.address.empty()) {
        IMS_LOG_WARN("Failed to derive destination from Path/Contact, fallback to localhost");
        dest.address = "127.0.0.1";
        dest.port = 5060;
        dest.transport = "udp";
    }

    auto prep = proxy_.prepareRequestForForward(*fwd_request, dest.transport);
    if (!prep) {
        IMS_LOG_ERROR("Failed to prepare INVITE for forwarding: {}", prep.error().message);
        auto resp = ims::sip::createResponse(request, 500, "Server Internal Error");
        if (resp) txn->sendResponse(std::move(*resp));
        return;
    }

    auto invite_branch = fwd_request->viaBranch();

    // Track session
    {
        std::lock_guard lock(sessions_mutex_);
        sessions_[call_id] = SessionInfo{
            .caller_impu = from,
            .callee_impu = binding->impu,
            .caller_endpoint = txn->source(),
            .callee_endpoint = dest,
            .callee_invite_branch = invite_branch,
        };
    }

    sip_stack_.sendRequest(std::move(*fwd_request), dest,
        [txn, call_id](const ims::sip::SipMessage& response) {
            IMS_LOG_DEBUG("Got response {} for INVITE Call-ID={}", response.statusCode(), call_id);
            // Forward response back to caller
            auto resp_clone = response.clone();
            if (resp_clone) {
                resp_clone->removeTopVia();
                txn->sendResponse(std::move(*resp_clone));
            }
        });
}

void SessionRouter::handleBye(const ims::sip::SipMessage& request,
                               std::shared_ptr<ims::sip::ServerTransaction> txn) {
    auto call_id = request.callId();
    IMS_LOG_INFO("BYE received, Call-ID={}", call_id);

    // Look up session
    SessionInfo session;
    {
        std::lock_guard lock(sessions_mutex_);
        auto it = sessions_.find(call_id);
        if (it == sessions_.end()) {
            IMS_LOG_WARN("No session found for BYE, Call-ID={}", call_id);
            auto resp = ims::sip::createResponse(request, 481, "Call/Transaction Does Not Exist");
            if (resp) txn->sendResponse(std::move(*resp));
            return;
        }
        session = it->second;
        sessions_.erase(it);
    }

    // Forward BYE to the opposite dialog leg
    auto fwd_bye = request.clone();
    if (!fwd_bye) {
        auto resp = ims::sip::createResponse(request, 500, "Server Internal Error");
        if (resp) txn->sendResponse(std::move(*resp));
        return;
    }

    proxy_.processRouteHeaders(*fwd_bye);
    auto dest = session.callee_endpoint;
    if (endpointEquals(txn->source(), session.callee_endpoint)) {
        dest = session.caller_endpoint;
    }

    auto prep = proxy_.prepareRequestForForward(*fwd_bye, dest.transport);
    if (!prep) {
        auto resp = ims::sip::createResponse(request, 500, "Server Internal Error");
        if (resp) txn->sendResponse(std::move(*resp));
        return;
    }

    sip_stack_.sendRequest(std::move(*fwd_bye), dest,
        [txn, call_id](const ims::sip::SipMessage& response) {
            IMS_LOG_DEBUG("Got response {} for BYE Call-ID={}", response.statusCode(), call_id);
            auto resp_clone = response.clone();
            if (resp_clone) {
                resp_clone->removeTopVia();
                txn->sendResponse(std::move(*resp_clone));
            }
        });
}

void SessionRouter::handleCancel(const ims::sip::SipMessage& request,
                                  std::shared_ptr<ims::sip::ServerTransaction> txn) {
    auto call_id = request.callId();
    IMS_LOG_INFO("CANCEL received, Call-ID={}", call_id);

    // Send 200 OK to CANCEL immediately
    auto ok_resp = ims::sip::createResponse(request, 200, "OK");
    if (ok_resp) {
        txn->sendResponse(std::move(*ok_resp));
    }

    // Look up session to forward CANCEL
    SessionInfo session;
    {
        std::lock_guard lock(sessions_mutex_);
        auto it = sessions_.find(call_id);
        if (it == sessions_.end()) {
            IMS_LOG_WARN("No session found for CANCEL, Call-ID={}", call_id);
            return;
        }
        session = it->second;
        sessions_.erase(it);
    }

    // Build CANCEL for the downstream leg
    auto fwd_cancel = request.clone();
    if (!fwd_cancel) {
        IMS_LOG_ERROR("Failed to clone CANCEL");
        return;
    }

    proxy_.processRouteHeaders(*fwd_cancel);
    while (fwd_cancel->viaCount() > 0) {
        fwd_cancel->removeTopVia();
    }

    if (fwd_cancel->maxForwards() < 0) {
        fwd_cancel->setMaxForwards(70);
    } else {
        fwd_cancel->decrementMaxForwards();
    }

    auto branch = session.callee_invite_branch.empty()
        ? ims::sip::generateBranch()
        : session.callee_invite_branch;
    auto via = std::format("SIP/2.0/UDP {}:{};branch={};rport",
                           sip_stack_.localAddress(), sip_stack_.localPort(), branch);
    fwd_cancel->addVia(via);

    sip_stack_.sendRequest(std::move(*fwd_cancel), session.callee_endpoint,
        [call_id](const ims::sip::SipMessage& response) {
            IMS_LOG_DEBUG("Got response {} for CANCEL Call-ID={}", response.statusCode(), call_id);
        });
}

auto SessionRouter::lookupCallee(const std::string& request_uri)
    -> ims::Result<ims::registration::RegistrationBinding> {
    // Extract IMPU from request URI
    // Request-URI format: sip:user@domain
    std::string impu = request_uri;
    if (impu.find("sip:") == std::string::npos) {
        impu = "sip:" + impu;
    }

    return store_->lookup(impu);
}

} // namespace ims::scscf
