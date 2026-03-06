#include "api/solution_handler.h"
#include "models/solution_config.h"
#include "solutions/solution_registry.h"
#include "solutions/solution_storage.h"
#include <chrono>
#include <drogon/HttpRequest.h>
#include <drogon/HttpResponse.h>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <json/json.h>
#include <memory>
#include <thread>

using namespace drogon;

class SolutionHandlerTest : public ::testing::Test {
protected:
  void SetUp() override {
    handler_ = std::make_unique<SolutionHandler>();

    // Create temporary storage directory for testing
    test_storage_dir_ =
        std::filesystem::temp_directory_path() / "test_solutions";
    std::filesystem::create_directories(test_storage_dir_);

    // Get registry singleton instance and create storage instance
    registry_ = &SolutionRegistry::getInstance();
    storage_ = std::make_unique<SolutionStorage>(test_storage_dir_.string());

    // Register with handler
    SolutionHandler::setSolutionRegistry(registry_);
    SolutionHandler::setSolutionStorage(storage_.get());

    // Add a test solution to registry
    SolutionConfig testConfig;
    testConfig.solutionId = "test_solution";
    testConfig.solutionName = "Test Solution";
    testConfig.solutionType = "test";
    testConfig.isDefault = false;

    SolutionConfig::NodeConfig node;
    node.nodeType = "rtsp_src";
    node.nodeName = "test_node";
    node.parameters["url"] = "rtsp://test";
    testConfig.pipeline.push_back(node);

    registry_->registerSolution(testConfig);
  }

  void TearDown() override {
    handler_.reset();
    storage_.reset();

    // Clean up test storage directory
    if (std::filesystem::exists(test_storage_dir_)) {
      std::filesystem::remove_all(test_storage_dir_);
    }

    // Clear registry for next test
    SolutionHandler::setSolutionRegistry(nullptr);
    SolutionHandler::setSolutionStorage(nullptr);
  }

  std::unique_ptr<SolutionHandler> handler_;
  SolutionRegistry *registry_; // Singleton, don't own
  std::unique_ptr<SolutionStorage> storage_;
  std::filesystem::path test_storage_dir_;
};

// Test list solutions endpoint returns valid JSON
TEST_F(SolutionHandlerTest, ListSolutionsReturnsValidJson) {
  auto req = HttpRequest::newHttpRequest();
  req->setPath("/v1/core/solution");
  req->setMethod(Get);

  HttpResponsePtr response;
  bool callbackCalled = false;

  handler_->listSolutions(req, [&](const HttpResponsePtr &resp) {
    callbackCalled = true;
    response = resp;
  });

  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  ASSERT_TRUE(callbackCalled);
  ASSERT_NE(response, nullptr);
  EXPECT_EQ(response->statusCode(), k200OK);
  EXPECT_EQ(response->contentType(), CT_APPLICATION_JSON);

  auto json = response->getJsonObject();
  ASSERT_NE(json, nullptr);

  // Check required fields
  EXPECT_TRUE(json->isMember("solutions"));
  EXPECT_TRUE(json->isMember("total"));
  EXPECT_TRUE(json->isMember("default"));
  EXPECT_TRUE(json->isMember("custom"));

  // Check solutions is array
  EXPECT_TRUE((*json)["solutions"].isArray());

  // Check counts are non-negative
  EXPECT_GE((*json)["total"].asInt(), 0);
  EXPECT_GE((*json)["default"].asInt(), 0);
  EXPECT_GE((*json)["custom"].asInt(), 0);
}

// Test get solution endpoint - success case
TEST_F(SolutionHandlerTest, GetSolutionSuccess) {
  auto req = HttpRequest::newHttpRequest();
  req->setPath("/v1/core/solution/test_solution");
  req->setParameter("solutionId", "test_solution");
  req->setMethod(Get);

  HttpResponsePtr response;
  bool callbackCalled = false;

  handler_->getSolution(req, [&](const HttpResponsePtr &resp) {
    callbackCalled = true;
    response = resp;
  });

  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  if (!callbackCalled) {
    FAIL() << "Callback was not called";
  }
  if (response == nullptr) {
    FAIL() << "Response is nullptr";
  }
  if (response->statusCode() != k200OK) {
    FAIL() << "Expected status code 200, got " << response->statusCode();
  }
  ASSERT_TRUE(callbackCalled);
  ASSERT_NE(response, nullptr);
  EXPECT_EQ(response->statusCode(), k200OK);
  EXPECT_EQ(response->contentType(), CT_APPLICATION_JSON);

  auto json = response->getJsonObject();
  ASSERT_NE(json, nullptr);

  // Check required fields
  EXPECT_TRUE(json->isMember("solutionId"));
  EXPECT_TRUE(json->isMember("solutionName"));
  EXPECT_TRUE(json->isMember("solutionType"));
  EXPECT_TRUE(json->isMember("isDefault"));
  EXPECT_TRUE(json->isMember("pipeline"));

  EXPECT_EQ((*json)["solutionId"].asString(), "test_solution");
  EXPECT_EQ((*json)["solutionName"].asString(), "Test Solution");
}

