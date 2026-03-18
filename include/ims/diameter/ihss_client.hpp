#pragma once

#include "ims/common/types.hpp"
#include "ims/diameter/types.hpp"

namespace ims::diameter {

/// Abstract HSS/UDM client interface (Cx/Dx Diameter or Nudm HTTP/2)
///
/// Provides operations needed by I-CSCF and S-CSCF:
/// - UAR/UAA: User authorization and S-CSCF assignment (I-CSCF registration)
/// - MAR/MAA: Fetch AKA auth vectors (S-CSCF authentication)
/// - SAR/SAA: Server assignment and user profile download (S-CSCF registration)
/// - LIR/LIA: Location info query for MT call routing (I-CSCF)
struct IHssClient {
    virtual ~IHssClient() = default;

    /// User-Authorization-Request: I-CSCF asks HSS which S-CSCF to assign
    virtual auto userAuthorization(const UarParams& params) -> Result<UaaResult> = 0;

    /// Multimedia-Auth-Request: S-CSCF fetches AKA authentication vector
    virtual auto multimediaAuth(const MarParams& params) -> Result<MaaResult> = 0;

    /// Server-Assignment-Request: S-CSCF registers itself and downloads user profile
    virtual auto serverAssignment(const SarParams& params) -> Result<SaaResult> = 0;

    /// Location-Info-Request: I-CSCF queries serving S-CSCF for MT routing
    virtual auto locationInfo(const LirParams& params) -> Result<LiaResult> = 0;
};

} // namespace ims::diameter
