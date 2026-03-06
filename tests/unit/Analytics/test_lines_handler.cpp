#include "api/lines_handler.h"
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

class LinesHandlerTest : public ::testing::Test {
protected:
  void SetUp() override {
    handler_ = std::make_unique<LinesHandler>();

    // Create temporary storage directory for testing
    test_storage_dir_ = std::filesystem::temp_directory_path() /
                        ("test_instances_" + std::to_string(getpid()));
    std::filesystem::create_directories(test_storage_dir_);

    // Set font path environment variable to avoid permission errors
    // Use empty string to skip font requirement in tests
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
    LinesHandler::setInstanceManager(instance_manager_.get());

    // Create a test instance with ba_crossline solution
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
    LinesHandler::setInstanceManager(nullptr);
  }

  void createTestInstance() {
    // Create instance via instance manager
    CreateInstanceRequest req;
    req.name = "test_ba_crossline_instance";
    req.solution = "ba_crossline";
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

  std::unique_ptr<LinesHandler> handler_;
  std::unique_ptr<InstanceRegistry> instance_registry_;
  std::unique_ptr<InProcessInstanceManager> instance_manager_;
  SolutionRegistry *solution_registry_; // Singleton, don't own
  std::unique_ptr<PipelineBuilder> pipeline_builder_;
  std::unique_ptr<InstanceStorage> instance_storage_;
  std::filesystem::path test_storage_dir_;
  std::string instance_id_;
};

// Test GET /v1/core/instance/{instanceId}/lines - Instance not found
TEST_F(LinesHandlerTest, GetAllLinesInstanceNotFound) {
  auto req = HttpRequest::newHttpRequest();
  req->setPath("/v1/core/instance/nonexistent-id/lines");
  req->setMethod(Get);

  HttpResponsePtr response;
  bool callbackCalled = false;

  handler_->getAllLines(req, [&](const HttpResponsePtr &resp) {
    callbackCalled = true;
    response = resp;
  });

  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  ASSERT_TRUE(callbackCalled);
  ASSERT_NE(response, nullptr);
  EXPECT_EQ(response->statusCode(), k404NotFound);
}

// Test GET /v1/core/instance/{instanceId}/lines - Success (empty)
TEST_F(LinesHandlerTest, GetAllLinesEmpty) {
  if (instance_id_.empty()) {
    GTEST_SKIP() << "Test instance not created, skipping test";
  }

  auto req = HttpRequest::newHttpRequest();
  req->setPath("/v1/core/instance/" + instance_id_ + "/lines");
  req->setMethod(Get);

  HttpResponsePtr response;
  bool callbackCalled = false;

  handler_->getAllLines(req, [&](const HttpResponsePtr &resp) {
    callbackCalled = true;
    response = resp;
  });

  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  ASSERT_TRUE(callbackCalled);
  ASSERT_NE(response, nullptr);
  EXPECT_EQ(response->statusCode(), k200OK);

  auto json = response->getJsonObject();
  ASSERT_NE(json, nullptr);
  EXPECT_TRUE(json->isMember("crossingLines"));
  EXPECT_TRUE((*json)["crossingLines"].isArray());
}

// Test POST /v1/core/instance/{instanceId}/lines - Create line
TEST_F(LinesHandlerTest, CreateLine) {
  if (instance_id_.empty()) {
    GTEST_SKIP() << "Test instance not created, skipping test";
  }

  auto req = HttpRequest::newHttpRequest();
  req->setPath("/v1/core/instance/" + instance_id_ + "/lines");
  req->setMethod(Post);

  Json::Value body;
  body["name"] = "Test Line";
  Json::Value coordinates(Json::arrayValue);
  Json::Value coord1;
  coord1["x"] = 0;
  coord1["y"] = 250;
  Json::Value coord2;
  coord2["x"] = 700;
  coord2["y"] = 220;
  coordinates.append(coord1);
  coordinates.append(coord2);
  body["coordinates"] = coordinates;
  body["direction"] = "Both";
  Json::Value classes(Json::arrayValue);
  classes.append("Vehicle");
  body["classes"] = classes;
  Json::Value color(Json::arrayValue);
  color.append(255);
  color.append(0);
  color.append(0);
  color.append(255);
  body["color"] = color;

  req->setBody(body.toStyledString());
  req->addHeader("Content-Type", "application/json");

  HttpResponsePtr response;
  bool callbackCalled = false;

  handler_->createLine(req, [&](const HttpResponsePtr &resp) {
    callbackCalled = true;
    response = resp;
  });

  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  ASSERT_TRUE(callbackCalled);
  ASSERT_NE(response, nullptr);
  if (response->statusCode() != k201Created) {
    std::cerr << "[TEST] CreateLine failed: Expected status 201, got "
              << response->statusCode() << std::endl;
    if (response->getJsonObject()) {
      std::cerr << "[TEST] Response body: "
                << response->getJsonObject()->toStyledString() << std::endl;
    }
  }
  EXPECT_EQ(response->statusCode(), k201Created);

  auto json = response->getJsonObject();
  ASSERT_NE(json, nullptr);
  EXPECT_TRUE(json->isMember("id"));
  EXPECT_TRUE(json->isMember("coordinates"));
  EXPECT_TRUE(json->isMember("direction"));
  EXPECT_EQ((*json)["direction"].asString(), "Both");
}

// Test POST /v1/core/instance/{instanceId}/lines - Invalid coordinates
TEST_F(LinesHandlerTest, CreateLineInvalidCoordinates) {
  if (instance_id_.empty()) {
    GTEST_SKIP() << "Test instance not created, skipping test";
  }

  auto req = HttpRequest::newHttpRequest();
  req->setPath("/v1/core/instance/" + instance_id_ + "/lines");
  req->setMethod(Post);

  Json::Value body;
  Json::Value coordinates(Json::arrayValue);
  // Only one coordinate (need at least 2)
  Json::Value coord1;
  coord1["x"] = 0;
  coord1["y"] = 250;
  coordinates.append(coord1);
  body["coordinates"] = coordinates;

  req->setBody(body.toStyledString());

  HttpResponsePtr response;
  bool callbackCalled = false;

  handler_->createLine(req, [&](const HttpResponsePtr &resp) {
    callbackCalled = true;
    response = resp;
  });

  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  ASSERT_TRUE(callbackCalled);
  ASSERT_NE(response, nullptr);
  EXPECT_EQ(response->statusCode(), k400BadRequest);
}

// Test GET /v1/core/instance/{instanceId}/lines/{lineId} - Get line by ID
TEST_F(LinesHandlerTest, GetLineById) {
  if (instance_id_.empty()) {
    GTEST_SKIP() << "Test instance not created, skipping test";
  }

  // First create a line
  auto createReq = HttpRequest::newHttpRequest();
  createReq->setPath("/v1/core/instance/" + instance_id_ + "/lines");
  createReq->setMethod(Post);

  Json::Value body;
  body["name"] = "Test Line for Get";
  Json::Value coordinates(Json::arrayValue);
  Json::Value coord1;
  coord1["x"] = 100;
  coord1["y"] = 300;
  Json::Value coord2;
  coord2["x"] = 600;
  coord2["y"] = 300;
  coordinates.append(coord1);
  coordinates.append(coord2);
  body["coordinates"] = coordinates;
  body["direction"] = "Up";
  Json::Value classes(Json::arrayValue);
  classes.append("Person");
  body["classes"] = classes;

  createReq->setBody(body.toStyledString());

  HttpResponsePtr createResponse;
  bool createCallbackCalled = false;
  std::string line_id;

  handler_->createLine(createReq, [&](const HttpResponsePtr &resp) {
    createCallbackCalled = true;
    createResponse = resp;
    if (resp && resp->statusCode() == k201Created) {
      auto json = resp->getJsonObject();
      if (json && json->isMember("id")) {
        line_id = (*json)["id"].asString();
      }
    }
  });

  std::this_thread::sleep_for(std::chrono::milliseconds(200));

  if (line_id.empty()) {
    GTEST_SKIP() << "Failed to create line for test, skipping";
  }

  // Now get the line by ID
  auto req = HttpRequest::newHttpRequest();
  req->setPath("/v1/core/instance/" + instance_id_ + "/lines/" + line_id);
  req->setMethod(Get);

  HttpResponsePtr response;
  bool callbackCalled = false;

  handler_->getLine(req, [&](const HttpResponsePtr &resp) {
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
  EXPECT_EQ((*json)["id"].asString(), line_id);
  EXPECT_TRUE(json->isMember("coordinates"));
  EXPECT_TRUE(json->isMember("direction"));
  EXPECT_EQ((*json)["direction"].asString(), "Up");
}

// Test GET /v1/core/instance/{instanceId}/lines/{lineId} - Line not found
TEST_F(LinesHandlerTest, GetLineByIdNotFound) {
  if (instance_id_.empty()) {
    GTEST_SKIP() << "Test instance not created, skipping test";
  }

  auto req = HttpRequest::newHttpRequest();
  req->setPath("/v1/core/instance/" + instance_id_ +
               "/lines/nonexistent-line-id");
  req->setMethod(Get);

  HttpResponsePtr response;
  bool callbackCalled = false;

  handler_->getLine(req, [&](const HttpResponsePtr &resp) {
    callbackCalled = true;
    response = resp;
  });

  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  ASSERT_TRUE(callbackCalled);
  ASSERT_NE(response, nullptr);
  EXPECT_EQ(response->statusCode(), k404NotFound);
}

// Test PUT /v1/core/instance/{instanceId}/lines/{lineId} - Update line
TEST_F(LinesHandlerTest, UpdateLine) {
  if (instance_id_.empty()) {
    GTEST_SKIP() << "Test instance not created, skipping test";
  }

  // First create a line
  auto createReq = HttpRequest::newHttpRequest();
  createReq->setPath("/v1/core/instance/" + instance_id_ + "/lines");
  createReq->setMethod(Post);

  Json::Value createBody;
  createBody["name"] = "Line to Update";
  Json::Value coordinates(Json::arrayValue);
  Json::Value coord1;
  coord1["x"] = 0;
  coord1["y"] = 250;
  Json::Value coord2;
  coord2["x"] = 700;
  coord2["y"] = 220;
  coordinates.append(coord1);
  coordinates.append(coord2);
  createBody["coordinates"] = coordinates;
  createBody["direction"] = "Both";
  Json::Value classes(Json::arrayValue);
  classes.append("Vehicle");
  createBody["classes"] = classes;

  createReq->setBody(createBody.toStyledString());

  HttpResponsePtr createResponse;
  bool createCallbackCalled = false;
  std::string line_id;

  handler_->createLine(createReq, [&](const HttpResponsePtr &resp) {
    createCallbackCalled = true;
    createResponse = resp;
    if (resp && resp->statusCode() == k201Created) {
      auto json = resp->getJsonObject();
      if (json && json->isMember("id")) {
        line_id = (*json)["id"].asString();
      }
    }
  });

  std::this_thread::sleep_for(std::chrono::milliseconds(200));

  if (line_id.empty()) {
    GTEST_SKIP() << "Failed to create line for test, skipping";
  }

  // Now update the line
  auto req = HttpRequest::newHttpRequest();
  req->setPath("/v1/core/instance/" + instance_id_ + "/lines/" + line_id);
  req->setMethod(Put);

  Json::Value body;
  body["name"] = "Updated Line";
  Json::Value updateCoordinates(Json::arrayValue);
  Json::Value updateCoord1;
  updateCoord1["x"] = 100;
  updateCoord1["y"] = 350;
  Json::Value updateCoord2;
  updateCoord2["x"] = 800;
  updateCoord2["y"] = 330;
  updateCoordinates.append(updateCoord1);
  updateCoordinates.append(updateCoord2);
  body["coordinates"] = updateCoordinates;
  body["direction"] = "Down";
  Json::Value updateClasses(Json::arrayValue);
  updateClasses.append("Person");
  updateClasses.append("Vehicle");
  body["classes"] = updateClasses;
  Json::Value color(Json::arrayValue);
  color.append(0);
  color.append(255);
  color.append(0);
  color.append(255);
  body["color"] = color;

  req->setBody(body.toStyledString());

  HttpResponsePtr response;
  bool callbackCalled = false;

  handler_->updateLine(req, [&](const HttpResponsePtr &resp) {
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
  EXPECT_EQ((*json)["id"].asString(), line_id);
  EXPECT_TRUE(json->isMember("coordinates"));
  EXPECT_TRUE(json->isMember("direction"));
  EXPECT_EQ((*json)["direction"].asString(), "Down");
  if (json->isMember("name")) {
    EXPECT_EQ((*json)["name"].asString(), "Updated Line");
  }
}

// Test PUT /v1/core/instance/{instanceId}/lines/{lineId} - Line not found
TEST_F(LinesHandlerTest, UpdateLineNotFound) {
  if (instance_id_.empty()) {
    GTEST_SKIP() << "Test instance not created, skipping test";
  }

  auto req = HttpRequest::newHttpRequest();
  req->setPath("/v1/core/instance/" + instance_id_ +
               "/lines/nonexistent-line-id");
  req->setMethod(Put);

  Json::Value body;
  Json::Value coordinates(Json::arrayValue);
  Json::Value coord1;
  coord1["x"] = 100;
  coord1["y"] = 300;
  Json::Value coord2;
  coord2["x"] = 600;
  coord2["y"] = 300;
  coordinates.append(coord1);
  coordinates.append(coord2);
  body["coordinates"] = coordinates;

  req->setBody(body.toStyledString());
  req->addHeader("Content-Type", "application/json");

  HttpResponsePtr response;
  bool callbackCalled = false;

  handler_->updateLine(req, [&](const HttpResponsePtr &resp) {
    callbackCalled = true;
    response = resp;
  });

  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  ASSERT_TRUE(callbackCalled);
  ASSERT_NE(response, nullptr);
  if (response->statusCode() != k404NotFound) {
    std::cerr << "[TEST] UpdateLineNotFound failed: Expected status 404, got "
              << response->statusCode() << std::endl;
    if (response->getJsonObject()) {
      std::cerr << "[TEST] Response body: "
                << response->getJsonObject()->toStyledString() << std::endl;
    }
  }
  EXPECT_EQ(response->statusCode(), k404NotFound);
}

// Test PUT /v1/core/instance/{instanceId}/lines/{lineId} - Invalid coordinates
TEST_F(LinesHandlerTest, UpdateLineInvalidCoordinates) {
  if (instance_id_.empty()) {
    GTEST_SKIP() << "Test instance not created, skipping test";
  }

  // First create a line
  auto createReq = HttpRequest::newHttpRequest();
  createReq->setPath("/v1/core/instance/" + instance_id_ + "/lines");
  createReq->setMethod(Post);

  Json::Value createBody;
  Json::Value coordinates(Json::arrayValue);
  Json::Value coord1;
  coord1["x"] = 0;
  coord1["y"] = 250;
  Json::Value coord2;
  coord2["x"] = 700;
  coord2["y"] = 220;
  coordinates.append(coord1);
  coordinates.append(coord2);
  createBody["coordinates"] = coordinates;

  createReq->setBody(createBody.toStyledString());

  HttpResponsePtr createResponse;
  bool createCallbackCalled = false;
  std::string line_id;

  handler_->createLine(createReq, [&](const HttpResponsePtr &resp) {
    createCallbackCalled = true;
    createResponse = resp;
    if (resp && resp->statusCode() == k201Created) {
      auto json = resp->getJsonObject();
      if (json && json->isMember("id")) {
        line_id = (*json)["id"].asString();
      }
    }
  });

  std::this_thread::sleep_for(std::chrono::milliseconds(200));

  if (line_id.empty()) {
    GTEST_SKIP() << "Failed to create line for test, skipping";
  }

  // Now try to update with invalid coordinates
  auto req = HttpRequest::newHttpRequest();
  req->setPath("/v1/core/instance/" + instance_id_ + "/lines/" + line_id);
  req->setMethod(Put);

  Json::Value body;
  Json::Value updateCoordinates(Json::arrayValue);
  // Only one coordinate (need at least 2)
  Json::Value updateCoord1;
  updateCoord1["x"] = 100;
  updateCoord1["y"] = 300;
  updateCoordinates.append(updateCoord1);
  body["coordinates"] = updateCoordinates;

  req->setBody(body.toStyledString());

  HttpResponsePtr response;
  bool callbackCalled = false;

  handler_->updateLine(req, [&](const HttpResponsePtr &resp) {
    callbackCalled = true;
    response = resp;
  });

  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  ASSERT_TRUE(callbackCalled);
  ASSERT_NE(response, nullptr);
  EXPECT_EQ(response->statusCode(), k400BadRequest);
}

// Test DELETE /v1/core/instance/{instanceId}/lines/{lineId} - Delete line
TEST_F(LinesHandlerTest, DeleteLineById) {
  if (instance_id_.empty()) {
    GTEST_SKIP() << "Test instance not created, skipping test";
  }

  // First create a line
  auto createReq = HttpRequest::newHttpRequest();
  createReq->setPath("/v1/core/instance/" + instance_id_ + "/lines");
  createReq->setMethod(Post);

  Json::Value body;
  Json::Value coordinates(Json::arrayValue);
  Json::Value coord1;
  coord1["x"] = 0;
  coord1["y"] = 250;
  Json::Value coord2;
  coord2["x"] = 700;
  coord2["y"] = 220;
  coordinates.append(coord1);
  coordinates.append(coord2);
  body["coordinates"] = coordinates;

  createReq->setBody(body.toStyledString());

  HttpResponsePtr createResponse;
  bool createCallbackCalled = false;
  std::string line_id;

  handler_->createLine(createReq, [&](const HttpResponsePtr &resp) {
    createCallbackCalled = true;
    createResponse = resp;
    if (resp && resp->statusCode() == k201Created) {
      auto json = resp->getJsonObject();
      if (json && json->isMember("id")) {
        line_id = (*json)["id"].asString();
      }
    }
  });

  std::this_thread::sleep_for(std::chrono::milliseconds(200));

  if (line_id.empty()) {
    GTEST_SKIP() << "Failed to create line for test, skipping";
  }

  // Now delete the line
  auto req = HttpRequest::newHttpRequest();
  req->setPath("/v1/core/instance/" + instance_id_ + "/lines/" + line_id);
  req->setMethod(Delete);

  HttpResponsePtr response;
  bool callbackCalled = false;

  handler_->deleteLine(req, [&](const HttpResponsePtr &resp) {
    callbackCalled = true;
    response = resp;
  });

  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  ASSERT_TRUE(callbackCalled);
  ASSERT_NE(response, nullptr);
  EXPECT_EQ(response->statusCode(), k200OK);

  auto json = response->getJsonObject();
  ASSERT_NE(json, nullptr);
  EXPECT_TRUE(json->isMember("message"));
}

// Test DELETE /v1/core/instance/{instanceId}/lines/{lineId} - Line not found
TEST_F(LinesHandlerTest, DeleteLineByIdNotFound) {
  if (instance_id_.empty()) {
    GTEST_SKIP() << "Test instance not created, skipping test";
  }

  auto req = HttpRequest::newHttpRequest();
  req->setPath("/v1/core/instance/" + instance_id_ +
               "/lines/nonexistent-line-id");
  req->setMethod(Delete);

  HttpResponsePtr response;
  bool callbackCalled = false;

  handler_->deleteLine(req, [&](const HttpResponsePtr &resp) {
    callbackCalled = true;
    response = resp;
  });

  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  ASSERT_TRUE(callbackCalled);
  ASSERT_NE(response, nullptr);
  EXPECT_EQ(response->statusCode(), k404NotFound);
}

// Test DELETE /v1/core/instance/{instanceId}/lines - Delete all lines
TEST_F(LinesHandlerTest, DeleteAllLines) {
  if (instance_id_.empty()) {
    GTEST_SKIP() << "Test instance not created, skipping test";
  }

  // First create a couple of lines
  for (int i = 0; i < 2; i++) {
    auto createReq = HttpRequest::newHttpRequest();
    createReq->setPath("/v1/core/instance/" + instance_id_ + "/lines");
    createReq->setMethod(Post);

    Json::Value body;
    Json::Value coordinates(Json::arrayValue);
    Json::Value coord1;
    coord1["x"] = 0 + i * 100;
    coord1["y"] = 250;
    Json::Value coord2;
    coord2["x"] = 700 + i * 100;
    coord2["y"] = 220;
    coordinates.append(coord1);
    coordinates.append(coord2);
    body["coordinates"] = coordinates;

    createReq->setBody(body.toStyledString());

    HttpResponsePtr createResponse;
    bool createCallbackCalled = false;

    handler_->createLine(createReq, [&](const HttpResponsePtr &resp) {
      createCallbackCalled = true;
      createResponse = resp;
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }

  // Now delete all lines
  auto req = HttpRequest::newHttpRequest();
  req->setPath("/v1/core/instance/" + instance_id_ + "/lines");
  req->setMethod(Delete);

  HttpResponsePtr response;
  bool callbackCalled = false;

  handler_->deleteAllLines(req, [&](const HttpResponsePtr &resp) {
    callbackCalled = true;
    response = resp;
  });

  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  ASSERT_TRUE(callbackCalled);
  ASSERT_NE(response, nullptr);
  EXPECT_EQ(response->statusCode(), k200OK);

  auto json = response->getJsonObject();
  ASSERT_NE(json, nullptr);
  EXPECT_TRUE(json->isMember("message"));
}

// Test OPTIONS request
TEST_F(LinesHandlerTest, HandleOptions) {
  auto req = HttpRequest::newHttpRequest();
  req->setPath("/v1/core/instance/test/lines");
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
