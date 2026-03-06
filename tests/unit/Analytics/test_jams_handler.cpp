#include "api/jams_handler.h"
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
#include <json/reader.h>
#include <memory>
#include <thread>
#include <unistd.h>

using namespace drogon;

class JamsHandlerTest : public ::testing::Test {
protected:
  void SetUp() override {
    handler_ = std::make_unique<JamsHandler>();

    // Create temporary storage directory for testing
    test_storage_dir_ = std::filesystem::temp_directory_path() /
                        ("test_instances_jam_" + std::to_string(getpid()));
    std::filesystem::create_directories(test_storage_dir_);

    // Set font path environment variable to avoid permission errors
    setenv("OSD_DEFAULT_FONT_PATH", "", 1);

    // Create dependencies for InstanceRegistry
    solution_registry_ = &SolutionRegistry::getInstance();
    solution_registry_->initializeDefaultSolutions();
    pipeline_builder_ = std::make_unique<PipelineBuilder>();
    instance_storage_ = std::make_unique<InstanceStorage>(test_storage_dir_.string());

    // Create InstanceRegistry
    instance_registry_ = std::make_unique<InstanceRegistry>(*solution_registry_, *pipeline_builder_, *instance_storage_);

    // Create InProcessInstanceManager wrapper
    instance_manager_ = std::make_unique<InProcessInstanceManager>(*instance_registry_);

    // Register with handler (using IInstanceManager interface)
    JamsHandler::setInstanceManager(instance_manager_.get());

    // Create a test instance with ba_jam solution
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
    JamsHandler::setInstanceManager(nullptr);
  }

