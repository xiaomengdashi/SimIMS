#pragma once

#include "ims/common/types.hpp"

#include <osipparser2/osip_message.h>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace ims::sip {

struct OsipMessageDeleter {
    void operator()(osip_message_t* msg) const {
        if (msg) osip_message_free(msg);
    }
};

using OsipMessagePtr = std::unique_ptr<osip_message_t, OsipMessageDeleter>;

class SipMessage {
public:
    explicit SipMessage(OsipMessagePtr msg);

    static auto parse(const std::string& raw) -> Result<SipMessage>;
    static auto create() -> Result<SipMessage>;
    auto clone() const -> Result<SipMessage>;
    auto toString() const -> Result<std::string>;

    bool isRequest() const;
    bool isResponse() const;

    auto method() const -> std::string;
    auto requestUri() const -> std::string;
    void setRequestUri(const std::string& uri);

    auto statusCode() const -> int;
    auto reasonPhrase() const -> std::string;
    void setStatus(int code, const std::string& reason);

    auto getHeader(const std::string& name) const -> std::optional<std::string>;
    auto getHeaders(const std::string& name) const -> std::vector<std::string>;
    void addHeader(const std::string& name, const std::string& value);
    void removeHeader(const std::string& name);

    auto callId() const -> std::string;
    void setCallId(const std::string& value);

    auto fromHeader() const -> std::string;
    auto toHeader() const -> std::string;
    auto fromTag() const -> std::string;
    auto toTag() const -> std::string;
    void setToTag(const std::string& tag);

    auto cseq() const -> uint32_t;
    auto cseqMethod() const -> std::string;
    void setCSeq(uint32_t seq, const std::string& method);

    auto topVia() const -> std::string;
    void addVia(const std::string& via_str);
    void removeTopVia();
    auto viaCount() const -> int;
    auto viaBranch() const -> std::string;

    auto routes() const -> std::vector<std::string>;
    void addRoute(const std::string& route);
    void addRecordRoute(const std::string& rr);
    void removeTopRoute();

    auto contact() const -> std::optional<std::string>;
    void setContact(const std::string& contact_str);

    auto body() const -> std::optional<std::string>;
    void setBody(const std::string& body_str, const std::string& content_type);

    auto maxForwards() const -> int;
    void setMaxForwards(int value);
    void decrementMaxForwards();

    auto raw() const -> osip_message_t* { return msg_.get(); }

private:
    OsipMessagePtr msg_;
};

auto createResponse(const SipMessage& request, int status_code,
                    const std::string& reason) -> Result<SipMessage>;
auto generateBranch() -> std::string;
auto generateTag() -> std::string;
auto generateCallId(const std::string& host) -> std::string;

} // namespace ims::sip
