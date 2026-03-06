#include "api/area_handler.h"
#include "core/area_manager.h"
#include "core/area_types_specific.h"
#include "core/logging_flags.h"
#include "core/logger.h"
#include "core/metrics_interceptor.h"
#include <chrono>
#include <drogon/HttpResponse.h>
#include <iostream>

AreaManager *AreaHandler::area_manager_ = nullptr;

void AreaHandler::setAreaManager(AreaManager *manager) {
  area_manager_ = manager;
}

// ========================================================================
// Standard Areas - POST handlers
// ========================================================================

void AreaHandler::createCrossingArea(
    const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback) {
  auto start_time = std::chrono::steady_clock::now();

  if (isApiLoggingEnabled()) {
    PLOG_INFO << "[API] POST /v1/securt/instance/{instanceId}/area/crossing - Create crossing area";
    PLOG_DEBUG << "[API] Request from: " << req->getPeerAddr().toIpPort();
    PLOG_DEBUG << "[API] Request path: " << req->getPath();
  }

  try {
    if (!area_manager_) {
      if (isApiLoggingEnabled()) {
        PLOG_ERROR << "[API] POST /v1/securt/instance/{instanceId}/area/crossing - Error: Area manager not initialized";
      }
      callback(createErrorResponse(500, "Internal server error",
                                   "Area manager not initialized"));
      return;
    }

    std::string instanceId = extractInstanceId(req);
    if (instanceId.empty()) {
      if (isApiLoggingEnabled()) {
        PLOG_WARNING << "[API] POST /v1/securt/instance/{instanceId}/area/crossing - Error: Instance ID is required";
      }
      callback(createErrorResponse(400, "Invalid request",
                                   "Instance ID is required"));
      return;
    }

    if (isApiLoggingEnabled()) {
      PLOG_DEBUG << "[API] POST /v1/securt/instance/" << instanceId << "/area/crossing - Instance ID: " << instanceId;
    }

    auto json = req->getJsonObject();
    if (!json) {
      if (isApiLoggingEnabled()) {
        PLOG_WARNING << "[API] POST /v1/securt/instance/" << instanceId << "/area/crossing - Error: Invalid JSON body";
        PLOG_DEBUG << "[API] Request body: " << req->getBody();
      }
      callback(createErrorResponse(400, "Invalid request",
                                   "Request body must be valid JSON"));
      return;
    }

    if (isApiLoggingEnabled()) {
      Json::StreamWriterBuilder builder;
      builder["indentation"] = "";
      std::string jsonStr = Json::writeString(builder, *json);
      PLOG_DEBUG << "[API] POST /v1/securt/instance/" << instanceId << "/area/crossing - Request body: " << jsonStr;
    }

    CrossingAreaWrite write = CrossingAreaWrite::fromJson(*json);
    if (isApiLoggingEnabled()) {
      PLOG_DEBUG << "[API] POST /v1/securt/instance/" << instanceId << "/area/crossing - Parsed area data - name: " << write.name
                 << ", coordinates count: " << write.coordinates.size()
                 << ", classes count: " << write.classes.size();
    }

    if (isApiLoggingEnabled()) {
      PLOG_DEBUG << "[API] POST /v1/securt/instance/" << instanceId << "/area/crossing - Calling area_manager_->createCrossingArea()";
    }
    std::string areaId = area_manager_->createCrossingArea(instanceId, write);

    if (areaId.empty()) {
      auto end_time = std::chrono::steady_clock::now();
      auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
          end_time - start_time);
      if (isApiLoggingEnabled()) {
        PLOG_WARNING << "[API] POST /v1/securt/instance/" << instanceId << "/area/crossing - Failed to create area - " << duration.count() << "ms";
      }
      callback(createErrorResponse(400, "Invalid request",
                                   "Failed to create area. Check validation errors."));
      return;
    }

    Json::Value response;
    response["areaId"] = areaId;

    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time);

    if (isApiLoggingEnabled()) {
      PLOG_INFO << "[API] POST /v1/securt/instance/" << instanceId << "/area/crossing - Success: Created area " << areaId << " - " << duration.count() << "ms";
      Json::StreamWriterBuilder builder;
      builder["indentation"] = "";
      std::string responseStr = Json::writeString(builder, response);
      PLOG_DEBUG << "[API] POST /v1/securt/instance/" << instanceId << "/area/crossing - Response: " << responseStr;
    }

    auto resp = HttpResponse::newHttpJsonResponse(response);
    resp->setStatusCode(k201Created);
    resp->addHeader("Access-Control-Allow-Origin", "*");
    resp->addHeader("Access-Control-Allow-Methods", "POST, OPTIONS");
    resp->addHeader("Access-Control-Allow-Headers", "Content-Type");

    MetricsInterceptor::callWithMetrics(req, resp, std::move(callback));

  } catch (const std::exception &e) {
    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time);
    if (isApiLoggingEnabled()) {
      PLOG_ERROR << "[API] POST /v1/securt/instance/{instanceId}/area/crossing - Exception: " << e.what() << " - " << duration.count() << "ms";
    }
    callback(createErrorResponse(500, "Internal server error", e.what()));
  } catch (...) {
    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time);
    if (isApiLoggingEnabled()) {
      PLOG_ERROR << "[API] POST /v1/securt/instance/{instanceId}/area/crossing - Unknown exception - " << duration.count() << "ms";
    }
    callback(createErrorResponse(500, "Internal server error",
                                 "Unknown error occurred"));
  }
}

void AreaHandler::createIntrusionArea(
    const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback) {
  auto start_time = std::chrono::steady_clock::now();

  try {
    if (!area_manager_) {
      callback(createErrorResponse(500, "Internal server error",
                                   "Area manager not initialized"));
      return;
    }

    std::string instanceId = extractInstanceId(req);
    if (instanceId.empty()) {
      callback(createErrorResponse(400, "Invalid request",
                                   "Instance ID is required"));
      return;
    }

    auto json = req->getJsonObject();
    if (!json) {
      callback(createErrorResponse(400, "Invalid request",
                                   "Request body must be valid JSON"));
      return;
    }

    IntrusionAreaWrite write = IntrusionAreaWrite::fromJson(*json);
    std::string areaId = area_manager_->createIntrusionArea(instanceId, write);

    if (areaId.empty()) {
      callback(createErrorResponse(400, "Invalid request",
                                   "Failed to create area"));
      return;
    }

    Json::Value response;
    response["areaId"] = areaId;

    auto resp = HttpResponse::newHttpJsonResponse(response);
    resp->setStatusCode(k201Created);
    resp->addHeader("Access-Control-Allow-Origin", "*");
    resp->addHeader("Access-Control-Allow-Methods", "POST, OPTIONS");
    resp->addHeader("Access-Control-Allow-Headers", "Content-Type");

    MetricsInterceptor::callWithMetrics(req, resp, std::move(callback));

  } catch (const std::exception &e) {
    callback(createErrorResponse(500, "Internal server error", e.what()));
  } catch (...) {
    callback(createErrorResponse(500, "Internal server error",
                                 "Unknown error occurred"));
  }
}

void AreaHandler::createLoiteringArea(
    const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback) {
  try {
    if (!area_manager_) {
      callback(createErrorResponse(500, "Internal server error",
                                   "Area manager not initialized"));
      return;
    }

    std::string instanceId = extractInstanceId(req);
    if (instanceId.empty()) {
      callback(createErrorResponse(400, "Invalid request",
                                   "Instance ID is required"));
      return;
    }

    auto json = req->getJsonObject();
    if (!json) {
      callback(createErrorResponse(400, "Invalid request",
                                   "Request body must be valid JSON"));
      return;
    }

    LoiteringAreaWrite write = LoiteringAreaWrite::fromJson(*json);
    std::string areaId = area_manager_->createLoiteringArea(instanceId, write);

    if (areaId.empty()) {
      callback(createErrorResponse(400, "Invalid request",
                                   "Failed to create area"));
      return;
    }

    Json::Value response;
    response["areaId"] = areaId;

    auto resp = HttpResponse::newHttpJsonResponse(response);
    resp->setStatusCode(k201Created);
    resp->addHeader("Access-Control-Allow-Origin", "*");
    resp->addHeader("Access-Control-Allow-Methods", "POST, OPTIONS");
    resp->addHeader("Access-Control-Allow-Headers", "Content-Type");

    MetricsInterceptor::callWithMetrics(req, resp, std::move(callback));

  } catch (const std::exception &e) {
    callback(createErrorResponse(500, "Internal server error", e.what()));
  } catch (...) {
    callback(createErrorResponse(500, "Internal server error",
                                 "Unknown error occurred"));
  }
}

