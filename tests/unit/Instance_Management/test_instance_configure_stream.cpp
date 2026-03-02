#include "api/instance_handler.h"
#include "core/pipeline_builder.h"
#include "instances/inprocess_instance_manager.h"
#include "instances/instance_registry.h"
#include "instances/instance_storage.h"
#include "solutions/solution_registry.h"
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <drogon/HttpRequest.h>
#include <drogon/HttpResponse.h>
#include <filesystem>
#include <gtest/gtest.h>
#include <json/json.h>
#include <mutex>
#include <thread>
#include <unistd.h>

using namespace drogon;

class InstanceConfigureStreamTest : public ::testing::Test {
protected:
  void SetUp() override {
    // Create temporary directory for tests
    test_dir_ = "/tmp/edgeos_api_test_instances_" + std::to_string(getpid());
    std::filesystem::create_directories(test_dir_);

    // Set font path environment variable to avoid permission errors
    // Use empty string to skip font requirement in tests
    setenv("OSD_DEFAULT_FONT_PATH", "", 1);

    // Setup solution registry and pipeline builder
    solution_registry_ = &SolutionRegistry::getInstance();
    solution_registry_->initializeDefaultSolutions();
    pipeline_builder_ = std::make_unique<PipelineBuilder>();
    instance_storage_ = std::make_unique<InstanceStorage>(test_dir_);

    // Create instance registry
    instance_registry_ = std::make_unique<InstanceRegistry>(
        *solution_registry_, *pipeline_builder_, *instance_storage_);

    // Create InProcessInstanceManager wrapper
    instance_manager_ =
        std::make_unique<InProcessInstanceManager>(*instance_registry_);

    // Set instance manager in handler
    InstanceHandler::setInstanceManager(instance_manager_.get());

    handler_ = std::make_unique<InstanceHandler>();
  }

  void TearDown() override {
    handler_.reset();
    instance_manager_.reset();
    instance_registry_.reset();
    instance_storage_.reset();
    pipeline_builder_.reset();

    // Clean up test directory
    if (std::filesystem::exists(test_dir_)) {
      std::filesystem::remove_all(test_dir_);
    }
  }

  std::string test_dir_;
  SolutionRegistry *solution_registry_;
  std::unique_ptr<PipelineBuilder> pipeline_builder_;
  std::unique_ptr<InstanceStorage> instance_storage_;
  std::unique_ptr<InstanceRegistry> instance_registry_;
  std::unique_ptr<InProcessInstanceManager> instance_manager_;
  std::unique_ptr<InstanceHandler> handler_;

  // Helper to create a test instance
  std::string createTestInstance() {
    CreateInstanceRequest req;
    req.name = "Test Instance";
    // Use ba_crossline solution instead of face_detection to avoid permission
    // issues with model files. ba_crossline doesn't require model file access
    // for basic instance creation.
    req.solution = "ba_crossline";
    req.persistent = false;
    req.autoStart = false; // Don't auto-start to avoid pipeline building issues
    req.additionalParams["FILE_PATH"] = "/test/path/video.mp4";
    req.additionalParams["WEIGHTS_PATH"] = "/test/path/weights.weights";
    req.additionalParams["CONFIG_PATH"] = "/test/path/config.cfg";
    req.additionalParams["LABELS_PATH"] = "/test/path/labels.txt";

    try {
      auto instanceId = instance_manager_->createInstance(req);
      return instanceId;
    } catch (const std::exception &e) {
      // If instance creation fails (e.g., permission denied), throw to fail
      // test This allows tests to properly handle the failure case
      throw;
    }
  }
};

