#pragma once

#include "rtp/types.hpp"
#include "common/types.hpp"

#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>

namespace ims::media {

struct MediaSessionKey {
    std::string call_id;
    std::string from_tag;
    std::string to_tag;

    auto operator==(const MediaSessionKey&) const -> bool = default;
};

struct MediaSessionKeyHash {
    auto operator()(const MediaSessionKey& key) const -> std::size_t;
};

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
    auto createSession(const MediaSessionKey& key) -> MediaSession;

    auto getSession(const MediaSessionKey& key) -> std::optional<MediaSessionState>;
    auto getSession(const std::string& call_id) -> std::optional<MediaSessionState>;

    void updateToTag(const MediaSessionKey& key, const std::string& to_tag);
    void updateToTag(const std::string& call_id, const std::string& to_tag);
    void updateCallerSdp(const MediaSessionKey& key, const std::string& sdp);
    void updateCallerSdp(const std::string& call_id, const std::string& sdp);
    void updateCalleeSdp(const MediaSessionKey& key, const std::string& sdp);
    void updateCalleeSdp(const std::string& call_id, const std::string& sdp);
    void setRxSession(const MediaSessionKey& key, const std::string& rx_session_id);
    void setRxSession(const std::string& call_id, const std::string& rx_session_id);
    void setQosActive(const MediaSessionKey& key, bool active);
    void setQosActive(const std::string& call_id, bool active);

    void removeSession(const MediaSessionKey& key);
    void removeSession(const std::string& call_id);
    auto sessionCount() const -> size_t;

private:
    using SessionMap = std::unordered_map<MediaSessionKey, MediaSessionState, MediaSessionKeyHash>;
    using SessionIterator = SessionMap::iterator;
    using ConstSessionIterator = SessionMap::const_iterator;

    auto findUniqueByCallIdLocked(const std::string& call_id) -> std::optional<SessionIterator>;
    auto findUniqueByCallIdLocked(const std::string& call_id) const -> std::optional<ConstSessionIterator>;

    mutable std::mutex mutex_;
    SessionMap sessions_;
};

} // namespace ims::media
