#pragma once

#include "diameter/types.hpp"
#include "common/types.hpp"

#include <string>
#include <vector>

namespace ims::scscf {

class AuthManager {
public:
    /// Build WWW-Authenticate header value from AKA auth vector
    static auto buildChallenge(const ims::diameter::AuthVector& av,
                               const std::string& realm) -> std::string;

    /// Verify client response against expected xres
    static auto verifyResponse(const std::string& auth_header,
                               const ims::diameter::AuthVector& av,
                               const std::string& method) -> bool;

    /// Parsed Authorization header fields
    struct AuthParams {
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

    /// Parse Authorization header to extract fields
    static auto parseAuthorization(const std::string& header) -> Result<AuthParams>;

private:
    static auto base64Encode(const std::vector<uint8_t>& data) -> std::string;
    static auto base64Decode(const std::string& encoded) -> std::vector<uint8_t>;
};

} // namespace ims::scscf
