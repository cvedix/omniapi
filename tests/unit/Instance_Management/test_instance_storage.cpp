#include "instances/instance_info.h"
#include "instances/instance_storage.h"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <json/json.h>
#include <sstream>
#include <unistd.h>

class InstanceStorageTest : public ::testing::Test {
protected:
  void SetUp() override {
    // Create temporary directory for tests
    test_dir_ = "/tmp/omniapi_test_instances_" + std::to_string(getpid());
    std::filesystem::create_directories(test_dir_);
    storage_ = std::make_unique<InstanceStorage>(test_dir_);
  }

  void TearDown() override {
    // Clean up test directory
    if (std::filesystem::exists(test_dir_)) {
      std::filesystem::remove_all(test_dir_);
    }
  }

  std::string test_dir_;
  std::unique_ptr<InstanceStorage> storage_;

  // Helper function to create a valid InstanceInfo
  InstanceInfo
  createValidInstanceInfo(const std::string &instanceId = "test-instance-123") {
    InstanceInfo info;
    info.instanceId = instanceId;
    info.displayName = "Test Instance";
    info.group = "test_group";
    info.solutionId = "face_detection";
    info.solutionName = "Face Detection";
    info.persistent = true;
    info.frameRateLimit = 30;
    info.metadataMode = true;
    info.statisticsMode = false;
    info.diagnosticsMode = false;
    info.debugMode = false;
    info.readOnly = false;
    info.autoStart = true;
    info.autoRestart = false;
    info.systemInstance = false;
    info.inputPixelLimit = 1920;
    info.inputOrientation = 0;
    info.detectorMode = "SmartDetection";
    info.detectionSensitivity = "Medium";
    info.movementSensitivity = "Low";
    info.sensorModality = "RGB";
    info.loaded = true;
    info.running = false;
    info.fps = 0.0;
    info.version = "2025.0.1.2";
    return info;
  }

  // Helper function to create a valid config JSON
  Json::Value
  createValidConfigJson(const std::string &instanceId = "test-instance-123") {
    Json::Value config(Json::objectValue);
    config["InstanceId"] = instanceId;
    config["DisplayName"] = "Test Instance";
    config["Solution"] = "face_detection";
    config["AutoStart"] = true;
    config["AutoRestart"] = false;
    config["ReadOnly"] = false;
    config["SystemInstance"] = false;

    Json::Value solutionManager(Json::objectValue);
    solutionManager["frame_rate_limit"] = 30;
    solutionManager["send_metadata"] = true;
    solutionManager["run_statistics"] = false;
    config["SolutionManager"] = solutionManager;

    Json::Value detector(Json::objectValue);
    detector["current_preset"] = "SmartDetection";
    detector["current_sensitivity_preset"] = "Medium";
    config["Detector"] = detector;

    return config;
  }
};

// Test validation functions
TEST_F(InstanceStorageTest, ValidateInstanceInfo_Valid) {
  InstanceInfo info = createValidInstanceInfo();
  std::string error;

  EXPECT_TRUE(storage_->validateInstanceInfo(info, error));
  EXPECT_TRUE(error.empty());
}

TEST_F(InstanceStorageTest, ValidateInstanceInfo_EmptyInstanceId) {
  InstanceInfo info = createValidInstanceInfo();
  info.instanceId = "";
  std::string error;

  EXPECT_FALSE(storage_->validateInstanceInfo(info, error));
  EXPECT_FALSE(error.empty());
  EXPECT_NE(error.find("InstanceId"), std::string::npos);
}

TEST_F(InstanceStorageTest, ValidateInstanceInfo_InvalidFrameRateLimit) {
  InstanceInfo info = createValidInstanceInfo();
  info.frameRateLimit = 2000; // Too high
  std::string error;

  EXPECT_FALSE(storage_->validateInstanceInfo(info, error));
  EXPECT_FALSE(error.empty());
  EXPECT_NE(error.find("frameRateLimit"), std::string::npos);
}

TEST_F(InstanceStorageTest, ValidateInstanceInfo_InvalidInputOrientation) {
  InstanceInfo info = createValidInstanceInfo();
  info.inputOrientation = 5; // Invalid (should be 0-3)
  std::string error;

  EXPECT_FALSE(storage_->validateInstanceInfo(info, error));
  EXPECT_FALSE(error.empty());
  EXPECT_NE(error.find("inputOrientation"), std::string::npos);
}

