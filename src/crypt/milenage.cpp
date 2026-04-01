#include "crypt/milenage.hpp"

#include <openssl/aes.h>

#include <algorithm>
#include <array>
#include <cstddef>

namespace ims::crypt {
namespace {

using Block16 = std::array<uint8_t, 16>;

constexpr uint8_t kR1 = 64;
constexpr uint8_t kR2 = 0;
constexpr uint8_t kR3 = 32;
constexpr uint8_t kR4 = 64;
constexpr uint8_t kR5 = 96;

constexpr auto xor_block(std::span<const uint8_t, 16> lhs,
                         std::span<const uint8_t, 16> rhs) -> Block16 {
    Block16 out{};
    for (std::size_t i = 0; i < out.size(); ++i) {
        out[i] = lhs[i] ^ rhs[i];
    }
    return out;
}

auto aes_128_encrypt(std::span<const uint8_t, 16> key,
                     std::span<const uint8_t, 16> in) -> Result<Block16> {
    AES_KEY aes_key;
    if (AES_set_encrypt_key(key.data(), 128, &aes_key) != 0) {
        return std::unexpected(ErrorInfo{ErrorCode::kInternalError, "failed to setup AES-128 key"});
    }

    Block16 out{};
    AES_encrypt(in.data(), out.data(), &aes_key);
    return out;
}

constexpr auto rotate_left(std::span<const uint8_t, 16> input,
                           uint8_t bits) -> Block16 {
    Block16 out{};
    const auto whole_bytes = static_cast<std::size_t>(bits / 8);
    const auto rem_bits = static_cast<uint8_t>(bits % 8);

    for (std::size_t i = 0; i < out.size(); ++i) {
        const auto src_index = (i + whole_bytes) % out.size();
        if (rem_bits == 0) {
            out[i] = input[src_index];
            continue;
        }

        const auto next_index = (src_index + 1) % out.size();
        out[i] = static_cast<uint8_t>((input[src_index] << rem_bits) |
                                      (input[next_index] >> (8 - rem_bits)));
    }
    return out;
}

auto f1(std::span<const uint8_t, 16> opc,
        std::span<const uint8_t, 16> k,
        std::span<const uint8_t, 16> rand,
        std::span<const uint8_t, 6> sqn,
        std::span<const uint8_t, 2> amf) -> Result<std::array<uint8_t, 8>> {
    const auto temp_input = xor_block(rand, opc);
    auto temp = aes_128_encrypt(k, temp_input);
    if (!temp) {
        return std::unexpected(temp.error());
    }

    Block16 in1{};
    std::copy(sqn.begin(), sqn.end(), in1.begin());
    std::copy(amf.begin(), amf.end(), in1.begin() + 6);
    std::copy(in1.begin(), in1.begin() + 8, in1.begin() + 8);

    auto rijndael_input = rotate_left(xor_block(in1, opc), kR1);
    for (std::size_t i = 0; i < rijndael_input.size(); ++i) {
        rijndael_input[i] ^= (*temp)[i];
    }

    auto out1 = aes_128_encrypt(k, rijndael_input);
    if (!out1) {
        return std::unexpected(out1.error());
    }

    for (std::size_t i = 0; i < out1->size(); ++i) {
        (*out1)[i] ^= opc[i];
    }

    std::array<uint8_t, 8> mac_a{};
    std::copy(out1->begin(), out1->begin() + 8, mac_a.begin());
    return mac_a;
}

auto f2345(std::span<const uint8_t, 16> opc,
           std::span<const uint8_t, 16> k,
           std::span<const uint8_t, 16> rand) -> Result<MilenageVector> {
    MilenageVector vector;

    const auto temp_input = xor_block(rand, opc);
    auto temp = aes_128_encrypt(k, temp_input);
    if (!temp) {
        return std::unexpected(temp.error());
    }

    auto out2_input = rotate_left(xor_block(*temp, opc), kR2);
    out2_input[15] ^= 0x01;
    auto out2 = aes_128_encrypt(k, out2_input);
    if (!out2) {
        return std::unexpected(out2.error());
    }
    for (std::size_t i = 0; i < out2->size(); ++i) {
        (*out2)[i] ^= opc[i];
    }
    std::copy(out2->begin(), out2->begin() + 6, vector.ak.begin());
    std::copy(out2->begin() + 8, out2->begin() + 16, vector.xres.begin());

    auto out3_input = rotate_left(xor_block(*temp, opc), kR3);
    out3_input[15] ^= 0x02;
    auto out3 = aes_128_encrypt(k, out3_input);
    if (!out3) {
        return std::unexpected(out3.error());
    }
    for (std::size_t i = 0; i < out3->size(); ++i) {
        vector.ck[i] = (*out3)[i] ^ opc[i];
    }

    auto out4_input = rotate_left(xor_block(*temp, opc), kR4);
    out4_input[15] ^= 0x04;
    auto out4 = aes_128_encrypt(k, out4_input);
    if (!out4) {
        return std::unexpected(out4.error());
    }
    for (std::size_t i = 0; i < out4->size(); ++i) {
        vector.ik[i] = (*out4)[i] ^ opc[i];
    }

    return vector;
}

} // namespace

auto compute_opc(std::span<const uint8_t, 16> k,
                 std::span<const uint8_t, 16> op) -> Result<std::array<uint8_t, 16>> {
    auto encrypted = aes_128_encrypt(k, op);
    if (!encrypted) {
        return std::unexpected(encrypted.error());
    }

    Block16 opc{};
    for (std::size_t i = 0; i < opc.size(); ++i) {
        opc[i] = (*encrypted)[i] ^ op[i];
    }
    return opc;
}

auto generate_vector(std::span<const uint8_t, 16> opc,
                     std::span<const uint8_t, 16> k,
                     std::span<const uint8_t, 6> sqn,
                     std::span<const uint8_t, 2> amf,
                     std::span<const uint8_t, 16> rand) -> Result<MilenageVector> {
    auto vector = f2345(opc, k, rand);
    if (!vector) {
        return std::unexpected(vector.error());
    }

    auto mac_a = f1(opc, k, rand, sqn, amf);
    if (!mac_a) {
        return std::unexpected(mac_a.error());
    }

    for (std::size_t i = 0; i < 6; ++i) {
        vector->autn[i] = sqn[i] ^ vector->ak[i];
    }
    vector->autn[6] = amf[0];
    vector->autn[7] = amf[1];
    std::copy(mac_a->begin(), mac_a->end(), vector->autn.begin() + 8);
    return *vector;
}

} // namespace ims::crypt
