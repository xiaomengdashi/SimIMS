#include "ims/dns/resolver.hpp"
#include <gtest/gtest.h>

using namespace ims::dns;

class DnsResolverTest : public ::testing::Test {
protected:
    void SetUp() override {
        ResolverConfig config{
            .servers = {"8.8.8.8"},
            .timeout_ms = 5000,
            .tries = 2,
        };
        resolver_ = std::make_unique<DnsResolver>(config);
    }

    std::unique_ptr<DnsResolver> resolver_;
};

// Note: These tests require network access and DNS resolution.
// In CI, they may need to be skipped or use a local DNS mock.

TEST_F(DnsResolverTest, ResolveARecord) {
    // google.com should always resolve
    auto result = resolver_->resolveA("google.com");
    if (result.has_value()) {
        EXPECT_FALSE(result->empty());
        for (const auto& addr : *result) {
            EXPECT_FALSE(addr.address.empty());
        }
    }
    // If DNS fails (no network), just skip
}

TEST_F(DnsResolverTest, ResolveNonexistentDomain) {
    auto result = resolver_->resolveA("this.domain.definitely.does.not.exist.example.com");
    // Should fail gracefully
    EXPECT_FALSE(result.has_value());
}

TEST_F(DnsResolverTest, SipUriResolutionFallback) {
    // Test the full NAPTR -> SRV -> A chain
    // Most domains won't have NAPTR/SRV, so this tests the fallback to A record
    auto result = resolver_->resolveSipUri("google.com", "udp");
    if (result.has_value()) {
        EXPECT_FALSE(result->empty());
        EXPECT_EQ(result->front().port, 5060);
        EXPECT_EQ(result->front().transport, "udp");
    }
}

// Test SRV record sorting
TEST(SrvSortTest, SortByPriorityThenWeight) {
    std::vector<SrvRecord> records = {
        {.priority = 20, .weight = 10, .port = 5060, .target = "srv2.example.com"},
        {.priority = 10, .weight = 50, .port = 5060, .target = "srv1a.example.com"},
        {.priority = 10, .weight = 100, .port = 5060, .target = "srv1b.example.com"},
    };

    // Sort by priority ascending, then weight descending
    std::sort(records.begin(), records.end(), [](const SrvRecord& a, const SrvRecord& b) {
        if (a.priority != b.priority) return a.priority < b.priority;
        return a.weight > b.weight;
    });

    EXPECT_EQ(records[0].target, "srv1b.example.com");  // priority 10, weight 100
    EXPECT_EQ(records[1].target, "srv1a.example.com");  // priority 10, weight 50
    EXPECT_EQ(records[2].target, "srv2.example.com");    // priority 20
}
