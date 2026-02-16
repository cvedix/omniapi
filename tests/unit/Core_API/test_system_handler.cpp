/**
 * ⚠️ CẢNH BÁO: KHÔNG ĐƯỢC THAY ĐỔI CODE Ở NỘI DUNG NÀY
 * 
 * File test này đã được xác nhận hoạt động đúng với API hiện tại.
 * Nếu cần thay đổi code trong file này, phải đảm bảo:
 * 1. Test vẫn pass sau khi thay đổi
 * 2. Test vẫn phù hợp với API handler hiện tại (SystemHandler)
 * 3. Tất cả các test case vẫn hoạt động như cũ
 * 
 * API được test:
 * - GET /v1/core/system/config - SystemHandler::getSystemConfig()
 * - PUT /v1/core/system/config - SystemHandler::updateSystemConfig()
 * - GET /v1/core/system/preferences - SystemHandler::getPreferences()
 * - GET /v1/core/system/decoders - SystemHandler::getDecoders()
 * - GET /v1/core/system/registry - SystemHandler::getRegistry()
 * - POST /v1/core/system/shutdown - SystemHandler::shutdown()
 * - OPTIONS - SystemHandler::handleOptions()
 * 
 * Các test case hiện tại:
 * - GetSystemConfigReturnsValidJson: Kiểm tra response JSON hợp lệ
 * - GetSystemConfigHasValidStructure: Kiểm tra cấu trúc config
 * - UpdateSystemConfigWithValidJson: Kiểm tra update config
 * - UpdateSystemConfigWithInvalidJson: Kiểm tra invalid JSON
 * - UpdateSystemConfigWithEmptyBody: Kiểm tra empty body
 * - GetPreferencesReturnsValidJson: Kiểm tra preferences
 * - GetPreferencesHasExpectedKeys: Kiểm tra keys trong preferences
 * - GetDecodersReturnsValidJson: Kiểm tra decoders
 * - GetDecodersHasValidStructure: Kiểm tra cấu trúc decoders
 * - GetRegistryWithKeyReturnsValidJson: Kiểm tra registry với key
 * - GetRegistryWithoutKeyReturnsError: Kiểm tra registry không có key
 * - ShutdownReturnsValidJson: Kiểm tra shutdown endpoint
 * - OptionsRequestReturnsCorsHeaders: Kiểm tra CORS headers
 * - AllEndpointsHaveCorsHeaders: Kiểm tra tất cả endpoints có CORS headers
 */

#include "api/system_handler.h"
#include "core/decoder_detector.h"
#include "core/preferences_manager.h"
#include "core/system_config_manager.h"
#include <chrono>
#include <drogon/HttpRequest.h>
#include <drogon/HttpResponse.h>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <json/json.h>
#include <thread>

using namespace drogon;

class SystemHandlerTest : public ::testing::Test {
protected:
  void SetUp() override {
    handler_ = std::make_unique<SystemHandler>();

    // Initialize managers for testing
    auto &configManager = SystemConfigManager::getInstance();
    configManager.loadConfig();

    auto &prefsManager = PreferencesManager::getInstance();
    prefsManager.loadPreferences();

    auto &decoderDetector = DecoderDetector::getInstance();
    decoderDetector.detectDecoders();
  }

  void TearDown() override { handler_.reset(); }

  std::unique_ptr<SystemHandler> handler_;
};

