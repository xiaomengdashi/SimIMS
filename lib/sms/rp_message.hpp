#pragma once

#include "address.hpp"
#include "core/types.hpp"

#include <cstdint>
#include <span>
#include <variant>
#include <vector>

namespace ims::sms {

/// RP message type indicator (3GPP TS 24.011 8.2.2, Table 8.3).
/// The MTI encodes both the message category and the transfer direction, so the
/// same logical message uses a different value depending on the originator:
///   - MS -> Network (uplink, mobile originated)
///   - Network -> MS (downlink, mobile terminated)
enum class RpMessageType : uint8_t {
    kDataMs = 0x00,   ///< RP-DATA  MS -> Network (carries SMS-SUBMIT)
    kDataMn = 0x01,   ///< RP-DATA  Network -> MS (carries SMS-DELIVER)
    kAckMs = 0x02,    ///< RP-ACK   MS -> Network
    kAckMn = 0x03,    ///< RP-ACK   Network -> MS
    kErrorMs = 0x04,  ///< RP-ERROR MS -> Network
    kErrorMn = 0x05,  ///< RP-ERROR Network -> MS
    kSmmaMs = 0x06,   ///< RP-SMMA  MS -> Network
};

/// Logical RP message category, independent of the transfer direction.
enum class RpMessageCategory : uint8_t { kData, kAck, kError, kSmma };

/// Transfer direction carried by the RP message type indicator.
enum class RpDirection : uint8_t {
    kMsToNetwork,  ///< uplink, mobile originated
    kNetworkToMs,  ///< downlink, mobile terminated
};

/// RP-DATA (3GPP TS 24.011 7.3.1).
struct RpDataMessage {
    uint8_t message_reference = 0;
    std::optional<SmsAddress> destination;
    std::optional<SmsAddress> originator;
    std::vector<uint8_t> user_data;
    RpDirection direction = RpDirection::kMsToNetwork;
};

/// RP-ACK (3GPP TS 24.011 7.3.4).
struct RpAckMessage {
    uint8_t message_reference = 0;
    std::optional<SmsAddress> originator;
    std::optional<SmsAddress> destination;
    RpDirection direction = RpDirection::kNetworkToMs;
};

/// RP-ERROR (3GPP TS 24.011 7.3.5).
struct RpErrorMessage {
    uint8_t message_reference = 0;
    uint8_t cause = 0;
    std::optional<SmsAddress> originator;
    std::optional<SmsAddress> destination;
    RpDirection direction = RpDirection::kNetworkToMs;
};

using RpMessage = std::variant<RpDataMessage, RpAckMessage, RpErrorMessage>;

auto rp_message_type(std::span<const uint8_t> payload) -> std::optional<RpMessageType>;
auto rp_message_category(RpMessageType type) -> RpMessageCategory;
auto rp_message_direction(RpMessageType type) -> RpDirection;
auto parse_rp_message(std::span<const uint8_t> payload) -> Result<RpMessage>;
auto encode_rp_message(const RpMessage& message) -> Result<std::vector<uint8_t>>;
auto build_rp_ack(uint8_t message_reference) -> std::vector<uint8_t>;
auto build_mt_rp_data_from_mo(const RpDataMessage& mo, const SmsAddress& originator) -> Result<RpDataMessage>;

} // namespace ims::sms