void AreaHandler::createCrowdingArea(
    const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback) {
  try {
    if (!area_manager_) {
      callback(createErrorResponse(500, "Internal server error",
                                   "Area manager not initialized"));
      return;
    }

    std::string instanceId = extractInstanceId(req);
    if (instanceId.empty()) {
      callback(createErrorResponse(400, "Invalid request",
                                   "Instance ID is required"));
      return;
    }

    auto json = req->getJsonObject();
    if (!json) {
      callback(createErrorResponse(400, "Invalid request",
                                   "Request body must be valid JSON"));
      return;
    }

    CrowdingAreaWrite write = CrowdingAreaWrite::fromJson(*json);
    std::string areaId = area_manager_->createCrowdingArea(instanceId, write);

    if (areaId.empty()) {
      callback(createErrorResponse(400, "Invalid request",
                                   "Failed to create area"));
      return;
    }

    Json::Value response;
    response["areaId"] = areaId;

    auto resp = HttpResponse::newHttpJsonResponse(response);
    resp->setStatusCode(k201Created);
    resp->addHeader("Access-Control-Allow-Origin", "*");
    resp->addHeader("Access-Control-Allow-Methods", "POST, OPTIONS");
    resp->addHeader("Access-Control-Allow-Headers", "Content-Type");

    MetricsInterceptor::callWithMetrics(req, resp, std::move(callback));

  } catch (const std::exception &e) {
    callback(createErrorResponse(500, "Internal server error", e.what()));
  } catch (...) {
    callback(createErrorResponse(500, "Internal server error",
                                 "Unknown error occurred"));
  }
}

void AreaHandler::createOccupancyArea(
    const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback) {
  try {
    if (!area_manager_) {
      callback(createErrorResponse(500, "Internal server error",
                                   "Area manager not initialized"));
      return;
    }

    std::string instanceId = extractInstanceId(req);
    if (instanceId.empty()) {
      callback(createErrorResponse(400, "Invalid request",
                                   "Instance ID is required"));
      return;
    }

    auto json = req->getJsonObject();
    if (!json) {
      callback(createErrorResponse(400, "Invalid request",
                                   "Request body must be valid JSON"));
      return;
    }

    OccupancyAreaWrite write = OccupancyAreaWrite::fromJson(*json);
    std::string areaId = area_manager_->createOccupancyArea(instanceId, write);

    if (areaId.empty()) {
      callback(createErrorResponse(400, "Invalid request",
                                   "Failed to create area"));
      return;
    }

    Json::Value response;
    response["areaId"] = areaId;

    auto resp = HttpResponse::newHttpJsonResponse(response);
    resp->setStatusCode(k201Created);
    resp->addHeader("Access-Control-Allow-Origin", "*");
    resp->addHeader("Access-Control-Allow-Methods", "POST, OPTIONS");
    resp->addHeader("Access-Control-Allow-Headers", "Content-Type");

    MetricsInterceptor::callWithMetrics(req, resp, std::move(callback));

  } catch (const std::exception &e) {
    callback(createErrorResponse(500, "Internal server error", e.what()));
  } catch (...) {
    callback(createErrorResponse(500, "Internal server error",
                                 "Unknown error occurred"));
  }
}

void AreaHandler::createCrowdEstimationArea(
    const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback) {
  try {
    if (!area_manager_) {
      callback(createErrorResponse(500, "Internal server error",
                                   "Area manager not initialized"));
      return;
    }

    std::string instanceId = extractInstanceId(req);
    if (instanceId.empty()) {
      callback(createErrorResponse(400, "Invalid request",
                                   "Instance ID is required"));
      return;
    }

    auto json = req->getJsonObject();
    if (!json) {
      callback(createErrorResponse(400, "Invalid request",
                                   "Request body must be valid JSON"));
      return;
    }

    CrowdEstimationAreaWrite write = CrowdEstimationAreaWrite::fromJson(*json);
    std::string areaId =
        area_manager_->createCrowdEstimationArea(instanceId, write);

    if (areaId.empty()) {
      callback(createErrorResponse(400, "Invalid request",
                                   "Failed to create area"));
      return;
    }

    Json::Value response;
    response["areaId"] = areaId;

    auto resp = HttpResponse::newHttpJsonResponse(response);
    resp->setStatusCode(k201Created);
    resp->addHeader("Access-Control-Allow-Origin", "*");
    resp->addHeader("Access-Control-Allow-Methods", "POST, OPTIONS");
    resp->addHeader("Access-Control-Allow-Headers", "Content-Type");

    MetricsInterceptor::callWithMetrics(req, resp, std::move(callback));

  } catch (const std::exception &e) {
    callback(createErrorResponse(500, "Internal server error", e.what()));
  } catch (...) {
    callback(createErrorResponse(500, "Internal server error",
                                 "Unknown error occurred"));
  }
}

void AreaHandler::createDwellingArea(
    const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback) {
  try {
    if (!area_manager_) {
      callback(createErrorResponse(500, "Internal server error",
                                   "Area manager not initialized"));
      return;
    }

    std::string instanceId = extractInstanceId(req);
    if (instanceId.empty()) {
      callback(createErrorResponse(400, "Invalid request",
                                   "Instance ID is required"));
      return;
    }

    auto json = req->getJsonObject();
    if (!json) {
      callback(createErrorResponse(400, "Invalid request",
                                   "Request body must be valid JSON"));
      return;
    }

    DwellingAreaWrite write = DwellingAreaWrite::fromJson(*json);
    std::string areaId = area_manager_->createDwellingArea(instanceId, write);

    if (areaId.empty()) {
      callback(createErrorResponse(400, "Invalid request",
                                   "Failed to create area"));
      return;
    }

    Json::Value response;
    response["areaId"] = areaId;

    auto resp = HttpResponse::newHttpJsonResponse(response);
    resp->setStatusCode(k201Created);
    resp->addHeader("Access-Control-Allow-Origin", "*");
    resp->addHeader("Access-Control-Allow-Methods", "POST, OPTIONS");
    resp->addHeader("Access-Control-Allow-Headers", "Content-Type");

    MetricsInterceptor::callWithMetrics(req, resp, std::move(callback));

  } catch (const std::exception &e) {
    callback(createErrorResponse(500, "Internal server error", e.what()));
  } catch (...) {
    callback(createErrorResponse(500, "Internal server error",
                                 "Unknown error occurred"));
  }
}

void AreaHandler::createArmedPersonArea(
    const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback) {
  try {
    if (!area_manager_) {
      callback(createErrorResponse(500, "Internal server error",
                                   "Area manager not initialized"));
      return;
    }

    std::string instanceId = extractInstanceId(req);
    if (instanceId.empty()) {
      callback(createErrorResponse(400, "Invalid request",
                                   "Instance ID is required"));
      return;
    }

    auto json = req->getJsonObject();
    if (!json) {
      callback(createErrorResponse(400, "Invalid request",
                                   "Request body must be valid JSON"));
      return;
    }

    ArmedPersonAreaWrite write = ArmedPersonAreaWrite::fromJson(*json);
    std::string areaId = area_manager_->createArmedPersonArea(instanceId, write);

    if (areaId.empty()) {
      callback(createErrorResponse(400, "Invalid request",
                                   "Failed to create area"));
      return;
    }

    Json::Value response;
    response["areaId"] = areaId;

    auto resp = HttpResponse::newHttpJsonResponse(response);
    resp->setStatusCode(k201Created);
    resp->addHeader("Access-Control-Allow-Origin", "*");
    resp->addHeader("Access-Control-Allow-Methods", "POST, OPTIONS");
    resp->addHeader("Access-Control-Allow-Headers", "Content-Type");

    MetricsInterceptor::callWithMetrics(req, resp, std::move(callback));

  } catch (const std::exception &e) {
    callback(createErrorResponse(500, "Internal server error", e.what()));
  } catch (...) {
    callback(createErrorResponse(500, "Internal server error",
                                 "Unknown error occurred"));
  }
}

void AreaHandler::createObjectLeftArea(
    const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback) {
  try {
    if (!area_manager_) {
      callback(createErrorResponse(500, "Internal server error",
                                   "Area manager not initialized"));
      return;
    }

    std::string instanceId = extractInstanceId(req);
    if (instanceId.empty()) {
      callback(createErrorResponse(400, "Invalid request",
                                   "Instance ID is required"));
      return;
    }

    auto json = req->getJsonObject();
    if (!json) {
      callback(createErrorResponse(400, "Invalid request",
                                   "Request body must be valid JSON"));
      return;
    }

    ObjectLeftAreaWrite write = ObjectLeftAreaWrite::fromJson(*json);
    std::string areaId = area_manager_->createObjectLeftArea(instanceId, write);

    if (areaId.empty()) {
      callback(createErrorResponse(400, "Invalid request",
                                   "Failed to create area"));
      return;
    }

    Json::Value response;
    response["areaId"] = areaId;

    auto resp = HttpResponse::newHttpJsonResponse(response);
    resp->setStatusCode(k201Created);
    resp->addHeader("Access-Control-Allow-Origin", "*");
    resp->addHeader("Access-Control-Allow-Methods", "POST, OPTIONS");
    resp->addHeader("Access-Control-Allow-Headers", "Content-Type");

    MetricsInterceptor::callWithMetrics(req, resp, std::move(callback));

  } catch (const std::exception &e) {
    callback(createErrorResponse(500, "Internal server error", e.what()));
  } catch (...) {
    callback(createErrorResponse(500, "Internal server error",
                                 "Unknown error occurred"));
  }
}

