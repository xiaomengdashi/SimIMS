#include "sms_validator.hpp"

#include "constants.hpp"
#include "rp_message.hpp"
#include "tpdu.hpp"

#include <algorithm>
#include <cctype>
#include <format>
#include <span>

namespace ims::sms {

namespace {

auto body_as_bytes(const std::string& body) -> std::span<const uint8_t> {
    return {reinterpret_cast<const uint8_t*>(body.data()), body.size()};
}

auto trim_copy(std::string_view value) -> std::string {
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front())) != 0) {
        value.remove_prefix(1);
    }
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back())) != 0) {
        value.remove_suffix(1);
    }
    return std::string(value);
}

auto equals_ignore_case(std::string_view lhs, std::string_view rhs) -> bool {
    if (lhs.size() != rhs.size()) {
        return false;
    }
    return std::equal(lhs.begin(), lhs.end(), rhs.begin(), rhs.end(),
                      [](char a, char b) {
                          return std::tolower(static_cast<unsigned char>(a))
                              == std::tolower(static_cast<unsigned char>(b));
                      });
}

auto validate_rp_data_tpdu(const RpDataMessage& data) -> VoidResult {
    if (data.user_data.empty()) {
        return std::unexpected(ims::ErrorInfo{
            ims::ErrorCode::kSmsInvalidPayload,
            "RP-DATA missing TPDU",
        });
    }
    if (auto tpdu = parse_tpdu(data.user_data); !tpdu) {
        return std::unexpected(tpdu.error());
    }
    return {};
}

} // namespace

auto is_3gpp_sms_content_type(std::string_view content_type) -> bool {
    const auto normalized = trim_copy(content_type);
    return equals_ignore_case(normalized, kContentType3gppSms);
}

auto validate_sip_message_body(std::string_view content_type, const std::string& body) -> VoidResult {
    const auto normalized = trim_copy(content_type);
    if (normalized.empty()) {
        return std::unexpected(ims::ErrorInfo{
            ims::ErrorCode::kSmsInvalidPayload,
            "MESSAGE missing Content-Type",
        });
    }

    if (equals_ignore_case(normalized, kContentTypeTextPlain)) {
        return std::unexpected(ims::ErrorInfo{
            ims::ErrorCode::kSmsInvalidPayload,
            "SMS over IMS requires application/vnd.3gpp.sms, not text/plain",
        });
    }

    if (!equals_ignore_case(normalized, kContentType3gppSms)) {
        return std::unexpected(ims::ErrorInfo{
            ims::ErrorCode::kSmsInvalidPayload,
            std::format("unsupported MESSAGE Content-Type: {}", normalized),
        });
    }

    if (body.empty()) {
        return std::unexpected(ims::ErrorInfo{
            ims::ErrorCode::kSmsInvalidPayload,
            "application/vnd.3gpp.sms body is empty",
        });
    }

    auto rp = parse_rp_message(body_as_bytes(body));
    if (!rp) {
        return std::unexpected(rp.error());
    }

    if (const auto* data = std::get_if<RpDataMessage>(&*rp)) {
        return validate_rp_data_tpdu(*data);
    }

    return {};
}

} // namespace ims::sms
