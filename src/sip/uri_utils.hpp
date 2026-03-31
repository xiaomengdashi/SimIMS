#pragma once

#include "common/types.hpp"
#include "transport.hpp"

#include <optional>
#include <string>
#include <string_view>

namespace ims::sip {

struct DigestAuthFields {
    std::string username;
    std::string realm;
    std::string nonce;
    std::string response;
    std::string uri;
    std::string algorithm;
    std::string qop;
    std::string nc;
    std::string cnonce;
};

auto extract_uri_from_name_addr(const std::string& value) -> std::string;
auto normalize_impu_uri(const std::string& value) -> std::string;
auto parse_endpoint_from_uri(const std::string& sip_uri) -> std::optional<Endpoint>;
auto route_points_to_endpoint(const std::string& route_value,
                              const Endpoint& endpoint,
                              std::string_view fallback_transport = "udp") -> bool;
auto parse_digest_authorization(const std::string& header) -> Result<DigestAuthFields>;

} // namespace ims::sip
