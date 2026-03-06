#include "api/stops_handler.h"
#include "core/pipeline_builder.h"
#include "instances/inprocess_instance_manager.h"
#include "instances/instance_registry.h"
#include "instances/instance_storage.h"
#include "models/create_instance_request.h"
#include "solutions/solution_registry.h"
#include <chrono>
#include <cstdlib>
#include <drogon/HttpRequest.h>
#include <drogon/HttpResponse.h>
#include <filesystem>
#include <gtest/gtest.h>
#include <json/json.h>
#include <memory>
#include <thread>
#include <unistd.h>

using namespace drogon;

class StopsHandlerTest : public ::testing::Test {
protected:
  void SetUp() override {
    handler_ = std::make_unique<StopsHandler>();

    // Create temporary storage directory for testing
    test_storage_dir_ = std::filesystem::temp_directory_path() /
                        ("test_instances_" + std::to_string(getpid()));
    std::filesystem::create_directories(test_storage_dir_);

    // Set font path environment variable to avoid permission errors
    setenv("OSD_DEFAULT_FONT_PATH", "", 1);

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
    instance_manager_ =
        std::make_unique<InProcessInstanceManager>(*instance_registry_);

    // Register with handler (using IInstanceManager interface)
    StopsHandler::setInstanceManager(instance_manager_.get());

    // Create a test instance with ba_stop solution
    createTestInstance();
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
    StopsHandler::setInstanceManager(nullptr);
  }

  void createTestInstance() {
    // Create instance via instance manager
    CreateInstanceRequest req;
    req.name = "test_ba_stop_instance";
    // Leave solution empty to avoid building runtime pipeline in unit tests
    req.solution = "";
    req.group = "test";
    req.autoStart = false;

    // Set FILE_PATH directly in additionalParams to avoid permission errors
    req.additionalParams["FILE_PATH"] = "/tmp/test_video.mp4";

    // Add input parameters
    Json::Value input;
    input["FILE_PATH"] = "/tmp/test_video.mp4";
    input["WEIGHTS_PATH"] = "/test/path/weights.weights";
    input["CONFIG_PATH"] = "/test/path/config.cfg";
    input["LABELS_PATH"] = "/test/path/labels.txt";

    Json::StreamWriterBuilder builder;
    builder["indentation"] = "";
    req.additionalParams["input"] = Json::writeString(builder, input);

    instance_id_ = instance_manager_->createInstance(req);
  }

  std::unique_ptr<StopsHandler> handler_;
  std::unique_ptr<InstanceRegistry> instance_registry_;
  std::unique_ptr<InProcessInstanceManager> instance_manager_;
  SolutionRegistry *solution_registry_; // Singleton, don't own
  std::unique_ptr<PipelineBuilder> pipeline_builder_;
  std::unique_ptr<InstanceStorage> instance_storage_;
  std::filesystem::path test_storage_dir_;
  std::string instance_id_;
};

// Test GET /v1/core/instance/{instanceId}/stops - Instance not found
TEST_F(StopsHandlerTest, GetAllStopsInstanceNotFound) {
  auto req = HttpRequest::newHttpRequest();
  req->setPath("/v1/core/instance/nonexistent-id/stops");
  req->setMethod(Get);

  HttpResponsePtr response;
  bool callbackCalled = false;

  handler_->getAllStops(req, [&](const HttpResponsePtr &resp) {
    callbackCalled = true;
    response = resp;
  });

  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  ASSERT_TRUE(callbackCalled);
  ASSERT_NE(response, nullptr);
  EXPECT_EQ(response->statusCode(), k404NotFound);
}

// Test GET /v1/core/instance/{instanceId}/stops - Success (empty)
TEST_F(StopsHandlerTest, GetAllStopsEmpty) {
  if (instance_id_.empty()) {
    GTEST_SKIP() << "Test instance not created, skipping test";
  }

  auto req = HttpRequest::newHttpRequest();
  req->setPath("/v1/core/instance/" + instance_id_ + "/stops");
  req->setMethod(Get);

  HttpResponsePtr response;
  bool callbackCalled = false;

  handler_->getAllStops(req, [&](const HttpResponsePtr &resp) {
    callbackCalled = true;
    response = resp;
  });

  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  ASSERT_TRUE(callbackCalled);
  ASSERT_NE(response, nullptr);
  EXPECT_EQ(response->statusCode(), k200OK);

  auto json = response->getJsonObject();
  ASSERT_NE(json, nullptr);
  EXPECT_TRUE(json->isMember("stopZones"));
  EXPECT_TRUE((*json)["stopZones"].isArray());
  EXPECT_EQ((*json)["stopZones"].size(), 0);
}

