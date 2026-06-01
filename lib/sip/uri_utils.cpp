#include "uri_utils.hpp"

#include <algorithm>
#include <cctype>
#include <format>
#include <sstream>
#include <string_view>

#include <osipparser2/osip_parser.h>
#include <osipparser2/osip_message.h>
#include <osipparser2/osip_uri.h>

namespace ims::sip {
namespace {

auto trim(std::string value) -> std::string {
    auto is_space = [](unsigned char ch) { return std::isspace(ch) != 0; };
    value.erase(value.begin(), std::find_if(value.begin(), value.end(),
                                            [&](unsigned char ch) { return !is_space(ch); }));
    value.erase(std::find_if(value.rbegin(), value.rend(),
                             [&](unsigned char ch) { return !is_space(ch); }).base(),
                value.end());
    return value;
}

auto trim_quotes(std::string value) -> std::string {
    if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
        return value.substr(1, value.size() - 2);
    }
    return value;
}

auto lowercase(std::string value) -> std::string {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

auto uri_to_string(osip_uri_t* uri) -> std::string {
    if (!uri) return {};
    char* buf = nullptr;
    if (osip_uri_to_str(uri, &buf) == 0 && buf) {
        std::string result(buf);
        osip_free(buf);
        return result;
    }
    return {};
}

struct OsipUriGuard {
    osip_uri_t* value = nullptr;
    ~OsipUriGuard() {
        if (value) {
            osip_uri_free(value);
        }
    }
};

} // namespace

auto extract_uri_from_name_addr(const std::string& value) -> std::string {
    auto trimmed = trim(value);
    if (trimmed.empty() || trimmed == "*") {
        return trimmed;
    }

    auto angle_start = trimmed.find('<');
    auto angle_end = trimmed.find('>', angle_start == std::string::npos ? 0 : angle_start + 1);
    if (angle_start != std::string::npos && angle_end != std::string::npos && angle_end > angle_start) {
        return trimmed.substr(angle_start + 1, angle_end - angle_start - 1);
    }

    auto lowered = lowercase(trimmed);
    if (lowered.rfind("tel:", 0) == 0) {
        auto semi = trimmed.find(';');
        if (semi != std::string::npos) {
            return trimmed.substr(0, semi);
        }
    }

    return trimmed;
}

auto normalize_impu_uri(const std::string& value) -> std::string {
    auto uri_text = extract_uri_from_name_addr(value);
    if (uri_text.empty() || uri_text == "*") {
        return uri_text;
    }

    auto lowered = lowercase(uri_text);
    if (lowered.rfind("tel:", 0) == 0) {
        return lowered;
    }

    OsipUriGuard uri;
    if (osip_uri_init(&uri.value) != 0 || !uri.value) {
        return lowered;
    }
    if (osip_uri_parse(uri.value, uri_text.c_str()) != 0) {
        return lowered;
    }

    auto scheme = uri.value->scheme ? lowercase(uri.value->scheme) : std::string{};
    if (scheme == "tel") {
        if (!uri.value->username) {
            return "tel:";
        }
        return std::format("tel:{}", uri.value->username);
    }


    if (scheme != "sip" && scheme != "sips") {
        return lowercase(uri_text);
    }
    if (!uri.value->username || !uri.value->host) {
        return lowercase(uri_text);
    }

    return std::format("{}:{}@{}", scheme, uri.value->username, lowercase(uri.value->host));
}

auto parse_endpoint_from_uri(const std::string& sip_uri) -> std::optional<Endpoint> {
    auto uri_text = extract_uri_from_name_addr(sip_uri);
    if (uri_text.empty() || uri_text == "*") {
        return std::nullopt;
    }

    OsipUriGuard uri;
    if (osip_uri_init(&uri.value) != 0 || !uri.value) {
        return std::nullopt;
    }
    if (osip_uri_parse(uri.value, uri_text.c_str()) != 0) {
        return std::nullopt;
    }

    if (!uri.value->host) {
        return std::nullopt;
    }

    std::string transport = "udp";
    osip_uri_param_t* transport_param = nullptr;
    if (osip_uri_uparam_get_byname(uri.value, const_cast<char*>("transport"), &transport_param) == 0 &&
        transport_param && transport_param->gvalue) {
        transport = lowercase(transport_param->gvalue);
    }

    Port port = 5060;
    if (uri.value->port) {
        try {
            port = static_cast<Port>(std::stoi(uri.value->port));
        } catch (...) {
            return std::nullopt;
        }
    }

    return Endpoint{
        .address = uri.value->host,
        .port = port,
        .transport = transport,
    };
}

auto route_points_to_endpoint(const std::string& route_value,
                              const Endpoint& endpoint,
                              std::string_view fallback_transport) -> bool {
    auto route_endpoint = parse_endpoint_from_uri(route_value);
    if (!route_endpoint) {
        return false;
    }

    auto lower = [](std::string value) {
        std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
            return static_cast<char>(std::tolower(ch));
        });
        return value;
    };

    auto normalized_route_host = lower(route_endpoint->address);
    auto normalized_host = lower(endpoint.address);
    if (normalized_route_host != normalized_host) {
        return false;
    }

    auto normalized_transport = endpoint.transport.empty()
        ? std::string(fallback_transport)
        : endpoint.transport;
    normalized_transport = lower(normalized_transport);

    auto normalized_route_transport = route_endpoint->transport.empty()
        ? std::string(fallback_transport)
        : route_endpoint->transport;
    normalized_route_transport = lower(normalized_route_transport);

    if (normalized_transport != normalized_route_transport) {
        return false;
    }

    auto default_port_for = [](std::string_view transport) -> Port {
        return 5060;
    };

    auto route_port = route_endpoint->port == 0
        ? default_port_for(normalized_route_transport)
        : route_endpoint->port;
    auto endpoint_port = endpoint.port == 0
        ? default_port_for(normalized_transport)
        : endpoint.port;

    return route_port == endpoint_port;
}

auto parse_digest_authorization(const std::string& header) -> Result<DigestAuthFields> {
    auto pos = header.find("Digest ");
    if (pos == std::string::npos) {
        pos = header.find("digest ");
    }
    if (pos == std::string::npos) {
        return std::unexpected(ErrorInfo{
            ErrorCode::kSipParseError,
            "Missing Digest scheme in Authorization header",
        });
    }

    DigestAuthFields fields;
    std::string fields_str = header.substr(pos + 7);
    std::istringstream stream(fields_str);
    std::string token;
    while (std::getline(stream, token, ',')) {
        token = trim(token);
        auto eq = token.find('=');
        if (eq == std::string::npos) {
            continue;
        }

        auto key = lowercase(trim(token.substr(0, eq)));
        auto value = trim_quotes(trim(token.substr(eq + 1)));

        if (key == "username") fields.username = value;
        else if (key == "realm") fields.realm = value;
        else if (key == "nonce") fields.nonce = value;
        else if (key == "response") fields.response = value;
        else if (key == "uri") fields.uri = value;
        else if (key == "algorithm") fields.algorithm = value;
        else if (key == "qop") fields.qop = value;
        else if (key == "nc") fields.nc = value;
        else if (key == "cnonce") fields.cnonce = value;
    }

    if (fields.username.empty()) {
        return std::unexpected(ErrorInfo{
            ErrorCode::kSipParseError,
            "Missing username in Authorization header",
        });
    }

    return fields;
}

} // namespace ims::sip