TEST_F(InstanceStorageTest, ValidateConfigJson_Valid) {
  Json::Value config = createValidConfigJson();
  std::string error;

  EXPECT_TRUE(storage_->validateConfigJson(config, error));
  EXPECT_TRUE(error.empty());
}

TEST_F(InstanceStorageTest, ValidateConfigJson_MissingInstanceId) {
  Json::Value config = createValidConfigJson();
  config.removeMember("InstanceId");
  std::string error;

  EXPECT_FALSE(storage_->validateConfigJson(config, error));
  EXPECT_FALSE(error.empty());
  EXPECT_NE(error.find("InstanceId"), std::string::npos);
}

TEST_F(InstanceStorageTest, ValidateConfigJson_InvalidInput) {
  Json::Value config = createValidConfigJson();
  config["Input"] = "not an object"; // Should be object
  std::string error;

  EXPECT_FALSE(storage_->validateConfigJson(config, error));
  EXPECT_FALSE(error.empty());
  EXPECT_NE(error.find("Input"), std::string::npos);
}

// Test conversion functions
TEST_F(InstanceStorageTest, InstanceInfoToConfigJson_Valid) {
  InstanceInfo info = createValidInstanceInfo();
  std::string error;

  Json::Value config = storage_->instanceInfoToConfigJson(info, &error);

  EXPECT_FALSE(config.isNull());
  EXPECT_TRUE(error.empty());
  EXPECT_EQ(config["InstanceId"].asString(), info.instanceId);
  EXPECT_EQ(config["DisplayName"].asString(), info.displayName);
  EXPECT_EQ(config["Solution"].asString(), info.solutionId);
  EXPECT_EQ(config["AutoStart"].asBool(), info.autoStart);
  EXPECT_TRUE(config.isMember("SolutionManager"));
  EXPECT_TRUE(config.isMember("Detector"));
}

TEST_F(InstanceStorageTest, InstanceInfoToConfigJson_WithRTSPUrl) {
  InstanceInfo info = createValidInstanceInfo();
  info.rtspUrl = "rtsp://localhost:8554/stream";
  std::string error;

  Json::Value config = storage_->instanceInfoToConfigJson(info, &error);

  EXPECT_FALSE(config.isNull());
  EXPECT_TRUE(error.empty());
  EXPECT_TRUE(config.isMember("Input"));
  EXPECT_EQ(config["Input"]["media_type"].asString(), "IP Camera");
  EXPECT_NE(
      config["Input"]["uri"].asString().find("rtsp://localhost:8554/stream"),
      std::string::npos);
}

TEST_F(InstanceStorageTest, InstanceInfoToConfigJson_WithFilePath) {
  InstanceInfo info = createValidInstanceInfo();
  info.filePath = "/path/to/video.mp4";
  std::string error;

  Json::Value config = storage_->instanceInfoToConfigJson(info, &error);

  EXPECT_FALSE(config.isNull());
  EXPECT_TRUE(error.empty());
  EXPECT_TRUE(config.isMember("Input"));
  EXPECT_EQ(config["Input"]["media_type"].asString(), "File");
  EXPECT_EQ(config["Input"]["uri"].asString(), "/path/to/video.mp4");
}

TEST_F(InstanceStorageTest, InstanceInfoToConfigJson_InvalidInput) {
  InstanceInfo info = createValidInstanceInfo();
  info.instanceId = ""; // Invalid
  std::string error;

  Json::Value config = storage_->instanceInfoToConfigJson(info, &error);

  EXPECT_TRUE(config.isNull() || config.empty());
  EXPECT_FALSE(error.empty());
}

TEST_F(InstanceStorageTest, ConfigJsonToInstanceInfo_Valid) {
  Json::Value config = createValidConfigJson();
  std::string error;

  auto info = storage_->configJsonToInstanceInfo(config, &error);

  EXPECT_TRUE(info.has_value());
  EXPECT_TRUE(error.empty());
  EXPECT_EQ(info->instanceId, config["InstanceId"].asString());
  EXPECT_EQ(info->displayName, config["DisplayName"].asString());
  EXPECT_EQ(info->solutionId, config["Solution"].asString());
  EXPECT_EQ(info->autoStart, config["AutoStart"].asBool());
  EXPECT_EQ(info->frameRateLimit,
            config["SolutionManager"]["frame_rate_limit"].asInt());
}