// Test POST /v1/core/instance/{instanceId}/stops - Create stop
TEST_F(StopsHandlerTest, CreateStop) {
  if (instance_id_.empty()) {
    GTEST_SKIP() << "Test instance not created, skipping test";
  }

  auto req = HttpRequest::newHttpRequest();
  req->setPath("/v1/core/instance/" + instance_id_ + "/stops");
  req->setMethod(Post);

  Json::Value body;
  Json::Value roi(Json::arrayValue);
  Json::Value p1; p1["x"] = 0; p1["y"] = 0;
  Json::Value p2; p2["x"] = 10; p2["y"] = 0;
  Json::Value p3; p3["x"] = 10; p3["y"] = 10;
  roi.append(p1); roi.append(p2); roi.append(p3);
  body["roi"] = roi;
  body["name"] = "Test Stop Zone";

  req->setBody(body.toStyledString());
  req->addHeader("Content-Type", "application/json");

  HttpResponsePtr response;
  bool callbackCalled = false;

  handler_->createStop(req, [&](const HttpResponsePtr &resp) {
    callbackCalled = true;
    response = resp;
  });

  std::this_thread::sleep_for(std::chrono::milliseconds(200));

  ASSERT_TRUE(callbackCalled);
  ASSERT_NE(response, nullptr);
  EXPECT_EQ(response->statusCode(), k201Created);

  auto json = response->getJsonObject();
  ASSERT_NE(json, nullptr);
  EXPECT_TRUE(json->isMember("id"));
  EXPECT_TRUE(json->isMember("roi"));
  EXPECT_TRUE(json->isMember("name"));
  EXPECT_EQ((*json)["name"].asString(), "Test Stop Zone");
}

// Test POST invalid ROI
TEST_F(StopsHandlerTest, CreateStopInvalidROI) {
  if (instance_id_.empty()) {
    GTEST_SKIP() << "Test instance not created, skipping test";
  }

  auto req = HttpRequest::newHttpRequest();
  req->setPath("/v1/core/instance/" + instance_id_ + "/stops");
  req->setMethod(Post);

  Json::Value body;
  Json::Value roi(Json::arrayValue);
  Json::Value p1; p1["x"] = 0; p1["y"] = 0;
  Json::Value p2; p2["x"] = 10; p2["y"] = 0;
  roi.append(p1); roi.append(p2); // only 2 points -> invalid
  body["roi"] = roi;

  req->setBody(body.toStyledString());

  HttpResponsePtr response;
  bool callbackCalled = false;

  handler_->createStop(req, [&](const HttpResponsePtr &resp) {
    callbackCalled = true;
    response = resp;
  });

  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  ASSERT_TRUE(callbackCalled);
  ASSERT_NE(response, nullptr);
  EXPECT_EQ(response->statusCode(), k400BadRequest);
}

