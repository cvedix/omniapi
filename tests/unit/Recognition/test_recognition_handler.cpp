#include "api/recognition_handler.h"
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <drogon/HttpRequest.h>
#include <drogon/HttpResponse.h>
#include <gtest/gtest.h>
#include <json/json.h>
#include <mutex>
#include <thread>

using namespace drogon;

class RecognitionHandlerTest : public ::testing::Test {
protected:
  void SetUp() override { 
    // Disable Kafka in test environment to avoid connection errors
    setenv("DISABLE_KAFKA", "1", 1);
    handler_ = std::make_unique<RecognitionHandler>(); 
  }

  void TearDown() override { 
    handler_.reset();
    unsetenv("DISABLE_KAFKA");
  }

  std::unique_ptr<RecognitionHandler> handler_;
};

// Test renameSubject endpoint returns success with valid request
TEST_F(RecognitionHandlerTest, DISABLED_RenameSubjectSuccess) {
  bool callbackCalled = false;
  HttpResponsePtr response;

  auto req = HttpRequest::newHttpRequest();
  req->setPath("/v1/recognition/subjects/old_subject");
  req->setMethod(Put);
  req->addHeader("x-api-key", "test-api-key");
  req->addHeader("Content-Type", "application/json");

  // Set JSON body
  Json::Value body;
  body["subject"] = "new_subject";
  req->setBody(body.toStyledString());

  handler_->renameSubject(req, [&](const HttpResponsePtr &resp) {
    callbackCalled = true;
    response = resp;
  });

  // Wait for async callback
  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  ASSERT_TRUE(callbackCalled);
  ASSERT_NE(response, nullptr);
  EXPECT_EQ(response->statusCode(), k200OK);
  EXPECT_EQ(response->contentType(), CT_APPLICATION_JSON);

  // Parse and validate JSON
  auto json = response->getJsonObject();
  ASSERT_NE(json, nullptr);
  EXPECT_TRUE(json->isMember("updated"));
  EXPECT_EQ((*json)["updated"].asString(), "true");
}

// Test renameSubject endpoint with missing API key
TEST_F(RecognitionHandlerTest, DISABLED_RenameSubjectMissingApiKey) {
  bool callbackCalled = false;
  HttpResponsePtr response;

  auto req = HttpRequest::newHttpRequest();
  req->setPath("/v1/recognition/subjects/old_subject");
  req->setMethod(Put);
  req->addHeader("Content-Type", "application/json");

  // Set JSON body
  Json::Value body;
  body["subject"] = "new_subject";
  req->setBody(body.toStyledString());

  handler_->renameSubject(req, [&](const HttpResponsePtr &resp) {
    callbackCalled = true;
    response = resp;
  });

  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  ASSERT_TRUE(callbackCalled);
  EXPECT_EQ(response->statusCode(), k401Unauthorized);

  auto json = response->getJsonObject();
  ASSERT_NE(json, nullptr);
  EXPECT_TRUE(json->isMember("error"));
}

// Test renameSubject endpoint with missing subject in path
TEST_F(RecognitionHandlerTest, RenameSubjectMissingSubjectInPath) {
  bool callbackCalled = false;
  HttpResponsePtr response;
  std::mutex callbackMutex;
  std::condition_variable callbackCv;
  std::string callbackError;

  auto req = HttpRequest::newHttpRequest();
  req->setPath("/v1/recognition/subjects/");
  req->setMethod(Put);
  req->addHeader("x-api-key", "test-api-key");
  req->addHeader("Content-Type", "application/json");

  // Set JSON body
  Json::Value body;
  body["subject"] = "new_subject";
  req->setBody(body.toStyledString());

  handler_->renameSubject(req, [&](const HttpResponsePtr &resp) {
    try {
      std::lock_guard<std::mutex> lock(callbackMutex);
      callbackCalled = true;
      response = resp;
      callbackCv.notify_one();
    } catch (const std::exception &e) {
      std::lock_guard<std::mutex> lock(callbackMutex);
      callbackError = std::string("Exception in callback: ") + e.what();
      callbackCalled = true;
      callbackCv.notify_one();
    } catch (...) {
      std::lock_guard<std::mutex> lock(callbackMutex);
      callbackError = "Unknown exception in callback";
      callbackCalled = true;
      callbackCv.notify_one();
    }
  });

  // Wait for callback with timeout
  std::unique_lock<std::mutex> lock(callbackMutex);
  if (!callbackCv.wait_for(lock, std::chrono::milliseconds(500), [&] { return callbackCalled; })) {
    FAIL() << "Callback not called within timeout";
  }

  ASSERT_TRUE(callbackCalled);
  if (!callbackError.empty()) {
    FAIL() << callbackError;
  }
  ASSERT_NE(response, nullptr);
  EXPECT_EQ(response->statusCode(), k400BadRequest);

  auto json = response->getJsonObject();
  ASSERT_NE(json, nullptr);
  EXPECT_TRUE(json->isMember("error"));
}