// Test get solution endpoint - not found case
TEST_F(SolutionHandlerTest, GetSolutionNotFound) {
  auto req = HttpRequest::newHttpRequest();
  req->setPath("/v1/core/solution/nonexistent");
  req->setParameter("solutionId", "nonexistent");
  req->setMethod(Get);

  HttpResponsePtr response;
  bool callbackCalled = false;

  handler_->getSolution(req, [&](const HttpResponsePtr &resp) {
    callbackCalled = true;
    response = resp;
  });

  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  if (!callbackCalled) {
    FAIL() << "Callback was not called";
  }
  if (response == nullptr) {
    FAIL() << "Response is nullptr";
  }
  if (response->statusCode() != k404NotFound) {
    FAIL() << "Expected status code 404, got " << response->statusCode();
  }
  ASSERT_TRUE(callbackCalled);
  ASSERT_NE(response, nullptr);
  EXPECT_EQ(response->statusCode(), k404NotFound);

  auto json = response->getJsonObject();
  ASSERT_NE(json, nullptr);
  EXPECT_TRUE(json->isMember("error"));
}

// Test create solution endpoint - success case
TEST_F(SolutionHandlerTest, DISABLED_CreateSolutionSuccess) {
  auto req = HttpRequest::newHttpRequest();
  req->setPath("/v1/core/solution");
  req->setMethod(Post);

  // Create JSON body
  Json::Value body;
  body["solutionId"] = "new_solution";
  body["solutionName"] = "New Solution";
  body["solutionType"] = "test";

  Json::Value pipeline(Json::arrayValue);
  Json::Value node(Json::objectValue);
  node["nodeType"] = "rtsp_src";
  node["nodeName"] = "source";
  Json::Value params(Json::objectValue);
  params["url"] = "rtsp://test";
  node["parameters"] = params;
  pipeline.append(node);
  body["pipeline"] = pipeline;

  req->setBody(body.toStyledString());

  HttpResponsePtr response;
  bool callbackCalled = false;

  handler_->createSolution(req, [&](const HttpResponsePtr &resp) {
    callbackCalled = true;
    response = resp;
  });

  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  ASSERT_TRUE(callbackCalled);
  ASSERT_NE(response, nullptr);
  EXPECT_EQ(response->statusCode(), k201Created);

  auto json = response->getJsonObject();
  ASSERT_NE(json, nullptr);
  EXPECT_TRUE(json->isMember("solutionId"));
  EXPECT_EQ((*json)["solutionId"].asString(), "new_solution");
}

// Test create solution endpoint - invalid JSON
TEST_F(SolutionHandlerTest, CreateSolutionInvalidJson) {
  auto req = HttpRequest::newHttpRequest();
  req->setPath("/v1/core/solution");
  req->setMethod(Post);
  req->setBody("invalid json");

  HttpResponsePtr response;
  bool callbackCalled = false;

  handler_->createSolution(req, [&](const HttpResponsePtr &resp) {
    callbackCalled = true;
    response = resp;
  });

  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  ASSERT_TRUE(callbackCalled);
  EXPECT_EQ(response->statusCode(), k400BadRequest);

  auto json = response->getJsonObject();
  ASSERT_NE(json, nullptr);
  EXPECT_TRUE(json->isMember("error"));
}

// Test create solution endpoint - missing required field
TEST_F(SolutionHandlerTest, CreateSolutionMissingRequiredField) {
  auto req = HttpRequest::newHttpRequest();
  req->setPath("/v1/core/solution");
  req->setMethod(Post);

  // Create JSON body without solutionId
  Json::Value body;
  body["solutionName"] = "New Solution";
  body["solutionType"] = "test";

  req->setBody(body.toStyledString());

  HttpResponsePtr response;
  bool callbackCalled = false;

  handler_->createSolution(req, [&](const HttpResponsePtr &resp) {
    callbackCalled = true;
    response = resp;
  });

  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  ASSERT_TRUE(callbackCalled);
  EXPECT_EQ(response->statusCode(), k400BadRequest);

  auto json = response->getJsonObject();
  ASSERT_NE(json, nullptr);
  EXPECT_TRUE(json->isMember("error"));
}

