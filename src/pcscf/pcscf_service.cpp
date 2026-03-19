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
    IMS_LOG_DEBUG("P-CSCF received REGISTER from UE");

    // Add Path header so subsequent requests route through P-CSCF
    auto path = std::format("<sip:{}:{};lr>", config_.listen_addr, config_.listen_port);
    request.addHeader("Path", path);
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
            // Replace SDP with rtpengine's version
            request.setBody(offer_result->sdp, "application/sdp");
            media_sessions_.updateCallerSdp(call_id, *sdp);
            IMS_LOG_DEBUG("SDP rewritten by rtpengine for call={}", call_id);
        } else {
            IMS_LOG_WARN("rtpengine offer failed: {}", offer_result.error().message);
        }
    }

    forwardStatefulToIcscf(std::move(txn), request, true);
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

void PcscfService::forwardStatefulToIcscf(std::shared_ptr<ims::sip::ServerTransaction> txn,
                                          ims::sip::SipMessage& request,
                                          bool add_record_route)
{
    if (proxy_.isLoopDetected(request)) {
        auto resp = ims::sip::createResponse(request, 482, "Loop Detected");
        if (resp) txn->sendResponse(std::move(*resp));
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

    auto prep = proxy_.prepareRequestForForward(request, dest.transport);
    if (!prep) {
        int code = (request.maxForwards() == 0) ? 483 : 500;
        const char* reason = (code == 483) ? "Too Many Hops" : "Internal Server Error";
        IMS_LOG_ERROR("Failed to prepare request forwarding: {}", prep.error().message);
        auto resp = ims::sip::createResponse(request, code, reason);
        if (resp) txn->sendResponse(std::move(*resp));
        return;
    }

    auto send_result = sip_stack_->sendRequest(std::move(request), dest,
        [txn](const ims::sip::SipMessage& response) {
            auto upstream = response.clone();
            if (!upstream) return;
            upstream->removeTopVia();
            txn->sendResponse(std::move(*upstream));
        });

    if (!send_result) {
        IMS_LOG_ERROR("Failed to send request to I-CSCF: {}", send_result.error().message);
        auto resp = ims::sip::createResponse(txn->request(), 500, "Internal Server Error");
        if (resp) txn->sendResponse(std::move(*resp));
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
