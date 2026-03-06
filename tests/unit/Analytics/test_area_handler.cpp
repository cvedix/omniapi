#include "api/area_handler.h"
#include "core/area_manager.h"
#include "core/area_storage.h"
#include "core/logging_flags.h"
#include "core/pipeline_builder.h"
#include "core/securt_instance.h"
#include "core/securt_instance_manager.h"
#include "core/securt_instance_registry.h"
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

// Define logging flags for tests
// Logging flags are defined in test_main.cpp to avoid multiple definition

class AreaHandlerTest : public ::testing::Test {
protected:
  void SetUp() override {
    handler_ = std::make_unique<AreaHandler>();

    // Create temporary storage directory for testing
    test_storage_dir_ = std::filesystem::temp_directory_path() /
                        ("test_areas_" + std::to_string(getpid()));
    std::filesystem::create_directories(test_storage_dir_);

    // Create dependencies for InstanceRegistry
    solution_registry_ = &SolutionRegistry::getInstance();
    solution_registry_->initializeDefaultSolutions();
    pipeline_builder_ = std::make_unique<PipelineBuilder>();
    instance_storage_ =
        std::make_unique<InstanceStorage>(test_storage_dir_.string());

    // Create InstanceRegistry
    instance_registry_ = std::make_unique<InstanceRegistry>(
        *solution_registry_, *pipeline_builder_, *instance_storage_);

    // Create InProcessInstanceManager wrapper
    core_instance_manager_ =
        std::make_unique<InProcessInstanceManager>(*instance_registry_);

    // Create SecuRTInstanceManager
    securt_instance_manager_ = std::make_unique<SecuRTInstanceManager>(
        core_instance_manager_.get());

    // Create AreaStorage
    area_storage_ = std::make_unique<AreaStorage>();

    // Create AreaManager
    area_manager_ = std::make_unique<AreaManager>(area_storage_.get(),
                                                   securt_instance_manager_.get());

    // Register with handler
    AreaHandler::setAreaManager(area_manager_.get());

    // Create a test instance
    createTestInstance();
  }

  void TearDown() override {
    handler_.reset();
    area_manager_.reset();
    area_storage_.reset();
    securt_instance_manager_.reset();
    core_instance_manager_.reset();
    instance_registry_.reset();
    instance_storage_.reset();
    pipeline_builder_.reset();

    // Clean up test storage directory
    if (std::filesystem::exists(test_storage_dir_)) {
      std::filesystem::remove_all(test_storage_dir_);
    }

    // Clear handler dependencies
    AreaHandler::setAreaManager(nullptr);
  }

  void createTestInstance() {
    // Create instance via SecuRT manager
    SecuRTInstanceWrite write;
    write.name = "Test Instance for Areas";
    write.nameSet = true;
    instance_id_ = securt_instance_manager_->createInstance("test-area-instance-001", write);
  }

  Json::Value createValidAreaBase() {
    Json::Value area;
    area["name"] = "Test Area";
    Json::Value coordinates(Json::arrayValue);
    Json::Value coord1;
    coord1["x"] = 0.1;
    coord1["y"] = 0.1;
    Json::Value coord2;
    coord2["x"] = 0.5;
    coord2["y"] = 0.1;
    Json::Value coord3;
    coord3["x"] = 0.5;
    coord3["y"] = 0.5;
    Json::Value coord4;
    coord4["x"] = 0.1;
    coord4["y"] = 0.5;
    coordinates.append(coord1);
    coordinates.append(coord2);
    coordinates.append(coord3);
    coordinates.append(coord4);
    area["coordinates"] = coordinates;
    Json::Value classes(Json::arrayValue);
    classes.append("Person");
    area["classes"] = classes;
    Json::Value color(Json::arrayValue);
    color.append(1.0);
    color.append(0.0);
    color.append(0.0);
    color.append(1.0);
    area["color"] = color;
    return area;
  }

