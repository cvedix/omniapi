/**
 * ⚠️ CẢNH BÁO: KHÔNG ĐƯỢC THAY ĐỔI CODE Ở NỘI DUNG NÀY
 * 
 * File test này đã được xác nhận hoạt động đúng với API hiện tại.
 * Nếu cần thay đổi code trong file này, phải đảm bảo:
 * 1. Test vẫn pass sau khi thay đổi
 * 2. Test vẫn phù hợp với API handler hiện tại (EndpointsHandler)
 * 3. Tất cả các test case vẫn hoạt động như cũ
 * 
 * API được test: GET /v1/core/endpoints
 * Handler: EndpointsHandler::getEndpointsStats()
 * 
 * Các test case hiện tại:
 * - EndpointsStatsReturnsValidJson: Kiểm tra response JSON hợp lệ với endpoints và total_endpoints
 */

#include "api/endpoints_handler.h"
#include "core/endpoint_monitor.h"
#include <chrono>
#include <drogon/HttpRequest.h>
#include <drogon/HttpResponse.h>
#include <gtest/gtest.h>
#include <json/json.h>
#include <thread>

using namespace drogon;

class EndpointsHandlerTest : public ::testing::Test {
protected:
  void SetUp() override { handler_ = std::make_unique<EndpointsHandler>(); }

  void TearDown() override { handler_.reset(); }

  std::unique_ptr<EndpointsHandler> handler_;
};

// Test endpoints stats endpoint returns valid JSON
TEST_F(EndpointsHandlerTest, EndpointsStatsReturnsValidJson) {
  auto req = HttpRequest::newHttpRequest();
  req->setPath("/v1/core/endpoints");
  req->setMethod(Get);

  HttpResponsePtr response;
  bool callbackCalled = false;

  handler_->getEndpointsStats(req, [&](const HttpResponsePtr &resp) {
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
  EXPECT_TRUE(json->isMember("endpoints"));
  EXPECT_TRUE(json->isMember("total_endpoints"));

  // Endpoints should be an array
  EXPECT_TRUE((*json)["endpoints"].isArray());
  EXPECT_GE((*json)["total_endpoints"].asInt64(), 0);
}
