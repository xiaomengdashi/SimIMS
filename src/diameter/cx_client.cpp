#include "cx_client.hpp"
#include "common/logger.hpp"
#include "sip/uri_utils.hpp"

#include <format>
#include <random>
#include <utility>

namespace ims::diameter {
namespace {

constexpr uint32_t kDiameterSuccess = 2001;

auto make_impi(const HssSubscriberConfig& subscriber) -> std::string {
    return std::format("{}@{}", subscriber.imsi, subscriber.realm);
}

auto make_canonical_impu(const HssSubscriberConfig& subscriber) -> std::string {
    return std::format("sip:{}@{}", subscriber.imsi, subscriber.realm);
}

auto make_associated_impus(const HssSubscriberConfig& subscriber) -> std::vector<std::string> {
    return {
        std::format("tel:{}", subscriber.tel),
        std::format("sip:{}@{}", subscriber.tel, subscriber.realm),
        make_canonical_impu(subscriber),
    };
}

} // namespace

StubHssClient::StubHssClient(const HssAdapterConfig& config)
    : config_(config)
    , scscf_uri_("sip:127.0.0.1:5062;transport=udp")
{
    for (const auto& subscriber : config_.subscribers) {
        SubscriberRecord record{
            .config = subscriber,
            .impi = make_impi(subscriber),
            .canonical_impu = make_canonical_impu(subscriber),
            .associated_impus = make_associated_impus(subscriber),
        };
        auto index = subscribers_.size();
        subscriber_by_impi_.emplace(record.impi, index);
        for (const auto& identity : record.associated_impus) {
            subscriber_by_public_identity_.emplace(ims::sip::normalize_impu_uri(identity), index);
        }
        subscribers_.push_back(std::move(record));
    }

    IMS_LOG_WARN("Using STUB HSS client - not suitable for production");
}

auto StubHssClient::userAuthorization(const UarParams& params) -> Result<UaaResult> {
    IMS_LOG_INFO("Stub UAR: IMPI={} IMPU={}", params.impi, params.impu);

    auto* subscriber = findSubscriber(params.impi, params.impu);
    if (!subscriber) {
        return std::unexpected(ErrorInfo{
            ErrorCode::kDiameterUserNotFound,
            "Subscriber not found for UAR",
            std::format("IMPI={} IMPU={}", params.impi, params.impu)
        });
    }

    return UaaResult{
        .result_code = kDiameterSuccess,
        .assigned_scscf = scscf_uri_,
    };
}

auto StubHssClient::multimediaAuth(const MarParams& params) -> Result<MaaResult> {
    IMS_LOG_INFO("Stub MAR: IMPI={} scheme={}", params.impi, params.sip_auth_scheme);

    auto* subscriber = findSubscriber(params.impi, params.impu);
    if (!subscriber) {
        return std::unexpected(ErrorInfo{
            ErrorCode::kDiameterUserNotFound,
            "Subscriber not found for MAR",
            std::format("IMPI={} IMPU={}", params.impi, params.impu)
        });
    }

    static thread_local std::mt19937 rng{42};
    std::uniform_int_distribution<uint8_t> dist(0, 255);

    AuthVector av;
    av.rand.resize(16);
    av.autn.resize(16);
    av.ck.resize(16);
    av.ik.resize(16);

    for (auto& b : av.rand) b = dist(rng);
    for (auto& b : av.autn) b = dist(rng);
    for (auto& b : av.ck) b = dist(rng);
    for (auto& b : av.ik) b = dist(rng);

    av.xres.assign(subscriber->config.password.begin(), subscriber->config.password.end());

    return MaaResult{
        .result_code = kDiameterSuccess,
        .sip_auth_scheme = params.sip_auth_scheme,
        .auth_vector = std::move(av),
    };
}

auto StubHssClient::serverAssignment(const SarParams& params) -> Result<SaaResult> {
    IMS_LOG_INFO("Stub SAR: IMPI={} IMPU={} type={}",
                 params.impi, params.impu, static_cast<uint32_t>(params.assignment_type));

    auto* subscriber = findSubscriber(params.impi, params.impu);
    if (!subscriber) {
        return std::unexpected(ErrorInfo{
            ErrorCode::kDiameterUserNotFound,
            "Subscriber not found for SAR",
            std::format("IMPI={} IMPU={}", params.impi, params.impu)
        });
    }

    return SaaResult{
        .result_code = kDiameterSuccess,
        .user_profile = {
            .impu = subscriber->canonical_impu,
            .associated_impus = subscriber->associated_impus,
            .ifcs = {},
        },
    };
}

auto StubHssClient::locationInfo(const LirParams& params) -> Result<LiaResult> {
    IMS_LOG_INFO("Stub LIR: IMPU={}", params.impu);

    auto* subscriber = findSubscriberByPublicIdentity(params.impu);
    if (!subscriber) {
        return std::unexpected(ErrorInfo{
            ErrorCode::kDiameterUserNotFound,
            "Subscriber not found for LIR",
            params.impu
        });
    }

    return LiaResult{
        .result_code = kDiameterSuccess,
        .assigned_scscf = scscf_uri_,
    };
}

auto StubHssClient::findSubscriberByImpi(const std::string& impi) const -> const SubscriberRecord* {
    auto it = subscriber_by_impi_.find(impi);
    if (it == subscriber_by_impi_.end()) {
        return nullptr;
    }
    return &subscribers_[it->second];
}

auto StubHssClient::findSubscriberByPublicIdentity(const std::string& identity) const -> const SubscriberRecord* {
    auto normalized = ims::sip::normalize_impu_uri(identity);
    auto it = subscriber_by_public_identity_.find(normalized);
    if (it == subscriber_by_public_identity_.end()) {
        return nullptr;
    }
    return &subscribers_[it->second];
}

auto StubHssClient::findSubscriber(const std::string& impi, const std::string& impu) const -> const SubscriberRecord* {
    if (auto* subscriber = findSubscriberByImpi(impi)) {
        return subscriber;
    }
    if (!impu.empty()) {
        return findSubscriberByPublicIdentity(impu);
    }
    return nullptr;
}

} // namespace ims::diameter
