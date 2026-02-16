/**
 * ⚠️ CẢNH BÁO: KHÔNG ĐƯỢC THAY ĐỔI CODE Ở NỘI DUNG NÀY
 * 
 * File test này đã được xác nhận hoạt động đúng với API hiện tại.
 * Nếu cần thay đổi code trong file này, phải đảm bảo:
 * 1. Test vẫn pass sau khi thay đổi
 * 2. Test vẫn phù hợp với API handler hiện tại (SystemInfoHandler)
 * 3. Tất cả các test case vẫn hoạt động như cũ
 * 
 * API được test:
 * - GET /v1/core/system/info - SystemInfoHandler::getSystemInfo()
 * - GET /v1/core/system/status - SystemInfoHandler::getSystemStatus() (có test DISABLED)
 * - OPTIONS - SystemInfoHandler::handleOptions()
 * 
 * Các test case hiện tại:
 * - GetSystemInfoReturnsValidJson: Kiểm tra response JSON hợp lệ với cpu, ram, gpu, disk, mainboard, os
 * - DISABLED_GetSystemStatusReturnsValidJson: Test bị tắt (cần kiểm tra lại)
 * - SystemInfoHasValidCPUInfo: Kiểm tra thông tin CPU
 * - DISABLED_SystemStatusHasValidMemoryInfo: Test bị tắt (cần kiểm tra lại)
 * - HandleOptions: Kiểm tra OPTIONS request
 */

#include "api/system_info_handler.h"
#include <chrono>
#include <drogon/HttpRequest.h>
#include <drogon/HttpResponse.h>
#include <gtest/gtest.h>
#include <json/json.h>
#include <thread>

using namespace drogon;

class SystemInfoHandlerTest : public ::testing::Test {
protected:
  void SetUp() override { handler_ = std::make_unique<SystemInfoHandler>(); }

  void TearDown() override { handler_.reset(); }

  std::unique_ptr<SystemInfoHandler> handler_;
};

// Test GET /v1/core/system/info returns valid JSON
TEST_F(SystemInfoHandlerTest, GetSystemInfoReturnsValidJson) {
  auto req = HttpRequest::newHttpRequest();
  req->setPath("/v1/core/system/info");
  req->setMethod(Get);

  HttpResponsePtr response;
  bool callbackCalled = false;

  handler_->getSystemInfo(req, [&](const HttpResponsePtr &resp) {
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
  EXPECT_TRUE(json->isMember("cpu"));
  EXPECT_TRUE(json->isMember("ram"));
  EXPECT_TRUE(json->isMember("gpu"));
  EXPECT_TRUE(json->isMember("disk"));
  EXPECT_TRUE(json->isMember("mainboard"));
  EXPECT_TRUE(json->isMember("os"));

  // CPU should be an object
  EXPECT_TRUE((*json)["cpu"].isObject());

  // RAM should be an object
  EXPECT_TRUE((*json)["ram"].isObject());

  // OS should be an object
  EXPECT_TRUE((*json)["os"].isObject());
}

// Test GET /v1/core/system/status returns valid JSON
TEST_F(SystemInfoHandlerTest, DISABLED_GetSystemStatusReturnsValidJson) {
  auto req = HttpRequest::newHttpRequest();
  req->setPath("/v1/core/system/status");
  req->setMethod(Get);

  HttpResponsePtr response;
  bool callbackCalled = false;

  handler_->getSystemStatus(req, [&](const HttpResponsePtr &resp) {
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
  EXPECT_TRUE(json->isMember("cpu_usage"));
  EXPECT_TRUE(json->isMember("memory"));
  EXPECT_TRUE(json->isMember("uptime"));

  // CPU usage should be a number between 0 and 100
  EXPECT_TRUE((*json)["cpu_usage"].isNumeric());
  double cpuUsage = (*json)["cpu_usage"].asDouble();
  EXPECT_GE(cpuUsage, 0.0);
  EXPECT_LE(cpuUsage, 100.0);

  // Memory should be an object
  EXPECT_TRUE((*json)["memory"].isObject());

  // Uptime should be a number (seconds)
  EXPECT_TRUE((*json)["uptime"].isNumeric());
  EXPECT_GE((*json)["uptime"].asInt64(), 0);
}

// Test system info has valid CPU information
TEST_F(SystemInfoHandlerTest, SystemInfoHasValidCPUInfo) {
  auto req = HttpRequest::newHttpRequest();
  req->setPath("/v1/core/system/info");
  req->setMethod(Get);

  HttpResponsePtr response;
  bool callbackCalled = false;

  handler_->getSystemInfo(req, [&](const HttpResponsePtr &resp) {
    callbackCalled = true;
    response = resp;
  });

  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  ASSERT_TRUE(callbackCalled);
  auto json = response->getJsonObject();
  ASSERT_NE(json, nullptr);

  Json::Value cpu = (*json)["cpu"];
  EXPECT_TRUE(cpu.isObject());

  // CPU should have at least some basic info
  // (exact fields depend on system, but should be an object)
}

// Test system status has valid memory information
TEST_F(SystemInfoHandlerTest, DISABLED_SystemStatusHasValidMemoryInfo) {
  auto req = HttpRequest::newHttpRequest();
  req->setPath("/v1/core/system/status");
  req->setMethod(Get);

  HttpResponsePtr response;
  bool callbackCalled = false;

  handler_->getSystemStatus(req, [&](const HttpResponsePtr &resp) {
    callbackCalled = true;
    response = resp;
  });

  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  ASSERT_TRUE(callbackCalled);
  auto json = response->getJsonObject();
  ASSERT_NE(json, nullptr);

  Json::Value memory = (*json)["memory"];
  EXPECT_TRUE(memory.isObject());

  // Memory should have usage information
  if (memory.isMember("usage_percent")) {
    EXPECT_TRUE(memory["usage_percent"].isNumeric());
    double usage = memory["usage_percent"].asDouble();
    EXPECT_GE(usage, 0.0);
    EXPECT_LE(usage, 100.0);
  }
}

// Test OPTIONS request
TEST_F(SystemInfoHandlerTest, HandleOptions) {
  auto req = HttpRequest::newHttpRequest();
  req->setPath("/v1/core/system/info");
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
