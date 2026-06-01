#pragma once

#include "core/config.hpp"
#include "core/types.hpp"
#include "rp_message.hpp"
#include "sip/message.hpp"
#include "sip/proxy_core.hpp"
#include "sip/stack.hpp"
#include "sip/transaction.hpp"

#include <boost/asio/io_context.hpp>
#include <memory>
#include <string>

namespace ims::sms {

/// Minimal SMS Center for SMS-over-IMS MO/MT (3GPP TS 24.341).
class SmscService {
public:
    SmscService(const ims::SmscConfig& config,
                boost::asio::io_context& io,
                std::shared_ptr<ims::sip::ITransport> transport = nullptr);

    auto start() -> ims::VoidResult;
    void stop();

    void processMessage(ims::sip::SipMessage msg, ims::sip::Endpoint source);

private:
    void onMessage(std::shared_ptr<ims::sip::ServerTransaction> txn, ims::sip::SipMessage& request);

    void handleMoRpData(const ims::sip::SipMessage& request,
                        std::shared_ptr<ims::sip::ServerTransaction> txn,
                        const RpDataMessage& data);
    void handleRpAck(const ims::sip::SipMessage& request,
                     std::shared_ptr<ims::sip::ServerTransaction> txn);

    void sendMoRpAck(const std::string& sender_from_header, uint8_t message_reference);
    void deliverMt(const std::string& callee_impu,
                   const RpDataMessage& mo_data,
                   const std::string& sender_from_header);

    auto resolve_mo_originator(const RpDataMessage& mo_data, const std::string& sender_from_header)
        -> ims::Result<SmsAddress>;

    auto buildOutboundMessage(const std::string& request_uri,
                              const std::string& from_header,
                              const std::string& to_header,
                              std::span<const uint8_t> body) -> ims::Result<ims::sip::SipMessage>;

    ims::SmscConfig config_;
    std::string psi_;
    ims::sip::Endpoint listen_;
    ims::sip::Endpoint scscf_;
    boost::asio::io_context& io_;
    std::unique_ptr<ims::sip::SipStack> sip_stack_;
    ims::sip::ProxyCore proxy_;
};

} // namespace ims::sms