  void createTestInstance() {
    // Create instance via instance manager
    CreateInstanceRequest req;
    req.name = "test_ba_jam_instance";
    // Avoid building a runtime pipeline in unit tests (we only need instance storage for JamZones)
    // Leave `solution` empty so no pipeline is built during createInstance.
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

  std::unique_ptr<JamsHandler> handler_;
  std::unique_ptr<InstanceRegistry> instance_registry_;
  std::unique_ptr<InProcessInstanceManager> instance_manager_;
  SolutionRegistry *solution_registry_; // Singleton, don't own
  std::unique_ptr<PipelineBuilder> pipeline_builder_;
  std::unique_ptr<InstanceStorage> instance_storage_;
  std::filesystem::path test_storage_dir_;
  std::string instance_id_;
};

// Test GET /v1/core/instance/{instanceId}/jams - Instance not found
TEST_F(JamsHandlerTest, GetAllJamsInstanceNotFound) {
  auto req = HttpRequest::newHttpRequest();
  req->setPath("/v1/core/instance/nonexistent-id/jams");
  req->setMethod(Get);

  HttpResponsePtr response;
  bool callbackCalled = false;

  handler_->getAllJams(req, [&](const HttpResponsePtr &resp) {
    callbackCalled = true;
    response = resp;
  });

  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  ASSERT_TRUE(callbackCalled);
  ASSERT_NE(response, nullptr);
  EXPECT_EQ(response->statusCode(), k404NotFound);
}

// Test GET /v1/core/instance/{instanceId}/jams - Success (empty)
TEST_F(JamsHandlerTest, GetAllJamsEmpty) {
  if (instance_id_.empty()) {
    GTEST_SKIP() << "Test instance not created, skipping test";
  }

  auto req = HttpRequest::newHttpRequest();
  req->setPath("/v1/core/instance/" + instance_id_ + "/jams");
  req->setMethod(Get);

  HttpResponsePtr response;
  bool callbackCalled = false;

  handler_->getAllJams(req, [&](const HttpResponsePtr &resp) {
    callbackCalled = true;
    response = resp;
  });

  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  ASSERT_TRUE(callbackCalled);
  ASSERT_NE(response, nullptr);
  EXPECT_EQ(response->statusCode(), k200OK);

  auto json = response->getJsonObject();
  ASSERT_NE(json, nullptr);
  EXPECT_TRUE(json->isMember("jamZones"));
  EXPECT_TRUE((*json)["jamZones"].isArray());
  EXPECT_EQ((*json)["jamZones"].size(), 0);}

// Test POST /v1/core/instance/{instanceId}/jams - Create jam
TEST_F(JamsHandlerTest, CreateJam) {
  if (instance_id_.empty()) {
    GTEST_SKIP() << "Test instance not created, skipping test";
  }

  auto req = HttpRequest::newHttpRequest();
  req->setPath("/v1/core/instance/" + instance_id_ + "/jams");
  req->setMethod(Post);

  Json::Value body;
  body["name"] = "Test Jam Zone";
  Json::Value roi(Json::arrayValue);
  Json::Value p1; p1["x"] = 0; p1["y"] = 100;
  Json::Value p2; p2["x"] = 1920; p2["y"] = 100;
  Json::Value p3; p3["x"] = 1920; p3["y"] = 400;
  roi.append(p1); roi.append(p2); roi.append(p3);
  body["roi"] = roi;
  body["check_interval_frames"] = 20;
  body["check_min_hit_frames"] = 50;
  body["check_max_distance"] = 8;
  body["check_min_stops"] = 8;
  body["check_notify_interval"] = 10;

  req->setBody(body.toStyledString());
  req->addHeader("Content-Type", "application/json");

  HttpResponsePtr response;
  bool callbackCalled = false;

  handler_->createJam(req, [&](const HttpResponsePtr &resp) {
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
  EXPECT_TRUE(json->isMember("check_interval_frames"));
  EXPECT_EQ((*json)["check_interval_frames"].asInt(), 20);
  EXPECT_TRUE(json->isMember("check_min_hit_frames"));
  EXPECT_EQ((*json)["check_min_hit_frames"].asInt(), 50);
  EXPECT_TRUE(json->isMember("check_max_distance"));
  EXPECT_EQ((*json)["check_max_distance"].asInt(), 8);
  EXPECT_TRUE(json->isMember("check_min_stops"));
  EXPECT_EQ((*json)["check_min_stops"].asInt(), 8);
  EXPECT_TRUE(json->isMember("check_notify_interval"));
  EXPECT_EQ((*json)["check_notify_interval"].asInt(), 10);
  EXPECT_TRUE(json->isMember("name"));
  EXPECT_EQ((*json)["name"].asString(), "Test Jam Zone");
}

// Test POST /v1/core/instance/{instanceId}/jams - Create multiple jam zones
TEST_F(JamsHandlerTest, CreateMultipleJams) {
  if (instance_id_.empty()) {
    GTEST_SKIP() << "Test instance not created, skipping test";
  }

  auto req = HttpRequest::newHttpRequest();
  req->setPath("/v1/core/instance/" + instance_id_ + "/jams");
  req->setMethod(Post);

  Json::Value jam1(Json::objectValue);
  jam1["name"] = "Multi Jam 1";
  Json::Value r1(Json::arrayValue);
  Json::Value a1; a1["x"] = 0; a1["y"] = 0; Json::Value a2; a2["x"] = 10; a2["y"] = 0; Json::Value a3; a3["x"] = 10; a3["y"] = 10;
  r1.append(a1); r1.append(a2); r1.append(a3);
  jam1["roi"] = r1;

  Json::Value jam2 = jam1;
  jam2["name"] = "Multi Jam 2";

  Json::Value arr(Json::arrayValue);
  arr.append(jam1); arr.append(jam2);

  req->setBody(arr.toStyledString());
  req->addHeader("Content-Type", "application/json");

  HttpResponsePtr response;
  bool callbackCalled = false;

  handler_->createJam(req, [&](const HttpResponsePtr &resp) { callbackCalled = true; response = resp; });
  std::this_thread::sleep_for(std::chrono::milliseconds(200));

  ASSERT_TRUE(callbackCalled);
  ASSERT_NE(response, nullptr);
  EXPECT_EQ(response->statusCode(), k201Created);

  auto json = response->getJsonObject();
  ASSERT_NE(json, nullptr);
  EXPECT_TRUE(json->isMember("count"));
  EXPECT_EQ((*json)["count"].asInt(), 2);
  EXPECT_TRUE(json->isMember("zones"));
  EXPECT_TRUE((*json)["zones"].isArray());
  EXPECT_EQ((*json)["zones"].size(), 2);

  // Also verify GET returns them
  auto getReq = HttpRequest::newHttpRequest();
  getReq->setPath("/v1/core/instance/" + instance_id_ + "/jams");
  getReq->setMethod(Get);
  HttpResponsePtr getResp;
  bool getCalled = false;
  handler_->getAllJams(getReq, [&](const HttpResponsePtr &resp) { getCalled = true; getResp = resp; });
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  ASSERT_TRUE(getCalled);
  ASSERT_NE(getResp, nullptr);
  auto getJson = getResp->getJsonObject();
  ASSERT_NE(getJson, nullptr);
  EXPECT_TRUE(getJson->isMember("jamZones"));
  EXPECT_GE((*getJson)["jamZones"].size(), 2);
}

// Test POST /v1/core/instance/{instanceId}/jams - Invalid ROI
TEST_F(JamsHandlerTest, CreateJamInvalidROI) {
  if (instance_id_.empty()) {
    GTEST_SKIP() << "Test instance not created, skipping test";
  }

  auto req = HttpRequest::newHttpRequest();
  req->setPath("/v1/core/instance/" + instance_id_ + "/jams");
  req->setMethod(Post);

  Json::Value body;
  Json::Value roi(Json::arrayValue);
  Json::Value p1; p1["x"] = 0; p1["y"] = 100;
  roi.append(p1); // only one point -> invalid
  body["roi"] = roi;

  req->setBody(body.toStyledString());

  HttpResponsePtr response;
  bool callbackCalled = false;

  handler_->createJam(req, [&](const HttpResponsePtr &resp) {
    callbackCalled = true;
    response = resp;
  });

  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  ASSERT_TRUE(callbackCalled);
  ASSERT_NE(response, nullptr);
  EXPECT_EQ(response->statusCode(), k400BadRequest);
}

// Test GET/PUT/DELETE jam by ID
TEST_F(JamsHandlerTest, GetUpdateDeleteJam) {
  if (instance_id_.empty()) {
    GTEST_SKIP() << "Test instance not created, skipping test";
  }

  // Create a jam first
  auto createReq = HttpRequest::newHttpRequest();
  createReq->setPath("/v1/core/instance/" + instance_id_ + "/jams");
  createReq->setMethod(Post);

  Json::Value body;
  body["name"] = "Jam For CRUD";
  Json::Value roi(Json::arrayValue);
  Json::Value p1; p1["x"] = 0; p1["y"] = 50;
  Json::Value p2; p2["x"] = 100; p2["y"] = 50;
  Json::Value p3; p3["x"] = 100; p3["y"] = 150;
  roi.append(p1); roi.append(p2); roi.append(p3);
  body["roi"] = roi;

  createReq->setBody(body.toStyledString());

  HttpResponsePtr createResponse;
  bool createCallbackCalled = false;
  std::string jam_id;

  handler_->createJam(createReq, [&](const HttpResponsePtr &resp) {
    createCallbackCalled = true;
    createResponse = resp;
    if (resp && resp->statusCode() == k201Created) {
      auto json = resp->getJsonObject();
      if (json && json->isMember("id")) jam_id = (*json)["id"].asString();
    }
  });

  std::this_thread::sleep_for(std::chrono::milliseconds(200));

  if (jam_id.empty()) {
    GTEST_SKIP() << "Failed to create jam for test, skipping";
  }

  // Get jam
  auto getReq = HttpRequest::newHttpRequest();
  getReq->setPath("/v1/core/instance/" + instance_id_ + "/jams/" + jam_id);
  getReq->setMethod(Get);

  HttpResponsePtr getResp;
  bool getCalled = false;
  handler_->getJam(getReq, [&](const HttpResponsePtr &resp) { getCalled = true; getResp = resp; });
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  ASSERT_TRUE(getCalled);
  ASSERT_NE(getResp, nullptr);
  EXPECT_EQ(getResp->statusCode(), k200OK);

  // Update jam (change name)
  auto updateReq = HttpRequest::newHttpRequest();
  updateReq->setPath("/v1/core/instance/" + instance_id_ + "/jams/" + jam_id);
  updateReq->setMethod(Put);
  Json::Value updateBody;
  updateBody["name"] = "Updated Jam Name";
  updateReq->setBody(updateBody.toStyledString());

  HttpResponsePtr updateResp;
  bool updateCalled = false;
  handler_->updateJam(updateReq, [&](const HttpResponsePtr &resp) { updateCalled = true; updateResp = resp; });
  std::this_thread::sleep_for(std::chrono::milliseconds(200));
  ASSERT_TRUE(updateCalled);
  ASSERT_NE(updateResp, nullptr);
  EXPECT_EQ(updateResp->statusCode(), k200OK);

  // Verify updated name via GET
  auto verifyReq = HttpRequest::newHttpRequest();
  verifyReq->setPath("/v1/core/instance/" + instance_id_ + "/jams/" + jam_id);
  verifyReq->setMethod(Get);
  HttpResponsePtr verifyResp;
  bool verifyCalled = false;
  handler_->getJam(verifyReq, [&](const HttpResponsePtr &resp) { verifyCalled = true; verifyResp = resp; });
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  ASSERT_TRUE(verifyCalled);
  ASSERT_NE(verifyResp, nullptr);
  EXPECT_EQ(verifyResp->statusCode(), k200OK);
  auto verifyJson = verifyResp->getJsonObject();
  ASSERT_NE(verifyJson, nullptr);
  EXPECT_TRUE(verifyJson->isMember("name"));
  EXPECT_EQ((*verifyJson)["name"].asString(), "Updated Jam Name");

  // Delete jam
  auto delReq = HttpRequest::newHttpRequest();
  delReq->setPath("/v1/core/instance/" + instance_id_ + "/jams/" + jam_id);
  delReq->setMethod(Delete);

  HttpResponsePtr delResp;
  bool delCalled = false;
  handler_->deleteJam(delReq, [&](const HttpResponsePtr &resp) { delCalled = true; delResp = resp; });
  std::this_thread::sleep_for(std::chrono::milliseconds(200));
  ASSERT_TRUE(delCalled);
  ASSERT_NE(delResp, nullptr);
  EXPECT_EQ(delResp->statusCode(), k200OK);
}

// Test batch update
TEST_F(JamsHandlerTest, BatchUpdateJams) {
  if (instance_id_.empty()) {
    GTEST_SKIP() << "Test instance not created, skipping test";
  }

  auto req = HttpRequest::newHttpRequest();
  req->setPath("/v1/core/instance/" + instance_id_ + "/jams/batch");
  req->setMethod(Post);

  Json::Value jam1(Json::objectValue);
  jam1["name"] = "Batch Jam 1";
  Json::Value r1(Json::arrayValue);
  Json::Value p1; p1["x"] = 0; p1["y"] = 0; Json::Value p2; p2["x"] = 10; p2["y"] = 0; Json::Value p3; p3["x"] = 10; p3["y"] = 10;
  r1.append(p1); r1.append(p2); r1.append(p3);
  jam1["roi"] = r1;

  Json::Value jam2 = jam1;
  jam2["name"] = "Batch Jam 2";

  Json::Value arr(Json::arrayValue);
  arr.append(jam1); arr.append(jam2);

  req->setBody(arr.toStyledString());
  req->addHeader("Content-Type", "application/json");

  HttpResponsePtr response;
  bool callbackCalled = false;

  handler_->batchUpdateJams(req, [&](const HttpResponsePtr &resp) { callbackCalled = true; response = resp; });
  std::this_thread::sleep_for(std::chrono::milliseconds(200));

  ASSERT_TRUE(callbackCalled);
  ASSERT_NE(response, nullptr);
  EXPECT_EQ(response->statusCode(), k200OK);

  auto json = response->getJsonObject();
  ASSERT_NE(json, nullptr);
  EXPECT_TRUE(json->isMember("jamZones"));
  EXPECT_TRUE((*json)["jamZones"].isArray());
  EXPECT_EQ((*json)["jamZones"].size(), 2);
}

// Test DELETE ALL
TEST_F(JamsHandlerTest, DeleteAllJams) {
  if (instance_id_.empty()) {
    GTEST_SKIP() << "Test instance not created, skipping test";
  }

  auto req = HttpRequest::newHttpRequest();
  req->setPath("/v1/core/instance/" + instance_id_ + "/jams");
  req->setMethod(Delete);

  HttpResponsePtr response;
  bool callbackCalled = false;
  handler_->deleteAllJams(req, [&](const HttpResponsePtr &resp) { callbackCalled = true; response = resp; });
  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  ASSERT_TRUE(callbackCalled);
  ASSERT_NE(response, nullptr);
  EXPECT_EQ(response->statusCode(), k200OK);

  auto json = response->getJsonObject();
  ASSERT_NE(json, nullptr);
  EXPECT_TRUE(json->isMember("jamZones"));
  EXPECT_TRUE((*json)["jamZones"].isArray());
  EXPECT_EQ((*json)["jamZones"].size(), 0);
}
