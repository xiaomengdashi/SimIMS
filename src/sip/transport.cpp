#include "ims/sip/transport.hpp"
#include "ims/sip/message.hpp"
#include "ims/common/logger.hpp"
#include <boost/asio/ip/udp.hpp>

namespace ims::sip {

UdpTransport::UdpTransport(boost::asio::io_context& io, const std::string& bind_addr, Port port)
    : io_(io)
    , socket_(io)
    , local_ep_(boost::asio::ip::make_address(bind_addr), port) {}

UdpTransport::~UdpTransport() {
    stop();
}

auto UdpTransport::send(const SipMessage& msg, const Endpoint& dest) -> VoidResult {
    auto data = msg.toString();
    if (!data) {
        return std::unexpected(ErrorInfo(
            ErrorCode::kSipTransportError, "Failed to serialize message", data.error().message));
    }

    boost::system::error_code ec;
    boost::asio::ip::udp::endpoint ep(
        boost::asio::ip::make_address(dest.address, ec), dest.port);
    if (ec) {
        return std::unexpected(ErrorInfo(
            ErrorCode::kSipTransportError, "Invalid destination address", ec.message()));
    }

    socket_.send_to(boost::asio::buffer(*data), ep, 0, ec);
    if (ec) {
        return std::unexpected(ErrorInfo{
            ErrorCode::kSipTransportError, "UDP send failed", ec.message()});
    }

    IMS_LOG_DEBUG("SIP TX to {}:{} ({} bytes)", dest.address, dest.port, data->size());
    return {};
}

void UdpTransport::setMessageCallback(MessageCallback cb) {
    on_message_ = std::move(cb);
}

auto UdpTransport::start() -> VoidResult {
    boost::system::error_code ec;

    socket_.open(local_ep_.protocol(), ec);
    if (ec) {
        return std::unexpected(ErrorInfo{
            ErrorCode::kSipTransportError, "Failed to open UDP socket", ec.message()});
    }

    socket_.set_option(boost::asio::socket_base::reuse_address(true), ec);
    socket_.bind(local_ep_, ec);
    if (ec) {
        return std::unexpected(ErrorInfo{
            ErrorCode::kSipTransportError, "Failed to bind UDP socket", ec.message()});
    }

    // Update local endpoint after bind (in case port was 0)
    local_ep_ = socket_.local_endpoint(ec);

    running_ = true;
    doReceive();

    IMS_LOG_INFO("UDP transport started on {}:{}", local_ep_.address().to_string(), local_ep_.port());
    return {};
}

void UdpTransport::stop() {
    running_ = false;
    boost::system::error_code ec;
    if (socket_.is_open()) {
        socket_.cancel(ec);
        socket_.close(ec);
    }
    IMS_LOG_INFO("UDP transport stopped");
}

auto UdpTransport::localEndpoint() const -> Endpoint {
    return Endpoint{
        .address = local_ep_.address().to_string(),
        .port = local_ep_.port(),
        .transport = "udp"
    };
}

void UdpTransport::doReceive() {
    if (!running_) return;

    socket_.async_receive_from(
        boost::asio::buffer(recv_buffer_), remote_ep_,
        [this](boost::system::error_code ec, std::size_t bytes_received) {
            if (ec) {
                if (ec != boost::asio::error::operation_aborted) {
                    IMS_LOG_WARN("UDP receive error: {}", ec.message());
                }
                if (running_) doReceive();
                return;
            }

            IMS_LOG_DEBUG("SIP RX from {}:{} ({} bytes)",
                remote_ep_.address().to_string(), remote_ep_.port(), bytes_received);

            std::string raw(recv_buffer_.data(), bytes_received);
            auto msg_result = SipMessage::parse(raw);
            if (!msg_result) {
                IMS_LOG_WARN("Failed to parse SIP message: {}", msg_result.error().message);
                doReceive();
                return;
            }

            if (on_message_) {
                Endpoint src{
                    .address = remote_ep_.address().to_string(),
                    .port = static_cast<Port>(remote_ep_.port()),
                    .transport = "udp"
                };
                on_message_(std::move(*msg_result), std::move(src));
            }

            doReceive();
        });
}

} // namespace ims::sip
