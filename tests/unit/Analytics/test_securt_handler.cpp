#include "api/securt_handler.h"
#include "core/analytics_entities_manager.h"
#include "core/logging_flags.h"
#include "core/pipeline_builder.h"
#include "core/securt_instance.h"
#include "core/securt_instance_manager.h"
#include "core/securt_instance_registry.h"
#include "core/securt_statistics_collector.h"
#include "instances/inprocess_instance_manager.h"
#include "instances/instance_registry.h"
#include "instances/instance_storage.h"
#include "solutions/solution_registry.h"
#include <chrono>
#include <drogon/HttpRequest.h>
#include <drogon/HttpResponse.h>
#include <filesystem>
#include <gtest/gtest.h>
#include <json/json.h>
#include <memory>
#include <thread>
#include <unistd.h>

using namespace drogon;

// Define logging flags for tests
// Logging flags are defined in test_main.cpp to avoid multiple definition

class SecuRTHandlerTest : public ::testing::Test {
protected:
  void SetUp() override {
    handler_ = std::make_unique<SecuRTHandler>();

    // Create temporary storage directory for testing
    test_storage_dir_ = std::filesystem::temp_directory_path() /
                        ("test_securt_instances_" + std::to_string(getpid()));
    std::filesystem::create_directories(test_storage_dir_);

    // Create dependencies for InstanceRegistry
    solution_registry_ = &SolutionRegistry::getInstance();
    solution_registry_->initializeDefaultSolutions();
    pipeline_builder_ = std::make_unique<PipelineBuilder>();
    instance_storage_ =
        std::make_unique<InstanceStorage>(test_storage_dir_.string());

    // Create InstanceRegistry
    instance_registry_ = std::make_unique<InstanceRegistry>(
        *solution_registry_, *pipeline_builder_, *instance_storage_);

    // Create InProcessInstanceManager wrapper
    core_instance_manager_ =
        std::make_unique<InProcessInstanceManager>(*instance_registry_);

    // Create SecuRTInstanceManager
    securt_instance_manager_ = std::make_unique<SecuRTInstanceManager>(
        core_instance_manager_.get());

    // Create AnalyticsEntitiesManager
    analytics_entities_manager_ = std::make_unique<AnalyticsEntitiesManager>();

    // Register with handler
    SecuRTHandler::setInstanceManager(securt_instance_manager_.get());
    SecuRTHandler::setAnalyticsEntitiesManager(
        analytics_entities_manager_.get());
  }

  void TearDown() override {
    handler_.reset();
    analytics_entities_manager_.reset();
    securt_instance_manager_.reset();
    core_instance_manager_.reset();
    instance_registry_.reset();
    instance_storage_.reset();
    pipeline_builder_.reset();

    // Clean up test storage directory
    if (std::filesystem::exists(test_storage_dir_)) {
      std::filesystem::remove_all(test_storage_dir_);
    }

    // Clear handler dependencies
    SecuRTHandler::setInstanceManager(nullptr);
    SecuRTHandler::setAnalyticsEntitiesManager(nullptr);
  }

  std::unique_ptr<SecuRTHandler> handler_;
  std::unique_ptr<SecuRTInstanceManager> securt_instance_manager_;
  std::unique_ptr<InProcessInstanceManager> core_instance_manager_;
  std::unique_ptr<InstanceRegistry> instance_registry_;
  SolutionRegistry *solution_registry_; // Singleton, don't own
  std::unique_ptr<PipelineBuilder> pipeline_builder_;
  std::unique_ptr<InstanceStorage> instance_storage_;
  std::unique_ptr<AnalyticsEntitiesManager> analytics_entities_manager_;
  std::filesystem::path test_storage_dir_;
};

// ============================================================================
// Test Create Instance (POST)
// ============================================================================

TEST_F(SecuRTHandlerTest, CreateInstanceWithInvalidJson) {
  auto req = HttpRequest::newHttpRequest();
  req->setPath("/v1/securt/instance");
  req->setMethod(Post);
  req->setBody("invalid json");

  HttpResponsePtr response;
  bool callbackCalled = false;

  handler_->createInstance(req, [&](const HttpResponsePtr &resp) {
    callbackCalled = true;
    response = resp;
  });

  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  ASSERT_TRUE(callbackCalled);
  ASSERT_NE(response, nullptr);
  EXPECT_EQ(response->statusCode(), k400BadRequest);
}

