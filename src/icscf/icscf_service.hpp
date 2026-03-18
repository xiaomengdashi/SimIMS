#pragma once

#include "scscf_selector.hpp"
#include "ims/sip/stack.hpp"
#include "ims/sip/proxy_core.hpp"
#include "ims/common/config.hpp"
#include "ims/diameter/ihss_client.hpp"

#include <memory>

namespace ims::icscf {

class IcscfService {
public:
    IcscfService(const ims::IcscfConfig& config,
                 boost::asio::io_context& io,
                 std::shared_ptr<ims::diameter::IHssClient> hss);

    auto start() -> VoidResult;
    void stop();

private:
    void onRegister(std::shared_ptr<ims::sip::ServerTransaction> txn,
                    ims::sip::SipMessage& request);
    void onInvite(std::shared_ptr<ims::sip::ServerTransaction> txn,
                  ims::sip::SipMessage& request);

    ims::IcscfConfig config_;
    std::unique_ptr<ims::sip::SipStack> sip_stack_;
    std::unique_ptr<ScscfSelector> selector_;
    ims::sip::ProxyCore proxy_;
};

} // namespace ims::icscf
