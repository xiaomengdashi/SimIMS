#pragma once

#include "core/types.hpp"

#include <array>
#include <cstdint>
#include <span>

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

// When use_ak is true (default, standard IMS-AKA) the SQN field in AUTN is
// concealed with the Anonymity Key: AUTN = (SQN xor AK) || AMF || MAC-A.
// When false, SQN is carried in clear: AUTN = SQN || AMF || MAC-A. MAC-A is
// always computed over the cleartext SQN regardless of this flag.
auto generate_vector(std::span<const uint8_t, 16> opc,
                     std::span<const uint8_t, 16> k,
                     std::span<const uint8_t, 6> sqn,
                     std::span<const uint8_t, 2> amf,
                     std::span<const uint8_t, 16> rand,
                     bool use_ak = true) -> Result<MilenageVector>;

} // namespace ims::crypt
