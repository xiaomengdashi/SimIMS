#include "cx_client.hpp"
#include "common/logger.hpp"

#include <random>
#include <format>

namespace ims::diameter {

StubHssClient::StubHssClient(const HssAdapterConfig& config)
    : config_(config)
    , scscf_uri_(std::format("sip:scscf.{}", config.diameter_realm))
{
    IMS_LOG_WARN("Using STUB HSS client - not suitable for production");
}

auto StubHssClient::userAuthorization(const UarParams& params) -> Result<UaaResult> {
    IMS_LOG_INFO("Stub UAR: IMPI={} IMPU={}", params.impi, params.impu);

    return UaaResult{
        .result_code = 2001,  // DIAMETER_SUCCESS
        .assigned_scscf = scscf_uri_,
    };
}

auto StubHssClient::multimediaAuth(const MarParams& params) -> Result<MaaResult> {
    IMS_LOG_INFO("Stub MAR: IMPI={} scheme={}", params.impi, params.sip_auth_scheme);

    // Generate a deterministic but unique auth vector for testing
    static thread_local std::mt19937 rng{42};  // Fixed seed for reproducibility
    std::uniform_int_distribution<uint8_t> dist(0, 255);

    AuthVector av;
    av.rand.resize(16);
    av.autn.resize(16);
    av.xres.resize(8);
    av.ck.resize(16);
    av.ik.resize(16);

    for (auto& b : av.rand) b = dist(rng);
    for (auto& b : av.autn) b = dist(rng);
    for (auto& b : av.xres) b = dist(rng);
    for (auto& b : av.ck) b = dist(rng);
    for (auto& b : av.ik) b = dist(rng);

    return MaaResult{
        .result_code = 2001,
        .sip_auth_scheme = "Digest-AKAv1-MD5",
        .auth_vector = std::move(av),
    };
}

auto StubHssClient::serverAssignment(const SarParams& params) -> Result<SaaResult> {
    IMS_LOG_INFO("Stub SAR: IMPI={} IMPU={} type={}",
                 params.impi, params.impu, static_cast<uint32_t>(params.assignment_type));

    return SaaResult{
        .result_code = 2001,
        .user_profile = {
            .impu = params.impu,
            .associated_impus = {params.impu},
            .ifcs = {},
        },
    };
}

auto StubHssClient::locationInfo(const LirParams& params) -> Result<LiaResult> {
    IMS_LOG_INFO("Stub LIR: IMPU={}", params.impu);

    return LiaResult{
        .result_code = 2001,
        .assigned_scscf = scscf_uri_,
    };
}

} // namespace ims::diameter
