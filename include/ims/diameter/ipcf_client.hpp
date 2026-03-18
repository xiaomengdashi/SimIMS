#pragma once

#include "ims/common/types.hpp"
#include "ims/diameter/types.hpp"

namespace ims::diameter {

/// Abstract PCF/PCRF client interface (Rx Diameter)
///
/// Used by P-CSCF to request dedicated QoS bearers for VoNR media.
/// In VoNR, this triggers 5QI=1 QoS flow establishment via PCF -> SMF.
struct IPcfClient {
    virtual ~IPcfClient() = default;

    /// AA-Request: Request QoS resources for media session
    /// Called when P-CSCF receives 183/200 with SDP
    virtual auto authorizeSession(const AarParams& params) -> Result<AaaResult> = 0;

    /// Session-Termination-Request: Release QoS resources
    /// Called on BYE or session failure
    virtual auto terminateSession(const StrParams& params) -> Result<StaResult> = 0;

    /// Handle Abort-Session-Request from PCF (bearer lost)
    /// Implementation should trigger session release on P-CSCF side
    using AsrHandler = std::function<void(const AsrParams&)>;
    virtual void setAsrHandler(AsrHandler handler) = 0;
};

} // namespace ims::diameter
