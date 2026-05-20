#pragma once

#include "common/types.hpp"

#include <span>
#include <string>
#include <vector>

namespace ims::sms {

auto decode_hex(std::string_view hex) -> Result<std::vector<uint8_t>>;
auto encode_hex(std::span<const uint8_t> bytes) -> std::string;

} // namespace ims::sms
