#include "icscf_service.hpp"
#include "ims/common/logger.hpp"

#include <format>

namespace ims::icscf {

IcscfService::IcscfService(const ims::IcscfConfig& config,
                           boost::asio::io_context& io,
                           std::shared_ptr<ims::diameter::IHssClient> hss)
    : config_(config)
    , sip_stack_(std::make_unique<ims::sip::SipStack>(
          io, config.listen_addr, config.listen_port))
    , selector_(std::make_unique<ScscfSelector>(std::move(hss)))
    , proxy_(config.listen_addr, config.listen_port)
{
}

auto IcscfService::start() -> VoidResult {
    IMS_LOG_INFO("Starting I-CSCF on {}:{}", config_.listen_addr, config_.listen_port);

    sip_stack_->onRequest("REGISTER", [this](auto txn, auto& req) {
        onRegister(txn, req);
    });
    sip_stack_->onRequest("INVITE", [this](auto txn, auto& req) {
        onInvite(txn, req);
    });

    return sip_stack_->start();
}

void IcscfService::stop() {
    IMS_LOG_INFO("Stopping I-CSCF");
    sip_stack_->stop();
}

void IcscfService::onRegister(std::shared_ptr<ims::sip::ServerTransaction> txn,
                               ims::sip::SipMessage& request)
{
    IMS_LOG_DEBUG("I-CSCF received REGISTER");

    // Extract IMPI/IMPU from the REGISTER
    auto from = request.fromHeader();
    auto to = request.toHeader();

    // Simplified: extract user@domain from To header
    std::string impu;
    auto start = to.find("sip:");
    if (start != std::string::npos) {
        auto end = to.find('>', start);
        impu = (end != std::string::npos)
            ? to.substr(start, end - start)
            : to.substr(start);
    }

    std::string impi;
    start = from.find("sip:");
    if (start != std::string::npos) {
        auto end = from.find('>', start);
        auto uri = (end != std::string::npos)
            ? from.substr(start + 4, end - start - 4)
            : from.substr(start + 4);
        impi = uri;
    }

    // Query HSS for S-CSCF assignment
    auto scscf_result = selector_->selectForRegistration(impi, impu, config_.hss.realm);
    if (!scscf_result) {
        IMS_LOG_ERROR("S-CSCF selection failed: {}", scscf_result.error().message);
        auto resp = ims::sip::createResponse(request, 500, "Internal Server Error");
        if (resp) txn->sendResponse(std::move(*resp));
        return;
    }

    // Forward REGISTER to selected S-CSCF
    // Parse S-CSCF URI to get host:port
    auto& scscf_uri = *scscf_result;
    std::string host;
    ims::Port port = 5060;

    // Parse "sip:host:port" or "sip:host"
    auto sip_pos = scscf_uri.find("sip:");
    if (sip_pos != std::string::npos) {
        auto host_start = sip_pos + 4;
        auto colon_pos = scscf_uri.find(':', host_start);
        if (colon_pos != std::string::npos) {
            host = scscf_uri.substr(host_start, colon_pos - host_start);
            port = static_cast<ims::Port>(std::stoi(scscf_uri.substr(colon_pos + 1)));
        } else {
            host = scscf_uri.substr(host_start);
        }
    }

    ims::sip::Endpoint dest{.address = host, .port = port, .transport = "udp"};

    auto fwd_result = proxy_.forwardRequest(request, dest, sip_stack_->transport());
    if (!fwd_result) {
        IMS_LOG_ERROR("Failed to forward REGISTER to S-CSCF: {}", fwd_result.error().message);
        auto resp = ims::sip::createResponse(request, 500, "Internal Server Error");
        if (resp) txn->sendResponse(std::move(*resp));
    }
}

void IcscfService::onInvite(std::shared_ptr<ims::sip::ServerTransaction> txn,
                             ims::sip::SipMessage& request)
{
    IMS_LOG_DEBUG("I-CSCF received INVITE (MT routing)");

    // Extract callee IMPU from Request-URI
    auto callee_uri = request.requestUri();
    std::string impu = "sip:" + callee_uri;

    // Query HSS for serving S-CSCF
    auto scscf_result = selector_->selectForRouting(impu);
    if (!scscf_result) {
        IMS_LOG_WARN("No serving S-CSCF for {}", impu);
        auto resp = ims::sip::createResponse(request, 404, "Not Found");
        if (resp) txn->sendResponse(std::move(*resp));
        return;
    }

    // Forward INVITE to serving S-CSCF
    auto& scscf_uri = *scscf_result;
    std::string host;
    ims::Port port = 5060;

    auto sip_pos = scscf_uri.find("sip:");
    if (sip_pos != std::string::npos) {
        auto host_start = sip_pos + 4;
        auto colon_pos = scscf_uri.find(':', host_start);
        if (colon_pos != std::string::npos) {
            host = scscf_uri.substr(host_start, colon_pos - host_start);
            port = static_cast<ims::Port>(std::stoi(scscf_uri.substr(colon_pos + 1)));
        } else {
            host = scscf_uri.substr(host_start);
        }
    }

    ims::sip::Endpoint dest{.address = host, .port = port, .transport = "udp"};

    auto fwd_result = proxy_.forwardRequest(request, dest, sip_stack_->transport());
    if (!fwd_result) {
        IMS_LOG_ERROR("Failed to forward INVITE: {}", fwd_result.error().message);
        auto resp = ims::sip::createResponse(request, 500, "Internal Server Error");
        if (resp) txn->sendResponse(std::move(*resp));
    }
}

} // namespace ims::icscf
