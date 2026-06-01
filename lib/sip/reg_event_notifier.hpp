#pragma once

#include "core/types.hpp"
#include "stack.hpp"

#include <string>
#include <vector>

namespace ims::sip {

struct InitialRegNotifyContext {
    std::string request_uri;
    std::string from_header;
    std::string to_header;
    std::string call_id;
    uint32_t cseq = 1;
    std::string event = "reg";
    std::string subscription_state;
    std::vector<std::string> route_set;
    std::string contact;
    std::string body;
    std::string content_type = "application/reginfo+xml";
};

void applyInitialRegNotifyContext(SipMessage& notify,
                                  const InitialRegNotifyContext& context);

struct IRegEventNotifier {
    virtual ~IRegEventNotifier() = default;

    virtual auto start() -> VoidResult = 0;
    virtual auto sendInitialNotify(const InitialRegNotifyContext& context) -> VoidResult = 0;
    virtual void shutdown() = 0;
};

class SipStackRegEventNotifier final : public IRegEventNotifier {
public:
    SipStackRegEventNotifier(SipStack& sip_stack,
                             std::string local_address,
                             Port local_port);

    auto start() -> VoidResult override;
    auto sendInitialNotify(const InitialRegNotifyContext& context) -> VoidResult override;
    void shutdown() override;

private:
    SipStack& sip_stack_;
    std::string local_address_;
    Port local_port_;
};

} // namespace ims::sip
