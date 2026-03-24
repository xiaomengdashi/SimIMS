#include "rtpengine_client_impl.hpp"
#include "common/logger.hpp"

#include <format>
#include <array>

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
    auto result_it = dict.find("result");
    if (result_it == dict.end() ||
        std::get<std::string>(result_it->second) != "ok") {
        std::string err_msg = "rtpengine offer failed";
        if (auto it = dict.find("error-reason"); it != dict.end()) {
            err_msg = std::get<std::string>(it->second);
        }
        return std::unexpected(ErrorInfo{ErrorCode::kMediaRtpengineError, err_msg});
    }

    RtpengineResult result;
    if (auto it = dict.find("sdp"); it != dict.end()) {
        result.sdp = std::get<std::string>(it->second);
    }
    if (auto it = dict.find("tag"); it != dict.end()) {
        result.tag = std::get<std::string>(it->second);
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
    auto result_it = dict.find("result");
    if (result_it == dict.end() ||
        std::get<std::string>(result_it->second) != "ok") {
        return std::unexpected(ErrorInfo{ErrorCode::kMediaRtpengineError, "rtpengine answer failed"});
    }

    RtpengineResult result;
    if (auto it = dict.find("sdp"); it != dict.end()) {
        result.sdp = std::get<std::string>(it->second);
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
    if (auto it = dict.find("result"); it != dict.end()) {
        if (std::get<std::string>(it->second) == "pong") {
            return {};
        }
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

    // Receive response (synchronous with timeout)
    std::array<char, 65535> recv_buf;
    boost::asio::ip::udp::endpoint sender_ep;

    // Set up a deadline for receive
    socket_.non_blocking(true, ec);

    // Simple blocking receive for now
    socket_.non_blocking(false, ec);
    auto bytes = socket_.receive_from(boost::asio::buffer(recv_buf), sender_ep, 0, ec);
    if (ec) {
        return std::unexpected(ErrorInfo{
            ErrorCode::kMediaRtpengineError,
            "Failed to receive from rtpengine",
            ec.message()
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