TEST_F(SecuRTHandlerTest, CreateInstanceWithMissingName) {
  auto req = HttpRequest::newHttpRequest();
  req->setPath("/v1/securt/instance");
  req->setMethod(Post);

  Json::Value body;
  // Missing required field 'name'
  req->setBody(body.toStyledString());

  HttpResponsePtr response;
  bool callbackCalled = false;

  handler_->createInstance(req, [&](const HttpResponsePtr &resp) {
    callbackCalled = true;
    response = resp;
  });

  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  ASSERT_TRUE(callbackCalled);
  ASSERT_NE(response, nullptr);
  EXPECT_EQ(response->statusCode(), k400BadRequest);
}

TEST_F(SecuRTHandlerTest, CreateInstanceWithValidData) {
  auto req = HttpRequest::newHttpRequest();
  req->setPath("/v1/securt/instance");
  req->setMethod(Post);

  Json::Value body;
  body["name"] = "Test SecuRT Instance";
  req->setBody(body.toStyledString());

  HttpResponsePtr response;
  bool callbackCalled = false;

  handler_->createInstance(req, [&](const HttpResponsePtr &resp) {
    callbackCalled = true;
    response = resp;
  });

  std::this_thread::sleep_for(std::chrono::milliseconds(200));

  ASSERT_TRUE(callbackCalled);
  ASSERT_NE(response, nullptr);
  EXPECT_EQ(response->statusCode(), k201Created);

  auto json = response->getJsonObject();
  ASSERT_NE(json, nullptr);
  EXPECT_TRUE(json->isMember("instanceId"));
  EXPECT_FALSE((*json)["instanceId"].asString().empty());
}

TEST_F(SecuRTHandlerTest, CreateInstanceWithInstanceId) {
  auto req = HttpRequest::newHttpRequest();
  req->setPath("/v1/securt/instance");
  req->setMethod(Post);

  Json::Value body;
  body["name"] = "Test Instance with ID";
  body["instanceId"] = "test-instance-001";
  req->setBody(body.toStyledString());

  HttpResponsePtr response;
  bool callbackCalled = false;

  handler_->createInstance(req, [&](const HttpResponsePtr &resp) {
    callbackCalled = true;
    response = resp;
  });

  std::this_thread::sleep_for(std::chrono::milliseconds(200));

  ASSERT_TRUE(callbackCalled);
  ASSERT_NE(response, nullptr);
  // May return 201 or 409 depending on whether instance already exists
  EXPECT_TRUE(response->statusCode() == k201Created ||
              response->statusCode() == k409Conflict);
}

TEST_F(SecuRTHandlerTest, CreateInstanceDuplicate) {
  // First create an instance
  auto req1 = HttpRequest::newHttpRequest();
  req1->setPath("/v1/securt/instance");
  req1->setMethod(Post);

  Json::Value body1;
  body1["name"] = "Duplicate Test Instance";
  body1["instanceId"] = "duplicate-test-001";
  req1->setBody(body1.toStyledString());

  HttpResponsePtr response1;
  bool callback1Called = false;
  std::string createdId;

  handler_->createInstance(req1, [&](const HttpResponsePtr &resp) {
    callback1Called = true;
    response1 = resp;
    if (resp && resp->statusCode() == k201Created) {
      auto json = resp->getJsonObject();
      if (json && json->isMember("instanceId")) {
        createdId = (*json)["instanceId"].asString();
      }
    }
  });

  std::this_thread::sleep_for(std::chrono::milliseconds(200));

  // Try to create the same instance again
  auto req2 = HttpRequest::newHttpRequest();
  req2->setPath("/v1/securt/instance");
  req2->setMethod(Post);

  Json::Value body2;
  body2["name"] = "Duplicate Test Instance 2";
  body2["instanceId"] = "duplicate-test-001";
  req2->setBody(body2.toStyledString());

  HttpResponsePtr response2;
  bool callback2Called = false;

  handler_->createInstance(req2, [&](const HttpResponsePtr &resp) {
    callback2Called = true;
    response2 = resp;
  });

  std::this_thread::sleep_for(std::chrono::milliseconds(200));

  ASSERT_TRUE(callback2Called);
  ASSERT_NE(response2, nullptr);
  EXPECT_EQ(response2->statusCode(), k409Conflict);
}

// ============================================================================
// Test Create Instance with ID (PUT)
// ============================================================================