// Test renameSubject endpoint with invalid JSON body
TEST_F(RecognitionHandlerTest, RenameSubjectInvalidJson) {
  bool callbackCalled = false;
  HttpResponsePtr response;
  std::mutex callbackMutex;
  std::condition_variable callbackCv;
  std::string callbackError;

  auto req = HttpRequest::newHttpRequest();
  req->setPath("/v1/recognition/subjects/old_subject");
  req->setMethod(Put);
  req->addHeader("x-api-key", "test-api-key");
  req->addHeader("Content-Type", "application/json");
  req->setBody("invalid json");

  handler_->renameSubject(req, [&](const HttpResponsePtr &resp) {
    try {
      std::lock_guard<std::mutex> lock(callbackMutex);
      callbackCalled = true;
      response = resp;
      callbackCv.notify_one();
    } catch (const std::exception &e) {
      std::lock_guard<std::mutex> lock(callbackMutex);
      callbackError = std::string("Exception in callback: ") + e.what();
      callbackCalled = true;
      callbackCv.notify_one();
    } catch (...) {
      std::lock_guard<std::mutex> lock(callbackMutex);
      callbackError = "Unknown exception in callback";
      callbackCalled = true;
      callbackCv.notify_one();
    }
  });

  // Wait for callback with timeout
  std::unique_lock<std::mutex> lock(callbackMutex);
  if (!callbackCv.wait_for(lock, std::chrono::milliseconds(500), [&] { return callbackCalled; })) {
    FAIL() << "Callback not called within timeout";
  }

  ASSERT_TRUE(callbackCalled);
  if (!callbackError.empty()) {
    FAIL() << callbackError;
  }
  ASSERT_NE(response, nullptr);
  EXPECT_EQ(response->statusCode(), k400BadRequest);

  auto json = response->getJsonObject();
  ASSERT_NE(json, nullptr);
  EXPECT_TRUE(json->isMember("error"));
}

