#pragma once

#include "common/types.hpp"
#include "rtp/types.hpp"

namespace ims::media {

/// Abstract rtpengine client interface
///
/// Controls rtpengine media proxy via the NG (bencode over UDP) protocol.
/// P-CSCF uses this to anchor media through rtpengine so UE-to-UE RTP
/// passes through a known relay point.
struct IRtpengineClient {
    virtual ~IRtpengineClient() = default;

    /// Send offer SDP to rtpengine (called on INVITE/re-INVITE from caller)
    /// Returns rewritten SDP with rtpengine's relay addresses
    virtual auto offer(const MediaSession& session,
                       const std::string& sdp,
                       const RtpengineFlags& flags) -> Result<RtpengineResult> = 0;

    /// Send answer SDP to rtpengine (called on 183/200 from callee)
    /// Returns rewritten SDP with rtpengine's relay addresses
    virtual auto answer(const MediaSession& session,
                        const std::string& sdp,
                        const RtpengineFlags& flags) -> Result<RtpengineResult> = 0;

    /// Delete media session (called on BYE or session cleanup)
    virtual auto deleteSession(const MediaSession& session) -> VoidResult = 0;

    /// Query statistics for a media session
    virtual auto query(const MediaSession& session) -> Result<std::string> = 0;

    /// Ping rtpengine to check connectivity
    virtual auto ping() -> VoidResult = 0;
};

} // namespace ims::media
