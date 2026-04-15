#pragma once
#include "types.hpp"
#include <string>
#include <vector>
#include <optional>
#include <cstdint>

namespace ims {

struct GlobalConfig {
    std::string log_level = "info";
    std::string node_name = "ims-node";
};

struct RtpengineSettings {
    std::string host = "127.0.0.1";
    uint16_t port = 22222;
};

struct PcfSettings {
    std::string host = "127.0.0.1";
    uint16_t port = 8080;
};

struct SipEndpointConfig {
    std::string address = "127.0.0.1";
    uint16_t port = 5060;
    std::string transport = "udp";
};

struct HssSettings {
    std::string host = "127.0.0.1";
    uint16_t port = 3868;
    std::string realm = "ims.local";
};

struct PcscfConfig {
    std::string listen_addr = "0.0.0.0";
    uint16_t listen_port = 5060;
    std::string advertised_addr;
    PcfSettings pcf;
    SipEndpointConfig core_entry{
        .address = "127.0.0.1",
        .port = 5062,
        .transport = "udp",
    };
    std::vector<SipEndpointConfig> core_peers;
};

struct IcscfConfig {
    std::string listen_addr = "0.0.0.0";
    uint16_t listen_port = 5060;
    std::string advertised_addr;
    HssSettings hss;
    SipEndpointConfig local_scscf{
        .address = "127.0.0.1",
        .port = 5062,
        .transport = "udp",
    };
};

struct ExosipConfig {
    bool enabled = true;
    std::string listen_addr = "0.0.0.0";
    uint16_t listen_port = 5072;
    std::string transport = "udp";
    std::string user_agent = "SimIMS-eXosip2";
    uint32_t event_poll_ms = 100;
};

struct ScscfConfig {
    std::string listen_addr = "0.0.0.0";
    uint16_t listen_port = 5060;
    std::string advertised_addr;
    HssSettings hss;
    std::string domain = "ims.local";
    std::string auth_mode = "ims_only";  // ims_only, digest_only, hybrid_fallback
    uint32_t registration_cleanup_interval_ms = 30000;
    ExosipConfig exosip;
    std::optional<SipEndpointConfig> peer_icscf;
};

struct HssSubscriberConfig {
    std::string imsi;
    std::string tel;
    std::string password;
    std::string realm;
    std::string k;
    std::string operator_code_type;
    std::string opc;
    std::string op;
    std::string sqn;
    std::string amf = "8000";
};

struct HssAdapterConfig {
    std::string type = "diameter";  // "diameter" or "nudm"
    std::string diameter_host = "127.0.0.1";
    uint16_t diameter_port = 3868;
    std::string diameter_realm = "ims.local";
    std::string nudm_url = "http://127.0.0.1:8080";
    std::vector<HssSubscriberConfig> subscribers;
};

struct MediaConfig {
    std::string rtpengine_host = "127.0.0.1";
    uint16_t rtpengine_port = 22222;
};

struct DnsConfig {
    std::vector<std::string> servers = {"127.0.0.1"};
    uint32_t timeout_ms = 3000;
};

struct ImsConfig {
    GlobalConfig global;
    PcscfConfig pcscf;
    IcscfConfig icscf;
    ScscfConfig scscf;
    HssAdapterConfig hss_adapter;
    MediaConfig media;
    DnsConfig dns;
};

Result<ImsConfig> load_config(const std::string& path);

} // namespace ims
