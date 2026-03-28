#include "auth_manager.hpp"
#include "common/logger.hpp"
#include "sip/uri_utils.hpp"

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

    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    auto* bytes = reinterpret_cast<const unsigned char*>(digest);
    for (std::size_t i = 0; i < sizeof(digest); ++i) {
        oss << std::setw(2) << static_cast<unsigned int>(bytes[i]);
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

auto AuthManager::buildDigestChallenge(const std::string& realm,
                                       const std::string& nonce) -> std::string {
    return std::format(
        "Digest realm=\"{}\", domain=\"sip:{}\", nonce=\"{}\", algorithm=MD5, qop=\"auth\", stale=FALSE",
        realm, realm, nonce);
}

auto AuthManager::buildChallenge(const ims::diameter::AuthVector& av,
                                  const std::string& realm,
                                  const std::string& scheme) -> std::string {
    if (scheme == "Digest-MD5") {
        auto nonce = base64Encode(av.rand);
        auto challenge = buildDigestChallenge(realm, nonce);
        IMS_LOG_DEBUG("Built MD5 challenge, realm={}, nonce_len={}", realm, nonce.size());
        return challenge;
    }

    // IMS AKA: nonce = base64(RAND || AUTN || server-specific-data)
    // Concatenate RAND (16 bytes) + AUTN (16 bytes) for AKA nonce
    std::vector<uint8_t> nonce_data;
    nonce_data.reserve(av.rand.size() + av.autn.size());
    nonce_data.insert(nonce_data.end(), av.rand.begin(), av.rand.end());
    nonce_data.insert(nonce_data.end(), av.autn.begin(), av.autn.end());

    auto nonce = base64Encode(nonce_data);

    auto challenge = std::format(
        "Digest realm=\"{}\", nonce=\"{}\", algorithm=AKAv1-MD5, qop=\"auth\"",
        realm, nonce);

    IMS_LOG_DEBUG("Built AKA challenge, realm={}, nonce_len={}", realm, nonce.size());
    return challenge;
}

auto AuthManager::verifyResponse(const std::string& auth_header,
                                  const ims::diameter::AuthVector& av,
                                  const std::string& method,
                                  const std::string& scheme) -> bool {
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

    std::string expected_nonce;
    std::string secret;
    if (scheme == "Digest-MD5") {
        expected_nonce = base64Encode(av.rand);
        secret.assign(av.xres.begin(), av.xres.end());
    } else {
        std::vector<uint8_t> nonce_data;
        nonce_data.reserve(av.rand.size() + av.autn.size());
        nonce_data.insert(nonce_data.end(), av.rand.begin(), av.rand.end());
        nonce_data.insert(nonce_data.end(), av.autn.begin(), av.autn.end());
        expected_nonce = base64Encode(nonce_data);
        secret = bytesToHex(av.xres);
    }

    if (params.nonce != expected_nonce) {
        IMS_LOG_WARN("Authorization nonce mismatch");
        return false;
    }

    auto ha1 = md5Hex(std::format("{}:{}:{}", params.username, params.realm, secret));
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

auto AuthManager::verifyDigestPassword(const std::string& auth_header,
                                       const std::string& password,
                                       const std::string& method,
                                       const std::string& expected_nonce) -> bool {
    auto params_result = parseAuthorization(auth_header);
    if (!params_result) {
        IMS_LOG_WARN("Failed to parse Authorization header: {}", params_result.error().message);
        return false;
    }

    const auto& params = *params_result;
    if (params.username.empty() || params.realm.empty() || params.uri.empty() || params.response.empty()) {
        IMS_LOG_WARN("Authorization header missing required digest password fields");
        return false;
    }

    if (params.nonce != expected_nonce) {
        IMS_LOG_WARN("Digest password nonce mismatch");
        return false;
    }

    auto ha1 = md5Hex(std::format("{}:{}:{}", params.username, params.realm, password));
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

    return toLower(params.response) == toLower(expected_response);
}

auto AuthManager::parseAuthorization(const std::string& header) -> Result<AuthParams> {
    auto parsed = ims::sip::parse_digest_authorization(header);
    if (!parsed) {
        return std::unexpected(parsed.error());
    }

    AuthParams params;
    params.username = parsed->username;
    params.realm = parsed->realm;
    params.nonce = parsed->nonce;
    params.response = parsed->response;
    params.uri = parsed->uri;
    params.algorithm = parsed->algorithm;
    params.qop = parsed->qop;
    params.nc = parsed->nc;
    params.cnonce = parsed->cnonce;
    return params;
}

} // namespace ims::scscf
