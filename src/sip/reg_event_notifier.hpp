#pragma once

#include "common/types.hpp"
#include "exosip_context.hpp"

#include <mutex>
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

class ExosipRegEventNotifier final : public IRegEventNotifier {
public:
    explicit ExosipRegEventNotifier(const ims::ExosipConfig& config);

    auto start() -> VoidResult override;
    auto sendInitialNotify(const InitialRegNotifyContext& context) -> VoidResult override;
    void shutdown() override;

private:
    ExosipContext exosip_;
    std::mutex mutex_;
};

} // namespace ims::sip