// Test update solution endpoint - success case
TEST_F(SolutionHandlerTest, DISABLED_UpdateSolutionSuccess) {
  auto req = HttpRequest::newHttpRequest();
  req->setPath("/v1/core/solution/test_solution");
  req->setMethod(Put);

  // Create JSON body
  Json::Value body;
  body["solutionName"] = "Updated Solution Name";
  body["solutionType"] = "test";

  Json::Value pipeline(Json::arrayValue);
  Json::Value node(Json::objectValue);
  node["nodeType"] = "rtsp_src";
  node["nodeName"] = "source";
  Json::Value params(Json::objectValue);
  params["url"] = "rtsp://updated";
  node["parameters"] = params;
  pipeline.append(node);
  body["pipeline"] = pipeline;

  req->setBody(body.toStyledString());

  HttpResponsePtr response;
  bool callbackCalled = false;

  handler_->updateSolution(req, [&](const HttpResponsePtr &resp) {
    callbackCalled = true;
    response = resp;
  });

  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  ASSERT_TRUE(callbackCalled);
  ASSERT_NE(response, nullptr);
  EXPECT_EQ(response->statusCode(), k200OK);

  auto json = response->getJsonObject();
  ASSERT_NE(json, nullptr);
  EXPECT_TRUE(json->isMember("solutionId"));
  EXPECT_EQ((*json)["solutionName"].asString(), "Updated Solution Name");
}

// Test delete solution endpoint - success case
TEST_F(SolutionHandlerTest, DeleteSolutionSuccess) {
  // First create a custom solution
  SolutionConfig customConfig;
  customConfig.solutionId = "custom_solution";
  customConfig.solutionName = "Custom Solution";
  customConfig.solutionType = "test";
  customConfig.isDefault = false;
  registry_->registerSolution(customConfig);

  auto req = HttpRequest::newHttpRequest();
  req->setPath("/v1/core/solution/custom_solution");
  req->setParameter("solutionId", "custom_solution");
  req->setMethod(Delete);

  HttpResponsePtr response;
  bool callbackCalled = false;

  handler_->deleteSolution(req, [&](const HttpResponsePtr &resp) {
    callbackCalled = true;
    response = resp;
  });

  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  if (!callbackCalled) {
    FAIL() << "Callback was not called";
  }
  if (response == nullptr) {
    FAIL() << "Response is nullptr";
  }
  if (response->statusCode() != k200OK) {
    FAIL() << "Expected status code 200, got " << response->statusCode();
  }
  ASSERT_TRUE(callbackCalled);
  ASSERT_NE(response, nullptr);
  EXPECT_EQ(response->statusCode(), k200OK);

  auto json = response->getJsonObject();
  ASSERT_NE(json, nullptr);
  EXPECT_TRUE(json->isMember("message"));
}

// Test delete solution endpoint - not found
TEST_F(SolutionHandlerTest, DeleteSolutionNotFound) {
  auto req = HttpRequest::newHttpRequest();
  req->setPath("/v1/core/solution/nonexistent");
  req->setParameter("solutionId", "nonexistent");
  req->setMethod(Delete);

  HttpResponsePtr response;
  bool callbackCalled = false;

  handler_->deleteSolution(req, [&](const HttpResponsePtr &resp) {
    callbackCalled = true;
    response = resp;
  });

  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  ASSERT_TRUE(callbackCalled);
  EXPECT_EQ(response->statusCode(), k404NotFound);

  auto json = response->getJsonObject();
  ASSERT_NE(json, nullptr);
  EXPECT_TRUE(json->isMember("error"));
}

// Test list solutions when registry not initialized
TEST_F(SolutionHandlerTest, ListSolutionsRegistryNotInitialized) {
  SolutionHandler::setSolutionRegistry(nullptr);

  auto req = HttpRequest::newHttpRequest();
  req->setPath("/v1/core/solution");
  req->setMethod(Get);

  HttpResponsePtr response;
  bool callbackCalled = false;

  handler_->listSolutions(req, [&](const HttpResponsePtr &resp) {
    callbackCalled = true;
    response = resp;
  });

  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  ASSERT_TRUE(callbackCalled);
  EXPECT_EQ(response->statusCode(), k500InternalServerError);

  // Restore registry for other tests
  SolutionHandler::setSolutionRegistry(registry_);
}
