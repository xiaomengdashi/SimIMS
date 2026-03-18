#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace ims::diameter {

// ========== Cx Interface Types (CSCF <-> HSS) ==========

// User-Authorization-Request parameters
struct UarParams {
    std::string impi;          // IMS Private Identity (e.g., user@realm)
    std::string impu;          // IMS Public Identity (e.g., sip:user@domain)
    std::string visited_network;
    enum class AuthType : uint32_t {
        kRegistration = 0,
        kDeRegistration = 1,
        kRegistrationAndCapabilities = 2,
    } auth_type = AuthType::kRegistration;
};

// User-Authorization-Answer result
struct UaaResult {
    uint32_t result_code = 0;
    std::string assigned_scscf;  // S-CSCF URI assigned by HSS
    struct Capabilities {
        std::vector<uint32_t> mandatory;
        std::vector<uint32_t> optional;
    };
    std::optional<Capabilities> server_capabilities;
};

// Multimedia-Auth-Request parameters
struct MarParams {
    std::string impi;
    std::string impu;
    std::string sip_auth_scheme;  // "Digest-AKAv1-MD5" for VoLTE/VoNR
    std::string sip_authorization;  // Client nonce for resync
    std::string server_name;        // S-CSCF URI
    uint32_t sip_number_auth_items = 1;
};

// AKA authentication vector
struct AuthVector {
    std::vector<uint8_t> rand;   // 16 bytes
    std::vector<uint8_t> autn;   // 16 bytes
    std::vector<uint8_t> xres;   // 4-16 bytes
    std::vector<uint8_t> ck;     // 16 bytes (cipher key)
    std::vector<uint8_t> ik;     // 16 bytes (integrity key)
};

// Multimedia-Auth-Answer result
struct MaaResult {
    uint32_t result_code = 0;
    std::string sip_auth_scheme;
    AuthVector auth_vector;
};

// Server-Assignment-Request parameters
struct SarParams {
    std::string impi;
    std::string impu;
    std::string server_name;  // S-CSCF URI
    enum class AssignmentType : uint32_t {
        kNoAssignment = 0,
        kRegistration = 1,
        kReRegistration = 2,
        kUnregisteredUser = 3,
        kTimeoutDeregistration = 4,
        kUserDeregistration = 5,
        kDeregistrationTooMuchData = 6,
        kAdminDeregistration = 7,
        kAuthenticationFailure = 8,
        kAuthenticationTimeout = 9,
    } assignment_type = AssignmentType::kRegistration;
};

// Initial Filter Criteria (iFC) for service triggering
struct InitialFilterCriteria {
    uint32_t priority = 0;
    struct TriggerPoint {
        bool condition_type_cnf = true;  // CNF or DNF
        struct ServicePointTrigger {
            enum class Type { kMethod, kHeader, kRequestUri, kSessionCase };
            Type type;
            std::string value;
        };
        std::vector<ServicePointTrigger> triggers;
    } trigger_point;
    std::string application_server_uri;
};

// User profile data
struct UserProfile {
    std::string impu;
    std::vector<std::string> associated_impus;
    std::vector<InitialFilterCriteria> ifcs;
};

// Server-Assignment-Answer result
struct SaaResult {
    uint32_t result_code = 0;
    UserProfile user_profile;
};

// Location-Info-Request parameters
struct LirParams {
    std::string impu;
    enum class OriginatingRequest : uint32_t {
        kOriginating = 0,
    };
    std::optional<OriginatingRequest> originating;
};

// Location-Info-Answer result
struct LiaResult {
    uint32_t result_code = 0;
    std::string assigned_scscf;  // S-CSCF serving this user
    std::optional<UaaResult::Capabilities> server_capabilities;
};

// ========== Rx Interface Types (P-CSCF <-> PCF) ==========

// Media component for QoS request
struct MediaComponent {
    uint32_t media_component_number = 0;
    std::string media_type;  // "audio", "video"
    uint32_t max_requested_bandwidth_ul = 0;
    uint32_t max_requested_bandwidth_dl = 0;
    uint32_t flow_status = 0;  // 0=enabled-uplink, 1=enabled-downlink, 2=enabled, 3=disabled
    struct FlowDescription {
        std::string description;  // IP filter rule
    };
    std::vector<FlowDescription> flows;
    std::string codec_data;
};

// AA-Request parameters (Rx)
struct AarParams {
    std::string subscription_id;    // IMPU
    std::string ip_address;         // UE IP
    std::string called_station_id;  // Called party
    std::vector<MediaComponent> media_components;
    uint32_t specific_action = 0;
    std::string af_application_id = "IMS Services";
};

// AA-Answer result
struct AaaResult {
    uint32_t result_code = 0;
    std::string session_id;
};

// Session-Termination-Request parameters
struct StrParams {
    std::string session_id;
    uint32_t termination_cause = 1;  // DIAMETER_LOGOUT
};

// Session-Termination-Answer result
struct StaResult {
    uint32_t result_code = 0;
};

// Abort-Session-Request (from PCF to P-CSCF)
struct AsrParams {
    std::string session_id;
    uint32_t abort_cause = 0;
};

} // namespace ims::diameter
