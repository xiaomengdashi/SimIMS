#pragma once

#include "rtp/rtpengine_client.hpp"
#include "bencode.hpp"

#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <boost/asio.hpp>

namespace ims::media {

/// rtpengine client using NG protocol (bencode over UDP)
class RtpengineClientImpl : public IRtpengineClient {
public:
    RtpengineClientImpl(boost::asio::io_context& io,
                        const std::string& rtpengine_host,
                        uint16_t rtpengine_port);
    ~RtpengineClientImpl() override;

    auto offer(const MediaSession& session, const std::string& sdp,
               const RtpengineFlags& flags) -> Result<RtpengineResult> override;
    auto answer(const MediaSession& session, const std::string& sdp,
                const RtpengineFlags& flags) -> Result<RtpengineResult> override;
    auto deleteSession(const MediaSession& session) -> VoidResult override;
    auto query(const MediaSession& session) -> Result<std::string> override;
    auto ping() -> VoidResult override;

private:
    struct PendingRequest {
        std::mutex mutex;
        std::condition_variable cv;
        bool completed = false;
        Result<BencodeDict> result = std::unexpected(
            ErrorInfo{ErrorCode::kInternalError, "rtpengine request not completed"});
    };

    auto sendCommand(const BencodeDict& cmd) -> Result<BencodeDict>;
    auto buildBaseCommand(const std::string& command, const MediaSession& session) -> BencodeDict;
    auto generateCookie() -> std::string;
    auto ensureReceiverRunning() -> VoidResult;
    void receiverLoop();
    auto parseResponsePayload(const std::string& response) -> Result<std::pair<std::string, BencodeDict>>;
    void completePendingRequest(const std::string& cookie, Result<BencodeDict> result);
    void failAllPendingRequests(const ErrorInfo& error);

    boost::asio::io_context& io_;
    boost::asio::ip::udp::socket socket_;
    boost::asio::ip::udp::endpoint rtpengine_ep_;
    std::atomic<uint64_t> cookie_counter_{0};
    std::mutex pending_mutex_;
    std::unordered_map<std::string, std::shared_ptr<PendingRequest>> pending_requests_;
    std::mutex socket_send_mutex_;
    std::mutex receiver_start_mutex_;
    std::jthread receiver_thread_;
    std::atomic<bool> shutting_down_{false};
};

} // namespace ims::media
