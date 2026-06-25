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

struct SmscSettings {
    SipEndpointConfig endpoint{
        .address = "127.0.0.1",
        .port = 5063,
        .transport = "udp",
    };
    std::string psi;
};

struct SmscConfig {
    std::string listen_addr = "0.0.0.0";
    uint16_t listen_port = 5063;
    std::string transport = "udp";
    std::string advertised_addr;
    std::string psi;
    SipEndpointConfig scscf{
        .address = "127.0.0.1",
        .port = 5062,
        .transport = "udp",
    };
};

struct ScscfConfig {
    std::string listen_addr = "0.0.0.0";
    uint16_t listen_port = 5060;
    std::string advertised_addr;
    HssSettings hss;
    std::string domain = "ims.local";
    std::string auth_mode = "ims_only";  // ims_only, digest_only, hybrid_fallback
    uint32_t registration_cleanup_interval_ms = 30000;
    std::optional<SipEndpointConfig> peer_icscf;
    std::optional<SmscSettings> smsc;
};

struct HssSubscriberConfig {
    std::string imsi;
    std::string tel;
    std::string password;
    std::string realm;
    std::string ki;
    std::string operator_code_type;
    std::string opc;
    std::string op;
    std::string sqn;
    std::string amf = "8000";
    // Whether the Anonymity Key (AK) is used to conceal SQN inside AUTN.
    // true  -> AUTN carries (SQN xor AK); UE must de-conceal (security.eak=true).
    // false -> AUTN carries SQN in clear; matches UEs configured with eak=false.
    bool use_ak = true;
};

struct HssAdapterConfig {
    std::string type = "diameter";  // "diameter" or "nudm"
    std::string diameter_host = "127.0.0.1";
    uint16_t diameter_port = 3868;
    std::string diameter_realm = "ims.local";
    std::string nudm_url = "http://127.0.0.1:8080";
    std::vector<HssSubscriberConfig> subscribers;

    std::string mongo_uri = "mongodb://127.0.0.1:27017";
    std::string mongo_db = "simims";
    std::string mongo_collection = "subscribers";
    std::string default_scscf_uri = "sip:127.0.0.1:5062;transport=udp";

    // Global SQN/AK concealment setting, applied to every subscriber.
    // true  -> AUTN carries (SQN xor AK) (standard IMS-AKA, UE eak=true).
    // false -> AUTN carries SQN in clear (matches UEs with eak=false).
    bool use_ak = true;
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
    SmscConfig smsc;
    HssAdapterConfig hss_adapter;
    MediaConfig media;
    DnsConfig dns;
};

Result<ImsConfig> load_config(const std::string& path);

} // namespace ims
