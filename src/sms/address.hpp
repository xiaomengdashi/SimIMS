#pragma once

#include "common/types.hpp"

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace ims::sms {

/// RP/TP address: length (digits), type-of-address, BCD payload (3GPP TS 24.011 / 23.040).
struct SmsAddress {
    uint8_t digit_length = 0;
    uint8_t type_of_address = 0;
    std::vector<uint8_t> bcd;

    auto encoded_size() const -> std::size_t;
    auto encode(std::vector<uint8_t>& out) const -> VoidResult;
};

auto parse_address(std::span<const uint8_t> data, std::size_t& offset) -> Result<SmsAddress>;
auto decode_bcd_msisdn(const SmsAddress& address) -> std::string;
auto encode_bcd_msisdn(std::string_view e164_digits, uint8_t type_of_address) -> Result<SmsAddress>;

} // namespace ims::sms