// Test POST with unsupported parameters (classes/color) should be rejected
TEST_F(StopsHandlerTest, CreateStopRejectsUnsupportedParams) {
  if (instance_id_.empty()) {
    GTEST_SKIP() << "Test instance not created, skipping test";
  }

  auto req = HttpRequest::newHttpRequest();
  req->setPath("/v1/core/instance/" + instance_id_ + "/stops");
  req->setMethod(Post);

  Json::Value body;
  Json::Value roi(Json::arrayValue);
  Json::Value p1; p1["x"] = 0; p1["y"] = 0;
  Json::Value p2; p2["x"] = 10; p2["y"] = 0;
  Json::Value p3; p3["x"] = 10; p3["y"] = 10;
  roi.append(p1); roi.append(p2); roi.append(p3);
  body["roi"] = roi;
  Json::Value classes(Json::arrayValue);
  classes.append("Vehicle");
  body["classes"] = classes;

  req->setBody(body.toStyledString());

  HttpResponsePtr response;
  bool callbackCalled = false;

  handler_->createStop(req, [&](const HttpResponsePtr &resp) {
    callbackCalled = true;
    response = resp;
  });

  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  ASSERT_TRUE(callbackCalled);
  ASSERT_NE(response, nullptr);
  EXPECT_EQ(response->statusCode(), k400BadRequest);
}
// Test create -> get by id
TEST_F(StopsHandlerTest, GetStopById) {
  if (instance_id_.empty()) {
    GTEST_SKIP() << "Test instance not created, skipping test";
  }

  // Create stop
  auto createReq = HttpRequest::newHttpRequest();
  createReq->setPath("/v1/core/instance/" + instance_id_ + "/stops");
  createReq->setMethod(Post);

  Json::Value body;
  Json::Value roi(Json::arrayValue);
  Json::Value p1; p1["x"] = 0; p1["y"] = 0;
  Json::Value p2; p2["x"] = 10; p2["y"] = 0;
  Json::Value p3; p3["x"] = 10; p3["y"] = 10;
  roi.append(p1); roi.append(p2); roi.append(p3);
  body["roi"] = roi;

  createReq->setBody(body.toStyledString());

  HttpResponsePtr createResponse;
  bool createCallbackCalled = false;
  std::string stop_id;

  handler_->createStop(createReq, [&](const HttpResponsePtr &resp) {
    createCallbackCalled = true;
    createResponse = resp;
    if (resp && resp->statusCode() == k201Created) {
      auto json = resp->getJsonObject();
      if (json && json->isMember("id")) {
        stop_id = (*json)["id"].asString();
      }
    }
  });

  std::this_thread::sleep_for(std::chrono::milliseconds(200));

  if (stop_id.empty()) {
    GTEST_SKIP() << "Failed to create stop for test, skipping";
  }

  // Now get by id
  auto req = HttpRequest::newHttpRequest();
  req->setPath("/v1/core/instance/" + instance_id_ + "/stops/" + stop_id);
  req->setMethod(Get);

  HttpResponsePtr response;
  bool callbackCalled = false;

  handler_->getStop(req, [&](const HttpResponsePtr &resp) {
    callbackCalled = true;
    response = resp;
  });

  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  ASSERT_TRUE(callbackCalled);
  ASSERT_NE(response, nullptr);
  EXPECT_EQ(response->statusCode(), k200OK);

  auto json = response->getJsonObject();
  ASSERT_NE(json, nullptr);
  EXPECT_TRUE(json->isMember("id"));
  EXPECT_EQ((*json)["id"].asString(), stop_id);
}

// Test GET stop not found
TEST_F(StopsHandlerTest, GetStopByIdNotFound) {
  if (instance_id_.empty()) {
    GTEST_SKIP() << "Test instance not created, skipping test";
  }

  auto req = HttpRequest::newHttpRequest();
  req->setPath("/v1/core/instance/" + instance_id_ + "/stops/nonexistent-stop-id");
  req->setMethod(Get);

  HttpResponsePtr response;
  bool callbackCalled = false;

  handler_->getStop(req, [&](const HttpResponsePtr &resp) {
    callbackCalled = true;
    response = resp;
  });

  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  ASSERT_TRUE(callbackCalled);
  ASSERT_NE(response, nullptr);
  EXPECT_EQ(response->statusCode(), k404NotFound);
}

// Test update stop
TEST_F(StopsHandlerTest, UpdateStop) {
  if (instance_id_.empty()) {
    GTEST_SKIP() << "Test instance not created, skipping test";
  }

  // Create stop
  auto createReq = HttpRequest::newHttpRequest();
  createReq->setPath("/v1/core/instance/" + instance_id_ + "/stops");
  createReq->setMethod(Post);

  Json::Value body;
  Json::Value roi(Json::arrayValue);
  Json::Value p1; p1["x"] = 0; p1["y"] = 0;
  Json::Value p2; p2["x"] = 10; p2["y"] = 0;
  Json::Value p3; p3["x"] = 10; p3["y"] = 10;
  roi.append(p1); roi.append(p2); roi.append(p3);
  body["roi"] = roi;
  body["name"] = "Initial Stop Name";

  createReq->setBody(body.toStyledString());

  HttpResponsePtr createResponse;
  bool createCallbackCalled = false;
  std::string stop_id;

  handler_->createStop(createReq, [&](const HttpResponsePtr &resp) {
    createCallbackCalled = true;
    createResponse = resp;
    if (resp && resp->statusCode() == k201Created) {
      auto json = resp->getJsonObject();
      if (json && json->isMember("id")) {
        stop_id = (*json)["id"].asString();
      }
    }
  });

  std::this_thread::sleep_for(std::chrono::milliseconds(200));

  if (stop_id.empty()) {
    GTEST_SKIP() << "Failed to create stop for test, skipping";
  }

  // Update stop
  auto req = HttpRequest::newHttpRequest();
  req->setPath("/v1/core/instance/" + instance_id_ + "/stops/" + stop_id);
  req->setMethod(Put);

  Json::Value updateBody;
  updateBody["min_stop_seconds"] = 5;
  updateBody["name"] = "Updated Stop Name";

  req->setBody(updateBody.toStyledString());

  HttpResponsePtr response;
  bool callbackCalled = false;

  handler_->updateStop(req, [&](const HttpResponsePtr &resp) {
    callbackCalled = true;
    response = resp;
  });

  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  ASSERT_TRUE(callbackCalled);
  ASSERT_NE(response, nullptr);
  EXPECT_EQ(response->statusCode(), k200OK);

  // Verify updated name via GET
  auto getReq = HttpRequest::newHttpRequest();
  getReq->setPath("/v1/core/instance/" + instance_id_ + "/stops/" + stop_id);
  getReq->setMethod(Get);
  HttpResponsePtr getResp;
  bool getCalled = false;
  handler_->getStop(getReq, [&](const HttpResponsePtr &resp) { getCalled = true; getResp = resp; });
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  ASSERT_TRUE(getCalled);
  ASSERT_NE(getResp, nullptr);
  EXPECT_EQ(getResp->statusCode(), k200OK);
  auto getJson = getResp->getJsonObject();
  ASSERT_NE(getJson, nullptr);
  EXPECT_TRUE(getJson->isMember("name"));
  EXPECT_EQ((*getJson)["name"].asString(), "Updated Stop Name");
}

