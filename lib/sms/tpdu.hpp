#pragma once

#include "address.hpp"
#include "common/types.hpp"

#include "user_data.hpp"

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace ims::sms {

enum class TpduType : uint8_t {
    kDeliver = 0x00,
    kSubmit = 0x01,
    kStatusReport = 0x02,
    kCommand = 0x03,
};

struct TpduInfo {
    TpduType type = TpduType::kDeliver;
    uint8_t first_octet = 0;
    std::size_t length = 0;
};

struct SubmitTpduView {
    uint8_t first_octet = 0;
    uint8_t message_reference = 0;
    SmsAddress destination;
    uint8_t pid = 0;
    uint8_t dcs = 0;
    uint8_t user_data_length = 0;
    std::vector<uint8_t> user_data;
};

/// Service Centre Time Stamp (3GPP TS 23.040 §9.1.2.6), stored as raw 7 octets.
using ServiceCentreTimeStamp = std::array<uint8_t, 7>;

struct DeliverTpduView {
    uint8_t first_octet = 0;
    SmsAddress originator;
    uint8_t pid = 0;
    uint8_t dcs = 0;
    ServiceCentreTimeStamp service_centre_time_stamp{};
    uint8_t user_data_length = 0;
    std::vector<uint8_t> user_data;
};

auto parse_tpdu(std::span<const uint8_t> tpdu) -> Result<TpduInfo>;
auto validate_tpdu(std::span<const uint8_t> tpdu) -> VoidResult;
auto parse_submit_tpdu(std::span<const uint8_t> tpdu) -> Result<SubmitTpduView>;
auto parse_deliver_tpdu(std::span<const uint8_t> tpdu) -> Result<DeliverTpduView>;
auto encode_deliver_tpdu(uint8_t deliver_first_octet,
                         const SmsAddress& originator,
                         uint8_t pid,
                         uint8_t dcs,
                         std::span<const uint8_t> user_data) -> Result<std::vector<uint8_t>>;
auto submit_to_deliver_tpdu(std::span<const uint8_t> submit_tpdu,
                            const SmsAddress& originator) -> Result<std::vector<uint8_t>>;

auto submit_has_user_data_header(uint8_t submit_first_octet) -> bool;

auto decode_submit_user_data(const SubmitTpduView& submit) -> Result<DecodedUserData>;

auto deliver_has_user_data_header(uint8_t deliver_first_octet) -> bool;

auto deliver_has_more_messages(uint8_t deliver_first_octet) -> bool;

auto decode_deliver_user_data(const DeliverTpduView& deliver) -> Result<DecodedUserData>;

/// TP-SRR bit in SMS-SUBMIT first octet (3GPP TS 23.040).
auto submit_requests_status_report(uint8_t submit_first_octet) -> bool;

} // namespace ims::sms