TEST_F(SecuRTHandlerTest, CreateInstanceWithIdValid) {
  auto req = HttpRequest::newHttpRequest();
  req->setPath("/v1/securt/instance/test-put-instance-001");
  req->setMethod(Put);

  Json::Value body;
  body["name"] = "Test PUT Instance";
  req->setBody(body.toStyledString());

  HttpResponsePtr response;
  bool callbackCalled = false;

  handler_->createInstanceWithId(req, [&](const HttpResponsePtr &resp) {
    callbackCalled = true;
    response = resp;
  });

  std::this_thread::sleep_for(std::chrono::milliseconds(200));

  ASSERT_TRUE(callbackCalled);
  ASSERT_NE(response, nullptr);
  // May return 201 or 409
  EXPECT_TRUE(response->statusCode() == k201Created ||
              response->statusCode() == k409Conflict);
}

TEST_F(SecuRTHandlerTest, CreateInstanceWithIdMissingName) {
  auto req = HttpRequest::newHttpRequest();
  req->setPath("/v1/securt/instance/test-put-instance-002");
  req->setMethod(Put);

  Json::Value body;
  // Missing required field 'name'
  req->setBody(body.toStyledString());

  HttpResponsePtr response;
  bool callbackCalled = false;

  handler_->createInstanceWithId(req, [&](const HttpResponsePtr &resp) {
    callbackCalled = true;
    response = resp;
  });

  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  ASSERT_TRUE(callbackCalled);
  ASSERT_NE(response, nullptr);
  EXPECT_EQ(response->statusCode(), k400BadRequest);
}

TEST_F(SecuRTHandlerTest, CreateInstanceWithIdInvalidJson) {
  auto req = HttpRequest::newHttpRequest();
  req->setPath("/v1/securt/instance/test-put-instance-003");
  req->setMethod(Put);
  req->setBody("invalid json");

  HttpResponsePtr response;
  bool callbackCalled = false;

  handler_->createInstanceWithId(req, [&](const HttpResponsePtr &resp) {
    callbackCalled = true;
    response = resp;
  });

  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  ASSERT_TRUE(callbackCalled);
  ASSERT_NE(response, nullptr);
  EXPECT_EQ(response->statusCode(), k400BadRequest);
}

// ============================================================================
// Test Update Instance (PATCH)
// ============================================================================

TEST_F(SecuRTHandlerTest, UpdateInstanceNotFound) {
  auto req = HttpRequest::newHttpRequest();
  req->setPath("/v1/securt/instance/nonexistent-instance");
  req->setMethod(Patch);

  Json::Value body;
  body["name"] = "Updated Name";
  req->setBody(body.toStyledString());

  HttpResponsePtr response;
  bool callbackCalled = false;

  handler_->updateInstance(req, [&](const HttpResponsePtr &resp) {
    callbackCalled = true;
    response = resp;
  });

  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  ASSERT_TRUE(callbackCalled);
  ASSERT_NE(response, nullptr);
  EXPECT_EQ(response->statusCode(), k404NotFound);
}

TEST_F(SecuRTHandlerTest, UpdateInstanceValid) {
  // First create an instance
  auto createReq = HttpRequest::newHttpRequest();
  createReq->setPath("/v1/securt/instance");
  createReq->setMethod(Post);

  Json::Value createBody;
  createBody["name"] = "Instance to Update";
  createBody["instanceId"] = "update-test-001";
  createReq->setBody(createBody.toStyledString());

  HttpResponsePtr createResponse;
  bool createCallbackCalled = false;
  std::string instanceId;

  handler_->createInstance(createReq, [&](const HttpResponsePtr &resp) {
    createCallbackCalled = true;
    createResponse = resp;
    if (resp && resp->statusCode() == k201Created) {
      auto json = resp->getJsonObject();
      if (json && json->isMember("instanceId")) {
        instanceId = (*json)["instanceId"].asString();
      }
    }
  });

  std::this_thread::sleep_for(std::chrono::milliseconds(200));

  if (instanceId.empty()) {
    GTEST_SKIP() << "Failed to create instance for update test";
  }

  // Now update the instance
  auto req = HttpRequest::newHttpRequest();
  req->setPath("/v1/securt/instance/" + instanceId);
  req->setMethod(Patch);

  Json::Value body;
  body["name"] = "Updated Instance Name";
  req->setBody(body.toStyledString());

  HttpResponsePtr response;
  bool callbackCalled = false;

  handler_->updateInstance(req, [&](const HttpResponsePtr &resp) {
    callbackCalled = true;
    response = resp;
  });

  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  ASSERT_TRUE(callbackCalled);
  ASSERT_NE(response, nullptr);
  EXPECT_EQ(response->statusCode(), k204NoContent);
}

// ============================================================================
// Test Delete Instance (DELETE)
// ============================================================================

