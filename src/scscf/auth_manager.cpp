#include "auth_manager.hpp"
#include "ims/common/logger.hpp"

#include <algorithm>
#include <format>
#include <sstream>

namespace ims::scscf {

namespace {

constexpr char kBase64Chars[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

auto trimQuotes(const std::string& s) -> std::string {
    if (s.size() >= 2 && s.front() == '"' && s.back() == '"') {
        return s.substr(1, s.size() - 2);
    }
    return s;
}

auto trim(const std::string& s) -> std::string {
    auto start = s.find_first_not_of(" \t");
    auto end = s.find_last_not_of(" \t");
    if (start == std::string::npos) return {};
    return s.substr(start, end - start + 1);
}

} // anonymous namespace

auto AuthManager::base64Encode(const std::vector<uint8_t>& data) -> std::string {
    std::string result;
    result.reserve(((data.size() + 2) / 3) * 4);

    for (size_t i = 0; i < data.size(); i += 3) {
        uint32_t n = static_cast<uint32_t>(data[i]) << 16;
        if (i + 1 < data.size()) n |= static_cast<uint32_t>(data[i + 1]) << 8;
        if (i + 2 < data.size()) n |= static_cast<uint32_t>(data[i + 2]);

        result += kBase64Chars[(n >> 18) & 0x3F];
        result += kBase64Chars[(n >> 12) & 0x3F];
        result += (i + 1 < data.size()) ? kBase64Chars[(n >> 6) & 0x3F] : '=';
        result += (i + 2 < data.size()) ? kBase64Chars[n & 0x3F] : '=';
    }

    return result;
}

auto AuthManager::base64Decode(const std::string& encoded) -> std::vector<uint8_t> {
    std::vector<uint8_t> result;
    result.reserve((encoded.size() / 4) * 3);

    auto val = [](char c) -> uint8_t {
        if (c >= 'A' && c <= 'Z') return c - 'A';
        if (c >= 'a' && c <= 'z') return c - 'a' + 26;
        if (c >= '0' && c <= '9') return c - '0' + 52;
        if (c == '+') return 62;
        if (c == '/') return 63;
        return 0;
    };

    for (size_t i = 0; i < encoded.size(); i += 4) {
        if (i + 3 >= encoded.size()) break;
        uint32_t n = (val(encoded[i]) << 18) | (val(encoded[i + 1]) << 12)
                   | (val(encoded[i + 2]) << 6) | val(encoded[i + 3]);

        result.push_back((n >> 16) & 0xFF);
        if (encoded[i + 2] != '=') result.push_back((n >> 8) & 0xFF);
        if (encoded[i + 3] != '=') result.push_back(n & 0xFF);
    }

    return result;
}

auto AuthManager::buildChallenge(const ims::diameter::AuthVector& av,
                                  const std::string& realm) -> std::string {
    // IMS AKA: nonce = base64(RAND || AUTN || server-specific-data)
    // Concatenate RAND (16 bytes) + AUTN (16 bytes) for AKA nonce
    std::vector<uint8_t> nonce_data;
    nonce_data.reserve(av.rand.size() + av.autn.size());
    nonce_data.insert(nonce_data.end(), av.rand.begin(), av.rand.end());
    nonce_data.insert(nonce_data.end(), av.autn.begin(), av.autn.end());

    auto nonce = base64Encode(nonce_data);

    // Build WWW-Authenticate header for Digest-AKAv1-MD5
    auto challenge = std::format(
        "Digest realm=\"{}\", nonce=\"{}\", algorithm=AKAv1-MD5, qop=\"auth\"",
        realm, nonce);

    IMS_LOG_DEBUG("Built AKA challenge, realm={}, nonce_len={}", realm, nonce.size());
    return challenge;
}

auto AuthManager::verifyResponse(const std::string& auth_header,
                                  const ims::diameter::AuthVector& av) -> bool {
    auto params_result = parseAuthorization(auth_header);
    if (!params_result) {
        IMS_LOG_WARN("Failed to parse Authorization header: {}", params_result.error().message);
        return false;
    }

    const auto& params = *params_result;

    // In IMS AKA, the client response contains XRES (or a hash of it).
    // For simplified verification: decode client response and compare with XRES.
    auto client_response = base64Decode(params.response);

    // Compare client response with expected XRES
    // In a full implementation, we'd compute the Digest hash using HA1 derived from AKA.
    // For now, compare decoded response bytes with XRES directly.
    if (client_response.size() != av.xres.size()) {
        IMS_LOG_DEBUG("Response size mismatch: got {} expected {}", 
            client_response.size(), av.xres.size());
        return false;
    }

    bool match = std::equal(client_response.begin(), client_response.end(), av.xres.begin());
    IMS_LOG_DEBUG("AKA response verification: {}", match ? "success" : "failure");
    return match;
}

auto AuthManager::parseAuthorization(const std::string& header) -> Result<AuthParams> {
    // Expected format: Digest username="...", realm="...", nonce="...", ...
    AuthParams params;

    // Skip "Digest " prefix
    auto pos = header.find("Digest ");
    if (pos == std::string::npos) {
        pos = header.find("digest ");
    }
    if (pos == std::string::npos) {
        return std::unexpected(ims::ErrorInfo{
            ims::ErrorCode::kSipParseError, "Missing Digest scheme in Authorization header"});
    }

    std::string fields_str = header.substr(pos + 7);
    
    // Parse key=value pairs
    std::istringstream stream(fields_str);
    std::string token;
    while (std::getline(stream, token, ',')) {
        token = trim(token);
        auto eq = token.find('=');
        if (eq == std::string::npos) continue;

        auto key = trim(token.substr(0, eq));
        auto value = trimQuotes(trim(token.substr(eq + 1)));

        if (key == "username") params.username = value;
        else if (key == "realm") params.realm = value;
        else if (key == "nonce") params.nonce = value;
        else if (key == "response") params.response = value;
        else if (key == "uri") params.uri = value;
        else if (key == "algorithm") params.algorithm = value;
    }

    if (params.username.empty()) {
        return std::unexpected(ims::ErrorInfo{
            ims::ErrorCode::kSipParseError, "Missing username in Authorization header"});
    }

    return params;
}

} // namespace ims::scscf
