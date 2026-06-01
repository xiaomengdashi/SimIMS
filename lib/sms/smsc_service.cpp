#include "smsc_service.hpp"

#include "constants.hpp"
#include "address.hpp"
#include "sms_validator.hpp"
#include "tpdu.hpp"
#include "core/logger.hpp"
#include "sip/uri_utils.hpp"

#include <boost/asio/post.hpp>
#include <format>
#include <span>

namespace ims::sms {

namespace {

void sendResponse(std::shared_ptr<ims::sip::ServerTransaction> txn,
                  ims::sip::SipMessage response,
                  const std::string& context) {
    auto result = txn->sendResponse(std::move(response));
    if (!result) {
        IMS_LOG_WARN("SMSC failed to send {} response: {}", context, result.error().message);
    }
}

} // namespace

SmscService::SmscService(const ims::SmscConfig& config,
                         boost::asio::io_context& io,
                         std::shared_ptr<ims::sip::ITransport> transport)
    : config_(config)
    , psi_(config.psi)
    , listen_{
          .address = config.listen_addr,
          .port = config.listen_port,
          .transport = config.transport.empty() ? "udp" : config.transport,
      }
    , scscf_{
          .address = config.scscf.address,
          .port = config.scscf.port,
          .transport = config.scscf.transport.empty() ? "udp" : config.scscf.transport,
      }
    , io_(io)
    , sip_stack_(std::make_unique<ims::sip::SipStack>(io, listen_.address, listen_.port))
    , proxy_(listen_.address, listen_.port) {
    if (transport) {
        sip_stack_->assignTransport(std::move(transport));
    }
}

auto SmscService::start() -> ims::VoidResult {
    IMS_LOG_INFO("Starting SMSC on {}:{} psi={} scscf={}:{}",
                 listen_.address,
                 listen_.port,
                 psi_,
                 scscf_.address,
                 scscf_.port);

    sip_stack_->onRequest("MESSAGE", [this](auto txn, auto& req) {
        onMessage(std::move(txn), req);
    });

    return sip_stack_->start();
}

void SmscService::stop() {
    IMS_LOG_INFO("Stopping SMSC");
    sip_stack_->stop();
}

void SmscService::processMessage(ims::sip::SipMessage msg, ims::sip::Endpoint source) {
    sip_stack_->transactionLayer().processMessage(std::move(msg), std::move(source));
}

void SmscService::onMessage(std::shared_ptr<ims::sip::ServerTransaction> txn,
                            ims::sip::SipMessage& request) {
    const auto call_id = request.callId();
    const auto content_type = request.contentType().value_or(std::string{});
    const auto body = request.body().value_or(std::string{});

    if (auto valid = validate_sip_message_body(content_type, body); !valid) {
        IMS_LOG_WARN("SMSC invalid MESSAGE payload Call-ID={}: {}", call_id, valid.error().message);
        auto resp = ims::sip::createResponse(request, 400, "Bad Request");
        if (resp) {
            sendResponse(txn, std::move(*resp), "SMSC validation");
        }
        return;
    }

    const auto bytes = std::span<const uint8_t>{
        reinterpret_cast<const uint8_t*>(body.data()), body.size()};
    auto rp = parse_rp_message(bytes);
    if (!rp) {
        IMS_LOG_WARN("SMSC failed to parse RP message Call-ID={}: {}", call_id, rp.error().message);
        auto resp = ims::sip::createResponse(request, 400, "Bad Request");
        if (resp) {
            sendResponse(txn, std::move(*resp), "SMSC parse");
        }
        return;
    }

    if (const auto* data = std::get_if<RpDataMessage>(&*rp)) {
        handleMoRpData(request, txn, *data);
        return;
    }

    if (const auto* ack = std::get_if<RpAckMessage>(&*rp)) {
        (void)ack;
        handleRpAck(request, txn);
        return;
    }

    auto resp = ims::sip::createResponse(request, 415, "Unsupported Media Type");
    if (resp) {
        sendResponse(txn, std::move(*resp), "SMSC unsupported RP");
    }
}

void SmscService::handleMoRpData(const ims::sip::SipMessage& request,
                                 std::shared_ptr<ims::sip::ServerTransaction> txn,
                                 const RpDataMessage& data) {
    IMS_LOG_INFO("SMSC MO RP-DATA Call-ID={} ref={}", request.callId(), data.message_reference);

    auto tpdu = parse_tpdu(data.user_data);
    if (!tpdu || tpdu->type != TpduType::kSubmit) {
        IMS_LOG_WARN("SMSC MO payload is not SMS-SUBMIT Call-ID={}", request.callId());
        auto resp = ims::sip::createResponse(request, 400, "Bad Request");
        if (resp) {
            sendResponse(txn, std::move(*resp), "SMSC MO submit");
        }
        return;
    }

    auto accepted = ims::sip::createResponse(request, 202, "Accepted");
    if (accepted) {
        sendResponse(txn, std::move(*accepted), "SMSC MO 202");
    }

    const auto callee_impu = ims::sip::normalize_impu_uri(request.requestUri());
    const auto message_reference = data.message_reference;
    const bool send_rp_ack = submit_requests_status_report(tpdu->first_octet);
    const auto sender_from = request.fromHeader();

    boost::asio::post(io_, [this, sender_from, callee_impu, data, message_reference, send_rp_ack]() {
        if (send_rp_ack) {
            sendMoRpAck(sender_from, message_reference);
        }
        deliverMt(callee_impu, data, sender_from);
    });
}

void SmscService::handleRpAck(const ims::sip::SipMessage& request,
                              std::shared_ptr<ims::sip::ServerTransaction> txn) {
    IMS_LOG_INFO("SMSC MT RP-ACK Call-ID={}", request.callId());
    auto ok = ims::sip::createResponse(request, 200, "OK");
    if (ok) {
        sendResponse(txn, std::move(*ok), "SMSC MT RP-ACK 200");
    }
}

void SmscService::sendMoRpAck(const std::string& sender_from_header, uint8_t message_reference) {
    const auto sender_impu = ims::sip::normalize_impu_uri(sender_from_header);
    if (sender_impu.empty()) {
        IMS_LOG_WARN("SMSC cannot derive MO originator IMPU for RP-ACK");
        return;
    }

    const auto ack_bytes = build_rp_ack(message_reference);
    auto outbound = buildOutboundMessage(sender_impu,
                                         std::format("<{}>", psi_),
                                         sender_from_header,
                                         ack_bytes);
    if (!outbound) {
        IMS_LOG_WARN("SMSC failed to build MO RP-ACK: {}", outbound.error().message);
        return;
    }

    auto prep = proxy_.prepareRequestForForward(*outbound, scscf_.transport);
    if (!prep) {
        IMS_LOG_WARN("SMSC failed to prepare MO RP-ACK: {}", prep.error().message);
        return;
    }

    const auto call_id = outbound->callId();
    auto send_result = sip_stack_->sendRequest(std::move(*outbound), scscf_,
        [call_id](const ims::sip::SipMessage& response) {
            IMS_LOG_DEBUG("SMSC MO RP-ACK delivery response {} Call-ID={}",
                          response.statusCode(), call_id);
        });
    if (!send_result) {
        IMS_LOG_WARN("SMSC failed to send MO RP-ACK: {}", send_result.error().message);
    }
}

void SmscService::deliverMt(const std::string& callee_impu,
                            const RpDataMessage& mo_data,
                            const std::string& sender_from_header) {
    if (callee_impu.empty()) {
        IMS_LOG_WARN("SMSC MT delivery missing callee IMPU");
        return;
    }

    auto originator = resolve_mo_originator(mo_data, sender_from_header);
    if (!originator) {
        IMS_LOG_WARN("SMSC MT delivery missing originator: {}", originator.error().message);
        return;
    }

    auto mt_rp = build_mt_rp_data_from_mo(mo_data, *originator);
    if (!mt_rp) {
        IMS_LOG_WARN("SMSC failed to build MT RP-DATA: {}", mt_rp.error().message);
        return;
    }

    auto encoded = encode_rp_message(*mt_rp);
    if (!encoded) {
        IMS_LOG_WARN("SMSC failed to encode MT RP-DATA: {}", encoded.error().message);
        return;
    }

    auto outbound = buildOutboundMessage(callee_impu,
                                         std::format("<{}>", psi_),
                                         std::format("<{}>", callee_impu),
                                         *encoded);
    if (!outbound) {
        IMS_LOG_WARN("SMSC failed to build MT MESSAGE: {}", outbound.error().message);
        return;
    }

    auto prep = proxy_.prepareRequestForForward(*outbound, scscf_.transport);
    if (!prep) {
        IMS_LOG_WARN("SMSC failed to prepare MT MESSAGE: {}", prep.error().message);
        return;
    }

    const auto call_id = outbound->callId();
    auto send_result = sip_stack_->sendRequest(std::move(*outbound), scscf_,
        [call_id](const ims::sip::SipMessage& response) {
            IMS_LOG_DEBUG("SMSC MT delivery response {} Call-ID={}",
                          response.statusCode(), call_id);
        });
    if (!send_result) {
        IMS_LOG_WARN("SMSC failed to send MT MESSAGE: {}", send_result.error().message);
    }
}

auto SmscService::buildOutboundMessage(const std::string& request_uri,
                                       const std::string& from_header,
                                       const std::string& to_header,
                                       std::span<const uint8_t> body)
    -> ims::Result<ims::sip::SipMessage> {
    auto created = ims::sip::createRequest("MESSAGE", request_uri);
    if (!created) {
        return std::unexpected(created.error());
    }

    auto& msg = *created;
    msg.setFromHeader(from_header);
    msg.setToHeader(to_header);
    msg.setCallId(ims::sip::generateCallId(listen_.address));
    msg.setCSeq(1, "MESSAGE");
    msg.setMaxForwards(70);
    msg.addHeader("Contact", std::format("<{}>", psi_));

    const std::string body_str(reinterpret_cast<const char*>(body.data()), body.size());
    msg.setBody(body_str, std::string{kContentType3gppSms});

    return created;
}

auto SmscService::resolve_mo_originator(const RpDataMessage& mo_data,
                                        const std::string& sender_from_header)
    -> ims::Result<SmsAddress> {
    if (mo_data.originator && mo_data.originator->digit_length > 0) {
        return *mo_data.originator;
    }

    const auto impu = ims::sip::normalize_impu_uri(sender_from_header);
    if (impu.empty()) {
        return std::unexpected(ims::ErrorInfo{
            ims::ErrorCode::kSmsInvalidPayload,
            "MO MESSAGE missing usable originator IMPU",
        });
    }

    const auto colon = impu.find(':');
    const auto at = impu.find('@');
    if (colon == std::string::npos || at == std::string::npos || at <= colon + 1) {
        return std::unexpected(ims::ErrorInfo{
            ims::ErrorCode::kSmsInvalidPayload,
            "MO MESSAGE From is not a valid IMPU",
        });
    }

    const auto digits = impu.substr(colon + 1, at - colon - 1);
    return encode_bcd_msisdn(digits, 0x91);
}

} // namespace ims::sms
