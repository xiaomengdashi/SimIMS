#include "tpdu.hpp"

#include "address.hpp"
#include "constants.hpp"

#include <algorithm>
#include <format>

namespace ims::sms {

namespace {

constexpr auto kMtiMask = 0x03U;

/// Number of octets occupied by the TP-User-Data field for a given TP-UDL.
/// For the GSM 7-bit default alphabet TP-UDL counts septets, so the packed field
/// occupies ceil(udl * 7 / 8) octets; for 8-bit and UCS2 it already counts octets
/// (3GPP TS 23.040 §9.2.3.16). Unknown coding groups fall back to octet counting.
auto tp_user_data_octets(uint8_t dcs, uint8_t udl) -> std::size_t {
    if (auto alphabet = classify_dcs(dcs);
        alphabet && *alphabet == UserDataAlphabet::kGsm7BitDefault) {
        return (static_cast<std::size_t>(udl) * 7U + 7U) / 8U;
    }
    return udl;
}

auto tpdu_type_from_mti(uint8_t mti) -> std::optional<TpduType> {
    switch (mti) {
    case 0x00:
        return TpduType::kDeliver;
    case 0x01:
        return TpduType::kSubmit;
    case 0x02:
        return TpduType::kStatusReport;
    case 0x03:
        return TpduType::kCommand;
    default:
        return std::nullopt;
    }
}

auto skip_address(std::span<const uint8_t> tpdu, std::size_t& offset) -> VoidResult {
    if (offset >= tpdu.size()) {
        return std::unexpected(ims::ErrorInfo{ims::ErrorCode::kSmsParseError, "TPDU address truncated"});
    }
    const auto digit_length = tpdu[offset++];
    if (digit_length == 0) {
        return {};
    }
    const auto bcd_bytes = (digit_length + 1) / 2;
    if (offset + 1 + bcd_bytes > tpdu.size()) {
        return std::unexpected(ims::ErrorInfo{ims::ErrorCode::kSmsParseError, "TPDU address truncated"});
    }
    offset += 1 + bcd_bytes;
    return {};
}

auto validate_submit(std::span<const uint8_t> tpdu) -> VoidResult {
    if (tpdu.size() < 4) {
        return std::unexpected(ims::ErrorInfo{ims::ErrorCode::kSmsParseError, "SMS-SUBMIT too short"});
    }

    std::size_t offset = 0;
    const auto first_octet = tpdu[offset++];
    const auto vpf = (first_octet >> 3) & 0x03U;
    offset += 1; // MR

    if (auto address = skip_address(tpdu, offset); !address) {
        return address;
    }
    if (offset + 2 > tpdu.size()) {
        return std::unexpected(ims::ErrorInfo{ims::ErrorCode::kSmsParseError, "SMS-SUBMIT missing PID/DCS"});
    }
    const auto dcs = tpdu[offset + 1];
    offset += 2; // PID + DCS

    switch (vpf) {
    case 0x00:
        break;
    case 0x02:
        if (offset >= tpdu.size()) {
            return std::unexpected(ims::ErrorInfo{ims::ErrorCode::kSmsParseError, "SMS-SUBMIT missing VP"});
        }
        offset += 1;
        break;
    case 0x01:
    case 0x03:
        if (offset + 7 > tpdu.size()) {
            return std::unexpected(ims::ErrorInfo{ims::ErrorCode::kSmsParseError, "SMS-SUBMIT missing VP"});
        }
        offset += 7;
        break;
    default:
        break;
    }

    if (offset >= tpdu.size()) {
        return {};
    }
    const auto udl = tpdu[offset++];
    if (offset + tp_user_data_octets(dcs, udl) > tpdu.size()) {
        return std::unexpected(ims::ErrorInfo{
            ims::ErrorCode::kSmsParseError,
            "SMS-SUBMIT user data truncated",
        });
    }
    return {};
}

auto validate_deliver(std::span<const uint8_t> tpdu) -> VoidResult {
    if (tpdu.size() < 3) {
        return std::unexpected(ims::ErrorInfo{ims::ErrorCode::kSmsParseError, "SMS-DELIVER too short"});
    }

    std::size_t offset = 1; // skip first octet
    if (auto address = skip_address(tpdu, offset); !address) {
        return address;
    }
    if (offset + 2 > tpdu.size()) {
        return std::unexpected(ims::ErrorInfo{ims::ErrorCode::kSmsParseError, "SMS-DELIVER missing PID/DCS"});
    }
    const auto dcs = tpdu[offset + 1];
    offset += 2;

    if (offset + 7 > tpdu.size()) {
        return std::unexpected(ims::ErrorInfo{ims::ErrorCode::kSmsParseError, "SMS-DELIVER missing SCTS"});
    }
    offset += 7;

    if (offset >= tpdu.size()) {
        return {};
    }
    const auto udl = tpdu[offset++];
    if (offset + tp_user_data_octets(dcs, udl) > tpdu.size()) {
        return std::unexpected(ims::ErrorInfo{
            ims::ErrorCode::kSmsParseError,
            "SMS-DELIVER user data truncated",
        });
    }
    return {};
}

} // namespace

auto parse_tpdu(std::span<const uint8_t> tpdu) -> Result<TpduInfo> {
    if (tpdu.empty()) {
        return std::unexpected(ims::ErrorInfo{ims::ErrorCode::kSmsParseError, "empty TPDU"});
    }
    if (tpdu.size() > kMaxTpduLength) {
        return std::unexpected(ims::ErrorInfo{
            ims::ErrorCode::kSmsInvalidPayload,
            std::format("TPDU length {} exceeds maximum", tpdu.size()),
        });
    }

    const auto first_octet = tpdu[0];
    const auto mti = first_octet & kMtiMask;
    const auto type = tpdu_type_from_mti(static_cast<uint8_t>(mti));
    if (!type) {
        return std::unexpected(ims::ErrorInfo{
            ims::ErrorCode::kSmsInvalidPayload,
            std::format("unsupported TPDU MTI {}", mti),
        });
    }

    return TpduInfo{
        .type = *type,
        .first_octet = first_octet,
        .length = tpdu.size(),
    };
}

auto validate_tpdu(std::span<const uint8_t> tpdu) -> VoidResult {
    auto info = parse_tpdu(tpdu);
    if (!info) {
        return std::unexpected(info.error());
    }

    switch (info->type) {
    case TpduType::kSubmit:
        return validate_submit(tpdu);
    case TpduType::kDeliver:
        return validate_deliver(tpdu);
    case TpduType::kStatusReport:
    case TpduType::kCommand:
        if (tpdu.size() < 2) {
            return std::unexpected(ims::ErrorInfo{ims::ErrorCode::kSmsParseError, "TPDU report too short"});
        }
        return {};
    }
    return std::unexpected(ims::ErrorInfo{ims::ErrorCode::kInternalError, "unreachable TPDU validator"});
}

auto submit_requests_status_report(uint8_t submit_first_octet) -> bool {
    return (submit_first_octet & 0x20U) != 0;
}

auto parse_submit_tpdu(std::span<const uint8_t> tpdu) -> Result<SubmitTpduView> {
    auto info = parse_tpdu(tpdu);
    if (!info) {
        return std::unexpected(info.error());
    }
    if (info->type != TpduType::kSubmit) {
        return std::unexpected(ims::ErrorInfo{
            ims::ErrorCode::kSmsInvalidPayload,
            "TPDU is not SMS-SUBMIT",
        });
    }
    if (auto valid = validate_submit(tpdu); !valid) {
        return std::unexpected(valid.error());
    }

    SubmitTpduView fields{.first_octet = info->first_octet};
    std::size_t offset = 1;
    fields.message_reference = tpdu[offset++];

    auto destination = parse_address(tpdu, offset);
    if (!destination) {
        return std::unexpected(destination.error());
    }
    fields.destination = std::move(*destination);

    if (offset + 2 > tpdu.size()) {
        return std::unexpected(ims::ErrorInfo{
            ims::ErrorCode::kSmsParseError,
            "SMS-SUBMIT missing PID/DCS",
        });
    }
    fields.pid = tpdu[offset++];
    fields.dcs = tpdu[offset++];

    const auto vpf = (fields.first_octet >> 3) & 0x03U;
    switch (vpf) {
    case 0x00:
        break;
    case 0x02:
        if (offset >= tpdu.size()) {
            return std::unexpected(ims::ErrorInfo{
                ims::ErrorCode::kSmsParseError,
                "SMS-SUBMIT missing VP",
            });
        }
        offset += 1;
        break;
    case 0x01:
    case 0x03:
        if (offset + 7 > tpdu.size()) {
            return std::unexpected(ims::ErrorInfo{
                ims::ErrorCode::kSmsParseError,
                "SMS-SUBMIT missing VP",
            });
        }
        offset += 7;
        break;
    default:
        break;
    }

    if (offset >= tpdu.size()) {
        return fields;
    }

    const auto udl = tpdu[offset++];
    const auto ud_octets = tp_user_data_octets(fields.dcs, udl);
    if (offset + ud_octets > tpdu.size()) {
        return std::unexpected(ims::ErrorInfo{
            ims::ErrorCode::kSmsParseError,
            "SMS-SUBMIT user data truncated",
        });
    }
    fields.user_data_length = udl;
    fields.user_data.assign(tpdu.begin() + static_cast<std::ptrdiff_t>(offset),
                            tpdu.begin() + static_cast<std::ptrdiff_t>(offset + ud_octets));
    return fields;
}

auto parse_deliver_tpdu(std::span<const uint8_t> tpdu) -> Result<DeliverTpduView> {
    auto info = parse_tpdu(tpdu);
    if (!info) {
        return std::unexpected(info.error());
    }
    if (info->type != TpduType::kDeliver) {
        return std::unexpected(ims::ErrorInfo{
            ims::ErrorCode::kSmsInvalidPayload,
            "TPDU is not SMS-DELIVER",
        });
    }
    if (auto valid = validate_deliver(tpdu); !valid) {
        return std::unexpected(valid.error());
    }

    DeliverTpduView fields{.first_octet = info->first_octet};
    std::size_t offset = 1;

    auto originator = parse_address(tpdu, offset);
    if (!originator) {
        return std::unexpected(originator.error());
    }
    fields.originator = std::move(*originator);

    if (offset + 2 > tpdu.size()) {
        return std::unexpected(ims::ErrorInfo{
            ims::ErrorCode::kSmsParseError,
            "SMS-DELIVER missing PID/DCS",
        });
    }
    fields.pid = tpdu[offset++];
    fields.dcs = tpdu[offset++];

    if (offset + fields.service_centre_time_stamp.size() > tpdu.size()) {
        return std::unexpected(ims::ErrorInfo{
            ims::ErrorCode::kSmsParseError,
            "SMS-DELIVER missing SCTS",
        });
    }
    std::copy_n(tpdu.begin() + static_cast<std::ptrdiff_t>(offset),
                fields.service_centre_time_stamp.size(),
                fields.service_centre_time_stamp.begin());
    offset += fields.service_centre_time_stamp.size();

    if (offset >= tpdu.size()) {
        return fields;
    }

    const auto udl = tpdu[offset++];
    const auto ud_octets = tp_user_data_octets(fields.dcs, udl);
    if (offset + ud_octets > tpdu.size()) {
        return std::unexpected(ims::ErrorInfo{
            ims::ErrorCode::kSmsParseError,
            "SMS-DELIVER user data truncated",
        });
    }
    fields.user_data_length = udl;
    fields.user_data.assign(tpdu.begin() + static_cast<std::ptrdiff_t>(offset),
                            tpdu.begin() + static_cast<std::ptrdiff_t>(offset + ud_octets));
    return fields;
}

auto submit_has_user_data_header(uint8_t submit_first_octet) -> bool {
    return (submit_first_octet & 0x40U) != 0U;
}

auto decode_submit_user_data(const SubmitTpduView& submit) -> Result<DecodedUserData> {
    return decode_user_data(submit.dcs,
                            submit.user_data,
                            submit.user_data_length,
                            submit_has_user_data_header(submit.first_octet));
}

auto deliver_has_user_data_header(uint8_t deliver_first_octet) -> bool {
    return (deliver_first_octet & 0x40U) != 0U;
}

auto deliver_has_more_messages(uint8_t deliver_first_octet) -> bool {
    return (deliver_first_octet & 0x04U) == 0U;
}

auto decode_deliver_user_data(const DeliverTpduView& deliver) -> Result<DecodedUserData> {
    return decode_user_data(deliver.dcs,
                            deliver.user_data,
                            deliver.user_data_length,
                            deliver_has_user_data_header(deliver.first_octet));
}

auto encode_deliver_tpdu(uint8_t deliver_first_octet,
                         const SmsAddress& originator,
                         uint8_t pid,
                         uint8_t dcs,
                         std::span<const uint8_t> user_data,
                         std::size_t user_data_length) -> Result<std::vector<uint8_t>> {
    if (user_data.size() > 255) {
        return std::unexpected(ims::ErrorInfo{
            ims::ErrorCode::kSmsInvalidPayload,
            "SMS-DELIVER user data exceeds 255 octets",
        });
    }

    // TP-UDL keeps the source encoding's unit (septets for GSM 7-bit, octets
    // otherwise); when not supplied, fall back to the octet count.
    const auto udl = user_data_length == 0U ? user_data.size() : user_data_length;
    if (udl > 255) {
        return std::unexpected(ims::ErrorInfo{
            ims::ErrorCode::kSmsInvalidPayload,
            "SMS-DELIVER TP-UDL exceeds 255",
        });
    }

    std::vector<uint8_t> out;
    out.reserve(16 + user_data.size());
    out.push_back(deliver_first_octet);
    if (auto encoded = originator.encode(out); !encoded) {
        return std::unexpected(encoded.error());
    }
    out.push_back(pid);
    out.push_back(dcs);
    out.insert(out.end(), 7, 0x00); // SCTS placeholder
    out.push_back(static_cast<uint8_t>(udl));
    out.insert(out.end(), user_data.begin(), user_data.end());

    if (auto valid = validate_deliver(out); !valid) {
        return std::unexpected(valid.error());
    }
    return out;
}

auto submit_to_deliver_tpdu(std::span<const uint8_t> submit_tpdu,
                            const SmsAddress& originator) -> Result<std::vector<uint8_t>> {
    auto submit = parse_submit_tpdu(submit_tpdu);
    if (!submit) {
        return std::unexpected(submit.error());
    }

    const auto deliver_first =
        static_cast<uint8_t>((submit->first_octet & 0xC0U) | 0x04U); // MTI=DELIVER, MMS=1
    return encode_deliver_tpdu(deliver_first,
                               originator,
                               submit->pid,
                               submit->dcs,
                               submit->user_data,
                               submit->user_data_length);
}

} // namespace ims::sms
