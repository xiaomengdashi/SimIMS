#pragma once

#include "address.hpp"
#include "core/types.hpp"

#include <cstdint>
#include <span>
#include <variant>
#include <vector>

namespace ims::sms {

enum class RpMessageType : uint8_t {
    kData = 0x01,
    kAck = 0x02,
    kError = 0x03,
    kSmma = 0x05,
};

/// RP-DATA (3GPP TS 24.011 7.3.1).
struct RpDataMessage {
    uint8_t message_reference = 0;
    std::optional<SmsAddress> destination;
    std::optional<SmsAddress> originator;
    std::vector<uint8_t> user_data;
};

/// RP-ACK (3GPP TS 24.011 7.3.4).
struct RpAckMessage {
    uint8_t message_reference = 0;
    std::optional<SmsAddress> originator;
    std::optional<SmsAddress> destination;
};

/// RP-ERROR (3GPP TS 24.011 7.3.5).
struct RpErrorMessage {
    uint8_t message_reference = 0;
    uint8_t cause = 0;
    std::optional<SmsAddress> originator;
    std::optional<SmsAddress> destination;
};

using RpMessage = std::variant<RpDataMessage, RpAckMessage, RpErrorMessage>;

auto rp_message_type(std::span<const uint8_t> payload) -> std::optional<RpMessageType>;
auto parse_rp_message(std::span<const uint8_t> payload) -> Result<RpMessage>;
auto encode_rp_message(const RpMessage& message) -> Result<std::vector<uint8_t>>;
auto build_rp_ack(uint8_t message_reference) -> std::vector<uint8_t>;
auto build_mt_rp_data_from_mo(const RpDataMessage& mo, const SmsAddress& originator) -> Result<RpDataMessage>;

} // namespace ims::sms
