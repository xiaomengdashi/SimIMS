#include "scscf_selector.hpp"
#include "common/logger.hpp"

namespace ims::icscf {

ScscfSelector::ScscfSelector(std::shared_ptr<ims::diameter::IHssClient> hss)
    : hss_(std::move(hss))
{
}

auto ScscfSelector::selectForRegistration(const std::string& impi,
                                           const std::string& impu,
                                           const std::string& visited_network) -> Result<std::string>
{
    IMS_LOG_DEBUG("Selecting S-CSCF for registration: IMPI={} IMPU={}", impi, impu);

    ims::diameter::UarParams params{
        .impi = impi,
        .impu = impu,
        .visited_network = visited_network,
        .auth_type = ims::diameter::UarParams::AuthType::kRegistration,
    };

    auto result = hss_->userAuthorization(params);
    if (!result) {
        IMS_LOG_ERROR("UAR failed: {}", result.error().message);
        return std::unexpected(result.error());
    }

    if (result->assigned_scscf.empty()) {
        // HSS returned capabilities, need to select based on local config
        // For now, this shouldn't happen with stub HSS
        return std::unexpected(ErrorInfo{
            ErrorCode::kDiameterAuthFailed,
            "No S-CSCF assigned and capability selection not implemented"
        });
    }

    IMS_LOG_INFO("Selected S-CSCF: {} for IMPU={}", result->assigned_scscf, impu);
    return result->assigned_scscf;
}

auto ScscfSelector::selectForRouting(const std::string& impu) -> Result<std::string> {
    IMS_LOG_DEBUG("Looking up serving S-CSCF for IMPU={}", impu);

    ims::diameter::LirParams params{
        .impu = impu,
    };

    auto result = hss_->locationInfo(params);
    if (!result) {
        IMS_LOG_ERROR("LIR failed: {}", result.error().message);
        return std::unexpected(result.error());
    }

    if (result->assigned_scscf.empty()) {
        return std::unexpected(ErrorInfo{
            ErrorCode::kDiameterUserNotFound,
            "No serving S-CSCF found for " + impu
        });
    }

    IMS_LOG_INFO("Found serving S-CSCF: {} for IMPU={}", result->assigned_scscf, impu);
    return result->assigned_scscf;
}

} // namespace ims::icscf
