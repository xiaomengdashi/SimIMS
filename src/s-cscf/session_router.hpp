#pragma once

#include "sip/message.hpp"
#include "sip/stack.hpp"
#include "sip/proxy_core.hpp"
#include "sip/transaction.hpp"
#include "sip/store.hpp"
#include "common/types.hpp"

#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>

namespace ims::scscf {

class SessionRouter {
public:
    SessionRouter(std::shared_ptr<ims::registration::IRegistrationStore> store,
                  ims::sip::SipStack& sip_stack,
                  std::optional<ims::sip::Endpoint> peer_icscf = std::nullopt);

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

    /// Handle PRACK - forward in-dialog PRACK statefully
    void handlePrack(const ims::sip::SipMessage& request,
                     std::shared_ptr<ims::sip::ServerTransaction> txn);

private:
    struct DialogKey {
        std::string call_id;
        std::string caller_tag;
        std::string callee_tag;

        auto operator==(const DialogKey&) const -> bool = default;
    };

    struct DialogKeyHash {
        auto operator()(const DialogKey& key) const -> std::size_t;
    };

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

    using SessionMap = std::unordered_map<DialogKey, SessionInfo, DialogKeyHash>;
    using SessionIterator = SessionMap::iterator;

    auto lookupCallee(const std::string& request_uri)
        -> ims::Result<ims::registration::RegistrationBinding>;
    auto resolveBindingDestination(const ims::registration::RegistrationBinding& binding)
        -> ims::Result<ims::sip::Endpoint>;
    auto findSessionForRequestLocked(const ims::sip::SipMessage& request) -> std::optional<SessionIterator>;
    auto findInitialSessionLocked(const std::string& call_id, const std::string& caller_tag) -> std::optional<SessionIterator>;
    auto findCancelSessionLocked(const ims::sip::SipMessage& request) -> std::optional<SessionIterator>;
    auto resolveInDialogDestination(const ims::sip::SipMessage& request, const SessionInfo& session) const
        -> const ims::sip::Endpoint*;
    void recordInviteResponseDialog(const std::string& call_id,
                                    const std::string& caller_tag,
                                    const std::string& callee_tag);
    void eraseSessionLocked(const DialogKey& key);

    std::shared_ptr<ims::registration::IRegistrationStore> store_;
    ims::sip::SipStack& sip_stack_;
    ims::sip::ProxyCore proxy_;
    std::optional<ims::sip::Endpoint> peer_icscf_;

    std::mutex sessions_mutex_;
    SessionMap sessions_;
};

} // namespace ims::scscf
