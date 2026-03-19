#pragma once

#include "registrar.hpp"
#include "session_router.hpp"
#include "ims/sip/stack.hpp"
#include "ims/common/config.hpp"
#include "ims/diameter/ihss_client.hpp"
#include "ims/registration/store.hpp"

#include <memory>

namespace ims::scscf {

class ScscfService {
public:
    ScscfService(const ims::ScscfConfig& config,
                 boost::asio::io_context& io,
                 std::shared_ptr<ims::diameter::IHssClient> hss,
                 std::shared_ptr<ims::registration::IRegistrationStore> store);

    auto start() -> VoidResult;
    void stop();

private:
    void onRegister(std::shared_ptr<ims::sip::ServerTransaction> txn,
                    ims::sip::SipMessage& request);
    void onInvite(std::shared_ptr<ims::sip::ServerTransaction> txn,
                  ims::sip::SipMessage& request);
    void onBye(std::shared_ptr<ims::sip::ServerTransaction> txn,
               ims::sip::SipMessage& request);
    void onCancel(std::shared_ptr<ims::sip::ServerTransaction> txn,
                  ims::sip::SipMessage& request);
    void onSubscribe(std::shared_ptr<ims::sip::ServerTransaction> txn,
                     ims::sip::SipMessage& request);
    void sendInitialNotify(const ims::sip::SipMessage& subscribe,
                           const std::string& to_tag);

    ims::ScscfConfig config_;
    std::unique_ptr<ims::sip::SipStack> sip_stack_;
    std::unique_ptr<Registrar> registrar_;
    std::unique_ptr<SessionRouter> session_router_;
    std::shared_ptr<ims::diameter::IHssClient> hss_;
    std::shared_ptr<ims::registration::IRegistrationStore> store_;
};

} // namespace ims::scscf
