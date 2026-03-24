#include "exosip_context.hpp"

#include "common/logger.hpp"

#include <chrono>
#include <algorithm>
#include <cctype>
#include <format>
#include <netinet/in.h>
#include <osipparser2/osip_message.h>
#include <sys/socket.h>

namespace ims::sip {

namespace {

class ExosipLockGuard {
public:
    explicit ExosipLockGuard(ExosipNativeContext* context)
        : context_(context) {
        if (context_) {
            eXosip_lock(context_);
        }
    }

    ~ExosipLockGuard() {
        if (context_) {
            eXosip_unlock(context_);
        }
    }

    ExosipLockGuard(const ExosipLockGuard&) = delete;
    auto operator=(const ExosipLockGuard&) -> ExosipLockGuard& = delete;

private:
    ExosipNativeContext* context_;
};

struct ExosipEventDeleter {
    void operator()(eXosip_event_t* event) const {
        if (event) {
            eXosip_event_free(event);
        }
    }
};

using ExosipEventPtr = std::unique_ptr<eXosip_event_t, ExosipEventDeleter>;

auto normalize_transport(const std::string& transport) -> std::string {
    std::string normalized = transport;
    std::transform(normalized.begin(), normalized.end(), normalized.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return normalized;
}

auto to_upper_transport(const std::string& transport) -> std::string {
    std::string upper = transport;
    std::transform(upper.begin(), upper.end(), upper.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::toupper(ch)); });
    return upper;
}

} // namespace

void ExosipContextDeleter::operator()(ExosipNativeContext* context) const {
    if (context) {
        eXosip_quit(context);
    }
}

ExosipContext::ExosipContext(ims::ExosipConfig config)
    : config_(std::move(config)) {}

auto ExosipContext::ensureStarted() -> VoidResult {
    if (started_) {
        return {};
    }

    ExosipNativeContext* raw = eXosip_malloc();
    if (!raw) {
        return std::unexpected(ErrorInfo{
            ErrorCode::kInternalError,
            "Failed to allocate eXosip context"
        });
    }

    if (eXosip_init(raw) != 0) {
        eXosip_quit(raw);
        return std::unexpected(ErrorInfo{
            ErrorCode::kInternalError,
            "Failed to initialize eXosip context"
        });
    }

    context_.reset(raw);

    int use_rport = 1;
    eXosip_set_option(context_.get(), EXOSIP_OPT_USE_RPORT, &use_rport);
    eXosip_set_user_agent(context_.get(), config_.user_agent.c_str());

    auto transport_protocol = transportProtocol();
    if (!transport_protocol) {
        shutdown();
        return std::unexpected(transport_protocol.error());
    }

    if (eXosip_listen_addr(context_.get(), *transport_protocol,
                           config_.listen_addr.empty() ? nullptr : config_.listen_addr.c_str(),
                           static_cast<int>(config_.listen_port),
                           addressFamily(), secureMode()) != 0) {
        shutdown();
        return std::unexpected(ErrorInfo{
            ErrorCode::kSipTransportError,
            "Failed to bind eXosip listen socket",
            std::format("{}:{} via {}", config_.listen_addr, config_.listen_port, config_.transport)
        });
    }

    started_ = true;
    IMS_LOG_INFO("Started eXosip sidecar on {}:{} transport={}",
                 config_.listen_addr, config_.listen_port, config_.transport);
    return {};
}

void ExosipContext::shutdown() {
    if (!context_) {
        return;
    }

    context_.reset();
    started_ = false;
}

