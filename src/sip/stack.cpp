#include "stack.hpp"
#include "common/logger.hpp"

namespace ims::sip {

SipStack::SipStack(boost::asio::io_context& io, const std::string& bind_addr, Port port)
    : io_(io)
    , transport_(std::make_shared<DualTransport>(io, bind_addr, port)) {
    txn_layer_ = std::make_unique<TransactionLayer>(io, transport_);
}

SipStack::~SipStack() {
    stop();
}

auto SipStack::start() -> VoidResult {
    // Wire transport -> transaction layer
    transport_->setMessageCallback(
        [this](SipMessage msg, Endpoint source) {
            txn_layer_->processMessage(std::move(msg), std::move(source));
        });

    // Wire transaction layer -> method dispatch
    txn_layer_->setRequestHandler(
        [this](std::shared_ptr<ServerTransaction> txn) {
            handleRequest(std::move(txn));
        });

    auto result = transport_->start();
    if (!result) {
        return result;
    }

    IMS_LOG_INFO("SIP stack started on {}:{}", localAddress(), localPort());
    return {};
}

void SipStack::stop() {
    transport_->stop();
    IMS_LOG_INFO("SIP stack stopped");
}

void SipStack::onRequest(const std::string& method, RequestHandler handler) {
    method_handlers_[method] = std::move(handler);
}

void SipStack::onDefaultRequest(RequestHandler handler) {
    default_handler_ = std::move(handler);
}

auto SipStack::sendRequest(SipMessage request, const Endpoint& dest,
                           ClientTransaction::ResponseCallback on_response) -> VoidResult {
    auto result = txn_layer_->sendRequest(std::move(request), dest, std::move(on_response));
    if (!result) {
        return std::unexpected(result.error());
    }
    return {};
}

auto SipStack::transport() -> std::shared_ptr<ITransport> {
    return transport_;
}

auto SipStack::transactionLayer() -> TransactionLayer& {
    return *txn_layer_;
}

auto SipStack::localAddress() const -> std::string {
    return transport_->localEndpoint().address;
}

auto SipStack::localPort() const -> Port {
    return transport_->localEndpoint().port;
}

void SipStack::handleRequest(std::shared_ptr<ServerTransaction> txn) {
    auto method = txn->request().method();
    IMS_LOG_DEBUG("Dispatching {} request", method);

    auto it = method_handlers_.find(method);
    if (it != method_handlers_.end()) {
        // Clone request for the handler (transaction owns the original)
        auto req_clone = txn->request().clone();
        if (req_clone) {
            it->second(txn, *req_clone);
        } else {
            IMS_LOG_ERROR("Failed to clone request for handler");
        }
    } else if (default_handler_) {
        auto req_clone = txn->request().clone();
        if (req_clone) {
            default_handler_(txn, *req_clone);
        }
    } else {
        // No handler - send 405 Method Not Allowed
        auto resp = createResponse(txn->request(), 405, "Method Not Allowed");
        if (resp) {
            txn->sendResponse(std::move(*resp));
        }
    }
}

} // namespace ims::sip
