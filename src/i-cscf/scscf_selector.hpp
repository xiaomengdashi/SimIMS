#pragma once

#include "diameter/ihss_client.hpp"
#include "diameter/types.hpp"
#include "common/types.hpp"

#include <memory>
#include <string>

namespace ims::icscf {

/// S-CSCF selection logic based on HSS capabilities and local config
class ScscfSelector {
public:
    explicit ScscfSelector(std::shared_ptr<ims::diameter::IHssClient> hss);

    /// Select S-CSCF for registration (uses UAR)
    auto selectForRegistration(const std::string& impi,
                               const std::string& impu,
                               const std::string& visited_network) -> Result<std::string>;

    /// Select S-CSCF for MT call routing (uses LIR)
    auto selectForRouting(const std::string& impu) -> Result<std::string>;

private:
    std::shared_ptr<ims::diameter::IHssClient> hss_;
};

} // namespace ims::icscf
