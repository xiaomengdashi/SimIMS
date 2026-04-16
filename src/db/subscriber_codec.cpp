#include "db/subscriber_codec.hpp"

#include <bson/bson.h>

#include <optional>
#include <string>

namespace ims::db {
namespace {

auto invalid_field(std::string_view field) -> ErrorInfo {
    return ErrorInfo{
        ErrorCode::kConfigInvalidValue,
        "Invalid subscriber document field",
        std::string(field),
    };
}

auto extract_utf8(const bson_t& doc, std::string_view key) -> std::optional<std::string> {
    bson_iter_t iter;
    if (!bson_iter_init_find(&iter, &doc, std::string(key).c_str()) || !BSON_ITER_HOLDS_UTF8(&iter)) {
        return std::nullopt;
    }
    uint32_t length = 0;
    const char* value = bson_iter_utf8(&iter, &length);
    return std::string(value, length);
}

auto extract_uint64(const bson_t& doc, std::string_view key) -> std::optional<uint64_t> {
    bson_iter_t iter;
    if (!bson_iter_init_find(&iter, &doc, std::string(key).c_str())) {
        return std::nullopt;
    }

    if (BSON_ITER_HOLDS_INT64(&iter)) {
        const auto value = bson_iter_int64(&iter);
        return value < 0 ? std::nullopt : std::optional<uint64_t>(static_cast<uint64_t>(value));
    }
    if (BSON_ITER_HOLDS_INT32(&iter)) {
        const auto value = bson_iter_int32(&iter);
        return value < 0 ? std::nullopt : std::optional<uint64_t>(static_cast<uint64_t>(value));
    }

    return std::nullopt;
}

auto extract_document(const bson_t& root, std::string_view key, bson_t* out_doc) -> bool {
    bson_iter_t iter;
    if (!bson_iter_init_find(&iter, &root, std::string(key).c_str()) || !BSON_ITER_HOLDS_DOCUMENT(&iter)) {
        return false;
    }

    const uint8_t* data = nullptr;
    uint32_t length = 0;
    bson_iter_document(&iter, &length, &data);
    return bson_init_static(out_doc, data, length);
}

auto extract_string_array(const bson_t& root, std::string_view key) -> std::optional<std::vector<std::string>> {
    bson_iter_t iter;
    if (!bson_iter_init_find(&iter, &root, std::string(key).c_str()) || !BSON_ITER_HOLDS_ARRAY(&iter)) {
        return std::nullopt;
    }

    bson_t array_doc;
    const uint8_t* data = nullptr;
    uint32_t length = 0;
    bson_iter_array(&iter, &length, &data);
    if (!bson_init_static(&array_doc, data, length)) {
        return std::nullopt;
    }

    std::vector<std::string> values;
    bson_iter_t array_iter;
    if (!bson_iter_init(&array_iter, &array_doc)) {
        return std::nullopt;
    }

    while (bson_iter_next(&array_iter)) {
        if (!BSON_ITER_HOLDS_UTF8(&array_iter)) {
            return std::nullopt;
        }
        uint32_t value_length = 0;
        const char* value = bson_iter_utf8(&array_iter, &value_length);
        values.emplace_back(value, value_length);
    }

    return values;
}

} // namespace

auto decodeSubscriber(const bson_t& document) -> Result<SubscriberRecord> {
    SubscriberRecord out;

    auto legacy_tel = extract_utf8(document, "tel");

    bson_t identities;
    if (!extract_document(document, "identities", &identities)) {
        return std::unexpected(invalid_field("identities"));
    }

    auto impi = extract_utf8(identities, "impi");
    auto canonical_impu = extract_utf8(identities, "canonical_impu");
    auto associated_impus = extract_string_array(identities, "associated_impus");
    auto realm = extract_utf8(identities, "realm");

    if (!impi || !canonical_impu || !realm) {
        return std::unexpected(invalid_field("identities"));
    }

    if (!associated_impus) {
        if (!legacy_tel) {
            return std::unexpected(invalid_field("identities.associated_impus"));
        }

        std::vector<std::string> generated;
        generated.emplace_back("tel:" + *legacy_tel);
        generated.emplace_back("sip:" + *legacy_tel + "@" + *realm);
        generated.emplace_back(*canonical_impu);
        associated_impus = std::move(generated);
    }

    out.identities.impi = *impi;
    out.identities.canonical_impu = *canonical_impu;
    out.identities.associated_impus = std::move(*associated_impus);
    out.identities.realm = *realm;

    auto at_pos = out.identities.impi.find('@');
    if (at_pos != std::string::npos) {
        out.identities.username = out.identities.impi.substr(0, at_pos);
    }

    bson_t auth;
    if (!extract_document(document, "auth", &auth)) {
        return std::unexpected(invalid_field("auth"));
    }

    auto password = extract_utf8(auth, "password");
    auto ki = extract_utf8(auth, "ki");
    auto operator_code_type = extract_utf8(auth, "operator_code_type");
    auto opc = extract_utf8(auth, "opc");
    auto op = extract_utf8(auth, "op");
    auto sqn = extract_uint64(auth, "sqn");
    auto amf = extract_utf8(auth, "amf");

    if (!password || !ki || !operator_code_type || !sqn || !amf) {
        return std::unexpected(invalid_field("auth"));
    }

    if (*operator_code_type == "opc") {
        if (!opc || opc->empty()) {
            return std::unexpected(invalid_field("auth.opc"));
        }
    } else if (*operator_code_type == "op") {
        if (!op || op->empty()) {
            return std::unexpected(invalid_field("auth.op"));
        }
    } else {
        const bool has_opc = opc && !opc->empty();
        const bool has_op = op && !op->empty();
        if (!has_opc && !has_op) {
            return std::unexpected(invalid_field("auth.opc/auth.op"));
        }
    }

    out.auth.password = *password;
    out.auth.ki = *ki;
    out.auth.operator_code_type = *operator_code_type;
    out.auth.opc = opc.value_or("");
    out.auth.op = op.value_or("");
    out.auth.sqn = *sqn;
    out.auth.amf = *amf;

    bson_t serving;
    if (extract_document(document, "serving", &serving)) {
        if (auto assigned_scscf = extract_utf8(serving, "assigned_scscf"); assigned_scscf) {
            out.serving.assigned_scscf = *assigned_scscf;
        }
    }

    out.profile.ifcs = {};

    return out;
}

auto nextSqn(uint64_t current, uint64_t step, uint64_t mask) -> uint64_t {
    return (current + step) & mask;
}

} // namespace ims::db
