#include "session_router.hpp"
#include "common/logger.hpp"
#include "sip/uri_utils.hpp"

#include <algorithm>
#include <cctype>
#include <format>
#include <optional>

namespace ims::scscf {

namespace {

auto isUsableDestination(const ims::sip::Endpoint& endpoint) -> bool {
    return !endpoint.address.empty() && endpoint.address != "0.0.0.0";
}

auto dialogEquals(const ims::sip::DialogId& lhs, const ims::sip::DialogId& rhs) -> bool {
    return lhs.call_id == rhs.call_id
        && lhs.local_tag == rhs.local_tag
        && lhs.remote_tag == rhs.remote_tag;
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

    // Set Request-URI to the callee's contact URI without Contact params.
    fwd_request->setRequestUri(ims::sip::extract_uri_from_name_addr(contacts[0]->contact_uri));
    proxy_.processRouteHeaders(*fwd_request);

    // Add Record-Route so subsequent requests pass through us
    proxy_.addRecordRoute(*fwd_request);

    // Determine destination from contact's Path or contact URI
    ims::sip::Endpoint dest;
    if (!contacts[0]->path.empty()) {
        // Path header contains P-CSCF route URI.
        auto parsed = ims::sip::parse_endpoint_from_uri(contacts[0]->path);
        if (parsed && isUsableDestination(*parsed)) {
            dest = *parsed;
        }
    }
    if (!isUsableDestination(dest)) {
        auto parsed = ims::sip::parse_endpoint_from_uri(contacts[0]->contact_uri);
        if (parsed) {
            dest = *parsed;
        }
    }

    if (!isUsableDestination(dest)) {
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

    {
        std::lock_guard lock(sessions_mutex_);
        sessions_[call_id] = SessionInfo{
            .call_id = call_id,
            .caller_tag = request.fromTag(),
            .callee_tag = request.toTag(),
            .caller_impu = from,
            .callee_impu = binding->impu,
            .caller_endpoint = txn->source(),
            .callee_endpoint = dest,
            .callee_invite_branch = invite_branch,
        };
    }

    IMS_LOG_DEBUG("Forwarding INVITE to callee contact={} path={} dest={}:{} transport={}",
                  contacts[0]->contact_uri,
                  contacts[0]->path.empty() ? "<none>" : contacts[0]->path,
                  dest.address,
                  dest.port,
                  dest.transport);

    sip_stack_.sendRequest(std::move(*fwd_request), dest,
        [this, txn, call_id](const ims::sip::SipMessage& response) {
            IMS_LOG_DEBUG("Got response {} for INVITE Call-ID={}", response.statusCode(), call_id);
            {
                std::lock_guard lock(sessions_mutex_);
                auto it = sessions_.find(call_id);
                if (it != sessions_.end() && response.statusCode() >= 200 && response.statusCode() < 300) {
                    it->second.callee_tag = response.toTag();
                }
            }
            auto upstream = proxy_.forwardResponseUpstream(response, txn);
            if (!upstream) {
                IMS_LOG_WARN("Failed to forward INVITE response upstream: {}", upstream.error().message);
            }
        });
}

void SessionRouter::handleBye(const ims::sip::SipMessage& request,
                               std::shared_ptr<ims::sip::ServerTransaction> txn) {
    auto call_id = request.callId();
    IMS_LOG_INFO("BYE received, Call-ID={}", call_id);

    auto fwd_bye = request.clone();
    if (!fwd_bye) {
        auto resp = ims::sip::createResponse(request, 500, "Server Internal Error");
        if (resp) txn->sendResponse(std::move(*resp));
        return;
    }

    ims::sip::Endpoint dest;
    {
        std::lock_guard lock(sessions_mutex_);
        auto* session = findSessionByCallId(call_id);
        if (!session) {
            IMS_LOG_WARN("No session found for BYE, Call-ID={} from_tag={} to_tag={}", call_id, request.fromTag(), request.toTag());
            auto resp = ims::sip::createResponse(request, 481, "Call/Transaction Does Not Exist");
            if (resp) txn->sendResponse(std::move(*resp));
            return;
        }
        auto* resolved = resolveInDialogDestination(request, *session);
        if (!resolved) {
            IMS_LOG_WARN("Unable to resolve BYE destination, Call-ID={} from_tag={} to_tag={}", call_id, request.fromTag(), request.toTag());
            auto resp = ims::sip::createResponse(request, 481, "Call/Transaction Does Not Exist");
            if (resp) txn->sendResponse(std::move(*resp));
            return;
        }
        dest = *resolved;
        session->bye_seen = true;
    }

    proxy_.processRouteHeaders(*fwd_bye);
    auto prep = proxy_.prepareRequestForForward(*fwd_bye, dest.transport);
    if (!prep) {
        auto resp = ims::sip::createResponse(request, 500, "Server Internal Error");
        if (resp) txn->sendResponse(std::move(*resp));
        return;
    }

    sip_stack_.sendRequest(std::move(*fwd_bye), dest,
        [this, txn, call_id](const ims::sip::SipMessage& response) {
            IMS_LOG_DEBUG("Got response {} for BYE Call-ID={}", response.statusCode(), call_id);
            auto upstream = proxy_.forwardResponseUpstream(response, txn);
            if (!upstream) {
                IMS_LOG_WARN("Failed to forward BYE response upstream: {}", upstream.error().message);
                return;
            }
            if (response.statusCode() >= 200 && response.statusCode() < 300) {
                std::lock_guard lock(sessions_mutex_);
                auto* session = findSessionByCallId(call_id);
                if (session && session->bye_seen) {
                    eraseSession(call_id);
                }
            }
        });
}

void SessionRouter::handleAck(const ims::sip::SipMessage& request,
                              std::shared_ptr<ims::sip::ServerTransaction> /*txn*/) {
    auto call_id = request.callId();
    IMS_LOG_INFO("ACK received, Call-ID={}", call_id);

    auto fwd_ack = request.clone();
    if (!fwd_ack) {
        IMS_LOG_WARN("Failed to clone ACK for forwarding, Call-ID={}", call_id);
        return;
    }

    ims::sip::Endpoint dest;
    {
        std::lock_guard lock(sessions_mutex_);
        auto* session = findSessionByCallId(call_id);
        if (!session) {
            IMS_LOG_WARN("No session found for ACK, Call-ID={} from_tag={} to_tag={}", call_id, request.fromTag(), request.toTag());
            return;
        }
        auto* resolved = resolveInDialogDestination(request, *session);
        if (!resolved) {
            IMS_LOG_WARN("Unable to resolve ACK destination, Call-ID={} from_tag={} to_tag={}", call_id, request.fromTag(), request.toTag());
            return;
        }
        dest = *resolved;
    }

    proxy_.processRouteHeaders(*fwd_ack);
    auto result = proxy_.forwardRequest(*fwd_ack, dest, sip_stack_.transport());
    if (!result) {
        IMS_LOG_WARN("Failed to forward ACK for Call-ID={}: {}", call_id, result.error().message);
    }
}

void SessionRouter::handleCancel(const ims::sip::SipMessage& request,
                                 std::shared_ptr<ims::sip::ServerTransaction> txn) {
    auto call_id = request.callId();
    IMS_LOG_INFO("CANCEL received, Call-ID={}", call_id);

    auto ok_resp = ims::sip::createResponse(request, 200, "OK");
    if (ok_resp) {
        txn->sendResponse(std::move(*ok_resp));
    }

    SessionInfo session;
    {
        std::lock_guard lock(sessions_mutex_);
        auto* found = findSessionByCallId(call_id);
        if (!found) {
            IMS_LOG_WARN("No session found for CANCEL, Call-ID={} from_tag={} to_tag={}", call_id, request.fromTag(), request.toTag());
            return;
        }
        session = *found;
        eraseSession(call_id);
    }

    auto fwd_cancel = proxy_.buildForwardedCancel(request, {
        .transport = session.callee_endpoint.transport,
        .invite_branch = session.callee_invite_branch,
        .local_address = sip_stack_.localAddress(),
        .local_port = sip_stack_.localPort(),
    });
    if (!fwd_cancel) {
        IMS_LOG_ERROR("Failed to build forwarded CANCEL: {}", fwd_cancel.error().message);
        return;
    }

    sip_stack_.sendRequest(std::move(*fwd_cancel), session.callee_endpoint,
        [call_id](const ims::sip::SipMessage& response) {
            IMS_LOG_DEBUG("Got response {} for CANCEL Call-ID={}", response.statusCode(), call_id);
        });
}

auto SessionRouter::lookupCallee(const std::string& request_uri)
    -> ims::Result<ims::registration::RegistrationBinding> {
    return store_->lookup(ims::sip::normalize_impu_uri(request_uri));
}

auto SessionRouter::findSessionByCallId(const std::string& call_id) -> SessionInfo* {
    auto it = sessions_.find(call_id);
    if (it == sessions_.end()) {
        return nullptr;
    }
    return &it->second;
}

auto SessionRouter::resolveInDialogDestination(const ims::sip::SipMessage& request,
                                               SessionInfo& session) const
    -> const ims::sip::Endpoint* {
    if (!session.callee_tag.empty() && request.fromTag() == session.caller_tag && request.toTag() == session.callee_tag) {
        return &session.callee_endpoint;
    }
    if (!session.callee_tag.empty() && request.fromTag() == session.callee_tag && request.toTag() == session.caller_tag) {
        return &session.caller_endpoint;
    }
    return nullptr;
}

void SessionRouter::eraseSession(const std::string& call_id) {
    sessions_.erase(call_id);
}

} // namespace ims::scscf
