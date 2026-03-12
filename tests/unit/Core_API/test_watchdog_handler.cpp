/**
 * ⚠️ CẢNH BÁO: KHÔNG ĐƯỢC THAY ĐỔI CODE Ở NỘI DUNG NÀY
 * 
 * File test này đã được xác nhận hoạt động đúng với API hiện tại.
 * Nếu cần thay đổi code trong file này, phải đảm bảo:
 * 1. Test vẫn pass sau khi thay đổi
 * 2. Test vẫn phù hợp với API handler hiện tại (WatchdogHandler)
 * 3. Tất cả các test case vẫn hoạt động như cũ
 * 
 * API được test: GET /v1/core/watchdog
 * Handler: WatchdogHandler::getWatchdogStatus()
 * 
 * Các test case hiện tại:
 * - WatchdogEndpointReturnsValidJson: Kiểm tra response JSON hợp lệ với cấu trúc watchdog và health_monitor
 */

#include "api/watchdog_handler.h"
#include "core/health_monitor.h"
#include "core/watchdog.h"
#include <chrono>
#include <drogon/HttpRequest.h>
#include <drogon/HttpResponse.h>
#include <gtest/gtest.h>
#include <json/json.h>
#include <memory>
#include <thread>

using namespace drogon;

class WatchdogHandlerTest : public ::testing::Test {
protected:
  void SetUp() override {
    handler_ = std::make_unique<WatchdogHandler>();

    // Create watchdog and health monitor for testing
    watchdog_ = std::make_unique<Watchdog>(5000, 30000);
    health_monitor_ = std::make_unique<HealthMonitor>(1000);

    // Register with handler (device_report not set in this test)
    WatchdogHandler::setWatchdog(watchdog_.get());
    WatchdogHandler::setHealthMonitor(health_monitor_.get());
    WatchdogHandler::setDeviceWatchdog(nullptr);
  }

  void TearDown() override {
    if (health_monitor_) {
      health_monitor_->stop();
    }
    if (watchdog_) {
      watchdog_->stop();
    }
    handler_.reset();
    watchdog_.reset();
    health_monitor_.reset();
  }

  std::unique_ptr<WatchdogHandler> handler_;
  std::unique_ptr<Watchdog> watchdog_;
  std::unique_ptr<HealthMonitor> health_monitor_;
};

// Test watchdog endpoint returns valid JSON
TEST_F(WatchdogHandlerTest, WatchdogEndpointReturnsValidJson) {
  auto req = HttpRequest::newHttpRequest();
  req->setPath("/v1/core/watchdog");
  req->setMethod(Get);

  HttpResponsePtr response;
  bool callbackCalled = false;

  handler_->getWatchdogStatus(req, [&](const HttpResponsePtr &resp) {
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

  // Check required fields - response has "watchdog", "health_monitor", "device_report"
  EXPECT_TRUE(json->isMember("watchdog"));
  EXPECT_TRUE(json->isMember("health_monitor"));
  EXPECT_TRUE(json->isMember("device_report"));

  // Check watchdog object structure
  if ((*json)["watchdog"].isObject() &&
      !(*json)["watchdog"].isMember("error")) {
    EXPECT_TRUE((*json)["watchdog"].isMember("running"));
    EXPECT_TRUE((*json)["watchdog"].isMember("total_heartbeats"));
    EXPECT_TRUE((*json)["watchdog"].isMember("missed_heartbeats"));
    EXPECT_TRUE((*json)["watchdog"].isMember("recovery_actions"));
    EXPECT_TRUE((*json)["watchdog"].isMember("is_healthy"));
  }

  // Check health_monitor object structure
  if ((*json)["health_monitor"].isObject() &&
      !(*json)["health_monitor"].isMember("error")) {
    EXPECT_TRUE((*json)["health_monitor"].isMember("running"));
    EXPECT_TRUE((*json)["health_monitor"].isMember("cpu_usage_percent"));
    EXPECT_TRUE((*json)["health_monitor"].isMember("memory_usage_mb"));
  }
}
