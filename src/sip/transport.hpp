#pragma once

#include "common/types.hpp"

#include <boost/asio/io_context.hpp>
#include <functional>
#include <memory>
#include <string>

namespace ims::sip {

class SipMessage;

struct Endpoint {
    std::string address;
    Port port = 5060;
    std::string transport = "udp";
};

struct ITransport {
    virtual ~ITransport() = default;
    virtual auto send(const SipMessage& msg, const Endpoint& dest) -> VoidResult = 0;
    using MessageCallback = std::function<void(SipMessage, Endpoint)>;
    virtual void setMessageCallback(MessageCallback cb) = 0;
    virtual auto start() -> VoidResult = 0;
    virtual void stop() = 0;
    virtual auto localEndpoint() const -> Endpoint = 0;
};

class UdpTransport : public ITransport {
public:
    UdpTransport(boost::asio::io_context& io, const std::string& bind_addr, Port port);
    ~UdpTransport() override;

    auto send(const SipMessage& msg, const Endpoint& dest) -> VoidResult override;
    void setMessageCallback(MessageCallback cb) override;
    auto start() -> VoidResult override;
    void stop() override;
    auto localEndpoint() const -> Endpoint override;

private:
    struct State;

    void doReceive();
    static void doReceive(std::shared_ptr<State> state);

    std::shared_ptr<State> state_;
};

class TcpTransport : public ITransport {
public:
    TcpTransport(boost::asio::io_context& io, const std::string& bind_addr, Port port);
    ~TcpTransport() override;

    auto send(const SipMessage& msg, const Endpoint& dest) -> VoidResult override;
    void setMessageCallback(MessageCallback cb) override;
    auto start() -> VoidResult override;
    void stop() override;
    auto localEndpoint() const -> Endpoint override;

private:
    struct Connection;
    struct State;

    void doAccept();
    static void doAccept(std::shared_ptr<State> state);
    static void handleIncomingMessage(const std::shared_ptr<State>& state,
                                      const std::string& raw,
                                      const Endpoint& src);
    auto getOrCreateConnection(const Endpoint& dest) -> Result<std::shared_ptr<Connection>>;
    static auto getOrCreateConnection(const std::shared_ptr<State>& state,
                                      const Endpoint& dest) -> Result<std::shared_ptr<Connection>>;
    static void unregisterConnection(const std::shared_ptr<State>& state,
                                     const Endpoint& endpoint,
                                     const Connection* connection);
    static auto endpointKey(const Endpoint& endpoint) -> std::string;

    std::shared_ptr<State> state_;
};

class DualTransport : public ITransport {
public:
    DualTransport(boost::asio::io_context& io, const std::string& bind_addr, Port port);
    ~DualTransport() override;

    auto send(const SipMessage& msg, const Endpoint& dest) -> VoidResult override;
    void setMessageCallback(MessageCallback cb) override;
    auto start() -> VoidResult override;
    void stop() override;
    auto localEndpoint() const -> Endpoint override;

private:
    struct State;

    std::shared_ptr<State> state_;
};

} // namespace ims::sip
