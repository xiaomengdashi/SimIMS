#include "transport.hpp"
#include "message.hpp"
#include "common/logger.hpp"

#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ip/udp.hpp>
#include <boost/asio/socket_base.hpp>
#include <boost/asio/write.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <condition_variable>
#include <mutex>
#include <optional>
#include <set>
#include <sstream>
#include <unordered_map>
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

struct UdpTransport::State {
    State(boost::asio::io_context& io_in, const std::string& bind_addr, Port port)
        : io(io_in)
        , socket(io_in)
        , local_ep(boost::asio::ip::make_address(bind_addr), port) {}

    boost::asio::io_context& io;
    boost::asio::ip::udp::socket socket;
    boost::asio::ip::udp::endpoint local_ep;
    boost::asio::ip::udp::endpoint remote_ep;
    std::array<char, 65536> recv_buffer{};
    MessageCallback on_message;
    std::mutex callback_mutex;
    std::mutex socket_mutex;
    std::atomic<bool> running{false};
};

UdpTransport::UdpTransport(boost::asio::io_context& io, const std::string& bind_addr, Port port)
    : state_(std::make_shared<State>(io, bind_addr, port)) {}

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

    {
        std::lock_guard lock(state_->socket_mutex);
        if (!state_->running || !state_->socket.is_open()) {
            return std::unexpected(ErrorInfo{
                ErrorCode::kSipTransportError, "UDP transport is stopped"});
        }
        state_->socket.send_to(boost::asio::buffer(*data), ep, 0, ec);
    }
    if (ec) {
        return std::unexpected(ErrorInfo{
            ErrorCode::kSipTransportError, "UDP send failed", ec.message()});
    }

    IMS_LOG_DEBUG("SIP TX/UDP to {}:{} ({} bytes)", dest.address, dest.port, data->size());
    return {};
}

void UdpTransport::setMessageCallback(MessageCallback cb) {
    std::lock_guard lock(state_->callback_mutex);
    state_->on_message = std::move(cb);
}

auto UdpTransport::start() -> VoidResult {
    boost::system::error_code ec;

    {
        std::lock_guard lock(state_->socket_mutex);
        state_->socket.open(state_->local_ep.protocol(), ec);
        if (ec) {
            return std::unexpected(ErrorInfo{
                ErrorCode::kSipTransportError, "Failed to open UDP socket", ec.message()});
        }

        state_->socket.set_option(boost::asio::socket_base::reuse_address(true), ec);
        state_->socket.bind(state_->local_ep, ec);
        if (ec) {
            return std::unexpected(ErrorInfo{
                ErrorCode::kSipTransportError, "Failed to bind UDP socket", ec.message()});
        }

        state_->local_ep = state_->socket.local_endpoint(ec);
        state_->running = true;
    }
    doReceive(state_);

    IMS_LOG_INFO("UDP transport started on {}:{}",
                 state_->local_ep.address().to_string(), state_->local_ep.port());
    return {};
}

void UdpTransport::stop() {
    auto state = state_;
    bool expected = true;
    if (!state->running.compare_exchange_strong(expected, false)) {
        return;
    }

    boost::system::error_code ec;
    std::lock_guard lock(state->socket_mutex);
    if (state->socket.is_open()) {
        state->socket.cancel(ec);
        state->socket.close(ec);
    }
    IMS_LOG_INFO("UDP transport stopped");
}

auto UdpTransport::localEndpoint() const -> Endpoint {
    std::lock_guard lock(state_->socket_mutex);
    return Endpoint{
        .address = state_->local_ep.address().to_string(),
        .port = state_->local_ep.port(),
        .transport = "udp"
    };
}

void UdpTransport::doReceive() {
    doReceive(state_);
}

void UdpTransport::doReceive(std::shared_ptr<State> state) {
    if (!state->running) return;

    std::lock_guard lock(state->socket_mutex);
    if (!state->running || !state->socket.is_open()) {
        return;
    }
    state->socket.async_receive_from(
        boost::asio::buffer(state->recv_buffer), state->remote_ep,
        [state](boost::system::error_code ec, std::size_t bytes_received) {
            if (ec) {
                if (ec != boost::asio::error::operation_aborted) {
                    IMS_LOG_WARN("UDP receive error: {}", ec.message());
                }
                if (state->running) doReceive(state);
                return;
            }

            IMS_LOG_DEBUG("SIP RX/UDP from {}:{} ({} bytes)",
                state->remote_ep.address().to_string(), state->remote_ep.port(), bytes_received);

            std::string raw(state->recv_buffer.data(), bytes_received);
            auto msg_result = SipMessage::parse(raw);
            if (!msg_result) {
                IMS_LOG_WARN("Failed to parse SIP message: {}", msg_result.error().message);
                doReceive(state);
                return;
            }

            MessageCallback on_message;
            {
                std::lock_guard lock(state->callback_mutex);
                on_message = state->on_message;
            }
            if (on_message) {
                Endpoint src{
                    .address = state->remote_ep.address().to_string(),
                    .port = static_cast<Port>(state->remote_ep.port()),
                    .transport = "udp"
                };
                on_message(std::move(*msg_result), std::move(src));
            }

            doReceive(state);
        });
}

