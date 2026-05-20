#include "hex_utils.hpp"

#include <cctype>
#include <format>

namespace ims::sms {

namespace {

auto hex_value(char ch) -> std::optional<uint8_t> {
    if (ch >= '0' && ch <= '9') {
        return static_cast<uint8_t>(ch - '0');
    }
    if (ch >= 'a' && ch <= 'f') {
        return static_cast<uint8_t>(10 + ch - 'a');
    }
    if (ch >= 'A' && ch <= 'F') {
        return static_cast<uint8_t>(10 + ch - 'A');
    }
    return std::nullopt;
}

} // namespace

auto decode_hex(std::string_view hex) -> Result<std::vector<uint8_t>> {
    std::string cleaned;
    cleaned.reserve(hex.size());
    for (char ch : hex) {
        if (std::isspace(static_cast<unsigned char>(ch)) != 0) {
            continue;
        }
        cleaned.push_back(ch);
    }

    if (cleaned.empty() || (cleaned.size() % 2) != 0) {
        return std::unexpected(ims::ErrorInfo{
            ims::ErrorCode::kSmsParseError,
            "invalid hex payload length",
        });
    }

    std::vector<uint8_t> out;
    out.reserve(cleaned.size() / 2);
    for (std::size_t i = 0; i < cleaned.size(); i += 2) {
        const auto hi = hex_value(cleaned[i]);
        const auto lo = hex_value(cleaned[i + 1]);
        if (!hi || !lo) {
            return std::unexpected(ims::ErrorInfo{
                ims::ErrorCode::kSmsParseError,
                std::format("invalid hex at offset {}", i),
            });
        }
        out.push_back(static_cast<uint8_t>((*hi << 4) | *lo));
    }
    return out;
}

auto encode_hex(std::span<const uint8_t> bytes) -> std::string {
    static constexpr char kHex[] = "0123456789abcdef";
    std::string out;
    out.reserve(bytes.size() * 2);
    for (auto byte : bytes) {
        out.push_back(kHex[(byte >> 4) & 0x0F]);
        out.push_back(kHex[byte & 0x0F]);
    }
    return out;
}

} // namespace ims::sms
