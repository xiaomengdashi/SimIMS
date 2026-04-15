#include "diameter/aka_vector_builder.hpp"

#include "crypt/milenage_adapter.hpp"

#include <algorithm>
#include <array>
#include <optional>
#include <random>
#include <span>
#include <string_view>

namespace ims::diameter {
namespace {

auto hex_value(char ch) -> std::optional<uint8_t> {
    if (ch >= '0' && ch <= '9') return static_cast<uint8_t>(ch - '0');
    if (ch >= 'a' && ch <= 'f') return static_cast<uint8_t>(ch - 'a' + 10);
    if (ch >= 'A' && ch <= 'F') return static_cast<uint8_t>(ch - 'A' + 10);
    return std::nullopt;
}

template <std::size_t N>
auto parse_hex_exact(std::string_view value, const char* field_name) -> Result<std::array<uint8_t, N>> {
    if (value.size() != N * 2) {
        return std::unexpected(ErrorInfo{
            ErrorCode::kConfigInvalidValue,
            "invalid AKA hex field length",
            std::string(field_name)
        });
    }

    std::array<uint8_t, N> out{};
    for (std::size_t i = 0; i < N; ++i) {
        auto hi = hex_value(value[i * 2]);
        auto lo = hex_value(value[i * 2 + 1]);
        if (!hi || !lo) {
            return std::unexpected(ErrorInfo{
                ErrorCode::kConfigInvalidValue,
                "invalid AKA hex field value",
                std::string(field_name)
            });
        }
        out[i] = static_cast<uint8_t>((*hi << 4) | *lo);
    }
    return out;
}

auto resolve_opc(const HssSubscriberConfig& subscriber,
                 std::span<const uint8_t, 16> k) -> Result<std::array<uint8_t, 16>> {
    if (subscriber.operator_code_type == "opc") {
        if (subscriber.opc.empty()) {
            return std::unexpected(ErrorInfo{
                ErrorCode::kConfigInvalidValue,
                "missing AKA opc for operator_code_type=opc",
                subscriber.imsi
            });
        }
        return parse_hex_exact<16>(subscriber.opc, "opc");
    }

    if (subscriber.operator_code_type == "op") {
        if (subscriber.op.empty()) {
            return std::unexpected(ErrorInfo{
                ErrorCode::kConfigInvalidValue,
                "missing AKA op for operator_code_type=op",
                subscriber.imsi
            });
        }
        auto op = parse_hex_exact<16>(subscriber.op, "op");
        if (!op) {
            return std::unexpected(op.error());
        }
        return ims::crypt::compute_opc(k, *op);
    }

    if (!subscriber.opc.empty()) {
        return parse_hex_exact<16>(subscriber.opc, "opc");
    }
    if (subscriber.op.empty()) {
        return std::unexpected(ErrorInfo{
            ErrorCode::kConfigInvalidValue,
            "missing AKA operator code",
            subscriber.imsi
        });
    }

    auto op = parse_hex_exact<16>(subscriber.op, "op");
    if (!op) {
        return std::unexpected(op.error());
    }
    return ims::crypt::compute_opc(k, *op);
}

} // namespace

auto build_aka_auth_vector(const HssSubscriberConfig& subscriber,
                           std::mt19937& rng) -> Result<AuthVector> {
    if (subscriber.ki.empty() || subscriber.sqn.empty()) {
        return std::unexpected(ErrorInfo{
            ErrorCode::kConfigInvalidValue,
            "missing AKA subscriber material",
            subscriber.imsi
        });
    }

    auto k = parse_hex_exact<16>(subscriber.ki, "ki");
    if (!k) {
        return std::unexpected(k.error());
    }

    auto opc = resolve_opc(subscriber, *k);
    if (!opc) {
        return std::unexpected(opc.error());
    }

    auto sqn = parse_hex_exact<6>(subscriber.sqn, "sqn");
    if (!sqn) {
        return std::unexpected(sqn.error());
    }

    auto amf = parse_hex_exact<2>(subscriber.amf.empty() ? std::string_view{"8000"} : std::string_view{subscriber.amf}, "amf");
    if (!amf) {
        return std::unexpected(amf.error());
    }

    std::array<uint8_t, 16> rand{};
    std::uniform_int_distribution<uint16_t> dist(0, 255);
    for (auto& byte : rand) {
        byte = static_cast<uint8_t>(dist(rng));
    }

    auto vector = ims::crypt::generate_vector(*opc, *k, *sqn, *amf, rand);
    if (!vector) {
        return std::unexpected(vector.error());
    }

    AuthVector auth_vector;
    auth_vector.rand.assign(rand.begin(), rand.end());
    auth_vector.autn.assign(vector->autn.begin(), vector->autn.end());
    auth_vector.xres.assign(vector->xres.begin(), vector->xres.end());
    auth_vector.ck.assign(vector->ck.begin(), vector->ck.end());
    auth_vector.ik.assign(vector->ik.begin(), vector->ik.end());
    return auth_vector;
}

} // namespace ims::diameter
