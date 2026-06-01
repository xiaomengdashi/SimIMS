#pragma once

#include <string_view>

namespace ims::sms {

/// RP-DATA uplink example (24 bytes): RP header + destination + TPDU (3GPP TS 24.011 / 23.040).
/// TPDU first octet 0x21 sets TP-SRR (VPF=0) so MO RP-ACK is requested.
inline constexpr std::string_view kSampleRpDataHex =
    "01000B915121551236F8000C21000B915121551236F80000";

/// Same payload with TP-SRR=0 (first TPDU octet 0x01): MO gets 202 + MT only.
inline constexpr std::string_view kSampleRpDataNoSrrHex =
    "01000B915121551236F8000C01000B915121551236F80000";

/// Minimal RP-ACK (type=0x02, ref=0x00, empty originator/destination).
inline constexpr std::string_view kSampleRpAckHex = "020000";

} // namespace ims::sms
