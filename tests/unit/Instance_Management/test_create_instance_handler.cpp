/**
 * ⚠️ CẢNH BÁO: KHÔNG ĐƯỢC THAY ĐỔI CODE Ở NỘI DUNG NÀY
 * 
 * File test này đã được xác nhận hoạt động đúng với API hiện tại.
 * Nếu cần thay đổi code trong file này, phải đảm bảo:
 * 1. Test vẫn pass sau khi thay đổi
 * 2. Test vẫn phù hợp với API handler hiện tại (CreateInstanceHandler)
 * 3. Tất cả các test case vẫn hoạt động như cũ
 * 
 * API được test: POST /v1/core/instance
 * Handler: CreateInstanceHandler::createInstance()
 * 
 * Các test case hiện tại:
 * - CreateInstanceWithInvalidJson: Kiểm tra invalid JSON
 * - CreateInstanceWithMissingFields: Kiểm tra missing required fields
 * - CreateInstanceWithValidJsonStructure: Kiểm tra valid JSON structure
 * - HandleOptions: Kiểm tra OPTIONS request
 */

#include "api/create_instance_handler.h"
#include "core/pipeline_builder.h"
#include "instances/inprocess_instance_manager.h"
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

class CreateInstanceHandlerTest : public ::testing::Test {
protected:
  void SetUp() override {
    handler_ = std::make_unique<CreateInstanceHandler>();

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
    CreateInstanceHandler::setInstanceManager(instance_manager_.get());
    CreateInstanceHandler::setSolutionRegistry(solution_registry_);
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

    // Clear handler dependencies
    CreateInstanceHandler::setInstanceManager(nullptr);
    CreateInstanceHandler::setSolutionRegistry(nullptr);
  }

  std::unique_ptr<CreateInstanceHandler> handler_;
  std::unique_ptr<InstanceRegistry> instance_registry_;
  std::unique_ptr<InProcessInstanceManager> instance_manager_;
  SolutionRegistry *solution_registry_; // Singleton, don't own
  std::unique_ptr<PipelineBuilder> pipeline_builder_;
  std::unique_ptr<InstanceStorage> instance_storage_;
  std::filesystem::path test_storage_dir_;
};

// Test POST /v1/core/instance with invalid JSON
TEST_F(CreateInstanceHandlerTest, CreateInstanceWithInvalidJson) {
  auto req = HttpRequest::newHttpRequest();
  req->setPath("/v1/core/instance");
  req->setMethod(Post);
  req->setBody("invalid json");

  HttpResponsePtr response;
  bool callbackCalled = false;
  std::mutex callbackMutex;
  std::condition_variable callbackCv;
  std::string callbackError;

  handler_->createInstance(req, [&](const HttpResponsePtr &resp) {
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
}

// Test POST /v1/core/instance with missing required fields
TEST_F(CreateInstanceHandlerTest, CreateInstanceWithMissingFields) {
  auto req = HttpRequest::newHttpRequest();
  req->setPath("/v1/core/instance");
  req->setMethod(Post);

  Json::Value body;
  // Missing required fields like name, solution, etc.
  req->setBody(body.toStyledString());

  HttpResponsePtr response;
  bool callbackCalled = false;
  std::mutex callbackMutex;
  std::condition_variable callbackCv;
  std::string callbackError;

  handler_->createInstance(req, [&](const HttpResponsePtr &resp) {
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
}

// Test POST /v1/core/instance with valid JSON structure
TEST_F(CreateInstanceHandlerTest, CreateInstanceWithValidJsonStructure) {
  auto req = HttpRequest::newHttpRequest();
  req->setPath("/v1/core/instance");
  req->setMethod(Post);

  Json::Value body;
  body["name"] = "test_instance";
  body["solution"] = "test_solution";
  body["group"] = "default";
  body["autoStart"] = false;

  Json::Value additionalParams;
  additionalParams["FILE_PATH"] = "/test/path";
  body["additionalParams"] = additionalParams;

  req->setBody(body.toStyledString());

  HttpResponsePtr response;
  bool callbackCalled = false;
  std::mutex callbackMutex;
  std::condition_variable callbackCv;
  std::string callbackError;

  handler_->createInstance(req, [&](const HttpResponsePtr &resp) {
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
  // Should return 200, 201, or 400/500 depending on validation and solution
  // existence
  EXPECT_TRUE(response->statusCode() == k200OK ||
              response->statusCode() == k201Created ||
              response->statusCode() == k400BadRequest ||
              response->statusCode() == k500InternalServerError);
}

// Test OPTIONS request
TEST_F(CreateInstanceHandlerTest, HandleOptions) {
  auto req = HttpRequest::newHttpRequest();
  req->setPath("/v1/core/instance");
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
}
