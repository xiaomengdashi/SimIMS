#include "rtpengine_client_impl.hpp"
#include "common/logger.hpp"

#include <array>
#include <chrono>
#include <format>
#include <string_view>
#include <utility>

namespace {

constexpr auto kReceiveTimeout = std::chrono::seconds(2);

auto findStringField(const ims::media::BencodeDict& dict,
                     std::string_view key) -> ims::Result<std::optional<std::string>>
{
    auto it = dict.find(std::string(key));
    if (it == dict.end()) {
        return std::optional<std::string>{};
    }
    if (auto* value = std::get_if<std::string>(&it->second)) {
        return std::optional<std::string>{*value};
    }
    return std::unexpected(ims::ErrorInfo{
        ims::ErrorCode::kMediaRtpengineError,
        std::format("rtpengine response field '{}' has unexpected type", key)
    });
}

} // namespace

namespace ims::media {

RtpengineClientImpl::RtpengineClientImpl(boost::asio::io_context& io,
                                         const std::string& rtpengine_host,
                                         uint16_t rtpengine_port)
    : io_(io)
    , socket_(io, boost::asio::ip::udp::v4())
    , rtpengine_ep_(boost::asio::ip::make_address(rtpengine_host), rtpengine_port)
{
    IMS_LOG_INFO("RtpengineClient connecting to {}:{}", rtpengine_host, rtpengine_port);
}

RtpengineClientImpl::~RtpengineClientImpl() {
    shutting_down_.store(true);

    boost::system::error_code ec;
    socket_.cancel(ec); // NOLINT(bugprone-unused-return-value)
    ec.clear();
    socket_.close(ec); // NOLINT(bugprone-unused-return-value)
    if (ec) {
        IMS_LOG_WARN("Failed to close rtpengine socket during shutdown: {}", ec.message());
    }

    failAllPendingRequests(ErrorInfo{
        ErrorCode::kMediaRtpengineError,
        "Rtpengine client is shutting down"
    });

    if (receiver_thread_.joinable()) {
        receiver_thread_.join();
    }
}

auto RtpengineClientImpl::offer(const MediaSession& session, const std::string& sdp,
                                const RtpengineFlags& flags) -> Result<RtpengineResult>
{
    auto cmd = buildBaseCommand("offer", session);
    cmd["sdp"] = BencodeValue{sdp};

    if (!flags.direction_from.empty()) {
        BencodeList dirs;
        dirs.push_back(BencodeValue{flags.direction_from});
        dirs.push_back(BencodeValue{flags.direction_to});
        cmd["direction"] = BencodeValue{std::move(dirs)};
    }
    if (flags.ice_remove) {
        cmd["ICE"] = BencodeValue{std::string("remove")};
    }
    if (flags.replace.has_value()) {
        BencodeList repl;
        repl.push_back(BencodeValue{*flags.replace});
        cmd["replace"] = BencodeValue{std::move(repl)};
    }

    auto response = sendCommand(cmd);
    if (!response) return std::unexpected(response.error());

    auto& dict = *response;
    auto result_value = findStringField(dict, "result");
    if (!result_value) {
        return std::unexpected(result_value.error());
    }
    if (!result_value->has_value() || **result_value != "ok") {
        std::string err_msg = "rtpengine offer failed";
        auto error_reason = findStringField(dict, "error-reason");
        if (!error_reason) {
            return std::unexpected(error_reason.error());
        }
        if (error_reason->has_value()) {
            err_msg = **error_reason;
        }
        return std::unexpected(ErrorInfo{ErrorCode::kMediaRtpengineError, err_msg});
    }

    RtpengineResult result;
    auto sdp_value = findStringField(dict, "sdp");
    if (!sdp_value) {
        return std::unexpected(sdp_value.error());
    }
    if (sdp_value->has_value()) {
        result.sdp = **sdp_value;
    }
    auto tag_value = findStringField(dict, "tag");
    if (!tag_value) {
        return std::unexpected(tag_value.error());
    }
    if (tag_value->has_value()) {
        result.tag = **tag_value;
    }

    IMS_LOG_DEBUG("rtpengine offer success for call={}", session.call_id);
    return result;
}

auto RtpengineClientImpl::answer(const MediaSession& session, const std::string& sdp,
                                 const RtpengineFlags& flags) -> Result<RtpengineResult>
{
    auto cmd = buildBaseCommand("answer", session);
    cmd["sdp"] = BencodeValue{sdp};

    if (!flags.direction_from.empty()) {
        BencodeList dirs;
        dirs.push_back(BencodeValue{flags.direction_from});
        dirs.push_back(BencodeValue{flags.direction_to});
        cmd["direction"] = BencodeValue{std::move(dirs)};
    }
    if (flags.ice_remove) {
        cmd["ICE"] = BencodeValue{std::string("remove")};
    }

    auto response = sendCommand(cmd);
    if (!response) return std::unexpected(response.error());

    auto& dict = *response;
    auto result_value = findStringField(dict, "result");
    if (!result_value) {
        return std::unexpected(result_value.error());
    }
    if (!result_value->has_value() || **result_value != "ok") {
        return std::unexpected(ErrorInfo{ErrorCode::kMediaRtpengineError, "rtpengine answer failed"});
    }

    RtpengineResult result;
    auto sdp_value = findStringField(dict, "sdp");
    if (!sdp_value) {
        return std::unexpected(sdp_value.error());
    }
    if (sdp_value->has_value()) {
        result.sdp = **sdp_value;
    }

    IMS_LOG_DEBUG("rtpengine answer success for call={}", session.call_id);
    return result;
}

auto RtpengineClientImpl::deleteSession(const MediaSession& session) -> VoidResult {
    auto cmd = buildBaseCommand("delete", session);

    auto response = sendCommand(cmd);
    if (!response) {
        IMS_LOG_WARN("rtpengine delete failed for call={}: {}",
                     session.call_id, response.error().message);
        return std::unexpected(response.error());
    }

    IMS_LOG_DEBUG("rtpengine delete success for call={}", session.call_id);
    return {};
}

auto RtpengineClientImpl::query(const MediaSession& session) -> Result<std::string> {
    auto cmd = buildBaseCommand("query", session);
    auto response = sendCommand(cmd);
    if (!response) return std::unexpected(response.error());

    return bencode_encode(BencodeValue{*response});
}

auto RtpengineClientImpl::ping() -> VoidResult {
    BencodeDict cmd;
    cmd["command"] = BencodeValue{std::string("ping")};

    auto response = sendCommand(cmd);
    if (!response) return std::unexpected(response.error());

    auto& dict = *response;
    auto result_value = findStringField(dict, "result");
    if (!result_value) {
        return std::unexpected(result_value.error());
    }
    if (result_value->has_value() && **result_value == "pong") {
        return {};
    }

    return std::unexpected(ErrorInfo{ErrorCode::kMediaRtpengineError, "Unexpected ping response"});
}

auto RtpengineClientImpl::sendCommand(const BencodeDict& cmd) -> Result<BencodeDict> {
    if (shutting_down_.load()) {
        return std::unexpected(ErrorInfo{
            ErrorCode::kMediaRtpengineError,
            "Rtpengine client is shutting down"
        });
    }

    auto receiver_ready = ensureReceiverRunning();
    if (!receiver_ready) {
        return std::unexpected(receiver_ready.error());
    }

    auto cookie = generateCookie();
    auto pending = std::make_shared<PendingRequest>();
    {
        std::scoped_lock lock(pending_mutex_);
        pending_requests_.emplace(cookie, pending);
    }

    auto encoded = bencode_encode(BencodeValue{cmd});
    auto payload = cookie + " " + encoded;

    {
        std::scoped_lock lock(socket_send_mutex_);
        boost::system::error_code ec;
        socket_.send_to(boost::asio::buffer(payload), rtpengine_ep_, 0, ec);
        if (ec) {
            {
                std::scoped_lock pending_lock(pending_mutex_);
                pending_requests_.erase(cookie);
            }
            return std::unexpected(ErrorInfo{
                ErrorCode::kMediaRtpengineError,
                "Failed to send to rtpengine",
                ec.message()
            });
        }
    }

    std::unique_lock lock(pending->mutex);
    if (!pending->cv.wait_for(lock, kReceiveTimeout, [&pending] {
            return pending->completed;
        })) {
        lock.unlock();
        {
            std::scoped_lock pending_lock(pending_mutex_);
            pending_requests_.erase(cookie);
        }
        return std::unexpected(ErrorInfo{
            ErrorCode::kMediaRtpengineError,
            "Timed out waiting for rtpengine response"
        });
    }

    return pending->result;
}

auto RtpengineClientImpl::buildBaseCommand(const std::string& command,
                                           const MediaSession& session) -> BencodeDict
{
    BencodeDict cmd;
    cmd["command"] = BencodeValue{command};
    cmd["call-id"] = BencodeValue{session.call_id};
    cmd["from-tag"] = BencodeValue{session.from_tag};
    if (!session.to_tag.empty()) {
        cmd["to-tag"] = BencodeValue{session.to_tag};
    }
    return cmd;
}

auto RtpengineClientImpl::generateCookie() -> std::string {
    return std::to_string(++cookie_counter_);
}

auto RtpengineClientImpl::ensureReceiverRunning() -> VoidResult {
    std::scoped_lock lock(receiver_start_mutex_);
    if (receiver_thread_.joinable()) {
        return {};
    }
    if (shutting_down_.load()) {
        return std::unexpected(ErrorInfo{
            ErrorCode::kMediaRtpengineError,
            "Rtpengine client is shutting down"
        });
    }

    try {
        receiver_thread_ = std::jthread([this] {
            receiverLoop();
        });
    } catch (const std::exception& e) {
        return std::unexpected(ErrorInfo{
            ErrorCode::kMediaRtpengineError,
            "Failed to start rtpengine receiver thread",
            e.what()
        });
    }

    return {};
}

void RtpengineClientImpl::receiverLoop() {
    std::array<char, 65535> recv_buf{};

    while (!shutting_down_.load()) {
        boost::asio::ip::udp::endpoint sender_ep;
        boost::system::error_code ec;
        const auto bytes = socket_.receive_from(boost::asio::buffer(recv_buf), sender_ep, 0, ec);
        if (ec) {
            if (shutting_down_.load() ||
                ec == boost::asio::error::operation_aborted ||
                ec == boost::asio::error::bad_descriptor) {
                break;
            }

            IMS_LOG_WARN("Failed to receive from rtpengine: {}", ec.message());
            failAllPendingRequests(ErrorInfo{
                ErrorCode::kMediaRtpengineError,
                "Failed to receive from rtpengine",
                ec.message()
            });
            break;
        }

        if (sender_ep.address() != rtpengine_ep_.address() || sender_ep.port() != rtpengine_ep_.port()) {
            IMS_LOG_WARN("Discarding rtpengine response from unexpected endpoint {}:{}",
                         sender_ep.address().to_string(), sender_ep.port());
            continue;
        }

        std::string response(recv_buf.data(), bytes);
        auto parsed = parseResponsePayload(response);
        if (!parsed) {
            IMS_LOG_WARN("Discarding invalid rtpengine response: {} ({})",
                         parsed.error().message, parsed.error().detail);
            continue;
        }

        auto [cookie, dict] = std::move(*parsed);
        completePendingRequest(cookie, std::move(dict));
    }
}

auto RtpengineClientImpl::parseResponsePayload(const std::string& response)
    -> Result<std::pair<std::string, BencodeDict>>
{
    auto space_pos = response.find(' ');
    if (space_pos == std::string::npos) {
        return std::unexpected(ErrorInfo{
            ErrorCode::kMediaRtpengineError,
            "Invalid rtpengine response format"
        });
    }

    auto cookie = response.substr(0, space_pos);
    auto bencode_data = response.substr(space_pos + 1);

    try {
        auto decoded = bencode_decode(bencode_data);
        auto* dict = std::get_if<BencodeDict>(&decoded);
        if (!dict) {
            return std::unexpected(ErrorInfo{
                ErrorCode::kMediaRtpengineError,
                "Expected dict in rtpengine response"
            });
        }
        return std::pair<std::string, BencodeDict>{std::move(cookie), std::move(*dict)};
    } catch (const std::exception& e) {
        return std::unexpected(ErrorInfo{
            ErrorCode::kMediaRtpengineError,
            "Failed to decode rtpengine response",
            e.what()
        });
    }
}

void RtpengineClientImpl::completePendingRequest(const std::string& cookie, Result<BencodeDict> result) {
    std::shared_ptr<PendingRequest> pending;
    {
        std::scoped_lock lock(pending_mutex_);
        auto it = pending_requests_.find(cookie);
        if (it == pending_requests_.end()) {
            IMS_LOG_WARN("Discarding rtpengine response for unknown or expired cookie {}", cookie);
            return;
        }
        pending = it->second;
        pending_requests_.erase(it);
    }

    {
        std::scoped_lock lock(pending->mutex);
        pending->result = std::move(result);
        pending->completed = true;
    }
    pending->cv.notify_one();
}

void RtpengineClientImpl::failAllPendingRequests(const ErrorInfo& error) {
    std::unordered_map<std::string, std::shared_ptr<PendingRequest>> pending_requests;
    {
        std::scoped_lock lock(pending_mutex_);
        pending_requests.swap(pending_requests_);
    }

    for (auto& [cookie, pending] : pending_requests) {
        {
            std::scoped_lock lock(pending->mutex);
            pending->result = std::unexpected(error);
            pending->completed = true;
        }
        pending->cv.notify_one();
    }
}

} // namespace ims::media
