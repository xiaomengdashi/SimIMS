#pragma once

#include "rtp/rtpengine_client.hpp"
#include "bencode.hpp"

#include <boost/asio.hpp>
#include <atomic>
#include <string>

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
    auto sendCommand(const BencodeDict& cmd) -> Result<BencodeDict>;
    auto buildBaseCommand(const std::string& command, const MediaSession& session) -> BencodeDict;
    auto generateCookie() -> std::string;

    boost::asio::io_context& io_;
    boost::asio::ip::udp::socket socket_;
    boost::asio::ip::udp::endpoint rtpengine_ep_;
    std::atomic<uint64_t> cookie_counter_{0};
};

} // namespace ims::media