// Test GET /v1/core/system/config returns valid JSON
TEST_F(SystemHandlerTest, GetSystemConfigReturnsValidJson) {
  bool callbackCalled = false;
  HttpResponsePtr response;

  auto req = HttpRequest::newHttpRequest();
  req->setPath("/v1/core/system/config");
  req->setMethod(Get);

  handler_->getSystemConfig(req, [&](const HttpResponsePtr &resp) {
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
  EXPECT_TRUE(json->isMember("systemConfig"));
  EXPECT_TRUE((*json)["systemConfig"].isArray());
}

// Test GET /v1/core/system/config returns valid structure
TEST_F(SystemHandlerTest, GetSystemConfigHasValidStructure) {
  auto req = HttpRequest::newHttpRequest();
  req->setPath("/v1/core/system/config");
  req->setMethod(Get);

  HttpResponsePtr response;
  bool callbackCalled = false;

  handler_->getSystemConfig(req, [&](const HttpResponsePtr &resp) {
    callbackCalled = true;
    response = resp;
  });

  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  ASSERT_TRUE(callbackCalled);
  auto json = response->getJsonObject();
  ASSERT_NE(json, nullptr);

  if ((*json)["systemConfig"].size() > 0) {
    auto &entity = (*json)["systemConfig"][0];
    EXPECT_TRUE(entity.isMember("fieldId"));
    EXPECT_TRUE(entity.isMember("displayName"));
    EXPECT_TRUE(entity.isMember("type"));
    EXPECT_TRUE(entity.isMember("value"));
    EXPECT_TRUE(entity.isMember("group"));
    EXPECT_TRUE(entity.isMember("availableValues"));
  }
}

// Test PUT /v1/core/system/config with valid JSON
TEST_F(SystemHandlerTest, UpdateSystemConfigWithValidJson) {
  // First get current config to find a valid fieldId
  auto getReq = HttpRequest::newHttpRequest();
  getReq->setPath("/v1/core/system/config");
  getReq->setMethod(Get);

  HttpResponsePtr getResponse;
  bool getCallbackCalled = false;

  handler_->getSystemConfig(getReq, [&](const HttpResponsePtr &resp) {
    getCallbackCalled = true;
    getResponse = resp;
  });

  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  ASSERT_TRUE(getCallbackCalled);
  auto getJson = getResponse->getJsonObject();
  ASSERT_NE(getJson, nullptr);

  // Skip test if no config entities
  if (!(*getJson)["systemConfig"].isArray() ||
      (*getJson)["systemConfig"].size() == 0) {
    GTEST_SKIP() << "No config entities available for update test";
  }

  std::string fieldId =
      (*getJson)["systemConfig"][0]["fieldId"].asString();
  std::string originalValue =
      (*getJson)["systemConfig"][0]["value"].asString();

  // Now test update
  auto req = HttpRequest::newHttpRequest();
  req->setPath("/v1/core/system/config");
  req->setMethod(Put);

  Json::Value body;
  Json::Value configArray(Json::arrayValue);
  Json::Value configItem;
  configItem["fieldId"] = fieldId;
  configItem["value"] = originalValue; // Use original value to avoid breaking
  configArray.append(configItem);
  body["systemConfig"] = configArray;

  req->setBody(body.toStyledString());
  req->setContentTypeCode(CT_APPLICATION_JSON);

  HttpResponsePtr response;
  bool callbackCalled = false;

  handler_->updateSystemConfig(req, [&](const HttpResponsePtr &resp) {
    callbackCalled = true;
    response = resp;
  });

  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  ASSERT_TRUE(callbackCalled);
  ASSERT_NE(response, nullptr);
  EXPECT_EQ(response->statusCode(), k200OK);

  auto json = response->getJsonObject();
  ASSERT_NE(json, nullptr);
  EXPECT_TRUE(json->isMember("status"));
  EXPECT_EQ((*json)["status"].asString(), "success");
}

// Test PUT /v1/core/system/config with invalid JSON (missing systemConfig)
TEST_F(SystemHandlerTest, UpdateSystemConfigWithInvalidJson) {
  auto req = HttpRequest::newHttpRequest();
  req->setPath("/v1/core/system/config");
  req->setMethod(Put);

  Json::Value body;
  body["invalid"] = "data";
  req->setBody(body.toStyledString());
  req->setContentTypeCode(CT_APPLICATION_JSON);

  HttpResponsePtr response;
  bool callbackCalled = false;

  handler_->updateSystemConfig(req, [&](const HttpResponsePtr &resp) {
    callbackCalled = true;
    response = resp;
  });

  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  ASSERT_TRUE(callbackCalled);
  ASSERT_NE(response, nullptr);
  // Should return 406 Not Acceptable or 400 Bad Request
  EXPECT_TRUE(response->statusCode() == k406NotAcceptable ||
              response->statusCode() == k400BadRequest);
}

// Test PUT /v1/core/system/config with empty body
TEST_F(SystemHandlerTest, UpdateSystemConfigWithEmptyBody) {
  auto req = HttpRequest::newHttpRequest();
  req->setPath("/v1/core/system/config");
  req->setMethod(Put);
  req->setBody("{}");
  req->setContentTypeCode(CT_APPLICATION_JSON);

  HttpResponsePtr response;
  bool callbackCalled = false;

  handler_->updateSystemConfig(req, [&](const HttpResponsePtr &resp) {
    callbackCalled = true;
    response = resp;
  });

  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  ASSERT_TRUE(callbackCalled);
  ASSERT_NE(response, nullptr);
  EXPECT_TRUE(response->statusCode() == k400BadRequest ||
              response->statusCode() == k406NotAcceptable);
}

// Test GET /v1/core/system/preferences returns valid JSON
TEST_F(SystemHandlerTest, GetPreferencesReturnsValidJson) {
  auto req = HttpRequest::newHttpRequest();
  req->setPath("/v1/core/system/preferences");
  req->setMethod(Get);

  HttpResponsePtr response;
  bool callbackCalled = false;

  handler_->getPreferences(req, [&](const HttpResponsePtr &resp) {
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
  EXPECT_TRUE(json->isObject());
}

// Test GET /v1/core/system/preferences has expected keys
TEST_F(SystemHandlerTest, GetPreferencesHasExpectedKeys) {
  auto req = HttpRequest::newHttpRequest();
  req->setPath("/v1/core/system/preferences");
  req->setMethod(Get);

  HttpResponsePtr response;
  bool callbackCalled = false;

  handler_->getPreferences(req, [&](const HttpResponsePtr &resp) {
    callbackCalled = true;
    response = resp;
  });

  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  ASSERT_TRUE(callbackCalled);
  auto json = response->getJsonObject();
  ASSERT_NE(json, nullptr);

  // Check for some expected preference keys
  EXPECT_TRUE(json->isMember("vms.show_area_crossing") ||
              json->isMember("global.default_performance_mode"));
}

// Test GET /v1/core/system/decoders returns valid JSON
TEST_F(SystemHandlerTest, GetDecodersReturnsValidJson) {
  auto req = HttpRequest::newHttpRequest();
  req->setPath("/v1/core/system/decoders");
  req->setMethod(Get);

  HttpResponsePtr response;
  bool callbackCalled = false;

  handler_->getDecoders(req, [&](const HttpResponsePtr &resp) {
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
  EXPECT_TRUE(json->isObject());
}

// Test GET /v1/core/system/decoders structure
TEST_F(SystemHandlerTest, GetDecodersHasValidStructure) {
  auto req = HttpRequest::newHttpRequest();
  req->setPath("/v1/core/system/decoders");
  req->setMethod(Get);

  HttpResponsePtr response;
  bool callbackCalled = false;

  handler_->getDecoders(req, [&](const HttpResponsePtr &resp) {
    callbackCalled = true;
    response = resp;
  });

  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  ASSERT_TRUE(callbackCalled);
  auto json = response->getJsonObject();
  ASSERT_NE(json, nullptr);

  // Decoders can be empty or have nvidia/intel keys
  // If nvidia exists, check structure
  if (json->isMember("nvidia")) {
    EXPECT_TRUE((*json)["nvidia"].isObject());
  }
  // If intel exists, check structure
  if (json->isMember("intel")) {
    EXPECT_TRUE((*json)["intel"].isObject());
  }
}

// Test GET /v1/core/system/registry with key parameter
TEST_F(SystemHandlerTest, GetRegistryWithKeyReturnsValidJson) {
  auto req = HttpRequest::newHttpRequest();
  req->setPath("/v1/core/system/registry");
  req->setMethod(Get);
  req->setParameter("key", "test");

  HttpResponsePtr response;
  bool callbackCalled = false;

  handler_->getRegistry(req, [&](const HttpResponsePtr &resp) {
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
  EXPECT_TRUE(json->isObject());
}

// Test GET /v1/core/system/registry without key parameter
TEST_F(SystemHandlerTest, GetRegistryWithoutKeyReturnsError) {
  auto req = HttpRequest::newHttpRequest();
  req->setPath("/v1/core/system/registry");
  req->setMethod(Get);
  // No key parameter

  HttpResponsePtr response;
  bool callbackCalled = false;

  handler_->getRegistry(req, [&](const HttpResponsePtr &resp) {
    callbackCalled = true;
    response = resp;
  });

  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  ASSERT_TRUE(callbackCalled);
  ASSERT_NE(response, nullptr);
  EXPECT_EQ(response->statusCode(), k400BadRequest);

  auto json = response->getJsonObject();
  ASSERT_NE(json, nullptr);
  EXPECT_TRUE(json->isMember("error"));
}

// Test POST /v1/core/system/shutdown (don't actually shutdown in test)
TEST_F(SystemHandlerTest, ShutdownReturnsValidJson) {
  auto req = HttpRequest::newHttpRequest();
  req->setPath("/v1/core/system/shutdown");
  req->setMethod(Post);

  HttpResponsePtr response;
  bool callbackCalled = false;

  handler_->shutdown(req, [&](const HttpResponsePtr &resp) {
    callbackCalled = true;
    response = resp;
  });

  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  ASSERT_TRUE(callbackCalled);
  ASSERT_NE(response, nullptr);
  EXPECT_EQ(response->statusCode(), k200OK);

  auto json = response->getJsonObject();
  ASSERT_NE(json, nullptr);
  EXPECT_TRUE(json->isMember("status"));
  EXPECT_EQ((*json)["status"].asString(), "success");
}

// Test OPTIONS request for CORS
TEST_F(SystemHandlerTest, OptionsRequestReturnsCorsHeaders) {
  auto req = HttpRequest::newHttpRequest();
  req->setPath("/v1/core/system/config");
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

  // Check CORS headers
  EXPECT_EQ(response->getHeader("Access-Control-Allow-Origin"), "*");
  EXPECT_EQ(response->getHeader("Access-Control-Allow-Methods"),
            "GET, PUT, POST, OPTIONS");
}

// Test all endpoints have CORS headers
TEST_F(SystemHandlerTest, AllEndpointsHaveCorsHeaders) {
  std::vector<std::pair<std::string, HttpMethod>> endpoints = {
      {"/v1/core/system/config", Get},
      {"/v1/core/system/preferences", Get},
      {"/v1/core/system/decoders", Get},
  };

  for (const auto &[path, method] : endpoints) {
    auto req = HttpRequest::newHttpRequest();
    req->setPath(path);
    req->setMethod(method);

    HttpResponsePtr response;
    bool callbackCalled = false;

    if (path == "/v1/core/system/config") {
      handler_->getSystemConfig(req, [&](const HttpResponsePtr &resp) {
        callbackCalled = true;
        response = resp;
      });
    } else if (path == "/v1/core/system/preferences") {
      handler_->getPreferences(req, [&](const HttpResponsePtr &resp) {
        callbackCalled = true;
        response = resp;
      });
    } else if (path == "/v1/core/system/decoders") {
      handler_->getDecoders(req, [&](const HttpResponsePtr &resp) {
        callbackCalled = true;
        response = resp;
      });
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    ASSERT_TRUE(callbackCalled) << "Callback not called for " << path;
    ASSERT_NE(response, nullptr) << "Response is null for " << path;
    EXPECT_EQ(response->getHeader("Access-Control-Allow-Origin"), "*")
        << "Missing CORS header for " << path;
  }
}