// Test renameSubject endpoint with missing subject field in body
TEST_F(RecognitionHandlerTest, RenameSubjectMissingSubjectField) {
  bool callbackCalled = false;
  HttpResponsePtr response;

  auto req = HttpRequest::newHttpRequest();
  req->setPath("/v1/recognition/subjects/old_subject");
  req->setMethod(Put);
  req->addHeader("x-api-key", "test-api-key");
  req->addHeader("Content-Type", "application/json");

  // Set JSON body without subject field
  Json::Value body;
  body["other_field"] = "value";
  req->setBody(body.toStyledString());

  handler_->renameSubject(req, [&](const HttpResponsePtr &resp) {
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

// Test renameSubject endpoint with empty subject field in body
TEST_F(RecognitionHandlerTest, RenameSubjectEmptySubjectField) {
  bool callbackCalled = false;
  HttpResponsePtr response;
  std::mutex callbackMutex;
  std::condition_variable callbackCv;
  std::string callbackError;

  auto req = HttpRequest::newHttpRequest();
  req->setPath("/v1/recognition/subjects/old_subject");
  req->setMethod(Put);
  req->addHeader("x-api-key", "test-api-key");
  req->addHeader("Content-Type", "application/json");

  // Set JSON body with empty subject field
  Json::Value body;
  body["subject"] = "";
  req->setBody(body.toStyledString());

  handler_->renameSubject(req, [&](const HttpResponsePtr &resp) {
    try {
      std::lock_guard<std::mutex> lock(callbackMutex);
      callbackCalled = true;
      response = resp;
      callbackCv.notify_one();
    } catch (const std::exception &e) {
      std::lock_guard<std::mutex> lock(callbackMutex);
      callbackError = std::string("Exception in callback: ") + e.what();
      callbackCalled = true;
      callbackCv.notify_one();
    } catch (...) {
      std::lock_guard<std::mutex> lock(callbackMutex);
      callbackError = "Unknown exception in callback";
      callbackCalled = true;
      callbackCv.notify_one();
    }
  });

  // Wait for callback with timeout
  std::unique_lock<std::mutex> lock(callbackMutex);
  if (!callbackCv.wait_for(lock, std::chrono::milliseconds(500), [&] { return callbackCalled; })) {
    FAIL() << "Callback not called within timeout";
  }

  ASSERT_TRUE(callbackCalled);
  if (!callbackError.empty()) {
    FAIL() << callbackError;
  }
  ASSERT_NE(response, nullptr);
  EXPECT_EQ(response->statusCode(), k400BadRequest);

  auto json = response->getJsonObject();
  ASSERT_NE(json, nullptr);
  EXPECT_TRUE(json->isMember("error"));
}

// Test renameSubject endpoint with URL-encoded subject name
TEST_F(RecognitionHandlerTest, DISABLED_RenameSubjectUrlEncoded) {
  bool callbackCalled = false;
  HttpResponsePtr response;

  auto req = HttpRequest::newHttpRequest();
  req->setPath("/v1/recognition/subjects/old%20subject%20name");
  req->setMethod(Put);
  req->addHeader("x-api-key", "test-api-key");
  req->addHeader("Content-Type", "application/json");

  // Set JSON body
  Json::Value body;
  body["subject"] = "new subject name";
  req->setBody(body.toStyledString());

  handler_->renameSubject(req, [&](const HttpResponsePtr &resp) {
    callbackCalled = true;
    response = resp;
  });

  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  ASSERT_TRUE(callbackCalled);
  EXPECT_EQ(response->statusCode(), k200OK);

  auto json = response->getJsonObject();
  ASSERT_NE(json, nullptr);
  EXPECT_TRUE(json->isMember("updated"));
  EXPECT_EQ((*json)["updated"].asString(), "true");
}

// Test renameSubject endpoint CORS headers
TEST_F(RecognitionHandlerTest, RenameSubjectCorsHeaders) {
  bool callbackCalled = false;
  HttpResponsePtr response;
  std::mutex callbackMutex;
  std::condition_variable callbackCv;
  std::string callbackError;

  auto req = HttpRequest::newHttpRequest();
  req->setPath("/v1/recognition/subjects/old_subject");
  req->setMethod(Put);
  req->addHeader("x-api-key", "test-api-key");
  req->addHeader("Content-Type", "application/json");

  // Set JSON body
  Json::Value body;
  body["subject"] = "new_subject";
  req->setBody(body.toStyledString());

  handler_->renameSubject(req, [&](const HttpResponsePtr &resp) {
    try {
      std::lock_guard<std::mutex> lock(callbackMutex);
      callbackCalled = true;
      response = resp;
      callbackCv.notify_one();
    } catch (const std::exception &e) {
      std::lock_guard<std::mutex> lock(callbackMutex);
      callbackError = std::string("Exception in callback: ") + e.what();
      callbackCalled = true;
      callbackCv.notify_one();
    } catch (...) {
      std::lock_guard<std::mutex> lock(callbackMutex);
      callbackError = "Unknown exception in callback";
      callbackCalled = true;
      callbackCv.notify_one();
    }
  });

  // Wait for callback with timeout
  std::unique_lock<std::mutex> lock(callbackMutex);
  if (!callbackCv.wait_for(lock, std::chrono::milliseconds(500), [&] { return callbackCalled; })) {
    FAIL() << "Callback not called within timeout";
  }

  ASSERT_TRUE(callbackCalled);
  if (!callbackError.empty()) {
    FAIL() << callbackError;
  }
  ASSERT_NE(response, nullptr);
  EXPECT_TRUE(response->getHeader("Access-Control-Allow-Origin") == "*");
  EXPECT_TRUE(response->getHeader("Access-Control-Allow-Methods").find("PUT") !=
              std::string::npos);
  EXPECT_TRUE(
      response->getHeader("Access-Control-Allow-Headers").find("x-api-key") !=
      std::string::npos);
}

// Test deleteFaceSubject endpoint returns success with valid image_id
TEST_F(RecognitionHandlerTest, DeleteFaceSubjectSuccess) {
  // First, register a face subject to have something to delete
  bool registerCallbackCalled = false;
  auto registerReq = HttpRequest::newHttpRequest();
  registerReq->setPath("/v1/recognition/faces");
  registerReq->setMethod(Post);
  registerReq->addHeader("x-api-key", "test-api-key");
  registerReq->addHeader("Content-Type", "application/json");
  registerReq->setParameter("subject", "test_subject");

  Json::Value registerBody;
  registerBody["file"] = "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQ"
                         "VR42mNk+M9QDwADhgGAWjR9awAAAABJRU5ErkJggg==";
  registerReq->setBody(registerBody.toStyledString());

  std::string registeredImageId;
  handler_->registerFaceSubject(registerReq, [&](const HttpResponsePtr &resp) {
    registerCallbackCalled = true;
    if (resp->statusCode() == k200OK) {
      auto json = resp->getJsonObject();
      if (json && json->isMember("image_id")) {
        registeredImageId = (*json)["image_id"].asString();
      }
    }
  });

  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  if (registeredImageId.empty()) {
    GTEST_SKIP() << "Could not register face subject for deletion test";
    return;
  }

  // Now delete it
  bool callbackCalled = false;
  HttpResponsePtr response;

  auto req = HttpRequest::newHttpRequest();
  req->setPath("/v1/recognition/faces/" + registeredImageId);
  req->setMethod(Delete);
  req->addHeader("x-api-key", "test-api-key");

  handler_->deleteFaceSubject(req, [&](const HttpResponsePtr &resp) {
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
  EXPECT_TRUE(json->isMember("image_id"));
  EXPECT_TRUE(json->isMember("subject"));
  EXPECT_EQ((*json)["image_id"].asString(), registeredImageId);
}

// Test deleteFaceSubject endpoint with missing API key
TEST_F(RecognitionHandlerTest, DISABLED_DeleteFaceSubjectMissingApiKey) {
  bool callbackCalled = false;
  HttpResponsePtr response;

  auto req = HttpRequest::newHttpRequest();
  req->setPath("/v1/recognition/faces/test-image-id");
  req->setMethod(Delete);
  // No x-api-key header

  handler_->deleteFaceSubject(req, [&](const HttpResponsePtr &resp) {
    callbackCalled = true;
    response = resp;
  });

  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  ASSERT_TRUE(callbackCalled);
  EXPECT_EQ(response->statusCode(), k401Unauthorized);

  auto json = response->getJsonObject();
  ASSERT_NE(json, nullptr);
  EXPECT_TRUE(json->isMember("error"));
}

// Test deleteFaceSubject endpoint with non-existent image_id
TEST_F(RecognitionHandlerTest, DeleteFaceSubjectNotFound) {
  bool callbackCalled = false;
  HttpResponsePtr response;

  auto req = HttpRequest::newHttpRequest();
  req->setPath("/v1/recognition/faces/non-existent-id");
  req->setMethod(Delete);
  req->addHeader("x-api-key", "test-api-key");

  handler_->deleteFaceSubject(req, [&](const HttpResponsePtr &resp) {
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

// Test deleteFaceSubject endpoint with missing image_id in path
TEST_F(RecognitionHandlerTest, DeleteFaceSubjectMissingImageId) {
  bool callbackCalled = false;
  HttpResponsePtr response;

  auto req = HttpRequest::newHttpRequest();
  req->setPath("/v1/recognition/faces/");
  req->setMethod(Delete);
  req->addHeader("x-api-key", "test-api-key");

  handler_->deleteFaceSubject(req, [&](const HttpResponsePtr &resp) {
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

// Test deleteMultipleFaceSubjects endpoint returns success
TEST_F(RecognitionHandlerTest, DeleteMultipleFaceSubjectsSuccess) {
  // First, register face subjects to have something to delete
  bool registerCallbackCalled = false;
  auto registerReq = HttpRequest::newHttpRequest();
  registerReq->setPath("/v1/recognition/faces");
  registerReq->setMethod(Post);
  registerReq->addHeader("x-api-key", "test-api-key");
  registerReq->addHeader("Content-Type", "application/json");
  registerReq->setParameter("subject", "test_subject1");

  Json::Value registerBody;
  registerBody["file"] = "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQ"
                         "VR42mNk+M9QDwADhgGAWjR9awAAAABJRU5ErkJggg==";
  registerReq->setBody(registerBody.toStyledString());

  std::vector<std::string> registeredImageIds;
  handler_->registerFaceSubject(registerReq, [&](const HttpResponsePtr &resp) {
    registerCallbackCalled = true;
    if (resp->statusCode() == k200OK) {
      auto json = resp->getJsonObject();
      if (json && json->isMember("image_id")) {
        registeredImageIds.push_back((*json)["image_id"].asString());
      }
    }
  });

  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  if (registeredImageIds.empty()) {
    GTEST_SKIP() << "Could not register face subject for deletion test";
    return;
  }

  // Now delete them
  bool callbackCalled = false;
  HttpResponsePtr response;

  auto req = HttpRequest::newHttpRequest();
  req->setPath("/v1/recognition/faces/delete");
  req->setMethod(Post);
  req->addHeader("x-api-key", "test-api-key");
  req->addHeader("Content-Type", "application/json");

  Json::Value body(Json::arrayValue);
  for (const auto &id : registeredImageIds) {
    body.append(id);
  }
  req->setBody(body.toStyledString());

  handler_->deleteMultipleFaceSubjects(req, [&](const HttpResponsePtr &resp) {
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
  EXPECT_TRUE(json->isMember("deleted"));
  EXPECT_TRUE(json->get("deleted", Json::Value()).isArray());
}

// Test deleteMultipleFaceSubjects endpoint with missing API key
TEST_F(RecognitionHandlerTest,
       DISABLED_DeleteMultipleFaceSubjectsMissingApiKey) {
  bool callbackCalled = false;
  HttpResponsePtr response;

  auto req = HttpRequest::newHttpRequest();
  req->setPath("/v1/recognition/faces/delete");
  req->setMethod(Post);
  req->addHeader("Content-Type", "application/json");

  Json::Value body(Json::arrayValue);
  body.append("test-id-1");
  body.append("test-id-2");
  req->setBody(body.toStyledString());

  handler_->deleteMultipleFaceSubjects(req, [&](const HttpResponsePtr &resp) {
    callbackCalled = true;
    response = resp;
  });

  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  ASSERT_TRUE(callbackCalled);
  EXPECT_EQ(response->statusCode(), k401Unauthorized);

  auto json = response->getJsonObject();
  ASSERT_NE(json, nullptr);
  EXPECT_TRUE(json->isMember("error"));
}

// Test deleteMultipleFaceSubjects endpoint with invalid JSON (not array)
TEST_F(RecognitionHandlerTest, DeleteMultipleFaceSubjectsInvalidJson) {
  bool callbackCalled = false;
  HttpResponsePtr response;

  auto req = HttpRequest::newHttpRequest();
  req->setPath("/v1/recognition/faces/delete");
  req->setMethod(Post);
  req->addHeader("x-api-key", "test-api-key");
  req->addHeader("Content-Type", "application/json");

  Json::Value body;
  body["not_an_array"] = "value";
  req->setBody(body.toStyledString());

  handler_->deleteMultipleFaceSubjects(req, [&](const HttpResponsePtr &resp) {
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

// Test deleteMultipleFaceSubjects endpoint with non-existent IDs (should ignore
// them)
TEST_F(RecognitionHandlerTest, DeleteMultipleFaceSubjectsNonExistentIds) {
  bool callbackCalled = false;
  HttpResponsePtr response;

  auto req = HttpRequest::newHttpRequest();
  req->setPath("/v1/recognition/faces/delete");
  req->setMethod(Post);
  req->addHeader("x-api-key", "test-api-key");
  req->addHeader("Content-Type", "application/json");

  Json::Value body(Json::arrayValue);
  body.append("non-existent-id-1");
  body.append("non-existent-id-2");
  req->setBody(body.toStyledString());

  handler_->deleteMultipleFaceSubjects(req, [&](const HttpResponsePtr &resp) {
    callbackCalled = true;
    response = resp;
  });

  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  ASSERT_TRUE(callbackCalled);
  EXPECT_EQ(response->statusCode(), k200OK);

  auto json = response->getJsonObject();
  ASSERT_NE(json, nullptr);
  EXPECT_TRUE(json->isMember("deleted"));
  auto deleted = json->get("deleted", Json::Value());
  EXPECT_TRUE(deleted.isArray());
  EXPECT_EQ(deleted.size(), 0); // No faces were deleted
}
