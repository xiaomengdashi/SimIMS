#pragma once

#include "diameter/types.hpp"
#include "common/types.hpp"

#include <string>
#include <vector>

namespace ims::scscf {

class AuthManager {
public:
    /// Build a plain Digest-MD5 challenge from server-generated nonce
    static auto buildDigestChallenge(const std::string& realm,
                                     const std::string& nonce) -> std::string;

    /// Build WWW-Authenticate header value from AKA auth vector
    static auto buildChallenge(const ims::diameter::AuthVector& av,
                               const std::string& realm,
                               const std::string& scheme) -> std::string;

    /// Verify client response against expected xres/shared secret
    static auto verifyResponse(const std::string& auth_header,
                               const ims::diameter::AuthVector& av,
                               const std::string& method,
                               const std::string& scheme) -> bool;

    static auto verifyDigestPassword(const std::string& auth_header,
                                     const std::string& password,
                                     const std::string& method,
                                     const std::string& expected_nonce) -> bool;

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
