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
    auto address = parse_address(data, offset);
    if (!address) {
        offset = saved;
        return std::unexpected(address.error());
    }
    if (address->digit_length == 0) {
        return std::optional<SmsAddress>{};
    }
    return *address;
}

auto parse_rp_data(std::span<const uint8_t> data, uint8_t message_reference) -> Result<RpDataMessage> {
    std::size_t offset = 2;
    RpDataMessage message{.message_reference = message_reference};

    // MS->CN RP-DATA uses destination then originator (3GPP TS 24.011 / common UE encoding).
    auto destination = parse_optional_address(data, offset);
    if (!destination) {
        return std::unexpected(destination.error());
    }
    message.destination = std::move(*destination);

    auto originator = parse_optional_address(data, offset);
    if (!originator) {
        return std::unexpected(originator.error());
    }
    message.originator = std::move(*originator);

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

auto parse_rp_ack(std::span<const uint8_t> data, uint8_t message_reference) -> Result<RpAckMessage> {
    std::size_t offset = 2;
    RpAckMessage message{.message_reference = message_reference};

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

    if (offset != data.size()) {
        return std::unexpected(ims::ErrorInfo{
            ims::ErrorCode::kSmsParseError,
            "RP-ACK trailing bytes",
        });
    }
    return message;
}

auto parse_rp_error(std::span<const uint8_t> data, uint8_t message_reference) -> Result<RpErrorMessage> {
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

    auto originator = parse_optional_address(data, offset);
    if (!originator) {
        return std::unexpected(originator.error());
    }
    message.originator = std::move(*originator);

    if (offset < data.size()) {
        auto destination = parse_optional_address(data, offset);
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
    case static_cast<uint8_t>(RpMessageType::kData):
    case static_cast<uint8_t>(RpMessageType::kAck):
    case static_cast<uint8_t>(RpMessageType::kError):
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
    case RpMessageType::kData:
        return parse_rp_data(payload, message_reference);
    case RpMessageType::kAck:
        return parse_rp_ack(payload, message_reference);
    case RpMessageType::kError:
        return parse_rp_error(payload, message_reference);
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
        out.push_back(static_cast<uint8_t>(RpMessageType::kData));
        out.push_back(data->message_reference);
        if (data->destination) {
            if (auto encoded = data->destination->encode(out); !encoded) {
                return std::unexpected(encoded.error());
            }
        } else {
            out.push_back(0x00);
        }
        if (data->originator) {
            if (auto encoded = data->originator->encode(out); !encoded) {
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
        out.push_back(static_cast<uint8_t>(RpMessageType::kAck));
        out.push_back(ack->message_reference);
        if (ack->originator) {
            if (auto encoded = ack->originator->encode(out); !encoded) {
                return std::unexpected(encoded.error());
            }
        } else {
            out.push_back(0x00);
        }
        if (ack->destination) {
            if (auto encoded = ack->destination->encode(out); !encoded) {
                return std::unexpected(encoded.error());
            }
        } else {
            out.push_back(0x00);
        }
        return out;
    }

    if (const auto* error = std::get_if<RpErrorMessage>(&message)) {
        out.push_back(static_cast<uint8_t>(RpMessageType::kError));
        out.push_back(error->message_reference);
        out.push_back(error->cause);
        if (error->originator) {
            if (auto encoded = error->originator->encode(out); !encoded) {
                return std::unexpected(encoded.error());
            }
        }
        if (error->destination) {
            if (auto encoded = error->destination->encode(out); !encoded) {
                return std::unexpected(encoded.error());
            }
        }
        return out;
    }

    return std::unexpected(ims::ErrorInfo{ims::ErrorCode::kInternalError, "unknown RP message variant"});
}

auto build_rp_ack(uint8_t message_reference) -> std::vector<uint8_t> {
    return {
        static_cast<uint8_t>(RpMessageType::kAck),
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
    return mt;
}

} // namespace ims::sms
