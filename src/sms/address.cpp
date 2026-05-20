#include "address.hpp"

#include <algorithm>
#include <cctype>
#include <format>

namespace ims::sms {

namespace {

auto is_digit(char ch) -> bool {
    return ch >= '0' && ch <= '9';
}

} // namespace

auto SmsAddress::encoded_size() const -> std::size_t {
    if (digit_length == 0) {
        return 1;
    }
    return 2 + ((digit_length + 1) / 2);
}

auto SmsAddress::encode(std::vector<uint8_t>& out) const -> VoidResult {
    if (digit_length == 0) {
        out.push_back(0x00);
        return {};
    }
    if (digit_length > 20) {
        return std::unexpected(ims::ErrorInfo{
            ims::ErrorCode::kSmsInvalidPayload,
            "SMS address digit length out of range",
        });
    }
    out.push_back(digit_length);
    out.push_back(type_of_address);
    out.insert(out.end(), bcd.begin(), bcd.end());
    return {};
}

auto parse_address(std::span<const uint8_t> data, std::size_t& offset) -> Result<SmsAddress> {
    if (offset >= data.size()) {
        return std::unexpected(ims::ErrorInfo{
            ims::ErrorCode::kSmsParseError,
            "truncated SMS address",
        });
    }

    SmsAddress address;
    address.digit_length = data[offset++];
    if (address.digit_length == 0) {
        return address;
    }

    if (offset >= data.size()) {
        return std::unexpected(ims::ErrorInfo{
            ims::ErrorCode::kSmsParseError,
            "truncated SMS address type",
        });
    }

    address.type_of_address = data[offset++];
    const auto bcd_bytes = (address.digit_length + 1) / 2;
    if (offset + bcd_bytes > data.size()) {
        return std::unexpected(ims::ErrorInfo{
            ims::ErrorCode::kSmsParseError,
            "truncated SMS address BCD",
        });
    }

    address.bcd.assign(data.begin() + static_cast<std::ptrdiff_t>(offset),
                       data.begin() + static_cast<std::ptrdiff_t>(offset + bcd_bytes));
    offset += bcd_bytes;
    return address;
}

auto decode_bcd_msisdn(const SmsAddress& address) -> std::string {
    if (address.digit_length == 0) {
        return {};
    }

    std::string digits;
    digits.reserve(address.digit_length);
    for (auto byte : address.bcd) {
        digits.push_back(static_cast<char>('0' + (byte & 0x0F)));
        if (digits.size() >= address.digit_length) {
            break;
        }
        digits.push_back(static_cast<char>('0' + ((byte >> 4) & 0x0F)));
        if (digits.size() >= address.digit_length) {
            break;
        }
    }
    if (digits.size() > address.digit_length) {
        digits.resize(address.digit_length);
    }
    return digits;
}

auto encode_bcd_msisdn(std::string_view e164_digits, uint8_t type_of_address) -> Result<SmsAddress> {
    std::string digits;
    digits.reserve(e164_digits.size());
    for (char ch : e164_digits) {
        if (ch == '+') {
            continue;
        }
        if (!is_digit(ch)) {
            return std::unexpected(ims::ErrorInfo{
                ims::ErrorCode::kSmsInvalidPayload,
                std::format("invalid MSISDN digit '{}'", ch),
            });
        }
        digits.push_back(ch);
    }

    if (digits.empty() || digits.size() > 20) {
        return std::unexpected(ims::ErrorInfo{
            ims::ErrorCode::kSmsInvalidPayload,
            "invalid MSISDN length",
        });
    }

    SmsAddress address;
    address.digit_length = static_cast<uint8_t>(digits.size());
    address.type_of_address = type_of_address;
    address.bcd.resize((digits.size() + 1) / 2, 0xFF);

    for (std::size_t i = 0; i < digits.size(); ++i) {
        const auto nibble = static_cast<uint8_t>(digits[i] - '0');
        if ((i & 1U) == 0) {
            address.bcd[i / 2] = static_cast<uint8_t>((address.bcd[i / 2] & 0xF0) | nibble);
        } else {
            address.bcd[i / 2] = static_cast<uint8_t>((address.bcd[i / 2] & 0x0F) | (nibble << 4));
        }
    }
    return address;
}

} // namespace ims::sms
