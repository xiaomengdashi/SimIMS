#include "db/mongo_subscriber_repository.hpp"

#include "db/subscriber_codec.hpp"
#include "sip/uri_utils.hpp"

#include <bson/bson.h>
#include <mongoc/mongoc.h>

#include <algorithm>
#include <format>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace ims::db {
namespace {

auto db_error(std::string_view message, std::string_view detail) -> ErrorInfo {
    return ErrorInfo{ErrorCode::kInternalError, std::string(message), std::string(detail)};
}

auto not_found(std::string_view detail) -> ErrorInfo {
    return ErrorInfo{ErrorCode::kDiameterUserNotFound, "Subscriber not found", std::string(detail)};
}

auto parse_matched_count(const bson_t& reply) -> int64_t {
    bson_iter_t iter;
    if (bson_iter_init_find(&iter, &reply, "matchedCount") && BSON_ITER_HOLDS_INT64(&iter)) {
        return bson_iter_int64(&iter);
    }
    if (bson_iter_init_find(&iter, &reply, "matchedCount") && BSON_ITER_HOLDS_INT32(&iter)) {
        return static_cast<int64_t>(bson_iter_int32(&iter));
    }
    if (bson_iter_init_find(&iter, &reply, "n") && BSON_ITER_HOLDS_INT32(&iter)) {
        return static_cast<int64_t>(bson_iter_int32(&iter));
    }
    if (bson_iter_init_find(&iter, &reply, "n") && BSON_ITER_HOLDS_INT64(&iter)) {
        return bson_iter_int64(&iter);
    }
    return 0;
}

auto add_unique_candidate(std::vector<std::string>& candidates, std::string value) {
    if (value.empty()) {
        return;
    }
    if (std::find(candidates.begin(), candidates.end(), value) == candidates.end()) {
        candidates.push_back(std::move(value));
    }
}

auto strip_scheme(std::string value) -> std::string {
    if (value.rfind("sip:", 0) == 0) {
        return value.substr(4);
    }
    if (value.rfind("sips:", 0) == 0) {
        return value.substr(5);
    }
    if (value.rfind("tel:", 0) == 0) {
        return value.substr(4);
    }
    return value;
}

auto user_part(std::string value) -> std::string {
    value = strip_scheme(std::move(value));

    auto semicolon = value.find(';');
    if (semicolon != std::string::npos) {
        value = value.substr(0, semicolon);
    }

    auto at = value.find('@');
    if (at != std::string::npos) {
        value = value.substr(0, at);
    }

    return value;
}

auto identity_candidates(std::string_view identity) -> std::pair<std::vector<std::string>, std::vector<std::string>> {
    std::vector<std::string> imsis;
    std::vector<std::string> tels;

    if (identity.empty()) {
        return {imsis, tels};
    }

    auto normalized = ims::sip::normalize_impu_uri(std::string(identity));
    auto value = normalized.empty() ? std::string(identity) : normalized;
    auto user = user_part(value);

    if (value.rfind("tel:", 0) == 0 || user.rfind("+", 0) == 0) {
        add_unique_candidate(tels, user);
    } else {
        add_unique_candidate(imsis, user);
    }

    return {imsis, tels};
}

auto private_identity_imsi(std::string_view impi) -> std::optional<std::string> {
    if (impi.empty()) {
        return std::nullopt;
    }

    auto user = user_part(ims::sip::normalize_impu_uri(std::string(impi)));
    if (user.empty()) {
        user = user_part(std::string(impi));
    }
    if (user.empty()) {
        return std::nullopt;
    }

    return user;
}

} // namespace

auto MongoSubscriberRepository::create(const HssAdapterConfig& config)
    -> Result<std::shared_ptr<MongoSubscriberRepository>> {
    auto mongo_client = MongoClient::create(config);
    if (!mongo_client) {
        return std::unexpected(mongo_client.error());
    }
    return std::make_shared<MongoSubscriberRepository>(*mongo_client);
}

MongoSubscriberRepository::MongoSubscriberRepository(std::shared_ptr<MongoClient> client)
    : client_(std::move(client)) {}