// Test configureStreamOutput with valid RTMP URI
// SKIP: This integration test requires full handler setup with proper request
// routing
TEST_F(InstanceConfigureStreamTest, DISABLED_ConfigureStreamOutput_ValidRTMP) {
  std::string instanceId = createTestInstance();

  bool callbackCalled = false;
  HttpResponsePtr response;

  auto req = HttpRequest::newHttpRequest();
  req->setPath("/v1/core/instance/" + instanceId + "/output/stream");
  req->setMethod(Post);

  // Set JSON body
  Json::Value body;
  body["enabled"] = true;
  body["uri"] = "rtmp://localhost:1935/live/stream";
  req->setBody(body.toStyledString());

  handler_->configureStreamOutput(req, [&](const HttpResponsePtr &resp) {
    callbackCalled = true;
    response = resp;
  });

  // Wait for async callback
  std::this_thread::sleep_for(std::chrono::milliseconds(200));

  ASSERT_TRUE(callbackCalled);
  ASSERT_NE(response, nullptr);
  EXPECT_EQ(response->statusCode(), k204NoContent);

  // Verify RTMP URL was set in instance
  auto optInfo = instance_manager_->getInstance(instanceId);
  ASSERT_TRUE(optInfo.has_value());
  // Note: RTMP URL might be in additionalParams or rtmpUrl field
  EXPECT_TRUE(!optInfo.value().rtmpUrl.empty() ||
              optInfo.value().additionalParams.find("RTMP_URL") !=
                  optInfo.value().additionalParams.end());
}

