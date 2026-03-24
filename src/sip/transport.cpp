#include "transport.hpp"
#include "message.hpp"
#include "common/logger.hpp"

#include <boost/asio/write.hpp>

#include <algorithm>
#include <cctype>
#include <optional>
#include <sstream>
#include <vector>

namespace ims::sip {

namespace {

auto trim(std::string value) -> std::string {
    auto is_space = [](unsigned char ch) { return std::isspace(ch) != 0; };
    value.erase(value.begin(), std::find_if(value.begin(), value.end(),
                                            [&](unsigned char ch) { return !is_space(ch); }));
    value.erase(std::find_if(value.rbegin(), value.rend(),
                             [&](unsigned char ch) { return !is_space(ch); }).base(),
                value.end());
    return value;
}

auto toLower(std::string value) -> std::string {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return value;
}

auto parseContentLength(const std::string& headers) -> size_t {
    std::istringstream stream(headers);
    std::string line;
    while (std::getline(stream, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        auto colon = line.find(':');
        if (colon == std::string::npos) {
            continue;
        }

        auto name = toLower(trim(line.substr(0, colon)));
        if (name != "content-length" && name != "l") {
            continue;
        }

        auto value = trim(line.substr(colon + 1));
        if (value.empty()) {
            return 0;
        }

        try {
            return static_cast<size_t>(std::stoul(value));
        } catch (...) {
            return 0;
        }
    }
    return 0;
}

auto tryExtractSipMessage(std::string& buffer) -> std::optional<std::string> {
    constexpr std::string_view kHeaderTerminator = "\r\n\r\n";

    auto header_end = buffer.find(kHeaderTerminator);
    if (header_end == std::string::npos) {
        return std::nullopt;
    }

    auto headers_len = header_end + kHeaderTerminator.size();
    auto content_length = parseContentLength(buffer.substr(0, headers_len));
    auto full_len = headers_len + content_length;
    if (buffer.size() < full_len) {
        return std::nullopt;
    }

    auto message = buffer.substr(0, full_len);
    buffer.erase(0, full_len);
    return message;
}

} // namespace

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

    IMS_LOG_DEBUG("SIP TX/UDP to {}:{} ({} bytes)", dest.address, dest.port, data->size());
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

    local_ep_ = socket_.local_endpoint(ec);

    running_ = true;
    doReceive();

    IMS_LOG_INFO("UDP transport started on {}:{}",
                 local_ep_.address().to_string(), local_ep_.port());
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

