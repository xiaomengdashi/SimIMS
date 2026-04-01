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
  pcf:
    host: 10.0.0.9
    port: 7777

icscf:
  listen_addr: 10.0.0.2
  listen_port: 5061

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
    EXPECT_EQ(config.icscf.listen_port, 5061);
    EXPECT_EQ(config.scscf.listen_port, 5062);
    EXPECT_EQ(config.scscf.domain, "test.ims.com");
    EXPECT_EQ(config.scscf.auth_mode, "hybrid_fallback");
    EXPECT_EQ(config.scscf.registration_cleanup_interval_ms, 15000u);
    EXPECT_TRUE(config.scscf.exosip.enabled);
    EXPECT_EQ(config.scscf.exosip.listen_addr, "0.0.0.0");
    EXPECT_EQ(config.scscf.exosip.listen_port, 5072);
    EXPECT_EQ(config.scscf.exosip.transport, "udp");
    ASSERT_EQ(config.dns.servers.size(), 2u);
    EXPECT_EQ(config.dns.servers[0], "8.8.8.8");
    EXPECT_EQ(config.dns.timeout_ms, 5000u);
}

TEST_F(ConfigTest, LoadHssSubscribers) {
    writeConfig(R"yaml(
hss_adapter:
  type: diameter
  diameter_host: hss.example.com
  diameter_port: 3868
  diameter_realm: ims.operator.com
  subscribers:
    - imsi: "460112024122023"
      tel: "+8613824122023"
      password: "pass-a"
      realm: "ims.operator.com"
      k: "465b5ce8b199b49faa5f0a2ee238a6bc"
      operator_code_type: "opc"
      opc: "cd63cb71954a9f4e48a5994e37a02baf"
      sqn: "000000000001"
    - imsi: "460112024122024"
      tel: "+8613824122024"
      password: "pass-b"
      realm: "ims.operator.com"
      k: "465b5ce8b199b49faa5f0a2ee238a6bc"
      operator_code_type: "op"
      op: "cdc202d5123e20f62b6d676ac72cb318"
      sqn: "000000000002"
      amf: "9001"
)yaml");

    auto result = load_config(config_path_);
    ASSERT_TRUE(result.has_value()) << result.error().message;

    auto& config = *result;
    EXPECT_EQ(config.hss_adapter.type, "diameter");
    EXPECT_EQ(config.hss_adapter.diameter_host, "hss.example.com");
    EXPECT_EQ(config.hss_adapter.diameter_port, 3868);
    EXPECT_EQ(config.hss_adapter.diameter_realm, "ims.operator.com");
    ASSERT_EQ(config.hss_adapter.subscribers.size(), 2u);
    EXPECT_EQ(config.hss_adapter.subscribers[0].imsi, "460112024122023");
    EXPECT_EQ(config.hss_adapter.subscribers[0].tel, "+8613824122023");
    EXPECT_EQ(config.hss_adapter.subscribers[0].password, "pass-a");
    EXPECT_EQ(config.hss_adapter.subscribers[0].realm, "ims.operator.com");
    EXPECT_EQ(config.hss_adapter.subscribers[0].amf, "8000");
    EXPECT_EQ(config.hss_adapter.subscribers[0].operator_code_type, "opc");
    EXPECT_EQ(config.hss_adapter.subscribers[1].imsi, "460112024122024");
    EXPECT_EQ(config.hss_adapter.subscribers[1].tel, "+8613824122024");
    EXPECT_EQ(config.hss_adapter.subscribers[0].k, "465b5ce8b199b49faa5f0a2ee238a6bc");
    EXPECT_EQ(config.hss_adapter.subscribers[0].opc, "cd63cb71954a9f4e48a5994e37a02baf");
    EXPECT_EQ(config.hss_adapter.subscribers[0].sqn, "000000000001");
    EXPECT_EQ(config.hss_adapter.subscribers[1].operator_code_type, "op");
    EXPECT_EQ(config.hss_adapter.subscribers[1].op, "cdc202d5123e20f62b6d676ac72cb318");
    EXPECT_EQ(config.hss_adapter.subscribers[1].amf, "9001");
}

TEST_F(ConfigTest, LoadHssSubscribersWithoutOperatorCodeTypeUsesLegacyFields) {
    writeConfig(R"yaml(
 hss_adapter:
   subscribers:
     - imsi: "460112024122023"
       tel: "+8613824122023"
       password: "pass-a"
       realm: "ims.operator.com"
       k: "465b5ce8b199b49faa5f0a2ee238a6bc"
       opc: "cd63cb71954a9f4e48a5994e37a02baf"
       sqn: "000000000001"
 )yaml");

    auto result = load_config(config_path_);
    ASSERT_TRUE(result.has_value()) << result.error().message;
    ASSERT_EQ(result->hss_adapter.subscribers.size(), 1u);
    EXPECT_TRUE(result->hss_adapter.subscribers[0].operator_code_type.empty());
    EXPECT_EQ(result->hss_adapter.subscribers[0].opc, "cd63cb71954a9f4e48a5994e37a02baf");
}

TEST_F(ConfigTest, RejectsMissingOperatorCodeFieldForExplicitType) {
    writeConfig(R"yaml(
 hss_adapter:
   subscribers:
     - imsi: "460112024122023"
       tel: "+8613824122023"
       password: "pass-a"
       realm: "ims.operator.com"
       k: "465b5ce8b199b49faa5f0a2ee238a6bc"
       operator_code_type: "opc"
       sqn: "000000000001"
 )yaml");

    auto result = load_config(config_path_);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, ErrorCode::kConfigInvalidValue);
}

TEST_F(ConfigTest, RejectsInvalidOperatorCodeType) {
    writeConfig(R"yaml(
 hss_adapter:
   subscribers:
     - imsi: "460112024122023"
       tel: "+8613824122023"
       password: "pass-a"
       realm: "ims.operator.com"
       k: "465b5ce8b199b49faa5f0a2ee238a6bc"
       operator_code_type: "bad"
       opc: "cd63cb71954a9f4e48a5994e37a02baf"
       sqn: "000000000001"
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
    EXPECT_EQ(config.scscf.domain, "ims.local");
    EXPECT_EQ(config.scscf.auth_mode, "ims_only");
    EXPECT_EQ(config.scscf.registration_cleanup_interval_ms, 30000u);
    EXPECT_TRUE(config.scscf.exosip.enabled);
    EXPECT_EQ(config.scscf.exosip.listen_addr, "0.0.0.0");
    EXPECT_EQ(config.scscf.exosip.listen_port, 5072);
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