// Test delete stop
TEST_F(StopsHandlerTest, DeleteStop) {
  if (instance_id_.empty()) {
    GTEST_SKIP() << "Test instance not created, skipping test";
  }

  // Create stop
  auto createReq = HttpRequest::newHttpRequest();
  createReq->setPath("/v1/core/instance/" + instance_id_ + "/stops");
  createReq->setMethod(Post);

  Json::Value body;
  Json::Value roi(Json::arrayValue);
  Json::Value p1; p1["x"] = 0; p1["y"] = 0;
  Json::Value p2; p2["x"] = 10; p2["y"] = 0;
  Json::Value p3; p3["x"] = 10; p3["y"] = 10;
  roi.append(p1); roi.append(p2); roi.append(p3);
  body["roi"] = roi;

  createReq->setBody(body.toStyledString());

  HttpResponsePtr createResponse;
  bool createCallbackCalled = false;
  std::string stop_id;

  handler_->createStop(createReq, [&](const HttpResponsePtr &resp) {
    createCallbackCalled = true;
    createResponse = resp;
    if (resp && resp->statusCode() == k201Created) {
      auto json = resp->getJsonObject();
      if (json && json->isMember("id")) {
        stop_id = (*json)["id"].asString();
      }
    }
  });

  std::this_thread::sleep_for(std::chrono::milliseconds(200));

  if (stop_id.empty()) {
    GTEST_SKIP() << "Failed to create stop for test, skipping";
  }

  auto req = HttpRequest::newHttpRequest();
  req->setPath("/v1/core/instance/" + instance_id_ + "/stops/" + stop_id);
  req->setMethod(Delete);

  HttpResponsePtr response;
  bool callbackCalled = false;

  handler_->deleteStop(req, [&](const HttpResponsePtr &resp) {
    callbackCalled = true;
    response = resp;
  });

  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  ASSERT_TRUE(callbackCalled);
  ASSERT_NE(response, nullptr);
  EXPECT_EQ(response->statusCode(), k200OK);
}

// Test batch update
TEST_F(StopsHandlerTest, BatchUpdateStops) {
  if (instance_id_.empty()) {
    GTEST_SKIP() << "Test instance not created, skipping test";
  }

  auto req = HttpRequest::newHttpRequest();
  req->setPath("/v1/core/instance/" + instance_id_ + "/stops/batch");
  req->setMethod(Post);

  Json::Value arr(Json::arrayValue);

  for (int i = 0; i < 2; ++i) {
    Json::Value s;
    Json::Value roi(Json::arrayValue);
    Json::Value p1; p1["x"] = 0; p1["y"] = 0;
    Json::Value p2; p2["x"] = 10; p2["y"] = 0;
    Json::Value p3; p3["x"] = 10; p3["y"] = 10;
    roi.append(p1); roi.append(p2); roi.append(p3);
    s["roi"] = roi;
    s["name"] = "Batch Stop " + std::to_string(i+1);
    arr.append(s);
  }

  req->setBody(arr.toStyledString());
  req->addHeader("Content-Type", "application/json");

  HttpResponsePtr response;
  bool callbackCalled = false;

  handler_->batchUpdateStops(req, [&](const HttpResponsePtr &resp) {
    callbackCalled = true;
    response = resp;
  });

  std::this_thread::sleep_for(std::chrono::milliseconds(200));

  ASSERT_TRUE(callbackCalled);
  ASSERT_NE(response, nullptr);
  EXPECT_EQ(response->statusCode(), k200OK);

  auto json = response->getJsonObject();
  ASSERT_NE(json, nullptr);
  EXPECT_TRUE(json->isMember("count"));
  EXPECT_EQ((*json)["count"].asInt(), 2);
}

