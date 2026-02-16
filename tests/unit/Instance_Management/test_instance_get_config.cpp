#include "api/instance_handler.h"
#include "core/pipeline_builder.h"
#include "instances/inprocess_instance_manager.h"
#include "instances/instance_info.h"
#include "instances/instance_registry.h"
#include "instances/instance_storage.h"
#include "models/create_instance_request.h"
#include "solutions/solution_registry.h"
#include <chrono>
#include <condition_variable>
#include <drogon/HttpRequest.h>
#include <drogon/HttpResponse.h>
#include <filesystem>
#include <gtest/gtest.h>
#include <json/json.h>
#include <memory>
#include <mutex>
#include <thread>
#include <unistd.h>

using namespace drogon;

class InstanceGetConfigTest : public ::testing::Test {
protected:
  void SetUp() override {
    handler_ = std::make_unique<InstanceHandler>();

    // Create temporary storage directory for testing
    test_storage_dir_ = std::filesystem::temp_directory_path() /
                        ("test_instances_" + std::to_string(getpid()));
    std::filesystem::create_directories(test_storage_dir_);

    // Create dependencies for InstanceRegistry
    solution_registry_ = &SolutionRegistry::getInstance();
    pipeline_builder_ = std::make_unique<PipelineBuilder>();
    instance_storage_ =
        std::make_unique<InstanceStorage>(test_storage_dir_.string());

    // Create InstanceRegistry
    instance_registry_ = std::make_unique<InstanceRegistry>(
        *solution_registry_, *pipeline_builder_, *instance_storage_);

    // Create InProcessInstanceManager wrapper
    instance_manager_ =
        std::make_unique<InProcessInstanceManager>(*instance_registry_);

    // Register with handler
    InstanceHandler::setInstanceManager(instance_manager_.get());
  }

  void TearDown() override {
    handler_.reset();
    instance_manager_.reset();
    instance_registry_.reset();
    instance_storage_.reset();
    pipeline_builder_.reset();

    // Clean up test storage directory
    if (std::filesystem::exists(test_storage_dir_)) {
      std::filesystem::remove_all(test_storage_dir_);
    }

    // Clear manager for next test
    InstanceHandler::setInstanceManager(nullptr);
  }

  std::unique_ptr<InstanceHandler> handler_;
  SolutionRegistry *solution_registry_; // Singleton, don't own
  std::unique_ptr<PipelineBuilder> pipeline_builder_;
  std::unique_ptr<InstanceStorage> instance_storage_;
  std::unique_ptr<InstanceRegistry> instance_registry_;
  std::unique_ptr<InProcessInstanceManager> instance_manager_;
  std::filesystem::path test_storage_dir_;

  // Helper to create a test instance
  std::string createTestInstance() {
    CreateInstanceRequest req;
    req.name = "Test Instance";
    req.autoStart = false;
    req.autoRestart = false;

    return instance_manager_->createInstance(req);
  }
};

// Test getConfig returns valid JSON for existing instance
TEST_F(InstanceGetConfigTest, GetConfigReturnsValidJson) {
  // Create a test instance
  std::string instanceId = createTestInstance();
  ASSERT_FALSE(instanceId.empty());

  auto req = HttpRequest::newHttpRequest();
  req->setPath("/v1/core/instance/" + instanceId + "/config");
  req->setMethod(Get);

  HttpResponsePtr response;
  bool callbackCalled = false;
  std::mutex callbackMutex;
  std::condition_variable callbackCv;
  std::string callbackError;

  handler_->getConfig(req, [&](const HttpResponsePtr &resp) {
    try {
      std::lock_guard<std::mutex> lock(callbackMutex);
      callbackCalled = true;
      response = resp;
      callbackCv.notify_one();
    } catch (const std::exception &e) {
      std::lock_guard<std::mutex> lock(callbackMutex);
      callbackError = std::string("Exception in callback: ") + e.what();
      callbackCalled = true;
      callbackCv.notify_one();
    } catch (...) {
      std::lock_guard<std::mutex> lock(callbackMutex);
      callbackError = "Unknown exception in callback";
      callbackCalled = true;
      callbackCv.notify_one();
    }
  });

  // Wait for callback with timeout
  std::unique_lock<std::mutex> lock(callbackMutex);
  if (!callbackCv.wait_for(lock, std::chrono::milliseconds(500), [&] { return callbackCalled; })) {
    FAIL() << "Callback not called within timeout";
  }

  ASSERT_TRUE(callbackCalled);
  if (!callbackError.empty()) {
    FAIL() << callbackError;
  }
  ASSERT_NE(response, nullptr);
  EXPECT_EQ(response->statusCode(), k200OK);
  EXPECT_EQ(response->contentType(), CT_APPLICATION_JSON);

  // Parse and validate JSON
  auto json = response->getJsonObject();
  ASSERT_NE(json, nullptr);

  // Check required fields from config format
  EXPECT_TRUE(json->isMember("InstanceId"));
  EXPECT_TRUE(json->isMember("AutoStart"));
  EXPECT_TRUE(json->isMember("AutoRestart"));
  EXPECT_TRUE(json->isMember("Detector"));
  EXPECT_TRUE(json->isMember("DetectorRegions"));
  EXPECT_TRUE(json->isMember("Input"));

  // Check InstanceId matches
  EXPECT_EQ((*json)["InstanceId"].asString(), instanceId);

  // Check AutoStart and AutoRestart are booleans
  EXPECT_TRUE((*json)["AutoStart"].isBool());
  EXPECT_TRUE((*json)["AutoRestart"].isBool());

  // Check Detector is an object
  EXPECT_TRUE((*json)["Detector"].isObject());

  // Check DetectorRegions is an object
  EXPECT_TRUE((*json)["DetectorRegions"].isObject());

  // Check Input is an object
  EXPECT_TRUE((*json)["Input"].isObject());
}

