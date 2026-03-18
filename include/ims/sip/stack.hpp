#pragma once

#include "ims/sip/message.hpp"
#include "ims/sip/transport.hpp"
#include "ims/sip/transaction.hpp"
#include "ims/sip/dialog.hpp"
#include "ims/common/types.hpp"

#include <boost/asio/io_context.hpp>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>

namespace ims::sip {

class SipStack {
public:
    using RequestHandler = std::function<void(std::shared_ptr<ServerTransaction>, SipMessage&)>;

    SipStack(boost::asio::io_context& io, const std::string& bind_addr, Port port);
    ~SipStack();

    auto start() -> VoidResult;
    void stop();

    void onRequest(const std::string& method, RequestHandler handler);
    void onDefaultRequest(RequestHandler handler);

    auto sendRequest(SipMessage request, const Endpoint& dest,
                     ClientTransaction::ResponseCallback on_response) -> VoidResult;

    auto transport() -> std::shared_ptr<ITransport>;
    auto transactionLayer() -> TransactionLayer&;
    auto dialogManager() -> DialogManager&;

    auto localAddress() const -> std::string;
    auto localPort() const -> Port;

private:
    void handleRequest(std::shared_ptr<ServerTransaction> txn);

    boost::asio::io_context& io_;
    std::shared_ptr<UdpTransport> transport_;
    std::unique_ptr<TransactionLayer> txn_layer_;
    DialogManager dialog_mgr_;
    std::unordered_map<std::string, RequestHandler> method_handlers_;
    RequestHandler default_handler_;
};

} // namespace ims::sip
