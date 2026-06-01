#pragma once

#include "core/types.hpp"

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace ims::sms {

enum class UserDataAlphabet : uint8_t {
    kGsm7BitDefault = 0,
    kEightBit = 1,
    kUcs2 = 2,
};

struct DecodedUserData {
    UserDataAlphabet alphabet = UserDataAlphabet::kGsm7BitDefault;
    std::string text;
    std::vector<uint8_t> raw;
};

/// Classify the alphabet/coding from a TP-DCS octet (3GPP TS 23.040 §4).
auto classify_dcs(uint8_t dcs) -> Result<UserDataAlphabet>;

/// Decode TP-User-Data payload bytes according to DCS.
/// @param user_data_length TP-UDL: septets for GSM 7-bit, octets for 8-bit/UCS2. Zero uses payload size.
/// @param user_data_header_present When true, the first octet is UDHL and a UDH is stripped before decoding.
auto decode_user_data(uint8_t dcs,
                      std::span<const uint8_t> user_data,
                      std::size_t user_data_length = 0,
                      bool user_data_header_present = false) -> Result<DecodedUserData>;

} // namespace ims::sms
