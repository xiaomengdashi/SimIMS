#include "ims/sip/dialog.hpp"
#include "ims/common/logger.hpp"
#include <osipparser2/osip_parser.h>

namespace ims::sip {

// ========== Dialog ==========

Dialog::Dialog(const DialogId& id, bool is_caller)
    : id_(id)
    , is_caller_(is_caller) {}

auto Dialog::id() const -> const DialogId& {
    return id_;
}

auto Dialog::state() const -> DialogState {
    return state_;
}

auto Dialog::localUri() const -> const std::string& {
    return local_uri_;
}

auto Dialog::remoteUri() const -> const std::string& {
    return remote_uri_;
}

auto Dialog::remoteTarget() const -> const std::string& {
    return remote_target_;
}

auto Dialog::routeSet() const -> const std::vector<std::string>& {
    return route_set_;
}

auto Dialog::localCSeq() const -> uint32_t {
    return local_cseq_;
}

auto Dialog::remoteCSeq() const -> uint32_t {
    return remote_cseq_;
}

auto Dialog::nextLocalCSeq() -> uint32_t {
    return ++local_cseq_;
}

void Dialog::updateFromRequest(const SipMessage& request) {
    if (is_caller_) {
        // We are UAC, this is our outgoing request
        local_uri_ = request.fromHeader();
        remote_uri_ = request.toHeader();
        local_cseq_ = request.cseq();
    } else {
        // We are UAS, this is an incoming request
        remote_uri_ = request.fromHeader();
        local_uri_ = request.toHeader();
        remote_cseq_ = request.cseq();

        // Update remote target from Contact
        auto ct = request.contact();
        if (ct) {
            remote_target_ = *ct;
        }
    }
}

void Dialog::updateFromResponse(const SipMessage& response, bool is_target_refresh) {
    int code = response.statusCode();

    if (is_caller_) {
        // We are UAC, this is the response to our request
        auto ct = response.contact();
        if (ct && (is_target_refresh || state_ == DialogState::kEarly)) {
            remote_target_ = *ct;
        }

        // Capture To tag for remote tag
        auto to_tag = response.toTag();
        if (!to_tag.empty() && id_.remote_tag.empty()) {
            id_.remote_tag = to_tag;
        }

        // Build route set from Record-Route (reversed for UAC)
        if (state_ == DialogState::kEarly && code >= 200 && code < 300) {
            route_set_.clear();
            osip_record_route_t* rr = nullptr;
            int pos = 0;
            while (osip_message_get_record_route(response.raw(), pos, &rr) == 0 && rr) {
                char* buf = nullptr;
                if (osip_record_route_to_str(rr, &buf) == 0 && buf) {
                    route_set_.emplace_back(buf);
                    osip_free(buf);
                }
                ++pos;
            }
            // UAC reverses the Record-Route set
            // (Record-Route is already in top-to-bottom order in the response,
            //  which is the correct order for the UAC route set)
        }
    } else {
        // We are UAS, this is our outgoing response
        if (state_ == DialogState::kEarly && code >= 200 && code < 300) {
            // Build route set from Record-Route (not reversed for UAS)
            route_set_.clear();
            osip_record_route_t* rr = nullptr;
            int pos = 0;
            while (osip_message_get_record_route(response.raw(), pos, &rr) == 0 && rr) {
                char* buf = nullptr;
                if (osip_record_route_to_str(rr, &buf) == 0 && buf) {
                    route_set_.emplace_back(buf);
                    osip_free(buf);
                }
                ++pos;
            }
            // UAS reverses the order
            std::reverse(route_set_.begin(), route_set_.end());
        }
    }

    // Update dialog state
    if (code >= 101 && code < 200 && state_ == DialogState::kEarly) {
        // Stay in early state
    } else if (code >= 200 && code < 300) {
        state_ = DialogState::kConfirmed;
    } else if (code >= 300) {
        state_ = DialogState::kTerminated;
    }

    IMS_LOG_DEBUG("Dialog updated, call_id={}, state={}", id_.call_id, static_cast<int>(state_));
}

void Dialog::setState(DialogState state) {
    state_ = state;
}

// ========== DialogManager ==========

auto DialogManager::createDialog(const SipMessage& request, bool is_caller)
    -> std::shared_ptr<Dialog> {
    DialogId id;
    id.call_id = request.callId();

    if (is_caller) {
        id.local_tag = request.fromTag();
        id.remote_tag = request.toTag();
    } else {
        id.local_tag = request.toTag();
        id.remote_tag = request.fromTag();
    }

    auto dialog = std::make_shared<Dialog>(id, is_caller);
    dialog->updateFromRequest(request);

    std::lock_guard lock(mutex_);
    dialogs_[id] = dialog;

    IMS_LOG_DEBUG("Dialog created, call_id={}, local_tag={}, remote_tag={}",
        id.call_id, id.local_tag, id.remote_tag);

    return dialog;
}

auto DialogManager::findDialog(const DialogId& id) -> std::shared_ptr<Dialog> {
    std::lock_guard lock(mutex_);
    auto it = dialogs_.find(id);
    if (it != dialogs_.end()) {
        return it->second;
    }
    return nullptr;
}

auto DialogManager::findDialogByCallId(const std::string& call_id)
    -> std::vector<std::shared_ptr<Dialog>> {
    std::vector<std::shared_ptr<Dialog>> result;
    std::lock_guard lock(mutex_);
    for (const auto& [id, dialog] : dialogs_) {
        if (id.call_id == call_id) {
            result.push_back(dialog);
        }
    }
    return result;
}

void DialogManager::removeDialog(const DialogId& id) {
    std::lock_guard lock(mutex_);
    dialogs_.erase(id);
    IMS_LOG_DEBUG("Dialog removed, call_id={}", id.call_id);
}

auto DialogManager::dialogCount() const -> size_t {
    std::lock_guard lock(mutex_);
    return dialogs_.size();
}

} // namespace ims::sip
