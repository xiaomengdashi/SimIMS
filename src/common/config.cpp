#include <ims/common/config.hpp>
#include <yaml-cpp/yaml.h>
#include <filesystem>

namespace ims {

Result<ImsConfig> load_config(const std::string& path) {
    if (!std::filesystem::exists(path)) {
        return std::unexpected(ErrorInfo{
            ErrorCode::kConfigFileNotFound,
            "Config file not found",
            path
        });
    }

    YAML::Node root;
    try {
        root = YAML::LoadFile(path);
    } catch (const YAML::Exception& e) {
        return std::unexpected(ErrorInfo{
            ErrorCode::kConfigParseError,
            "Failed to parse config file",
            e.what()
        });
    }

    ImsConfig config;

    // Global section
    if (auto global = root["global"]) {
        if (auto v = global["log_level"])  config.global.log_level = v.as<std::string>();
        if (auto v = global["node_name"])  config.global.node_name = v.as<std::string>();
    }

    // P-CSCF section
    if (auto pcscf = root["pcscf"]) {
        if (auto v = pcscf["listen_addr"])  config.pcscf.listen_addr = v.as<std::string>();
        if (auto v = pcscf["listen_port"])  config.pcscf.listen_port = v.as<uint16_t>();
        if (auto rtp = pcscf["rtpengine"]) {
            if (auto v = rtp["host"])  config.pcscf.rtpengine.host = v.as<std::string>();
            if (auto v = rtp["port"])  config.pcscf.rtpengine.port = v.as<uint16_t>();
        }
        if (auto pcf = pcscf["pcf"]) {
            if (auto v = pcf["host"])  config.pcscf.pcf.host = v.as<std::string>();
            if (auto v = pcf["port"])  config.pcscf.pcf.port = v.as<uint16_t>();
        }
    }

    // I-CSCF section
    if (auto icscf = root["icscf"]) {
        if (auto v = icscf["listen_addr"])  config.icscf.listen_addr = v.as<std::string>();
        if (auto v = icscf["listen_port"])  config.icscf.listen_port = v.as<uint16_t>();
        if (auto hss = icscf["hss"]) {
            if (auto v = hss["host"])   config.icscf.hss.host = v.as<std::string>();
            if (auto v = hss["port"])   config.icscf.hss.port = v.as<uint16_t>();
            if (auto v = hss["realm"])  config.icscf.hss.realm = v.as<std::string>();
        }
    }

    // S-CSCF section
    if (auto scscf = root["scscf"]) {
        if (auto v = scscf["listen_addr"])  config.scscf.listen_addr = v.as<std::string>();
        if (auto v = scscf["listen_port"])  config.scscf.listen_port = v.as<uint16_t>();
        if (auto v = scscf["domain"])       config.scscf.domain = v.as<std::string>();
        if (auto hss = scscf["hss"]) {
            if (auto v = hss["host"])   config.scscf.hss.host = v.as<std::string>();
            if (auto v = hss["port"])   config.scscf.hss.port = v.as<uint16_t>();
            if (auto v = hss["realm"])  config.scscf.hss.realm = v.as<std::string>();
        }
    }

    // HSS adapter section
    if (auto hss = root["hss_adapter"]) {
        if (auto v = hss["type"])            config.hss_adapter.type = v.as<std::string>();
        if (auto v = hss["diameter_host"])   config.hss_adapter.diameter_host = v.as<std::string>();
        if (auto v = hss["diameter_port"])   config.hss_adapter.diameter_port = v.as<uint16_t>();
        if (auto v = hss["diameter_realm"])  config.hss_adapter.diameter_realm = v.as<std::string>();
        if (auto v = hss["nudm_url"])        config.hss_adapter.nudm_url = v.as<std::string>();
    }

    // Media section
    if (auto media = root["media"]) {
        if (auto v = media["rtpengine_host"])  config.media.rtpengine_host = v.as<std::string>();
        if (auto v = media["rtpengine_port"])  config.media.rtpengine_port = v.as<uint16_t>();
    }

    // DNS section
    if (auto dns = root["dns"]) {
        if (auto v = dns["timeout_ms"])  config.dns.timeout_ms = v.as<uint32_t>();
        if (auto servers = dns["servers"]) {
            config.dns.servers.clear();
            for (const auto& s : servers) {
                config.dns.servers.push_back(s.as<std::string>());
            }
        }
    }

    return config;
}

} // namespace ims
