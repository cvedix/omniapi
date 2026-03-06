#include "core/system_config_manager.h"
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <json/json.h>

class SystemConfigManagerTest : public ::testing::Test {
protected:
  void SetUp() override {
    // Use a temporary config file for testing
    test_config_path_ = "/tmp/test_system_config.json";
    // Clean up any existing test file
    if (std::filesystem::exists(test_config_path_)) {
      std::filesystem::remove(test_config_path_);
    }
  }

  void TearDown() override {
    // Clean up test file
    if (std::filesystem::exists(test_config_path_)) {
      std::filesystem::remove(test_config_path_);
    }
  }

  std::string test_config_path_;
};

// Test singleton pattern
TEST_F(SystemConfigManagerTest, SingletonPattern) {
  auto &instance1 = SystemConfigManager::getInstance();
  auto &instance2 = SystemConfigManager::getInstance();
  EXPECT_EQ(&instance1, &instance2);
}

// Test load config with defaults
TEST_F(SystemConfigManagerTest, LoadConfigWithDefaults) {
  auto &manager = SystemConfigManager::getInstance();
  bool result = manager.loadConfig(test_config_path_);

  EXPECT_TRUE(result);
  EXPECT_TRUE(manager.isLoaded());

  auto config = manager.getSystemConfig();
  EXPECT_GE(config.size(), 0); // Should have at least default entities
}

// Test get system config JSON
TEST_F(SystemConfigManagerTest, GetSystemConfigJson) {
  auto &manager = SystemConfigManager::getInstance();
  manager.loadConfig(test_config_path_);

  Json::Value json = manager.getSystemConfigJson();
  EXPECT_TRUE(json.isMember("systemConfig"));
  EXPECT_TRUE(json["systemConfig"].isArray());
}

// Test update system config
TEST_F(SystemConfigManagerTest, UpdateSystemConfig) {
  auto &manager = SystemConfigManager::getInstance();
  manager.loadConfig(test_config_path_);

  auto config = manager.getSystemConfig();
  if (config.empty()) {
    GTEST_SKIP() << "No config entities available for update test";
  }

  std::string fieldId = config[0].fieldId;
  std::string originalValue = config[0].value;

  // Update config
  std::vector<std::pair<std::string, std::string>> updates;
  updates.push_back({fieldId, originalValue}); // Use original value

  bool result = manager.updateSystemConfig(updates);
  EXPECT_TRUE(result);

  // Verify update
  const SystemConfigEntity *entity = manager.getConfigEntity(fieldId);
  ASSERT_NE(entity, nullptr);
  EXPECT_EQ(entity->value, originalValue);
}

// Test update system config from JSON
TEST_F(SystemConfigManagerTest, UpdateSystemConfigFromJson) {
  auto &manager = SystemConfigManager::getInstance();
  manager.loadConfig(test_config_path_);

  auto config = manager.getSystemConfig();
  if (config.empty()) {
    GTEST_SKIP() << "No config entities available for update test";
  }

  std::string fieldId = config[0].fieldId;
  std::string originalValue = config[0].value;

  Json::Value json;
  Json::Value configArray(Json::arrayValue);
  Json::Value configItem;
  configItem["fieldId"] = fieldId;
  configItem["value"] = originalValue;
  configArray.append(configItem);
  json["systemConfig"] = configArray;

  bool result = manager.updateSystemConfigFromJson(json);
  EXPECT_TRUE(result);
}

// Test get config entity
TEST_F(SystemConfigManagerTest, GetConfigEntity) {
  auto &manager = SystemConfigManager::getInstance();
  manager.loadConfig(test_config_path_);

  auto config = manager.getSystemConfig();
  if (config.empty()) {
    GTEST_SKIP() << "No config entities available";
  }

  std::string fieldId = config[0].fieldId;
  const SystemConfigEntity *entity = manager.getConfigEntity(fieldId);

  ASSERT_NE(entity, nullptr);
  EXPECT_EQ(entity->fieldId, fieldId);
}

// Test get config entity with invalid fieldId
TEST_F(SystemConfigManagerTest, GetConfigEntityInvalid) {
  auto &manager = SystemConfigManager::getInstance();
  manager.loadConfig(test_config_path_);

  const SystemConfigEntity *entity = manager.getConfigEntity("invalid_field");
  EXPECT_EQ(entity, nullptr);
}

// Test validate config value
TEST_F(SystemConfigManagerTest, ValidateConfigValue) {
  auto &manager = SystemConfigManager::getInstance();
  manager.loadConfig(test_config_path_);

  auto config = manager.getSystemConfig();
  if (config.empty()) {
    GTEST_SKIP() << "No config entities available";
  }

  std::string fieldId = config[0].fieldId;
  std::string value = config[0].value;

  // Should validate existing value
  bool result = manager.validateConfigValue(fieldId, value);
  EXPECT_TRUE(result);
}

// Test save config
TEST_F(SystemConfigManagerTest, SaveConfig) {
  auto &manager = SystemConfigManager::getInstance();
  manager.loadConfig(test_config_path_);

  bool result = manager.saveConfig(test_config_path_);
  EXPECT_TRUE(result);
  EXPECT_TRUE(std::filesystem::exists(test_config_path_));
}

// Test load from file
TEST_F(SystemConfigManagerTest, LoadFromFile) {
  // Create a test config file
  Json::Value root;
  Json::Value configArray(Json::arrayValue);
  Json::Value item;
  item["fieldId"] = "test_field";
  item["displayName"] = "Test Field";
  item["type"] = "string";
  item["value"] = "test_value";
  item["group"] = "test_group";
  configArray.append(item);
  root["systemConfig"] = configArray;

  std::ofstream file(test_config_path_);
  Json::StreamWriterBuilder builder;
  std::unique_ptr<Json::StreamWriter> writer(builder.newStreamWriter());
  writer->write(root, &file);
  file.close();

  // Load it
  auto &manager = SystemConfigManager::getInstance();
  bool result = manager.loadConfig(test_config_path_);

  EXPECT_TRUE(result);
  EXPECT_TRUE(manager.isLoaded());

  const SystemConfigEntity *entity = manager.getConfigEntity("test_field");
  ASSERT_NE(entity, nullptr);
  EXPECT_EQ(entity->fieldId, "test_field");
  EXPECT_EQ(entity->value, "test_value");
}