  std::unique_ptr<AreaHandler> handler_;
  std::unique_ptr<AreaManager> area_manager_;
  std::unique_ptr<AreaStorage> area_storage_;
  std::unique_ptr<SecuRTInstanceManager> securt_instance_manager_;
  std::unique_ptr<InProcessInstanceManager> core_instance_manager_;
  std::unique_ptr<InstanceRegistry> instance_registry_;
  SolutionRegistry *solution_registry_; // Singleton, don't own
  std::unique_ptr<PipelineBuilder> pipeline_builder_;
  std::unique_ptr<InstanceStorage> instance_storage_;
  std::filesystem::path test_storage_dir_;
  std::string instance_id_;
};

// ============================================================================
// Test Common Validation
// ============================================================================

TEST_F(AreaHandlerTest, CreateAreaInstanceNotFound) {
  auto req = HttpRequest::newHttpRequest();
  req->setPath("/v1/securt/instance/nonexistent-instance/area/crossing");
  req->setMethod(Post);

  Json::Value body = createValidAreaBase();
  req->setBody(body.toStyledString());

  HttpResponsePtr response;
  bool callbackCalled = false;

  handler_->createCrossingArea(req, [&](const HttpResponsePtr &resp) {
    callbackCalled = true;
    response = resp;
  });

  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  ASSERT_TRUE(callbackCalled);
  ASSERT_NE(response, nullptr);
  // Should return 400 (validation error) or 404
  EXPECT_TRUE(response->statusCode() == k400BadRequest ||
              response->statusCode() == k404NotFound);
}

TEST_F(AreaHandlerTest, CreateAreaInvalidJson) {
  if (instance_id_.empty()) {
    GTEST_SKIP() << "Test instance not created, skipping test";
  }

  auto req = HttpRequest::newHttpRequest();
  req->setPath("/v1/securt/instance/" + instance_id_ + "/area/crossing");
  req->setMethod(Post);
  req->setBody("invalid json");

  HttpResponsePtr response;
  bool callbackCalled = false;

  handler_->createCrossingArea(req, [&](const HttpResponsePtr &resp) {
    callbackCalled = true;
    response = resp;
  });

  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  ASSERT_TRUE(callbackCalled);
  ASSERT_NE(response, nullptr);
  EXPECT_EQ(response->statusCode(), k400BadRequest);
}

TEST_F(AreaHandlerTest, CreateAreaInvalidCoordinates) {
  if (instance_id_.empty()) {
    GTEST_SKIP() << "Test instance not created, skipping test";
  }

  auto req = HttpRequest::newHttpRequest();
  req->setPath("/v1/securt/instance/" + instance_id_ + "/area/crossing");
  req->setMethod(Post);

  Json::Value body;
  body["name"] = "Invalid Area";
  Json::Value coordinates(Json::arrayValue);
  // Only one coordinate (need at least 3)
  Json::Value coord1;
  coord1["x"] = 0.1;
  coord1["y"] = 0.1;
  coordinates.append(coord1);
  body["coordinates"] = coordinates;
  Json::Value classes(Json::arrayValue);
  classes.append("Person");
  body["classes"] = classes;

  req->setBody(body.toStyledString());

  HttpResponsePtr response;
  bool callbackCalled = false;

  handler_->createCrossingArea(req, [&](const HttpResponsePtr &resp) {
    callbackCalled = true;
    response = resp;
  });

  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  ASSERT_TRUE(callbackCalled);
  ASSERT_NE(response, nullptr);
  EXPECT_EQ(response->statusCode(), k400BadRequest);
}

// ============================================================================
// Test Crossing Area
// ============================================================================

TEST_F(AreaHandlerTest, CreateCrossingArea) {
  if (instance_id_.empty()) {
    GTEST_SKIP() << "Test instance not created, skipping test";
  }

  auto req = HttpRequest::newHttpRequest();
  req->setPath("/v1/securt/instance/" + instance_id_ + "/area/crossing");
  req->setMethod(Post);

  Json::Value body = createValidAreaBase();
  body["ignoreStationaryObjects"] = false;
  body["areaEvent"] = "Both";
  req->setBody(body.toStyledString());

  HttpResponsePtr response;
  bool callbackCalled = false;

  handler_->createCrossingArea(req, [&](const HttpResponsePtr &resp) {
    callbackCalled = true;
    response = resp;
  });

  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  ASSERT_TRUE(callbackCalled);
  ASSERT_NE(response, nullptr);
  EXPECT_EQ(response->statusCode(), k201Created);

  auto json = response->getJsonObject();
  ASSERT_NE(json, nullptr);
  EXPECT_TRUE(json->isMember("areaId"));
  EXPECT_FALSE((*json)["areaId"].asString().empty());
}

