#include "user_data.hpp"

#include <array>
#include <format>

namespace ims::sms {

namespace {

constexpr auto kGeneralDataCodingMask = 0xC0U;
constexpr auto kGeneralDataCodingGroup = 0x00U;
constexpr auto kAlphabetMask = 0x0CU;
constexpr auto kCompressedFlag = 0x20U;

// 3GPP TS 23.038 default alphabet (GSM 7-bit), index 0..127.
constexpr std::array<char32_t, 128> kGsmDefaultAlphabet = {
    u'@', u'£', u'$', u'¥', u'è', u'é', u'ù', u'ì', u'ò', u'Ç', u'\n', u'Ø', u'ø', u'\r', u'Å', u'å',
    u'Δ', u'_', u'Φ', u'Γ', u'Λ', u'Ω', u'Π', u'Ψ', u'Σ', u'Θ', u'Ξ', u'\x1B', u'Æ', u'æ', u'ß', u'É',
    u' ', u'!', u'"', u'#', u'¤', u'%', u'&', u'\'', u'(', u')', u'*', u'+', u',', u'-', u'.', u'/',
    u'0', u'1', u'2', u'3', u'4', u'5', u'6', u'7', u'8', u'9', u':', u';', u'<', u'=', u'>', u'?',
    u'¡', u'A', u'B', u'C', u'D', u'E', u'F', u'G', u'H', u'I', u'J', u'K', u'L', u'M', u'N', u'O',
    u'P', u'Q', u'R', u'S', u'T', u'U', u'V', u'W', u'X', u'Y', u'Z', u'Ä', u'Ö', u'Ñ', u'Ü', u'§',
    u'¿', u'a', u'b', u'c', u'd', u'e', u'f', u'g', u'h', u'i', u'j', u'k', u'l', u'm', u'n', u'o',
    u'p', u'q', u'r', u's', u't', u'u', u'v', u'w', u'x', u'y', u'z', u'ä', u'ö', u'ñ', u'ü', u'à',
};

auto append_utf8(char32_t codepoint, std::string& out) -> VoidResult {
    if (codepoint <= 0x7FU) {
        out.push_back(static_cast<char>(codepoint));
        return {};
    }
    if (codepoint <= 0x7FFU) {
        out.push_back(static_cast<char>(0xC0U | ((codepoint >> 6U) & 0x1FU)));
        out.push_back(static_cast<char>(0x80U | (codepoint & 0x3FU)));
        return {};
    }
    if (codepoint <= 0xFFFFU) {
        out.push_back(static_cast<char>(0xE0U | ((codepoint >> 12U) & 0x0FU)));
        out.push_back(static_cast<char>(0x80U | ((codepoint >> 6U) & 0x3FU)));
        out.push_back(static_cast<char>(0x80U | (codepoint & 0x3FU)));
        return {};
    }
    return std::unexpected(ims::ErrorInfo{
        ims::ErrorCode::kSmsParseError,
        std::format("unsupported Unicode scalar 0x{:04X}", static_cast<uint32_t>(codepoint)),
    });
}

auto utf16be_to_utf8(std::span<const uint8_t> data) -> Result<std::string> {
    if ((data.size() % 2U) != 0U) {
        return std::unexpected(ims::ErrorInfo{
            ims::ErrorCode::kSmsParseError,
            "UCS2 user data length is not even",
        });
    }

    std::string out;
    out.reserve(data.size());
    for (std::size_t offset = 0; offset + 1 < data.size(); offset += 2) {
        const auto codepoint = static_cast<char32_t>((static_cast<uint16_t>(data[offset]) << 8U)
                                                     | static_cast<uint16_t>(data[offset + 1]));
        if (auto appended = append_utf8(codepoint, out); !appended) {
            return std::unexpected(appended.error());
        }
    }
    return out;
}

auto unpack_gsm_septets(std::span<const uint8_t> packed, std::size_t septet_count)
    -> Result<std::vector<uint8_t>> {
    std::vector<uint8_t> out;
    out.reserve(septet_count);

    for (std::size_t index = 0; index < septet_count; ++index) {
        const auto bit_offset = index * 7U;
        const auto byte_index = bit_offset / 8U;
        const auto shift = bit_offset % 8U;

        if (byte_index >= packed.size()) {
            return std::unexpected(ims::ErrorInfo{
                ims::ErrorCode::kSmsParseError,
                "GSM 7-bit user data truncated",
            });
        }

        auto septet = static_cast<uint8_t>((packed[byte_index] >> shift) & 0x7FU);
        if (shift > 1U && byte_index + 1 < packed.size()) {
            septet = static_cast<uint8_t>(
                septet | ((packed[byte_index + 1] << (8U - shift)) & 0x7FU));
        }
        out.push_back(septet);
    }
    return out;
}

auto gsm_septets_to_text(std::span<const uint8_t> septets) -> Result<std::string> {
    std::string out;
    out.reserve(septets.size());

    for (const auto value : septets) {
        if (value >= kGsmDefaultAlphabet.size()) {
            return std::unexpected(ims::ErrorInfo{
                ims::ErrorCode::kSmsParseError,
                std::format("unsupported GSM septet value 0x{:02X}", value),
            });
        }

        if (value == 0x1BU) {
            return std::unexpected(ims::ErrorInfo{
                ims::ErrorCode::kNotImplemented,
                "GSM 7-bit extension table is not supported",
            });
        }

        if (auto appended = append_utf8(kGsmDefaultAlphabet[value], out); !appended) {
            return std::unexpected(appended.error());
        }
    }
    return out;
}

auto strip_user_data_header(std::span<const uint8_t> user_data, bool user_data_header_present)
    -> Result<std::span<const uint8_t>> {
    if (!user_data_header_present) {
        return user_data;
    }
    if (user_data.empty()) {
        return std::unexpected(ims::ErrorInfo{
            ims::ErrorCode::kSmsParseError,
            "user data header missing UDHL",
        });
    }

    const auto header_length = user_data[0];
    const auto total_header_bytes = static_cast<std::size_t>(header_length) + 1U;
    if (total_header_bytes > user_data.size()) {
        return std::unexpected(ims::ErrorInfo{
            ims::ErrorCode::kSmsParseError,
            "user data header truncated",
        });
    }
    return user_data.subspan(total_header_bytes);
}

auto septets_for_user_data_header(std::size_t header_bytes) -> std::size_t {
    return (header_bytes * 8U + 6U) / 7U;
}

auto decode_gsm7_user_data(std::span<const uint8_t> user_data,
                           std::size_t user_data_length,
                           bool user_data_header_present) -> Result<DecodedUserData> {
    if (user_data_length == 0U) {
        return DecodedUserData{
            .alphabet = UserDataAlphabet::kGsm7BitDefault,
            .text = {},
            .raw = {},
        };
    }

    std::size_t text_septets = user_data_length;
    if (user_data_header_present) {
        if (user_data.empty()) {
            return std::unexpected(ims::ErrorInfo{
                ims::ErrorCode::kSmsParseError,
                "GSM 7-bit user data header missing UDHL",
            });
        }
        const auto header_bytes = static_cast<std::size_t>(user_data[0]) + 1U;
        const auto header_septets = septets_for_user_data_header(header_bytes);
        if (header_septets > user_data_length) {
            return std::unexpected(ims::ErrorInfo{
                ims::ErrorCode::kSmsParseError,
                "GSM 7-bit user data header exceeds UDL",
            });
        }
        text_septets = user_data_length - header_septets;
    }

    auto unpacked = unpack_gsm_septets(user_data, user_data_length);
    if (!unpacked) {
        return std::unexpected(unpacked.error());
    }

    std::span<const uint8_t> text_septets_view = *unpacked;
    if (user_data_header_present) {
        const auto header_bytes = static_cast<std::size_t>(user_data[0]) + 1U;
        const auto header_septets = septets_for_user_data_header(header_bytes);
        if (header_septets > unpacked->size()) {
            return std::unexpected(ims::ErrorInfo{
                ims::ErrorCode::kSmsParseError,
                "GSM 7-bit user data header exceeds unpacked length",
            });
        }
        text_septets_view = std::span<const uint8_t>(unpacked->data() + header_septets, text_septets);
    } else if (text_septets < unpacked->size()) {
        text_septets_view = std::span<const uint8_t>(unpacked->data(), text_septets);
    }

    auto text = gsm_septets_to_text(text_septets_view);
    if (!text) {
        return std::unexpected(text.error());
    }

    return DecodedUserData{
        .alphabet = UserDataAlphabet::kGsm7BitDefault,
        .text = std::move(*text),
        .raw = std::move(*unpacked),
    };
}

auto decode_octet_user_data(UserDataAlphabet alphabet,
                            std::span<const uint8_t> payload) -> Result<DecodedUserData> {
    if (alphabet == UserDataAlphabet::kUcs2) {
        auto text = utf16be_to_utf8(payload);
        if (!text) {
            return std::unexpected(text.error());
        }
        return DecodedUserData{
            .alphabet = UserDataAlphabet::kUcs2,
            .text = std::move(*text),
            .raw = {payload.begin(), payload.end()},
        };
    }

    std::string text;
    text.reserve(payload.size());
    for (const auto byte : payload) {
        if (auto appended = append_utf8(static_cast<char32_t>(byte), text); !appended) {
            return std::unexpected(appended.error());
        }
    }

    return DecodedUserData{
        .alphabet = UserDataAlphabet::kEightBit,
        .text = std::move(text),
        .raw = {payload.begin(), payload.end()},
    };
}

} // namespace

auto classify_dcs(uint8_t dcs) -> Result<UserDataAlphabet> {
    if ((dcs & kGeneralDataCodingMask) != kGeneralDataCodingGroup) {
        return std::unexpected(ims::ErrorInfo{
            ims::ErrorCode::kNotImplemented,
            std::format("unsupported TP-DCS coding group 0x{:02X}", dcs),
        });
    }
    if ((dcs & kCompressedFlag) != 0U) {
        return std::unexpected(ims::ErrorInfo{
            ims::ErrorCode::kNotImplemented,
            "compressed SMS user data is not supported",
        });
    }

    switch ((dcs & kAlphabetMask) >> 2U) {
    case 0U:
        return UserDataAlphabet::kGsm7BitDefault;
    case 1U:
        return UserDataAlphabet::kEightBit;
    case 2U:
        return UserDataAlphabet::kUcs2;
    default:
        return std::unexpected(ims::ErrorInfo{
            ims::ErrorCode::kNotImplemented,
            std::format("unsupported TP-DCS alphabet in 0x{:02X}", dcs),
        });
    }
}

auto decode_user_data(uint8_t dcs,
                      std::span<const uint8_t> user_data,
                      std::size_t user_data_length,
                      bool user_data_header_present) -> Result<DecodedUserData> {
    auto alphabet = classify_dcs(dcs);
    if (!alphabet) {
        return std::unexpected(alphabet.error());
    }

    if (*alphabet == UserDataAlphabet::kGsm7BitDefault) {
        return decode_gsm7_user_data(user_data, user_data_length, user_data_header_present);
    }

    auto payload = strip_user_data_header(user_data, user_data_header_present);
    if (!payload) {
        return std::unexpected(payload.error());
    }

    const auto expected_length = user_data_length == 0U ? payload->size() : user_data_length;
    if (expected_length > payload->size()) {
        return std::unexpected(ims::ErrorInfo{
            ims::ErrorCode::kSmsParseError,
            "user data shorter than TP-UDL",
        });
    }

    return decode_octet_user_data(*alphabet, payload->subspan(0, expected_length));
}

} // namespace ims::sms