auto ExosipContext::buildRequest(const std::string& method,
                                 const std::string& request_uri,
                                 const std::string& from_header) -> Result<SipMessage> {
    auto start_result = ensureStarted();
    if (!start_result) {
        return std::unexpected(start_result.error());
    }

    osip_message_t* request = nullptr;
    {
        ExosipLockGuard lock(context_.get());
        if (eXosip_message_build_request(context_.get(), &request,
                                         method.c_str(),
                                         request_uri.c_str(),
                                         from_header.c_str(),
                                         nullptr) != 0 || !request) {
            return std::unexpected(ErrorInfo{
                ErrorCode::kSipTransactionFailed,
                "Failed to build request with eXosip",
                method
            });
        }
    }

    auto message = SipMessage(OsipMessagePtr(request));
    auto transport = to_upper_transport(normalize_transport(config_.transport));
    if (eXosip_transport_set(message.raw(), transport.c_str()) != 0) {
        return std::unexpected(ErrorInfo{
            ErrorCode::kSipTransportError,
            "Failed to set eXosip transport",
            transport
        });
    }

    return Result<SipMessage>(std::move(message));
}

auto ExosipContext::sendRequest(SipMessage request) -> Result<int> {
    auto start_result = ensureStarted();
    if (!start_result) {
        return std::unexpected(start_result.error());
    }

    auto* raw_request = request.release();
    ExosipLockGuard lock(context_.get());
    int transaction_id = eXosip_message_send_request(context_.get(), raw_request);
    if (transaction_id < 0) {
        osip_message_free(raw_request);
        return std::unexpected(ErrorInfo{
            ErrorCode::kSipTransactionFailed,
            "Failed to send request with eXosip"
        });
    }

    return Result<int>(transaction_id);
}

auto ExosipContext::waitForFinalResponse(int transaction_id, uint32_t timeout_ms) -> Result<int> {
    auto start_result = ensureStarted();
    if (!start_result) {
        return std::unexpected(start_result.error());
    }

    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    auto poll_ms = static_cast<int>(std::max<uint32_t>(config_.event_poll_ms, 10));

    while (std::chrono::steady_clock::now() < deadline) {
        ExosipEventPtr event(eXosip_event_wait(context_.get(), 0, poll_ms));
        {
            ExosipLockGuard lock(context_.get());
            eXosip_automatic_action(context_.get());
        }

        if (!event) {
            continue;
        }

        if (event->tid != transaction_id) {
            continue;
        }

        switch (event->type) {
        case EXOSIP_MESSAGE_ANSWERED:
        case EXOSIP_MESSAGE_REDIRECTED:
        case EXOSIP_MESSAGE_REQUESTFAILURE:
        case EXOSIP_MESSAGE_SERVERFAILURE:
        case EXOSIP_MESSAGE_GLOBALFAILURE:
        case EXOSIP_NOTIFICATION_ANSWERED:
        case EXOSIP_NOTIFICATION_REDIRECTED:
        case EXOSIP_NOTIFICATION_REQUESTFAILURE:
        case EXOSIP_NOTIFICATION_SERVERFAILURE:
        case EXOSIP_NOTIFICATION_GLOBALFAILURE:
            if (event->response) {
                return osip_message_get_status_code(event->response);
            }
            return Result<int>(0);
        default:
            break;
        }
    }

    return std::unexpected(ErrorInfo{
        ErrorCode::kSipTimeout,
        "Timed out waiting for eXosip response",
        std::to_string(transaction_id)
    });
}

auto ExosipContext::config() const -> const ims::ExosipConfig& {
    return config_;
}

auto ExosipContext::transportProtocol() const -> Result<int> {
    auto transport = normalize_transport(config_.transport);
    if (transport == "udp") {
        return Result<int>(IPPROTO_UDP);
    }
    if (transport == "tcp" || transport == "tls") {
        return Result<int>(IPPROTO_TCP);
    }

    return std::unexpected(ErrorInfo{
        ErrorCode::kConfigInvalidValue,
        "Unsupported eXosip transport",
        config_.transport
    });
}

auto ExosipContext::secureMode() const -> int {
    return normalize_transport(config_.transport) == "tls" ? 1 : 0;
}

auto ExosipContext::addressFamily() const -> int {
    return config_.listen_addr.find(':') != std::string::npos ? AF_INET6 : AF_INET;
}

} // namespace ims::sip