TEST_F(AreaHandlerTest, CreateCrossingAreaWithId) {
  if (instance_id_.empty()) {
    GTEST_SKIP() << "Test instance not created, skipping test";
  }

  auto req = HttpRequest::newHttpRequest();
  req->setPath("/v1/securt/instance/" + instance_id_ +
               "/area/crossing/test-crossing-area-001");
  req->setMethod(Put);

  Json::Value body = createValidAreaBase();
  body["ignoreStationaryObjects"] = true;
  body["areaEvent"] = "Enter";
  req->setBody(body.toStyledString());

  HttpResponsePtr response;
  bool callbackCalled = false;

  handler_->createCrossingAreaWithId(req, [&](const HttpResponsePtr &resp) {
    callbackCalled = true;
    response = resp;
  });

  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  ASSERT_TRUE(callbackCalled);
  ASSERT_NE(response, nullptr);
  EXPECT_EQ(response->statusCode(), k201Created);
}

// ============================================================================
// Test Intrusion Area
// ============================================================================

TEST_F(AreaHandlerTest, CreateIntrusionArea) {
  if (instance_id_.empty()) {
    GTEST_SKIP() << "Test instance not created, skipping test";
  }

  auto req = HttpRequest::newHttpRequest();
  req->setPath("/v1/securt/instance/" + instance_id_ + "/area/intrusion");
  req->setMethod(Post);

  Json::Value body = createValidAreaBase();
  req->setBody(body.toStyledString());

  HttpResponsePtr response;
  bool callbackCalled = false;

  handler_->createIntrusionArea(req, [&](const HttpResponsePtr &resp) {
    callbackCalled = true;
    response = resp;
  });

  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  ASSERT_TRUE(callbackCalled);
  ASSERT_NE(response, nullptr);
  EXPECT_EQ(response->statusCode(), k201Created);

  auto json = response->getJsonObject();
  ASSERT_NE(json, nullptr);
  EXPECT_TRUE(json->isMember("areaId"));
}

// ============================================================================
// Test Loitering Area
// ============================================================================

TEST_F(AreaHandlerTest, CreateLoiteringArea) {
  if (instance_id_.empty()) {
    GTEST_SKIP() << "Test instance not created, skipping test";
  }

  auto req = HttpRequest::newHttpRequest();
  req->setPath("/v1/securt/instance/" + instance_id_ + "/area/loitering");
  req->setMethod(Post);

  Json::Value body = createValidAreaBase();
  body["seconds"] = 10;
  req->setBody(body.toStyledString());

  HttpResponsePtr response;
  bool callbackCalled = false;

  handler_->createLoiteringArea(req, [&](const HttpResponsePtr &resp) {
    callbackCalled = true;
    response = resp;
  });

  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  ASSERT_TRUE(callbackCalled);
  ASSERT_NE(response, nullptr);
  EXPECT_EQ(response->statusCode(), k201Created);

  auto json = response->getJsonObject();
  ASSERT_NE(json, nullptr);
  EXPECT_TRUE(json->isMember("areaId"));
}

// ============================================================================
// Test Crowding Area
// ============================================================================

TEST_F(AreaHandlerTest, CreateCrowdingArea) {
  if (instance_id_.empty()) {
    GTEST_SKIP() << "Test instance not created, skipping test";
  }

  auto req = HttpRequest::newHttpRequest();
  req->setPath("/v1/securt/instance/" + instance_id_ + "/area/crowding");
  req->setMethod(Post);

  Json::Value body = createValidAreaBase();
  body["objectCount"] = 5;
  body["seconds"] = 3;
  req->setBody(body.toStyledString());

  HttpResponsePtr response;
  bool callbackCalled = false;

  handler_->createCrowdingArea(req, [&](const HttpResponsePtr &resp) {
    callbackCalled = true;
    response = resp;
  });

  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  ASSERT_TRUE(callbackCalled);
  ASSERT_NE(response, nullptr);
  EXPECT_EQ(response->statusCode(), k201Created);

  auto json = response->getJsonObject();
  ASSERT_NE(json, nullptr);
  EXPECT_TRUE(json->isMember("areaId"));
}

