#pragma once

#include "digest_credential_store.hpp"
#include "registrar.hpp"
#include "session_router.hpp"
#include "sip/stack.hpp"
#include "sip/reg_event_notifier.hpp"
#include "common/config.hpp"
#include "common/types.hpp"
#include "diameter/ihss_client.hpp"
#include "sip/store.hpp"

#include <boost/asio/steady_timer.hpp>
#include <memory>

namespace ims::scscf {

class ScscfServiceTestPeer;

class ScscfService {
public:
    ScscfService(const ims::ScscfConfig& config,
                 boost::asio::io_context& io,
                 std::shared_ptr<ims::diameter::IHssClient> hss,
                 std::shared_ptr<ims::registration::IRegistrationStore> store,
                 std::unique_ptr<ims::sip::IRegEventNotifier> reg_event_notifier = nullptr);

    ScscfService(const ims::ScscfConfig& config,
                 boost::asio::io_context& io,
                 std::shared_ptr<ims::diameter::IHssClient> hss,
                 std::shared_ptr<ims::registration::IRegistrationStore> store,
                 std::shared_ptr<IDigestCredentialStore> digest_store,
                 std::unique_ptr<ims::sip::IRegEventNotifier> reg_event_notifier);

    auto start() -> VoidResult;
    void stop();

private:
    friend class ScscfServiceTestPeer;

    void onRegister(std::shared_ptr<ims::sip::ServerTransaction> txn,
                    ims::sip::SipMessage& request);
    void onInvite(std::shared_ptr<ims::sip::ServerTransaction> txn,
                  ims::sip::SipMessage& request);
    void onBye(std::shared_ptr<ims::sip::ServerTransaction> txn,
               ims::sip::SipMessage& request);
    void onCancel(std::shared_ptr<ims::sip::ServerTransaction> txn,
                  ims::sip::SipMessage& request);
    void onPrack(std::shared_ptr<ims::sip::ServerTransaction> txn,
                 ims::sip::SipMessage& request);
    void onSubscribe(std::shared_ptr<ims::sip::ServerTransaction> txn,
                     ims::sip::SipMessage& request);
    void sendInitialNotify(const ims::sip::SipMessage& subscribe,
                           const std::string& to_tag);
    void scheduleRegistrationCleanup();
    void runRegistrationCleanup();

private:

    ims::ScscfConfig config_;
    std::unique_ptr<ims::sip::SipStack> sip_stack_;
    std::unique_ptr<ims::sip::IRegEventNotifier> reg_event_notifier_;
    std::unique_ptr<Registrar> registrar_;
    std::unique_ptr<SessionRouter> session_router_;
    std::shared_ptr<ims::diameter::IHssClient> hss_;
    std::shared_ptr<ims::registration::IRegistrationStore> store_;
    std::shared_ptr<IDigestCredentialStore> digest_store_;
    boost::asio::steady_timer registration_cleanup_timer_;
};

} // namespace ims::scscf