auto MongoSubscriberRepository::findByImpiOrImpu(std::string_view impi,
                                                 std::string_view impu) const
    -> Result<std::optional<SubscriberRecord>> {
    std::vector<std::string> imsis;
    std::vector<std::string> tels;

    if (auto imsi = private_identity_imsi(impi); imsi) {
        add_unique_candidate(imsis, *imsi);
    }

    if (!impu.empty()) {
        auto [impu_imsis, impu_tels] = identity_candidates(impu);
        for (auto& imsi : impu_imsis) {
            add_unique_candidate(imsis, std::move(imsi));
        }
        for (auto& tel : impu_tels) {
            add_unique_candidate(tels, std::move(tel));
        }
    }

    return find_one_by_imsi_or_tel(imsis, tels);
}

auto MongoSubscriberRepository::findByIdentity(std::string_view identity) const
    -> Result<std::optional<SubscriberRecord>> {
    auto [imsis, tels] = identity_candidates(identity);
    return find_one_by_imsi_or_tel(imsis, tels);
}

auto MongoSubscriberRepository::findByUsernameRealm(std::string_view username,
                                                    std::string_view realm) const
    -> Result<std::optional<SubscriberRecord>> {
    std::vector<std::string> imsis;
    std::vector<std::string> tels;

    auto username_text = std::string(username);
    add_unique_candidate(imsis, user_part(username_text));

    if (username.rfind("+", 0) == 0 || username.rfind("tel:", 0) == 0) {
        add_unique_candidate(tels, user_part(username_text));
    }

    auto impi = username.find('@') != std::string_view::npos
        ? std::string(username)
        : std::format("{}@{}", username, realm);
    if (auto imsi = private_identity_imsi(impi); imsi) {
        add_unique_candidate(imsis, *imsi);
    }

    return find_one_by_imsi_or_tel(imsis, tels);
}

auto MongoSubscriberRepository::setSqn(std::string_view impi, uint64_t sqn) -> VoidResult {
    bson_t filter;
    bson_t update;
    bson_t set_doc;

    bson_init(&filter);
    bson_init(&update);

    auto imsi = private_identity_imsi(impi).value_or(std::string(impi));
    BSON_APPEND_UTF8(&filter, "imsi", imsi.c_str());
    BSON_APPEND_DOCUMENT_BEGIN(&update, "$set", &set_doc);
    BSON_APPEND_INT64(&set_doc, "auth.sqn", static_cast<int64_t>(sqn));
    bson_append_document_end(&update, &set_doc);

    auto result = update_one_with_filter(filter, update);

    bson_destroy(&update);
    bson_destroy(&filter);

    return result;
}

auto MongoSubscriberRepository::incrementSqn(std::string_view impi,
                                             uint64_t step,
                                             uint64_t mask) -> VoidResult {
    auto advanced = advanceSqn(impi, step, mask);
    if (!advanced) {
        return std::unexpected(advanced.error());
    }
    return {};
}

auto MongoSubscriberRepository::advanceSqn(std::string_view impi,
                                           uint64_t step,
                                           uint64_t mask) -> Result<uint64_t> {
    constexpr int kMaxRetries = 5;

    MongoClientGuard guard(client_);
    auto* collection = guard.collection();
    auto imsi = private_identity_imsi(impi).value_or(std::string(impi));

    for (int attempt = 0; attempt < kMaxRetries; ++attempt) {
        auto record_opt = find_one_by_field("imsi", imsi);
        if (!record_opt) {
            mongoc_collection_destroy(collection);
            return std::unexpected(record_opt.error());
        }
        if (!*record_opt) {
            mongoc_collection_destroy(collection);
            return std::unexpected(not_found("Subscriber not found for advanceSqn"));
        }

        const uint64_t old_sqn = (*record_opt)->auth.sqn;
        const uint64_t new_sqn = (old_sqn + step) & mask;

        bson_t filter;
        bson_t update;
        bson_t set_doc;

        bson_init(&filter);
        bson_init(&update);

        BSON_APPEND_UTF8(&filter, "imsi", imsi.c_str());
        BSON_APPEND_INT64(&filter, "auth.sqn", static_cast<int64_t>(old_sqn));

        BSON_APPEND_DOCUMENT_BEGIN(&update, "$set", &set_doc);
        BSON_APPEND_INT64(&set_doc, "auth.sqn", static_cast<int64_t>(new_sqn));
        bson_append_document_end(&update, &set_doc);

        bson_error_t error;
        bson_t reply;
        bson_init(&reply);

        const bool ok = mongoc_collection_update_one(
            collection, &filter, &update, nullptr, &reply, &error);
            
        bson_destroy(&update);
        bson_destroy(&filter);

        if (!ok) {
            bson_destroy(&reply);
            mongoc_collection_destroy(collection);
            return std::unexpected(db_error("Mongo update failed in advanceSqn", error.message));
        }

        const auto matched = parse_matched_count(reply);
        bson_destroy(&reply);

        if (matched > 0) {
            mongoc_collection_destroy(collection);
            return old_sqn;
        }

        // If matched == 0, it means CAS failed (another process updated it), retry
    }

    mongoc_collection_destroy(collection);
    return std::unexpected(db_error("Mongo advanceSqn exceeded max retries", "CAS conflict"));
}

