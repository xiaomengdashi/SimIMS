#pragma once

#include "i_auth_provider.hpp"
#include "sip/message.hpp"
#include "sip/transaction.hpp"
#include "sip/store.hpp"
#include "diameter/ihss_client.hpp"

#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace ims::scscf {

class Registrar {
public:
    Registrar(std::shared_ptr<ims::registration::IRegistrationStore> store,
              std::vector<std::shared_ptr<IAuthProvider>> auth_providers,
              std::shared_ptr<ims::diameter::IHssClient> hss,
              const std::string& domain = "ims.local");

    /// Handle incoming REGISTER request
    void handleRegister(ims::sip::SipMessage& request,
                        std::shared_ptr<ims::sip::ServerTransaction> txn);

private:
    void sendChallenge(ims::sip::SipMessage& request,
                       std::shared_ptr<ims::sip::ServerTransaction> txn);

    void verifyAndRegister(ims::sip::SipMessage& request,
                           std::shared_ptr<ims::sip::ServerTransaction> txn);
    bool tryHandleReregister(ims::sip::SipMessage& request,
                             std::shared_ptr<ims::sip::ServerTransaction> txn);

    void handleDeregister(ims::sip::SipMessage& request,
                          std::shared_ptr<ims::sip::ServerTransaction> txn);
    void sendRegisterOk(ims::sip::SipMessage& request,
                        std::shared_ptr<ims::sip::ServerTransaction> txn,
                        uint32_t expires,
                        const std::optional<std::string>& contact,
                        const std::vector<std::string>& associated_impus);

    auto extractImpi(const ims::sip::SipMessage& msg) const -> std::string;
    auto extractImpu(const ims::sip::SipMessage& msg) const -> std::string;
    bool isDeregister(const ims::sip::SipMessage& msg) const;
    bool hasAuthorization(const ims::sip::SipMessage& msg) const;
    auto selectProviderForInitialRegister(const ims::sip::SipMessage& request) const
        -> std::shared_ptr<IAuthProvider>;
    auto selectProviderForAuthorization(const ims::sip::SipMessage& request) const
        -> std::shared_ptr<IAuthProvider>;

    std::shared_ptr<ims::registration::IRegistrationStore> store_;
    std::vector<std::shared_ptr<IAuthProvider>> auth_providers_;
    std::shared_ptr<ims::diameter::IHssClient> hss_;
    std::string domain_;
};

} // namespace ims::scscf
