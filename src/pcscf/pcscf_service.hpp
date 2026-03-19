#pragma once

#include "ims/sip/stack.hpp"
#include "ims/sip/proxy_core.hpp"
#include "ims/common/config.hpp"
#include "ims/diameter/ipcf_client.hpp"
#include "ims/media/rtpengine_client.hpp"
#include "../media/media_session.hpp"

#include <memory>

namespace ims::pcscf {

/// P-CSCF: first contact point for UE.
/// Handles:
///   - REGISTER forwarding to I-CSCF with Path header
///   - INVITE processing with SDP/rtpengine media anchoring
///   - QoS request via Rx interface on 183/200
///   - BYE cleanup (rtpengine delete, Rx STR)
class PcscfService {
public:
    PcscfService(const ims::PcscfConfig& config,
                 boost::asio::io_context& io,
                 std::shared_ptr<ims::diameter::IPcfClient> pcf,
                 std::shared_ptr<ims::media::IRtpengineClient> rtpengine,
                 const std::string& icscf_addr,
                 ims::Port icscf_port);

    auto start() -> VoidResult;
    void stop();

private:
    void onRegister(std::shared_ptr<ims::sip::ServerTransaction> txn,
                    ims::sip::SipMessage& request);
    void onInvite(std::shared_ptr<ims::sip::ServerTransaction> txn,
                  ims::sip::SipMessage& request);
    void onBye(std::shared_ptr<ims::sip::ServerTransaction> txn,
               ims::sip::SipMessage& request);
    void onAck(std::shared_ptr<ims::sip::ServerTransaction> txn,
               ims::sip::SipMessage& request);
    void onCancel(std::shared_ptr<ims::sip::ServerTransaction> txn,
                  ims::sip::SipMessage& request);
    void onPrack(std::shared_ptr<ims::sip::ServerTransaction> txn,
                 ims::sip::SipMessage& request);
    void onSubscribe(std::shared_ptr<ims::sip::ServerTransaction> txn,
                     ims::sip::SipMessage& request);
    void forwardStatefulToIcscf(std::shared_ptr<ims::sip::ServerTransaction> txn,
                                ims::sip::SipMessage& request,
                                bool add_record_route = false);
    void forwardStatelessToIcscf(ims::sip::SipMessage& request,
                                 bool add_record_route = false);

    ims::PcscfConfig config_;
    std::unique_ptr<ims::sip::SipStack> sip_stack_;
    ims::sip::ProxyCore proxy_;
    std::shared_ptr<ims::diameter::IPcfClient> pcf_;
    std::shared_ptr<ims::media::IRtpengineClient> rtpengine_;
    ims::media::MediaSessionManager media_sessions_;

    std::string icscf_addr_;
    ims::Port icscf_port_;
};

} // namespace ims::pcscf