TEST_F(SecuRTHandlerTest, DeleteInstanceNotFound) {
  auto req = HttpRequest::newHttpRequest();
  req->setPath("/v1/securt/instance/nonexistent-instance");
  req->setMethod(Delete);

  HttpResponsePtr response;
  bool callbackCalled = false;

  handler_->deleteInstance(req, [&](const HttpResponsePtr &resp) {
    callbackCalled = true;
    response = resp;
  });

  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  ASSERT_TRUE(callbackCalled);
  ASSERT_NE(response, nullptr);
  EXPECT_EQ(response->statusCode(), k404NotFound);
}

TEST_F(SecuRTHandlerTest, DeleteInstanceValid) {
  // First create an instance
  auto createReq = HttpRequest::newHttpRequest();
  createReq->setPath("/v1/securt/instance");
  createReq->setMethod(Post);

  Json::Value createBody;
  createBody["name"] = "Instance to Delete";
  createBody["instanceId"] = "delete-test-001";
  createReq->setBody(createBody.toStyledString());

  HttpResponsePtr createResponse;
  bool createCallbackCalled = false;
  std::string instanceId;

  handler_->createInstance(createReq, [&](const HttpResponsePtr &resp) {
    createCallbackCalled = true;
    createResponse = resp;
    if (resp && resp->statusCode() == k201Created) {
      auto json = resp->getJsonObject();
      if (json && json->isMember("instanceId")) {
        instanceId = (*json)["instanceId"].asString();
      }
    }
  });

  std::this_thread::sleep_for(std::chrono::milliseconds(200));

  if (instanceId.empty()) {
    GTEST_SKIP() << "Failed to create instance for delete test";
  }

  // Now delete the instance
  auto req = HttpRequest::newHttpRequest();
  req->setPath("/v1/securt/instance/" + instanceId);
  req->setMethod(Delete);

  HttpResponsePtr response;
  bool callbackCalled = false;

  handler_->deleteInstance(req, [&](const HttpResponsePtr &resp) {
    callbackCalled = true;
    response = resp;
  });

  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  ASSERT_TRUE(callbackCalled);
  ASSERT_NE(response, nullptr);
  EXPECT_EQ(response->statusCode(), k204NoContent);
}

// ============================================================================
// Test Get Instance Stats (GET)
// ============================================================================

TEST_F(SecuRTHandlerTest, GetInstanceStatsNotFound) {
  auto req = HttpRequest::newHttpRequest();
  req->setPath("/v1/securt/instance/nonexistent-instance/stats");
  req->setMethod(Get);

  HttpResponsePtr response;
  bool callbackCalled = false;

  handler_->getInstanceStats(req, [&](const HttpResponsePtr &resp) {
    callbackCalled = true;
    response = resp;
  });

  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  ASSERT_TRUE(callbackCalled);
  ASSERT_NE(response, nullptr);
  EXPECT_EQ(response->statusCode(), k404NotFound);
}

TEST_F(SecuRTHandlerTest, GetInstanceStatsValid) {
  // First create an instance
  auto createReq = HttpRequest::newHttpRequest();
  createReq->setPath("/v1/securt/instance");
  createReq->setMethod(Post);

  Json::Value createBody;
  createBody["name"] = "Instance for Stats";
  createBody["instanceId"] = "stats-test-001";
  createReq->setBody(createBody.toStyledString());

  HttpResponsePtr createResponse;
  bool createCallbackCalled = false;
  std::string instanceId;

  handler_->createInstance(createReq, [&](const HttpResponsePtr &resp) {
    createCallbackCalled = true;
    createResponse = resp;
    if (resp && resp->statusCode() == k201Created) {
      auto json = resp->getJsonObject();
      if (json && json->isMember("instanceId")) {
        instanceId = (*json)["instanceId"].asString();
      }
    }
  });

  std::this_thread::sleep_for(std::chrono::milliseconds(200));

  if (instanceId.empty()) {
    GTEST_SKIP() << "Failed to create instance for stats test";
  }

  // Now get stats
  auto req = HttpRequest::newHttpRequest();
  req->setPath("/v1/securt/instance/" + instanceId + "/stats");
  req->setMethod(Get);

  HttpResponsePtr response;
  bool callbackCalled = false;

  handler_->getInstanceStats(req, [&](const HttpResponsePtr &resp) {
    callbackCalled = true;
    response = resp;
  });

  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  ASSERT_TRUE(callbackCalled);
  ASSERT_NE(response, nullptr);
  EXPECT_EQ(response->statusCode(), k200OK);

  auto json = response->getJsonObject();
  ASSERT_NE(json, nullptr);
  EXPECT_TRUE(json->isMember("startTime"));
  EXPECT_TRUE(json->isMember("frameRate"));
  EXPECT_TRUE(json->isMember("latency"));
  EXPECT_TRUE(json->isMember("framesProcessed"));
  EXPECT_TRUE(json->isMember("trackCount"));
  EXPECT_TRUE(json->isMember("isRunning"));
}