TEST_F(InstanceStorageTest, ConfigJsonToInstanceInfo_WithRTSPUrl) {
  Json::Value config = createValidConfigJson();
  config["Input"]["media_type"] = "IP Camera";
  config["Input"]["uri"] =
      "gstreamer:///urisourcebin uri=rtsp://localhost:8554/stream ! decodebin "
      "! videoconvert ! video/x-raw, format=NV12 ! appsink drop=true "
      "name=cvdsink";
  std::string error;

  auto info = storage_->configJsonToInstanceInfo(config, &error);

  EXPECT_TRUE(info.has_value());
  EXPECT_TRUE(error.empty());
  EXPECT_EQ(info->rtspUrl, "rtsp://localhost:8554/stream");
}

TEST_F(InstanceStorageTest, ConfigJsonToInstanceInfo_WithFilePath) {
  Json::Value config = createValidConfigJson();
  config["Input"]["media_type"] = "File";
  config["Input"]["uri"] = "/path/to/video.mp4";
  std::string error;

  auto info = storage_->configJsonToInstanceInfo(config, &error);

  EXPECT_TRUE(info.has_value());
  EXPECT_TRUE(error.empty());
  EXPECT_EQ(info->filePath, "/path/to/video.mp4");
}

TEST_F(InstanceStorageTest, ConfigJsonToInstanceInfo_MissingInstanceId) {
  Json::Value config = createValidConfigJson();
  config.removeMember("InstanceId");
  std::string error;

  auto info = storage_->configJsonToInstanceInfo(config, &error);

  EXPECT_FALSE(info.has_value());
  EXPECT_FALSE(error.empty());
}

// Test round-trip conversion
TEST_F(InstanceStorageTest, RoundTripConversion) {
  InstanceInfo original = createValidInstanceInfo();
  original.rtspUrl = "rtsp://localhost:8554/stream";
  original.additionalParams["MODEL_PATH"] = "/path/to/model.onnx";

  std::string error1;
  Json::Value config = storage_->instanceInfoToConfigJson(original, &error1);
  EXPECT_TRUE(error1.empty());

  std::string error2;
  auto converted = storage_->configJsonToInstanceInfo(config, &error2);
  EXPECT_TRUE(error2.empty());
  EXPECT_TRUE(converted.has_value());

  // Compare key fields
  EXPECT_EQ(converted->instanceId, original.instanceId);
  EXPECT_EQ(converted->displayName, original.displayName);
  EXPECT_EQ(converted->solutionId, original.solutionId);
  EXPECT_EQ(converted->autoStart, original.autoStart);
  EXPECT_EQ(converted->frameRateLimit, original.frameRateLimit);
  EXPECT_EQ(converted->rtspUrl, original.rtspUrl);
  EXPECT_EQ(converted->detectionSensitivity, original.detectionSensitivity);
}

// Test merge logic
TEST_F(InstanceStorageTest, MergeConfigs_PreserveTensorRT) {
  Json::Value existing = createValidConfigJson();
  Json::Value tensorrt(Json::objectValue);
  tensorrt["TensorRT"]["model"]["comment"] = "Test Model";
  existing["0b2ed637-68ae-69cf-5e32-ef7c83f26af4"] = tensorrt;

  Json::Value newConfig = createValidConfigJson();
  newConfig["DisplayName"] = "Updated Name";

  std::vector<std::string> preserveKeys;
  bool merged = storage_->mergeConfigs(existing, newConfig, preserveKeys);

  EXPECT_TRUE(merged);
  EXPECT_EQ(existing["DisplayName"].asString(), "Updated Name");
  // TensorRT config should be preserved
  EXPECT_TRUE(existing.isMember("0b2ed637-68ae-69cf-5e32-ef7c83f26af4"));
}

TEST_F(InstanceStorageTest, MergeConfigs_PreserveSpecialKeys) {
  Json::Value existing = createValidConfigJson();
  existing["Zone"]["Zones"]["zone-123"]["name"] = "Test Zone";
  existing["AnimalTracker"]["enable_thumbnail_creation"] = true;

  Json::Value newConfig = createValidConfigJson();
  newConfig["DisplayName"] = "Updated Name";

  std::vector<std::string> preserveKeys = {"Zone", "AnimalTracker"};
  bool merged = storage_->mergeConfigs(existing, newConfig, preserveKeys);

  EXPECT_TRUE(merged);
  EXPECT_EQ(existing["DisplayName"].asString(), "Updated Name");
  // Special keys should be preserved
  EXPECT_TRUE(existing.isMember("Zone"));
  EXPECT_TRUE(existing.isMember("AnimalTracker"));
}