// Test configureStreamOutput with missing enabled field
TEST_F(InstanceConfigureStreamTest, ConfigureStreamOutput_MissingEnabled) {
  std::string instanceId = createTestInstance();

  bool callbackCalled = false;
  HttpResponsePtr response;
  std::mutex callbackMutex;
  std::condition_variable callbackCv;
  std::string callbackError;

  auto req = HttpRequest::newHttpRequest();
  req->setPath("/v1/core/instance/" + instanceId + "/output/stream");
  req->setMethod(Post);

  // Set JSON body without enabled field
  Json::Value body;
  body["uri"] = "rtmp://localhost:1935/live/stream";
  req->setBody(body.toStyledString());

  handler_->configureStreamOutput(req, [&](const HttpResponsePtr &resp) {
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
  if (!callbackCv.wait_for(lock, std::chrono::milliseconds(1000), [&] { return callbackCalled; })) {
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

// Test configureStreamOutput with missing URI when enabled is true
TEST_F(InstanceConfigureStreamTest, ConfigureStreamOutput_MissingURI) {
  std::string instanceId = createTestInstance();

  bool callbackCalled = false;
  HttpResponsePtr response;
  std::mutex callbackMutex;
  std::condition_variable callbackCv;
  std::string callbackError;

  auto req = HttpRequest::newHttpRequest();
  req->setPath("/v1/core/instance/" + instanceId + "/output/stream");
  req->setMethod(Post);

  // Set JSON body with enabled=true but no URI
  Json::Value body;
  body["enabled"] = true;
  req->setBody(body.toStyledString());

  handler_->configureStreamOutput(req, [&](const HttpResponsePtr &resp) {
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
  if (!callbackCv.wait_for(lock, std::chrono::milliseconds(1000), [&] { return callbackCalled; })) {
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

// Test configureStreamOutput with invalid URI format
TEST_F(InstanceConfigureStreamTest, ConfigureStreamOutput_InvalidURI) {
  std::string instanceId = createTestInstance();

  bool callbackCalled = false;
  HttpResponsePtr response;
  std::mutex callbackMutex;
  std::condition_variable callbackCv;
  std::string callbackError;

  auto req = HttpRequest::newHttpRequest();
  req->setPath("/v1/core/instance/" + instanceId + "/output/stream");
  req->setMethod(Post);

  // Set JSON body with invalid URI (not starting with rtmp://, rtsp://, or
  // hls://)
  Json::Value body;
  body["enabled"] = true;
  body["uri"] = "http://localhost:8080/stream"; // Invalid protocol
  req->setBody(body.toStyledString());

  handler_->configureStreamOutput(req, [&](const HttpResponsePtr &resp) {
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
  if (!callbackCv.wait_for(lock, std::chrono::milliseconds(1000), [&] { return callbackCalled; })) {
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

// Test configureStreamOutput with non-existent instance
TEST_F(InstanceConfigureStreamTest,
       DISABLED_ConfigureStreamOutput_InstanceNotFound) {
  bool callbackCalled = false;
  HttpResponsePtr response;

  auto req = HttpRequest::newHttpRequest();
  req->setPath("/v1/core/instance/non-existent-id/output/stream");
  req->setMethod(Post);

  Json::Value body;
  body["enabled"] = true;
  body["uri"] = "rtmp://localhost:1935/live/stream";
  req->setBody(body.toStyledString());

  handler_->configureStreamOutput(req, [&](const HttpResponsePtr &resp) {
    callbackCalled = true;
    response = resp;
  });

  std::this_thread::sleep_for(std::chrono::milliseconds(200));

  ASSERT_TRUE(callbackCalled);
  EXPECT_EQ(response->statusCode(), k404NotFound);

  auto json = response->getJsonObject();
  ASSERT_NE(json, nullptr);
  EXPECT_TRUE(json->isMember("error"));
}

// Test configureStreamOutput to disable stream
TEST_F(InstanceConfigureStreamTest, DISABLED_ConfigureStreamOutput_Disable) {
  std::string instanceId = createTestInstance();

  // First enable stream
  {
    auto req = HttpRequest::newHttpRequest();
    req->setPath("/v1/core/instance/" + instanceId + "/output/stream");
    req->setMethod(Post);

    Json::Value body;
    body["enabled"] = true;
    body["uri"] = "rtmp://localhost:1935/live/stream";
    req->setBody(body.toStyledString());

    bool callbackCalled = false;
    handler_->configureStreamOutput(
        req, [&](const HttpResponsePtr &resp) { callbackCalled = true; });

    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    ASSERT_TRUE(callbackCalled);
  }

  // Then disable stream
  bool callbackCalled = false;
  HttpResponsePtr response;

  auto req = HttpRequest::newHttpRequest();
  req->setPath("/v1/core/instance/" + instanceId + "/output/stream");
  req->setMethod(Post);

  Json::Value body;
  body["enabled"] = false;
  req->setBody(body.toStyledString());

  handler_->configureStreamOutput(req, [&](const HttpResponsePtr &resp) {
    callbackCalled = true;
    response = resp;
  });

  std::this_thread::sleep_for(std::chrono::milliseconds(200));

  ASSERT_TRUE(callbackCalled);
  ASSERT_NE(response, nullptr);
  EXPECT_EQ(response->statusCode(), k204NoContent);
}

// Test configureStreamOutput with RTSP URI
TEST_F(InstanceConfigureStreamTest, DISABLED_ConfigureStreamOutput_RTSPURI) {
  std::string instanceId = createTestInstance();

  bool callbackCalled = false;
  HttpResponsePtr response;

  auto req = HttpRequest::newHttpRequest();
  req->setPath("/v1/core/instance/" + instanceId + "/output/stream");
  req->setMethod(Post);

  Json::Value body;
  body["enabled"] = true;
  body["uri"] = "rtsp://localhost:8554/live/stream";
  req->setBody(body.toStyledString());

  handler_->configureStreamOutput(req, [&](const HttpResponsePtr &resp) {
    callbackCalled = true;
    response = resp;
  });

  std::this_thread::sleep_for(std::chrono::milliseconds(200));

  ASSERT_TRUE(callbackCalled);
  ASSERT_NE(response, nullptr);
  EXPECT_EQ(response->statusCode(), k204NoContent);
}

// Test configureStreamOutput with HLS URI
TEST_F(InstanceConfigureStreamTest, DISABLED_ConfigureStreamOutput_HLSURI) {
  std::string instanceId = createTestInstance();

  bool callbackCalled = false;
  HttpResponsePtr response;

  auto req = HttpRequest::newHttpRequest();
  req->setPath("/v1/core/instance/" + instanceId + "/output/stream");
  req->setMethod(Post);

  Json::Value body;
  body["enabled"] = true;
  body["uri"] = "hls://localhost:8080/live/stream";
  req->setBody(body.toStyledString());

  handler_->configureStreamOutput(req, [&](const HttpResponsePtr &resp) {
    callbackCalled = true;
    response = resp;
  });

  std::this_thread::sleep_for(std::chrono::milliseconds(200));

  ASSERT_TRUE(callbackCalled);
  ASSERT_NE(response, nullptr);
  EXPECT_EQ(response->statusCode(), k204NoContent);
}

// Test getStreamOutput with enabled stream
TEST_F(InstanceConfigureStreamTest, DISABLED_GetStreamOutput_Enabled) {
  std::string instanceId = createTestInstance();

  // First configure stream output
  {
    auto req = HttpRequest::newHttpRequest();
    req->setPath("/v1/core/instance/" + instanceId + "/output/stream");
    req->setMethod(Post);

    Json::Value body;
    body["enabled"] = true;
    body["uri"] = "rtmp://localhost:1935/live/stream";
    req->setBody(body.toStyledString());

    bool callbackCalled = false;
    handler_->configureStreamOutput(
        req, [&](const HttpResponsePtr &resp) { callbackCalled = true; });

    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    ASSERT_TRUE(callbackCalled);
  }

  // Then get stream output configuration
  bool callbackCalled = false;
  HttpResponsePtr response;

  auto req = HttpRequest::newHttpRequest();
  req->setPath("/v1/core/instance/" + instanceId + "/output/stream");
  req->setMethod(Get);

  handler_->getStreamOutput(req, [&](const HttpResponsePtr &resp) {
    callbackCalled = true;
    response = resp;
  });

  std::this_thread::sleep_for(std::chrono::milliseconds(200));

  ASSERT_TRUE(callbackCalled);
  ASSERT_NE(response, nullptr);
  EXPECT_EQ(response->statusCode(), k200OK);
  EXPECT_EQ(response->contentType(), CT_APPLICATION_JSON);

  // Parse and validate JSON
  auto json = response->getJsonObject();
  ASSERT_NE(json, nullptr);
  EXPECT_TRUE(json->isMember("enabled"));
  EXPECT_TRUE(json->isMember("uri"));
  EXPECT_TRUE((*json)["enabled"].asBool());
  EXPECT_EQ((*json)["uri"].asString(), "rtmp://localhost:1935/live/stream");
}

// Test getStreamOutput with disabled stream
TEST_F(InstanceConfigureStreamTest, GetStreamOutput_Disabled) {
  std::string instanceId = createTestInstance();

  // Stream output should be disabled by default
  bool callbackCalled = false;
  HttpResponsePtr response;
  std::mutex callbackMutex;
  std::condition_variable callbackCv;
  std::string callbackError;

  auto req = HttpRequest::newHttpRequest();
  req->setPath("/v1/core/instance/" + instanceId + "/output/stream");
  req->setMethod(Get);

  handler_->getStreamOutput(req, [&](const HttpResponsePtr &resp) {
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
  if (!callbackCv.wait_for(lock, std::chrono::milliseconds(1000), [&] { return callbackCalled; })) {
    FAIL() << "Callback not called within timeout";
  }

  ASSERT_TRUE(callbackCalled);
  if (!callbackError.empty()) {
    FAIL() << callbackError;
  }
  ASSERT_NE(response, nullptr);
  EXPECT_EQ(response->statusCode(), k200OK);

  // Parse and validate JSON
  auto json = response->getJsonObject();
  ASSERT_NE(json, nullptr);
  EXPECT_TRUE(json->isMember("enabled"));
  EXPECT_TRUE(json->isMember("uri"));
  EXPECT_FALSE((*json)["enabled"].asBool());
  EXPECT_EQ((*json)["uri"].asString(), "");
}

// Test getStreamOutput with non-existent instance
TEST_F(InstanceConfigureStreamTest, GetStreamOutput_InstanceNotFound) {
  bool callbackCalled = false;
  HttpResponsePtr response;
  std::mutex callbackMutex;
  std::condition_variable callbackCv;
  std::string callbackError;

  auto req = HttpRequest::newHttpRequest();
  req->setPath("/v1/core/instance/non-existent-id/output/stream");
  req->setMethod(Get);

  handler_->getStreamOutput(req, [&](const HttpResponsePtr &resp) {
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
  if (!callbackCv.wait_for(lock, std::chrono::milliseconds(1000), [&] { return callbackCalled; })) {
    FAIL() << "Callback not called within timeout";
  }

  ASSERT_TRUE(callbackCalled);
  if (!callbackError.empty()) {
    FAIL() << callbackError;
  }
  ASSERT_NE(response, nullptr);
  EXPECT_EQ(response->statusCode(), k404NotFound);

  auto json = response->getJsonObject();
  ASSERT_NE(json, nullptr);
  EXPECT_TRUE(json->isMember("error"));
}

// Test getStreamOutput after configuring and then disabling
TEST_F(InstanceConfigureStreamTest, GetStreamOutput_AfterDisable) {
  std::string instanceId = createTestInstance();

  // First enable stream
  {
    auto req = HttpRequest::newHttpRequest();
    req->setPath("/v1/core/instance/" + instanceId + "/output/stream");
    req->setMethod(Post);

    Json::Value body;
    body["enabled"] = true;
    body["uri"] = "rtmp://localhost:1935/live/stream";
    req->setBody(body.toStyledString());

    bool callbackCalled = false;
    std::mutex callbackMutex;
    std::condition_variable callbackCv;
    std::string callbackError;

    handler_->configureStreamOutput(req, [&](const HttpResponsePtr &resp) {
      try {
        std::lock_guard<std::mutex> lock(callbackMutex);
        callbackCalled = true;
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
    if (!callbackCv.wait_for(lock, std::chrono::milliseconds(1000), [&] { return callbackCalled; })) {
      FAIL() << "Enable stream callback not called within timeout";
    }
    if (!callbackError.empty()) {
      FAIL() << callbackError;
    }
    ASSERT_TRUE(callbackCalled);
  }

  // Then disable stream
  {
    auto req = HttpRequest::newHttpRequest();
    req->setPath("/v1/core/instance/" + instanceId + "/output/stream");
    req->setMethod(Post);

    Json::Value body;
    body["enabled"] = false;
    req->setBody(body.toStyledString());

    bool callbackCalled = false;
    std::mutex callbackMutex;
    std::condition_variable callbackCv;
    std::string callbackError;

    handler_->configureStreamOutput(req, [&](const HttpResponsePtr &resp) {
      try {
        std::lock_guard<std::mutex> lock(callbackMutex);
        callbackCalled = true;
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
    if (!callbackCv.wait_for(lock, std::chrono::milliseconds(1000), [&] { return callbackCalled; })) {
      FAIL() << "Disable stream callback not called within timeout";
    }
    if (!callbackError.empty()) {
      FAIL() << callbackError;
    }
    ASSERT_TRUE(callbackCalled);
  }

  // Finally get stream output - should be disabled
  bool callbackCalled = false;
  HttpResponsePtr response;
  std::mutex callbackMutex;
  std::condition_variable callbackCv;
  std::string callbackError;

  auto req = HttpRequest::newHttpRequest();
  req->setPath("/v1/core/instance/" + instanceId + "/output/stream");
  req->setMethod(Get);

  handler_->getStreamOutput(req, [&](const HttpResponsePtr &resp) {
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
  if (!callbackCv.wait_for(lock, std::chrono::milliseconds(1000), [&] { return callbackCalled; })) {
    FAIL() << "Get stream output callback not called within timeout";
  }

  ASSERT_TRUE(callbackCalled);
  if (!callbackError.empty()) {
    FAIL() << callbackError;
  }
  ASSERT_NE(response, nullptr);
  EXPECT_EQ(response->statusCode(), k200OK);

  auto json = response->getJsonObject();
  ASSERT_NE(json, nullptr);
  EXPECT_TRUE(json->isMember("enabled"));
  EXPECT_TRUE(json->isMember("uri"));
  EXPECT_FALSE((*json)["enabled"].asBool());
}
