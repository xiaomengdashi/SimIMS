#pragma once

#include "sms/hex_utils.hpp"
#include "sms/samples.hpp"

#include <boost/uuid/detail/md5.hpp>
#include <format>
#include <iomanip>
#include <sstream>
#include <string>

namespace ims::integration {

inline constexpr auto kDomain = "ims.mnc011.mcc460.3gppnetwork.org";
inline constexpr auto kCallerImpu = "sip:460112024122023@ims.mnc011.mcc460.3gppnetwork.org";
inline constexpr auto kCalleeImpu = "sip:460112024122024@ims.mnc011.mcc460.3gppnetwork.org";
inline constexpr auto kCallerImpi = "460112024122023@ims.mnc011.mcc460.3gppnetwork.org";
inline constexpr auto kCalleeImpi = "460112024122024@ims.mnc011.mcc460.3gppnetwork.org";
inline constexpr auto kPassword = "testpass";

inline constexpr ims::Port kPcscfPort = 5060;
inline constexpr ims::Port kIcscfPort = 5061;
inline constexpr ims::Port kScscfPort = 5062;
inline constexpr ims::Port kSmscPort = 5063;
inline constexpr ims::Port kCallerUePort = 5090;
inline constexpr ims::Port kCalleeUePort = 5091;

inline constexpr auto kSmscPsi = "sip:smsc@ims.mnc011.mcc460.3gppnetwork.org";

inline auto md5_hex(const std::string& input) -> std::string {
    boost::uuids::detail::md5 hash;
    boost::uuids::detail::md5::digest_type digest{};

    hash.process_bytes(input.data(), input.size());
    hash.get_digest(digest);

    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    for (auto word : digest) {
        for (int shift = 24; shift >= 0; shift -= 8) {
            oss << std::setw(2) << ((word >> shift) & 0xFF);
        }
    }
    return oss.str();
}

inline auto digest_response(const std::string& username,
                            const std::string& realm,
                            const std::string& password,
                            const std::string& method,
                            const std::string& uri,
                            const std::string& nonce) -> std::string {
    constexpr auto k_nc = "00000001";
    constexpr auto k_cnonce = "simims-integration";
    constexpr auto k_qop = "auth";
    const auto ha1 = md5_hex(username + ":" + realm + ":" + password);
    const auto ha2 = md5_hex(method + ":" + uri);
    return md5_hex(ha1 + ":" + nonce + ":" + k_nc + ":" + k_cnonce + ":" + k_qop + ":" + ha2);
}

inline auto imsi_from_impu(const std::string& impu) -> std::string {
    const auto start = impu.find(':') + 1;
    const auto end = impu.find('@');
    return impu.substr(start, end - start);
}

inline auto register_challenge_raw(const std::string& impu,
                                   const std::string& call_id,
                                   ims::Port ue_port,
                                   uint32_t cseq) -> std::string {
    const auto register_uri = std::format("sip:{}", kDomain);
    const auto contact = std::format("<sip:{}@127.0.0.1:{}>", imsi_from_impu(impu), ue_port);
    return std::format(
        "REGISTER {} SIP/2.0\r\n"
        "Via: SIP/2.0/UDP 127.0.0.1:{};branch=z9hG4bK-{}-{}\r\n"
        "From: <{}>;tag=reg-from\r\n"
        "To: <{}>\r\n"
        "Call-ID: {}\r\n"
        "CSeq: {} REGISTER\r\n"
        "Contact: {}\r\n"
        "Expires: 3600\r\n"
        "Content-Length: 0\r\n\r\n",
        register_uri,
        ue_port,
        call_id,
        cseq,
        impu,
        impu,
        call_id,
        cseq,
        contact);
}

inline auto register_authorized_raw(const std::string& impi,
                                    const std::string& impu,
                                    const std::string& call_id,
                                    ims::Port ue_port,
                                    uint32_t cseq,
                                    const std::string& nonce) -> std::string {
    const auto register_uri = std::format("sip:{}", kDomain);
    const auto contact = std::format("<sip:{}@127.0.0.1:{}>", impi.substr(0, impi.find('@')), ue_port);
    const auto response = digest_response(impi, kDomain, kPassword, "REGISTER", register_uri, nonce);
    return std::format(
        "REGISTER {} SIP/2.0\r\n"
        "Via: SIP/2.0/UDP 127.0.0.1:{};branch=z9hG4bK-{}-{}\r\n"
        "From: <{}>;tag=reg-from\r\n"
        "To: <{}>\r\n"
        "Call-ID: {}\r\n"
        "CSeq: {} REGISTER\r\n"
        "Contact: {}\r\n"
        "Expires: 3600\r\n"
        "Authorization: Digest username=\"{}\", realm=\"{}\", nonce=\"{}\", uri=\"{}\", "
        "response=\"{}\", algorithm=MD5, qop=auth, nc=00000001, cnonce=\"simims-integration\"\r\n"
        "Content-Length: 0\r\n\r\n",
        register_uri,
        ue_port,
        call_id,
        cseq,
        impu,
        impu,
        call_id,
        cseq,
        contact,
        impi,
        kDomain,
        nonce,
        register_uri,
        response);
}

inline auto invite_raw(const std::string& caller_impu,
                       const std::string& callee_impu,
                       const std::string& call_id,
                       ims::Port caller_port) -> std::string {
    return std::format(
        "INVITE {} SIP/2.0\r\n"
        "Via: SIP/2.0/UDP 127.0.0.1:{};branch=z9hG4bK-invite-{}\r\n"
        "From: <{}>;tag=caller-tag\r\n"
        "To: <{}>\r\n"
        "Call-ID: {}\r\n"
        "CSeq: 1 INVITE\r\n"
        "Max-Forwards: 70\r\n"
        "Contact: <sip:{}@127.0.0.1:{}>\r\n"
        "Content-Type: application/sdp\r\n"
        "Content-Length: 0\r\n\r\n",
        callee_impu,
        caller_port,
        call_id,
        caller_impu,
        callee_impu,
        call_id,
        imsi_from_impu(caller_impu),
        caller_port);
}

inline auto message_raw(const std::string& caller_impu,
                        const std::string& callee_impu,
                        const std::string& call_id,
                        ims::Port caller_port,
                        std::string_view body_hex = ims::sms::kSampleRpDataHex) -> std::string {
    auto body_bytes = ims::sms::decode_hex(body_hex);
    if (!body_bytes) {
        return {};
    }
    const std::string body(body_bytes->begin(), body_bytes->end());
    const auto headers = std::format(
        "MESSAGE {} SIP/2.0\r\n"
        "Via: SIP/2.0/UDP 127.0.0.1:{};branch=z9hG4bK-sms-{}\r\n"
        "From: <{}>;tag=sms-from\r\n"
        "To: <{}>\r\n"
        "Call-ID: {}\r\n"
        "CSeq: 1 MESSAGE\r\n"
        "Max-Forwards: 70\r\n"
        "Contact: <sip:{}@127.0.0.1:{}>\r\n"
        "Content-Type: application/vnd.3gpp.sms\r\n"
        "Content-Length: {}\r\n\r\n",
        callee_impu,
        caller_port,
        call_id,
        caller_impu,
        callee_impu,
        call_id,
        imsi_from_impu(caller_impu),
        caller_port,
        body.size());
    return headers + body;
}

inline auto message_rp_ack_raw(const std::string& caller_impu,
                               const std::string& call_id,
                               ims::Port ue_port,
                               uint32_t cseq) -> std::string {
    auto body_bytes = ims::sms::decode_hex(ims::sms::kSampleRpAckHex);
    if (!body_bytes) {
        return {};
    }
    const std::string body(body_bytes->begin(), body_bytes->end());
    const auto headers = std::format(
        "MESSAGE {} SIP/2.0\r\n"
        "Via: SIP/2.0/UDP 127.0.0.1:{};branch=z9hG4bK-rpack-{}-{}\r\n"
        "From: <{}>;tag=rpack-from\r\n"
        "To: <{}>\r\n"
        "Call-ID: {}\r\n"
        "CSeq: {} MESSAGE\r\n"
        "Max-Forwards: 70\r\n"
        "Contact: <sip:{}@127.0.0.1:{}>\r\n"
        "Content-Type: application/vnd.3gpp.sms\r\n"
        "Content-Length: {}\r\n\r\n",
        kSmscPsi,
        ue_port,
        call_id,
        cseq,
        caller_impu,
        kSmscPsi,
        call_id,
        cseq,
        imsi_from_impu(caller_impu),
        ue_port,
        body.size());
    return headers + body;
}

} // namespace ims::integration