TEST_F(InstanceStorageTest, MergeConfigs_UpdateDetector) {
  Json::Value existing = createValidConfigJson();
  existing["Detector"]["current_preset"] = "FullRegionInference";
  existing["Detector"]["person_confidence_threshold"] = 0.3;

  Json::Value newConfig(Json::objectValue);
  newConfig["Detector"]["current_preset"] = "SmartDetection";
  newConfig["Detector"]["current_sensitivity_preset"] = "High";
  newConfig["Detector"]["person_confidence_threshold"] = 0.5;

  std::vector<std::string> preserveKeys;
  bool merged = storage_->mergeConfigs(existing, newConfig, preserveKeys);

  EXPECT_TRUE(merged);
  EXPECT_EQ(existing["Detector"]["current_preset"].asString(),
            "SmartDetection");
  EXPECT_EQ(existing["Detector"]["current_sensitivity_preset"].asString(),
            "High");
  EXPECT_EQ(existing["Detector"]["person_confidence_threshold"].asDouble(),
            0.5);
}

TEST_F(InstanceStorageTest, MergeConfigs_UpdateInput) {
  Json::Value existing = createValidConfigJson();
  existing["Input"]["uri"] =
      "gstreamer:///urisourcebin uri=rtsp://old:8554/stream ! decodebin ! "
      "videoconvert ! video/x-raw, format=NV12 ! appsink drop=true "
      "name=cvdsink";

  Json::Value newConfig(Json::objectValue);
  newConfig["Input"]["uri"] =
      "gstreamer:///urisourcebin uri=rtsp://new:8554/stream ! decodebin ! "
      "videoconvert ! video/x-raw, format=NV12 ! appsink drop=true "
      "name=cvdsink";

  std::vector<std::string> preserveKeys;
  bool merged = storage_->mergeConfigs(existing, newConfig, preserveKeys);

  EXPECT_TRUE(merged);
  EXPECT_NE(existing["Input"]["uri"].asString().find("rtsp://new:8554/stream"),
            std::string::npos);
}

TEST_F(InstanceStorageTest, MergeConfigs_UpdateOutput) {
  Json::Value existing = createValidConfigJson();
  existing["Output"]["JSONExport"]["enabled"] = false;

  Json::Value newConfig(Json::objectValue);
  newConfig["Output"]["JSONExport"]["enabled"] = true;
  newConfig["Output"]["handlers"]["rtsp:--0.0.0.0:8554-stream1"]["config"]
           ["fps"] = 15;

  std::vector<std::string> preserveKeys;
  bool merged = storage_->mergeConfigs(existing, newConfig, preserveKeys);

  EXPECT_TRUE(merged);
  EXPECT_TRUE(existing["Output"]["JSONExport"]["enabled"].asBool());
  EXPECT_EQ(existing["Output"]["handlers"]["rtsp:--0.0.0.0:8554-stream1"]
                    ["config"]["fps"]
                        .asInt(),
            15);
}

TEST_F(InstanceStorageTest, MergeConfigs_UpdateSolutionManager) {
  Json::Value existing = createValidConfigJson();
  existing["SolutionManager"]["frame_rate_limit"] = 10;
  existing["SolutionManager"]["send_metadata"] = false;

  Json::Value newConfig(Json::objectValue);
  newConfig["SolutionManager"]["frame_rate_limit"] = 20;
  newConfig["SolutionManager"]["send_metadata"] = true;
  newConfig["SolutionManager"]["run_statistics"] = true;

  std::vector<std::string> preserveKeys;
  bool merged = storage_->mergeConfigs(existing, newConfig, preserveKeys);

  EXPECT_TRUE(merged);
  EXPECT_EQ(existing["SolutionManager"]["frame_rate_limit"].asInt(), 20);
  EXPECT_TRUE(existing["SolutionManager"]["send_metadata"].asBool());
  EXPECT_TRUE(existing["SolutionManager"]["run_statistics"].asBool());
}