void AreaHandler::createObjectRemovedArea(
    const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback) {
  try {
    if (!area_manager_) {
      callback(createErrorResponse(500, "Internal server error",
                                   "Area manager not initialized"));
      return;
    }

    std::string instanceId = extractInstanceId(req);
    if (instanceId.empty()) {
      callback(createErrorResponse(400, "Invalid request",
                                   "Instance ID is required"));
      return;
    }

    auto json = req->getJsonObject();
    if (!json) {
      callback(createErrorResponse(400, "Invalid request",
                                   "Request body must be valid JSON"));
      return;
    }

    ObjectRemovedAreaWrite write = ObjectRemovedAreaWrite::fromJson(*json);
    std::string areaId =
        area_manager_->createObjectRemovedArea(instanceId, write);

    if (areaId.empty()) {
      callback(createErrorResponse(400, "Invalid request",
                                   "Failed to create area"));
      return;
    }

    Json::Value response;
    response["areaId"] = areaId;

    auto resp = HttpResponse::newHttpJsonResponse(response);
    resp->setStatusCode(k201Created);
    resp->addHeader("Access-Control-Allow-Origin", "*");
    resp->addHeader("Access-Control-Allow-Methods", "POST, OPTIONS");
    resp->addHeader("Access-Control-Allow-Headers", "Content-Type");

    MetricsInterceptor::callWithMetrics(req, resp, std::move(callback));

  } catch (const std::exception &e) {
    callback(createErrorResponse(500, "Internal server error", e.what()));
  } catch (...) {
    callback(createErrorResponse(500, "Internal server error",
                                 "Unknown error occurred"));
  }
}

void AreaHandler::createFallenPersonArea(
    const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback) {
  try {
    if (!area_manager_) {
      callback(createErrorResponse(500, "Internal server error",
                                   "Area manager not initialized"));
      return;
    }

    std::string instanceId = extractInstanceId(req);
    if (instanceId.empty()) {
      callback(createErrorResponse(400, "Invalid request",
                                   "Instance ID is required"));
      return;
    }

    auto json = req->getJsonObject();
    if (!json) {
      callback(createErrorResponse(400, "Invalid request",
                                   "Request body must be valid JSON"));
      return;
    }

    FallenPersonAreaWrite write = FallenPersonAreaWrite::fromJson(*json);
    std::string areaId = area_manager_->createFallenPersonArea(instanceId, write);

    if (areaId.empty()) {
      callback(createErrorResponse(400, "Invalid request",
                                   "Failed to create area"));
      return;
    }

    Json::Value response;
    response["areaId"] = areaId;

    auto resp = HttpResponse::newHttpJsonResponse(response);
    resp->setStatusCode(k201Created);
    resp->addHeader("Access-Control-Allow-Origin", "*");
    resp->addHeader("Access-Control-Allow-Methods", "POST, OPTIONS");
    resp->addHeader("Access-Control-Allow-Headers", "Content-Type");

    MetricsInterceptor::callWithMetrics(req, resp, std::move(callback));

  } catch (const std::exception &e) {
    callback(createErrorResponse(500, "Internal server error", e.what()));
  } catch (...) {
    callback(createErrorResponse(500, "Internal server error",
                                 "Unknown error occurred"));
  }
}

void AreaHandler::createObjectEnterExitArea(
    const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback) {
  auto start_time = std::chrono::steady_clock::now();

  if (isApiLoggingEnabled()) {
    PLOG_INFO << "[API] POST /v1/securt/instance/{instanceId}/area/objectEnterExit - Create object enter/exit area";
    PLOG_DEBUG << "[API] Request from: " << req->getPeerAddr().toIpPort();
    PLOG_DEBUG << "[API] Request path: " << req->getPath();
  }

  try {
    if (!area_manager_) {
      if (isApiLoggingEnabled()) {
        PLOG_ERROR << "[API] POST /v1/securt/instance/{instanceId}/area/objectEnterExit - Error: Area manager not initialized";
      }
      callback(createErrorResponse(500, "Internal server error",
                                   "Area manager not initialized"));
      return;
    }

    std::string instanceId = extractInstanceId(req);
    if (instanceId.empty()) {
      if (isApiLoggingEnabled()) {
        PLOG_WARNING << "[API] POST /v1/securt/instance/{instanceId}/area/objectEnterExit - Error: Instance ID is required";
      }
      callback(createErrorResponse(400, "Invalid request",
                                   "Instance ID is required"));
      return;
    }

    if (isApiLoggingEnabled()) {
      PLOG_DEBUG << "[API] POST /v1/securt/instance/" << instanceId << "/area/objectEnterExit - Instance ID: " << instanceId;
    }

    auto json = req->getJsonObject();
    if (!json) {
      if (isApiLoggingEnabled()) {
        PLOG_WARNING << "[API] POST /v1/securt/instance/" << instanceId << "/area/objectEnterExit - Error: Invalid JSON body";
        PLOG_DEBUG << "[API] Request body: " << req->getBody();
      }
      callback(createErrorResponse(400, "Invalid request",
                                   "Request body must be valid JSON"));
      return;
    }

    if (isApiLoggingEnabled()) {
      Json::StreamWriterBuilder builder;
      builder["indentation"] = "";
      std::string jsonStr = Json::writeString(builder, *json);
      PLOG_DEBUG << "[API] POST /v1/securt/instance/" << instanceId << "/area/objectEnterExit - Request body: " << jsonStr;
    }

    ObjectEnterExitAreaWrite write = ObjectEnterExitAreaWrite::fromJson(*json);
    if (isApiLoggingEnabled()) {
      PLOG_DEBUG << "[API] POST /v1/securt/instance/" << instanceId << "/area/objectEnterExit - Parsed area data - name: " << write.name
                 << ", coordinates count: " << write.coordinates.size()
                 << ", alertOnEnter: " << write.alertOnEnter
                 << ", alertOnExit: " << write.alertOnExit;
    }

    if (isApiLoggingEnabled()) {
      PLOG_DEBUG << "[API] POST /v1/securt/instance/" << instanceId << "/area/objectEnterExit - Calling area_manager_->createObjectEnterExitArea()";
    }
    std::string areaId = area_manager_->createObjectEnterExitArea(instanceId, write);

    if (areaId.empty()) {
      auto end_time = std::chrono::steady_clock::now();
      auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
          end_time - start_time);
      if (isApiLoggingEnabled()) {
        PLOG_WARNING << "[API] POST /v1/securt/instance/" << instanceId << "/area/objectEnterExit - Failed to create area - " << duration.count() << "ms";
      }
      callback(createErrorResponse(400, "Invalid request",
                                   "Failed to create area. Check validation errors."));
      return;
    }

    Json::Value response;
    response["areaId"] = areaId;

    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time);

    if (isApiLoggingEnabled()) {
      PLOG_INFO << "[API] POST /v1/securt/instance/" << instanceId << "/area/objectEnterExit - Success: Created area " << areaId << " - " << duration.count() << "ms";
      Json::StreamWriterBuilder builder;
      builder["indentation"] = "";
      std::string responseStr = Json::writeString(builder, response);
      PLOG_DEBUG << "[API] POST /v1/securt/instance/" << instanceId << "/area/objectEnterExit - Response: " << responseStr;
    }

    auto resp = HttpResponse::newHttpJsonResponse(response);
    resp->setStatusCode(k201Created);
    resp->addHeader("Access-Control-Allow-Origin", "*");
    resp->addHeader("Access-Control-Allow-Methods", "POST, OPTIONS");
    resp->addHeader("Access-Control-Allow-Headers", "Content-Type");

    MetricsInterceptor::callWithMetrics(req, resp, std::move(callback));

  } catch (const std::exception &e) {
    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time);
    if (isApiLoggingEnabled()) {
      PLOG_ERROR << "[API] POST /v1/securt/instance/{instanceId}/area/objectEnterExit - Exception: " << e.what() << " - " << duration.count() << "ms";
    }
    callback(createErrorResponse(500, "Internal server error", e.what()));
  } catch (...) {
    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time);
    if (isApiLoggingEnabled()) {
      PLOG_ERROR << "[API] POST /v1/securt/instance/{instanceId}/area/objectEnterExit - Unknown exception - " << duration.count() << "ms";
    }
    callback(createErrorResponse(500, "Internal server error",
                                 "Unknown error occurred"));
  }
}

// ========================================================================
// Standard Areas - PUT handlers (create with ID)
// ========================================================================

