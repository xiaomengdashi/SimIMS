#include "db/mongo_subscriber_repository.hpp"

#include "db/subscriber_codec.hpp"
#include "sip/uri_utils.hpp"

#include <bson/bson.h>
#include <mongoc/mongoc.h>

#include <format>
#include <string>

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

auto extract_old_sqn_from_value(const bson_t& reply) -> Result<uint64_t> {
    bson_iter_t value_iter;
    if (!bson_iter_init_find(&value_iter, &reply, "value")) {
        return std::unexpected(db_error("Mongo findAndModify reply missing value", "value"));
    }
    if (BSON_ITER_HOLDS_NULL(&value_iter)) {
        return std::unexpected(not_found("No subscriber matched in advanceSqn"));
    }
    if (!BSON_ITER_HOLDS_DOCUMENT(&value_iter)) {
        return std::unexpected(db_error("Mongo findAndModify value is not document", "value"));
    }

    const uint8_t* value_data = nullptr;
    uint32_t value_len = 0;
    bson_iter_document(&value_iter, &value_len, &value_data);

    bson_t value_doc;
    if (!bson_init_static(&value_doc, value_data, value_len)) {
        return std::unexpected(db_error("Failed to parse findAndModify value document", "value"));
    }

    bson_iter_t auth_iter;
    if (!bson_iter_init_find(&auth_iter, &value_doc, "auth") || !BSON_ITER_HOLDS_DOCUMENT(&auth_iter)) {
        return std::unexpected(db_error("auth field missing in findAndModify value", "auth"));
    }

    const uint8_t* auth_data = nullptr;
    uint32_t auth_len = 0;
    bson_iter_document(&auth_iter, &auth_len, &auth_data);

    bson_t auth_doc;
    if (!bson_init_static(&auth_doc, auth_data, auth_len)) {
        return std::unexpected(db_error("Failed to parse auth document", "auth"));
    }

    bson_iter_t sqn_iter;
    if (!bson_iter_init_find(&sqn_iter, &auth_doc, "sqn")) {
        return std::unexpected(db_error("sqn field missing in auth document", "auth.sqn"));
    }

    if (BSON_ITER_HOLDS_INT64(&sqn_iter)) {
        const auto sqn = bson_iter_int64(&sqn_iter);
        if (sqn < 0) {
            return std::unexpected(db_error("sqn must be non-negative", "auth.sqn"));
        }
        return static_cast<uint64_t>(sqn);
    }
    if (BSON_ITER_HOLDS_INT32(&sqn_iter)) {
        const auto sqn = bson_iter_int32(&sqn_iter);
        if (sqn < 0) {
            return std::unexpected(db_error("sqn must be non-negative", "auth.sqn"));
        }
        return static_cast<uint64_t>(sqn);
    }

    return std::unexpected(db_error("sqn field must be integer", "auth.sqn"));
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
    if (!impi.empty()) {
        auto by_impi = find_one_by_field("identities.impi", impi);
        if (!by_impi) {
            return std::unexpected(by_impi.error());
        }
        if (*by_impi) {
            return by_impi;
        }
    }

    if (!impu.empty()) {
        auto normalized_impu = ims::sip::normalize_impu_uri(std::string(impu));

        auto by_canonical = find_one_by_field("identities.canonical_impu", normalized_impu);
        if (!by_canonical) {
            return std::unexpected(by_canonical.error());
        }
        if (*by_canonical) {
            return by_canonical;
        }

        auto by_associated = find_one_by_field("identities.associated_impus", normalized_impu);
        if (!by_associated) {
            return std::unexpected(by_associated.error());
        }
        if (*by_associated) {
            return by_associated;
        }
    }

    return std::optional<SubscriberRecord>{std::nullopt};
}

auto MongoSubscriberRepository::findByIdentity(std::string_view identity) const
    -> Result<std::optional<SubscriberRecord>> {
    auto normalized_identity = ims::sip::normalize_impu_uri(std::string(identity));

    auto by_impi = find_one_by_field("identities.impi", normalized_identity);
    if (!by_impi || *by_impi) {
        return by_impi;
    }

    auto by_canonical = find_one_by_field("identities.canonical_impu", normalized_identity);
    if (!by_canonical || *by_canonical) {
        return by_canonical;
    }

    return find_one_by_field("identities.associated_impus", normalized_identity);
}

