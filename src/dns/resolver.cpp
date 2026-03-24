#include "dns/resolver.hpp"
#include "common/logger.hpp"

#include <ares.h>
#include <arpa/nameser.h>
#include <netdb.h>
#include <arpa/inet.h>

#include <algorithm>
#include <cstring>
#include <mutex>
#include <format>

namespace ims::dns {

// ========== Implementation ==========

struct DnsResolver::Impl {
    ares_channel channel = nullptr;
    ResolverConfig config;

    // Callback result storage
    struct QueryResult {
        int status = ARES_ENOTINITIALIZED;
        std::vector<AddressRecord> addresses;
        std::vector<SrvRecord> srvs;
        std::vector<NaptrRecord> naptrs;
        bool done = false;
    };

    Impl(const ResolverConfig& cfg) : config(cfg) {}

    auto init() -> VoidResult {
        int status = ares_library_init(ARES_LIB_INIT_ALL);
        if (status != ARES_SUCCESS) {
            return std::unexpected(ErrorInfo{
                ErrorCode::kDnsResolveFailed,
                "Failed to init c-ares library",
                ares_strerror(status)
            });
        }

        struct ares_options options {};
        int optmask = 0;

        options.timeout = config.timeout_ms;
        optmask |= ARES_OPT_TIMEOUTMS;
        options.tries = config.tries;
        optmask |= ARES_OPT_TRIES;

        status = ares_init_options(&channel, &options, optmask);
        if (status != ARES_SUCCESS) {
            return std::unexpected(ErrorInfo{
                ErrorCode::kDnsResolveFailed,
                "Failed to init ares channel",
                ares_strerror(status)
            });
        }

        // Set custom DNS servers if configured
        if (!config.servers.empty()) {
            std::string servers_str;
            for (size_t i = 0; i < config.servers.size(); ++i) {
                if (i > 0) servers_str += ",";
                servers_str += config.servers[i];
            }
            status = ares_set_servers_csv(channel, servers_str.c_str());
            if (status != ARES_SUCCESS) {
                IMS_LOG_WARN("Failed to set DNS servers: {}", ares_strerror(status));
            }
        }

        return {};
    }

    void destroy() {
        if (channel) {
            ares_destroy(channel);
            channel = nullptr;
        }
        ares_library_cleanup();
    }

    // Wait for query completion with simple poll loop
    void waitForCompletion() {
        int nfds;
        fd_set read_fds, write_fds;
        struct timeval tv;

        while (true) {
            FD_ZERO(&read_fds);
            FD_ZERO(&write_fds);
            nfds = ares_fds(channel, &read_fds, &write_fds);
            if (nfds == 0) break;

            struct timeval* tvp = ares_timeout(channel, nullptr, &tv);
            select(nfds, &read_fds, &write_fds, nullptr, tvp);
            ares_process(channel, &read_fds, &write_fds);
        }
    }

    // A record callback
    static void aCallback(void* arg, int status, int /*timeouts*/,
                          unsigned char* abuf, int alen) {
        auto* result = static_cast<QueryResult*>(arg);
        result->status = status;
        result->done = true;

        if (status != ARES_SUCCESS) return;

        struct hostent* host = nullptr;
        int parse_status = ares_parse_a_reply(abuf, alen, &host, nullptr, nullptr);
        if (parse_status != ARES_SUCCESS || !host) return;

        for (int i = 0; host->h_addr_list[i]; ++i) {
            char ip[INET6_ADDRSTRLEN];
            inet_ntop(host->h_addrtype, host->h_addr_list[i], ip, sizeof(ip));
            result->addresses.emplace_back(AddressRecord{
                .hostname = host->h_name ? host->h_name : "",
                .address = ip,
                .ttl = 300,  // Default TTL
            });
        }
        ares_free_hostent(host);
    }

    // AAAA record callback
    static void aaaaCallback(void* arg, int status, int /*timeouts*/,
                             unsigned char* abuf, int alen) {
        auto* result = static_cast<QueryResult*>(arg);
        result->status = status;
        result->done = true;

        if (status != ARES_SUCCESS) return;

        struct hostent* host = nullptr;
        int parse_status = ares_parse_aaaa_reply(abuf, alen, &host, nullptr, nullptr);
        if (parse_status != ARES_SUCCESS || !host) return;

        for (int i = 0; host->h_addr_list[i]; ++i) {
            char ip[INET6_ADDRSTRLEN];
            inet_ntop(AF_INET6, host->h_addr_list[i], ip, sizeof(ip));
            result->addresses.emplace_back(AddressRecord{
                .hostname = host->h_name ? host->h_name : "",
                .address = ip,
                .ttl = 300,
            });
        }
        ares_free_hostent(host);
    }

