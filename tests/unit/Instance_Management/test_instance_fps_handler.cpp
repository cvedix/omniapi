#include "api/instance_fps_handler.h"
#include "core/pipeline_builder.h"
#include "instances/inprocess_instance_manager.h"
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

class InstanceFpsHandlerTest : public ::testing::Test {
protected:
  void SetUp() override {
    handler_ = std::make_unique<InstanceFpsHandler>();

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
    InstanceFpsHandler::setInstanceManager(instance_manager_.get());

    // Create a test instance for FPS operations
    CreateInstanceRequest createReq;
    createReq.name = "Test Instance for FPS";
    createReq.solution = ""; // No solution needed for basic test
    createReq.fps = 10; // Set initial FPS to 10
    test_instance_id_ = instance_manager_->createInstance(createReq);
    ASSERT_FALSE(test_instance_id_.empty());
  }

  void TearDown() override {
    // Clean up test instance
    if (!test_instance_id_.empty()) {
      instance_manager_->deleteInstance(test_instance_id_);
    }

    handler_.reset();
    instance_manager_.reset();
    instance_registry_.reset();
    instance_storage_.reset();
    pipeline_builder_.reset();

    // Clean up test storage directory
    if (std::filesystem::exists(test_storage_dir_)) {
      std::filesystem::remove_all(test_storage_dir_);
    }

    // Clear handler dependencies
    InstanceFpsHandler::setInstanceManager(nullptr);
  }

  std::unique_ptr<InstanceFpsHandler> handler_;
  std::unique_ptr<InstanceRegistry> instance_registry_;
  std::unique_ptr<InProcessInstanceManager> instance_manager_;
  SolutionRegistry *solution_registry_; // Singleton, don't own
  std::unique_ptr<PipelineBuilder> pipeline_builder_;
  std::unique_ptr<InstanceStorage> instance_storage_;
  std::filesystem::path test_storage_dir_;
  std::string test_instance_id_;
};

// Test GET /api/v1/instances/{instance_id}/fps - Get current FPS
TEST_F(InstanceFpsHandlerTest, GetFpsSuccess) {
  auto req = HttpRequest::newHttpRequest();
  req->setPath("/api/v1/instances/" + test_instance_id_ + "/fps");
  req->setMethod(Get);
  req->setParameter("instance_id", test_instance_id_);

  HttpResponsePtr response;
  bool callbackCalled = false;
  std::mutex callbackMutex;
  std::condition_variable callbackCv;

  handler_->getFps(req, [&](const HttpResponsePtr &resp) {
    std::lock_guard<std::mutex> lock(callbackMutex);
    callbackCalled = true;
    response = resp;
    callbackCv.notify_one();
  });

  // Wait for callback
  std::unique_lock<std::mutex> lock(callbackMutex);
  ASSERT_TRUE(callbackCv.wait_for(lock, std::chrono::milliseconds(500),
                                   [&] { return callbackCalled; }));

  ASSERT_NE(response, nullptr);
  EXPECT_EQ(response->statusCode(), k200OK);

  auto json = response->getJsonObject();
  ASSERT_NE(json, nullptr);
  EXPECT_EQ((*json)["instance_id"].asString(), test_instance_id_);
  EXPECT_EQ((*json)["fps"].asInt(), 10); // Should be 10 as set in SetUp
}

// Test GET /api/v1/instances/{instance_id}/fps - Instance not found
TEST_F(InstanceFpsHandlerTest, GetFpsInstanceNotFound) {
  auto req = HttpRequest::newHttpRequest();
  req->setPath("/api/v1/instances/non-existent-instance/fps");
  req->setMethod(Get);
  req->setParameter("instance_id", "non-existent-instance");

  HttpResponsePtr response;
  bool callbackCalled = false;
  std::mutex callbackMutex;
  std::condition_variable callbackCv;

  handler_->getFps(req, [&](const HttpResponsePtr &resp) {
    std::lock_guard<std::mutex> lock(callbackMutex);
    callbackCalled = true;
    response = resp;
    callbackCv.notify_one();
  });

  // Wait for callback
  std::unique_lock<std::mutex> lock(callbackMutex);
  ASSERT_TRUE(callbackCv.wait_for(lock, std::chrono::milliseconds(500),
                                   [&] { return callbackCalled; }));

  ASSERT_NE(response, nullptr);
  EXPECT_EQ(response->statusCode(), k404NotFound);
}

