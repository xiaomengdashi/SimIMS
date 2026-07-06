#include "rp_message.hpp"

#include "constants.hpp"
#include "tpdu.hpp"

#include <format>

namespace ims::sms {

namespace {

auto parse_optional_address(std::span<const uint8_t> data, std::size_t& offset)
    -> Result<std::optional<SmsAddress>> {
    if (offset >= data.size()) {
        return std::nullopt;
    }

    const auto saved = offset;
    auto address = parse_rp_address(data, offset);
    if (!address) {
        offset = saved;
        return std::unexpected(address.error());
    }
    if (address->digit_length == 0) {
        return std::optional<SmsAddress>{};
    }
    return *address;
}

/// /// 解析 RP-DATA。TS 24.011 Table 7.4/7.5 中两个方向的字段顺序都是
///   Originator Address → Destination Address → User Data
/// 只是哪个 address 字段 length=0 不同：
///   - MO (MS→network): Originator length=0，Destination 是 SC
///   - MT (network→MS): Originator 是 SC，Destination length=0
auto parse_rp_data(std::span<const uint8_t> data, uint8_t message_reference) -> Result<RpDataMessage> {
    std::size_t offset = 2;
    RpDataMessage message{.message_reference = message_reference};

    auto originator = parse_optional_address(data, offset);
    if (!originator) {
        return std::unexpected(originator.error());
    }
    message.originator = std::move(*originator);

    auto destination = parse_optional_address(data, offset);
    if (!destination) {
        return std::unexpected(destination.error());
    }
    message.destination = std::move(*destination);

    if (offset >= data.size()) {
        return std::unexpected(ims::ErrorInfo{
            ims::ErrorCode::kSmsParseError,
            "RP-DATA missing user data length",
        });
    }

    const auto user_data_length = data[offset++];
    if (user_data_length > kMaxRpUserDataLength) {
        return std::unexpected(ims::ErrorInfo{
            ims::ErrorCode::kSmsInvalidPayload,
            std::format("RP user data length {} exceeds maximum", user_data_length),
        });
    }
    if (offset + user_data_length > data.size()) {
        return std::unexpected(ims::ErrorInfo{
            ims::ErrorCode::kSmsParseError,
            "RP-DATA user data truncated",
        });
    }

    message.user_data.assign(data.begin() + static_cast<std::ptrdiff_t>(offset),
                           data.begin() + static_cast<std::ptrdiff_t>(offset + user_data_length));
    offset += user_data_length;

    return message;
}

/// 解析 RP-ACK。TS 24.011 Table 7.4/7.5 中两个方向的字段顺序都是
///   Originator Address → Destination Address
/// 只是哪个 address 字段 length=0 不同：
///   - MO (MS→network): Originator length=0，Destination 是 SC
///   - MT (network→MS): Originator 是 SC，Destination length=0
auto parse_rp_ack(std::span<const uint8_t> data, RpMessageType dir, uint8_t message_reference)
    -> Result<RpAckMessage> {
    std::size_t offset = 2;
    RpAckMessage message{.message_reference = message_reference};

    if (dir == RpMessageType::kMtAck) {
        auto originator = parse_optional_address(data, offset);
        if (!originator) {
            return std::unexpected(originator.error());
        }
        message.originator = std::move(*originator);
    }

    return message;
}

/// 解析 RP-ERROR。TS 24.011 Table 7.4/7.5 中两个方向的字段顺序都是
///   Originator Address → Destination Address → Cause
/// 只是哪个 address 字段 length=0 不同：
///   - MO (MS→network): Originator length=0，Destination 是 SC
///   - MT (network→MS): Originator 是 SC，Destination length=0
auto parse_rp_error(std::span<const uint8_t> data, RpMessageType dir, uint8_t message_reference)
    -> Result<RpErrorMessage> {
    if (data.size() < 3) {
        return std::unexpected(ims::ErrorInfo{
            ims::ErrorCode::kSmsParseError,
            "RP-ERROR too short",
        });
    }

    std::size_t offset = 2;
    RpErrorMessage message{
        .message_reference = message_reference,
        .cause = data[offset++],
    };

    if (dir == RpMessageType::kMoError) {
        auto originator = parse_optional_address(data, offset);
        if (!originator) {
            return std::unexpected(originator.error());
        }
        message.originator = std::move(*originator);
    } else {
        auto destination = parse_optional_address(data, offset);
        if (!destination) {
            return std::unexpected(destination.error());
        }
        message.destination = std::move(*destination);
    }

    return message;
}

auto infer_data_direction(const RpDataMessage& data) -> RpMessageType {
    if (data.originator && data.originator->digit_length > 0) {
        return RpMessageType::kMoData;
    }
    return RpMessageType::kMtData;
}

} // namespace

auto rp_message_type(std::span<const uint8_t> payload) -> std::optional<RpMessageType> {
    if (payload.empty()) {
        return std::nullopt;
    }
    switch (payload[0]) {
    case static_cast<uint8_t>(RpMessageType::kMoData):
    case static_cast<uint8_t>(RpMessageType::kMtData):
    case static_cast<uint8_t>(RpMessageType::kMoAck):
    case static_cast<uint8_t>(RpMessageType::kMtAck):
    case static_cast<uint8_t>(RpMessageType::kMtError):
    case static_cast<uint8_t>(RpMessageType::kMoError):
    case static_cast<uint8_t>(RpMessageType::kSmma):
        return static_cast<RpMessageType>(payload[0]);
    default:
        return std::nullopt;
    }
}

auto parse_rp_message(std::span<const uint8_t> payload) -> Result<RpMessage> {
    if (payload.size() < 2) {
        return std::unexpected(ims::ErrorInfo{
            ims::ErrorCode::kSmsParseError,
            "RP message too short",
        });
    }

    const auto type = rp_message_type(payload);
    if (!type) {
        return std::unexpected(ims::ErrorInfo{
            ims::ErrorCode::kSmsInvalidPayload,
            std::format("unsupported RP message type 0x{:02X}", payload[0]),
        });
    }

    const auto message_reference = payload[1];
    switch (*type) {
    case RpMessageType::kMoData:
    case RpMessageType::kMtData:
        return parse_rp_data(payload, message_reference);
    case RpMessageType::kMoAck:
    case RpMessageType::kMtAck:
        return parse_rp_ack(payload, *type, message_reference);
    case RpMessageType::kMtError:
    case RpMessageType::kMoError:
        return parse_rp_error(payload, *type, message_reference);
    case RpMessageType::kSmma:
        return std::unexpected(ims::ErrorInfo{
            ims::ErrorCode::kSmsInvalidPayload,
            "RP-SMMA is not supported",
        });
    }
    return std::unexpected(ims::ErrorInfo{ims::ErrorCode::kInternalError, "unreachable RP parser"});
}

auto encode_rp_message(const RpMessage& message) -> Result<std::vector<uint8_t>> {
    std::vector<uint8_t> out;
    out.reserve(32);

    if (const auto* data = std::get_if<RpDataMessage>(&message)) {
        const auto dir = infer_data_direction(*data);
        out.push_back(static_cast<uint8_t>(dir));
        out.push_back(data->message_reference);
        if (data->originator) {
            if (auto encoded = encode_rp_address(*data->originator, out); !encoded) {
                return std::unexpected(encoded.error());
            }
        } else {
            out.push_back(0x00);
        }
        if (data->destination) {
            if (auto encoded = encode_rp_address(*data->destination, out); !encoded) {
                return std::unexpected(encoded.error());
            }
        } else {
            out.push_back(0x00);
        }
        if (data->user_data.size() > kMaxRpUserDataLength) {
            return std::unexpected(ims::ErrorInfo{
                ims::ErrorCode::kSmsInvalidPayload,
                "RP user data exceeds maximum length",
            });
        }
        out.push_back(static_cast<uint8_t>(data->user_data.size()));
        out.insert(out.end(), data->user_data.begin(), data->user_data.end());
        return out;
    }

    if (const auto* ack = std::get_if<RpAckMessage>(&message)) {
        const auto has_originator = ack->originator && ack->originator->digit_length > 0;
        out.push_back(static_cast<uint8_t>(has_originator ? RpMessageType::kMoAck : RpMessageType::kMtAck));
        out.push_back(ack->message_reference);
        if (ack->originator) {
            if (auto encoded = encode_rp_address(*ack->originator, out); !encoded) {
                return std::unexpected(encoded.error());
            }
        }
        return out;
    }

    if (const auto* error = std::get_if<RpErrorMessage>(&message)) {
        const auto dir = (error->destination && error->destination->digit_length > 0) ? RpMessageType::kMtError : RpMessageType::kMoError;
        out.push_back(static_cast<uint8_t>(dir));
        out.push_back(error->message_reference);
        out.push_back(error->cause);
        if (error->originator) {
            if (auto encoded = encode_rp_address(*error->originator, out); !encoded) {
                return std::unexpected(encoded.error());
            }
        }
        if (error->destination) {
            if (auto encoded = encode_rp_address(*error->destination, out); !encoded) {
                return std::unexpected(encoded.error());
            }
        }
        return out;
    }

    return std::unexpected(ims::ErrorInfo{ims::ErrorCode::kInternalError, "unknown RP message variant"});
}

auto build_rp_ack(uint8_t message_reference) -> std::vector<uint8_t> {
    return {
        static_cast<uint8_t>(RpMessageType::kMtAck),
        message_reference,
    };
}

auto build_mt_rp_data_from_mo(const RpDataMessage& mo, const SmsAddress& originator)
    -> Result<RpDataMessage> {
    auto deliver_tpdu = submit_to_deliver_tpdu(mo.user_data, originator);
    if (!deliver_tpdu) {
        return std::unexpected(deliver_tpdu.error());
    }

    RpDataMessage mt{
        .message_reference = mo.message_reference,
        .destination = std::nullopt,
        .originator = originator,
        .user_data = std::move(*deliver_tpdu),
    };
    return mt;
}

} // namespace ims::sms