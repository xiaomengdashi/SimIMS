#include "media_session.hpp"
#include "common/logger.hpp"

namespace ims::media {

auto MediaSessionManager::createSession(const std::string& call_id,
                                         const std::string& from_tag) -> MediaSession
{
    std::lock_guard lock(mutex_);

    MediaSession session{
        .call_id = call_id,
        .from_tag = from_tag,
    };

    MediaSessionState state{
        .session = session,
        .created = std::chrono::steady_clock::now(),
    };

    sessions_[call_id] = std::move(state);
    IMS_LOG_DEBUG("Created media session for call={}", call_id);
    return session;
}

auto MediaSessionManager::getSession(const std::string& call_id)
    -> std::optional<MediaSessionState>
{
    std::lock_guard lock(mutex_);
    auto it = sessions_.find(call_id);
    if (it == sessions_.end()) return std::nullopt;
    return it->second;
}

void MediaSessionManager::updateToTag(const std::string& call_id, const std::string& to_tag) {
    std::lock_guard lock(mutex_);
    if (auto it = sessions_.find(call_id); it != sessions_.end()) {
        it->second.session.to_tag = to_tag;
    }
}

void MediaSessionManager::updateCallerSdp(const std::string& call_id, const std::string& sdp) {
    std::lock_guard lock(mutex_);
    if (auto it = sessions_.find(call_id); it != sessions_.end()) {
        it->second.caller_sdp = sdp;
    }
}

void MediaSessionManager::updateCalleeSdp(const std::string& call_id, const std::string& sdp) {
    std::lock_guard lock(mutex_);
    if (auto it = sessions_.find(call_id); it != sessions_.end()) {
        it->second.callee_sdp = sdp;
    }
}

void MediaSessionManager::setRxSession(const std::string& call_id,
                                        const std::string& rx_session_id) {
    std::lock_guard lock(mutex_);
    if (auto it = sessions_.find(call_id); it != sessions_.end()) {
        it->second.rx_session_id = rx_session_id;
    }
}

void MediaSessionManager::setQosActive(const std::string& call_id, bool active) {
    std::lock_guard lock(mutex_);
    if (auto it = sessions_.find(call_id); it != sessions_.end()) {
        it->second.qos_active = active;
    }
}

void MediaSessionManager::removeSession(const std::string& call_id) {
    std::lock_guard lock(mutex_);
    sessions_.erase(call_id);
    IMS_LOG_DEBUG("Removed media session for call={}", call_id);
}

auto MediaSessionManager::sessionCount() const -> size_t {
    std::lock_guard lock(mutex_);
    return sessions_.size();
}

} // namespace ims::media
