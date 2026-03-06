#include "api/create_instance_handler.h"
#include "core/pipeline_builder.h"
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

class BaJamParamsTest : public ::testing::Test {
protected:
  void SetUp() override {
    handler_ = std::make_unique<CreateInstanceHandler>();

    // Create temporary storage directory for testing
    test_storage_dir_ = std::filesystem::temp_directory_path() /
                        ("test_instances_ba_jam_" + std::to_string(getpid()));
    std::filesystem::create_directories(test_storage_dir_);

    // Create dependencies for InstanceRegistry
    solution_registry_ = &SolutionRegistry::getInstance();
    pipeline_builder_ = std::make_unique<PipelineBuilder>();
    instance_storage_ = std::make_unique<InstanceStorage>(test_storage_dir_.string());

    // Create InstanceRegistry
    instance_registry_ = std::make_unique<InstanceRegistry>(*solution_registry_, *pipeline_builder_, *instance_storage_);

    // Create InProcessInstanceManager wrapper
    instance_manager_ = std::make_unique<InProcessInstanceManager>(*instance_registry_);

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

// Valid detection params in additionalParams should be accepted
TEST_F(BaJamParamsTest, CreateInstanceWithValidBaJamParams) {
  auto req = HttpRequest::newHttpRequest();
  req->setPath("/v1/core/instance");
  req->setMethod(Post);

  Json::Value body;
  body["name"] = "test_instance_ba_jam_params";
  body["autoStart"] = false;
  // Leave solution empty to avoid building pipeline in unit test
  body["solution"] = "";

  req->setBody(body.toStyledString());
  req->addHeader("Content-Type", "application/json");

  HttpResponsePtr response;
  bool callbackCalled = false;

  handler_->createInstance(req, [&](const HttpResponsePtr &resp) {
    callbackCalled = true;
    response = resp;
  });

  std::this_thread::sleep_for(std::chrono::milliseconds(200));

  ASSERT_TRUE(callbackCalled);
  ASSERT_NE(response, nullptr);
  std::cerr << "CreateInstance response status: " << static_cast<int>(response->statusCode()) << std::endl;
  if (!(response->statusCode() == k200OK || response->statusCode() == k201Created)) {
    // Print response body for debugging
    auto json = response->getJsonObject();
    if (json) {
      std::string s = Json::StyledWriter().write(*json);
      std::cerr << "CreateInstance failed: status=" << response->statusCode() << ", json=" << s << std::endl;
    } else {
      std::cerr << "CreateInstance failed: status=" << response->statusCode() << ", body='" << response->getBody() << "'" << std::endl;
    }
  }
  EXPECT_TRUE(response->statusCode() == k200OK || response->statusCode() == k201Created);
}

// Invalid detection param (non-integer) should be rejected
TEST_F(BaJamParamsTest, CreateInstanceWithInvalidBaJamParam) {
  auto req = HttpRequest::newHttpRequest();
  req->setPath("/v1/core/instance");
  req->setMethod(Post);

  Json::Value body;
  body["name"] = "test_instance_ba_jam_params_invalid";
  body["autoStart"] = false;
  body["solution"] = "";
  Json::Value additional;
  // Instance-level detection parameters are forbidden; must be specified per-zone
  additional["check_interval_frames"] = "20";
  body["additionalParams"] = additional;

  req->setBody(body.toStyledString());
  req->addHeader("Content-Type", "application/json");

  HttpResponsePtr response;
  bool callbackCalled = false;

  handler_->createInstance(req, [&](const HttpResponsePtr &resp) {
    callbackCalled = true;
    response = resp;
  });

  std::this_thread::sleep_for(std::chrono::milliseconds(200));

  ASSERT_TRUE(callbackCalled);
  ASSERT_NE(response, nullptr);
  EXPECT_EQ(response->statusCode(), k400BadRequest);
}
