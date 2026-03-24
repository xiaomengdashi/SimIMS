#include "reg_event_notifier.hpp"

#include "common/logger.hpp"

namespace ims::sip {

void applyInitialRegNotifyContext(SipMessage& notify,
                                  const InitialRegNotifyContext& context) {
    notify.setRequestUri(context.request_uri);
    notify.setFromHeader(context.from_header);
    notify.setToHeader(context.to_header);
    notify.setCallId(context.call_id);
    notify.setCSeq(context.cseq, "NOTIFY");
    notify.removeHeader("Event");
    notify.removeHeader("Subscription-State");
    notify.addHeader("Max-Forwards", "70");
    notify.addHeader("Event", context.event);
    notify.addHeader("Subscription-State", context.subscription_state);

    for (const auto& route : context.route_set) {
        notify.addRoute(route);
    }

    if (!context.contact.empty()) {
        notify.setContact(context.contact);
    }
    if (!context.body.empty()) {
        notify.setBody(context.body, context.content_type);
    }
}

ExosipRegEventNotifier::ExosipRegEventNotifier(const ims::ExosipConfig& config)
    : exosip_(config) {}

auto ExosipRegEventNotifier::start() -> VoidResult {
    if (!exosip_.config().enabled) {
        return {};
    }

    std::lock_guard lock(mutex_);
    return exosip_.ensureStarted();
}

auto ExosipRegEventNotifier::sendInitialNotify(const InitialRegNotifyContext& context) -> VoidResult {
    if (!exosip_.config().enabled) {
        return {};
    }

    std::lock_guard lock(mutex_);

    auto notify_result = exosip_.buildRequest("NOTIFY", context.request_uri, context.from_header);
    if (!notify_result) {
        return std::unexpected(notify_result.error());
    }

    auto notify = std::move(*notify_result);
    applyInitialRegNotifyContext(notify, context);

    auto transaction_id = exosip_.sendRequest(std::move(notify));
    if (!transaction_id) {
        return std::unexpected(transaction_id.error());
    }

    auto status = exosip_.waitForFinalResponse(*transaction_id, 5000);
    if (!status) {
        return std::unexpected(status.error());
    }

    IMS_LOG_DEBUG("Initial NOTIFY via eXosip got {} response", *status);
    if (*status < 200 || *status >= 300) {
        return std::unexpected(ErrorInfo{
            ErrorCode::kSipTransactionFailed,
            "Initial NOTIFY rejected",
            std::to_string(*status)
        });
    }

    return {};
}

void ExosipRegEventNotifier::shutdown() {
    std::lock_guard lock(mutex_);
    exosip_.shutdown();
}

} // namespace ims::sip
