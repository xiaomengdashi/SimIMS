#include "common/config.hpp"
#include <gtest/gtest.h>
#include <fstream>
#include <filesystem>

using namespace ims;

class ConfigTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Create a temporary config file
        temp_dir_ = std::filesystem::temp_directory_path() / "ims_test";
        std::filesystem::create_directories(temp_dir_);
        config_path_ = (temp_dir_ / "test_config.yaml").string();
    }

    void TearDown() override {
        std::filesystem::remove_all(temp_dir_);
    }

    void writeConfig(const std::string& content) {
        std::ofstream f(config_path_);
        f << content;
    }

    std::filesystem::path temp_dir_;
    std::string config_path_;
};

TEST_F(ConfigTest, LoadValidConfig) {
    writeConfig(R"yaml(
global:
  log_level: debug
  node_name: test-node

pcscf:
  listen_addr: 10.0.0.1
  listen_port: 5060
  core_entry:
    address: 127.0.0.1
    port: 5062
    transport: udp
  core_peers:
    - address: 10.0.0.2
      port: 5061
      transport: udp
  pcf:
    host: 10.0.0.9
    port: 7777

icscf:
  listen_addr: 10.0.0.2
  listen_port: 5061
  local_scscf:
    address: 10.0.0.3
    port: 5062
    transport: udp

scscf:
  listen_addr: 10.0.0.3
  listen_port: 5062
  domain: test.ims.com
  auth_mode: hybrid_fallback
  registration_cleanup_interval_ms: 15000
  exosip:
    enabled: true
    listen_addr: 0.0.0.0
    listen_port: 5072
    transport: udp

dns:
  servers:
    - 8.8.8.8
    - 8.8.4.4
  timeout_ms: 5000
)yaml");

    auto result = load_config(config_path_);
    ASSERT_TRUE(result.has_value()) << result.error().message;

    auto& config = *result;
    EXPECT_EQ(config.global.log_level, "debug");
    EXPECT_EQ(config.global.node_name, "test-node");
    EXPECT_EQ(config.pcscf.listen_addr, "10.0.0.1");
    EXPECT_EQ(config.pcscf.listen_port, 5060);
    EXPECT_EQ(config.pcscf.pcf.host, "10.0.0.9");
    EXPECT_EQ(config.pcscf.pcf.port, 7777);
    EXPECT_EQ(config.pcscf.core_entry.address, "127.0.0.1");
    EXPECT_EQ(config.pcscf.core_entry.port, 5062);
    EXPECT_EQ(config.pcscf.core_entry.transport, "udp");
    EXPECT_EQ(config.pcscf.core_peers.size(), 1u);
    EXPECT_EQ(config.pcscf.core_peers[0].address, "10.0.0.2");
    EXPECT_EQ(config.pcscf.core_peers[0].port, 5061);
    EXPECT_EQ(config.icscf.listen_port, 5061);
    EXPECT_EQ(config.icscf.local_scscf.address, "10.0.0.3");
    EXPECT_EQ(config.icscf.local_scscf.port, 5062);
    EXPECT_EQ(config.scscf.listen_port, 5062);
    EXPECT_EQ(config.scscf.domain, "test.ims.com");
    EXPECT_EQ(config.scscf.auth_mode, "hybrid_fallback");
    EXPECT_EQ(config.scscf.registration_cleanup_interval_ms, 15000u);
    EXPECT_TRUE(config.scscf.exosip.enabled);
    EXPECT_EQ(config.scscf.exosip.listen_addr, "0.0.0.0");
    EXPECT_EQ(config.scscf.exosip.listen_port, 5072);
    EXPECT_EQ(config.scscf.exosip.transport, "udp");
    EXPECT_FALSE(config.scscf.peer_icscf.has_value());
    ASSERT_EQ(config.dns.servers.size(), 2u);
    EXPECT_EQ(config.dns.servers[0], "8.8.8.8");
    EXPECT_EQ(config.dns.timeout_ms, 5000u);
}