            IMS_LOG_DEBUG("SIP RX/UDP from {}:{} ({} bytes)",
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

struct TcpTransport::Connection : public std::enable_shared_from_this<TcpTransport::Connection> {
    Connection(boost::asio::ip::tcp::socket socket_in,
               TcpTransport& owner_in,
               Endpoint remote_in)
        : socket(std::move(socket_in))
        , owner(owner_in)
        , remote(std::move(remote_in)) {}

    void start() {
        doRead();
    }

    auto send(const std::string& payload) -> VoidResult {
        std::lock_guard lock(send_mutex);

        boost::system::error_code ec;
        boost::asio::write(socket, boost::asio::buffer(payload), ec);
        if (ec) {
            return std::unexpected(ErrorInfo{
                ErrorCode::kSipTransportError, "TCP send failed", ec.message()});
        }
        return {};
    }

    void close() {
        boost::system::error_code ec;
        socket.shutdown(boost::asio::ip::tcp::socket::shutdown_both, ec);
        socket.close(ec);
    }

    void doRead() {
        auto self = shared_from_this();
        socket.async_read_some(
            boost::asio::buffer(recv_buffer),
            [self](boost::system::error_code ec, std::size_t bytes_received) {
                if (ec) {
                    if (ec != boost::asio::error::operation_aborted
                        && ec != boost::asio::error::eof)
                    {
                        IMS_LOG_WARN("TCP receive error from {}:{}: {}",
                                     self->remote.address, self->remote.port, ec.message());
                    }
                    self->owner.unregisterConnection(self->remote);
                    self->close();
                    return;
                }

                self->read_buffer.append(self->recv_buffer.data(), bytes_received);

                while (true) {
                    auto raw = tryExtractSipMessage(self->read_buffer);
                    if (!raw) break;
                    self->owner.handleIncomingMessage(*raw, self->remote);
                }

                self->doRead();
            });
    }

    boost::asio::ip::tcp::socket socket;
    TcpTransport& owner;
    Endpoint remote;
    std::array<char, 8192> recv_buffer{};
    std::string read_buffer;
    std::mutex send_mutex;
};

TcpTransport::TcpTransport(boost::asio::io_context& io, const std::string& bind_addr, Port port)
    : io_(io)
    , acceptor_(io)
    , local_ep_(boost::asio::ip::make_address(bind_addr), port) {}

TcpTransport::~TcpTransport() {
    stop();
}

auto TcpTransport::send(const SipMessage& msg, const Endpoint& dest) -> VoidResult {
    auto data = msg.toString();
    if (!data) {
        return std::unexpected(ErrorInfo(
            ErrorCode::kSipTransportError, "Failed to serialize message", data.error().message));
    }

    auto conn_result = getOrCreateConnection(dest);
    if (!conn_result) {
        return std::unexpected(conn_result.error());
    }

    auto send_result = (*conn_result)->send(*data);
    if (!send_result) {
        unregisterConnection(dest);
        return send_result;
    }

    IMS_LOG_DEBUG("SIP TX/TCP to {}:{} ({} bytes)", dest.address, dest.port, data->size());
    return {};
}

void TcpTransport::setMessageCallback(MessageCallback cb) {
    on_message_ = std::move(cb);
}

auto TcpTransport::start() -> VoidResult {
    boost::system::error_code ec;

    acceptor_.open(local_ep_.protocol(), ec);
    if (ec) {
        return std::unexpected(ErrorInfo{
            ErrorCode::kSipTransportError, "Failed to open TCP acceptor", ec.message()});
    }

    acceptor_.set_option(boost::asio::socket_base::reuse_address(true), ec);
    if (ec) {
        return std::unexpected(ErrorInfo{
            ErrorCode::kSipTransportError, "Failed to set TCP reuse_address", ec.message()});
    }

    acceptor_.bind(local_ep_, ec);
    if (ec) {
        return std::unexpected(ErrorInfo{
            ErrorCode::kSipTransportError, "Failed to bind TCP acceptor", ec.message()});
    }

    acceptor_.listen(boost::asio::socket_base::max_listen_connections, ec);
    if (ec) {
        return std::unexpected(ErrorInfo{
            ErrorCode::kSipTransportError, "Failed to listen on TCP acceptor", ec.message()});
    }

    local_ep_ = acceptor_.local_endpoint(ec);
    running_ = true;
    doAccept();

    IMS_LOG_INFO("TCP transport started on {}:{}",
                 local_ep_.address().to_string(), local_ep_.port());
    return {};
}

void TcpTransport::stop() {
    running_ = false;
    boost::system::error_code ec;
    acceptor_.cancel(ec);
    acceptor_.close(ec);

    std::vector<std::shared_ptr<Connection>> active;
    {
        std::lock_guard lock(connections_mutex_);
        for (auto it = connections_.begin(); it != connections_.end(); ++it) {
            if (auto conn = it->second.lock()) {
                active.push_back(std::move(conn));
            }
        }
        connections_.clear();
    }

    for (auto& conn : active) {
        conn->close();
    }

    IMS_LOG_INFO("TCP transport stopped");
}

auto TcpTransport::localEndpoint() const -> Endpoint {
    return Endpoint{
        .address = local_ep_.address().to_string(),
        .port = local_ep_.port(),
        .transport = "tcp"
    };
}

void TcpTransport::doAccept() {
    if (!running_) return;

    acceptor_.async_accept([this](boost::system::error_code ec, boost::asio::ip::tcp::socket socket) {
        if (!ec) {
            boost::system::error_code ep_ec;
            auto remote_ep = socket.remote_endpoint(ep_ec);
            if (!ep_ec) {
                Endpoint remote{
                    .address = remote_ep.address().to_string(),
                    .port = static_cast<Port>(remote_ep.port()),
                    .transport = "tcp"
                };

                auto conn = std::make_shared<Connection>(std::move(socket), *this, remote);
                {
                    std::lock_guard lock(connections_mutex_);
                    connections_[endpointKey(remote)] = conn;
                }

                IMS_LOG_DEBUG("TCP connection accepted from {}:{}",
                              remote.address, remote.port);
                conn->start();
            }
        } else if (ec != boost::asio::error::operation_aborted) {
            IMS_LOG_WARN("TCP accept error: {}", ec.message());
        }

        if (running_) {
            doAccept();
        }
    });
}

void TcpTransport::handleIncomingMessage(const std::string& raw, const Endpoint& src) {
    auto msg_result = SipMessage::parse(raw);
    if (!msg_result) {
        IMS_LOG_WARN("Failed to parse SIP/TCP message from {}:{}: {}",
                     src.address, src.port, msg_result.error().message);
        return;
    }

    IMS_LOG_DEBUG("SIP RX/TCP from {}:{} ({} bytes)",
                  src.address, src.port, raw.size());

    if (on_message_) {
        on_message_(std::move(*msg_result), src);
    }
}

auto TcpTransport::getOrCreateConnection(const Endpoint& dest) -> Result<std::shared_ptr<Connection>> {
    auto key = endpointKey(dest);
    {
        std::lock_guard lock(connections_mutex_);
        auto it = connections_.find(key);
        if (it != connections_.end()) {
            if (auto existing = it->second.lock()) {
                return existing;
            }
            connections_.erase(it);
        }
    }

    boost::system::error_code ec;
    boost::asio::ip::tcp::socket socket(io_);
    boost::asio::ip::tcp::endpoint remote_ep(boost::asio::ip::make_address(dest.address, ec), dest.port);
    if (ec) {
        return std::unexpected(ErrorInfo{
            ErrorCode::kSipTransportError, "Invalid TCP destination address", ec.message()});
    }

    socket.connect(remote_ep, ec);
    if (ec) {
        return std::unexpected(ErrorInfo{
            ErrorCode::kSipTransportError, "TCP connect failed", ec.message()});
    }

    Endpoint remote{
        .address = remote_ep.address().to_string(),
        .port = static_cast<Port>(remote_ep.port()),
        .transport = "tcp"
    };
    auto conn = std::make_shared<Connection>(std::move(socket), *this, remote);
    {
        std::lock_guard lock(connections_mutex_);
        connections_[key] = conn;
    }

    conn->start();
    return conn;
}

void TcpTransport::unregisterConnection(const Endpoint& endpoint) {
    std::lock_guard lock(connections_mutex_);
    connections_.erase(endpointKey(endpoint));
}

auto TcpTransport::endpointKey(const Endpoint& endpoint) -> std::string {
    return endpoint.address + ":" + std::to_string(endpoint.port);
}

DualTransport::DualTransport(boost::asio::io_context& io, const std::string& bind_addr, Port port)
    : udp_(std::make_shared<UdpTransport>(io, bind_addr, port))
    , tcp_(std::make_shared<TcpTransport>(io, bind_addr, port)) {}

DualTransport::~DualTransport() {
    stop();
}

auto DualTransport::send(const SipMessage& msg, const Endpoint& dest) -> VoidResult {
    auto transport = toLower(dest.transport);
    if (transport == "tcp") {
        return tcp_->send(msg, dest);
    }
    return udp_->send(msg, Endpoint{
        .address = dest.address,
        .port = dest.port,
        .transport = "udp"
    });
}

void DualTransport::setMessageCallback(MessageCallback cb) {
    on_message_ = std::move(cb);

    udp_->setMessageCallback([this](SipMessage msg, Endpoint src) {
        if (on_message_) on_message_(std::move(msg), std::move(src));
    });
    tcp_->setMessageCallback([this](SipMessage msg, Endpoint src) {
        if (on_message_) on_message_(std::move(msg), std::move(src));
    });
}

auto DualTransport::start() -> VoidResult {
    auto udp_result = udp_->start();
    if (!udp_result) {
        return udp_result;
    }

    auto tcp_result = tcp_->start();
    if (!tcp_result) {
        udp_->stop();
        return tcp_result;
    }
    return {};
}

void DualTransport::stop() {
    tcp_->stop();
    udp_->stop();
}

auto DualTransport::localEndpoint() const -> Endpoint {
    return udp_->localEndpoint();
}

} // namespace ims::sip
