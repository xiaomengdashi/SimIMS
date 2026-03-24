#pragma once
#include <expected>
#include <string>
#include <cstdint>
#include <string_view>
#include <vector>
#include <optional>
#include <chrono>
#include <functional>
#include <memory>
#include <format>

namespace ims {

enum class ErrorCode : uint32_t {
    kOk = 0,
    // Config errors
    kConfigFileNotFound = 1000,
    kConfigParseError,
    kConfigInvalidValue,
    // SIP errors
    kSipParseError = 2000,
    kSipTransactionFailed,
    kSipTransportError,
    kSipTimeout,
    kSipDialogNotFound,
    // Diameter errors
    kDiameterConnectFailed = 3000,
    kDiameterTimeout,
    kDiameterAuthFailed,
    kDiameterUserNotFound,
    // DNS errors
    kDnsResolveFailed = 4000,
    kDnsTimeout,
    kDnsNoRecords,
    // Media errors
    kMediaRtpengineError = 5000,
    kMediaSdpError,
    // Registration errors
    kRegistrationNotFound = 6000,
    kRegistrationExpired,
    // Internal errors
    kInternalError = 9000,
    kNotImplemented,
};

struct ErrorInfo {
    ErrorCode code;
    std::string message;
    std::string detail;

    ErrorInfo(ErrorCode c, std::string msg, std::string det = "")
        : code(c), message(std::move(msg)), detail(std::move(det)) {}
};

template<typename T>
using Result = std::expected<T, ErrorInfo>;

using VoidResult = Result<void>;

// Common type aliases
using Port = uint16_t;
using TimeDuration = std::chrono::milliseconds;
using TimePoint = std::chrono::steady_clock::time_point;

} // namespace ims