auto MongoSubscriberRepository::setServingScscf(std::string_view impi,
                                                std::string_view scscf_uri) -> VoidResult {
    bson_t filter;
    bson_t update;
    bson_t set_doc;

    bson_init(&filter);
    bson_init(&update);

    auto imsi = private_identity_imsi(impi).value_or(std::string(impi));
    BSON_APPEND_UTF8(&filter, "imsi", imsi.c_str());
    BSON_APPEND_DOCUMENT_BEGIN(&update, "$set", &set_doc);
    BSON_APPEND_UTF8(&set_doc, "serving.assigned_scscf", std::string(scscf_uri).c_str());
    bson_append_document_end(&update, &set_doc);

    auto result = update_one_with_filter(filter, update);

    bson_destroy(&update);
    bson_destroy(&filter);

    return result;
}

auto MongoSubscriberRepository::find_one_by_imsi_or_tel(const std::vector<std::string>& imsis,
                                                        const std::vector<std::string>& tels) const
    -> Result<std::optional<SubscriberRecord>> {
    for (const auto& imsi : imsis) {
        auto result = find_one_by_field("imsi", imsi);
        if (!result || *result) {
            return result;
        }
    }

    for (const auto& tel : tels) {
        auto result = find_one_by_field("tel", tel);
        if (!result || *result) {
            return result;
        }
    }

    return std::optional<SubscriberRecord>{std::nullopt};
}

auto MongoSubscriberRepository::find_one_by_field(std::string_view field, std::string_view value) const
    -> Result<std::optional<SubscriberRecord>> {
    bson_t filter;
    bson_init(&filter);
    BSON_APPEND_UTF8(&filter, std::string(field).c_str(), std::string(value).c_str());

    auto result = find_one_with_filter(filter);
    bson_destroy(&filter);
    return result;
}

auto MongoSubscriberRepository::find_one_with_filter(const bson_t& filter) const
    -> Result<std::optional<SubscriberRecord>> {
    
    MongoClientGuard guard(client_);
    auto* collection = guard.collection();
    
    auto* cursor = mongoc_collection_find_with_opts(collection, &filter, nullptr, nullptr);
    if (cursor == nullptr) {
        mongoc_collection_destroy(collection);
        return std::unexpected(db_error("Mongo query cursor creation failed", "find_with_opts"));
    }

    const bson_t* doc = nullptr;
    if (!mongoc_cursor_next(cursor, &doc)) {
        bson_error_t error;
        if (mongoc_cursor_error(cursor, &error)) {
            mongoc_cursor_destroy(cursor);
            mongoc_collection_destroy(collection);
            return std::unexpected(db_error("Mongo query failed", error.message));
        }
        mongoc_cursor_destroy(cursor);
        mongoc_collection_destroy(collection);
        return std::optional<SubscriberRecord>{std::nullopt};
    }

    auto decoded = decodeSubscriber(*doc);
    mongoc_cursor_destroy(cursor);
    mongoc_collection_destroy(collection);
    
    if (!decoded) {
        return std::unexpected(decoded.error());
    }

    return std::optional<SubscriberRecord>{*decoded};
}

auto MongoSubscriberRepository::update_one_with_filter(const bson_t& filter, const bson_t& update)
    -> VoidResult {
    bson_error_t error;
    bson_t reply;
    bson_init(&reply);

    MongoClientGuard guard(client_);
    auto* collection = guard.collection();

    const bool ok = mongoc_collection_update_one(
        collection, &filter, &update, nullptr, &reply, &error);
    
    mongoc_collection_destroy(collection);
    
    if (!ok) {
        bson_destroy(&reply);
        return std::unexpected(db_error("Mongo update failed", error.message));
    }

    const auto matched = parse_matched_count(reply);
    bson_destroy(&reply);
    if (matched <= 0) {
        return std::unexpected(not_found("No subscriber matched for update"));
    }

    return {};
}

} // namespace ims::db