// Test POST /api/v1/instances/{instance_id}/fps - Set FPS successfully
TEST_F(InstanceFpsHandlerTest, SetFpsSuccess) {
  auto req = HttpRequest::newHttpRequest();
  req->setPath("/api/v1/instances/" + test_instance_id_ + "/fps");
  req->setMethod(Post);
  req->setParameter("instance_id", test_instance_id_);

  Json::Value body;
  body["fps"] = 15;
  req->setBody(body.toStyledString());
  req->addHeader("Content-Type", "application/json");

  HttpResponsePtr response;
  bool callbackCalled = false;
  std::mutex callbackMutex;
  std::condition_variable callbackCv;

  handler_->setFps(req, [&](const HttpResponsePtr &resp) {
    std::lock_guard<std::mutex> lock(callbackMutex);
    callbackCalled = true;
    response = resp;
    callbackCv.notify_one();
  });

  // Wait for callback
  std::unique_lock<std::mutex> lock(callbackMutex);
  ASSERT_TRUE(callbackCv.wait_for(lock, std::chrono::milliseconds(500),
                                   [&] { return callbackCalled; }));

  ASSERT_NE(response, nullptr);
  EXPECT_EQ(response->statusCode(), k200OK);

  auto json = response->getJsonObject();
  ASSERT_NE(json, nullptr);
  EXPECT_EQ((*json)["instance_id"].asString(), test_instance_id_);
  EXPECT_EQ((*json)["fps"].asInt(), 15);
  EXPECT_EQ((*json)["message"].asString(), "FPS configuration updated successfully.");

  // Verify FPS was actually updated
  auto optInfo = instance_manager_->getInstance(test_instance_id_);
  ASSERT_TRUE(optInfo.has_value());
  EXPECT_EQ(optInfo.value().configuredFps, 15);
}

// Test POST /api/v1/instances/{instance_id}/fps - Invalid FPS (zero)
TEST_F(InstanceFpsHandlerTest, SetFpsInvalidZero) {
  auto req = HttpRequest::newHttpRequest();
  req->setPath("/api/v1/instances/" + test_instance_id_ + "/fps");
  req->setMethod(Post);
  req->setParameter("instance_id", test_instance_id_);

  Json::Value body;
  body["fps"] = 0; // Invalid: must be > 0
  req->setBody(body.toStyledString());
  req->addHeader("Content-Type", "application/json");

  HttpResponsePtr response;
  bool callbackCalled = false;
  std::mutex callbackMutex;
  std::condition_variable callbackCv;

  handler_->setFps(req, [&](const HttpResponsePtr &resp) {
    std::lock_guard<std::mutex> lock(callbackMutex);
    callbackCalled = true;
    response = resp;
    callbackCv.notify_one();
  });

  // Wait for callback
  std::unique_lock<std::mutex> lock(callbackMutex);
  ASSERT_TRUE(callbackCv.wait_for(lock, std::chrono::milliseconds(500),
                                   [&] { return callbackCalled; }));

  ASSERT_NE(response, nullptr);
  EXPECT_EQ(response->statusCode(), k400BadRequest);
}

// Test POST /api/v1/instances/{instance_id}/fps - Invalid FPS (negative)
TEST_F(InstanceFpsHandlerTest, SetFpsInvalidNegative) {
  auto req = HttpRequest::newHttpRequest();
  req->setPath("/api/v1/instances/" + test_instance_id_ + "/fps");
  req->setMethod(Post);
  req->setParameter("instance_id", test_instance_id_);

  Json::Value body;
  body["fps"] = -5; // Invalid: must be > 0
  req->setBody(body.toStyledString());
  req->addHeader("Content-Type", "application/json");

  HttpResponsePtr response;
  bool callbackCalled = false;
  std::mutex callbackMutex;
  std::condition_variable callbackCv;

  handler_->setFps(req, [&](const HttpResponsePtr &resp) {
    std::lock_guard<std::mutex> lock(callbackMutex);
    callbackCalled = true;
    response = resp;
    callbackCv.notify_one();
  });

  // Wait for callback
  std::unique_lock<std::mutex> lock(callbackMutex);
  ASSERT_TRUE(callbackCv.wait_for(lock, std::chrono::milliseconds(500),
                                   [&] { return callbackCalled; }));

  ASSERT_NE(response, nullptr);
  EXPECT_EQ(response->statusCode(), k400BadRequest);
}

// Test POST /api/v1/instances/{instance_id}/fps - Missing fps field
TEST_F(InstanceFpsHandlerTest, SetFpsMissingField) {
  auto req = HttpRequest::newHttpRequest();
  req->setPath("/api/v1/instances/" + test_instance_id_ + "/fps");
  req->setMethod(Post);
  req->setParameter("instance_id", test_instance_id_);

  Json::Value body;
  // Missing "fps" field
  req->setBody(body.toStyledString());
  req->addHeader("Content-Type", "application/json");

  HttpResponsePtr response;
  bool callbackCalled = false;
  std::mutex callbackMutex;
  std::condition_variable callbackCv;

  handler_->setFps(req, [&](const HttpResponsePtr &resp) {
    std::lock_guard<std::mutex> lock(callbackMutex);
    callbackCalled = true;
    response = resp;
    callbackCv.notify_one();
  });

  // Wait for callback
  std::unique_lock<std::mutex> lock(callbackMutex);
  ASSERT_TRUE(callbackCv.wait_for(lock, std::chrono::milliseconds(500),
                                   [&] { return callbackCalled; }));

  ASSERT_NE(response, nullptr);
  EXPECT_EQ(response->statusCode(), k400BadRequest);
}

