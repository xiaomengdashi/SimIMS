#include "message.hpp"
#include "common/logger.hpp"
#include <osipparser2/osip_port.h>
#include <osipparser2/osip_parser.h>
#include <osipparser2/osip_headers.h>
#include <random>
#include <sstream>
#include <iomanip>
#include <cstring>

namespace ims::sip {

// Global parser initialization - must be called before any osip parsing
namespace {
    struct ParserInit {
        ParserInit() {
            parser_init();
        }
    };
    static ParserInit g_parser_init;
}

namespace {

auto random_hex(int length) -> std::string {
    static thread_local std::mt19937 rng{std::random_device{}()};
    std::uniform_int_distribution<int> dist(0, 15);
    std::ostringstream oss;
    for (int i = 0; i < length; ++i) {
        oss << std::hex << dist(rng);
    }
    return oss.str();
}

auto trim(std::string value) -> std::string {
    auto is_space = [](unsigned char ch) { return std::isspace(ch) != 0; };
    value.erase(value.begin(), std::find_if(value.begin(), value.end(),
                                            [&](unsigned char ch) { return !is_space(ch); }));
    value.erase(std::find_if(value.rbegin(), value.rend(),
                             [&](unsigned char ch) { return !is_space(ch); }).base(),
                value.end());
    return value;
}

auto uri_to_string(osip_uri_t* uri) -> std::string {
    if (!uri) return {};
    char* buf = nullptr;
    if (osip_uri_to_str(uri, &buf) == 0 && buf) {
        std::string result(buf);
        osip_free(buf);
        return result;
    }
    return {};
}

auto from_to_string(osip_from_t* header) -> std::string {
    if (!header) return {};
    char* buf = nullptr;
    if (osip_from_to_str(header, &buf) == 0 && buf) {
        std::string result(buf);
        osip_free(buf);
        return result;
    }
    return {};
}

auto via_to_string(osip_via_t* via) -> std::string {
    if (!via) return {};
    char* buf = nullptr;
    if (osip_via_to_str(via, &buf) == 0 && buf) {
        std::string result(buf);
        osip_free(buf);
        return result;
    }
    return {};
}

auto contact_to_string(osip_contact_t* contact) -> std::string {
    if (!contact) return {};
    char* buf = nullptr;
    if (osip_contact_to_str(contact, &buf) == 0 && buf) {
        std::string result(buf);
        osip_free(buf);
        return result;
    }
    return {};
}

auto record_route_to_string(osip_record_route_t* record_route) -> std::string {
    if (!record_route) return {};
    char* buf = nullptr;
    if (osip_record_route_to_str(record_route, &buf) == 0 && buf) {
        std::string result(buf);
        osip_free(buf);
        return result;
    }
    return {};
}

auto split_header_values(const std::string& value) -> std::vector<std::string> {
    std::vector<std::string> parts;
    std::string current;
    bool in_quotes = false;
    int angle_depth = 0;

    for (char ch : value) {
        if (ch == '"') {
            in_quotes = !in_quotes;
            current.push_back(ch);
            continue;
        }
        if (!in_quotes) {
            if (ch == '<') {
                ++angle_depth;
            } else if (ch == '>' && angle_depth > 0) {
                --angle_depth;
            } else if (ch == ',' && angle_depth == 0) {
                auto part = trim(current);
                if (!part.empty()) {
                    parts.push_back(std::move(part));
                }
                current.clear();
                continue;
            }
        }
        current.push_back(ch);
    }

    auto part = trim(current);
    if (!part.empty()) {
        parts.push_back(std::move(part));
    }
    return parts;
}

} // anonymous namespace

SipMessage::SipMessage(OsipMessagePtr msg)
    : msg_(std::move(msg)) {}

auto SipMessage::parse(const std::string& raw) -> Result<SipMessage> {
    osip_message_t* msg = nullptr;
    if (osip_message_init(&msg) != 0) {
        return std::unexpected(ErrorInfo{
            ErrorCode::kSipParseError, "Failed to init osip message"});
    }

    OsipMessagePtr ptr(msg);
    if (osip_message_parse(msg, raw.c_str(), raw.size()) != 0) {
        return std::unexpected(ErrorInfo{
            ErrorCode::kSipParseError, "Failed to parse SIP message"});
    }

    return SipMessage(std::move(ptr));
}

auto SipMessage::create() -> Result<SipMessage> {
    osip_message_t* msg = nullptr;
    if (osip_message_init(&msg) != 0) {
        return std::unexpected(ErrorInfo{
            ErrorCode::kSipParseError, "Failed to init osip message"});
    }
    return SipMessage(OsipMessagePtr(msg));
}

auto SipMessage::clone() const -> Result<SipMessage> {
    osip_message_t* cloned = nullptr;
    if (osip_message_clone(msg_.get(), &cloned) != 0) {
        return std::unexpected(ErrorInfo{
            ErrorCode::kSipParseError, "Failed to clone SIP message"});
    }
    return SipMessage(OsipMessagePtr(cloned));
}

auto SipMessage::toString() const -> Result<std::string> {
    char* buf = nullptr;
    size_t len = 0;
    if (osip_message_to_str(msg_.get(), &buf, &len) != 0) {
        return std::unexpected(ErrorInfo{
            ErrorCode::kSipParseError, "Failed to serialize SIP message"});
    }
    std::string result(buf, len);
    osip_free(buf);
    return result;
}

bool SipMessage::isRequest() const {
    return MSG_IS_REQUEST(msg_.get());
}

bool SipMessage::isResponse() const {
    return MSG_IS_RESPONSE(msg_.get());
}

auto SipMessage::method() const -> std::string {
    if (!isRequest()) return {};
    char* method = osip_message_get_method(msg_.get());
    if (!method) return {};
    return method;  // Returns internal pointer, do NOT free
}

void SipMessage::setMethod(const std::string& method) {
    osip_message_set_method(msg_.get(), osip_strdup(method.c_str()));
}

auto SipMessage::statusCode() const -> int {
    if (!isResponse()) return 0;
    return osip_message_get_status_code(msg_.get());
}

auto SipMessage::reasonPhrase() const -> std::string {
    if (!isResponse()) return {};
    char* reason = osip_message_get_reason_phrase(msg_.get());
    if (!reason) return {};
    return reason;  // Returns internal pointer, do NOT free
}

auto SipMessage::requestUri() const -> std::string {
    osip_uri_t* uri = osip_message_get_uri(msg_.get());
    return uri_to_string(uri);
}

void SipMessage::setRequestUri(const std::string& uri) {
    osip_uri_t* parsed = nullptr;
    osip_uri_init(&parsed);
    osip_uri_parse(parsed, uri.c_str());
    osip_message_set_uri(msg_.get(), parsed);
}

void SipMessage::setStatus(int code, const std::string& reason) {
    osip_message_set_status_code(msg_.get(), code);
    osip_message_set_reason_phrase(msg_.get(), osip_strdup(reason.c_str()));
}

auto SipMessage::getHeader(const std::string& name) const -> std::optional<std::string> {
    osip_header_t* header = nullptr;
    int pos = 0;
    // osip_message_header_get_byname returns position (>=0) if found, -1 if not
    if (osip_message_header_get_byname(msg_.get(), name.c_str(), pos, &header) >= 0 && header) {
        if (header->hvalue) return std::string(header->hvalue);
        return std::string{};
    }
    return std::nullopt;
}

auto SipMessage::getHeaders(const std::string& name) const -> std::vector<std::string> {
    std::vector<std::string> result;

    if (name == "Record-Route" || name == "record-route") {
        osip_record_route_t* record_route = nullptr;
        int pos = 0;
        while (osip_message_get_record_route(msg_.get(), pos, &record_route) == 0 && record_route) {
            auto value = record_route_to_string(record_route);
            auto parts = split_header_values(value);
            result.insert(result.end(), parts.begin(), parts.end());
            ++pos;
        }
        return result;
    }

    osip_header_t* header = nullptr;
    int pos = 0;
    // osip_message_header_get_byname returns position (>=0) if found, -1 if not
    while (osip_message_header_get_byname(msg_.get(), name.c_str(), pos, &header) >= 0 && header) {
        if (header->hvalue) {
            result.emplace_back(header->hvalue);
        }
        ++pos;
    }
    return result;
}

void SipMessage::addHeader(const std::string& name, const std::string& value) {
    osip_message_set_header(msg_.get(), name.c_str(), value.c_str());
}

void SipMessage::removeHeader(const std::string& name) {
    osip_header_t* header = nullptr;
    int pos = 0;
    // osip_message_header_get_byname returns position (>=0) if found, -1 if not
    while (osip_message_header_get_byname(msg_.get(), name.c_str(), pos, &header) >= 0 && header) {
        osip_list_remove(&msg_->headers, pos);
        osip_header_free(header);
        header = nullptr;
        // Don't increment pos since we removed from the list
    }
}

auto SipMessage::callId() const -> std::string {
    osip_call_id_t* call_id = osip_message_get_call_id(msg_.get());
    if (!call_id || !call_id->number) return {};
    // Return full Call-ID: number@host if host exists
    if (call_id->host && call_id->host[0] != '\0') {
        return std::string(call_id->number) + "@" + call_id->host;
    }
    return call_id->number;
}

void SipMessage::setCallId(const std::string& value) {
    osip_message_set_call_id(msg_.get(), value.c_str());
}

auto SipMessage::fromHeader() const -> std::string {
    osip_from_t* from = osip_message_get_from(msg_.get());
    return from_to_string(from);
}

void SipMessage::setFromHeader(const std::string& from) {
    osip_from_t* parsed = nullptr;
    if (osip_from_init(&parsed) != 0) return;
    if (osip_from_parse(parsed, from.c_str()) != 0) {
        osip_from_free(parsed);
        return;
    }

    if (msg_->from) {
        osip_from_free(msg_->from);
    }
    msg_->from = parsed;
}

auto SipMessage::toHeader() const -> std::string {
    osip_to_t* to = osip_message_get_to(msg_.get());
    return from_to_string(to);
}

void SipMessage::setToHeader(const std::string& to) {
    osip_to_t* parsed = nullptr;
    if (osip_to_init(&parsed) != 0) return;
    if (osip_to_parse(parsed, to.c_str()) != 0) {
        osip_to_free(parsed);
        return;
    }

    if (msg_->to) {
        osip_to_free(msg_->to);
    }
    msg_->to = parsed;
}

auto SipMessage::fromTag() const -> std::string {
    osip_from_t* from = osip_message_get_from(msg_.get());
    if (from) {
        osip_generic_param_t* tag = nullptr;
        osip_from_get_tag(from, &tag);
        if (tag && tag->gvalue) return tag->gvalue;
    }
    return {};
}

auto SipMessage::toTag() const -> std::string {
    osip_to_t* to = osip_message_get_to(msg_.get());
    if (to) {
        osip_generic_param_t* tag = nullptr;
        osip_to_get_tag(to, &tag);
        if (tag && tag->gvalue) return tag->gvalue;
    }
    return {};
}

void SipMessage::setToTag(const std::string& tag) {
    osip_to_t* to = osip_message_get_to(msg_.get());
    if (to) {
        osip_to_set_tag(to, osip_strdup(tag.c_str()));
    }
}

auto SipMessage::cseq() const -> uint32_t {
    osip_cseq_t* cseq = osip_message_get_cseq(msg_.get());
    if (cseq && cseq->number) {
        return static_cast<uint32_t>(std::stoul(cseq->number));
    }
    return 0;
}

auto SipMessage::cseqMethod() const -> std::string {
    osip_cseq_t* cseq = osip_message_get_cseq(msg_.get());
    if (cseq && cseq->method) {
        return cseq->method;
    }
    return {};
}

void SipMessage::setCSeq(uint32_t seq, const std::string& method) {
    auto cseq_value = std::to_string(seq) + " " + method;
    osip_message_set_cseq(msg_.get(), cseq_value.c_str());
}

auto SipMessage::topVia() const -> std::string {
    osip_via_t* via = nullptr;
    if (osip_message_get_via(msg_.get(), 0, &via) == 0) {
        return via_to_string(via);
    }
    return {};
}

void SipMessage::addVia(const std::string& via_str) {
    // Parse the Via header
    osip_via_t* via = nullptr;
    if (osip_via_init(&via) != 0) return;
    if (osip_via_parse(via, via_str.c_str()) != 0) {
        osip_via_free(via);
        return;
    }
    // Insert at position 0 (top of list) per RFC 3261
    osip_list_add(&msg_->vias, via, 0);
}

void SipMessage::removeTopVia() {
    osip_via_t* via = nullptr;
    if (osip_message_get_via(msg_.get(), 0, &via) == 0 && via) {
        osip_list_remove(&msg_->vias, 0);
        osip_via_free(via);
    }
}

auto SipMessage::viaCount() const -> int {
    return osip_list_size(&msg_->vias);
}

auto SipMessage::viaBranch() const -> std::string {
    osip_via_t* via = nullptr;
    if (osip_message_get_via(msg_.get(), 0, &via) == 0 && via) {
        osip_generic_param_t* branch = nullptr;
        osip_via_param_get_byname(via, const_cast<char*>("branch"), &branch);
        if (branch && branch->gvalue) return branch->gvalue;
    }
    return {};
}

auto SipMessage::routes() const -> std::vector<std::string> {
    std::vector<std::string> result;
    osip_route_t* route = nullptr;
    int pos = 0;
    while (osip_message_get_route(msg_.get(), pos, &route) == 0 && route) {
        char* buf = nullptr;
        if (osip_route_to_str(route, &buf) == 0 && buf) {
            result.emplace_back(buf);
            osip_free(buf);
        }
        ++pos;
    }
    return result;
}

void SipMessage::addRoute(const std::string& route) {
    osip_message_set_route(msg_.get(), route.c_str());
}

void SipMessage::addRecordRoute(const std::string& rr) {
    osip_message_set_record_route(msg_.get(), rr.c_str());
}

void SipMessage::removeTopRoute() {
    osip_route_t* route = nullptr;
    if (osip_message_get_route(msg_.get(), 0, &route) == 0 && route) {
        osip_list_remove(&msg_->routes, 0);
        osip_route_free(route);
    }
}

auto SipMessage::contact() const -> std::optional<std::string> {
    osip_contact_t* ct = nullptr;
    if (osip_message_get_contact(msg_.get(), 0, &ct) == 0 && ct) {
        return contact_to_string(ct);
    }
    return std::nullopt;
}

void SipMessage::setContact(const std::string& contact_str) {
    // Remove existing contacts
    while (osip_list_size(&msg_->contacts) > 0) {
        osip_contact_t* ct = nullptr;
        osip_message_get_contact(msg_.get(), 0, &ct);
        if (ct) {
            osip_list_remove(&msg_->contacts, 0);
            osip_contact_free(ct);
        } else {
            break;
        }
    }
    osip_message_set_contact(msg_.get(), contact_str.c_str());
}

auto SipMessage::body() const -> std::optional<std::string> {
    osip_body_t* b = nullptr;
    if (osip_message_get_body(msg_.get(), 0, &b) == 0 && b && b->body) {
        return std::string(b->body, b->length);
    }
    return std::nullopt;
}

void SipMessage::setBody(const std::string& body_str, const std::string& content_type) {
    // Remove existing bodies
    while (osip_list_size(&msg_->bodies) > 0) {
        osip_body_t* b = nullptr;
        osip_message_get_body(msg_.get(), 0, &b);
        if (b) {
            osip_list_remove(&msg_->bodies, 0);
            osip_body_free(b);
        } else {
            break;
        }
    }
    osip_message_set_body(msg_.get(), body_str.c_str(), body_str.size());
    osip_message_set_content_type(msg_.get(), content_type.c_str());
    auto len_str = std::to_string(body_str.size());
    // Remove old content-length and set new
    if (msg_->content_length) {
        osip_content_length_free(msg_->content_length);
        msg_->content_length = nullptr;
    }
    osip_message_set_content_length(msg_.get(), len_str.c_str());
}

auto SipMessage::maxForwards() const -> int {
    // In osip2, Max-Forwards is stored as a generic header with lowercase name
    // osip_message_header_get_byname returns the position (>=0) if found, -1 if not
    osip_header_t* mf = nullptr;
    int pos = 0;
    int result = osip_message_header_get_byname(msg_.get(), "max-forwards", pos, &mf);
    if (result >= 0 && mf && mf->hvalue) {
        return std::stoi(mf->hvalue);
    }
    return -1;
}

void SipMessage::setMaxForwards(int value) {
    removeHeader("max-forwards");
    auto val_str = std::to_string(value);
    addHeader("Max-Forwards", val_str);
}

void SipMessage::decrementMaxForwards() {
    int mf = maxForwards();
    if (mf > 0) {
        setMaxForwards(mf - 1);
    }
}

// Free functions

auto createResponse(const SipMessage& request, int status_code,
                    const std::string& reason) -> Result<SipMessage> {
    auto result = SipMessage::create();
    if (!result) return std::unexpected(result.error());

    auto& resp = *result;
    resp.setStatus(status_code, reason);

    // Copy Via headers
    for (int i = request.viaCount() - 1; i >= 0; --i) {
        osip_via_t* via = nullptr;
        if (osip_message_get_via(request.raw(), i, &via) == 0 && via) {
            char* buf = nullptr;
            if (osip_via_to_str(via, &buf) == 0 && buf) {
                resp.addVia(buf);
                osip_free(buf);
            }
        }
    }

    // Copy From
    if (request.raw()->from) {
        osip_from_t* from_clone = nullptr;
        osip_from_clone(request.raw()->from, &from_clone);
        resp.raw()->from = from_clone;
    }

    // Copy To
    if (request.raw()->to) {
        osip_to_t* to_clone = nullptr;
        osip_to_clone(request.raw()->to, &to_clone);
        resp.raw()->to = to_clone;
    }

    // RFC 3261 8.2.6.2: all non-100 responses need a To-tag
    if (status_code > 100 && resp.toTag().empty()) {
        resp.setToTag(generateTag());
    }

    // Copy Call-ID
    if (request.raw()->call_id) {
        osip_call_id_t* cid_clone = nullptr;
        osip_call_id_clone(request.raw()->call_id, &cid_clone);
        resp.raw()->call_id = cid_clone;
    }

    // Copy CSeq
    if (request.raw()->cseq) {
        osip_cseq_t* cseq_clone = nullptr;
        osip_cseq_clone(request.raw()->cseq, &cseq_clone);
        resp.raw()->cseq = cseq_clone;
    }

    return result;
}

auto createRequest(const std::string& method, const std::string& request_uri) -> Result<SipMessage> {
    auto result = SipMessage::create();
    if (!result) return std::unexpected(result.error());

    auto& req = *result;
    osip_message_set_version(req.raw(), osip_strdup("SIP/2.0"));
    req.setMethod(method);
    req.setRequestUri(request_uri);
    return result;
}

auto generateBranch() -> std::string {
    return "z9hG4bK" + random_hex(16);
}

auto generateTag() -> std::string {
    return random_hex(8);
}

auto generateCallId(const std::string& host) -> std::string {
    return random_hex(16) + "@" + host;
}

} // namespace ims::sip
