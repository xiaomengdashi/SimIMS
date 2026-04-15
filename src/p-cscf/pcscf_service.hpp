#pragma once

#include "sip/stack.hpp"
#include "sip/proxy_core.hpp"
#include "common/config.hpp"
#include "diameter/ipcf_client.hpp"
#include "rtp/rtpengine_client.hpp"
#include "rtp/media_session.hpp"

#include <memory>
#include <mutex>
#include <optional>
#include <unordered_map>

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
                 const std::string& core_entry_addr,
                 ims::Port core_entry_port);

    auto start() -> VoidResult;
    void stop();

private:
    auto isCoreFacingRequest(const ims::sip::ServerTransaction& txn) const -> bool;
    auto resolveUeDestination(const ims::sip::SipMessage& request) const -> std::optional<ims::sip::Endpoint>;
    auto resolveCoreDestination(const ims::sip::SipMessage& request) const -> ims::sip::Endpoint;
    auto extractTopologyToken(const ims::sip::SipMessage& request) const -> std::optional<std::string>;
    auto createTopologyToken() -> std::string;
    void rememberTopologyRoute(const std::string& token, const ims::sip::Endpoint& endpoint);
    void addTopologyRecordRoute(ims::sip::SipMessage& request, const std::string& token) const;
    void sanitizeForUeEgress(ims::sip::SipMessage& request);

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
    void onInviteResponse(ims::sip::SipMessage& response);
    void forwardStatefulToCore(std::shared_ptr<ims::sip::ServerTransaction> txn,
                                ims::sip::SipMessage& request,
                                bool add_record_route = false,
                                bool process_invite_response_media = false);
    void forwardStatefulToUe(std::shared_ptr<ims::sip::ServerTransaction> txn,
                             ims::sip::SipMessage& request,
                             bool add_record_route = true);
    void forwardStatelessToCore(ims::sip::SipMessage& request,
                                 bool add_record_route = false);
    void forwardStatelessToUe(ims::sip::SipMessage& request,
                              bool add_record_route = true);

    ims::PcscfConfig config_;
    std::unique_ptr<ims::sip::SipStack> sip_stack_;
    ims::sip::ProxyCore proxy_;
    std::shared_ptr<ims::diameter::IPcfClient> pcf_;
    std::shared_ptr<ims::media::IRtpengineClient> rtpengine_;
    ims::media::MediaSessionManager media_sessions_;

    std::string core_entry_addr_;
    ims::Port core_entry_port_;
    std::string proxy_public_addr_;

    mutable std::mutex topology_mutex_;
    std::unordered_map<std::string, ims::sip::Endpoint> topology_routes_;
};

} // namespace ims::pcscf
