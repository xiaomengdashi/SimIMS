#pragma once

#include "common/types.hpp"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace ims::dns {

/// DNS A/AAAA record result
struct AddressRecord {
    std::string hostname;
    std::string address;  // IPv4 or IPv6 string
    uint32_t ttl = 0;
};

/// DNS SRV record result
struct SrvRecord {
    std::string service;
    uint16_t priority = 0;
    uint16_t weight = 0;
    uint16_t port = 0;
    std::string target;
    uint32_t ttl = 0;
};

/// DNS NAPTR record result
struct NaptrRecord {
    uint16_t order = 0;
    uint16_t preference = 0;
    std::string flags;
    std::string service;
    std::string regexp;
    std::string replacement;
    uint32_t ttl = 0;
};

/// SIP routing result from NAPTR/SRV/A resolution chain
struct SipRouteResult {
    std::string address;
    uint16_t port = 5060;
    std::string transport;  // "udp", "tcp", "tls"
};

/// DNS resolver configuration
struct ResolverConfig {
    std::vector<std::string> servers;  // DNS server addresses
    uint32_t timeout_ms = 3000;
    uint32_t tries = 2;
};

/// Asynchronous DNS resolver using c-ares
///
/// Provides SIP-specific resolution: NAPTR -> SRV -> A/AAAA chain
/// for locating SIP servers by domain name.
class DnsResolver {
public:
    explicit DnsResolver(const ResolverConfig& config);
    ~DnsResolver();

    // Non-copyable, movable
    DnsResolver(const DnsResolver&) = delete;
    DnsResolver& operator=(const DnsResolver&) = delete;
    DnsResolver(DnsResolver&&) noexcept;
    DnsResolver& operator=(DnsResolver&&) noexcept;

    /// Resolve A records (IPv4)
    auto resolveA(const std::string& hostname) -> Result<std::vector<AddressRecord>>;

    /// Resolve AAAA records (IPv6)
    auto resolveAAAA(const std::string& hostname) -> Result<std::vector<AddressRecord>>;

    /// Resolve SRV records
    auto resolveSRV(const std::string& service) -> Result<std::vector<SrvRecord>>;

    /// Resolve NAPTR records
    auto resolveNAPTR(const std::string& domain) -> Result<std::vector<NaptrRecord>>;

    /// Full SIP URI resolution: NAPTR -> SRV -> A/AAAA
    /// Returns ordered list of target addresses to try
    auto resolveSipUri(const std::string& domain,
                       const std::string& transport = "udp") -> Result<std::vector<SipRouteResult>>;

    /// Process pending DNS queries (call from event loop)
    void processEvents();

    /// Get file descriptors for select/poll integration
    auto getActiveSockets() const -> std::vector<int>;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace ims::dns
