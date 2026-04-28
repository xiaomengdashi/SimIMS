#include "media_session.hpp"
#include "common/logger.hpp"

#include <functional>

namespace ims::media {

namespace {

auto hashCombine(std::size_t seed, const std::string& value) -> std::size_t {
    return seed ^ (std::hash<std::string>{}(value) + 0x9e3779b9 + (seed << 6) + (seed >> 2));
}

} // namespace

auto MediaSessionKeyHash::operator()(const MediaSessionKey& key) const -> std::size_t {
    auto seed = std::hash<std::string>{}(key.call_id);
    seed = hashCombine(seed, key.from_tag);
    return hashCombine(seed, key.to_tag);
}

auto MediaSessionManager::createSession(const std::string& call_id,
                                         const std::string& from_tag) -> MediaSession
{
    return createSession(MediaSessionKey{.call_id = call_id, .from_tag = from_tag});
}

auto MediaSessionManager::createSession(const MediaSessionKey& key) -> MediaSession
{
    std::lock_guard lock(mutex_);

    MediaSession session{
        .call_id = key.call_id,
        .from_tag = key.from_tag,
        .to_tag = key.to_tag,
    };

    MediaSessionState state{
        .session = session,
        .created = std::chrono::steady_clock::now(),
    };

    sessions_[key] = std::move(state);
    IMS_LOG_DEBUG("Created media session for call={} from_tag={} to_tag={}",
                  key.call_id, key.from_tag, key.to_tag);
    return session;
}

auto MediaSessionManager::getSession(const MediaSessionKey& key)
    -> std::optional<MediaSessionState>
{
    std::lock_guard lock(mutex_);
    auto it = sessions_.find(key);
    if (it == sessions_.end()) return std::nullopt;
    return it->second;
}

auto MediaSessionManager::getSession(const std::string& call_id)
    -> std::optional<MediaSessionState>
{
    std::lock_guard lock(mutex_);
    auto it = findUniqueByCallIdLocked(call_id);
    if (!it) return std::nullopt;
    return (*it)->second;
}

void MediaSessionManager::updateToTag(const MediaSessionKey& key, const std::string& to_tag) {
    std::lock_guard lock(mutex_);
    auto it = sessions_.find(key);
    if (it == sessions_.end()) {
        return;
    }

    auto state = std::move(it->second);
    sessions_.erase(it);

    auto updated_key = MediaSessionKey{
        .call_id = key.call_id,
        .from_tag = key.from_tag,
        .to_tag = to_tag,
    };
    state.session.to_tag = to_tag;
    sessions_[updated_key] = std::move(state);
}

void MediaSessionManager::updateToTag(const std::string& call_id, const std::string& to_tag) {
    std::lock_guard lock(mutex_);
    auto it = findUniqueByCallIdLocked(call_id);
    if (!it) return;

    auto key = (*it)->first;
    auto state = std::move((*it)->second);
    sessions_.erase(*it);

    key.to_tag = to_tag;
    state.session.to_tag = to_tag;
    sessions_[key] = std::move(state);
}

void MediaSessionManager::updateCallerSdp(const MediaSessionKey& key, const std::string& sdp) {
    std::lock_guard lock(mutex_);
    if (auto it = sessions_.find(key); it != sessions_.end()) {
        it->second.caller_sdp = sdp;
    }
}

void MediaSessionManager::updateCallerSdp(const std::string& call_id, const std::string& sdp) {
    std::lock_guard lock(mutex_);
    auto it = findUniqueByCallIdLocked(call_id);
    if (it) {
        (*it)->second.caller_sdp = sdp;
    }
}

void MediaSessionManager::updateCalleeSdp(const MediaSessionKey& key, const std::string& sdp) {
    std::lock_guard lock(mutex_);
    if (auto it = sessions_.find(key); it != sessions_.end()) {
        it->second.callee_sdp = sdp;
    }
}

void MediaSessionManager::updateCalleeSdp(const std::string& call_id, const std::string& sdp) {
    std::lock_guard lock(mutex_);
    auto it = findUniqueByCallIdLocked(call_id);
    if (it) {
        (*it)->second.callee_sdp = sdp;
    }
}

void MediaSessionManager::setRxSession(const MediaSessionKey& key,
                                        const std::string& rx_session_id) {
    std::lock_guard lock(mutex_);
    if (auto it = sessions_.find(key); it != sessions_.end()) {
        it->second.rx_session_id = rx_session_id;
    }
}

void MediaSessionManager::setRxSession(const std::string& call_id,
                                        const std::string& rx_session_id) {
    std::lock_guard lock(mutex_);
    auto it = findUniqueByCallIdLocked(call_id);
    if (it) {
        (*it)->second.rx_session_id = rx_session_id;
    }
}

void MediaSessionManager::setQosActive(const MediaSessionKey& key, bool active) {
    std::lock_guard lock(mutex_);
    if (auto it = sessions_.find(key); it != sessions_.end()) {
        it->second.qos_active = active;
    }
}

void MediaSessionManager::setQosActive(const std::string& call_id, bool active) {
    std::lock_guard lock(mutex_);
    auto it = findUniqueByCallIdLocked(call_id);
    if (it) {
        (*it)->second.qos_active = active;
    }
}

void MediaSessionManager::removeSession(const MediaSessionKey& key) {
    std::lock_guard lock(mutex_);
    sessions_.erase(key);
    IMS_LOG_DEBUG("Removed media session for call={} from_tag={} to_tag={}",
                  key.call_id, key.from_tag, key.to_tag);
}

void MediaSessionManager::removeSession(const std::string& call_id) {
    std::lock_guard lock(mutex_);
    auto it = findUniqueByCallIdLocked(call_id);
    if (!it) return;

    IMS_LOG_DEBUG("Removed media session for call={} from_tag={} to_tag={}",
                  (*it)->first.call_id, (*it)->first.from_tag, (*it)->first.to_tag);
    sessions_.erase(*it);
}

auto MediaSessionManager::sessionCount() const -> size_t {
    std::lock_guard lock(mutex_);
    return sessions_.size();
}

auto MediaSessionManager::findUniqueByCallIdLocked(const std::string& call_id)
    -> std::optional<SessionIterator>
{
    std::optional<SessionIterator> found;
    for (auto it = sessions_.begin(); it != sessions_.end(); ++it) {
        if (it->first.call_id != call_id) {
            continue;
        }
        if (found) {
            IMS_LOG_WARN("Ambiguous media session lookup for call={}", call_id);
            return std::nullopt;
        }
        found = it;
    }
    return found;
}

auto MediaSessionManager::findUniqueByCallIdLocked(const std::string& call_id) const
    -> std::optional<ConstSessionIterator>
{
    std::optional<ConstSessionIterator> found;
    for (auto it = sessions_.cbegin(); it != sessions_.cend(); ++it) {
        if (it->first.call_id != call_id) {
            continue;
        }
        if (found) {
            IMS_LOG_WARN("Ambiguous media session lookup for call={}", call_id);
            return std::nullopt;
        }
        found = it;
    }
    return found;
}

} // namespace ims::media
