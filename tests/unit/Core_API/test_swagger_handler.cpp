/**
 * ⚠️ CẢNH BÁO: KHÔNG ĐƯỢC THAY ĐỔI CODE Ở NỘI DUNG NÀY
 * 
 * File test này đã được xác nhận hoạt động đúng với API hiện tại.
 * Nếu cần thay đổi code trong file này, phải đảm bảo:
 * 1. Test vẫn pass sau khi thay đổi
 * 2. Test vẫn phù hợp với API handler hiện tại (SwaggerHandler)
 * 3. Tất cả các test case vẫn hoạt động như cũ
 * 
 * API được test: GET /swagger, GET /v1/swagger, GET /v2/swagger
 * Handler: SwaggerHandler::getSwaggerUI()
 * 
 * Các test case hiện tại:
 * - ValidateVersionFormat: Kiểm tra validation định dạng version
 * - ExtractVersionFromPathViaRequest: Kiểm tra extract version từ path
 * - SanitizePath: Kiểm tra sanitize path để tránh path traversal
 * - SwaggerUIEndpoint: Kiểm tra endpoint Swagger UI
 * - SwaggerUIWithVersion: Kiểm tra Swagger UI với version
 * - InvalidVersionFormat: Kiểm tra invalid version format
 */

#include "api/swagger_handler.h"
#include <chrono>
#include <drogon/HttpRequest.h>
#include <drogon/HttpResponse.h>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <thread>

using namespace drogon;

class SwaggerHandlerTest : public ::testing::Test {
protected:
  void SetUp() override {
    handler_ = std::make_unique<SwaggerHandler>();

    // Create a temporary openapi.yaml file for testing
    test_file_path_ =
        std::filesystem::temp_directory_path() / "test_openapi.yaml";
    std::ofstream test_file(test_file_path_);
    test_file << R"(openapi: 3.0.3
info:
  title: Test API
  version: 2025.0.1.1
paths:
  /v1/core/health:
    get:
      summary: Health check
  /v2/core/test:
    get:
      summary: Test endpoint
)";
    test_file.close();
  }

  void TearDown() override {
    handler_.reset();
    // Clean up test file
    if (std::filesystem::exists(test_file_path_)) {
      std::filesystem::remove(test_file_path_);
    }
  }

  std::unique_ptr<SwaggerHandler> handler_;
  std::filesystem::path test_file_path_;
};

// Test version format validation
TEST_F(SwaggerHandlerTest, ValidateVersionFormat) {
  // Valid versions
  EXPECT_TRUE(handler_->validateVersionFormat("v1"));
  EXPECT_TRUE(handler_->validateVersionFormat("v2"));
  EXPECT_TRUE(handler_->validateVersionFormat("v10"));
  EXPECT_TRUE(handler_->validateVersionFormat("v99"));

  // Invalid versions
  EXPECT_FALSE(handler_->validateVersionFormat(""));
  EXPECT_FALSE(handler_->validateVersionFormat("v"));
  EXPECT_FALSE(handler_->validateVersionFormat("1"));
  EXPECT_FALSE(handler_->validateVersionFormat("v1.0"));
  EXPECT_FALSE(handler_->validateVersionFormat("v-1"));
  EXPECT_FALSE(handler_->validateVersionFormat("v1a"));
  EXPECT_FALSE(handler_->validateVersionFormat("version1"));
  EXPECT_FALSE(handler_->validateVersionFormat("../v1"));
}