// ============================================================================
// Test Occupancy Area
// ============================================================================

TEST_F(AreaHandlerTest, CreateOccupancyArea) {
  if (instance_id_.empty()) {
    GTEST_SKIP() << "Test instance not created, skipping test";
  }

  auto req = HttpRequest::newHttpRequest();
  req->setPath("/v1/securt/instance/" + instance_id_ + "/area/occupancy");
  req->setMethod(Post);

  Json::Value body = createValidAreaBase();
  req->setBody(body.toStyledString());

  HttpResponsePtr response;
  bool callbackCalled = false;

  handler_->createOccupancyArea(req, [&](const HttpResponsePtr &resp) {
    callbackCalled = true;
    response = resp;
  });

  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  ASSERT_TRUE(callbackCalled);
  ASSERT_NE(response, nullptr);
  EXPECT_EQ(response->statusCode(), k201Created);

  auto json = response->getJsonObject();
  ASSERT_NE(json, nullptr);
  EXPECT_TRUE(json->isMember("areaId"));
}

// ============================================================================
// Test Crowd Estimation Area
// ============================================================================

TEST_F(AreaHandlerTest, CreateCrowdEstimationArea) {
  if (instance_id_.empty()) {
    GTEST_SKIP() << "Test instance not created, skipping test";
  }

  auto req = HttpRequest::newHttpRequest();
  req->setPath("/v1/securt/instance/" + instance_id_ +
               "/area/crowdEstimation");
  req->setMethod(Post);

  Json::Value body = createValidAreaBase();
  req->setBody(body.toStyledString());

  HttpResponsePtr response;
  bool callbackCalled = false;

  handler_->createCrowdEstimationArea(req, [&](const HttpResponsePtr &resp) {
    callbackCalled = true;
    response = resp;
  });

  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  ASSERT_TRUE(callbackCalled);
  ASSERT_NE(response, nullptr);
  EXPECT_EQ(response->statusCode(), k201Created);

  auto json = response->getJsonObject();
  ASSERT_NE(json, nullptr);
  EXPECT_TRUE(json->isMember("areaId"));
}

// ============================================================================
// Test Dwelling Area
// ============================================================================

TEST_F(AreaHandlerTest, CreateDwellingArea) {
  if (instance_id_.empty()) {
    GTEST_SKIP() << "Test instance not created, skipping test";
  }

  auto req = HttpRequest::newHttpRequest();
  req->setPath("/v1/securt/instance/" + instance_id_ + "/area/dwelling");
  req->setMethod(Post);

  Json::Value body = createValidAreaBase();
  body["seconds"] = 15;
  req->setBody(body.toStyledString());

  HttpResponsePtr response;
  bool callbackCalled = false;

  handler_->createDwellingArea(req, [&](const HttpResponsePtr &resp) {
    callbackCalled = true;
    response = resp;
  });

  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  ASSERT_TRUE(callbackCalled);
  ASSERT_NE(response, nullptr);
  EXPECT_EQ(response->statusCode(), k201Created);

  auto json = response->getJsonObject();
  ASSERT_NE(json, nullptr);
  EXPECT_TRUE(json->isMember("areaId"));
}

// ============================================================================
// Test Armed Person Area
// ============================================================================

