#pragma once

#include "common/types.hpp"
#include "transport.hpp"

#include <optional>
#include <string>

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
auto parse_endpoint_from_uri(const std::string& sip_uri) -> std::optional<Endpoint>;
auto parse_digest_authorization(const std::string& header) -> Result<DigestAuthFields>;

} // namespace ims::sip