void AreaHandler::createCrossingAreaWithId(
    const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback) {
  try {
    if (!area_manager_) {
      callback(createErrorResponse(500, "Internal server error",
                                   "Area manager not initialized"));
      return;
    }

    std::string instanceId = extractInstanceId(req);
    std::string areaId = extractAreaId(req);

    if (instanceId.empty() || areaId.empty()) {
      callback(createErrorResponse(400, "Invalid request",
                                   "Instance ID and Area ID are required"));
      return;
    }

    auto json = req->getJsonObject();
    if (!json) {
      callback(createErrorResponse(400, "Invalid request",
                                   "Request body must be valid JSON"));
      return;
    }

    CrossingAreaWrite write = CrossingAreaWrite::fromJson(*json);
    std::string createdId =
        area_manager_->createCrossingAreaWithId(instanceId, areaId, write);

    if (createdId.empty()) {
      callback(createErrorResponse(409, "Conflict",
                                   "Area already exists or creation failed"));
      return;
    }

    auto resp = HttpResponse::newHttpResponse();
    resp->setStatusCode(k201Created);
    resp->addHeader("Access-Control-Allow-Origin", "*");
    resp->addHeader("Access-Control-Allow-Methods", "PUT, OPTIONS");
    resp->addHeader("Access-Control-Allow-Headers", "Content-Type");

    MetricsInterceptor::callWithMetrics(req, resp, std::move(callback));

  } catch (const std::exception &e) {
    callback(createErrorResponse(500, "Internal server error", e.what()));
  } catch (...) {
    callback(createErrorResponse(500, "Internal server error",
                                 "Unknown error occurred"));
  }
}

// Similar pattern for other PUT handlers - implementing key ones
void AreaHandler::createIntrusionAreaWithId(
    const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback) {
  try {
    if (!area_manager_) {
      callback(createErrorResponse(500, "Internal server error",
                                   "Area manager not initialized"));
      return;
    }

    std::string instanceId = extractInstanceId(req);
    std::string areaId = extractAreaId(req);

    if (instanceId.empty() || areaId.empty()) {
      callback(createErrorResponse(400, "Invalid request",
                                   "Instance ID and Area ID are required"));
      return;
    }

    auto json = req->getJsonObject();
    if (!json) {
      callback(createErrorResponse(400, "Invalid request",
                                   "Request body must be valid JSON"));
      return;
    }

    IntrusionAreaWrite write = IntrusionAreaWrite::fromJson(*json);
    std::string createdId =
        area_manager_->createIntrusionAreaWithId(instanceId, areaId, write);

    if (createdId.empty()) {
      callback(createErrorResponse(409, "Conflict",
                                   "Area already exists or creation failed"));
      return;
    }

    auto resp = HttpResponse::newHttpResponse();
    resp->setStatusCode(k201Created);
    resp->addHeader("Access-Control-Allow-Origin", "*");
    resp->addHeader("Access-Control-Allow-Methods", "PUT, OPTIONS");
    resp->addHeader("Access-Control-Allow-Headers", "Content-Type");

    MetricsInterceptor::callWithMetrics(req, resp, std::move(callback));

  } catch (const std::exception &e) {
    callback(createErrorResponse(500, "Internal server error", e.what()));
  } catch (...) {
    callback(createErrorResponse(500, "Internal server error",
                                 "Unknown error occurred"));
  }
}

// Implement remaining PUT handlers with similar pattern
// (Loitering, Crowding, Occupancy, CrowdEstimation, Dwelling, ArmedPerson,
// ObjectLeft, ObjectRemoved, FallenPerson)

void AreaHandler::createLoiteringAreaWithId(
    const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback) {
  try {
    if (!area_manager_) {
      callback(createErrorResponse(500, "Internal server error",
                                   "Area manager not initialized"));
      return;
    }
    std::string instanceId = extractInstanceId(req);
    std::string areaId = extractAreaId(req);
    if (instanceId.empty() || areaId.empty()) {
      callback(createErrorResponse(400, "Invalid request",
                                   "Instance ID and Area ID are required"));
      return;
    }
    auto json = req->getJsonObject();
    if (!json) {
      callback(createErrorResponse(400, "Invalid request",
                                   "Request body must be valid JSON"));
      return;
    }
    LoiteringAreaWrite write = LoiteringAreaWrite::fromJson(*json);
    std::string createdId =
        area_manager_->createLoiteringAreaWithId(instanceId, areaId, write);
    if (createdId.empty()) {
      callback(createErrorResponse(409, "Conflict",
                                   "Area already exists or creation failed"));
      return;
    }
    auto resp = HttpResponse::newHttpResponse();
    resp->setStatusCode(k201Created);
    resp->addHeader("Access-Control-Allow-Origin", "*");
    resp->addHeader("Access-Control-Allow-Methods", "PUT, OPTIONS");
    resp->addHeader("Access-Control-Allow-Headers", "Content-Type");
    MetricsInterceptor::callWithMetrics(req, resp, std::move(callback));
  } catch (const std::exception &e) {
    callback(createErrorResponse(500, "Internal server error", e.what()));
  } catch (...) {
    callback(createErrorResponse(500, "Internal server error",
                                 "Unknown error occurred"));
  }
}

void AreaHandler::createCrowdingAreaWithId(
    const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback) {
  try {
    if (!area_manager_) {
      callback(createErrorResponse(500, "Internal server error",
                                   "Area manager not initialized"));
      return;
    }
    std::string instanceId = extractInstanceId(req);
    std::string areaId = extractAreaId(req);
    if (instanceId.empty() || areaId.empty()) {
      callback(createErrorResponse(400, "Invalid request",
                                   "Instance ID and Area ID are required"));
      return;
    }
    auto json = req->getJsonObject();
    if (!json) {
      callback(createErrorResponse(400, "Invalid request",
                                   "Request body must be valid JSON"));
      return;
    }
    CrowdingAreaWrite write = CrowdingAreaWrite::fromJson(*json);
    std::string createdId =
        area_manager_->createCrowdingAreaWithId(instanceId, areaId, write);
    if (createdId.empty()) {
      callback(createErrorResponse(409, "Conflict",
                                   "Area already exists or creation failed"));
      return;
    }
    auto resp = HttpResponse::newHttpResponse();
    resp->setStatusCode(k201Created);
    resp->addHeader("Access-Control-Allow-Origin", "*");
    resp->addHeader("Access-Control-Allow-Methods", "PUT, OPTIONS");
    resp->addHeader("Access-Control-Allow-Headers", "Content-Type");
    MetricsInterceptor::callWithMetrics(req, resp, std::move(callback));
  } catch (const std::exception &e) {
    callback(createErrorResponse(500, "Internal server error", e.what()));
  } catch (...) {
    callback(createErrorResponse(500, "Internal server error",
                                 "Unknown error occurred"));
  }
}

void AreaHandler::createOccupancyAreaWithId(
    const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback) {
  try {
    if (!area_manager_) {
      callback(createErrorResponse(500, "Internal server error",
                                   "Area manager not initialized"));
      return;
    }
    std::string instanceId = extractInstanceId(req);
    std::string areaId = extractAreaId(req);
    if (instanceId.empty() || areaId.empty()) {
      callback(createErrorResponse(400, "Invalid request",
                                   "Instance ID and Area ID are required"));
      return;
    }
    auto json = req->getJsonObject();
    if (!json) {
      callback(createErrorResponse(400, "Invalid request",
                                   "Request body must be valid JSON"));
      return;
    }
    OccupancyAreaWrite write = OccupancyAreaWrite::fromJson(*json);
    std::string createdId =
        area_manager_->createOccupancyAreaWithId(instanceId, areaId, write);
    if (createdId.empty()) {
      callback(createErrorResponse(409, "Conflict",
                                   "Area already exists or creation failed"));
      return;
    }
    auto resp = HttpResponse::newHttpResponse();
    resp->setStatusCode(k201Created);
    resp->addHeader("Access-Control-Allow-Origin", "*");
    resp->addHeader("Access-Control-Allow-Methods", "PUT, OPTIONS");
    resp->addHeader("Access-Control-Allow-Headers", "Content-Type");
    MetricsInterceptor::callWithMetrics(req, resp, std::move(callback));
  } catch (const std::exception &e) {
    callback(createErrorResponse(500, "Internal server error", e.what()));
  } catch (...) {
    callback(createErrorResponse(500, "Internal server error",
                                 "Unknown error occurred"));
  }
}

void AreaHandler::createCrowdEstimationAreaWithId(
    const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback) {
  try {
    if (!area_manager_) {
      callback(createErrorResponse(500, "Internal server error",
                                   "Area manager not initialized"));
      return;
    }
    std::string instanceId = extractInstanceId(req);
    std::string areaId = extractAreaId(req);
    if (instanceId.empty() || areaId.empty()) {
      callback(createErrorResponse(400, "Invalid request",
                                   "Instance ID and Area ID are required"));
      return;
    }
    auto json = req->getJsonObject();
    if (!json) {
      callback(createErrorResponse(400, "Invalid request",
                                   "Request body must be valid JSON"));
      return;
    }
    CrowdEstimationAreaWrite write = CrowdEstimationAreaWrite::fromJson(*json);
    std::string createdId = area_manager_->createCrowdEstimationAreaWithId(
        instanceId, areaId, write);
    if (createdId.empty()) {
      callback(createErrorResponse(409, "Conflict",
                                   "Area already exists or creation failed"));
      return;
    }
    auto resp = HttpResponse::newHttpResponse();
    resp->setStatusCode(k201Created);
    resp->addHeader("Access-Control-Allow-Origin", "*");
    resp->addHeader("Access-Control-Allow-Methods", "PUT, OPTIONS");
    resp->addHeader("Access-Control-Allow-Headers", "Content-Type");
    MetricsInterceptor::callWithMetrics(req, resp, std::move(callback));
  } catch (const std::exception &e) {
    callback(createErrorResponse(500, "Internal server error", e.what()));
  } catch (...) {
    callback(createErrorResponse(500, "Internal server error",
                                 "Unknown error occurred"));
  }
}

