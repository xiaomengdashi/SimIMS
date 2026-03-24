#pragma once

#include "scscf_selector.hpp"
#include "sip/stack.hpp"
#include "sip/proxy_core.hpp"
#include "common/config.hpp"
#include "diameter/ihss_client.hpp"

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
    void onSubscribe(std::shared_ptr<ims::sip::ServerTransaction> txn,
                     ims::sip::SipMessage& request);
    void forwardStateful(std::shared_ptr<ims::sip::ServerTransaction> txn,
                         ims::sip::SipMessage& request,
                         const ims::sip::Endpoint& dest,
                         bool add_record_route = false);

    ims::IcscfConfig config_;
    std::unique_ptr<ims::sip::SipStack> sip_stack_;
    std::unique_ptr<ScscfSelector> selector_;
    ims::sip::ProxyCore proxy_;
};

} // namespace ims::icscf
