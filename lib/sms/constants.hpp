#pragma once

#include <string_view>

namespace ims::sms {

inline constexpr std::string_view kContentType3gppSms = "application/vnd.3gpp.sms";
inline constexpr std::string_view kContentType3gpp2Sms = "application/vnd.3gpp2.sms";
inline constexpr std::string_view kContentTypeTextPlain = "text/plain";

inline constexpr std::size_t kMaxRpUserDataLength = 140;
inline constexpr std::size_t kMaxTpduLength = 140;

} // namespace ims::sms