TEST_F(AreaHandlerTest, CreateArmedPersonArea) {
  if (instance_id_.empty()) {
    GTEST_SKIP() << "Test instance not created, skipping test";
  }

  auto req = HttpRequest::newHttpRequest();
  req->setPath("/v1/securt/instance/" + instance_id_ + "/area/armedPerson");
  req->setMethod(Post);

  Json::Value body = createValidAreaBase();
  req->setBody(body.toStyledString());

  HttpResponsePtr response;
  bool callbackCalled = false;

  handler_->createArmedPersonArea(req, [&](const HttpResponsePtr &resp) {
    callbackCalled = true;
    response = resp;
  });

  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  ASSERT_TRUE(callbackCalled);
  ASSERT_NE(response, nullptr);
  EXPECT_EQ(response->statusCode(), k201Created);

  auto json = response->getJsonObject();
  ASSERT_NE(json, nullptr);
  EXPECT_TRUE(json->isMember("areaId"));
}

// ============================================================================
// Test Object Left Area
// ============================================================================

TEST_F(AreaHandlerTest, CreateObjectLeftArea) {
  if (instance_id_.empty()) {
    GTEST_SKIP() << "Test instance not created, skipping test";
  }

  auto req = HttpRequest::newHttpRequest();
  req->setPath("/v1/securt/instance/" + instance_id_ + "/area/objectLeft");
  req->setMethod(Post);

  Json::Value body = createValidAreaBase();
  body["seconds"] = 5;
  req->setBody(body.toStyledString());

  HttpResponsePtr response;
  bool callbackCalled = false;

  handler_->createObjectLeftArea(req, [&](const HttpResponsePtr &resp) {
    callbackCalled = true;
    response = resp;
  });

  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  ASSERT_TRUE(callbackCalled);
  ASSERT_NE(response, nullptr);
  EXPECT_EQ(response->statusCode(), k201Created);

  auto json = response->getJsonObject();
  ASSERT_NE(json, nullptr);
  EXPECT_TRUE(json->isMember("areaId"));
}

// ============================================================================
// Test Object Removed Area
// ============================================================================

TEST_F(AreaHandlerTest, CreateObjectRemovedArea) {
  if (instance_id_.empty()) {
    GTEST_SKIP() << "Test instance not created, skipping test";
  }

  auto req = HttpRequest::newHttpRequest();
  req->setPath("/v1/securt/instance/" + instance_id_ + "/area/objectRemoved");
  req->setMethod(Post);

  Json::Value body = createValidAreaBase();
  body["seconds"] = 5;
  req->setBody(body.toStyledString());

  HttpResponsePtr response;
  bool callbackCalled = false;

  handler_->createObjectRemovedArea(req, [&](const HttpResponsePtr &resp) {
    callbackCalled = true;
    response = resp;
  });

  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  ASSERT_TRUE(callbackCalled);
  ASSERT_NE(response, nullptr);
  EXPECT_EQ(response->statusCode(), k201Created);

  auto json = response->getJsonObject();
  ASSERT_NE(json, nullptr);
  EXPECT_TRUE(json->isMember("areaId"));
}

// ============================================================================
// Test Fallen Person Area
// ============================================================================

TEST_F(AreaHandlerTest, CreateFallenPersonArea) {
  if (instance_id_.empty()) {
    GTEST_SKIP() << "Test instance not created, skipping test";
  }

  auto req = HttpRequest::newHttpRequest();
  req->setPath("/v1/securt/instance/" + instance_id_ + "/area/fallenPerson");
  req->setMethod(Post);

  Json::Value body = createValidAreaBase();
  req->setBody(body.toStyledString());

  HttpResponsePtr response;
  bool callbackCalled = false;

  handler_->createFallenPersonArea(req, [&](const HttpResponsePtr &resp) {
    callbackCalled = true;
    response = resp;
  });

  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  ASSERT_TRUE(callbackCalled);
  ASSERT_NE(response, nullptr);
  EXPECT_EQ(response->statusCode(), k201Created);

  auto json = response->getJsonObject();
  ASSERT_NE(json, nullptr);
  EXPECT_TRUE(json->isMember("areaId"));
}

// ============================================================================
// Test Experimental Areas
// ============================================================================

