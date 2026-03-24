#pragma once

#include "diameter/ipcf_client.hpp"
#include "common/config.hpp"

namespace ims::diameter {

/// Stub PCF/PCRF client for development/testing.
class StubPcfClient : public IPcfClient {
public:
    explicit StubPcfClient(const PcfSettings& config);

    auto authorizeSession(const AarParams& params) -> Result<AaaResult> override;
    auto terminateSession(const StrParams& params) -> Result<StaResult> override;
    void setAsrHandler(AsrHandler handler) override;

private:
    PcfSettings config_;
    AsrHandler asr_handler_;
    uint32_t session_counter_ = 0;
};

} // namespace ims::diameter
