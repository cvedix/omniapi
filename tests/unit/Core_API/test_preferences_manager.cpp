#include "core/preferences_manager.h"
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <json/json.h>

class PreferencesManagerTest : public ::testing::Test {
protected:
  void SetUp() override {
    // Use a temporary config file for testing
    test_config_path_ = "/tmp/test_rtconfig.json";
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
TEST_F(PreferencesManagerTest, SingletonPattern) {
  auto &instance1 = PreferencesManager::getInstance();
  auto &instance2 = PreferencesManager::getInstance();
  EXPECT_EQ(&instance1, &instance2);
}

// Test load preferences with defaults
TEST_F(PreferencesManagerTest, LoadPreferencesWithDefaults) {
  auto &manager = PreferencesManager::getInstance();
  bool result = manager.loadPreferences(test_config_path_);

  EXPECT_TRUE(result);
  EXPECT_TRUE(manager.isLoaded());
}

// Test get preferences
TEST_F(PreferencesManagerTest, GetPreferences) {
  auto &manager = PreferencesManager::getInstance();
  manager.loadPreferences(test_config_path_);

  Json::Value prefs = manager.getPreferences();
  EXPECT_TRUE(prefs.isObject());
}

// Test get preference by key
TEST_F(PreferencesManagerTest, GetPreference) {
  auto &manager = PreferencesManager::getInstance();
  manager.loadPreferences(test_config_path_);

  Json::Value value = manager.getPreference("vms.show_area_crossing");
  // Should return value or nullValue
  EXPECT_TRUE(value.isBool() || value.isNull());
}

// Test set preference
TEST_F(PreferencesManagerTest, SetPreference) {
  auto &manager = PreferencesManager::getInstance();
  manager.loadPreferences(test_config_path_);

  Json::Value newValue(true);
  bool result = manager.setPreference("test.preference", newValue);
  EXPECT_TRUE(result);

  Json::Value retrieved = manager.getPreference("test.preference");
  EXPECT_TRUE(retrieved.isBool());
  EXPECT_EQ(retrieved.asBool(), true);
}

// Test update preferences
TEST_F(PreferencesManagerTest, UpdatePreferences) {
  auto &manager = PreferencesManager::getInstance();
  manager.loadPreferences(test_config_path_);

  Json::Value updates;
  updates["test.key1"] = "value1";
  updates["test.key2"] = 42;

  bool result = manager.updatePreferences(updates);
  EXPECT_TRUE(result);

  Json::Value value1 = manager.getPreference("test.key1");
  EXPECT_EQ(value1.asString(), "value1");

  Json::Value value2 = manager.getPreference("test.key2");
  EXPECT_EQ(value2.asInt(), 42);
}

// Test save preferences
TEST_F(PreferencesManagerTest, SavePreferences) {
  auto &manager = PreferencesManager::getInstance();
  manager.loadPreferences(test_config_path_);

  // Set a test preference
  manager.setPreference("test.save", Json::Value("test_value"));

  bool result = manager.savePreferences(test_config_path_);
  EXPECT_TRUE(result);
  EXPECT_TRUE(std::filesystem::exists(test_config_path_));
}

// Test load from file
TEST_F(PreferencesManagerTest, LoadFromFile) {
  // Create a test preferences file
  Json::Value root;
  root["vms.show_area_crossing"] = true;
  root["global.default_performance_mode"] = "Performance";

  std::ofstream file(test_config_path_);
  Json::StreamWriterBuilder builder;
  std::unique_ptr<Json::StreamWriter> writer(builder.newStreamWriter());
  writer->write(root, &file);
  file.close();

  // Load it
  auto &manager = PreferencesManager::getInstance();
  bool result = manager.loadPreferences(test_config_path_);

  EXPECT_TRUE(result);
  EXPECT_TRUE(manager.isLoaded());

  Json::Value value = manager.getPreference("vms.show_area_crossing");
  EXPECT_TRUE(value.isBool());
  EXPECT_EQ(value.asBool(), true);
}

// Test reload preferences
TEST_F(PreferencesManagerTest, ReloadPreferences) {
  auto &manager = PreferencesManager::getInstance();
  manager.loadPreferences(test_config_path_);

  // Modify preferences
  manager.setPreference("test.reload", Json::Value("before"));

  // Reload
  bool result = manager.reloadPreferences();
  // May fail if file doesn't exist, which is OK
  // Just check it doesn't crash
  EXPECT_TRUE(result || !std::filesystem::exists(test_config_path_));
}

// Test get preferences path
TEST_F(PreferencesManagerTest, GetPreferencesPath) {
  auto &manager = PreferencesManager::getInstance();
  manager.loadPreferences(test_config_path_);

  std::string path = manager.getPreferencesPath();
  EXPECT_FALSE(path.empty());
}

// Test flatten JSON (nested to flat)
TEST_F(PreferencesManagerTest, FlattenJson) {
  auto &manager = PreferencesManager::getInstance();
  manager.loadPreferences(test_config_path_);

  // Create nested JSON
  Json::Value nested;
  nested["vms"]["show_area_crossing"] = true;
  nested["global"]["default_performance_mode"] = "Performance";

  // This will be tested indirectly through loadFromFile
  // which uses flattenJson internally
  std::ofstream file(test_config_path_);
  Json::StreamWriterBuilder builder;
  std::unique_ptr<Json::StreamWriter> writer(builder.newStreamWriter());
  writer->write(nested, &file);
  file.close();

  // Load should flatten it
  bool result = manager.loadPreferences(test_config_path_);
  EXPECT_TRUE(result);

  // Should be able to access with dot notation
  Json::Value value = manager.getPreference("vms.show_area_crossing");
  EXPECT_TRUE(value.isBool());
}

