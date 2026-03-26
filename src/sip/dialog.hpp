#pragma once

#include "message.hpp"

#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace ims::sip {

enum class DialogState {
    kEarly,
    kConfirmed,
    kTerminated,
};

struct DialogId {
    std::string call_id;
    std::string local_tag;
    std::string remote_tag;
};

inline auto operator==(const DialogId& lhs, const DialogId& rhs) -> bool {
    return lhs.call_id == rhs.call_id
        && lhs.local_tag == rhs.local_tag
        && lhs.remote_tag == rhs.remote_tag;
}

struct DialogIdHash {
    std::size_t operator()(const DialogId& id) const {
        auto h1 = std::hash<std::string>{}(id.call_id);
        auto h2 = std::hash<std::string>{}(id.local_tag);
        auto h3 = std::hash<std::string>{}(id.remote_tag);
        return h1 ^ (h2 << 1) ^ (h3 << 2);
    }
};

class Dialog {
public:
    Dialog(const DialogId& id, bool is_caller);

    auto id() const -> const DialogId&;
    auto state() const -> DialogState;
    auto localUri() const -> const std::string&;
    auto remoteUri() const -> const std::string&;
    auto remoteTarget() const -> const std::string&;
    auto routeSet() const -> const std::vector<std::string>&;
    auto localCSeq() const -> uint32_t;
    auto remoteCSeq() const -> uint32_t;
    auto nextLocalCSeq() -> uint32_t;

    void updateFromRequest(const SipMessage& request);
    void updateFromResponse(const SipMessage& response, bool is_target_refresh = false);
    void setState(DialogState state);

private:
    DialogId id_;
    bool is_caller_;
    DialogState state_ = DialogState::kEarly;
    std::string local_uri_;
    std::string remote_uri_;
    std::string remote_target_;
    std::vector<std::string> route_set_;
    uint32_t local_cseq_ = 0;
    uint32_t remote_cseq_ = 0;
};

class DialogManager {
public:
    auto createDialog(const SipMessage& request, bool is_caller) -> std::shared_ptr<Dialog>;
    auto findDialog(const DialogId& id) -> std::shared_ptr<Dialog>;
    auto findDialogByCallId(const std::string& call_id) -> std::vector<std::shared_ptr<Dialog>>;
    void removeDialog(const DialogId& id);
    auto dialogCount() const -> size_t;

private:
    mutable std::mutex mutex_;
    std::unordered_map<DialogId, std::shared_ptr<Dialog>, DialogIdHash> dialogs_;
};

} // namespace ims::sip
