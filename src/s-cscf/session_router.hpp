#pragma once

#include "sip/message.hpp"
#include "sip/stack.hpp"
#include "sip/proxy_core.hpp"
#include "sip/transaction.hpp"
#include "sip/store.hpp"
#include "common/types.hpp"

#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

namespace ims::scscf {

class SessionRouter {
public:
    SessionRouter(std::shared_ptr<ims::registration::IRegistrationStore> store,
                  ims::sip::SipStack& sip_stack);

    /// Handle incoming INVITE - look up callee and forward
    void handleInvite(const ims::sip::SipMessage& request,
                      std::shared_ptr<ims::sip::ServerTransaction> txn);

    /// Handle BYE - forward to appropriate side
    void handleBye(const ims::sip::SipMessage& request,
                   std::shared_ptr<ims::sip::ServerTransaction> txn);

    /// Handle CANCEL - forward CANCEL
    void handleCancel(const ims::sip::SipMessage& request,
                      std::shared_ptr<ims::sip::ServerTransaction> txn);

    /// Handle ACK - forward in-dialog ACK statelessly
    void handleAck(const ims::sip::SipMessage& request,
                   std::shared_ptr<ims::sip::ServerTransaction> txn);

private:
    struct SessionInfo {
        std::string call_id;
        std::string caller_tag;
        std::string callee_tag;
        std::string caller_impu;
        std::string callee_impu;
        ims::sip::Endpoint caller_endpoint;
        ims::sip::Endpoint callee_endpoint;
        std::string callee_invite_branch;
        bool bye_seen = false;
    };

    auto lookupCallee(const std::string& request_uri)
        -> ims::Result<ims::registration::RegistrationBinding>;
    auto findSessionByCallId(const std::string& call_id) -> SessionInfo*;
    auto resolveInDialogDestination(const ims::sip::SipMessage& request, SessionInfo& session) const
        -> const ims::sip::Endpoint*;
    void eraseSession(const std::string& call_id);

    std::shared_ptr<ims::registration::IRegistrationStore> store_;
    ims::sip::SipStack& sip_stack_;
    ims::sip::ProxyCore proxy_;

    std::mutex sessions_mutex_;
    std::unordered_map<std::string, SessionInfo> sessions_;
};

} // namespace ims::scscf
