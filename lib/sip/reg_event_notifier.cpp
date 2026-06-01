#include "reg_event_notifier.hpp"

#include "core/logger.hpp"
#include "uri_utils.hpp"

#include <algorithm>
#include <cctype>
#include <format>

namespace ims::sip {

namespace {

auto normalized_transport(std::string transport) -> std::string {
    if (transport.empty()) {
        return "udp";
    }
    std::transform(transport.begin(), transport.end(), transport.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return transport;
}

} // namespace

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

SipStackRegEventNotifier::SipStackRegEventNotifier(SipStack& sip_stack,
                                                   std::string local_address,
                                                   Port local_port)
    : sip_stack_(sip_stack)
    , local_address_(std::move(local_address))
    , local_port_(local_port) {}

auto SipStackRegEventNotifier::start() -> VoidResult {
    return {};
}

auto SipStackRegEventNotifier::sendInitialNotify(const InitialRegNotifyContext& context) -> VoidResult {
    auto notify_result = createRequest("NOTIFY", context.request_uri);
    if (!notify_result) {
        return std::unexpected(notify_result.error());
    }

    auto notify = std::move(*notify_result);
    applyInitialRegNotifyContext(notify, context);

    auto dest = parse_endpoint_from_uri(context.request_uri);
    if (!dest) {
        return std::unexpected(ErrorInfo{
            ErrorCode::kSipParseError,
            "Invalid NOTIFY request URI",
            context.request_uri,
        });
    }

    dest->transport = normalized_transport(dest->transport);

    std::string transport_token = dest->transport;
    std::transform(transport_token.begin(), transport_token.end(), transport_token.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::toupper(ch)); });
    notify.addVia(std::format("SIP/2.0/{} {}:{};branch={};rport",
                              transport_token,
                              local_address_,
                              local_port_,
                              generateBranch()));

    auto send_result = sip_stack_.sendRequest(std::move(notify), *dest,
        [](const SipMessage& response) {
            const auto status = response.statusCode();
            if (status >= 200 && status < 300) {
                IMS_LOG_DEBUG("Initial NOTIFY got {} response", status);
                return;
            }
            IMS_LOG_WARN("Initial NOTIFY rejected with {} {}", status, response.reasonPhrase());
        });
    if (!send_result) {
        return std::unexpected(send_result.error());
    }

    IMS_LOG_DEBUG("Initial NOTIFY sent via SIP stack to {}:{}",
                  dest->address,
                  dest->port);

    return {};
}

void SipStackRegEventNotifier::shutdown() {
}

} // namespace ims::sip