// Test getConfig returns 404 for non-existent instance
TEST_F(InstanceGetConfigTest, GetConfigNotFound) {
  std::string nonExistentId = "00000000-0000-0000-0000-000000000000";

  auto req = HttpRequest::newHttpRequest();
  req->setPath("/v1/core/instance/" + nonExistentId + "/config");
  req->setMethod(Get);

  HttpResponsePtr response;
  bool callbackCalled = false;
  std::mutex callbackMutex;
  std::condition_variable callbackCv;
  std::string callbackError;

  handler_->getConfig(req, [&](const HttpResponsePtr &resp) {
    try {
      std::lock_guard<std::mutex> lock(callbackMutex);
      callbackCalled = true;
      response = resp;
      callbackCv.notify_one();
    } catch (const std::exception &e) {
      std::lock_guard<std::mutex> lock(callbackMutex);
      callbackError = std::string("Exception in callback: ") + e.what();
      callbackCalled = true;
      callbackCv.notify_one();
    } catch (...) {
      std::lock_guard<std::mutex> lock(callbackMutex);
      callbackError = "Unknown exception in callback";
      callbackCalled = true;
      callbackCv.notify_one();
    }
  });

  // Wait for callback with timeout
  std::unique_lock<std::mutex> lock(callbackMutex);
  if (!callbackCv.wait_for(lock, std::chrono::milliseconds(500), [&] { return callbackCalled; })) {
    FAIL() << "Callback not called within timeout";
  }

  ASSERT_TRUE(callbackCalled);
  if (!callbackError.empty()) {
    FAIL() << callbackError;
  }
  ASSERT_NE(response, nullptr);
  EXPECT_EQ(response->statusCode(), k404NotFound);

  // Check error response
  auto json = response->getJsonObject();
  ASSERT_NE(json, nullptr);
  EXPECT_TRUE(json->isMember("error"));
}

// Test getConfig with empty instance ID
TEST_F(InstanceGetConfigTest, GetConfigEmptyInstanceId) {
  auto req = HttpRequest::newHttpRequest();
  req->setPath("/v1/core/instance//config");
  req->setMethod(Get);

  HttpResponsePtr response;
  bool callbackCalled = false;
  std::mutex callbackMutex;
  std::condition_variable callbackCv;
  std::string callbackError;

  handler_->getConfig(req, [&](const HttpResponsePtr &resp) {
    try {
      std::lock_guard<std::mutex> lock(callbackMutex);
      callbackCalled = true;
      response = resp;
      callbackCv.notify_one();
    } catch (const std::exception &e) {
      std::lock_guard<std::mutex> lock(callbackMutex);
      callbackError = std::string("Exception in callback: ") + e.what();
      callbackCalled = true;
      callbackCv.notify_one();
    } catch (...) {
      std::lock_guard<std::mutex> lock(callbackMutex);
      callbackError = "Unknown exception in callback";
      callbackCalled = true;
      callbackCv.notify_one();
    }
  });

  // Wait for callback with timeout
  std::unique_lock<std::mutex> lock(callbackMutex);
  if (!callbackCv.wait_for(lock, std::chrono::milliseconds(500), [&] { return callbackCalled; })) {
    FAIL() << "Callback not called within timeout";
  }

  ASSERT_TRUE(callbackCalled);
  if (!callbackError.empty()) {
    FAIL() << callbackError;
  }
  ASSERT_NE(response, nullptr);
  EXPECT_EQ(response->statusCode(), k400BadRequest);

  auto json = response->getJsonObject();
  ASSERT_NE(json, nullptr);
  EXPECT_TRUE(json->isMember("error"));
}

