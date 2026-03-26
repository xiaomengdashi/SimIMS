#pragma once

#include "diameter/ihss_client.hpp"
#include "common/config.hpp"

#include <string>
#include <unordered_map>
#include <vector>

namespace ims::diameter {

/// Stub HSS client for development/testing.
/// Returns mock data - replace with freeDiameter-based implementation for production.
class StubHssClient : public IHssClient {
public:
    explicit StubHssClient(const HssAdapterConfig& config);

    auto userAuthorization(const UarParams& params) -> Result<UaaResult> override;
    auto multimediaAuth(const MarParams& params) -> Result<MaaResult> override;
    auto serverAssignment(const SarParams& params) -> Result<SaaResult> override;
    auto locationInfo(const LirParams& params) -> Result<LiaResult> override;

private:
    struct SubscriberRecord {
        HssSubscriberConfig config;
        std::string impi;
        std::string canonical_impu;
        std::vector<std::string> associated_impus;
    };

    auto findSubscriberByImpi(const std::string& impi) const -> const SubscriberRecord*;
    auto findSubscriberByPublicIdentity(const std::string& identity) const -> const SubscriberRecord*;
    auto findSubscriber(const std::string& impi, const std::string& impu) const -> const SubscriberRecord*;

    HssAdapterConfig config_;
    std::string scscf_uri_;
    std::vector<SubscriberRecord> subscribers_;
    std::unordered_map<std::string, std::size_t> subscriber_by_impi_;
    std::unordered_map<std::string, std::size_t> subscriber_by_public_identity_;
};

} // namespace ims::diameter