void AreaHandler::createDwellingAreaWithId(
    const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback) {
  try {
    if (!area_manager_) {
      callback(createErrorResponse(500, "Internal server error",
                                   "Area manager not initialized"));
      return;
    }
    std::string instanceId = extractInstanceId(req);
    std::string areaId = extractAreaId(req);
    if (instanceId.empty() || areaId.empty()) {
      callback(createErrorResponse(400, "Invalid request",
                                   "Instance ID and Area ID are required"));
      return;
    }
    auto json = req->getJsonObject();
    if (!json) {
      callback(createErrorResponse(400, "Invalid request",
                                   "Request body must be valid JSON"));
      return;
    }
    DwellingAreaWrite write = DwellingAreaWrite::fromJson(*json);
    std::string createdId =
        area_manager_->createDwellingAreaWithId(instanceId, areaId, write);
    if (createdId.empty()) {
      callback(createErrorResponse(409, "Conflict",
                                   "Area already exists or creation failed"));
      return;
    }
    auto resp = HttpResponse::newHttpResponse();
    resp->setStatusCode(k201Created);
    resp->addHeader("Access-Control-Allow-Origin", "*");
    resp->addHeader("Access-Control-Allow-Methods", "PUT, OPTIONS");
    resp->addHeader("Access-Control-Allow-Headers", "Content-Type");
    MetricsInterceptor::callWithMetrics(req, resp, std::move(callback));
  } catch (const std::exception &e) {
    callback(createErrorResponse(500, "Internal server error", e.what()));
  } catch (...) {
    callback(createErrorResponse(500, "Internal server error",
                                 "Unknown error occurred"));
  }
}

void AreaHandler::createArmedPersonAreaWithId(
    const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback) {
  try {
    if (!area_manager_) {
      callback(createErrorResponse(500, "Internal server error",
                                   "Area manager not initialized"));
      return;
    }
    std::string instanceId = extractInstanceId(req);
    std::string areaId = extractAreaId(req);
    if (instanceId.empty() || areaId.empty()) {
      callback(createErrorResponse(400, "Invalid request",
                                   "Instance ID and Area ID are required"));
      return;
    }
    auto json = req->getJsonObject();
    if (!json) {
      callback(createErrorResponse(400, "Invalid request",
                                   "Request body must be valid JSON"));
      return;
    }
    ArmedPersonAreaWrite write = ArmedPersonAreaWrite::fromJson(*json);
    std::string createdId =
        area_manager_->createArmedPersonAreaWithId(instanceId, areaId, write);
    if (createdId.empty()) {
      callback(createErrorResponse(409, "Conflict",
                                   "Area already exists or creation failed"));
      return;
    }
    auto resp = HttpResponse::newHttpResponse();
    resp->setStatusCode(k201Created);
    resp->addHeader("Access-Control-Allow-Origin", "*");
    resp->addHeader("Access-Control-Allow-Methods", "PUT, OPTIONS");
    resp->addHeader("Access-Control-Allow-Headers", "Content-Type");
    MetricsInterceptor::callWithMetrics(req, resp, std::move(callback));
  } catch (const std::exception &e) {
    callback(createErrorResponse(500, "Internal server error", e.what()));
  } catch (...) {
    callback(createErrorResponse(500, "Internal server error",
                                 "Unknown error occurred"));
  }
}

void AreaHandler::createObjectLeftAreaWithId(
    const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback) {
  try {
    if (!area_manager_) {
      callback(createErrorResponse(500, "Internal server error",
                                   "Area manager not initialized"));
      return;
    }
    std::string instanceId = extractInstanceId(req);
    std::string areaId = extractAreaId(req);
    if (instanceId.empty() || areaId.empty()) {
      callback(createErrorResponse(400, "Invalid request",
                                   "Instance ID and Area ID are required"));
      return;
    }
    auto json = req->getJsonObject();
    if (!json) {
      callback(createErrorResponse(400, "Invalid request",
                                   "Request body must be valid JSON"));
      return;
    }
    ObjectLeftAreaWrite write = ObjectLeftAreaWrite::fromJson(*json);
    std::string createdId =
        area_manager_->createObjectLeftAreaWithId(instanceId, areaId, write);
    if (createdId.empty()) {
      callback(createErrorResponse(409, "Conflict",
                                   "Area already exists or creation failed"));
      return;
    }
    auto resp = HttpResponse::newHttpResponse();
    resp->setStatusCode(k201Created);
    resp->addHeader("Access-Control-Allow-Origin", "*");
    resp->addHeader("Access-Control-Allow-Methods", "PUT, OPTIONS");
    resp->addHeader("Access-Control-Allow-Headers", "Content-Type");
    MetricsInterceptor::callWithMetrics(req, resp, std::move(callback));
  } catch (const std::exception &e) {
    callback(createErrorResponse(500, "Internal server error", e.what()));
  } catch (...) {
    callback(createErrorResponse(500, "Internal server error",
                                 "Unknown error occurred"));
  }
}

void AreaHandler::createObjectRemovedAreaWithId(
    const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback) {
  try {
    if (!area_manager_) {
      callback(createErrorResponse(500, "Internal server error",
                                   "Area manager not initialized"));
      return;
    }
    std::string instanceId = extractInstanceId(req);
    std::string areaId = extractAreaId(req);
    if (instanceId.empty() || areaId.empty()) {
      callback(createErrorResponse(400, "Invalid request",
                                   "Instance ID and Area ID are required"));
      return;
    }
    auto json = req->getJsonObject();
    if (!json) {
      callback(createErrorResponse(400, "Invalid request",
                                   "Request body must be valid JSON"));
      return;
    }
    ObjectRemovedAreaWrite write = ObjectRemovedAreaWrite::fromJson(*json);
    std::string createdId =
        area_manager_->createObjectRemovedAreaWithId(instanceId, areaId, write);
    if (createdId.empty()) {
      callback(createErrorResponse(409, "Conflict",
                                   "Area already exists or creation failed"));
      return;
    }
    auto resp = HttpResponse::newHttpResponse();
    resp->setStatusCode(k201Created);
    resp->addHeader("Access-Control-Allow-Origin", "*");
    resp->addHeader("Access-Control-Allow-Methods", "PUT, OPTIONS");
    resp->addHeader("Access-Control-Allow-Headers", "Content-Type");
    MetricsInterceptor::callWithMetrics(req, resp, std::move(callback));
  } catch (const std::exception &e) {
    callback(createErrorResponse(500, "Internal server error", e.what()));
  } catch (...) {
    callback(createErrorResponse(500, "Internal server error",
                                 "Unknown error occurred"));
  }
}

void AreaHandler::createFallenPersonAreaWithId(
    const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback) {
  try {
    if (!area_manager_) {
      callback(createErrorResponse(500, "Internal server error",
                                   "Area manager not initialized"));
      return;
    }
    std::string instanceId = extractInstanceId(req);
    std::string areaId = extractAreaId(req);
    if (instanceId.empty() || areaId.empty()) {
      callback(createErrorResponse(400, "Invalid request",
                                   "Instance ID and Area ID are required"));
      return;
    }
    auto json = req->getJsonObject();
    if (!json) {
      callback(createErrorResponse(400, "Invalid request",
                                   "Request body must be valid JSON"));
      return;
    }
    FallenPersonAreaWrite write = FallenPersonAreaWrite::fromJson(*json);
    std::string createdId =
        area_manager_->createFallenPersonAreaWithId(instanceId, areaId, write);
    if (createdId.empty()) {
      callback(createErrorResponse(409, "Conflict",
                                   "Area already exists or creation failed"));
      return;
    }
    auto resp = HttpResponse::newHttpResponse();
    resp->setStatusCode(k201Created);
    resp->addHeader("Access-Control-Allow-Origin", "*");
    resp->addHeader("Access-Control-Allow-Methods", "PUT, OPTIONS");
    resp->addHeader("Access-Control-Allow-Headers", "Content-Type");
    MetricsInterceptor::callWithMetrics(req, resp, std::move(callback));
  } catch (const std::exception &e) {
    callback(createErrorResponse(500, "Internal server error", e.what()));
  } catch (...) {
    callback(createErrorResponse(500, "Internal server error",
                                 "Unknown error occurred"));
  }
}

