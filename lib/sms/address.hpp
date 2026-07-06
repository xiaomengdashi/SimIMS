#pragma once

#include "core/types.hpp"

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace ims::sms {

/// SmsAddress：digit_length 统一表示"数字位数"。
/// 但 RP 层（TS 24.011 §8.2.5）与 TPDU 层（TS 23.040 §9.1.2.5）的 Address-Length
/// 字段语义不同，必须用各自专属的解析/编码函数：
///   - parse_address / SmsAddress::encode —— TPDU 层，Length = 半字节数（useful semi-octets）
///   - parse_rp_address / encode_rp_address —— RP 层，Length = 后续字节数（含 Type-of-Address）
struct SmsAddress {
    uint8_t digit_length = 0;
    uint8_t type_of_address = 0;
    std::vector<uint8_t> bcd;

    /// TPDU 层编码（TS 23.040）：Length 字段 = digit_length（半字节数）。
    auto encoded_size() const -> std::size_t;
    auto encode(std::vector<uint8_t>& out) const -> VoidResult;
};

/// TPDU 层地址解析（TS 23.040）：Address-Length = 半字节数。
auto parse_address(std::span<const uint8_t> data, std::size_t& offset) -> Result<SmsAddress>;
/// RP 层地址解析（TS 24.011 §8.2.5）：Address-Length = 后续字节数（含 Type-of-Address）。
auto parse_rp_address(std::span<const uint8_t> data, std::size_t& offset) -> Result<SmsAddress>;
/// RP 层地址编码（TS 24.011 §8.2.5）：Length 字段 = 1(Type) + BCD 字节数。
auto encode_rp_address(const SmsAddress& address, std::vector<uint8_t>& out) -> VoidResult;

auto decode_bcd_msisdn(const SmsAddress& address) -> std::string;
auto encode_bcd_msisdn(std::string_view e164_digits, uint8_t type_of_address) -> Result<SmsAddress>;

} // namespace ims::sms
