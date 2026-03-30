#include "api/solution_handler.h"
#include "core/logger.h"
#include "core/logging_flags.h"
#include "core/metrics_interceptor.h"
#include "core/node_pool_manager.h"
#include "core/node_template_registry.h"
#include "models/solution_config.h"
#include "solutions/solution_registry.h"
#include "solutions/solution_storage.h"
#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstring>
#include <drogon/HttpResponse.h>
#include <filesystem>
#include <fstream>
#include <map>
#include <regex>
#include <set>
#include <unistd.h>

void SolutionHandler::createSolution(
    const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback) {
  // Set handler start time for accurate metrics
  MetricsInterceptor::setHandlerStartTime(req);

  auto start_time = std::chrono::steady_clock::now();

  if (isApiLoggingEnabled()) {
    PLOG_INFO << "[API] POST /v1/core/solution - Create solution";
    PLOG_DEBUG << "[API] Request from: " << req->getPeerAddr().toIpPort();
  }

  try {
    if (!solution_registry_ || !solution_storage_) {
      if (isApiLoggingEnabled()) {
        PLOG_ERROR << "[API] POST /v1/core/solution - Error: Solution "
                      "registry or storage not initialized";
      }
      callback(
          createErrorResponse(500, "Internal server error",
                              "Solution registry or storage not initialized"));
      return;
    }

    // Parse JSON body
    auto json = req->getJsonObject();
    if (!json) {
      if (isApiLoggingEnabled()) {
        PLOG_WARNING
            << "[API] POST /v1/core/solution - Error: Invalid JSON body";
      }
      callback(createErrorResponse(400, "Invalid request",
                                   "Request body must be valid JSON"));
      return;
    }

    // Parse solution config
    std::string parseError;
    auto optConfig = parseSolutionConfig(*json, parseError);
    if (!optConfig.has_value()) {
      if (isApiLoggingEnabled()) {
        PLOG_WARNING << "[API] POST /v1/core/solution - Parse error: "
                     << parseError;
      }
      callback(createErrorResponse(400, "Invalid request", parseError));
      return;
    }

    SolutionConfig config = optConfig.value();

    // Ensure isDefault is false for custom solutions
    config.isDefault = false;

    // Check if solution already exists
    if (solution_registry_->hasSolution(config.solutionId)) {
      // Check if it's a default solution - cannot override default solutions
      if (solution_registry_->isDefaultSolution(config.solutionId)) {
        if (isApiLoggingEnabled()) {
          PLOG_WARNING << "[API] POST /v1/core/solution - Error: Cannot "
                          "override default solution: "
                       << config.solutionId;
        }
        callback(createErrorResponse(
            403, "Forbidden",
            "Cannot create solution with ID '" + config.solutionId +
                "': This ID is reserved for a default system solution. Please "
                "use a different solution ID."));
        return;
      }

      // Solution exists and is not default - return conflict
      if (isApiLoggingEnabled()) {
        PLOG_WARNING << "[API] POST /v1/core/solution - Error: Solution "
                        "already exists: "
                     << config.solutionId;
      }
      callback(createErrorResponse(
          409, "Conflict",
          "Solution with ID '" + config.solutionId +
              "' already exists. Use PUT /v1/core/solution/" +
              config.solutionId + " to update it."));
      return;
    }

    // Validate node types in solution - ensure all node types are supported
    auto &nodePool = NodePoolManager::getInstance();
    auto allTemplates = nodePool.getAllTemplates();
    std::set<std::string> supportedNodeTypes;
    for (const auto &template_ : allTemplates) {
      supportedNodeTypes.insert(template_.nodeType);
    }

    std::vector<std::string> unsupportedNodeTypes;
    for (const auto &nodeConfig : config.pipeline) {
      if (supportedNodeTypes.find(nodeConfig.nodeType) ==
          supportedNodeTypes.end()) {
        unsupportedNodeTypes.push_back(nodeConfig.nodeType);
      }
    }

    if (!unsupportedNodeTypes.empty()) {
      std::string errorMsg = "Solution contains unsupported node types: ";
      for (size_t i = 0; i < unsupportedNodeTypes.size(); ++i) {
        errorMsg += unsupportedNodeTypes[i];
        if (i < unsupportedNodeTypes.size() - 1) {
          errorMsg += ", ";
        }
      }
      if (isApiLoggingEnabled()) {
        PLOG_WARNING << "[API] POST /v1/core/solution - Error: " << errorMsg;
      }
      callback(createErrorResponse(400, "Invalid request", errorMsg));
      return;
    }

    // Register solution
    solution_registry_->registerSolution(config);

    // Create nodes for node types in this custom solution (if they don't exist)
    // Note: These are user-created nodes, not default nodes
    size_t nodesCreated = nodePool.createNodesFromSolution(config);
    if (nodesCreated > 0) {
      std::cerr << "[SolutionHandler] Created " << nodesCreated
                << " nodes for custom solution: " << config.solutionId
                << std::endl;
    }

    // Save to storage
    std::cerr << "[SolutionHandler] Attempting to save solution to storage: "
              << config.solutionId << std::endl;
    if (!solution_storage_->saveSolution(config)) {
      std::cerr
          << "[SolutionHandler] Warning: Failed to save solution to storage: "
          << config.solutionId << std::endl;
    } else {
      std::cerr
          << "[SolutionHandler] ✓ Solution saved successfully to storage: "
          << config.solutionId << std::endl;
    }

    Json::Value response = solutionConfigToJson(config);
    response["message"] = "Solution created successfully";

    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time);

    if (isApiLoggingEnabled()) {
      PLOG_INFO << "[API] POST /v1/core/solution - Success: Created solution "
                << config.solutionId << " (" << config.solutionName << ") - "
                << duration.count() << "ms";
    }

    auto resp = HttpResponse::newHttpJsonResponse(response);
    resp->setStatusCode(k201Created);
    resp->addHeader("Access-Control-Allow-Origin", "*");
    resp->addHeader("Access-Control-Allow-Methods", "POST, OPTIONS");
    resp->addHeader("Access-Control-Allow-Headers",
                    "Content-Type, Authorization");

    // Record metrics and call callback
    MetricsInterceptor::callWithMetrics(req, resp, std::move(callback));

  } catch (const std::exception &e) {
    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time);
    if (isApiLoggingEnabled()) {
      PLOG_ERROR << "[API] POST /v1/core/solution - Exception: " << e.what()
                 << " - " << duration.count() << "ms";
    }
    std::cerr << "[SolutionHandler] Exception: " << e.what() << std::endl;
    callback(createErrorResponse(500, "Internal server error", e.what()));
  } catch (...) {
    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time);
    if (isApiLoggingEnabled()) {
      PLOG_ERROR << "[API] POST /v1/core/solution - Unknown exception - "
                 << duration.count() << "ms";
    }
    std::cerr << "[SolutionHandler] Unknown exception" << std::endl;
    callback(createErrorResponse(500, "Internal server error",
                                 "Unknown error occurred"));
  }
}