void AreaHandler::createObjectEnterExitAreaWithId(
    const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback) {
  auto start_time = std::chrono::steady_clock::now();

  if (isApiLoggingEnabled()) {
    PLOG_INFO << "[API] PUT /v1/securt/instance/{instanceId}/area/objectEnterExit/{areaId} - Create object enter/exit area with ID";
  }

  try {
    if (!area_manager_) {
      if (isApiLoggingEnabled()) {
        PLOG_ERROR << "[API] PUT /v1/securt/instance/{instanceId}/area/objectEnterExit/{areaId} - Error: Area manager not initialized";
      }
      callback(createErrorResponse(500, "Internal server error",
                                   "Area manager not initialized"));
      return;
    }

    std::string instanceId = extractInstanceId(req);
    std::string areaId = extractAreaId(req);

    if (instanceId.empty() || areaId.empty()) {
      if (isApiLoggingEnabled()) {
        PLOG_WARNING << "[API] PUT /v1/securt/instance/{instanceId}/area/objectEnterExit/{areaId} - Error: Instance ID and Area ID are required";
      }
      callback(createErrorResponse(400, "Invalid request",
                                   "Instance ID and Area ID are required"));
      return;
    }

    if (isApiLoggingEnabled()) {
      PLOG_DEBUG << "[API] PUT /v1/securt/instance/" << instanceId << "/area/objectEnterExit/" << areaId;
    }

    auto json = req->getJsonObject();
    if (!json) {
      if (isApiLoggingEnabled()) {
        PLOG_WARNING << "[API] PUT /v1/securt/instance/" << instanceId << "/area/objectEnterExit/" << areaId << " - Error: Invalid JSON body";
      }
      callback(createErrorResponse(400, "Invalid request",
                                   "Request body must be valid JSON"));
      return;
    }

    if (isApiLoggingEnabled()) {
      Json::StreamWriterBuilder builder;
      builder["indentation"] = "";
      std::string jsonStr = Json::writeString(builder, *json);
      PLOG_DEBUG << "[API] PUT /v1/securt/instance/" << instanceId << "/area/objectEnterExit/" << areaId << " - Request body: " << jsonStr;
    }

    ObjectEnterExitAreaWrite write = ObjectEnterExitAreaWrite::fromJson(*json);
    std::string createdId =
        area_manager_->createObjectEnterExitAreaWithId(instanceId, areaId, write);

    if (createdId.empty()) {
      auto end_time = std::chrono::steady_clock::now();
      auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
          end_time - start_time);
      if (isApiLoggingEnabled()) {
        PLOG_WARNING << "[API] PUT /v1/securt/instance/" << instanceId << "/area/objectEnterExit/" << areaId << " - Failed to create area - " << duration.count() << "ms";
      }
      callback(createErrorResponse(409, "Conflict",
                                   "Area already exists or creation failed"));
      return;
    }

    Json::Value response;
    response["areaId"] = createdId;

    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time);

    if (isApiLoggingEnabled()) {
      PLOG_INFO << "[API] PUT /v1/securt/instance/" << instanceId << "/area/objectEnterExit/" << areaId << " - Success: Created area " << createdId << " - " << duration.count() << "ms";
    }

    auto resp = HttpResponse::newHttpJsonResponse(response);
    resp->setStatusCode(k201Created);
    resp->addHeader("Access-Control-Allow-Origin", "*");
    resp->addHeader("Access-Control-Allow-Methods", "PUT, OPTIONS");
    resp->addHeader("Access-Control-Allow-Headers", "Content-Type");

    MetricsInterceptor::callWithMetrics(req, resp, std::move(callback));

  } catch (const std::exception &e) {
    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time);
    if (isApiLoggingEnabled()) {
      PLOG_ERROR << "[API] PUT /v1/securt/instance/{instanceId}/area/objectEnterExit/{areaId} - Exception: " << e.what() << " - " << duration.count() << "ms";
    }
    callback(createErrorResponse(500, "Internal server error", e.what()));
  } catch (...) {
    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time);
    if (isApiLoggingEnabled()) {
      PLOG_ERROR << "[API] PUT /v1/securt/instance/{instanceId}/area/objectEnterExit/{areaId} - Unknown exception - " << duration.count() << "ms";
    }
    callback(createErrorResponse(500, "Internal server error",
                                 "Unknown error occurred"));
  }
}

// ========================================================================
// Experimental Areas
// ========================================================================

void AreaHandler::createVehicleGuardArea(
    const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback) {
  try {
    if (!area_manager_) {
      callback(createErrorResponse(500, "Internal server error",
                                   "Area manager not initialized"));
      return;
    }
    std::string instanceId = extractInstanceId(req);
    if (instanceId.empty()) {
      callback(createErrorResponse(400, "Invalid request",
                                   "Instance ID is required"));
      return;
    }
    auto json = req->getJsonObject();
    if (!json) {
      callback(createErrorResponse(400, "Invalid request",
                                   "Request body must be valid JSON"));
      return;
    }
    VehicleGuardAreaWrite write = VehicleGuardAreaWrite::fromJson(*json);
    std::string areaId = area_manager_->createVehicleGuardArea(instanceId, write);
    if (areaId.empty()) {
      callback(createErrorResponse(400, "Invalid request",
                                   "Failed to create area"));
      return;
    }
    Json::Value response;
    response["areaId"] = areaId;
    auto resp = HttpResponse::newHttpJsonResponse(response);
    resp->setStatusCode(k201Created);
    resp->addHeader("Access-Control-Allow-Origin", "*");
    resp->addHeader("Access-Control-Allow-Methods", "POST, OPTIONS");
    resp->addHeader("Access-Control-Allow-Headers", "Content-Type");
    MetricsInterceptor::callWithMetrics(req, resp, std::move(callback));
  } catch (const std::exception &e) {
    callback(createErrorResponse(500, "Internal server error", e.what()));
  } catch (...) {
    callback(createErrorResponse(500, "Internal server error",
                                 "Unknown error occurred"));
  }
}

void AreaHandler::createFaceCoveredArea(
    const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback) {
  try {
    if (!area_manager_) {
      callback(createErrorResponse(500, "Internal server error",
                                   "Area manager not initialized"));
      return;
    }
    std::string instanceId = extractInstanceId(req);
    if (instanceId.empty()) {
      callback(createErrorResponse(400, "Invalid request",
                                   "Instance ID is required"));
      return;
    }
    auto json = req->getJsonObject();
    if (!json) {
      callback(createErrorResponse(400, "Invalid request",
                                   "Request body must be valid JSON"));
      return;
    }
    FaceCoveredAreaWrite write = FaceCoveredAreaWrite::fromJson(*json);
    std::string areaId = area_manager_->createFaceCoveredArea(instanceId, write);
    if (areaId.empty()) {
      callback(createErrorResponse(400, "Invalid request",
                                   "Failed to create area"));
      return;
    }
    Json::Value response;
    response["areaId"] = areaId;
    auto resp = HttpResponse::newHttpJsonResponse(response);
    resp->setStatusCode(k201Created);
    resp->addHeader("Access-Control-Allow-Origin", "*");
    resp->addHeader("Access-Control-Allow-Methods", "POST, OPTIONS");
    resp->addHeader("Access-Control-Allow-Headers", "Content-Type");
    MetricsInterceptor::callWithMetrics(req, resp, std::move(callback));
  } catch (const std::exception &e) {
    callback(createErrorResponse(500, "Internal server error", e.what()));
  } catch (...) {
    callback(createErrorResponse(500, "Internal server error",
                                 "Unknown error occurred"));
  }
}

void AreaHandler::createVehicleGuardAreaWithId(
    const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback) {
  try {
    if (!area_manager_) {
      callback(createErrorResponse(500, "Internal server error",
                                   "Area manager not initialized"));
      return;
    }
    std::string instanceId = extractInstanceId(req);
    std::string areaId = extractAreaId(req);
    if (instanceId.empty() || areaId.empty()) {
      callback(createErrorResponse(400, "Invalid request",
                                   "Instance ID and Area ID are required"));
      return;
    }
    auto json = req->getJsonObject();
    if (!json) {
      callback(createErrorResponse(400, "Invalid request",
                                   "Request body must be valid JSON"));
      return;
    }
    VehicleGuardAreaWrite write = VehicleGuardAreaWrite::fromJson(*json);
    std::string createdId =
        area_manager_->createVehicleGuardAreaWithId(instanceId, areaId, write);
    if (createdId.empty()) {
      callback(createErrorResponse(409, "Conflict",
                                   "Area already exists or creation failed"));
      return;
    }
    auto resp = HttpResponse::newHttpResponse();
    resp->setStatusCode(k201Created);
    resp->addHeader("Access-Control-Allow-Origin", "*");
    resp->addHeader("Access-Control-Allow-Methods", "PUT, OPTIONS");
    resp->addHeader("Access-Control-Allow-Headers", "Content-Type");
    MetricsInterceptor::callWithMetrics(req, resp, std::move(callback));
  } catch (const std::exception &e) {
    callback(createErrorResponse(500, "Internal server error", e.what()));
  } catch (...) {
    callback(createErrorResponse(500, "Internal server error",
                                 "Unknown error occurred"));
  }
}

