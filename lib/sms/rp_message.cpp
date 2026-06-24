#include "rp_message.hpp"

#include "constants.hpp"
#include "tpdu.hpp"

#include <format>

namespace ims::sms {

namespace {

auto parse_optional_rp_address(std::span<const uint8_t> data, std::size_t& offset)
    -> Result<std::optional<SmsAddress>> {
    if (offset >= data.size()) {
        return std::optional<SmsAddress>{};
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

auto parse_rp_data(std::span<const uint8_t> data, uint8_t message_reference, RpDirection direction)
    -> Result<RpDataMessage> {
    std::size_t offset = 2;
    RpDataMessage message{.message_reference = message_reference};
    message.direction = direction;

    // 3GPP TS 24.011 7.3.1: RP-Originator Address precedes RP-Destination Address
    // in both directions. For MO (MS->Network) the originator is empty and the
    // destination is the SMSC; for MT (Network->MS) it is the reverse.
    auto originator = parse_optional_rp_address(data, offset);
    if (!originator) {
        return std::unexpected(originator.error());
    }
    message.originator = std::move(*originator);

    auto destination = parse_optional_rp_address(data, offset);
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

    if (offset != data.size()) {
        return std::unexpected(ims::ErrorInfo{
            ims::ErrorCode::kSmsParseError,
            "RP-DATA trailing bytes",
        });
    }
    return message;
}

auto parse_rp_ack(std::span<const uint8_t> data, uint8_t message_reference, RpDirection direction)
    -> Result<RpAckMessage> {
    std::size_t offset = 2;
    RpAckMessage message{.message_reference = message_reference};
    message.direction = direction;

    auto originator = parse_optional_rp_address(data, offset);
    if (!originator) {
        return std::unexpected(originator.error());
    }
    message.originator = std::move(*originator);

    auto destination = parse_optional_rp_address(data, offset);
    if (!destination) {
        return std::unexpected(destination.error());
    }
    message.destination = std::move(*destination);

    if (offset != data.size()) {
        return std::unexpected(ims::ErrorInfo{
            ims::ErrorCode::kSmsParseError,
            "RP-ACK trailing bytes",
        });
    }
    return message;
}

auto parse_rp_error(std::span<const uint8_t> data, uint8_t message_reference, RpDirection direction)
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
    message.direction = direction;

    auto originator = parse_optional_rp_address(data, offset);
    if (!originator) {
        return std::unexpected(originator.error());
    }
    message.originator = std::move(*originator);

    if (offset < data.size()) {
        auto destination = parse_optional_rp_address(data, offset);
        if (!destination) {
            return std::unexpected(destination.error());
        }
        message.destination = std::move(*destination);
    }

    if (offset != data.size()) {
        return std::unexpected(ims::ErrorInfo{
            ims::ErrorCode::kSmsParseError,
            "RP-ERROR trailing bytes",
        });
    }
    return message;
}

} // namespace

auto rp_message_type(std::span<const uint8_t> payload) -> std::optional<RpMessageType> {
    if (payload.empty()) {
        return std::nullopt;
    }
    switch (payload[0]) {
    case static_cast<uint8_t>(RpMessageType::kDataMs):
    case static_cast<uint8_t>(RpMessageType::kDataMn):
    case static_cast<uint8_t>(RpMessageType::kAckMs):
    case static_cast<uint8_t>(RpMessageType::kAckMn):
    case static_cast<uint8_t>(RpMessageType::kErrorMs):
    case static_cast<uint8_t>(RpMessageType::kErrorMn):
    case static_cast<uint8_t>(RpMessageType::kSmmaMs):
        return static_cast<RpMessageType>(payload[0]);
    default:
        return std::nullopt;
    }
}

auto rp_message_category(RpMessageType type) -> RpMessageCategory {
    switch (type) {
    case RpMessageType::kDataMs:
    case RpMessageType::kDataMn:
        return RpMessageCategory::kData;
    case RpMessageType::kAckMs:
    case RpMessageType::kAckMn:
        return RpMessageCategory::kAck;
    case RpMessageType::kErrorMs:
    case RpMessageType::kErrorMn:
        return RpMessageCategory::kError;
    case RpMessageType::kSmmaMs:
        return RpMessageCategory::kSmma;
    }
    return RpMessageCategory::kData;
}

auto rp_message_direction(RpMessageType type) -> RpDirection {
    switch (type) {
    case RpMessageType::kDataMn:
    case RpMessageType::kAckMn:
    case RpMessageType::kErrorMn:
        return RpDirection::kNetworkToMs;
    case RpMessageType::kDataMs:
    case RpMessageType::kAckMs:
    case RpMessageType::kErrorMs:
    case RpMessageType::kSmmaMs:
        return RpDirection::kMsToNetwork;
    }
    return RpDirection::kMsToNetwork;
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
    const auto direction = rp_message_direction(*type);
    switch (rp_message_category(*type)) {
    case RpMessageCategory::kData:
        return parse_rp_data(payload, message_reference, direction);
    case RpMessageCategory::kAck:
        return parse_rp_ack(payload, message_reference, direction);
    case RpMessageCategory::kError:
        return parse_rp_error(payload, message_reference, direction);
    case RpMessageCategory::kSmma:
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
        out.push_back(static_cast<uint8_t>(data->direction == RpDirection::kNetworkToMs
                                               ? RpMessageType::kDataMn
                                               : RpMessageType::kDataMs));
        out.push_back(data->message_reference);
        // 3GPP TS 24.011 7.3.1: originator address first, then destination address.
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
        out.push_back(static_cast<uint8_t>(ack->direction == RpDirection::kNetworkToMs
                                               ? RpMessageType::kAckMn
                                               : RpMessageType::kAckMs));
        out.push_back(ack->message_reference);
        if (ack->originator) {
            if (auto encoded = encode_rp_address(*ack->originator, out); !encoded) {
                return std::unexpected(encoded.error());
            }
        } else {
            out.push_back(0x00);
        }
        if (ack->destination) {
            if (auto encoded = encode_rp_address(*ack->destination, out); !encoded) {
                return std::unexpected(encoded.error());
            }
        } else {
            out.push_back(0x00);
        }
        return out;
    }

    if (const auto* error = std::get_if<RpErrorMessage>(&message)) {
        out.push_back(static_cast<uint8_t>(error->direction == RpDirection::kNetworkToMs
                                               ? RpMessageType::kErrorMn
                                               : RpMessageType::kErrorMs));
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
    // The network acknowledges an MO RP-DATA towards the originating UE, i.e. in
    // the Network -> MS direction (3GPP TS 24.011 8.2.2, MTI 0x03).
    return {
        static_cast<uint8_t>(RpMessageType::kAckMn),
        message_reference,
        0x00,
        0x00,
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
    // MT delivery is sent from the network towards the terminating UE.
    mt.direction = RpDirection::kNetworkToMs;
    return mt;
}

} // namespace ims::sms