struct TcpTransport::State {
    State(boost::asio::io_context& io_in, const std::string& bind_addr, Port port)
        : io(io_in)
        , acceptor(io_in)
        , local_ep(boost::asio::ip::make_address(bind_addr), port) {}

    boost::asio::io_context& io;
    boost::asio::ip::tcp::acceptor acceptor;
    boost::asio::ip::tcp::endpoint local_ep;
    MessageCallback on_message;
    std::mutex callback_mutex;
    std::mutex acceptor_mutex;
    std::mutex connections_mutex;
    std::condition_variable connections_cv;
    std::unordered_map<std::string, std::shared_ptr<Connection>> connections;
    std::set<std::string> connecting;
    std::atomic<bool> running{false};
    std::atomic<bool> stopped{false};
};

struct TcpTransport::Connection : public std::enable_shared_from_this<TcpTransport::Connection> {
    Connection(boost::asio::ip::tcp::socket socket_in,
               std::shared_ptr<State> state_in,
               Endpoint remote_in)
        : socket(std::move(socket_in))
        , state(std::move(state_in))
        , remote(std::move(remote_in)) {}

    void start() {
        doRead();
    }

    auto send(const std::string& payload) -> VoidResult {
        boost::system::error_code ec;
        std::lock_guard lock(socket_mutex);
        if (!open || state->stopped || !socket.is_open()) {
            return std::unexpected(ErrorInfo{
                ErrorCode::kSipTransportError, "TCP connection is closed"});
        }
        boost::asio::write(socket, boost::asio::buffer(payload), ec);
        if (ec) {
            return std::unexpected(ErrorInfo{
                ErrorCode::kSipTransportError, "TCP send failed", ec.message()});
        }
        return {};
    }

    void close() {
        bool expected = true;
        if (!open.compare_exchange_strong(expected, false)) {
            return;
        }

        boost::system::error_code ec;
        std::lock_guard lock(socket_mutex);
        if (socket.is_open()) {
            socket.shutdown(boost::asio::ip::tcp::socket::shutdown_both, ec);
            socket.close(ec);
        }
    }

    void doRead() {
        if (!open || !state->running) {
            return;
        }

        auto self = shared_from_this();
        {
            std::lock_guard lock(socket_mutex);
            if (!open || state->stopped || !socket.is_open()) {
                return;
            }
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
                        unregisterConnection(self->state, self->remote, self.get());
                        self->close();
                        return;
                    }

                    self->read_buffer.append(self->recv_buffer.data(), bytes_received);

                    while (true) {
                        auto raw = tryExtractSipMessage(self->read_buffer);
                        if (!raw) break;
                        handleIncomingMessage(self->state, *raw, self->remote);
                    }

                    self->doRead();
                });
        }
    }

    boost::asio::ip::tcp::socket socket;
    std::shared_ptr<State> state;
    Endpoint remote;
    std::array<char, 8192> recv_buffer{};
    std::string read_buffer;
    std::mutex socket_mutex;
    std::atomic<bool> open{true};
};

TcpTransport::TcpTransport(boost::asio::io_context& io, const std::string& bind_addr, Port port)
    : state_(std::make_shared<State>(io, bind_addr, port)) {}

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
        unregisterConnection(state_, (*conn_result)->remote, conn_result->get());
        return send_result;
    }

    IMS_LOG_DEBUG("SIP TX/TCP to {}:{} ({} bytes)", dest.address, dest.port, data->size());
    return {};
}

void TcpTransport::setMessageCallback(MessageCallback cb) {
    std::lock_guard lock(state_->callback_mutex);
    state_->on_message = std::move(cb);
}