void AreaHandler::createFaceCoveredAreaWithId(
    const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback) {
  try {
    if (!area_manager_) {
      callback(createErrorResponse(500, "Internal server error",
                                   "Area manager not initialized"));
      return;
    }
    std::string instanceId = extractInstanceId(req);
    std::string areaId = extractAreaId(req);
    if (instanceId.empty() || areaId.empty()) {
      callback(createErrorResponse(400, "Invalid request",
                                   "Instance ID and Area ID are required"));
      return;
    }
    auto json = req->getJsonObject();
    if (!json) {
      callback(createErrorResponse(400, "Invalid request",
                                   "Request body must be valid JSON"));
      return;
    }
    FaceCoveredAreaWrite write = FaceCoveredAreaWrite::fromJson(*json);
    std::string createdId =
        area_manager_->createFaceCoveredAreaWithId(instanceId, areaId, write);
    if (createdId.empty()) {
      callback(createErrorResponse(409, "Conflict",
                                   "Area already exists or creation failed"));
      return;
    }
    auto resp = HttpResponse::newHttpResponse();
    resp->setStatusCode(k201Created);
    resp->addHeader("Access-Control-Allow-Origin", "*");
    resp->addHeader("Access-Control-Allow-Methods", "PUT, OPTIONS");
    resp->addHeader("Access-Control-Allow-Headers", "Content-Type");
    MetricsInterceptor::callWithMetrics(req, resp, std::move(callback));
  } catch (const std::exception &e) {
    callback(createErrorResponse(500, "Internal server error", e.what()));
  } catch (...) {
    callback(createErrorResponse(500, "Internal server error",
                                 "Unknown error occurred"));
  }
}

// ========================================================================
// Common handlers
// ========================================================================

void AreaHandler::getAllAreas(
    const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback) {
  auto start_time = std::chrono::steady_clock::now();

  if (isApiLoggingEnabled()) {
    PLOG_INFO << "[API] GET /v1/securt/instance/{instanceId}/areas - Get all areas";
    PLOG_DEBUG << "[API] Request from: " << req->getPeerAddr().toIpPort();
    PLOG_DEBUG << "[API] Request path: " << req->getPath();
  }

  try {
    if (!area_manager_) {
      if (isApiLoggingEnabled()) {
        PLOG_ERROR << "[API] GET /v1/securt/instance/{instanceId}/areas - Error: Area manager not initialized";
      }
      callback(createErrorResponse(500, "Internal server error",
                                   "Area manager not initialized"));
      return;
    }

    std::string instanceId = extractInstanceId(req);
    if (instanceId.empty()) {
      if (isApiLoggingEnabled()) {
        PLOG_WARNING << "[API] GET /v1/securt/instance/{instanceId}/areas - Error: Instance ID is required";
      }
      callback(createErrorResponse(400, "Invalid request",
                                   "Instance ID is required"));
      return;
    }

    if (isApiLoggingEnabled()) {
      PLOG_DEBUG << "[API] GET /v1/securt/instance/" << instanceId << "/areas - Instance ID: " << instanceId;
      PLOG_DEBUG << "[API] GET /v1/securt/instance/" << instanceId << "/areas - Calling area_manager_->getAllAreas()";
    }

    auto areasMap = area_manager_->getAllAreas(instanceId);

    std::cerr << "[AreaHandler::getAllAreas] DEBUG: Received areasMap from area_manager with " << areasMap.size() << " keys" << std::endl;
    for (const auto &[type, areas] : areasMap) {
      std::cerr << "[AreaHandler::getAllAreas] DEBUG:   - Key: \"" << type << "\" has " << areas.size() << " areas" << std::endl;
    }

    // Convert to JSON response
    Json::Value response;
    size_t totalAreas = 0;
    for (const auto &[type, areas] : areasMap) {
      std::cerr << "[AreaHandler::getAllAreas] DEBUG: Processing type key: \"" << type << "\" with " << areas.size() << " areas" << std::endl;
      Json::Value areasArray(Json::arrayValue);
      for (const auto &area : areas) {
        if (area.isMember("id") && area.isMember("name")) {
          std::cerr << "[AreaHandler::getAllAreas] DEBUG:     - Adding area ID: " << area["id"].asString() 
                    << ", Name: " << area["name"].asString() << " to key: \"" << type << "\"" << std::endl;
        }
        areasArray.append(area);
      }
      response[type] = areasArray;
      totalAreas += areas.size();
      std::cerr << "[AreaHandler::getAllAreas] DEBUG: Added " << areasArray.size() << " areas to response with key: \"" << type << "\"" << std::endl;
    }

    std::cerr << "[AreaHandler::getAllAreas] DEBUG: Final response JSON has " << response.size() << " keys: ";
    for (const auto &key : response.getMemberNames()) {
      std::cerr << "\"" << key << "\"(" << response[key].size() << " areas) ";
    }
    std::cerr << std::endl;

    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time);

    if (isApiLoggingEnabled()) {
      PLOG_INFO << "[API] GET /v1/securt/instance/" << instanceId << "/areas - Success: Found " << totalAreas << " areas - " << duration.count() << "ms";
      PLOG_DEBUG << "[API] GET /v1/securt/instance/" << instanceId << "/areas - Area types: " << areasMap.size();
    }

    callback(createSuccessResponse(response));

  } catch (const std::exception &e) {
    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time);
    if (isApiLoggingEnabled()) {
      PLOG_ERROR << "[API] GET /v1/securt/instance/{instanceId}/areas - Exception: " << e.what() << " - " << duration.count() << "ms";
    }
    callback(createErrorResponse(500, "Internal server error", e.what()));
  } catch (...) {
    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time);
    if (isApiLoggingEnabled()) {
      PLOG_ERROR << "[API] GET /v1/securt/instance/{instanceId}/areas - Unknown exception - " << duration.count() << "ms";
    }
    callback(createErrorResponse(500, "Internal server error",
                                 "Unknown error occurred"));
  }
}

void AreaHandler::deleteAllAreas(
    const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback) {
  auto start_time = std::chrono::steady_clock::now();

  if (isApiLoggingEnabled()) {
    PLOG_INFO << "[API] DELETE /v1/securt/instance/{instanceId}/areas - Delete all areas";
    PLOG_DEBUG << "[API] Request from: " << req->getPeerAddr().toIpPort();
    PLOG_DEBUG << "[API] Request path: " << req->getPath();
  }

  try {
    if (!area_manager_) {
      if (isApiLoggingEnabled()) {
        PLOG_ERROR << "[API] DELETE /v1/securt/instance/{instanceId}/areas - Error: Area manager not initialized";
      }
      callback(createErrorResponse(500, "Internal server error",
                                   "Area manager not initialized"));
      return;
    }

    std::string instanceId = extractInstanceId(req);
    if (instanceId.empty()) {
      if (isApiLoggingEnabled()) {
        PLOG_WARNING << "[API] DELETE /v1/securt/instance/{instanceId}/areas - Error: Instance ID is required";
      }
      callback(createErrorResponse(400, "Invalid request",
                                   "Instance ID is required"));
      return;
    }

    if (isApiLoggingEnabled()) {
      PLOG_DEBUG << "[API] DELETE /v1/securt/instance/" << instanceId << "/areas - Instance ID: " << instanceId;
      PLOG_DEBUG << "[API] DELETE /v1/securt/instance/" << instanceId << "/areas - Calling area_manager_->deleteAllAreas()";
    }

    bool success = area_manager_->deleteAllAreas(instanceId);
    if (!success) {
      auto end_time = std::chrono::steady_clock::now();
      auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
          end_time - start_time);
      if (isApiLoggingEnabled()) {
        PLOG_WARNING << "[API] DELETE /v1/securt/instance/" << instanceId << "/areas - Instance not found - " << duration.count() << "ms";
      }
      callback(createErrorResponse(404, "Not Found",
                                   "Instance not found"));
      return;
    }

    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time);

    if (isApiLoggingEnabled()) {
      PLOG_INFO << "[API] DELETE /v1/securt/instance/" << instanceId << "/areas - Success: Deleted all areas - " << duration.count() << "ms";
    }

    auto resp = HttpResponse::newHttpResponse();
    resp->setStatusCode(k204NoContent);
    resp->addHeader("Access-Control-Allow-Origin", "*");
    resp->addHeader("Access-Control-Allow-Methods", "DELETE, OPTIONS");
    resp->addHeader("Access-Control-Allow-Headers", "Content-Type");

    MetricsInterceptor::callWithMetrics(req, resp, std::move(callback));

  } catch (const std::exception &e) {
    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time);
    if (isApiLoggingEnabled()) {
      PLOG_ERROR << "[API] DELETE /v1/securt/instance/{instanceId}/areas - Exception: " << e.what() << " - " << duration.count() << "ms";
    }
    callback(createErrorResponse(500, "Internal server error", e.what()));
  } catch (...) {
    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time);
    if (isApiLoggingEnabled()) {
      PLOG_ERROR << "[API] DELETE /v1/securt/instance/{instanceId}/areas - Unknown exception - " << duration.count() << "ms";
    }
    callback(createErrorResponse(500, "Internal server error",
                                 "Unknown error occurred"));
  }
}

