#include "scscf_service.hpp"
#include "ims/common/logger.hpp"

namespace ims::scscf {

ScscfService::ScscfService(const ims::ScscfConfig& config,
                           boost::asio::io_context& io,
                           std::shared_ptr<ims::diameter::IHssClient> hss,
                           std::shared_ptr<ims::registration::IRegistrationStore> store)
    : config_(config)
    , sip_stack_(std::make_unique<ims::sip::SipStack>(
          io, config.listen_addr, config.listen_port))
    , hss_(std::move(hss))
    , store_(std::move(store))
{
    registrar_ = std::make_unique<Registrar>(store_, hss_, config.domain);
    session_router_ = std::make_unique<SessionRouter>(store_, *sip_stack_);
}

auto ScscfService::start() -> VoidResult {
    IMS_LOG_INFO("Starting S-CSCF on {}:{} domain={}",
                 config_.listen_addr, config_.listen_port, config_.domain);

    sip_stack_->onRequest("REGISTER", [this](auto txn, auto& req) {
        onRegister(txn, req);
    });
    sip_stack_->onRequest("INVITE", [this](auto txn, auto& req) {
        onInvite(txn, req);
    });
    sip_stack_->onRequest("BYE", [this](auto txn, auto& req) {
        onBye(txn, req);
    });
    sip_stack_->onRequest("CANCEL", [this](auto txn, auto& req) {
        onCancel(txn, req);
    });

    return sip_stack_->start();
}

void ScscfService::stop() {
    IMS_LOG_INFO("Stopping S-CSCF");
    sip_stack_->stop();
}

void ScscfService::onRegister(std::shared_ptr<ims::sip::ServerTransaction> txn,
                               ims::sip::SipMessage& request)
{
    IMS_LOG_DEBUG("S-CSCF received REGISTER");
    registrar_->handleRegister(request, txn);
}

void ScscfService::onInvite(std::shared_ptr<ims::sip::ServerTransaction> txn,
                             ims::sip::SipMessage& request)
{
    IMS_LOG_DEBUG("S-CSCF received INVITE");
    session_router_->handleInvite(request, txn);
}

void ScscfService::onBye(std::shared_ptr<ims::sip::ServerTransaction> txn,
                          ims::sip::SipMessage& request)
{
    IMS_LOG_DEBUG("S-CSCF received BYE");
    session_router_->handleBye(request, txn);
}

void ScscfService::onCancel(std::shared_ptr<ims::sip::ServerTransaction> txn,
                             ims::sip::SipMessage& request)
{
    IMS_LOG_DEBUG("S-CSCF received CANCEL");
    session_router_->handleCancel(request, txn);
}

} // namespace ims::scscf