auto TcpTransport::start() -> VoidResult {
    boost::system::error_code ec;

    {
        std::lock_guard lock(state_->acceptor_mutex);
        state_->acceptor.open(state_->local_ep.protocol(), ec);
        if (ec) {
            return std::unexpected(ErrorInfo{
                ErrorCode::kSipTransportError, "Failed to open TCP acceptor", ec.message()});
        }

        state_->acceptor.set_option(boost::asio::socket_base::reuse_address(true), ec);
        if (ec) {
            return std::unexpected(ErrorInfo{
                ErrorCode::kSipTransportError, "Failed to set TCP reuse_address", ec.message()});
        }

        state_->acceptor.bind(state_->local_ep, ec);
        if (ec) {
            return std::unexpected(ErrorInfo{
                ErrorCode::kSipTransportError, "Failed to bind TCP acceptor", ec.message()});
        }

        state_->acceptor.listen(boost::asio::socket_base::max_listen_connections, ec);
        if (ec) {
            return std::unexpected(ErrorInfo{
                ErrorCode::kSipTransportError, "Failed to listen on TCP acceptor", ec.message()});
        }

        state_->local_ep = state_->acceptor.local_endpoint(ec);
        state_->stopped = false;
        state_->running = true;
    }
    doAccept(state_);

    IMS_LOG_INFO("TCP transport started on {}:{}",
                 state_->local_ep.address().to_string(), state_->local_ep.port());
    return {};
}

void TcpTransport::stop() {
    auto state = state_;
    bool expected = false;
    if (!state->stopped.compare_exchange_strong(expected, true)) {
        return;
    }
    state->running = false;

    boost::system::error_code ec;
    {
        std::lock_guard lock(state->acceptor_mutex);
        if (state->acceptor.is_open()) {
            state->acceptor.cancel(ec);
            state->acceptor.close(ec);
        }
    }

    std::vector<std::shared_ptr<Connection>> active;
    {
        std::lock_guard lock(state->connections_mutex);
        state->connecting.clear();
        for (auto& [_, conn] : state->connections) {
            active.push_back(conn);
        }
        state->connections.clear();
    }
    state->connections_cv.notify_all();

    for (auto& conn : active) {
        conn->close();
    }

    IMS_LOG_INFO("TCP transport stopped");
}

auto TcpTransport::localEndpoint() const -> Endpoint {
    std::lock_guard lock(state_->acceptor_mutex);
    return Endpoint{
        .address = state_->local_ep.address().to_string(),
        .port = state_->local_ep.port(),
        .transport = "tcp"
    };
}

void TcpTransport::doAccept() {
    doAccept(state_);
}

void TcpTransport::doAccept(std::shared_ptr<State> state) {
    if (!state->running) return;

    std::lock_guard lock(state->acceptor_mutex);
    if (!state->running || !state->acceptor.is_open()) {
        return;
    }

    state->acceptor.async_accept([state](boost::system::error_code ec, boost::asio::ip::tcp::socket socket) {
        if (!ec) {
            boost::system::error_code ep_ec;
            auto remote_ep = socket.remote_endpoint(ep_ec);
            if (!ep_ec) {
                Endpoint remote{
                    .address = remote_ep.address().to_string(),
                    .port = static_cast<Port>(remote_ep.port()),
                    .transport = "tcp"
                };

                auto conn = std::make_shared<Connection>(std::move(socket), state, remote);
                {
                    std::lock_guard lock(state->connections_mutex);
                    state->connections[endpointKey(remote)] = conn;
                }

                IMS_LOG_DEBUG("TCP connection accepted from {}:{}",
                              remote.address, remote.port);
                conn->start();
            }
        } else if (ec != boost::asio::error::operation_aborted) {
            IMS_LOG_WARN("TCP accept error: {}", ec.message());
        }

        if (state->running) {
            doAccept(state);
        }
    });
}

void TcpTransport::handleIncomingMessage(const std::shared_ptr<State>& state,
                                         const std::string& raw,
                                         const Endpoint& src) {
    auto msg_result = SipMessage::parse(raw);
    if (!msg_result) {
        IMS_LOG_WARN("Failed to parse SIP/TCP message from {}:{}: {}",
                     src.address, src.port, msg_result.error().message);
        return;
    }

    IMS_LOG_DEBUG("SIP RX/TCP from {}:{} ({} bytes)",
                  src.address, src.port, raw.size());

    MessageCallback on_message;
    {
        std::lock_guard lock(state->callback_mutex);
        on_message = state->on_message;
    }
    if (on_message) {
        on_message(std::move(*msg_result), src);
    }
}

auto TcpTransport::getOrCreateConnection(const Endpoint& dest) -> Result<std::shared_ptr<Connection>> {
    return getOrCreateConnection(state_, dest);
}

