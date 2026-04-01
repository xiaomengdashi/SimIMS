#pragma once

#include "common/types.hpp"

#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace ims::crypt {

struct MilenageVector {
    std::array<uint8_t, 16> autn{};
    std::array<uint8_t, 16> ck{};
    std::array<uint8_t, 16> ik{};
    std::array<uint8_t, 6> ak{};
    std::array<uint8_t, 8> xres{};
};

auto compute_opc(std::span<const uint8_t, 16> k,
                 std::span<const uint8_t, 16> op) -> Result<std::array<uint8_t, 16>>;

auto generate_vector(std::span<const uint8_t, 16> opc,
                     std::span<const uint8_t, 16> k,
                     std::span<const uint8_t, 6> sqn,
                     std::span<const uint8_t, 2> amf,
                     std::span<const uint8_t, 16> rand) -> Result<MilenageVector>;

} // namespace ims::crypt