// ============================================================================
// Test Get Analytics Entities (GET)
// ============================================================================

TEST_F(SecuRTHandlerTest, GetAnalyticsEntitiesNotFound) {
  auto req = HttpRequest::newHttpRequest();
  req->setPath("/v1/securt/instance/nonexistent-instance/analytics_entities");
  req->setMethod(Get);

  HttpResponsePtr response;
  bool callbackCalled = false;

  handler_->getAnalyticsEntities(req, [&](const HttpResponsePtr &resp) {
    callbackCalled = true;
    response = resp;
  });

  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  ASSERT_TRUE(callbackCalled);
  ASSERT_NE(response, nullptr);
  // Should return 200 with empty arrays or 404
  EXPECT_TRUE(response->statusCode() == k200OK ||
              response->statusCode() == k404NotFound);
}

TEST_F(SecuRTHandlerTest, GetAnalyticsEntitiesValid) {
  // First create an instance
  auto createReq = HttpRequest::newHttpRequest();
  createReq->setPath("/v1/securt/instance");
  createReq->setMethod(Post);

  Json::Value createBody;
  createBody["name"] = "Instance for Analytics";
  createBody["instanceId"] = "analytics-test-001";
  createReq->setBody(createBody.toStyledString());

  HttpResponsePtr createResponse;
  bool createCallbackCalled = false;
  std::string instanceId;

  handler_->createInstance(createReq, [&](const HttpResponsePtr &resp) {
    createCallbackCalled = true;
    createResponse = resp;
    if (resp && resp->statusCode() == k201Created) {
      auto json = resp->getJsonObject();
      if (json && json->isMember("instanceId")) {
        instanceId = (*json)["instanceId"].asString();
      }
    }
  });

  std::this_thread::sleep_for(std::chrono::milliseconds(200));

  if (instanceId.empty()) {
    GTEST_SKIP() << "Failed to create instance for analytics test";
  }

  // Now get analytics entities
  auto req = HttpRequest::newHttpRequest();
  req->setPath("/v1/securt/instance/" + instanceId + "/analytics_entities");
  req->setMethod(Get);

  HttpResponsePtr response;
  bool callbackCalled = false;

  handler_->getAnalyticsEntities(req, [&](const HttpResponsePtr &resp) {
    callbackCalled = true;
    response = resp;
  });

  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  ASSERT_TRUE(callbackCalled);
  ASSERT_NE(response, nullptr);
  EXPECT_EQ(response->statusCode(), k200OK);

  auto json = response->getJsonObject();
  ASSERT_NE(json, nullptr);
  // Verify structure has all area and line types
  EXPECT_TRUE(json->isMember("crossingAreas"));
  EXPECT_TRUE(json->isMember("intrusionAreas"));
  EXPECT_TRUE(json->isMember("loiteringAreas"));
  EXPECT_TRUE(json->isMember("crowdingAreas"));
  EXPECT_TRUE(json->isMember("occupancyAreas"));
  EXPECT_TRUE(json->isMember("crowdEstimationAreas"));
  EXPECT_TRUE(json->isMember("dwellingAreas"));
  EXPECT_TRUE(json->isMember("armedPersonAreas"));
  EXPECT_TRUE(json->isMember("objectLeftAreas"));
  EXPECT_TRUE(json->isMember("objectRemovedAreas"));
  EXPECT_TRUE(json->isMember("fallenPersonAreas"));
}

// ============================================================================
// Test OPTIONS (CORS)
// ============================================================================

TEST_F(SecuRTHandlerTest, HandleOptions) {
  auto req = HttpRequest::newHttpRequest();
  req->setPath("/v1/securt/instance");
  req->setMethod(Options);

  HttpResponsePtr response;
  bool callbackCalled = false;

  handler_->handleOptions(req, [&](const HttpResponsePtr &resp) {
    callbackCalled = true;
    response = resp;
  });

  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  ASSERT_TRUE(callbackCalled);
  ASSERT_NE(response, nullptr);
  EXPECT_EQ(response->statusCode(), k200OK);
}

