#pragma once

#include "common/config.hpp"
#include "common/types.hpp"
#include "message.hpp"

#include <eXosip2/eXosip.h>
#include <memory>
#include <string>

namespace ims::sip {

using ExosipNativeContext = ::eXosip_t;

struct ExosipContextDeleter {
    void operator()(ExosipNativeContext* context) const;
};

using ExosipContextPtr = std::unique_ptr<ExosipNativeContext, ExosipContextDeleter>;

class ExosipContext {
public:
    explicit ExosipContext(ims::ExosipConfig config);

    auto ensureStarted() -> VoidResult;
    void shutdown();

    auto buildRequest(const std::string& method,
                      const std::string& request_uri,
                      const std::string& from_header) -> Result<SipMessage>;
    auto sendRequest(SipMessage request) -> Result<int>;
    auto waitForFinalResponse(int transaction_id, uint32_t timeout_ms) -> Result<int>;

    auto config() const -> const ims::ExosipConfig&;

private:
    auto transportProtocol() const -> Result<int>;
    auto secureMode() const -> int;
    auto addressFamily() const -> int;

    ExosipContextPtr context_;
    ims::ExosipConfig config_;
    bool started_ = false;
};

} // namespace ims::sip
