#include "api/ai_handler.h"
#include "core/ai_cache.h"
#include "core/priority_queue.h"
#include "core/rate_limiter.h"
#include "core/resource_manager.h"
#include <chrono>
#include <drogon/HttpRequest.h>
#include <drogon/HttpResponse.h>
#include <gtest/gtest.h>
#include <json/json.h>
#include <memory>
#include <thread>

using namespace drogon;

class AIHandlerTest : public ::testing::Test {
protected:
  void SetUp() override {
    handler_ = std::make_unique<AIHandler>();

    // Initialize dependencies
    auto queue = std::make_shared<PriorityQueue>();
    auto cache = std::make_shared<AICache>(100); // 100 MB cache
    auto rate_limiter = std::make_shared<RateLimiter>(
        100, std::chrono::seconds(60)); // 100 requests per 60 seconds
    auto &resource_manager = ResourceManager::getInstance(); // Singleton

    AIHandler::initialize(queue, cache, rate_limiter,
                          std::shared_ptr<ResourceManager>(
                              &resource_manager, [](ResourceManager *) {}),
                          10); // max 10 concurrent
  }

  void TearDown() override { handler_.reset(); }

  std::unique_ptr<AIHandler> handler_;
};

// Test GET /v1/core/ai/status returns valid JSON
TEST_F(AIHandlerTest, GetStatusReturnsValidJson) {
  auto req = HttpRequest::newHttpRequest();
  req->setPath("/v1/core/ai/status");
  req->setMethod(Get);

  HttpResponsePtr response;
  bool callbackCalled = false;

  handler_->getStatus(req, [&](const HttpResponsePtr &resp) {
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
}

// Test GET /v1/core/ai/metrics returns valid JSON
TEST_F(AIHandlerTest, GetMetricsReturnsValidJson) {
  auto req = HttpRequest::newHttpRequest();
  req->setPath("/v1/core/ai/metrics");
  req->setMethod(Get);

  HttpResponsePtr response;
  bool callbackCalled = false;

  handler_->getMetrics(req, [&](const HttpResponsePtr &resp) {
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
}

// Test POST /v1/core/ai/process with invalid JSON
TEST_F(AIHandlerTest, ProcessImageWithInvalidJson) {
  auto req = HttpRequest::newHttpRequest();
  req->setPath("/v1/core/ai/process");
  req->setMethod(Post);
  req->setBody("invalid json");

  HttpResponsePtr response;
  bool callbackCalled = false;

  handler_->processImage(req, [&](const HttpResponsePtr &resp) {
    callbackCalled = true;
    response = resp;
  });

  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  ASSERT_TRUE(callbackCalled);
  ASSERT_NE(response, nullptr);
  // Should return 400 for invalid JSON
  EXPECT_EQ(response->statusCode(), k400BadRequest);
}

// Test POST /v1/core/ai/process with missing required fields
TEST_F(AIHandlerTest, ProcessImageWithMissingFields) {
  auto req = HttpRequest::newHttpRequest();
  req->setPath("/v1/core/ai/process");
  req->setMethod(Post);

  Json::Value body;
  // Missing required fields like image_data
  req->setBody(body.toStyledString());

  HttpResponsePtr response;
  bool callbackCalled = false;

  handler_->processImage(req, [&](const HttpResponsePtr &resp) {
    callbackCalled = true;
    response = resp;
  });

  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  ASSERT_TRUE(callbackCalled);
  ASSERT_NE(response, nullptr);
  EXPECT_EQ(response->statusCode(), k400BadRequest);
}