// Test DELETE /api/v1/instances/{instance_id}/fps - Reset to default
TEST_F(InstanceFpsHandlerTest, ResetFpsToDefault) {
  // First, set FPS to a non-default value
  auto setReq = HttpRequest::newHttpRequest();
  setReq->setPath("/api/v1/instances/" + test_instance_id_ + "/fps");
  setReq->setMethod(Post);
  setReq->setParameter("instance_id", test_instance_id_);

  Json::Value body;
  body["fps"] = 20;
  setReq->setBody(body.toStyledString());
  setReq->addHeader("Content-Type", "application/json");

  HttpResponsePtr setResponse;
  bool setCallbackCalled = false;
  std::mutex setCallbackMutex;
  std::condition_variable setCallbackCv;

  handler_->setFps(setReq, [&](const HttpResponsePtr &resp) {
    std::lock_guard<std::mutex> lock(setCallbackMutex);
    setCallbackCalled = true;
    setResponse = resp;
    setCallbackCv.notify_one();
  });

  std::unique_lock<std::mutex> setLock(setCallbackMutex);
  ASSERT_TRUE(setCallbackCv.wait_for(setLock, std::chrono::milliseconds(500),
                                      [&] { return setCallbackCalled; }));
  ASSERT_EQ(setResponse->statusCode(), k200OK);

  // Now reset to default
  auto req = HttpRequest::newHttpRequest();
  req->setPath("/api/v1/instances/" + test_instance_id_ + "/fps");
  req->setMethod(Delete);
  req->setParameter("instance_id", test_instance_id_);

  HttpResponsePtr response;
  bool callbackCalled = false;
  std::mutex callbackMutex;
  std::condition_variable callbackCv;

  handler_->resetFps(req, [&](const HttpResponsePtr &resp) {
    std::lock_guard<std::mutex> lock(callbackMutex);
    callbackCalled = true;
    response = resp;
    callbackCv.notify_one();
  });

  // Wait for callback
  std::unique_lock<std::mutex> lock(callbackMutex);
  ASSERT_TRUE(callbackCv.wait_for(lock, std::chrono::milliseconds(500),
                                   [&] { return callbackCalled; }));

  ASSERT_NE(response, nullptr);
  EXPECT_EQ(response->statusCode(), k200OK);

  auto json = response->getJsonObject();
  ASSERT_NE(json, nullptr);
  EXPECT_EQ((*json)["instance_id"].asString(), test_instance_id_);
  EXPECT_EQ((*json)["fps"].asInt(), 5); // Default FPS
  EXPECT_EQ((*json)["message"].asString(), "FPS configuration reset to default.");

  // Verify FPS was actually reset
  auto optInfo = instance_manager_->getInstance(test_instance_id_);
  ASSERT_TRUE(optInfo.has_value());
  EXPECT_EQ(optInfo.value().configuredFps, 5);
}

// Test DELETE /api/v1/instances/{instance_id}/fps - Instance not found
TEST_F(InstanceFpsHandlerTest, ResetFpsInstanceNotFound) {
  auto req = HttpRequest::newHttpRequest();
  req->setPath("/api/v1/instances/non-existent-instance/fps");
  req->setMethod(Delete);
  req->setParameter("instance_id", "non-existent-instance");

  HttpResponsePtr response;
  bool callbackCalled = false;
  std::mutex callbackMutex;
  std::condition_variable callbackCv;

  handler_->resetFps(req, [&](const HttpResponsePtr &resp) {
    std::lock_guard<std::mutex> lock(callbackMutex);
    callbackCalled = true;
    response = resp;
    callbackCv.notify_one();
  });

  // Wait for callback
  std::unique_lock<std::mutex> lock(callbackMutex);
  ASSERT_TRUE(callbackCv.wait_for(lock, std::chrono::milliseconds(500),
                                   [&] { return callbackCalled; }));

  ASSERT_NE(response, nullptr);
  EXPECT_EQ(response->statusCode(), k404NotFound);
}

// Test instance creation with default FPS (5)
TEST_F(InstanceFpsHandlerTest, CreateInstanceWithDefaultFps) {
  CreateInstanceRequest createReq;
  createReq.name = "Test Instance Default FPS";
  createReq.solution = "";
  // Don't set fps - should default to 5

  std::string instanceId = instance_manager_->createInstance(createReq);
  ASSERT_FALSE(instanceId.empty());

  // Check that default FPS is 5
  auto optInfo = instance_manager_->getInstance(instanceId);
  ASSERT_TRUE(optInfo.has_value());
  EXPECT_EQ(optInfo.value().configuredFps, 5);

  // Clean up
  instance_manager_->deleteInstance(instanceId);
}

// Test instance creation with custom FPS
TEST_F(InstanceFpsHandlerTest, CreateInstanceWithCustomFps) {
  CreateInstanceRequest createReq;
  createReq.name = "Test Instance Custom FPS";
  createReq.solution = "";
  createReq.fps = 25; // Custom FPS

  std::string instanceId = instance_manager_->createInstance(createReq);
  ASSERT_FALSE(instanceId.empty());

  // Check that custom FPS is set
  auto optInfo = instance_manager_->getInstance(instanceId);
  ASSERT_TRUE(optInfo.has_value());
  EXPECT_EQ(optInfo.value().configuredFps, 25);

  // Clean up
  instance_manager_->deleteInstance(instanceId);
}
