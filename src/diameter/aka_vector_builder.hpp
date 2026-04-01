#pragma once

#include "common/config.hpp"
#include "diameter/types.hpp"

#include <array>
#include <random>

namespace ims::diameter {

auto build_aka_auth_vector(const HssSubscriberConfig& subscriber,
                           std::mt19937& rng) -> Result<AuthVector>;

} // namespace ims::diameter
