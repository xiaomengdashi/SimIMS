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

auto isInDialogRequest(const ims::sip::SipMessage& request) -> bool {
    return !request.toTag().empty();
}

} // namespace

SessionRouter::SessionRouter(std::shared_ptr<ims::registration::IRegistrationStore> store,
                             ims::sip::SipStack& sip_stack,
                             std::optional<ims::sip::Endpoint> peer_icscf)
    : store_(std::move(store))
    , sip_stack_(sip_stack)
    , proxy_(sip_stack.localAddress(), sip_stack.localPort())
    , peer_icscf_(std::move(peer_icscf)) {
    IMS_LOG_INFO("SessionRouter initialized");
}

void SessionRouter::handleInvite(const ims::sip::SipMessage& request,
                                 std::shared_ptr<ims::sip::ServerTransaction> txn) {
    auto call_id = request.callId();
    auto from = request.fromHeader();
    auto request_uri = request.requestUri();
    auto in_dialog = isInDialogRequest(request);

    IMS_LOG_INFO("INVITE received, Call-ID={}, From={}, To={}", call_id, from, request_uri);

    // Send 100 Trying immediately
    auto trying = ims::sip::createResponse(request, 100, "Trying");
    if (trying) {
        txn->sendResponse(std::move(*trying));
    }

    // Clone the request for forwarding
    auto fwd_request = request.clone();
    if (!fwd_request) {
        IMS_LOG_ERROR("Failed to clone INVITE for forwarding");
        auto resp = ims::sip::createResponse(request, 500, "Server Internal Error");
        if (resp) txn->sendResponse(std::move(*resp));
        return;
    }

    ims::sip::Endpoint dest;
    std::string contact_uri = "<existing-target>";
    std::string path = "<none>";
    std::string callee_impu;

    if (in_dialog) {
        std::lock_guard lock(sessions_mutex_);
        auto* session = findSessionByCallId(call_id);
        if (!session) {
            IMS_LOG_WARN("No session found for in-dialog INVITE, Call-ID={} from_tag={} to_tag={}",
                         call_id, request.fromTag(), request.toTag());
            auto resp = ims::sip::createResponse(request, 481, "Call/Transaction Does Not Exist");
            if (resp) txn->sendResponse(std::move(*resp));
            return;
        }

        auto* resolved = resolveInDialogDestination(request, *session);
        if (!resolved) {
            IMS_LOG_WARN("Unable to resolve in-dialog INVITE destination, Call-ID={} from_tag={} to_tag={}",
                         call_id, request.fromTag(), request.toTag());
            auto resp = ims::sip::createResponse(request, 481, "Call/Transaction Does Not Exist");
            if (resp) txn->sendResponse(std::move(*resp));
            return;
        }

        dest = *resolved;
    } else {
        auto binding = lookupCallee(request_uri);
        if (!binding) {
            if (!peer_icscf_) {
                IMS_LOG_WARN("Callee not found and no peer I-CSCF configured: {}", request_uri);
                auto resp = ims::sip::createResponse(request, 404, "Not Found");
                if (resp) txn->sendResponse(std::move(*resp));
                return;
            }
            IMS_LOG_INFO("Callee {} not found locally, forwarding INVITE to peer I-CSCF {}:{}",
                         request_uri, peer_icscf_->address, peer_icscf_->port);
            dest = *peer_icscf_;
            callee_impu = ims::sip::normalize_impu_uri(request_uri);
        } else {
            auto contacts = binding->active_contacts();
            if (contacts.empty()) {
                IMS_LOG_WARN("No active contacts for callee: {}", request_uri);
                auto resp = ims::sip::createResponse(request, 480, "Temporarily Unavailable");
                if (resp) txn->sendResponse(std::move(*resp));
                return;
            }

            contact_uri = contacts[0]->contact_uri;
            if (!contacts[0]->path.empty()) {
                path = contacts[0]->path;
            }
            callee_impu = binding->impu;
            auto target_uri = ims::sip::extract_uri_from_name_addr(contact_uri);
            if (!target_uri.empty()) {
                fwd_request->setRequestUri(target_uri);
            }

            auto resolved_dest = resolveBindingDestination(*binding);
            if (!resolved_dest) {
                IMS_LOG_WARN("Failed to resolve INVITE destination for {}: {}",
                             request_uri, resolved_dest.error().message);
                auto resp = ims::sip::createResponse(request, 480, "Temporarily Unavailable");
                if (resp) txn->sendResponse(std::move(*resp));
                return;
            }

            dest = *resolved_dest;
        }
    }

    proxy_.processRouteHeaders(*fwd_request);

    if (!in_dialog) {
        // Topology exposure reduction: S-CSCF does not add Record-Route towards UE side.
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
        auto& session = sessions_[call_id];
        if (session.call_id.empty()) {
            session.call_id = call_id;
            session.caller_tag = request.fromTag();
            session.caller_impu = from;
            session.caller_endpoint = txn->source();
        }
        if (!in_dialog) {
            session.callee_tag = request.toTag();
            session.callee_impu = callee_impu;
            session.callee_endpoint = dest;
            session.callee_invite_branch = invite_branch;
            session.bye_seen = false;
        }
    }

    IMS_LOG_DEBUG("Forwarding {}INVITE request_uri={} contact={} path={} dest={}:{} transport={}",
                  in_dialog ? "in-dialog " : "",
                  fwd_request->requestUri(),
                  contact_uri,
                  path,
                  dest.address,
                  dest.port,
                  dest.transport);

    sip_stack_.sendRequest(std::move(*fwd_request), dest,
        [this, txn, call_id](const ims::sip::SipMessage& response) {
            IMS_LOG_DEBUG("Got response {} for INVITE Call-ID={}", response.statusCode(), call_id);
            {
                std::lock_guard lock(sessions_mutex_);
                auto it = sessions_.find(call_id);
                if (it != sessions_.end() && !response.toTag().empty()) {
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

void SessionRouter::handlePrack(const ims::sip::SipMessage& request,
                                std::shared_ptr<ims::sip::ServerTransaction> txn) {
    auto call_id = request.callId();
    IMS_LOG_INFO("PRACK received, Call-ID={}", call_id);

    auto fwd_prack = request.clone();
    if (!fwd_prack) {
        auto resp = ims::sip::createResponse(request, 500, "Server Internal Error");
        if (resp) txn->sendResponse(std::move(*resp));
        return;
    }

    ims::sip::Endpoint dest;
    {
        std::lock_guard lock(sessions_mutex_);
        auto* session = findSessionByCallId(call_id);
        if (!session) {
            IMS_LOG_WARN("No session found for PRACK, Call-ID={} from_tag={} to_tag={}", call_id, request.fromTag(), request.toTag());
            auto resp = ims::sip::createResponse(request, 481, "Call/Transaction Does Not Exist");
            if (resp) txn->sendResponse(std::move(*resp));
            return;
        }
        auto* resolved = resolveInDialogDestination(request, *session);
        if (!resolved) {
            IMS_LOG_WARN("Unable to resolve PRACK destination, Call-ID={} from_tag={} to_tag={}", call_id, request.fromTag(), request.toTag());
            auto resp = ims::sip::createResponse(request, 481, "Call/Transaction Does Not Exist");
            if (resp) txn->sendResponse(std::move(*resp));
            return;
        }
        dest = *resolved;
    }

    proxy_.processRouteHeaders(*fwd_prack);
    auto prep = proxy_.prepareRequestForForward(*fwd_prack, dest.transport);
    if (!prep) {
        auto resp = ims::sip::createResponse(request, 500, "Server Internal Error");
        if (resp) txn->sendResponse(std::move(*resp));
        return;
    }

    sip_stack_.sendRequest(std::move(*fwd_prack), dest,
        [this, txn, call_id](const ims::sip::SipMessage& response) {
            IMS_LOG_DEBUG("Got response {} for PRACK Call-ID={}", response.statusCode(), call_id);
            auto upstream = proxy_.forwardResponseUpstream(response, txn);
            if (!upstream) {
                IMS_LOG_WARN("Failed to forward PRACK response upstream: {}", upstream.error().message);
            }
        });
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

auto SessionRouter::resolveBindingDestination(const ims::registration::RegistrationBinding& binding)
    -> ims::Result<ims::sip::Endpoint> {
    auto contacts = binding.active_contacts();
    if (contacts.empty()) {
        return std::unexpected(ims::ErrorInfo{
            ims::ErrorCode::kRegistrationExpired,
            "No active contacts for callee"
        });
    }

    ims::sip::Endpoint dest;
    if (!contacts[0]->path.empty()) {
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
        return std::unexpected(ims::ErrorInfo{
            ims::ErrorCode::kSipDialogNotFound,
            "Failed to derive destination from Path or Contact"
        });
    }

    return dest;
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
