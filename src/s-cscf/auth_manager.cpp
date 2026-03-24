#include "auth_manager.hpp"
#include "common/logger.hpp"

#include <algorithm>
#include <boost/uuid/detail/md5.hpp>
#include <cctype>
#include <format>
#include <iomanip>
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

auto toLower(std::string value) -> std::string {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return value;
}

auto bytesToHex(const std::vector<uint8_t>& data) -> std::string {
    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    for (auto byte : data) {
        oss << std::setw(2) << static_cast<unsigned int>(byte);
    }
    return oss.str();
}

auto md5Hex(const std::string& input) -> std::string {
    boost::uuids::detail::md5 hash;
    boost::uuids::detail::md5::digest_type digest{};

    hash.process_bytes(input.data(), input.size());
    hash.get_digest(digest);

    // boost::uuids::detail::md5 stores words in host uint32 representation.
    // Emit low byte first for each word to get standard MD5 hex output.
    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    for (auto word : digest) {
        auto b0 = static_cast<uint8_t>(word & 0xFF);
        auto b1 = static_cast<uint8_t>((word >> 8) & 0xFF);
        auto b2 = static_cast<uint8_t>((word >> 16) & 0xFF);
        auto b3 = static_cast<uint8_t>((word >> 24) & 0xFF);
        oss << std::setw(2) << static_cast<unsigned int>(b0)
            << std::setw(2) << static_cast<unsigned int>(b1)
            << std::setw(2) << static_cast<unsigned int>(b2)
            << std::setw(2) << static_cast<unsigned int>(b3);
    }
    return oss.str();
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
                                  const ims::diameter::AuthVector& av,
                                  const std::string& method) -> bool {
    auto params_result = parseAuthorization(auth_header);
    if (!params_result) {
        IMS_LOG_WARN("Failed to parse Authorization header: {}", params_result.error().message);
        return false;
    }

    const auto& params = *params_result;

    if (params.response.empty() || params.nonce.empty() || params.uri.empty()) {
        IMS_LOG_WARN("Authorization header missing required digest fields");
        return false;
    }

    // Nonce must match the challenge generated from RAND||AUTN.
    std::vector<uint8_t> nonce_data;
    nonce_data.reserve(av.rand.size() + av.autn.size());
    nonce_data.insert(nonce_data.end(), av.rand.begin(), av.rand.end());
    nonce_data.insert(nonce_data.end(), av.autn.begin(), av.autn.end());
    auto expected_nonce = base64Encode(nonce_data);
    if (params.nonce != expected_nonce) {
        IMS_LOG_WARN("Authorization nonce mismatch");
        return false;
    }

    // AKAv1-MD5 uses RES as digest password.
    auto xres_hex = bytesToHex(av.xres);
    auto ha1 = md5Hex(std::format("{}:{}:{}", params.username, params.realm, xres_hex));
    auto ha2 = md5Hex(std::format("{}:{}", method, params.uri));

    std::string expected_response;
    if (!params.qop.empty()) {
        if (params.nc.empty() || params.cnonce.empty()) {
            IMS_LOG_WARN("Authorization qop present but nc/cnonce missing");
            return false;
        }

        expected_response = md5Hex(std::format("{}:{}:{}:{}:{}:{}",
                                               ha1, params.nonce, params.nc,
                                               params.cnonce, params.qop, ha2));
    } else {
        expected_response = md5Hex(std::format("{}:{}:{}", ha1, params.nonce, ha2));
    }

    bool match = (toLower(params.response) == toLower(expected_response));
    IMS_LOG_DEBUG("AKA Digest verification: {}", match ? "success" : "failure");
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

        auto key = toLower(trim(token.substr(0, eq)));
        auto value = trimQuotes(trim(token.substr(eq + 1)));

        if (key == "username") params.username = value;
        else if (key == "realm") params.realm = value;
        else if (key == "nonce") params.nonce = value;
        else if (key == "response") params.response = value;
        else if (key == "uri") params.uri = value;
        else if (key == "algorithm") params.algorithm = value;
        else if (key == "qop") params.qop = value;
        else if (key == "nc") params.nc = value;
        else if (key == "cnonce") params.cnonce = value;
    }

    if (params.username.empty()) {
        return std::unexpected(ims::ErrorInfo{
            ims::ErrorCode::kSipParseError, "Missing username in Authorization header"});
    }

    return params;
}

} // namespace ims::scscf