    // SRV record callback
    static void srvCallback(void* arg, int status, int /*timeouts*/,
                            unsigned char* abuf, int alen) {
        auto* result = static_cast<QueryResult*>(arg);
        result->status = status;
        result->done = true;

        if (status != ARES_SUCCESS) return;

        struct ares_srv_reply* reply = nullptr;
        int parse_status = ares_parse_srv_reply(abuf, alen, &reply);
        if (parse_status != ARES_SUCCESS || !reply) return;

        for (auto* r = reply; r; r = r->next) {
            result->srvs.push_back({
                .priority = r->priority,
                .weight = r->weight,
                .port = r->port,
                .target = r->host ? r->host : "",
                .ttl = 300,
            });
        }
        ares_free_data(reply);
    }

    // NAPTR record callback
    static void naptrCallback(void* arg, int status, int /*timeouts*/,
                              unsigned char* abuf, int alen) {
        auto* result = static_cast<QueryResult*>(arg);
        result->status = status;
        result->done = true;

        if (status != ARES_SUCCESS) return;

        struct ares_naptr_reply* reply = nullptr;
        int parse_status = ares_parse_naptr_reply(abuf, alen, &reply);
        if (parse_status != ARES_SUCCESS || !reply) return;

        for (auto* r = reply; r; r = r->next) {
            result->naptrs.push_back({
                .order = r->order,
                .preference = r->preference,
                .flags = reinterpret_cast<const char*>(r->flags),
                .service = reinterpret_cast<const char*>(r->service),
                .regexp = reinterpret_cast<const char*>(r->regexp),
                .replacement = r->replacement ? r->replacement : "",
                .ttl = 300,
            });
        }
        ares_free_data(reply);
    }
};

// ========== Public API ==========

DnsResolver::DnsResolver(const ResolverConfig& config)
    : impl_(std::make_unique<Impl>(config))
{
    auto result = impl_->init();
    if (!result) {
        IMS_LOG_ERROR("DNS resolver init failed: {}", result.error().message);
    }
}

DnsResolver::~DnsResolver() {
    if (impl_) {
        impl_->destroy();
    }
}

DnsResolver::DnsResolver(DnsResolver&&) noexcept = default;
DnsResolver& DnsResolver::operator=(DnsResolver&&) noexcept = default;

auto DnsResolver::resolveA(const std::string& hostname)
    -> Result<std::vector<AddressRecord>>
{
    if (!impl_->channel) {
        return std::unexpected(ErrorInfo{
            ErrorCode::kDnsResolveFailed, "DNS resolver not initialized"});
    }

    Impl::QueryResult qr;
    ares_query(impl_->channel, hostname.c_str(), ns_c_in, ns_t_a,
               Impl::aCallback, &qr);
    impl_->waitForCompletion();

    if (qr.status != ARES_SUCCESS) {
        return std::unexpected(ErrorInfo{
            ErrorCode::kDnsResolveFailed,
            std::format("A record lookup failed for {}", hostname),
            ares_strerror(qr.status)
        });
    }

    if (qr.addresses.empty()) {
        return std::unexpected(ErrorInfo{
            ErrorCode::kDnsNoRecords,
            std::format("No A records found for {}", hostname)
        });
    }

    return qr.addresses;
}

auto DnsResolver::resolveAAAA(const std::string& hostname)
    -> Result<std::vector<AddressRecord>>
{
    if (!impl_->channel) {
        return std::unexpected(ErrorInfo{
            ErrorCode::kDnsResolveFailed, "DNS resolver not initialized"});
    }

    Impl::QueryResult qr;
    ares_query(impl_->channel, hostname.c_str(), ns_c_in, ns_t_aaaa,
               Impl::aaaaCallback, &qr);
    impl_->waitForCompletion();

    if (qr.status != ARES_SUCCESS) {
        return std::unexpected(ErrorInfo{
            ErrorCode::kDnsResolveFailed,
            std::format("AAAA record lookup failed for {}", hostname),
            ares_strerror(qr.status)
        });
    }

    return qr.addresses;
}

auto DnsResolver::resolveSRV(const std::string& service)
    -> Result<std::vector<SrvRecord>>
{
    if (!impl_->channel) {
        return std::unexpected(ErrorInfo{
            ErrorCode::kDnsResolveFailed, "DNS resolver not initialized"});
    }

    Impl::QueryResult qr;
    ares_query(impl_->channel, service.c_str(), ns_c_in, ns_t_srv,
               Impl::srvCallback, &qr);
    impl_->waitForCompletion();

    if (qr.status != ARES_SUCCESS) {
        return std::unexpected(ErrorInfo{
            ErrorCode::kDnsResolveFailed,
            std::format("SRV lookup failed for {}", service),
            ares_strerror(qr.status)
        });
    }

    // Sort by priority (ascending), then by weight (descending)
    auto& srvs = qr.srvs;
    std::sort(srvs.begin(), srvs.end(), [](const SrvRecord& a, const SrvRecord& b) {
        if (a.priority != b.priority) return a.priority < b.priority;
        return a.weight > b.weight;
    });

    return srvs;
}

auto DnsResolver::resolveNAPTR(const std::string& domain)
    -> Result<std::vector<NaptrRecord>>
{
    if (!impl_->channel) {
        return std::unexpected(ErrorInfo{
            ErrorCode::kDnsResolveFailed, "DNS resolver not initialized"});
    }

    Impl::QueryResult qr;
    ares_query(impl_->channel, domain.c_str(), ns_c_in, ns_t_naptr,
               Impl::naptrCallback, &qr);
    impl_->waitForCompletion();

    if (qr.status != ARES_SUCCESS) {
        return std::unexpected(ErrorInfo{
            ErrorCode::kDnsResolveFailed,
            std::format("NAPTR lookup failed for {}", domain),
            ares_strerror(qr.status)
        });
    }

    // Sort by order, then preference
    auto& naptrs = qr.naptrs;
    std::sort(naptrs.begin(), naptrs.end(), [](const NaptrRecord& a, const NaptrRecord& b) {
        if (a.order != b.order) return a.order < b.order;
        return a.preference < b.preference;
    });

    return naptrs;
}

auto DnsResolver::resolveSipUri(const std::string& domain,
                                 const std::string& transport)
    -> Result<std::vector<SipRouteResult>>
{
    IMS_LOG_DEBUG("Resolving SIP URI for domain={} transport={}", domain, transport);

    std::vector<SipRouteResult> results;

    // Step 1: Try NAPTR lookup
    auto naptr_result = resolveNAPTR(domain);
    if (naptr_result) {
        // Look for SIP-related NAPTR records
        std::string target_service;
        if (transport == "udp") target_service = "SIP+D2U";
        else if (transport == "tcp") target_service = "SIP+D2T";
        else if (transport == "tls") target_service = "SIPS+D2T";

        for (const auto& naptr : *naptr_result) {
            if (naptr.service == target_service && naptr.flags == "s") {
                // NAPTR points to SRV record
                auto srv_result = resolveSRV(naptr.replacement);
                if (srv_result) {
                    for (const auto& srv : *srv_result) {
                        auto a_result = resolveA(srv.target);
                        if (a_result) {
                            for (const auto& addr : *a_result) {
                                results.push_back({
                                    .address = addr.address,
                                    .port = srv.port,
                                    .transport = transport,
                                });
                            }
                        }
                    }
                }
            }
        }
    }

    if (!results.empty()) return results;

    // Step 2: Fallback to SRV lookup directly
    std::string srv_name;
    if (transport == "udp") srv_name = "_sip._udp." + domain;
    else if (transport == "tcp") srv_name = "_sip._tcp." + domain;
    else if (transport == "tls") srv_name = "_sips._tcp." + domain;

    auto srv_result = resolveSRV(srv_name);
    if (srv_result) {
        for (const auto& srv : *srv_result) {
            auto a_result = resolveA(srv.target);
            if (a_result) {
                for (const auto& addr : *a_result) {
                    results.push_back({
                        .address = addr.address,
                        .port = srv.port,
                        .transport = transport,
                    });
                }
            }
        }
    }

    if (!results.empty()) return results;

    // Step 3: Fallback to A record with default port
    auto a_result = resolveA(domain);
    if (a_result && !a_result->empty()) {
        for (const auto& addr : *a_result) {
            results.push_back({
                .address = addr.address,
                .port = (transport == "tls") ? uint16_t{5061} : uint16_t{5060},
                .transport = transport,
            });
        }
        return results;
    }

    return std::unexpected(ErrorInfo{
        ErrorCode::kDnsNoRecords,
        std::format("No SIP route found for {}", domain)
    });
}

void DnsResolver::processEvents() {
    if (!impl_->channel) return;

    fd_set read_fds, write_fds;
    FD_ZERO(&read_fds);
    FD_ZERO(&write_fds);

    int nfds = ares_fds(impl_->channel, &read_fds, &write_fds);
    if (nfds > 0) {
        struct timeval tv = {0, 0};  // Non-blocking
        select(nfds, &read_fds, &write_fds, nullptr, &tv);
        ares_process(impl_->channel, &read_fds, &write_fds);
    }
}

auto DnsResolver::getActiveSockets() const -> std::vector<int> {
    std::vector<int> sockets;
    if (!impl_->channel) return sockets;

    fd_set read_fds, write_fds;
    FD_ZERO(&read_fds);
    FD_ZERO(&write_fds);

    int nfds = ares_fds(impl_->channel, &read_fds, &write_fds);
    for (int i = 0; i < nfds; ++i) {
        if (FD_ISSET(i, &read_fds) || FD_ISSET(i, &write_fds)) {
            sockets.push_back(i);
        }
    }
    return sockets;
}

} // namespace ims::dns
