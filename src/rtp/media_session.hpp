#pragma once

#include "rtp/types.hpp"
#include "common/types.hpp"

#include <mutex>
#include <string>
#include <unordered_map>
#include <optional>

namespace ims::media {

/// Track active media sessions through rtpengine
struct MediaSessionState {
    MediaSession session;
    std::string rx_session_id;   // Diameter Rx session for QoS
    std::string caller_sdp;
    std::string callee_sdp;
    TimePoint created;
    bool qos_active = false;
};

class MediaSessionManager {
public:
    auto createSession(const std::string& call_id,
                       const std::string& from_tag) -> MediaSession;

    auto getSession(const std::string& call_id) -> std::optional<MediaSessionState>;

    void updateToTag(const std::string& call_id, const std::string& to_tag);
    void updateCallerSdp(const std::string& call_id, const std::string& sdp);
    void updateCalleeSdp(const std::string& call_id, const std::string& sdp);
    void setRxSession(const std::string& call_id, const std::string& rx_session_id);
    void setQosActive(const std::string& call_id, bool active);

    void removeSession(const std::string& call_id);
    auto sessionCount() const -> size_t;

private:
    mutable std::mutex mutex_;
    std::unordered_map<std::string, MediaSessionState> sessions_;
};

} // namespace ims::media
