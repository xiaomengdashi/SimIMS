#pragma once

#include "ims/sip/message.hpp"
#include "ims/sip/transport.hpp"
#include "ims/common/types.hpp"

#include <boost/asio/io_context.hpp>
#include <boost/asio/steady_timer.hpp>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

namespace ims::sip {

enum class TransactionState {
    kTrying,
    kProceeding,
    kCompleted,
    kConfirmed,
    kTerminated,
};

class ServerTransaction {
public:
    ServerTransaction(SipMessage request, std::shared_ptr<ITransport> transport,
                      Endpoint source, boost::asio::io_context& io);

    auto request() const -> const SipMessage&;
    auto state() const -> TransactionState;
    auto branch() const -> const std::string&;
    auto sendResponse(SipMessage response) -> VoidResult;
    void onTerminated(std::function<void()> cb);

private:
    void startTimers();

    SipMessage request_;
    std::shared_ptr<ITransport> transport_;
    Endpoint source_;
    boost::asio::io_context& io_;
    TransactionState state_ = TransactionState::kTrying;
    std::string branch_;
    std::function<void()> on_terminated_;
    boost::asio::steady_timer timer_j_;
    boost::asio::steady_timer timer_h_;
};

class ClientTransaction {
public:
    using ResponseCallback = std::function<void(const SipMessage&)>;

    ClientTransaction(SipMessage request, std::shared_ptr<ITransport> transport,
                      Endpoint dest, boost::asio::io_context& io);

    auto request() const -> const SipMessage&;
    auto state() const -> TransactionState;
    auto branch() const -> const std::string&;

    void start();
    void onResponse(ResponseCallback cb);
    void onTimeout(std::function<void()> cb);
    bool matches(const SipMessage& response) const;
    void processResponse(SipMessage response);

private:
    void retransmit();

    SipMessage request_;
    std::shared_ptr<ITransport> transport_;
    Endpoint dest_;
    boost::asio::io_context& io_;
    TransactionState state_ = TransactionState::kTrying;
    std::string branch_;
    ResponseCallback on_response_;
    std::function<void()> on_timeout_;
    boost::asio::steady_timer timer_a_;
    boost::asio::steady_timer timer_b_;
    int retransmit_count_ = 0;
};

class TransactionLayer {
public:
    using RequestHandler = std::function<void(std::shared_ptr<ServerTransaction>)>;

    TransactionLayer(boost::asio::io_context& io, std::shared_ptr<ITransport> transport);

    void setRequestHandler(RequestHandler handler);
    auto sendRequest(SipMessage request, const Endpoint& dest,
                     ClientTransaction::ResponseCallback on_response) -> Result<std::string>;
    void processMessage(SipMessage msg, Endpoint source);

private:
    auto findClientTransaction(const SipMessage& response) -> std::shared_ptr<ClientTransaction>;
    auto getTransactionKey(const SipMessage& msg) const -> std::string;

    boost::asio::io_context& io_;
    std::shared_ptr<ITransport> transport_;
    RequestHandler request_handler_;
    std::mutex mutex_;
    std::unordered_map<std::string, std::shared_ptr<ServerTransaction>> server_txns_;
    std::unordered_map<std::string, std::shared_ptr<ClientTransaction>> client_txns_;
};

} // namespace ims::sip