TEST_F(InstanceStorageTest, MergeConfigs_PreserveZoneAndTripwire) {
  Json::Value existing = createValidConfigJson();
  existing["Zone"]["Zones"]["zone-123"]["name"] = "Test Zone";
  existing["Tripwire"]["Tripwires"]["tripwire-123"]["name"] = "Test Tripwire";

  Json::Value newConfig(Json::objectValue);
  newConfig["DisplayName"] = "Updated Name";
  newConfig["Detector"]["current_preset"] = "SmartDetection";

  std::vector<std::string> preserveKeys = {"Zone", "Tripwire"};
  bool merged = storage_->mergeConfigs(existing, newConfig, preserveKeys);

  EXPECT_TRUE(merged);
  EXPECT_EQ(existing["DisplayName"].asString(), "Updated Name");
  // Zone and Tripwire should be preserved
  EXPECT_TRUE(existing.isMember("Zone"));
  EXPECT_TRUE(existing.isMember("Tripwire"));
  EXPECT_EQ(existing["Zone"]["Zones"]["zone-123"]["name"].asString(),
            "Test Zone");
  EXPECT_EQ(
      existing["Tripwire"]["Tripwires"]["tripwire-123"]["name"].asString(),
      "Test Tripwire");
}

TEST_F(InstanceStorageTest, MergeConfigs_UpdatePerformanceMode) {
  Json::Value existing = createValidConfigJson();
  existing["PerformanceMode"]["current_preset"] = "Balanced";

  Json::Value newConfig(Json::objectValue);
  newConfig["PerformanceMode"]["current_preset"] = "HighPerformance";

  std::vector<std::string> preserveKeys;
  bool merged = storage_->mergeConfigs(existing, newConfig, preserveKeys);

  EXPECT_TRUE(merged);
  EXPECT_EQ(existing["PerformanceMode"]["current_preset"].asString(),
            "HighPerformance");
}

TEST_F(InstanceStorageTest, MergeConfigs_UpdateDetectorThermal) {
  Json::Value existing = createValidConfigJson();
  existing["DetectorThermal"]["model_file"] = "pva_det_mosaic_320";

  Json::Value newConfig(Json::objectValue);
  newConfig["DetectorThermal"]["model_file"] = "pva_det_thermal_512";

  std::vector<std::string> preserveKeys;
  bool merged = storage_->mergeConfigs(existing, newConfig, preserveKeys);

  EXPECT_TRUE(merged);
  EXPECT_EQ(existing["DetectorThermal"]["model_file"].asString(),
            "pva_det_thermal_512");
}

TEST_F(InstanceStorageTest, InstanceInfoToConfigJson_AllFields) {
  InstanceInfo info = createValidInstanceInfo();
  info.rtspUrl = "rtsp://localhost:8554/stream";
  info.detectorMode = "SmartDetection";
  info.detectionSensitivity = "High";
  info.performanceMode = "HighPerformance";
  info.animalConfidenceThreshold = 0.4;
  info.personConfidenceThreshold = 0.5;
  info.vehicleConfidenceThreshold = 0.6;
  info.faceConfidenceThreshold = 0.2;
  info.licensePlateConfidenceThreshold = 0.2;
  info.confThreshold = 0.3;
  info.detectorModelFile = "pva_det_custom_512";
  info.detectorThermalModelFile = "pva_det_thermal_custom";

  std::string error;
  Json::Value config = storage_->instanceInfoToConfigJson(info, &error);

  EXPECT_FALSE(config.isNull());
  EXPECT_TRUE(error.empty());

  // Check all fields are present
  EXPECT_TRUE(config.isMember("Detector"));
  EXPECT_EQ(config["Detector"]["current_preset"].asString(), "SmartDetection");
  EXPECT_EQ(config["Detector"]["current_sensitivity_preset"].asString(),
            "High");
  EXPECT_EQ(config["Detector"]["person_confidence_threshold"].asDouble(), 0.5);
  EXPECT_EQ(config["Detector"]["animal_confidence_threshold"].asDouble(), 0.4);
  EXPECT_EQ(config["Detector"]["vehicle_confidence_threshold"].asDouble(), 0.6);
  EXPECT_EQ(config["Detector"]["face_confidence_threshold"].asDouble(), 0.2);
  EXPECT_EQ(config["Detector"]["license_plate_confidence_threshold"].asDouble(),
            0.2);
  EXPECT_EQ(config["Detector"]["conf_threshold"].asDouble(), 0.3);
  EXPECT_EQ(config["Detector"]["model_file"].asString(), "pva_det_custom_512");

  EXPECT_TRUE(config.isMember("DetectorThermal"));
  EXPECT_EQ(config["DetectorThermal"]["model_file"].asString(),
            "pva_det_thermal_custom");

  EXPECT_TRUE(config.isMember("PerformanceMode"));
  EXPECT_EQ(config["PerformanceMode"]["current_preset"].asString(),
            "HighPerformance");

  EXPECT_TRUE(config.isMember("DetectorRegions"));
  EXPECT_TRUE(config.isMember("Zone"));
  EXPECT_TRUE(config.isMember("Tripwire"));

  EXPECT_TRUE(config.isMember("Input"));
  EXPECT_EQ(config["Input"]["media_type"].asString(), "IP Camera");

  EXPECT_TRUE(config.isMember("Output"));
  EXPECT_TRUE(config["Output"].isMember("JSONExport"));
  EXPECT_TRUE(config["Output"].isMember("handlers"));
}

