#pragma once

#include <string_view>

namespace ims::sms {

/// RP-DATA uplink (MO) example (24 bytes): RP-MTI 0x00 (MS->Network), empty
/// RP-Originator Address, RP-Destination Address (SMSC, octet-length encoding per
/// 3GPP TS 24.011 8.2.5), then the SMS-SUBMIT TPDU (3GPP TS 23.040).
/// TPDU first octet 0x21 sets TP-SRR (VPF=0) so MO RP-ACK is requested.
inline constexpr std::string_view kSampleRpDataHex =
    "00000007915121551236F80C21000B915121551236F80000";

/// Same payload with TP-SRR=0 (first TPDU octet 0x01): MO gets 202 + MT only.
inline constexpr std::string_view kSampleRpDataNoSrrHex =
    "00000007915121551236F80C01000B915121551236F80000";

/// Minimal RP-ACK (type=0x02 MS->Network, ref=0x00, empty originator/destination).
inline constexpr std::string_view kSampleRpAckHex = "020000";

} // namespace ims::sms
