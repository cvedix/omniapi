/**
 * ⚠️ CẢNH BÁO: KHÔNG ĐƯỢC THAY ĐỔI CODE Ở NỘI DUNG NÀY
 * 
 * File test này đã được xác nhận hoạt động đúng với API hiện tại.
 * Nếu cần thay đổi code trong file này, phải đảm bảo:
 * 1. Test vẫn pass sau khi thay đổi
 * 2. Test vẫn phù hợp với API handler hiện tại (HealthHandler)
 * 3. Tất cả các test case vẫn hoạt động như cũ
 * 
 * API được test: GET /v1/core/health
 * Handler: HealthHandler::getHealth()
 * 
 * Các test case hiện tại:
 * - HealthEndpointReturnsValidJson: Kiểm tra response JSON hợp lệ
 * - HealthStatusValues: Kiểm tra các giá trị status hợp lệ
 * - HealthTimestampFormat: Kiểm tra định dạng timestamp
 */

#include "api/health_handler.h"
#include <chrono>
#include <drogon/HttpRequest.h>
#include <drogon/HttpResponse.h>
#include <gtest/gtest.h>
#include <json/json.h>
#include <thread>

using namespace drogon;

class HealthHandlerTest : public ::testing::Test {
protected:
  void SetUp() override { handler_ = std::make_unique<HealthHandler>(); }

  void TearDown() override { handler_.reset(); }

  std::unique_ptr<HealthHandler> handler_;
};

// Test health endpoint returns valid JSON
TEST_F(HealthHandlerTest, HealthEndpointReturnsValidJson) {
  bool callbackCalled = false;
  HttpResponsePtr response;

  auto req = HttpRequest::newHttpRequest();
  req->setPath("/v1/core/health");
  req->setMethod(Get);

  handler_->getHealth(req, [&](const HttpResponsePtr &resp) {
    callbackCalled = true;
    response = resp;
  });

  // Wait a bit for async callback
  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  ASSERT_TRUE(callbackCalled);
  ASSERT_NE(response, nullptr);
  EXPECT_EQ(response->statusCode(), k200OK);

  // Check content type
  EXPECT_EQ(response->contentType(), CT_APPLICATION_JSON);

  // Parse JSON response
  auto json = response->getJsonObject();
  ASSERT_NE(json, nullptr);

  // Check required fields
  EXPECT_TRUE(json->isMember("status"));
  EXPECT_TRUE(json->isMember("timestamp"));
  EXPECT_TRUE(json->isMember("uptime"));
  EXPECT_TRUE(json->isMember("service"));
  EXPECT_TRUE(json->isMember("version"));

  // Check status is string
  EXPECT_TRUE((*json)["status"].isString());

  // Check uptime is positive
  EXPECT_GE((*json)["uptime"].asInt64(), 0);

  // Check service name
  EXPECT_EQ((*json)["service"].asString(), "edge_ai_api");
}

// Test health endpoint status values
TEST_F(HealthHandlerTest, HealthStatusValues) {
  auto req = HttpRequest::newHttpRequest();
  req->setPath("/v1/core/health");
  req->setMethod(Get);

  HttpResponsePtr response;
  bool callbackCalled = false;

  handler_->getHealth(req, [&](const HttpResponsePtr &resp) {
    callbackCalled = true;
    response = resp;
  });

  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  ASSERT_TRUE(callbackCalled);
  auto json = response->getJsonObject();
  ASSERT_NE(json, nullptr);

  std::string status = (*json)["status"].asString();
  EXPECT_TRUE(status == "healthy" || status == "degraded" ||
              status == "unhealthy");
}

// Test health endpoint has valid timestamp
TEST_F(HealthHandlerTest, HealthTimestampFormat) {
  auto req = HttpRequest::newHttpRequest();
  req->setPath("/v1/core/health");
  req->setMethod(Get);

  HttpResponsePtr response;
  bool callbackCalled = false;

  handler_->getHealth(req, [&](const HttpResponsePtr &resp) {
    callbackCalled = true;
    response = resp;
  });

  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  ASSERT_TRUE(callbackCalled);
  auto json = response->getJsonObject();
  ASSERT_NE(json, nullptr);

  std::string timestamp = (*json)["timestamp"].asString();
  EXPECT_FALSE(timestamp.empty());
  // Should be ISO 8601 format (contains 'T' and 'Z')
  EXPECT_NE(timestamp.find('T'), std::string::npos);
}