// Test save/load operations
TEST_F(InstanceStorageTest, SaveAndLoadInstance) {
  InstanceInfo info = createValidInstanceInfo("test-save-load-123");
  info.rtspUrl = "rtsp://localhost:8554/stream";

  // Save instance
  bool saved = storage_->saveInstance("test-save-load-123", info);
  EXPECT_TRUE(saved);

  // Load instance
  auto loaded = storage_->loadInstance("test-save-load-123");
  EXPECT_TRUE(loaded.has_value());
  EXPECT_EQ(loaded->instanceId, info.instanceId);
  EXPECT_EQ(loaded->displayName, info.displayName);
  EXPECT_EQ(loaded->rtspUrl, info.rtspUrl);
}

TEST_F(InstanceStorageTest, SaveInstance_InvalidInstanceId) {
  InstanceInfo info = createValidInstanceInfo();
  info.instanceId = ""; // Invalid

  bool saved = storage_->saveInstance("test-id", info);
  EXPECT_FALSE(saved);
}

TEST_F(InstanceStorageTest, LoadInstance_NotFound) {
  auto loaded = storage_->loadInstance("non-existent-instance");
  EXPECT_FALSE(loaded.has_value());
}

TEST_F(InstanceStorageTest, LoadAllInstances) {
  // Save multiple instances
  InstanceInfo info1 = createValidInstanceInfo("instance-1");
  InstanceInfo info2 = createValidInstanceInfo("instance-2");

  EXPECT_TRUE(storage_->saveInstance("instance-1", info1));
  EXPECT_TRUE(storage_->saveInstance("instance-2", info2));

  // Load all instances
  auto allInstances = storage_->loadAllInstances();
  EXPECT_GE(allInstances.size(), 2);
  EXPECT_NE(std::find(allInstances.begin(), allInstances.end(), "instance-1"),
            allInstances.end());
  EXPECT_NE(std::find(allInstances.begin(), allInstances.end(), "instance-2"),
            allInstances.end());
}

TEST_F(InstanceStorageTest, DeleteInstance) {
  InstanceInfo info = createValidInstanceInfo("instance-to-delete");

  // Save instance
  EXPECT_TRUE(storage_->saveInstance("instance-to-delete", info));
  EXPECT_TRUE(storage_->instanceExists("instance-to-delete"));

  // Delete instance
  EXPECT_TRUE(storage_->deleteInstance("instance-to-delete"));
  EXPECT_FALSE(storage_->instanceExists("instance-to-delete"));

  // Try to load deleted instance
  auto loaded = storage_->loadInstance("instance-to-delete");
  EXPECT_FALSE(loaded.has_value());
}

TEST_F(InstanceStorageTest, InstanceExists) {
  InstanceInfo info = createValidInstanceInfo("instance-exists-test");

  EXPECT_FALSE(storage_->instanceExists("instance-exists-test"));

  EXPECT_TRUE(storage_->saveInstance("instance-exists-test", info));
  EXPECT_TRUE(storage_->instanceExists("instance-exists-test"));
}

// Test error handling
TEST_F(InstanceStorageTest, SaveInstance_InstanceIdMismatch) {
  InstanceInfo info = createValidInstanceInfo("instance-123");

  // Try to save with different instanceId
  bool saved = storage_->saveInstance("different-id", info);
  EXPECT_FALSE(saved);
}

TEST_F(InstanceStorageTest, LoadInstance_InvalidConfig) {
  // Create invalid config file manually
  std::string filepath = test_dir_ + "/instances.json";
  std::ofstream file(filepath);
  file << R"({"invalid-instance": "not an object"})";
  file.close();

  auto loaded = storage_->loadInstance("invalid-instance");
  EXPECT_FALSE(loaded.has_value());
}