// Test batch update rejects unsupported parameters
TEST_F(StopsHandlerTest, BatchUpdateRejectsUnsupportedParams) {
  if (instance_id_.empty()) {
    GTEST_SKIP() << "Test instance not created, skipping test";
  }

  auto req = HttpRequest::newHttpRequest();
  req->setPath("/v1/core/instance/" + instance_id_ + "/stops/batch");
  req->setMethod(Post);

  Json::Value arr(Json::arrayValue);

  Json::Value s;
  Json::Value roi(Json::arrayValue);
  Json::Value p1; p1["x"] = 0; p1["y"] = 0;
  Json::Value p2; p2["x"] = 10; p2["y"] = 0;
  Json::Value p3; p3["x"] = 10; p3["y"] = 10;
  roi.append(p1); roi.append(p2); roi.append(p3);
  s["roi"] = roi;
  Json::Value classes(Json::arrayValue); classes.append("Vehicle");
  s["classes"] = classes;
  arr.append(s);

  req->setBody(arr.toStyledString());
  req->addHeader("Content-Type", "application/json");

  HttpResponsePtr response;
  bool callbackCalled = false;

  handler_->batchUpdateStops(req, [&](const HttpResponsePtr &resp) {
    callbackCalled = true;
    response = resp;
  });

  std::this_thread::sleep_for(std::chrono::milliseconds(200));

  ASSERT_TRUE(callbackCalled);
  ASSERT_NE(response, nullptr);
  EXPECT_EQ(response->statusCode(), k400BadRequest);
}
// Test delete all stops
TEST_F(StopsHandlerTest, DeleteAllStops) {
  if (instance_id_.empty()) {
    GTEST_SKIP() << "Test instance not created, skipping test";
  }

  // Add a stop
  auto createReq = HttpRequest::newHttpRequest();
  createReq->setPath("/v1/core/instance/" + instance_id_ + "/stops");
  createReq->setMethod(Post);

  Json::Value body;
  Json::Value roi(Json::arrayValue);
  Json::Value p1; p1["x"] = 0; p1["y"] = 0;
  Json::Value p2; p2["x"] = 10; p2["y"] = 0;
  Json::Value p3; p3["x"] = 10; p3["y"] = 10;
  roi.append(p1); roi.append(p2); roi.append(p3);
  body["roi"] = roi;

  createReq->setBody(body.toStyledString());

  handler_->createStop(createReq, [&](const HttpResponsePtr &resp) {});
  std::this_thread::sleep_for(std::chrono::milliseconds(200));

  // Delete all
  auto req = HttpRequest::newHttpRequest();
  req->setPath("/v1/core/instance/" + instance_id_ + "/stops");
  req->setMethod(Delete);

  HttpResponsePtr response;
  bool callbackCalled = false;

  handler_->deleteAllStops(req, [&](const HttpResponsePtr &resp) {
    callbackCalled = true;
    response = resp;
  });

  std::this_thread::sleep_for(std::chrono::milliseconds(200));

  ASSERT_TRUE(callbackCalled);
  ASSERT_NE(response, nullptr);
  EXPECT_EQ(response->statusCode(), k200OK);

  // Confirm empty
  auto getReq = HttpRequest::newHttpRequest();
  getReq->setPath("/v1/core/instance/" + instance_id_ + "/stops");
  getReq->setMethod(Get);

  HttpResponsePtr getResp;
  bool getCalled = false;
  handler_->getAllStops(getReq, [&](const HttpResponsePtr &resp) {
    getCalled = true;
    getResp = resp;
  });

  std::this_thread::sleep_for(std::chrono::milliseconds(200));

  ASSERT_TRUE(getCalled);
  ASSERT_NE(getResp, nullptr);
  EXPECT_EQ(getResp->statusCode(), k200OK);
  auto json = getResp->getJsonObject();
  ASSERT_NE(json, nullptr);
  EXPECT_TRUE(json->isMember("stopZones"));
  EXPECT_EQ((*json)["stopZones"].size(), 0);
}