auto TcpTransport::getOrCreateConnection(const std::shared_ptr<State>& state,
                                         const Endpoint& dest) -> Result<std::shared_ptr<Connection>> {
    if (state->stopped) {
        return std::unexpected(ErrorInfo{
            ErrorCode::kSipTransportError, "TCP transport is stopped"});
    }

    auto key = endpointKey(dest);
    {
        std::unique_lock lock(state->connections_mutex);
        state->connections_cv.wait(lock, [&] {
            return state->stopped || !state->connecting.contains(key);
        });
        if (state->stopped) {
            return std::unexpected(ErrorInfo{
                ErrorCode::kSipTransportError, "TCP transport is stopped"});
        }
        if (auto it = state->connections.find(key); it != state->connections.end()) {
            return it->second;
        }
        state->connecting.insert(key);
    }

    boost::system::error_code ec;
    boost::asio::ip::tcp::socket socket(state->io);
    boost::asio::ip::tcp::endpoint remote_ep(boost::asio::ip::make_address(dest.address, ec), dest.port);
    if (ec) {
        {
            std::lock_guard lock(state->connections_mutex);
            state->connecting.erase(key);
        }
        state->connections_cv.notify_all();
        return std::unexpected(ErrorInfo{
            ErrorCode::kSipTransportError, "Invalid TCP destination address", ec.message()});
    }

    socket.connect(remote_ep, ec);
    if (ec) {
        {
            std::lock_guard lock(state->connections_mutex);
            state->connecting.erase(key);
        }
        state->connections_cv.notify_all();
        return std::unexpected(ErrorInfo{
            ErrorCode::kSipTransportError, "TCP connect failed", ec.message()});
    }

    Endpoint remote{
        .address = remote_ep.address().to_string(),
        .port = static_cast<Port>(remote_ep.port()),
        .transport = "tcp"
    };
    auto conn = std::make_shared<Connection>(std::move(socket), state, remote);
    {
        std::lock_guard lock(state->connections_mutex);
        state->connecting.erase(key);
        if (state->stopped) {
            conn->close();
            state->connections_cv.notify_all();
            return std::unexpected(ErrorInfo{
                ErrorCode::kSipTransportError, "TCP transport is stopped"});
        }
        if (auto it = state->connections.find(key); it != state->connections.end()) {
            conn->close();
            state->connections_cv.notify_all();
            return it->second;
        }
        state->connections[key] = conn;
    }
    state->connections_cv.notify_all();
    conn->start();
    return conn;
}

void TcpTransport::unregisterConnection(const std::shared_ptr<State>& state,
                                        const Endpoint& endpoint,
                                        const Connection* connection) {
    std::lock_guard lock(state->connections_mutex);
    if (auto it = state->connections.find(endpointKey(endpoint));
        it != state->connections.end() && it->second.get() == connection) {
        state->connections.erase(it);
    }
}

auto TcpTransport::endpointKey(const Endpoint& endpoint) -> std::string {
    return endpoint.address + ":" + std::to_string(endpoint.port);
}

struct DualTransport::State {
    State(boost::asio::io_context& io, const std::string& bind_addr, Port port)
        : udp(std::make_shared<UdpTransport>(io, bind_addr, port))
        , tcp(std::make_shared<TcpTransport>(io, bind_addr, port)) {}

    std::shared_ptr<UdpTransport> udp;
    std::shared_ptr<TcpTransport> tcp;
    MessageCallback on_message;
    std::mutex callback_mutex;
};

DualTransport::DualTransport(boost::asio::io_context& io, const std::string& bind_addr, Port port)
    : state_(std::make_shared<State>(io, bind_addr, port)) {}

DualTransport::~DualTransport() {
    stop();
}

auto DualTransport::send(const SipMessage& msg, const Endpoint& dest) -> VoidResult {
    auto transport = toLower(dest.transport);
    if (transport == "tcp") {
        return state_->tcp->send(msg, dest);
    }
    return state_->udp->send(msg, Endpoint{
        .address = dest.address,
        .port = dest.port,
        .transport = "udp"
    });
}

void DualTransport::setMessageCallback(MessageCallback cb) {
    {
        std::lock_guard lock(state_->callback_mutex);
        state_->on_message = std::move(cb);
    }

    auto state = state_;
    state_->udp->setMessageCallback([state](SipMessage msg, Endpoint src) {
        MessageCallback on_message;
        {
            std::lock_guard lock(state->callback_mutex);
            on_message = state->on_message;
        }
        if (on_message) on_message(std::move(msg), std::move(src));
    });
    state_->tcp->setMessageCallback([state](SipMessage msg, Endpoint src) {
        MessageCallback on_message;
        {
            std::lock_guard lock(state->callback_mutex);
            on_message = state->on_message;
        }
        if (on_message) on_message(std::move(msg), std::move(src));
    });
}

auto DualTransport::start() -> VoidResult {
    auto udp_result = state_->udp->start();
    if (!udp_result) {
        return udp_result;
    }

    auto tcp_result = state_->tcp->start();
    if (!tcp_result) {
        state_->udp->stop();
        return tcp_result;
    }
    return {};
}

void DualTransport::stop() {
    state_->tcp->stop();
    state_->udp->stop();
}

auto DualTransport::localEndpoint() const -> Endpoint {
    return state_->udp->localEndpoint();
}

} // namespace ims::sip
