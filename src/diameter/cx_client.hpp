#pragma once

#include "ims/diameter/ihss_client.hpp"
#include "ims/common/config.hpp"

namespace ims::diameter {

/// Stub HSS client for development/testing.
/// Returns mock data - replace with freeDiameter-based implementation for production.
class StubHssClient : public IHssClient {
public:
    explicit StubHssClient(const HssAdapterConfig& config);

    auto userAuthorization(const UarParams& params) -> Result<UaaResult> override;
    auto multimediaAuth(const MarParams& params) -> Result<MaaResult> override;
    auto serverAssignment(const SarParams& params) -> Result<SaaResult> override;
    auto locationInfo(const LirParams& params) -> Result<LiaResult> override;

private:
    HssAdapterConfig config_;
    std::string scscf_uri_;
};

} // namespace ims::diameter