TEST_F(AreaHandlerTest, CreateVehicleGuardArea) {
  if (instance_id_.empty()) {
    GTEST_SKIP() << "Test instance not created, skipping test";
  }

  auto req = HttpRequest::newHttpRequest();
  req->setPath("/v1/securt/instance/" + instance_id_ + "/area/vehicleGuard");
  req->setMethod(Post);

  Json::Value body = createValidAreaBase();
  Json::Value classes(Json::arrayValue);
  classes.append("Vehicle");
  body["classes"] = classes;
  req->setBody(body.toStyledString());

  HttpResponsePtr response;
  bool callbackCalled = false;

  handler_->createVehicleGuardArea(req, [&](const HttpResponsePtr &resp) {
    callbackCalled = true;
    response = resp;
  });

  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  ASSERT_TRUE(callbackCalled);
  ASSERT_NE(response, nullptr);
  EXPECT_EQ(response->statusCode(), k201Created);

  auto json = response->getJsonObject();
  ASSERT_NE(json, nullptr);
  EXPECT_TRUE(json->isMember("areaId"));
}

TEST_F(AreaHandlerTest, CreateFaceCoveredArea) {
  if (instance_id_.empty()) {
    GTEST_SKIP() << "Test instance not created, skipping test";
  }

  auto req = HttpRequest::newHttpRequest();
  req->setPath("/v1/securt/instance/" + instance_id_ + "/area/faceCovered");
  req->setMethod(Post);

  Json::Value body = createValidAreaBase();
  Json::Value classes(Json::arrayValue);
  classes.append("Face");
  body["classes"] = classes;
  req->setBody(body.toStyledString());

  HttpResponsePtr response;
  bool callbackCalled = false;

  handler_->createFaceCoveredArea(req, [&](const HttpResponsePtr &resp) {
    callbackCalled = true;
    response = resp;
  });

  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  ASSERT_TRUE(callbackCalled);
  ASSERT_NE(response, nullptr);
  EXPECT_EQ(response->statusCode(), k201Created);

  auto json = response->getJsonObject();
  ASSERT_NE(json, nullptr);
  EXPECT_TRUE(json->isMember("areaId"));
}

// ============================================================================
// Test Common Operations
// ============================================================================

TEST_F(AreaHandlerTest, GetAllAreas) {
  if (instance_id_.empty()) {
    GTEST_SKIP() << "Test instance not created, skipping test";
  }

  // First create a few areas
  Json::Value body1 = createValidAreaBase();
  body1["name"] = "Crossing Area 1";
  body1["ignoreStationaryObjects"] = false;
  body1["areaEvent"] = "Both";

  auto req1 = HttpRequest::newHttpRequest();
  req1->setPath("/v1/securt/instance/" + instance_id_ + "/area/crossing");
  req1->setMethod(Post);
  req1->setBody(body1.toStyledString());

  handler_->createCrossingArea(req1, [](const HttpResponsePtr &) {});

  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  // Now get all areas
  auto req = HttpRequest::newHttpRequest();
  req->setPath("/v1/securt/instance/" + instance_id_ + "/areas");
  req->setMethod(Get);

  HttpResponsePtr response;
  bool callbackCalled = false;

  handler_->getAllAreas(req, [&](const HttpResponsePtr &resp) {
    callbackCalled = true;
    response = resp;
  });

  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  ASSERT_TRUE(callbackCalled);
  ASSERT_NE(response, nullptr);
  EXPECT_EQ(response->statusCode(), k200OK);

  auto json = response->getJsonObject();
  ASSERT_NE(json, nullptr);
  // Verify structure has all area types
  EXPECT_TRUE(json->isMember("crossing"));
  EXPECT_TRUE(json->isMember("intrusion"));
  EXPECT_TRUE(json->isMember("loitering"));
  EXPECT_TRUE(json->isMember("crowding"));
  EXPECT_TRUE(json->isMember("occupancy"));
  EXPECT_TRUE(json->isMember("crowdEstimation"));
  EXPECT_TRUE(json->isMember("dwelling"));
  EXPECT_TRUE(json->isMember("armedPerson"));
  EXPECT_TRUE(json->isMember("objectLeft"));
  EXPECT_TRUE(json->isMember("objectRemoved"));
  EXPECT_TRUE(json->isMember("fallenPerson"));
  EXPECT_TRUE(json->isMember("vehicleGuard"));
  EXPECT_TRUE(json->isMember("faceCovered"));
}

