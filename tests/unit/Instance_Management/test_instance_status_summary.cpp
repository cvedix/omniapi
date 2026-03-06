#include "api/instance_handler.h"
#include "core/pipeline_builder.h"
#include "instances/inprocess_instance_manager.h"
#include "instances/instance_info.h"
#include "instances/instance_registry.h"
#include "instances/instance_storage.h"
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

class InstanceStatusSummaryTest : public ::testing::Test {
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
};

// Test status summary endpoint returns valid JSON
TEST_F(InstanceStatusSummaryTest, StatusSummaryReturnsValidJson) {
  auto req = HttpRequest::newHttpRequest();
  req->setPath("/v1/core/instance/status/summary");
  req->setMethod(Get);

  HttpResponsePtr response;
  bool callbackCalled = false;
  std::mutex callbackMutex;
  std::condition_variable callbackCv;
  std::string callbackError;

  handler_->getStatusSummary(req, [&](const HttpResponsePtr &resp) {
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

  // Check content type
  EXPECT_EQ(response->contentType(), CT_APPLICATION_JSON);

  // Parse JSON response
  auto json = response->getJsonObject();
  ASSERT_NE(json, nullptr);

  // Check required fields
  EXPECT_TRUE(json->isMember("total"));
  EXPECT_TRUE(json->isMember("configured"));
  EXPECT_TRUE(json->isMember("running"));
  EXPECT_TRUE(json->isMember("stopped"));
  EXPECT_TRUE(json->isMember("timestamp"));

  // Check types
  EXPECT_TRUE((*json)["total"].isInt());
  EXPECT_TRUE((*json)["configured"].isInt());
  EXPECT_TRUE((*json)["running"].isInt());
  EXPECT_TRUE((*json)["stopped"].isInt());
  EXPECT_TRUE((*json)["timestamp"].isString());

  // Check values are non-negative
  EXPECT_GE((*json)["total"].asInt(), 0);
  EXPECT_GE((*json)["configured"].asInt(), 0);
  EXPECT_GE((*json)["running"].asInt(), 0);
  EXPECT_GE((*json)["stopped"].asInt(), 0);

  // Check configured equals total
  EXPECT_EQ((*json)["configured"].asInt(), (*json)["total"].asInt());

  // Check running + stopped equals total
  EXPECT_EQ((*json)["running"].asInt() + (*json)["stopped"].asInt(),
            (*json)["total"].asInt());
}

// Test status summary endpoint with no instances
TEST_F(InstanceStatusSummaryTest, StatusSummaryWithNoInstances) {
  auto req = HttpRequest::newHttpRequest();
  req->setPath("/v1/core/instance/status/summary");
  req->setMethod(Get);

  HttpResponsePtr response;
  bool callbackCalled = false;
  std::mutex callbackMutex;
  std::condition_variable callbackCv;
  std::string callbackError;

  handler_->getStatusSummary(req, [&](const HttpResponsePtr &resp) {
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
  auto json = response->getJsonObject();
  ASSERT_NE(json, nullptr);

  EXPECT_EQ((*json)["total"].asInt(), 0);
  EXPECT_EQ((*json)["configured"].asInt(), 0);
  EXPECT_EQ((*json)["running"].asInt(), 0);
  EXPECT_EQ((*json)["stopped"].asInt(), 0);
}

// Test status summary endpoint when manager not initialized
TEST_F(InstanceStatusSummaryTest, StatusSummaryRegistryNotInitialized) {
  InstanceHandler::setInstanceManager(nullptr);

  auto req = HttpRequest::newHttpRequest();
  req->setPath("/v1/core/instance/status/summary");
  req->setMethod(Get);

  HttpResponsePtr response;
  bool callbackCalled = false;
  std::mutex callbackMutex;
  std::condition_variable callbackCv;
  std::string callbackError;

  handler_->getStatusSummary(req, [&](const HttpResponsePtr &resp) {
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
  ASSERT_NE(response, nullptr) << "Response is null";
  EXPECT_EQ(response->statusCode(), k500InternalServerError);

  auto json = response->getJsonObject();
  ASSERT_NE(json, nullptr);
  EXPECT_TRUE(json->isMember("error"));

  // Restore manager for other tests
  InstanceHandler::setInstanceManager(instance_manager_.get());
}

// Test OPTIONS endpoint for CORS
TEST_F(InstanceStatusSummaryTest, StatusSummaryOptionsEndpoint) {
  auto req = HttpRequest::newHttpRequest();
  req->setPath("/v1/core/instance/status/summary");
  req->setMethod(Options);

  HttpResponsePtr response;
  bool callbackCalled = false;
  std::mutex callbackMutex;
  std::condition_variable callbackCv;
  std::string callbackError;

  handler_->handleOptions(req, [&](const HttpResponsePtr &resp) {
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

  // Check CORS headers
  EXPECT_EQ(response->getHeader("Access-Control-Allow-Origin"), "*");
  EXPECT_EQ(response->getHeader("Access-Control-Allow-Methods"),
            "GET, POST, PUT, DELETE, OPTIONS");
  EXPECT_EQ(response->getHeader("Access-Control-Allow-Headers"),
            "Content-Type, Authorization");
}