void SolutionHandler::updateSolution(
    const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback) {
  // Set handler start time for accurate metrics
  MetricsInterceptor::setHandlerStartTime(req);

  auto start_time = std::chrono::steady_clock::now();

  std::string solutionId = extractSolutionId(req);

  if (isApiLoggingEnabled()) {
    PLOG_INFO << "[API] PUT /v1/core/solution/" << solutionId
              << " - Update solution";
  }

  try {
    if (!solution_registry_ || !solution_storage_) {
      if (isApiLoggingEnabled()) {
        PLOG_ERROR << "[API] PUT /v1/core/solution/" << solutionId
                   << " - Error: Solution registry or storage not initialized";
      }
      callback(
          createErrorResponse(500, "Internal server error",
                              "Solution registry or storage not initialized"));
      return;
    }

    if (solutionId.empty()) {
      if (isApiLoggingEnabled()) {
        PLOG_WARNING << "[API] PUT /v1/core/solution/{id} - Error: Solution "
                        "ID is empty";
      }
      callback(createErrorResponse(400, "Invalid request",
                                   "Solution ID is required"));
      return;
    }

    // Check if solution exists
    if (!solution_registry_->hasSolution(solutionId)) {
      if (isApiLoggingEnabled()) {
        PLOG_WARNING << "[API] PUT /v1/core/solution/" << solutionId
                     << " - Not found";
      }
      callback(createErrorResponse(404, "Not found",
                                   "Solution not found: " + solutionId));
      return;
    }

    // Check if it's a default solution
    if (solution_registry_->isDefaultSolution(solutionId)) {
      if (isApiLoggingEnabled()) {
        PLOG_WARNING << "[API] PUT /v1/core/solution/" << solutionId
                     << " - Cannot update default solution";
      }
      callback(createErrorResponse(
          403, "Forbidden", "Cannot update default solution: " + solutionId));
      return;
    }

    // Parse JSON body
    auto json = req->getJsonObject();
    if (!json) {
      if (isApiLoggingEnabled()) {
        PLOG_WARNING << "[API] PUT /v1/core/solution/" << solutionId
                     << " - Error: Invalid JSON body";
      }
      callback(createErrorResponse(400, "Invalid request",
                                   "Request body must be valid JSON"));
      return;
    }

    // Parse solution config
    std::string parseError;
    auto optConfig = parseSolutionConfig(*json, parseError);
    if (!optConfig.has_value()) {
      if (isApiLoggingEnabled()) {
        PLOG_WARNING << "[API] PUT /v1/core/solution/" << solutionId
                     << " - Parse error: " << parseError;
      }
      callback(createErrorResponse(400, "Invalid request", parseError));
      return;
    }

    SolutionConfig config = optConfig.value();

    // Ensure solutionId matches
    if (config.solutionId != solutionId) {
      if (isApiLoggingEnabled()) {
        PLOG_WARNING << "[API] PUT /v1/core/solution/" << solutionId
                     << " - Solution ID mismatch";
      }
      callback(
          createErrorResponse(400, "Invalid request",
                              "Solution ID in body must match URL parameter"));
      return;
    }

    // Ensure isDefault is false for custom solutions
    config.isDefault = false;

    // Validate node types in solution - ensure all node types are supported
    auto &nodePool = NodePoolManager::getInstance();
    auto allTemplates = nodePool.getAllTemplates();
    std::set<std::string> supportedNodeTypes;
    for (const auto &template_ : allTemplates) {
      supportedNodeTypes.insert(template_.nodeType);
    }

    std::vector<std::string> unsupportedNodeTypes;
    for (const auto &nodeConfig : config.pipeline) {
      if (supportedNodeTypes.find(nodeConfig.nodeType) ==
          supportedNodeTypes.end()) {
        unsupportedNodeTypes.push_back(nodeConfig.nodeType);
      }
    }

    if (!unsupportedNodeTypes.empty()) {
      std::string errorMsg = "Solution contains unsupported node types: ";
      for (size_t i = 0; i < unsupportedNodeTypes.size(); ++i) {
        errorMsg += unsupportedNodeTypes[i];
        if (i < unsupportedNodeTypes.size() - 1) {
          errorMsg += ", ";
        }
      }
      if (isApiLoggingEnabled()) {
        PLOG_WARNING << "[API] PUT /v1/core/solution/" << solutionId
                     << " - Error: " << errorMsg;
      }
      callback(createErrorResponse(400, "Invalid request", errorMsg));
      return;
    }

    // Update solution
    if (!solution_registry_->updateSolution(config)) {
      if (isApiLoggingEnabled()) {
        PLOG_ERROR << "[API] PUT /v1/core/solution/" << solutionId
                   << " - Failed to update";
      }
      callback(createErrorResponse(500, "Internal server error",
                                   "Failed to update solution"));
      return;
    }

    // Create nodes for node types in this custom solution (if they don't exist)
    // Note: These are user-created nodes, not default nodes
    size_t nodesCreated = nodePool.createNodesFromSolution(config);
    if (nodesCreated > 0) {
      std::cerr << "[SolutionHandler] Created " << nodesCreated
                << " nodes for custom solution: " << config.solutionId
                << std::endl;
    }

    // Save to storage
    std::cerr
        << "[SolutionHandler] Attempting to save updated solution to storage: "
        << config.solutionId << std::endl;
    if (!solution_storage_->saveSolution(config)) {
      std::cerr
          << "[SolutionHandler] Warning: Failed to save solution to storage: "
          << config.solutionId << std::endl;
    } else {
      std::cerr << "[SolutionHandler] ✓ Solution updated and saved "
                   "successfully to storage: "
                << config.solutionId << std::endl;
    }

    Json::Value response = solutionConfigToJson(config);
    response["message"] = "Solution updated successfully";

    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time);

    if (isApiLoggingEnabled()) {
      PLOG_INFO << "[API] PUT /v1/core/solution/" << solutionId
                << " - Success - " << duration.count() << "ms";
    }

    callback(createSuccessResponse(response));

  } catch (const std::exception &e) {
    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time);
    if (isApiLoggingEnabled()) {
      PLOG_ERROR << "[API] PUT /v1/core/solution/" << solutionId
                 << " - Exception: " << e.what() << " - " << duration.count()
                 << "ms";
    }
    std::cerr << "[SolutionHandler] Exception: " << e.what() << std::endl;
    callback(createErrorResponse(500, "Internal server error", e.what()));
  } catch (...) {
    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time);
    if (isApiLoggingEnabled()) {
      PLOG_ERROR << "[API] PUT /v1/core/solution/" << solutionId
                 << " - Unknown exception - " << duration.count() << "ms";
    }
    std::cerr << "[SolutionHandler] Unknown exception" << std::endl;
    callback(createErrorResponse(500, "Internal server error",
                                 "Unknown error occurred"));
  }
}

