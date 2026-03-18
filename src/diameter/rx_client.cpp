#include "rx_client.hpp"
#include "ims/common/logger.hpp"

#include <format>

namespace ims::diameter {

StubPcfClient::StubPcfClient(const PcfSettings& config)
    : config_(config)
{
    IMS_LOG_WARN("Using STUB PCF client - no real QoS will be established");
}

auto StubPcfClient::authorizeSession(const AarParams& params) -> Result<AaaResult> {
    IMS_LOG_INFO("Stub AAR: subscriber={} media_components={}",
                 params.subscription_id, params.media_components.size());

    auto session_id = std::format("stub-rx-session-{}", ++session_counter_);

    return AaaResult{
        .result_code = 2001,
        .session_id = session_id,
    };
}

auto StubPcfClient::terminateSession(const StrParams& params) -> Result<StaResult> {
    IMS_LOG_INFO("Stub STR: session={}", params.session_id);

    return StaResult{
        .result_code = 2001,
    };
}

void StubPcfClient::setAsrHandler(AsrHandler handler) {
    asr_handler_ = std::move(handler);
}

} // namespace ims::diameter
