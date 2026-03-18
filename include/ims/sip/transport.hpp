#pragma once

#include "ims/common/types.hpp"

#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/udp.hpp>
#include <array>
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
    void doReceive();

    boost::asio::io_context& io_;
    boost::asio::ip::udp::socket socket_;
    boost::asio::ip::udp::endpoint local_ep_;
    boost::asio::ip::udp::endpoint remote_ep_;
    std::array<char, 65536> recv_buffer_;
    MessageCallback on_message_;
    bool running_ = false;
};

} // namespace ims::sip