auto MongoSubscriberRepository::findByUsernameRealm(std::string_view username,
                                                    std::string_view realm) const
    -> Result<std::optional<SubscriberRecord>> {
    const auto impi = username.find('@') != std::string_view::npos
        ? std::string(username)
        : std::format("{}@{}", username, realm);

    auto by_impi = find_one_by_field("identities.impi", impi);
    if (!by_impi || *by_impi) {
        return by_impi;
    }

    bson_t filter;
    bson_init(&filter);
    BSON_APPEND_UTF8(&filter, "identities.username", std::string(username).c_str());
    BSON_APPEND_UTF8(&filter, "identities.realm", std::string(realm).c_str());

    auto result = find_one_with_filter(filter);
    bson_destroy(&filter);
    return result;
}

auto MongoSubscriberRepository::setSqn(std::string_view impi, uint64_t sqn) -> VoidResult {
    bson_t filter;
    bson_t update;
    bson_t set_doc;

    bson_init(&filter);
    bson_init(&update);

    BSON_APPEND_UTF8(&filter, "identities.impi", std::string(impi).c_str());
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
    bson_t query;
    bson_t update;
    bson_t inc_doc;
    bson_t bit_doc;
    bson_t and_doc;

    bson_init(&query);
    bson_init(&update);

    BSON_APPEND_UTF8(&query, "identities.impi", std::string(impi).c_str());

    BSON_APPEND_DOCUMENT_BEGIN(&update, "$inc", &inc_doc);
    BSON_APPEND_INT64(&inc_doc, "auth.sqn", static_cast<int64_t>(step));
    bson_append_document_end(&update, &inc_doc);

    BSON_APPEND_DOCUMENT_BEGIN(&update, "$bit", &bit_doc);
    BSON_APPEND_DOCUMENT_BEGIN(&bit_doc, "auth.sqn", &and_doc);
    BSON_APPEND_INT64(&and_doc, "and", static_cast<int64_t>(mask));
    bson_append_document_end(&bit_doc, &and_doc);
    bson_append_document_end(&update, &bit_doc);

    auto* opts = mongoc_find_and_modify_opts_new();
    mongoc_find_and_modify_opts_set_update(opts, &update);
    mongoc_find_and_modify_opts_set_flags(opts, MONGOC_FIND_AND_MODIFY_NONE);

    bson_t reply;
    bson_init(&reply);
    bson_error_t error;

    const bool ok = mongoc_collection_find_and_modify_with_opts(
        client_->collection(), &query, opts, &reply, &error);

    mongoc_find_and_modify_opts_destroy(opts);
    bson_destroy(&update);
    bson_destroy(&query);

    if (!ok) {
        bson_destroy(&reply);
        return std::unexpected(db_error("Mongo advanceSqn failed", error.message));
    }

    auto old_sqn = extract_old_sqn_from_value(reply);
    bson_destroy(&reply);
    return old_sqn;
}

auto MongoSubscriberRepository::setServingScscf(std::string_view impi,
                                                std::string_view scscf_uri) -> VoidResult {
    bson_t filter;
    bson_t update;
    bson_t set_doc;

    bson_init(&filter);
    bson_init(&update);

    BSON_APPEND_UTF8(&filter, "identities.impi", std::string(impi).c_str());
    BSON_APPEND_DOCUMENT_BEGIN(&update, "$set", &set_doc);
    BSON_APPEND_UTF8(&set_doc, "serving.assigned_scscf", std::string(scscf_uri).c_str());
    bson_append_document_end(&update, &set_doc);

    auto result = update_one_with_filter(filter, update);

    bson_destroy(&update);
    bson_destroy(&filter);

    return result;
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
    auto* cursor = mongoc_collection_find_with_opts(client_->collection(), &filter, nullptr, nullptr);
    if (cursor == nullptr) {
        return std::unexpected(db_error("Mongo query cursor creation failed", "find_with_opts"));
    }

    const bson_t* doc = nullptr;
    if (!mongoc_cursor_next(cursor, &doc)) {
        bson_error_t error;
        if (mongoc_cursor_error(cursor, &error)) {
            mongoc_cursor_destroy(cursor);
            return std::unexpected(db_error("Mongo query failed", error.message));
        }
        mongoc_cursor_destroy(cursor);
        return std::optional<SubscriberRecord>{std::nullopt};
    }

    auto decoded = decodeSubscriber(*doc);
    mongoc_cursor_destroy(cursor);
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

    const bool ok = mongoc_collection_update_one(
        client_->collection(), &filter, &update, nullptr, &reply, &error);
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
