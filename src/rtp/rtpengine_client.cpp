#include "rtpengine_client_impl.hpp"
#include "common/logger.hpp"

#include <format>
#include <array>
#include <chrono>
#include <string_view>
#include <thread>

namespace {

constexpr auto kReceiveTimeout = std::chrono::seconds(2);
constexpr auto kReceivePollInterval = std::chrono::milliseconds(10);

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
    boost::system::error_code ec;
    socket_.close(ec);
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
    auto cookie = generateCookie();
    auto encoded = bencode_encode(BencodeValue{cmd});
    auto payload = cookie + " " + encoded;

    boost::system::error_code ec;
    socket_.send_to(boost::asio::buffer(payload), rtpengine_ep_, 0, ec);
    if (ec) {
        return std::unexpected(ErrorInfo{
            ErrorCode::kMediaRtpengineError,
            "Failed to send to rtpengine",
            ec.message()
        });
    }

    // Receive response with bounded wait.
    std::array<char, 65535> recv_buf;
    boost::asio::ip::udp::endpoint sender_ep;
    socket_.non_blocking(true, ec);
    if (ec) {
        return std::unexpected(ErrorInfo{
            ErrorCode::kMediaRtpengineError,
            "Failed to set non-blocking mode",
            ec.message()
        });
    }

    const auto deadline = std::chrono::steady_clock::now() + kReceiveTimeout;
    size_t bytes = 0;
    while (true) {
        bytes = socket_.receive_from(boost::asio::buffer(recv_buf), sender_ep, 0, ec);
        if (!ec) {
            break;
        }
        if (ec != boost::asio::error::would_block && ec != boost::asio::error::try_again) {
            boost::system::error_code restore_ec;
            socket_.non_blocking(false, restore_ec);
            return std::unexpected(ErrorInfo{
                ErrorCode::kMediaRtpengineError,
                "Failed to receive from rtpengine",
                ec.message()
            });
        }
        if (std::chrono::steady_clock::now() >= deadline) {
            boost::system::error_code restore_ec;
            socket_.non_blocking(false, restore_ec);
            return std::unexpected(ErrorInfo{
                ErrorCode::kMediaRtpengineError,
                "Timed out waiting for rtpengine response"
            });
        }
        std::this_thread::sleep_for(kReceivePollInterval);
    }
    boost::system::error_code restore_ec;
    socket_.non_blocking(false, restore_ec);

    if (sender_ep.address() != rtpengine_ep_.address() || sender_ep.port() != rtpengine_ep_.port()) {
        return std::unexpected(ErrorInfo{
            ErrorCode::kMediaRtpengineError,
            "Received rtpengine response from unexpected endpoint",
            std::format("{}:{}", sender_ep.address().to_string(), sender_ep.port())
        });
    }

    std::string response(recv_buf.data(), bytes);

    // Response format: "<cookie> <bencode>"
    auto space_pos = response.find(' ');
    if (space_pos == std::string::npos) {
        return std::unexpected(ErrorInfo{
            ErrorCode::kMediaRtpengineError, "Invalid rtpengine response format"});
    }

    auto resp_cookie = response.substr(0, space_pos);
    if (resp_cookie != cookie) {
        return std::unexpected(ErrorInfo{
            ErrorCode::kMediaRtpengineError, "Cookie mismatch in rtpengine response"});
    }

    auto bencode_data = response.substr(space_pos + 1);
    try {
        auto decoded = bencode_decode(bencode_data);
        auto* dict = std::get_if<BencodeDict>(&decoded);
        if (!dict) {
            return std::unexpected(ErrorInfo{
                ErrorCode::kMediaRtpengineError, "Expected dict in rtpengine response"});
        }
        return *dict;
    } catch (const std::exception& e) {
        return std::unexpected(ErrorInfo{
            ErrorCode::kMediaRtpengineError, "Failed to decode rtpengine response", e.what()});
    }
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

} // namespace ims::media