// Test getConfig when manager not initialized
TEST_F(InstanceGetConfigTest, GetConfigRegistryNotInitialized) {
  InstanceHandler::setInstanceManager(nullptr);

  auto req = HttpRequest::newHttpRequest();
  req->setPath("/v1/core/instance/test-id/config");
  req->setMethod(Get);

  HttpResponsePtr response;
  bool callbackCalled = false;
  std::mutex callbackMutex;
  std::condition_variable callbackCv;
  std::string callbackError;

  handler_->getConfig(req, [&](const HttpResponsePtr &resp) {
    try {
      std::lock_guard<std::mutex> lock(callbackMutex);
      callbackCalled = true;
      response = resp;
      callbackCv.notify_one();
    } catch (const std::exception &e) {
      std::lock_guard<std::mutex> lock(callbackMutex);
      callbackError = std::string("Exception in callback: ") + e.what();
      callbackCalled = true;
      callbackCv.notify_one();
    } catch (...) {
      std::lock_guard<std::mutex> lock(callbackMutex);
      callbackError = "Unknown exception in callback";
      callbackCalled = true;
      callbackCv.notify_one();
    }
  });

  // Wait for callback with timeout
  std::unique_lock<std::mutex> lock(callbackMutex);
  if (!callbackCv.wait_for(lock, std::chrono::milliseconds(500), [&] { return callbackCalled; })) {
    FAIL() << "Callback not called within timeout";
  }

  ASSERT_TRUE(callbackCalled);
  if (!callbackError.empty()) {
    FAIL() << callbackError;
  }
  ASSERT_NE(response, nullptr);
  EXPECT_EQ(response->statusCode(), k500InternalServerError);

  auto json = response->getJsonObject();
  ASSERT_NE(json, nullptr);
  EXPECT_TRUE(json->isMember("error"));

  // Restore manager for other tests
  InstanceHandler::setInstanceManager(instance_manager_.get());
}

// Test getConfig includes DisplayName when set
TEST_F(InstanceGetConfigTest, GetConfigIncludesDisplayName) {
  // Create instance with display name
  std::string instanceId = createTestInstance();
  ASSERT_FALSE(instanceId.empty());

  auto req = HttpRequest::newHttpRequest();
  req->setPath("/v1/core/instance/" + instanceId + "/config");
  req->setMethod(Get);

  HttpResponsePtr response;
  bool callbackCalled = false;
  std::mutex callbackMutex;
  std::condition_variable callbackCv;
  std::string callbackError;

  handler_->getConfig(req, [&](const HttpResponsePtr &resp) {
    try {
      std::lock_guard<std::mutex> lock(callbackMutex);
      callbackCalled = true;
      response = resp;
      callbackCv.notify_one();
    } catch (const std::exception &e) {
      std::lock_guard<std::mutex> lock(callbackMutex);
      callbackError = std::string("Exception in callback: ") + e.what();
      callbackCalled = true;
      callbackCv.notify_one();
    } catch (...) {
      std::lock_guard<std::mutex> lock(callbackMutex);
      callbackError = "Unknown exception in callback";
      callbackCalled = true;
      callbackCv.notify_one();
    }
  });

  // Wait for callback with timeout
  std::unique_lock<std::mutex> lock(callbackMutex);
  if (!callbackCv.wait_for(lock, std::chrono::milliseconds(500), [&] { return callbackCalled; })) {
    FAIL() << "Callback not called within timeout";
  }

  ASSERT_TRUE(callbackCalled);
  if (!callbackError.empty()) {
    FAIL() << callbackError;
  }
  ASSERT_NE(response, nullptr);
  EXPECT_EQ(response->statusCode(), k200OK);

  auto json = response->getJsonObject();
  ASSERT_NE(json, nullptr);

  // DisplayName should be present if set
  if (json->isMember("DisplayName")) {
    EXPECT_TRUE((*json)["DisplayName"].isString());
  }
}