void AreaHandler::deleteArea(
    const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback) {
  auto start_time = std::chrono::steady_clock::now();

  if (isApiLoggingEnabled()) {
    PLOG_INFO << "[API] DELETE /v1/securt/instance/{instanceId}/area/{areaId} - Delete area";
    PLOG_DEBUG << "[API] Request from: " << req->getPeerAddr().toIpPort();
    PLOG_DEBUG << "[API] Request path: " << req->getPath();
  }

  try {
    if (!area_manager_) {
      if (isApiLoggingEnabled()) {
        PLOG_ERROR << "[API] DELETE /v1/securt/instance/{instanceId}/area/{areaId} - Error: Area manager not initialized";
      }
      callback(createErrorResponse(500, "Internal server error",
                                   "Area manager not initialized"));
      return;
    }

    std::string instanceId = extractInstanceId(req);
    std::string areaId = extractAreaId(req);

    if (instanceId.empty() || areaId.empty()) {
      if (isApiLoggingEnabled()) {
        PLOG_WARNING << "[API] DELETE /v1/securt/instance/{instanceId}/area/{areaId} - Error: Instance ID and Area ID are required";
        PLOG_DEBUG << "[API] Instance ID: " << (instanceId.empty() ? "empty" : instanceId)
                   << ", Area ID: " << (areaId.empty() ? "empty" : areaId);
      }
      callback(createErrorResponse(400, "Invalid request",
                                   "Instance ID and Area ID are required"));
      return;
    }

    if (isApiLoggingEnabled()) {
      PLOG_DEBUG << "[API] DELETE /v1/securt/instance/" << instanceId << "/area/" << areaId
                 << " - Instance ID: " << instanceId << ", Area ID: " << areaId;
      PLOG_DEBUG << "[API] DELETE /v1/securt/instance/" << instanceId << "/area/" << areaId
                 << " - Calling area_manager_->deleteArea()";
    }

    bool success = area_manager_->deleteArea(instanceId, areaId);
    if (!success) {
      auto end_time = std::chrono::steady_clock::now();
      auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
          end_time - start_time);
      if (isApiLoggingEnabled()) {
        PLOG_WARNING << "[API] DELETE /v1/securt/instance/" << instanceId << "/area/" << areaId
                     << " - Area not found - " << duration.count() << "ms";
      }
      callback(createErrorResponse(404, "Not Found",
                                   "Area not found"));
      return;
    }

    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time);

    if (isApiLoggingEnabled()) {
      PLOG_INFO << "[API] DELETE /v1/securt/instance/" << instanceId << "/area/" << areaId
                << " - Success: Deleted area - " << duration.count() << "ms";
    }

    auto resp = HttpResponse::newHttpResponse();
    resp->setStatusCode(k204NoContent);
    resp->addHeader("Access-Control-Allow-Origin", "*");
    resp->addHeader("Access-Control-Allow-Methods", "DELETE, OPTIONS");
    resp->addHeader("Access-Control-Allow-Headers", "Content-Type");

    MetricsInterceptor::callWithMetrics(req, resp, std::move(callback));

  } catch (const std::exception &e) {
    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time);
    if (isApiLoggingEnabled()) {
      PLOG_ERROR << "[API] DELETE /v1/securt/instance/{instanceId}/area/{areaId} - Exception: " << e.what() << " - " << duration.count() << "ms";
    }
    callback(createErrorResponse(500, "Internal server error", e.what()));
  } catch (...) {
    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time);
    if (isApiLoggingEnabled()) {
      PLOG_ERROR << "[API] DELETE /v1/securt/instance/{instanceId}/area/{areaId} - Unknown exception - " << duration.count() << "ms";
    }
    callback(createErrorResponse(500, "Internal server error",
                                 "Unknown error occurred"));
  }
}

void AreaHandler::handleOptions(
    const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback) {
  MetricsInterceptor::setHandlerStartTime(req);

  auto resp = HttpResponse::newHttpResponse();
  resp->setStatusCode(k200OK);
  resp->addHeader("Access-Control-Allow-Origin", "*");
  resp->addHeader("Access-Control-Allow-Methods",
                   "GET, POST, PUT, DELETE, OPTIONS");
  resp->addHeader("Access-Control-Allow-Headers", "Content-Type");
  resp->addHeader("Access-Control-Max-Age", "3600");

  MetricsInterceptor::callWithMetrics(req, resp, std::move(callback));
}

// ========================================================================
// Helper methods
// ========================================================================

std::string AreaHandler::extractInstanceId(const HttpRequestPtr &req) const {
  std::string instanceId = req->getParameter("instanceId");
  if (instanceId.empty()) {
    std::string path = req->getPath();
    std::cerr << "[AreaHandler::extractInstanceId] DEBUG: Path: " << path << std::endl;
    
    size_t instancePos = path.find("/securt/instance/");
    if (instancePos != std::string::npos) {
      size_t start = instancePos + 17; // length of "/securt/instance/"
      size_t end = path.find("/", start);
      if (end == std::string::npos) {
        end = path.length();
      }
      instanceId = path.substr(start, end - start);
      std::cerr << "[AreaHandler::extractInstanceId] DEBUG: Extracted instanceId: " << instanceId << std::endl;
    } else {
      std::cerr << "[AreaHandler::extractInstanceId] DEBUG: /securt/instance/ not found in path" << std::endl;
    }
  } else {
    std::cerr << "[AreaHandler::extractInstanceId] DEBUG: Got instanceId from parameter: " << instanceId << std::endl;
  }
  return instanceId;
}

std::string AreaHandler::extractAreaId(const HttpRequestPtr &req) const {
  std::string areaId = req->getParameter("areaId");
  if (areaId.empty()) {
    std::string path = req->getPath();
    std::cerr << "[AreaHandler::extractAreaId] DEBUG: Path: " << path << std::endl;
    
    // Try to find area ID
    // Pattern 1: /v1/securt/instance/{instanceId}/area/{areaId} (for DELETE)
    // Pattern 2: /v1/securt/instance/{instanceId}/area/{areaType}/{areaId} (for PUT with ID)
    size_t areaPos = path.find("/area/");
    if (areaPos != std::string::npos) {
      size_t start = areaPos + 6; // length of "/area/"
      std::cerr << "[AreaHandler::extractAreaId] DEBUG: Found /area/ at position " << areaPos << ", start = " << start << std::endl;
      
      size_t nextSlash = path.find("/", start);
      if (nextSlash != std::string::npos) {
        // Pattern 2: /area/{areaType}/{areaId}
        size_t areaIdStart = nextSlash + 1;
        size_t areaIdEnd = path.find("/", areaIdStart);
        if (areaIdEnd == std::string::npos) {
          areaIdEnd = path.length();
        }
        areaId = path.substr(areaIdStart, areaIdEnd - areaIdStart);
        std::cerr << "[AreaHandler::extractAreaId] DEBUG: Pattern 2 detected, extracted areaId: " << areaId << std::endl;
      } else {
        // Pattern 1: /area/{areaId} - areaId is directly after /area/
        areaId = path.substr(start);
        std::cerr << "[AreaHandler::extractAreaId] DEBUG: Pattern 1 detected, extracted areaId: " << areaId << std::endl;
      }
    } else {
      std::cerr << "[AreaHandler::extractAreaId] DEBUG: /area/ not found in path" << std::endl;
    }
  } else {
    std::cerr << "[AreaHandler::extractAreaId] DEBUG: Got areaId from parameter: " << areaId << std::endl;
  }
  return areaId;
}

HttpResponsePtr AreaHandler::createErrorResponse(int statusCode,
                                                   const std::string &error,
                                                   const std::string &message) const {
  Json::Value json;
  json["error"] = error;
  if (!message.empty()) {
    json["message"] = message;
  }

  auto resp = HttpResponse::newHttpJsonResponse(json);
  resp->setStatusCode(static_cast<drogon::HttpStatusCode>(statusCode));

  resp->addHeader("Access-Control-Allow-Origin", "*");
  resp->addHeader("Access-Control-Allow-Methods",
                   "GET, POST, PUT, DELETE, OPTIONS");
  resp->addHeader("Access-Control-Allow-Headers", "Content-Type");

  return resp;
}

HttpResponsePtr AreaHandler::createSuccessResponse(const Json::Value &data,
                                                    int statusCode) const {
  auto resp = HttpResponse::newHttpJsonResponse(data);
  resp->setStatusCode(static_cast<drogon::HttpStatusCode>(statusCode));

  resp->addHeader("Access-Control-Allow-Origin", "*");
  resp->addHeader("Access-Control-Allow-Methods",
                   "GET, POST, PUT, DELETE, OPTIONS");
  resp->addHeader("Access-Control-Allow-Headers", "Content-Type");

  return resp;
}

