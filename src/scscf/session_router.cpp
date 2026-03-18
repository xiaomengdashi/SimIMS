#include "session_router.hpp"
#include "ims/common/logger.hpp"

#include <format>

namespace ims::scscf {

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

    // Add Record-Route so subsequent requests pass through us
    proxy_.addRecordRoute(*fwd_request);

    // Determine destination from contact's Path or contact URI
    ims::sip::Endpoint dest;
    if (!contacts[0]->path.empty()) {
        // Path header contains P-CSCF address - route through it
        dest.address = contacts[0]->path;
        dest.port = 5060;
    } else {
        // Extract host:port from contact URI
        dest.address = "127.0.0.1";
        dest.port = 5060;
    }

    // Track session
    {
        std::lock_guard lock(sessions_mutex_);
        sessions_[call_id] = SessionInfo{
            .caller_impu = from,
            .callee_impu = binding->impu,
            .caller_endpoint = {},
            .callee_endpoint = dest,
        };
    }

    // Forward INVITE via client transaction
    auto via = std::format("SIP/2.0/UDP {}:{};branch={}",
        sip_stack_.localAddress(), sip_stack_.localPort(), ims::sip::generateBranch());
    fwd_request->addVia(via);
    fwd_request->decrementMaxForwards();

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

    // Forward BYE to the other side
    auto fwd_bye = request.clone();
    if (!fwd_bye) {
        auto resp = ims::sip::createResponse(request, 500, "Server Internal Error");
        if (resp) txn->sendResponse(std::move(*resp));
        return;
    }

    auto via = std::format("SIP/2.0/UDP {}:{};branch={}",
        sip_stack_.localAddress(), sip_stack_.localPort(), ims::sip::generateBranch());
    fwd_bye->addVia(via);
    fwd_bye->decrementMaxForwards();

    sip_stack_.sendRequest(std::move(*fwd_bye), session.callee_endpoint,
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

    auto via = std::format("SIP/2.0/UDP {}:{};branch={}",
        sip_stack_.localAddress(), sip_stack_.localPort(), ims::sip::generateBranch());
    fwd_cancel->addVia(via);
    fwd_cancel->decrementMaxForwards();

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