// Test extract version from path - test via actual requests
TEST_F(SwaggerHandlerTest, ExtractVersionFromPathViaRequest) {
  // Test v1/swagger
  auto req1 = HttpRequest::newHttpRequest();
  req1->setPath("/v1/swagger");
  req1->setMethod(Get);

  HttpResponsePtr response1;
  bool callback1 = false;
  handler_->getSwaggerUI(req1, [&](const HttpResponsePtr &resp) {
    callback1 = true;
    response1 = resp;
  });
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  ASSERT_TRUE(callback1);
  EXPECT_EQ(response1->statusCode(), k200OK);

  // Test v2/swagger
  auto req2 = HttpRequest::newHttpRequest();
  req2->setPath("/v2/swagger");
  req2->setMethod(Get);

  HttpResponsePtr response2;
  bool callback2 = false;
  handler_->getSwaggerUI(req2, [&](const HttpResponsePtr &resp) {
    callback2 = true;
    response2 = resp;
  });
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  ASSERT_TRUE(callback2);
  EXPECT_EQ(response2->statusCode(), k200OK);

  // Test /swagger (no version)
  auto req3 = HttpRequest::newHttpRequest();
  req3->setPath("/swagger");
  req3->setMethod(Get);

  HttpResponsePtr response3;
  bool callback3 = false;
  handler_->getSwaggerUI(req3, [&](const HttpResponsePtr &resp) {
    callback3 = true;
    response3 = resp;
  });
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  ASSERT_TRUE(callback3);
  EXPECT_EQ(response3->statusCode(), k200OK);
}

// Test sanitize path
TEST_F(SwaggerHandlerTest, SanitizePath) {
  EXPECT_EQ(handler_->sanitizePath("openapi.yaml"), "openapi.yaml");
  EXPECT_EQ(handler_->sanitizePath("test-file.yaml"), "test-file.yaml");
  EXPECT_EQ(handler_->sanitizePath("test_file.yaml"), "test_file.yaml");

  // Should reject path traversal
  EXPECT_EQ(handler_->sanitizePath("../openapi.yaml"), "");
  EXPECT_EQ(handler_->sanitizePath("../../etc/passwd"), "");
  EXPECT_EQ(handler_->sanitizePath("/etc/passwd"), "");
  EXPECT_EQ(handler_->sanitizePath("C:\\Windows\\System32"), "");

  // Should reject empty
  EXPECT_EQ(handler_->sanitizePath(""), "");

  // Should reject special characters
  EXPECT_EQ(handler_->sanitizePath("file;rm"), "");
}

// Test Swagger UI endpoint
TEST_F(SwaggerHandlerTest, SwaggerUIEndpoint) {
  auto req = HttpRequest::newHttpRequest();
  req->setPath("/swagger");
  req->setMethod(Get);

  HttpResponsePtr response;
  bool callbackCalled = false;

  handler_->getSwaggerUI(req, [&](const HttpResponsePtr &resp) {
    callbackCalled = true;
    response = resp;
  });

  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  ASSERT_TRUE(callbackCalled);
  ASSERT_NE(response, nullptr);
  EXPECT_EQ(response->statusCode(), k200OK);
  EXPECT_EQ(response->contentType(), CT_TEXT_HTML);

  std::string body = std::string(response->body());
  EXPECT_FALSE(body.empty());
  EXPECT_NE(body.find("swagger-ui"), std::string::npos);
}

// Test Swagger UI with version
TEST_F(SwaggerHandlerTest, SwaggerUIWithVersion) {
  auto req = HttpRequest::newHttpRequest();
  req->setPath("/v1/swagger");
  req->setMethod(Get);

  HttpResponsePtr response;
  bool callbackCalled = false;

  handler_->getSwaggerUI(req, [&](const HttpResponsePtr &resp) {
    callbackCalled = true;
    response = resp;
  });

  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  ASSERT_TRUE(callbackCalled);
  ASSERT_NE(response, nullptr);
  EXPECT_EQ(response->statusCode(), k200OK);

  std::string body = std::string(response->body());
  EXPECT_NE(body.find("/v1/openapi.yaml"), std::string::npos);
}

// Test invalid version format
TEST_F(SwaggerHandlerTest, InvalidVersionFormat) {
  // Create a request with invalid version (would need to manually set path)
  // Since we can't easily create invalid paths through HttpRequest,
  // we test the validation function directly
  EXPECT_FALSE(handler_->validateVersionFormat("v1.0"));
  EXPECT_FALSE(handler_->validateVersionFormat("../v1"));
}
