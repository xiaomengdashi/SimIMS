#pragma once

#include "core/types.hpp"

#include <span>
#include <string>
#include <string_view>

namespace ims::sms {

/// Validate SIP MESSAGE payload for SMS over IMS (3GPP TS 24.341 / 24.011 / 23.040).
auto validate_sip_message_body(std::string_view content_type, const std::string& body) -> VoidResult;

auto is_3gpp_sms_content_type(std::string_view content_type) -> bool;

} // namespace ims::sms