TEST_F(AreaHandlerTest, DeleteArea) {
  if (instance_id_.empty()) {
    GTEST_SKIP() << "Test instance not created, skipping test";
  }

  // First create an area
  auto createReq = HttpRequest::newHttpRequest();
  createReq->setPath("/v1/securt/instance/" + instance_id_ + "/area/crossing");
  createReq->setMethod(Post);

  Json::Value body = createValidAreaBase();
  createReq->setBody(body.toStyledString());

  HttpResponsePtr createResponse;
  bool createCallbackCalled = false;
  std::string areaId;

  handler_->createCrossingArea(createReq, [&](const HttpResponsePtr &resp) {
    createCallbackCalled = true;
    createResponse = resp;
    if (resp && resp->statusCode() == k201Created) {
      auto json = resp->getJsonObject();
      if (json && json->isMember("areaId")) {
        areaId = (*json)["areaId"].asString();
      }
    }
  });

  std::this_thread::sleep_for(std::chrono::milliseconds(200));

  if (areaId.empty()) {
    GTEST_SKIP() << "Failed to create area for delete test";
  }

  // Now delete the area
  auto req = HttpRequest::newHttpRequest();
  req->setPath("/v1/securt/instance/" + instance_id_ + "/area/" + areaId);
  req->setMethod(Delete);

  HttpResponsePtr response;
  bool callbackCalled = false;

  handler_->deleteArea(req, [&](const HttpResponsePtr &resp) {
    callbackCalled = true;
    response = resp;
  });

  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  ASSERT_TRUE(callbackCalled);
  ASSERT_NE(response, nullptr);
  EXPECT_EQ(response->statusCode(), k204NoContent);
}

TEST_F(AreaHandlerTest, DeleteAreaNotFound) {
  if (instance_id_.empty()) {
    GTEST_SKIP() << "Test instance not created, skipping test";
  }

  auto req = HttpRequest::newHttpRequest();
  req->setPath("/v1/securt/instance/" + instance_id_ +
               "/area/nonexistent-area-id");
  req->setMethod(Delete);

  HttpResponsePtr response;
  bool callbackCalled = false;

  handler_->deleteArea(req, [&](const HttpResponsePtr &resp) {
    callbackCalled = true;
    response = resp;
  });

  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  ASSERT_TRUE(callbackCalled);
  ASSERT_NE(response, nullptr);
  EXPECT_EQ(response->statusCode(), k404NotFound);
}

TEST_F(AreaHandlerTest, DeleteAllAreas) {
  if (instance_id_.empty()) {
    GTEST_SKIP() << "Test instance not created, skipping test";
  }

  // First create a couple of areas
  for (int i = 0; i < 2; i++) {
    auto createReq = HttpRequest::newHttpRequest();
    createReq->setPath("/v1/securt/instance/" + instance_id_ + "/area/crossing");
    createReq->setMethod(Post);

    Json::Value body = createValidAreaBase();
    body["name"] = "Area " + std::to_string(i);
    createReq->setBody(body.toStyledString());

    handler_->createCrossingArea(createReq, [](const HttpResponsePtr &) {});
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }

  // Now delete all areas
  auto req = HttpRequest::newHttpRequest();
  req->setPath("/v1/securt/instance/" + instance_id_ + "/areas");
  req->setMethod(Delete);

  HttpResponsePtr response;
  bool callbackCalled = false;

  handler_->deleteAllAreas(req, [&](const HttpResponsePtr &resp) {
    callbackCalled = true;
    response = resp;
  });

  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  ASSERT_TRUE(callbackCalled);
  ASSERT_NE(response, nullptr);
  EXPECT_EQ(response->statusCode(), k204NoContent);
}

// ============================================================================
// Test OPTIONS (CORS)
// ============================================================================

TEST_F(AreaHandlerTest, HandleOptions) {
  auto req = HttpRequest::newHttpRequest();
  req->setPath("/v1/securt/instance/test/area/crossing");
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

