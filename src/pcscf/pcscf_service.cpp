#include "pcscf_service.hpp"
#include "ims/common/logger.hpp"

#include <format>

namespace ims::pcscf {

PcscfService::PcscfService(const ims::PcscfConfig& config,
                           boost::asio::io_context& io,
                           std::shared_ptr<ims::diameter::IPcfClient> pcf,
                           std::shared_ptr<ims::media::IRtpengineClient> rtpengine,
                           const std::string& icscf_addr,
                           ims::Port icscf_port)
    : config_(config)
    , sip_stack_(std::make_unique<ims::sip::SipStack>(
          io, config.listen_addr, config.listen_port))
    , proxy_(config.listen_addr, config.listen_port)
    , pcf_(std::move(pcf))
    , rtpengine_(std::move(rtpengine))
    , icscf_addr_(icscf_addr)
    , icscf_port_(icscf_port)
{
}

auto PcscfService::start() -> VoidResult {
    IMS_LOG_INFO("Starting P-CSCF on {}:{}", config_.listen_addr, config_.listen_port);

    sip_stack_->onRequest("REGISTER", [this](auto txn, auto& req) { onRegister(txn, req); });
    sip_stack_->onRequest("INVITE", [this](auto txn, auto& req) { onInvite(txn, req); });
    sip_stack_->onRequest("BYE", [this](auto txn, auto& req) { onBye(txn, req); });
    sip_stack_->onRequest("ACK", [this](auto txn, auto& req) { onAck(txn, req); });
    sip_stack_->onRequest("CANCEL", [this](auto txn, auto& req) { onCancel(txn, req); });
    sip_stack_->onRequest("PRACK", [this](auto txn, auto& req) { onPrack(txn, req); });

    return sip_stack_->start();
}

void PcscfService::stop() {
    IMS_LOG_INFO("Stopping P-CSCF");
    sip_stack_->stop();
}

void PcscfService::onRegister(std::shared_ptr<ims::sip::ServerTransaction> txn,
                               ims::sip::SipMessage& request)
{
    IMS_LOG_DEBUG("P-CSCF received REGISTER from UE");

    // Add Path header so subsequent requests route through P-CSCF
    auto path = std::format("<sip:{}:{};lr>", config_.listen_addr, config_.listen_port);
    request.addHeader("Path", path);

    // Forward to I-CSCF
    ims::sip::Endpoint dest{
        .address = icscf_addr_,
        .port = icscf_port_,
        .transport = "udp"
    };

    auto result = proxy_.forwardRequest(request, dest, sip_stack_->transport());
    if (!result) {
        IMS_LOG_ERROR("Failed to forward REGISTER: {}", result.error().message);
        auto resp = ims::sip::createResponse(request, 500, "Internal Server Error");
        if (resp) txn->sendResponse(std::move(*resp));
    }
}

void PcscfService::onInvite(std::shared_ptr<ims::sip::ServerTransaction> txn,
                              ims::sip::SipMessage& request)
{
    IMS_LOG_DEBUG("P-CSCF received INVITE");

    auto call_id = request.callId();
    auto from_tag = request.fromTag();

    // Create media session tracking
    auto media_session = media_sessions_.createSession(call_id, from_tag);

    // Process SDP through rtpengine (offer)
    auto sdp = request.body();
    if (sdp && rtpengine_) {
        ims::media::RtpengineFlags flags{
            .direction_from = "internal",
            .direction_to = "internal",
            .ice_remove = true,
        };

        auto offer_result = rtpengine_->offer(media_session, *sdp, flags);
        if (offer_result) {
            // Replace SDP with rtpengine's version
            request.setBody(offer_result->sdp, "application/sdp");
            media_sessions_.updateCallerSdp(call_id, *sdp);
            IMS_LOG_DEBUG("SDP rewritten by rtpengine for call={}", call_id);
        } else {
            IMS_LOG_WARN("rtpengine offer failed: {}", offer_result.error().message);
        }
    }

    // Add Record-Route
    proxy_.addRecordRoute(request);

    // Forward to I-CSCF/S-CSCF
    ims::sip::Endpoint dest{
        .address = icscf_addr_,
        .port = icscf_port_,
        .transport = "udp"
    };

    auto result = proxy_.forwardRequest(request, dest, sip_stack_->transport());
    if (!result) {
        IMS_LOG_ERROR("Failed to forward INVITE: {}", result.error().message);
        auto resp = ims::sip::createResponse(request, 500, "Internal Server Error");
        if (resp) txn->sendResponse(std::move(*resp));
        media_sessions_.removeSession(call_id);
    }
}

void PcscfService::onBye(std::shared_ptr<ims::sip::ServerTransaction> txn,
                           ims::sip::SipMessage& request)
{
    auto call_id = request.callId();
    IMS_LOG_DEBUG("P-CSCF received BYE for call={}", call_id);

    // Clean up rtpengine session
    auto session_state = media_sessions_.getSession(call_id);
    if (session_state && rtpengine_) {
        rtpengine_->deleteSession(session_state->session);
    }

    // Release QoS via Rx STR
    if (session_state && !session_state->rx_session_id.empty() && pcf_) {
        ims::diameter::StrParams str{
            .session_id = session_state->rx_session_id,
            .termination_cause = 1,  // DIAMETER_LOGOUT
        };
        pcf_->terminateSession(str);
    }

    media_sessions_.removeSession(call_id);

    // Forward BYE
    // In a real implementation, would use dialog route set
    auto resp = ims::sip::createResponse(request, 200, "OK");
    if (resp) txn->sendResponse(std::move(*resp));
}

void PcscfService::onAck(std::shared_ptr<ims::sip::ServerTransaction> /*txn*/,
                          ims::sip::SipMessage& request)
{
    IMS_LOG_DEBUG("P-CSCF received ACK for call={}", request.callId());
    // Forward ACK - stateless
}

void PcscfService::onCancel(std::shared_ptr<ims::sip::ServerTransaction> txn,
                             ims::sip::SipMessage& request)
{
    auto call_id = request.callId();
    IMS_LOG_DEBUG("P-CSCF received CANCEL for call={}", call_id);

    // Clean up media session
    auto session_state = media_sessions_.getSession(call_id);
    if (session_state && rtpengine_) {
        rtpengine_->deleteSession(session_state->session);
    }
    media_sessions_.removeSession(call_id);

    auto resp = ims::sip::createResponse(request, 200, "OK");
    if (resp) txn->sendResponse(std::move(*resp));
}

void PcscfService::onPrack(std::shared_ptr<ims::sip::ServerTransaction> txn,
                            ims::sip::SipMessage& request)
{
    IMS_LOG_DEBUG("P-CSCF received PRACK for call={}", request.callId());
    // Forward PRACK through the dialog route
    auto resp = ims::sip::createResponse(request, 200, "OK");
    if (resp) txn->sendResponse(std::move(*resp));
}

} // namespace ims::pcscf
