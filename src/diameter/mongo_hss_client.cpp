#include "diameter/mongo_hss_client.hpp"

#include "diameter/aka_vector_builder.hpp"

#include <format>
#include <random>
#include <string_view>

namespace ims::diameter {
namespace {

constexpr uint32_t kDiameterSuccess = 2001;
constexpr uint64_t kSqnStep = 32;
constexpr uint64_t kSqnMask = 0x0000FFFFFFFFFFFFULL;

auto split_username_realm(std::string_view impi) -> std::pair<std::string, std::string> {
    auto pos = impi.find('@');
    if (pos == std::string_view::npos) {
        return {std::string{impi}, {}};
    }
    return {
        std::string{impi.substr(0, pos)},
        std::string{impi.substr(pos + 1)},
    };
}

auto to_hss_subscriber_config(const db::SubscriberRecord& subscriber) -> HssSubscriberConfig {
    HssSubscriberConfig out;
    auto [username, realm] = split_username_realm(subscriber.identities.impi);

    out.imsi = username.empty() ? subscriber.identities.impi : username;
    out.realm = subscriber.identities.realm.empty() ? realm : subscriber.identities.realm;
    out.ki = subscriber.auth.ki;
    out.operator_code_type = subscriber.auth.operator_code_type;
    out.opc = subscriber.auth.opc;
    out.op = subscriber.auth.op;
    out.sqn = std::format("{:012x}", (subscriber.auth.sqn & kSqnMask));
    out.amf = subscriber.auth.amf.empty() ? "8000" : subscriber.auth.amf;

    return out;
}

auto user_not_found(const std::string& message, const std::string& detail) -> ErrorInfo {
    return {
        ErrorCode::kDiameterUserNotFound,
        message,
        detail,
    };
}

} // namespace

MongoHssClient::MongoHssClient(const HssAdapterConfig& config, db::ISubscriberRepository& repository)
    : config_(config)
    , repository_(repository) {}

auto MongoHssClient::resolve_assigned_scscf(const db::SubscriberRecord& subscriber) const -> std::string {
    if (!subscriber.serving.assigned_scscf.empty()) {
        return subscriber.serving.assigned_scscf;
    }
    return config_.default_scscf_uri;
}

auto MongoHssClient::userAuthorization(const UarParams& params) -> Result<UaaResult> {
    auto subscriber = repository_.findByImpiOrImpu(params.impi, params.impu);
    if (!subscriber) {
        return std::unexpected(subscriber.error());
    }
    if (!*subscriber) {
        return std::unexpected(user_not_found(
            "Subscriber not found for UAR",
            std::format("IMPI={} IMPU={}", params.impi, params.impu)));
    }

    return UaaResult{
        .result_code = kDiameterSuccess,
        .assigned_scscf = resolve_assigned_scscf(**subscriber),
    };
}

auto MongoHssClient::multimediaAuth(const MarParams& params) -> Result<MaaResult> {
    auto subscriber = repository_.findByImpiOrImpu(params.impi, params.impu);
    if (!subscriber) {
        return std::unexpected(subscriber.error());
    }
    if (!*subscriber) {
        return std::unexpected(user_not_found(
            "Subscriber not found for MAR",
            std::format("IMPI={} IMPU={}", params.impi, params.impu)));
    }

    auto old_sqn = repository_.advanceSqn((*subscriber)->identities.impi, kSqnStep, kSqnMask);
    if (!old_sqn) {
        return std::unexpected(old_sqn.error());
    }

    auto subscriber_with_old_sqn = **subscriber;
    subscriber_with_old_sqn.auth.sqn = *old_sqn;

    static thread_local std::mt19937 rng = [] {
        std::random_device random_device;
        std::seed_seq seed{
            random_device(),
            random_device(),
            random_device(),
            random_device(),
            random_device(),
            random_device(),
            random_device(),
            random_device(),
        };
        return std::mt19937{seed};
    }();
    auto hss_subscriber = to_hss_subscriber_config(subscriber_with_old_sqn);
    auto auth_vector = build_aka_auth_vector(hss_subscriber, rng);
    if (!auth_vector) {
        return std::unexpected(auth_vector.error());
    }

    return MaaResult{
        .result_code = kDiameterSuccess,
        .sip_auth_scheme = params.sip_auth_scheme,
        .auth_vector = std::move(*auth_vector),
    };
}

auto MongoHssClient::serverAssignment(const SarParams& params) -> Result<SaaResult> {
    auto subscriber = repository_.findByImpiOrImpu(params.impi, params.impu);
    if (!subscriber) {
        return std::unexpected(subscriber.error());
    }
    if (!*subscriber) {
        return std::unexpected(user_not_found(
            "Subscriber not found for SAR",
            std::format("IMPI={} IMPU={}", params.impi, params.impu)));
    }

    auto set_serving_result = repository_.setServingScscf((*subscriber)->identities.impi, params.server_name);
    if (!set_serving_result) {
        return std::unexpected(set_serving_result.error());
    }

    return SaaResult{
        .result_code = kDiameterSuccess,
        .user_profile = {
            .impu = (*subscriber)->identities.canonical_impu,
            .associated_impus = (*subscriber)->identities.associated_impus,
            .ifcs = (*subscriber)->profile.ifcs,
        },
    };
}

auto MongoHssClient::locationInfo(const LirParams& params) -> Result<LiaResult> {
    auto subscriber = repository_.findByIdentity(params.impu);
    if (!subscriber) {
        return std::unexpected(subscriber.error());
    }
    if (!*subscriber) {
        return std::unexpected(user_not_found(
            "Subscriber not found for LIR",
            params.impu));
    }

    if ((*subscriber)->serving.assigned_scscf.empty()) {
        return std::unexpected(user_not_found(
            "Serving S-CSCF not found for LIR",
            params.impu));
    }

    return LiaResult{
        .result_code = kDiameterSuccess,
        .assigned_scscf = (*subscriber)->serving.assigned_scscf,
    };
}

} // namespace ims::diameter