TEST_F(ConfigTest, LoadHssAdapterMongoConfig) {
    writeConfig(R"yaml(
hss_adapter:
  type: diameter
  diameter_host: hss.example.com
  diameter_port: 3868
  diameter_realm: ims.operator.com
  mongo_uri: mongodb://127.0.0.1:27017
  mongo_db: simims
  mongo_collection: subscribers
  default_scscf_uri: sip:127.0.0.1:5062;transport=udp
)yaml");

    auto result = load_config(config_path_);
    ASSERT_TRUE(result.has_value()) << result.error().message;
    EXPECT_EQ(result->hss_adapter.type, "diameter");
    EXPECT_EQ(result->hss_adapter.diameter_host, "hss.example.com");
    EXPECT_EQ(result->hss_adapter.diameter_port, 3868);
    EXPECT_EQ(result->hss_adapter.diameter_realm, "ims.operator.com");
    EXPECT_EQ(result->hss_adapter.mongo_uri, "mongodb://127.0.0.1:27017");
    EXPECT_EQ(result->hss_adapter.mongo_db, "simims");
    EXPECT_EQ(result->hss_adapter.mongo_collection, "subscribers");
    EXPECT_EQ(result->hss_adapter.default_scscf_uri, "sip:127.0.0.1:5062;transport=udp");
}

TEST_F(ConfigTest, LoadHssAdapterMongoDefaults) {
    writeConfig("hss_adapter:\n  type: diameter\n");
    auto result = load_config(config_path_);
    ASSERT_TRUE(result.has_value()) << result.error().message;
    EXPECT_EQ(result->hss_adapter.mongo_uri, "mongodb://127.0.0.1:27017");
    EXPECT_EQ(result->hss_adapter.mongo_db, "simims");
    EXPECT_EQ(result->hss_adapter.mongo_collection, "subscribers");
    EXPECT_EQ(result->hss_adapter.default_scscf_uri, "sip:127.0.0.1:5062;transport=udp");
}

TEST_F(ConfigTest, LoadHssAdapterMongoUriEmptyFails) {
    writeConfig(R"yaml(
hss_adapter:
  mongo_uri: ""
  mongo_db: simims
  mongo_collection: subscribers
)yaml");

    auto result = load_config(config_path_);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, ErrorCode::kConfigInvalidValue);
}

TEST_F(ConfigTest, LoadHssAdapterMongoDbEmptyFails) {
    writeConfig(R"yaml(
hss_adapter:
  mongo_uri: mongodb://127.0.0.1:27017
  mongo_db: ""
  mongo_collection: subscribers
)yaml");

    auto result = load_config(config_path_);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, ErrorCode::kConfigInvalidValue);
}

TEST_F(ConfigTest, LoadHssAdapterMongoCollectionEmptyFails) {
    writeConfig(R"yaml(
hss_adapter:
  mongo_uri: mongodb://127.0.0.1:27017
  mongo_db: simims
  mongo_collection: ""
)yaml");

    auto result = load_config(config_path_);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, ErrorCode::kConfigInvalidValue);
}

TEST_F(ConfigTest, LoadWithDefaults) {
    writeConfig("global:\n  log_level: info\n");

    auto result = load_config(config_path_);
    ASSERT_TRUE(result.has_value());

    auto& config = *result;
    // Should use defaults for missing sections
    EXPECT_EQ(config.pcscf.listen_addr, "0.0.0.0");
    EXPECT_EQ(config.pcscf.listen_port, 5060);
    EXPECT_EQ(config.pcscf.core_entry.address, "127.0.0.1");
    EXPECT_EQ(config.pcscf.core_entry.port, 5062);
    EXPECT_TRUE(config.pcscf.core_peers.empty());
    EXPECT_EQ(config.icscf.local_scscf.address, "127.0.0.1");
    EXPECT_EQ(config.icscf.local_scscf.port, 5062);
    EXPECT_EQ(config.scscf.domain, "ims.local");
    EXPECT_EQ(config.scscf.auth_mode, "ims_only");
    EXPECT_EQ(config.scscf.registration_cleanup_interval_ms, 30000u);
    EXPECT_TRUE(config.scscf.exosip.enabled);
    EXPECT_EQ(config.scscf.exosip.listen_addr, "0.0.0.0");
    EXPECT_EQ(config.scscf.exosip.listen_port, 5072);
    EXPECT_FALSE(config.scscf.peer_icscf.has_value());
}

TEST_F(ConfigTest, FileNotFound) {
    auto result = load_config("/nonexistent/config.yaml");
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, ErrorCode::kConfigFileNotFound);
}

TEST_F(ConfigTest, InvalidYaml) {
    writeConfig("{ invalid yaml content [[[");

    auto result = load_config(config_path_);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, ErrorCode::kConfigParseError);
}

TEST_F(ConfigTest, EmptyConfig) {
    writeConfig("");

    auto result = load_config(config_path_);
    // Empty YAML is valid, should return defaults
    ASSERT_TRUE(result.has_value());
}
