#include "pcscf_service.hpp"
#include "common/logger.hpp"

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
    sip_stack_->onRequest("SUBSCRIBE", [this](auto txn, auto& req) { onSubscribe(txn, req); });

    return sip_stack_->start();
}

void PcscfService::stop() {
    IMS_LOG_INFO("Stopping P-CSCF");
    sip_stack_->stop();
}

void PcscfService::onRegister(std::shared_ptr<ims::sip::ServerTransaction> txn,
                               ims::sip::SipMessage& request)
{
    IMS_LOG_DEBUG("P-CSCF received REGISTER from UE via_count={} top_via={}",
                  request.viaCount(), request.topVia());

    proxy_.addPathHeader(request);
    forwardStatefulToIcscf(std::move(txn), request);
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
            media_sessions_.updateCallerSdp(call_id, *sdp);
            if (!offer_result->sdp.empty()) {
                // Replace SDP with rtpengine's version
                request.setBody(offer_result->sdp, "application/sdp");
                IMS_LOG_DEBUG("SDP rewritten by rtpengine for call={}", call_id);
            } else {
                IMS_LOG_WARN("rtpengine offer returned empty SDP for call={}, keeping original SDP", call_id);
            }
        } else {
            IMS_LOG_WARN("rtpengine offer failed: {}", offer_result.error().message);
        }
    }

    forwardStatefulToIcscf(std::move(txn), request, true, true);
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

    // Forward BYE along signaling path
    forwardStatefulToIcscf(std::move(txn), request);
}

void PcscfService::onAck(std::shared_ptr<ims::sip::ServerTransaction> /*txn*/,
                          ims::sip::SipMessage& request)
{
    IMS_LOG_DEBUG("P-CSCF received ACK for call={}", request.callId());
    // ACK has no response; forward statelessly.
    forwardStatelessToIcscf(request);
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

    forwardStatefulToIcscf(std::move(txn), request);
}

void PcscfService::onPrack(std::shared_ptr<ims::sip::ServerTransaction> txn,
                            ims::sip::SipMessage& request)
{
    IMS_LOG_DEBUG("P-CSCF received PRACK for call={}", request.callId());
    forwardStatefulToIcscf(std::move(txn), request);
}

void PcscfService::onSubscribe(std::shared_ptr<ims::sip::ServerTransaction> txn,
                                ims::sip::SipMessage& request)
{
    IMS_LOG_DEBUG("P-CSCF received SUBSCRIBE for {}", request.requestUri());
    forwardStatefulToIcscf(std::move(txn), request, true);
}

void PcscfService::onInviteResponse(ims::sip::SipMessage& response) {
    if (!response.isResponse()) {
        return;
    }
    if (response.cseqMethod() != "INVITE") {
        return;
    }

    const int status = response.statusCode();
    if (status != 183 && (status < 200 || status >= 300)) {
        return;
    }

    auto sdp = response.body();
    if (!sdp || !rtpengine_) {
        return;
    }

    const auto call_id = response.callId();
    auto session_state = media_sessions_.getSession(call_id);
    if (!session_state) {
        IMS_LOG_DEBUG("No media session found while handling INVITE response call={}", call_id);
        return;
    }

    auto to_tag = response.toTag();
    if (!to_tag.empty()) {
        media_sessions_.updateToTag(call_id, to_tag);
        session_state->session.to_tag = std::move(to_tag);
    }

    ims::media::RtpengineFlags flags{
        .direction_from = "internal",
        .direction_to = "internal",
        .ice_remove = true,
    };

    auto answer_result = rtpengine_->answer(session_state->session, *sdp, flags);
    if (!answer_result) {
        IMS_LOG_WARN("rtpengine answer failed for call={}: {}", call_id, answer_result.error().message);
        return;
    }

    media_sessions_.updateCalleeSdp(call_id, *sdp);
    if (!answer_result->sdp.empty()) {
        response.setBody(answer_result->sdp, "application/sdp");
        IMS_LOG_DEBUG("SDP answer rewritten by rtpengine for call={}", call_id);
        return;
    }

    IMS_LOG_WARN("rtpengine answer returned empty SDP for call={}, keeping original SDP", call_id);
}

void PcscfService::forwardStatefulToIcscf(std::shared_ptr<ims::sip::ServerTransaction> txn,
                                          ims::sip::SipMessage& request,
                                          bool add_record_route,
                                          bool process_invite_response_media)
{
    ims::sip::Endpoint dest{
        .address = icscf_addr_,
        .port = icscf_port_,
        .transport = "udp"
    };

    ims::sip::ForwardOptions options{
        .add_record_route = add_record_route,
    };
    if (process_invite_response_media) {
        options.on_response = [this](ims::sip::SipMessage& response) {
            onInviteResponse(response);
        };
    }

    auto result = proxy_.forwardStateful(request, dest, txn, *sip_stack_, options);
    if (!result) {
        IMS_LOG_ERROR("Failed to send request to I-CSCF: {}", result.error().message);
    }
}

void PcscfService::forwardStatelessToIcscf(ims::sip::SipMessage& request,
                                           bool add_record_route)
{
    if (proxy_.isLoopDetected(request)) {
        IMS_LOG_WARN("Dropping request due to loop detection");
        return;
    }

    proxy_.processRouteHeaders(request);
    if (add_record_route) {
        proxy_.addRecordRoute(request);
    }

    ims::sip::Endpoint dest{
        .address = icscf_addr_,
        .port = icscf_port_,
        .transport = "udp"
    };

    auto result = proxy_.forwardRequest(request, dest, sip_stack_->transport());
    if (!result) {
        IMS_LOG_ERROR("Failed to forward stateless request: {}", result.error().message);
    }
}

} // namespace ims::pcscf
