#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace ims::media {

/// Parsed SDP media line info
struct MediaStream {
    std::string media_type;      // "audio", "video"
    uint16_t port = 0;
    std::string protocol;        // "RTP/AVP", "RTP/SAVP"
    std::vector<uint32_t> payload_types;
    std::string connection_addr; // c= line IP
    std::vector<std::string> attributes;  // a= lines
};

/// Simplified SDP representation
struct SdpInfo {
    std::string origin;           // o= line
    std::string session_name;     // s= line
    std::string connection_addr;  // Session-level c= line
    std::vector<MediaStream> media_streams;
    std::string raw;              // Original SDP text

    /// Get first audio stream, if any
    auto audio_stream() const -> const MediaStream* {
        for (const auto& s : media_streams) {
            if (s.media_type == "audio") return &s;
        }
        return nullptr;
    }
};

/// rtpengine call/session identifiers
struct MediaSession {
    std::string call_id;
    std::string from_tag;
    std::string to_tag;
};

/// rtpengine operation flags
struct RtpengineFlags {
    std::string direction_from;          // e.g., "internal"
    std::string direction_to;            // e.g., "external"
    std::string transport_protocol;      // "RTP/AVP"
    bool ice_remove = false;
    bool rtcp_mux_demux = false;
    std::optional<std::string> replace;  // "origin session-connection"
    std::optional<std::string> record_call;
};

/// Result of rtpengine offer/answer
struct RtpengineResult {
    std::string sdp;             // Rewritten SDP
    std::string tag;             // Media tag for tracking
};

} // namespace ims::media
