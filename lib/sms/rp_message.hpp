#pragma once

#include "address.hpp"
#include "core/types.hpp"

#include <cstdint>
#include <span>
#include <variant>
#include <vector>

namespace ims::sms {

/// RP Message Type Indicator (3GPP TS 24.011 Table 8.3).
///
/// 低 3 位 bit 1..3 同时编码"消息大类"和"方向"：
/// - 低位为 0 → MS→network（UE 上行 / MO）
/// - 低位为 1 → network→MS（网络下发 / MT）
///
/// 因此所有 type 值都是成对出现的：MO 在前，MT 在后。
enum class RpMessageType : uint8_t {
    // MS -> network direction（P-CSCF / SMSC 在接收 UE 报文时只会看到这 4 个值
    kMoData  = 0x00,  ///< RP-DATA (MS to network)   —— UE 发起 MO SMS
    kMoAck   = 0x02,  ///< RP-ACK  (MS to network)   —— UE 确认收到 MT SMS
    kMoError = 0x04,  ///< RP-ERROR (MS to network)
    kSmma    = 0x06,  ///< RP-SMMA (MS to network)
    // network -> MS direction（SMSC 在向 UE 下发报文时使用这 3 个值
    kMtData  = 0x01,  ///< RP-DATA (network to MS)   —— SC 下发 MT SMS
    kMtAck   = 0x03,  ///< RP-ACK  (network to MS)   —— SC 确认收到 MO SMS
    kMtError = 0x05,  ///< RP-ERROR (network to MS)
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
