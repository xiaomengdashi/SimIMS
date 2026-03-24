# IMS Project - Claude Code Instructions

## Project Overview

VoNR-Only IMS system implementing P-CSCF, I-CSCF, S-CSCF in modern C++23.
This is a telecom/SIP signaling system, not a web application.

## Build System

- **CMake 3.22+**, C++23 standard
- Build: `cmake -B build && cmake --build build`
- Test: `cd build && ctest --output-on-failure`
- Dependencies: libosip2, boost (system), spdlog, yaml-cpp, c-ares, gtest

## Code Conventions

### Language & Style
- **C++23**: Use `std::expected`, `std::jthread`, `std::format`, concepts, designated initializers
- **Error handling**: Always use `Result<T>` (`std::expected<T, ErrorInfo>`), never throw exceptions in library code
- **RAII**: All C library resources (osip_message_t, ares_channel, etc.) must be wrapped in RAII types
- **Naming**: `snake_case` for variables/functions, `PascalCase` for types/classes, `kPascalCase` for enum values
- **Namespaces**: `ims::sip`, `ims::diameter`, `ims::media`, `ims::dns`, `ims::registration`, `ims::scscf`, `ims::icscf`, `ims::pcscf`

### Architecture Patterns
- **Dependency Injection**: All external deps injected via abstract interfaces (IHssClient, IPcfClient, IRtpengineClient, IRegistrationStore)
- **Interface locations**: Abstract interfaces in `include/ims/*/`, implementations in `src/*/`
- **Logging**: Use `IMS_LOG_*` macros (IMS_LOG_TRACE/DEBUG/INFO/WARN/ERROR/CRITICAL), never raw `std::cout`
- **Config**: YAML via yaml-cpp, config structs in `include/ims/common/config.hpp`

### File Organization
- Public headers: `include/ims/<module>/*.hpp`
- Private headers: `src/<module>/*.hpp` (implementation-specific)
- Sources: `src/<module>/*.cpp`
- Tests: `tests/unit/test_*.cpp`, mocks in `tests/mocks/mock_*.hpp`

### SIP Protocol
- SIP message manipulation goes through `SipMessage` class, never raw osip functions
- Transaction matching uses Via branch parameter
- Proxy operations (Via, Route, Record-Route) use `ProxyCore` helper
- Branch magic cookie: `z9hG4bK` prefix required (RFC 3261)

## Key Interfaces

```cpp
// HSS operations (Cx Diameter)
IHssClient: userAuthorization, multimediaAuth, serverAssignment, locationInfo

// PCF operations (Rx Diameter)
IPcfClient: authorizeSession, terminateSession

// Media proxy (rtpengine NG)
IRtpengineClient: offer, answer, deleteSession, query, ping

// Registration storage
IRegistrationStore: store, lookup, remove, purgeExpired, isRegistered
```

## Testing

- Use Google Test + Google Mock
- Mocks available in `tests/mocks/` for all abstract interfaces
- Test naming: `TEST_F(ClassTest, MethodBehavior)`
- SIP test messages: define as `static constexpr` in test fixtures

## Important Notes

- This is a **SIP proxy** system, NOT a SIP UA (user agent). Don't use libeXosip2 (it's UA-mode only).
- The system handles SIP messages statelessly where possible, using transactions only where required by RFC 3261.
- Diameter stubs (`StubHssClient`, `StubPcfClient`) exist for development; replace with freeDiameter for production.
- rtpengine is an external process; we control it via bencode-over-UDP (NG protocol).
- Default ports: P-CSCF=5060, I-CSCF=5061, S-CSCF=5062, Diameter=3868, rtpengine=22222
