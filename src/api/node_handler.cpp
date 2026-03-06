#include "api/node_handler.h"
#include "core/logger.h"
#include "core/logging_flags.h"
#include "core/metrics_interceptor.h"
#include "core/node_pool_manager.h"
#include <algorithm>
#include <ctime>
#include <drogon/HttpResponse.h>
#include <iomanip>
#include <json/json.h>
#include <set>
#include <sstream>
#include <vector>

void NodeHandler::listNodes(
    const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback) {
  // Set handler start time for accurate metrics
  MetricsInterceptor::setHandlerStartTime(req);

  auto start_time = std::chrono::steady_clock::now();

  if (isApiLoggingEnabled()) {
    PLOG_INFO << "[API] GET /v1/core/node - List all nodes";
  }

  try {
    auto &nodePool = NodePoolManager::getInstance();

    // Get query parameters
    bool availableOnly = false;
    std::string category;
    std::string type =
        req->getParameter("type"); // "preconfigured" (default) or "templates"

    auto availableParam = req->getParameter("available");
    if (availableParam == "true" || availableParam == "1") {
      availableOnly = true;
    }

    category = req->getParameter("category");

    // If type=templates, return templates instead of pre-configured nodes
    if (type == "templates") {
      std::vector<NodePoolManager::NodeTemplate> templates;
      if (!category.empty()) {
        templates = nodePool.getTemplatesByCategory(category);
      } else {
        templates = nodePool.getAllTemplates();
      }

      Json::Value response;
      Json::Value templatesArray(Json::arrayValue);

      for (const auto &nodeTemplate : templates) {
        templatesArray.append(templateToJson(nodeTemplate));
      }

      response["nodes"] = templatesArray;
      response["total"] = static_cast<Json::Int64>(templates.size());
      response["type"] = "templates";

      auto end_time = std::chrono::steady_clock::now();
      auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
          end_time - start_time);

      if (isApiLoggingEnabled()) {
        PLOG_INFO << "[API] GET /v1/core/node - Success: " << templates.size()
                  << " templates - " << duration.count() << "ms";
      }

      callback(createSuccessResponse(response));
      return;
    }

    // Get pre-configured nodes
    std::vector<NodePoolManager::PreConfiguredNode> nodes;
    if (availableOnly) {
      nodes = nodePool.getAvailableNodes();
    } else {
      nodes = nodePool.getAllPreConfiguredNodes();
    }

    // If no pre-configured nodes exist, return templates as available node
    // types
    if (nodes.empty()) {
      std::vector<NodePoolManager::NodeTemplate> templates;
      if (!category.empty()) {
        templates = nodePool.getTemplatesByCategory(category);
      } else {
        templates = nodePool.getAllTemplates();
      }

      Json::Value response;
      Json::Value nodesArray(Json::arrayValue);

      for (const auto &nodeTemplate : templates) {
        // Convert template to node-like JSON format
        Json::Value nodeJson;
        nodeJson["nodeId"] =
            nodeTemplate.templateId; // Use templateId as nodeId
        nodeJson["templateId"] = nodeTemplate.templateId;
        nodeJson["displayName"] = nodeTemplate.displayName;
        nodeJson["nodeType"] = nodeTemplate.nodeType;
        nodeJson["category"] = nodeTemplate.category;
        nodeJson["description"] = nodeTemplate.description;
        nodeJson["inUse"] = false;
        nodeJson["isTemplate"] =
            true; // Indicate this is a template, not a pre-configured node

        // Parameters from default parameters
        Json::Value params(Json::objectValue);
        for (const auto &[key, value] : nodeTemplate.defaultParameters) {
          params[key] = value;
        }
        nodeJson["parameters"] = params;

        // Required and optional parameters info
        Json::Value requiredParams(Json::arrayValue);
        for (const auto &param : nodeTemplate.requiredParameters) {
          requiredParams.append(param);
        }
        nodeJson["requiredParameters"] = requiredParams;

        Json::Value optionalParams(Json::arrayValue);
        for (const auto &param : nodeTemplate.optionalParameters) {
          optionalParams.append(param);
        }
        nodeJson["optionalParameters"] = optionalParams;

        nodesArray.append(nodeJson);
      }

      response["nodes"] = nodesArray;
      response["total"] = static_cast<Json::Int64>(templates.size());
      response["available"] = static_cast<Json::Int64>(templates.size());
      response["inUse"] = 0;
      response["type"] =
          "templates"; // Indicate these are templates, not pre-configured nodes
      response["message"] =
          "No pre-configured nodes found. Showing available node templates.";

      auto end_time = std::chrono::steady_clock::now();
      auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
          end_time - start_time);

      if (isApiLoggingEnabled()) {
        PLOG_INFO << "[API] GET /v1/core/node - Success: " << templates.size()
                  << " templates (no pre-configured nodes) - "
                  << duration.count() << "ms";
      }

      callback(createSuccessResponse(response));
      return;
    }

    // Filter by category if specified
    if (!category.empty()) {
      std::vector<NodePoolManager::PreConfiguredNode> filtered;
      for (const auto &node : nodes) {
        auto template_opt = nodePool.getTemplate(node.templateId);
        if (template_opt.has_value() &&
            template_opt.value().category == category) {
          filtered.push_back(node);
        }
      }
      nodes = filtered;
    }

    // Build response
    Json::Value response;
    Json::Value nodesArray(Json::arrayValue);

    for (const auto &node : nodes) {
      nodesArray.append(nodeToJson(node));
    }

    response["nodes"] = nodesArray;
    response["total"] = static_cast<Json::Int64>(nodes.size());
    response["available"] =
        static_cast<Json::Int64>(nodePool.getAvailableNodes().size());
    response["inUse"] =
        static_cast<Json::Int64>(nodePool.getAllPreConfiguredNodes().size() -
                                 nodePool.getAvailableNodes().size());
    response["type"] =
        "preconfigured"; // Indicate these are pre-configured nodes

    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time);

    if (isApiLoggingEnabled()) {
      PLOG_INFO << "[API] GET /v1/core/node - Success: " << nodes.size()
                << " nodes - " << duration.count() << "ms";
    }

    callback(createSuccessResponse(response));

  } catch (const std::exception &e) {
    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time);

    if (isApiLoggingEnabled()) {
      PLOG_ERROR << "[API] GET /v1/core/node - Exception: " << e.what() << " - "
                 << duration.count() << "ms";
    }

    callback(createErrorResponse(500, "Internal server error", e.what()));
  } catch (...) {
    callback(createErrorResponse(500, "Internal server error",
                                 "Unknown error occurred"));
  }
}

void NodeHandler::getNode(
    const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback) {
  auto start_time = std::chrono::steady_clock::now();

  // Extract nodeId from path
  std::string nodeId = req->getParameter("nodeId");
  if (nodeId.empty()) {
    // Try to extract from path
    std::string path = req->getPath();
    size_t nodesPos = path.find("/nodes/");
    if (nodesPos != std::string::npos) {
      size_t start = nodesPos + 7; // length of "/nodes/"
      size_t end = path.find("/", start);
      if (end == std::string::npos) {
        end = path.length();
      }
      nodeId = path.substr(start, end - start);
    }
  }

  if (isApiLoggingEnabled()) {
    PLOG_INFO << "[API] GET /v1/core/node/" << nodeId << " - Get node details";
  }

  try {
    if (nodeId.empty()) {
      callback(
          createErrorResponse(400, "Bad request", "Missing nodeId parameter"));
      return;
    }

    auto &nodePool = NodePoolManager::getInstance();
    auto nodeOpt = nodePool.getPreConfiguredNode(nodeId);

    if (!nodeOpt.has_value()) {
      callback(
          createErrorResponse(404, "Not found", "Node not found: " + nodeId));
      return;
    }

    Json::Value response = nodeToJson(nodeOpt.value());

    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time);

    if (isApiLoggingEnabled()) {
      PLOG_INFO << "[API] GET /v1/core/node/" << nodeId << " - Success - "
                << duration.count() << "ms";
    }

    callback(createSuccessResponse(response));

  } catch (const std::exception &e) {
    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time);

    if (isApiLoggingEnabled()) {
      PLOG_ERROR << "[API] GET /v1/core/node/" << nodeId
                 << " - Exception: " << e.what() << " - " << duration.count()
                 << "ms";
    }

    callback(createErrorResponse(500, "Internal server error", e.what()));
  } catch (...) {
    callback(createErrorResponse(500, "Internal server error",
                                 "Unknown error occurred"));
  }
}

void NodeHandler::createNode(
    const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback) {
  auto start_time = std::chrono::steady_clock::now();

  if (isApiLoggingEnabled()) {
    PLOG_INFO << "[API] POST /v1/core/node - Create new node";
  }

  try {
    // Parse JSON body
    auto json = req->getJsonObject();
    if (!json) {
      callback(createErrorResponse(400, "Bad request", "Invalid JSON body"));
      return;
    }

    // Validate required fields
    if (!json->isMember("templateId") ||
        (*json)["templateId"].asString().empty()) {
      callback(createErrorResponse(400, "Bad request",
                                   "Missing required field: templateId"));
      return;
    }

    std::string templateId = (*json)["templateId"].asString();

    // Parse parameters
    std::map<std::string, std::string> parameters;
    if (json->isMember("parameters") && (*json)["parameters"].isObject()) {
      const auto &paramsObj = (*json)["parameters"];
      for (const auto &key : paramsObj.getMemberNames()) {
        parameters[key] = paramsObj[key].asString();
      }
    }

    // Create node
    auto &nodePool = NodePoolManager::getInstance();
    std::string nodeId =
        nodePool.createPreConfiguredNode(templateId, parameters);

    if (nodeId.empty()) {
      callback(createErrorResponse(
          400, "Bad request",
          "Failed to create node. Check templateId and required parameters."));
      return;
    }

    // Get created node
    auto nodeOpt = nodePool.getPreConfiguredNode(nodeId);
    if (!nodeOpt.has_value()) {
      callback(createErrorResponse(500, "Internal server error",
                                   "Node created but could not be retrieved"));
      return;
    }

    Json::Value response = nodeToJson(nodeOpt.value());
    response["message"] = "Node created successfully";

    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time);

    if (isApiLoggingEnabled()) {
      PLOG_INFO << "[API] POST /v1/core/node - Success: Created node " << nodeId
                << " - " << duration.count() << "ms";
    }

    callback(createSuccessResponse(response, 201));

  } catch (const std::exception &e) {
    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time);

    if (isApiLoggingEnabled()) {
      PLOG_ERROR << "[API] POST /v1/core/node - Exception: " << e.what()
                 << " - " << duration.count() << "ms";
    }

    callback(createErrorResponse(500, "Internal server error", e.what()));
  } catch (...) {
    callback(createErrorResponse(500, "Internal server error",
                                 "Unknown error occurred"));
  }
}

void NodeHandler::updateNode(
    const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback) {
  auto start_time = std::chrono::steady_clock::now();

  // Extract nodeId from path
  std::string nodeId = req->getParameter("nodeId");
  if (nodeId.empty()) {
    std::string path = req->getPath();
    size_t nodesPos = path.find("/nodes/");
    if (nodesPos != std::string::npos) {
      size_t start = nodesPos + 7;
      size_t end = path.find("/", start);
      if (end == std::string::npos) {
        end = path.length();
      }
      nodeId = path.substr(start, end - start);
    }
  }

  if (isApiLoggingEnabled()) {
    PLOG_INFO << "[API] PUT /v1/core/node/" << nodeId << " - Update node";
  }

  try {
    if (nodeId.empty()) {
      callback(
          createErrorResponse(400, "Bad request", "Missing nodeId parameter"));
      return;
    }

    // Parse JSON body
    auto json = req->getJsonObject();
    if (!json) {
      callback(createErrorResponse(400, "Bad request", "Invalid JSON body"));
      return;
    }

    auto &nodePool = NodePoolManager::getInstance();
    auto nodeOpt = nodePool.getPreConfiguredNode(nodeId);

    if (!nodeOpt.has_value()) {
      callback(
          createErrorResponse(404, "Not found", "Node not found: " + nodeId));
      return;
    }

    // Check if node is in use
    if (nodeOpt.value().inUse) {
      callback(createErrorResponse(
          409, "Conflict", "Cannot update node that is currently in use"));
      return;
    }

    // For now, update means delete and recreate
    // In future, can implement actual update logic
    if (json->isMember("parameters") && (*json)["parameters"].isObject()) {
      // Parse new parameters
      std::map<std::string, std::string> parameters;
      const auto &paramsObj = (*json)["parameters"];
      for (const auto &key : paramsObj.getMemberNames()) {
        parameters[key] = paramsObj[key].asString();
      }

      // Delete old node
      nodePool.removePreConfiguredNode(nodeId);

      // Create new node with same templateId
      std::string newTemplateId = nodeOpt.value().templateId;
      std::string newNodeId =
          nodePool.createPreConfiguredNode(newTemplateId, parameters);

      if (newNodeId.empty()) {
        callback(createErrorResponse(500, "Internal server error",
                                     "Failed to update node"));
        return;
      }

      // Get updated node
      auto updatedNodeOpt = nodePool.getPreConfiguredNode(newNodeId);
      if (!updatedNodeOpt.has_value()) {
        callback(
            createErrorResponse(500, "Internal server error",
                                "Node updated but could not be retrieved"));
        return;
      }

      Json::Value response = nodeToJson(updatedNodeOpt.value());
      response["message"] = "Node updated successfully";
      response["oldNodeId"] = nodeId;
      response["newNodeId"] = newNodeId;

      auto end_time = std::chrono::steady_clock::now();
      auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
          end_time - start_time);

      if (isApiLoggingEnabled()) {
        PLOG_INFO << "[API] PUT /v1/core/node/" << nodeId
                  << " - Success: Updated to " << newNodeId << " - "
                  << duration.count() << "ms";
      }

      callback(createSuccessResponse(response));
    } else {
      callback(
          createErrorResponse(400, "Bad request", "Missing parameters field"));
    }

  } catch (const std::exception &e) {
    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time);

    if (isApiLoggingEnabled()) {
      PLOG_ERROR << "[API] PUT /v1/core/node/" << nodeId
                 << " - Exception: " << e.what() << " - " << duration.count()
                 << "ms";
    }

    callback(createErrorResponse(500, "Internal server error", e.what()));
  } catch (...) {
    callback(createErrorResponse(500, "Internal server error",
                                 "Unknown error occurred"));
  }
}

void NodeHandler::deleteNode(
    const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback) {
  auto start_time = std::chrono::steady_clock::now();

  // Extract nodeId from path
  std::string nodeId = req->getParameter("nodeId");
  if (nodeId.empty()) {
    std::string path = req->getPath();
    size_t nodesPos = path.find("/nodes/");
    if (nodesPos != std::string::npos) {
      size_t start = nodesPos + 7;
      size_t end = path.find("/", start);
      if (end == std::string::npos) {
        end = path.length();
      }
      nodeId = path.substr(start, end - start);
    }
  }

  if (isApiLoggingEnabled()) {
    PLOG_INFO << "[API] DELETE /v1/core/node/" << nodeId << " - Delete node";
  }

  try {
    if (nodeId.empty()) {
      callback(
          createErrorResponse(400, "Bad request", "Missing nodeId parameter"));
      return;
    }

    auto &nodePool = NodePoolManager::getInstance();
    auto nodeOpt = nodePool.getPreConfiguredNode(nodeId);

    if (!nodeOpt.has_value()) {
      callback(
          createErrorResponse(404, "Not found", "Node not found: " + nodeId));
      return;
    }

    // Check if node is in use
    if (nodeOpt.value().inUse) {
      callback(createErrorResponse(
          409, "Conflict", "Cannot delete node that is currently in use"));
      return;
    }

    bool deleted = nodePool.removePreConfiguredNode(nodeId);

    if (!deleted) {
      callback(createErrorResponse(500, "Internal server error",
                                   "Failed to delete node"));
      return;
    }

    Json::Value response;
    response["message"] = "Node deleted successfully";
    response["nodeId"] = nodeId;

    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time);

    if (isApiLoggingEnabled()) {
      PLOG_INFO << "[API] DELETE /v1/core/node/" << nodeId << " - Success - "
                << duration.count() << "ms";
    }

    callback(createSuccessResponse(response));

  } catch (const std::exception &e) {
    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time);

    if (isApiLoggingEnabled()) {
      PLOG_ERROR << "[API] DELETE /v1/core/node/" << nodeId
                 << " - Exception: " << e.what() << " - " << duration.count()
                 << "ms";
    }

    callback(createErrorResponse(500, "Internal server error", e.what()));
  } catch (...) {
    callback(createErrorResponse(500, "Internal server error",
                                 "Unknown error occurred"));
  }
}

void NodeHandler::listTemplates(
    const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback) {
  auto start_time = std::chrono::steady_clock::now();

  if (isApiLoggingEnabled()) {
    PLOG_INFO << "[API] GET /v1/core/node/template - List all templates";
  }

  try {
    auto &nodePool = NodePoolManager::getInstance();

    // Get query parameter for category filter
    std::string category = req->getParameter("category");

    std::vector<NodePoolManager::NodeTemplate> templates;
    if (!category.empty()) {
      templates = nodePool.getTemplatesByCategory(category);
    } else {
      templates = nodePool.getAllTemplates();
    }

    // Build response
    Json::Value response;
    Json::Value templatesArray(Json::arrayValue);

    for (const auto &nodeTemplate : templates) {
      templatesArray.append(templateToJson(nodeTemplate));
    }

    response["templates"] = templatesArray;
    response["total"] = static_cast<Json::Int64>(templates.size());

    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time);

    if (isApiLoggingEnabled()) {
      PLOG_INFO << "[API] GET /v1/core/node/template - Success: "
                << templates.size() << " templates - " << duration.count()
                << "ms";
    }

    callback(createSuccessResponse(response));

  } catch (const std::exception &e) {
    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time);

    if (isApiLoggingEnabled()) {
      PLOG_ERROR << "[API] GET /v1/core/node/template - Exception: " << e.what()
                 << " - " << duration.count() << "ms";
    }

    callback(createErrorResponse(500, "Internal server error", e.what()));
  } catch (...) {
    callback(createErrorResponse(500, "Internal server error",
                                 "Unknown error occurred"));
  }
}

void NodeHandler::getTemplate(
    const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback) {
  auto start_time = std::chrono::steady_clock::now();

  // Extract templateId from path
  std::string templateId = req->getParameter("templateId");
  if (templateId.empty()) {
    std::string path = req->getPath();
    size_t templatesPos = path.find("/templates/");
    if (templatesPos != std::string::npos) {
      size_t start = templatesPos + 11; // length of "/templates/"
      size_t end = path.find("/", start);
      if (end == std::string::npos) {
        end = path.length();
      }
      templateId = path.substr(start, end - start);
    }
  }

  if (isApiLoggingEnabled()) {
    PLOG_INFO << "[API] GET /v1/core/node/template/" << templateId
              << " - Get template details";
  }

  try {
    if (templateId.empty()) {
      callback(createErrorResponse(400, "Bad request",
                                   "Missing templateId parameter"));
      return;
    }

    auto &nodePool = NodePoolManager::getInstance();
    auto templateOpt = nodePool.getTemplate(templateId);

    if (!templateOpt.has_value()) {
      callback(createErrorResponse(404, "Not found",
                                   "Template not found: " + templateId));
      return;
    }

    Json::Value response = templateToJson(templateOpt.value());

    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time);

    if (isApiLoggingEnabled()) {
      PLOG_INFO << "[API] GET /v1/core/node/template/" << templateId
                << " - Success - " << duration.count() << "ms";
    }

    callback(createSuccessResponse(response));

  } catch (const std::exception &e) {
    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time);

    if (isApiLoggingEnabled()) {
      PLOG_ERROR << "[API] GET /v1/core/node/template/" << templateId
                 << " - Exception: " << e.what() << " - " << duration.count()
                 << "ms";
    }

    callback(createErrorResponse(500, "Internal server error", e.what()));
  } catch (...) {
    callback(createErrorResponse(500, "Internal server error",
                                 "Unknown error occurred"));
  }
}

void NodeHandler::getStats(
    const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback) {
  auto start_time = std::chrono::steady_clock::now();

  if (isApiLoggingEnabled()) {
    PLOG_INFO << "[API] GET /v1/core/node/stats - Get node pool statistics";
  }

  try {
    auto &nodePool = NodePoolManager::getInstance();
    auto stats = nodePool.getStats();

    Json::Value response;
    response["totalTemplates"] = static_cast<Json::Int64>(stats.totalTemplates);
    response["totalPreConfiguredNodes"] =
        static_cast<Json::Int64>(stats.totalPreConfiguredNodes);
    response["availableNodes"] = static_cast<Json::Int64>(stats.availableNodes);
    response["inUseNodes"] = static_cast<Json::Int64>(stats.inUseNodes);

    Json::Value nodesByCategory(Json::objectValue);
    for (const auto &[category, count] : stats.nodesByCategory) {
      nodesByCategory[category] = static_cast<Json::Int64>(count);
    }
    response["nodesByCategory"] = nodesByCategory;

    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time);

    if (isApiLoggingEnabled()) {
      PLOG_INFO << "[API] GET /v1/core/node/stats - Success - "
                << duration.count() << "ms";
    }

    callback(createSuccessResponse(response));

  } catch (const std::exception &e) {
    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time);

    if (isApiLoggingEnabled()) {
      PLOG_ERROR << "[API] GET /v1/core/node/stats - Exception: " << e.what()
                 << " - " << duration.count() << "ms";
    }

    callback(createErrorResponse(500, "Internal server error", e.what()));
  } catch (...) {
    callback(createErrorResponse(500, "Internal server error",
                                 "Unknown error occurred"));
  }
}

void NodeHandler::handleOptions(
    const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback) {
  // Set handler start time for accurate metrics
  MetricsInterceptor::setHandlerStartTime(req);

  auto resp = HttpResponse::newHttpResponse();
  resp->setStatusCode(k200OK);
  resp->addHeader("Access-Control-Allow-Origin", "*");
  resp->addHeader("Access-Control-Allow-Methods",
                  "GET, POST, PUT, DELETE, OPTIONS");
  resp->addHeader("Access-Control-Allow-Headers",
                  "Content-Type, Authorization");
  resp->addHeader("Access-Control-Max-Age", "3600");

  // Record metrics and call callback
  MetricsInterceptor::callWithMetrics(req, resp, std::move(callback));
}

HttpResponsePtr NodeHandler::createSuccessResponse(const Json::Value &data,
                                                   int statusCode) const {
  auto resp = HttpResponse::newHttpJsonResponse(data);
  resp->setStatusCode(static_cast<HttpStatusCode>(statusCode));
  resp->addHeader("Access-Control-Allow-Origin", "*");
  resp->addHeader("Access-Control-Allow-Methods",
                  "GET, POST, PUT, DELETE, OPTIONS");
  resp->addHeader("Access-Control-Allow-Headers",
                  "Content-Type, Authorization");
  return resp;
}

HttpResponsePtr
NodeHandler::createErrorResponse(int statusCode, const std::string &error,
                                 const std::string &message) const {
  Json::Value errorResponse;
  errorResponse["error"] = error;
  errorResponse["message"] = message;

  auto resp = HttpResponse::newHttpJsonResponse(errorResponse);
  resp->setStatusCode(static_cast<HttpStatusCode>(statusCode));
  resp->addHeader("Access-Control-Allow-Origin", "*");
  resp->addHeader("Access-Control-Allow-Methods",
                  "GET, POST, PUT, DELETE, OPTIONS");
  resp->addHeader("Access-Control-Allow-Headers",
                  "Content-Type, Authorization");
  return resp;
}

Json::Value
NodeHandler::nodeToJson(const NodePoolManager::PreConfiguredNode &node) const {
  Json::Value json;
  json["nodeId"] = node.nodeId;
  json["templateId"] = node.templateId;
  json["inUse"] = node.inUse;

  // Get template info for display
  auto &nodePool = NodePoolManager::getInstance();
  auto templateOpt = nodePool.getTemplate(node.templateId);
  std::string category;
  if (templateOpt.has_value()) {
    json["displayName"] = templateOpt.value().displayName;
    json["nodeType"] = templateOpt.value().nodeType;
    json["category"] = templateOpt.value().category;
    json["description"] = templateOpt.value().description;
    category = templateOpt.value().category;

    // Add template information (required/optional parameters)
    Json::Value requiredParams(Json::arrayValue);
    for (const auto &param : templateOpt.value().requiredParameters) {
      requiredParams.append(param);
    }
    json["requiredParameters"] = requiredParams;

    Json::Value optionalParams(Json::arrayValue);
    for (const auto &param : templateOpt.value().optionalParameters) {
      optionalParams.append(param);
    }
    json["optionalParameters"] = optionalParams;

    // Add link to template detail
    json["templateDetailUrl"] = "/v1/core/node/template/" + node.templateId;
  }

  // Parameters
  Json::Value params(Json::objectValue);
  for (const auto &[key, value] : node.parameters) {
    params[key] = value;
  }
  json["parameters"] = params;

  // Get nodeType for parameter schema
  std::string nodeType = json["nodeType"].asString();

  // Add detailed parameter schema for UI
  if (templateOpt.has_value()) {
    Json::Value parameterSchema(Json::objectValue);

    // Process all parameters (required + optional)
    std::set<std::string> allParams;
    for (const auto &param : templateOpt.value().requiredParameters) {
      allParams.insert(param);
    }
    for (const auto &param : templateOpt.value().optionalParameters) {
      allParams.insert(param);
    }

    for (const auto &paramName : allParams) {
      Json::Value paramInfo(Json::objectValue);

      // Basic info
      paramInfo["name"] = paramName;
      bool isRequired =
          std::find(templateOpt.value().requiredParameters.begin(),
                    templateOpt.value().requiredParameters.end(),
                    paramName) != templateOpt.value().requiredParameters.end();
      paramInfo["required"] = isRequired;

      // Get current value
      if (params.isMember(paramName)) {
        paramInfo["currentValue"] = params[paramName];
      }

      // Get default value
      auto defaultIt = templateOpt.value().defaultParameters.find(paramName);
      if (defaultIt != templateOpt.value().defaultParameters.end()) {
        paramInfo["default"] = defaultIt->second;
      }

      // Infer type and add metadata based on parameter name and value
      std::string paramType = inferParameterType(paramName, nodeType);
      paramInfo["type"] = paramType;

      // Add UI hints
      Json::Value uiHints(Json::objectValue);
      uiHints["inputType"] = getInputType(paramName, paramType);
      uiHints["widget"] = getWidgetType(paramName, paramType);
      uiHints["placeholder"] = getPlaceholder(paramName, nodeType);
      paramInfo["uiHints"] = uiHints;

      // Add validation rules
      Json::Value validation(Json::objectValue);
      addValidationRules(validation, paramName, paramType, nodeType);
      if (validation.size() > 0) {
        paramInfo["validation"] = validation;
      }

      // Add description
      paramInfo["description"] = getParameterDescription(paramName, nodeType);

      // Add examples
      Json::Value examples(Json::arrayValue);
      auto exampleValues = getParameterExamples(paramName, nodeType);
      for (const auto &ex : exampleValues) {
        examples.append(ex);
      }
      if (examples.size() > 0) {
        paramInfo["examples"] = examples;
      }

      // Add category/group
      paramInfo["category"] = getParameterCategory(paramName, nodeType);

      parameterSchema[paramName] = paramInfo;
    }

    json["parameterSchema"] = parameterSchema;
  }

  // Add detailed input/output information based on node type and category
  Json::Value input(Json::arrayValue);
  Json::Value output(Json::arrayValue);

  // nodeType already declared above (line 859)

  if (category == "source") {
    // Source nodes: no input from pipeline, output video frames
    Json::Value inputItem(Json::objectValue);
    inputItem["type"] = "external_source";
    inputItem["description"] =
        "Receives input from external source (file, RTSP, RTMP, etc.)";
    input.append(inputItem);

    Json::Value outputItem(Json::objectValue);
    outputItem["type"] = "video_frames";
    outputItem["description"] =
        "Video frames (cv::Mat format) - can connect to multiple nodes";
    outputItem["format"] = "BGR/RGB image frames";
    outputItem["multiple"] = true;
    outputItem["maxOutputs"] = -1; // Unlimited - can connect to multiple
                                   // processors/detectors/destinations
    output.append(outputItem);
  } else if (category == "detector") {
    // Detector nodes: input video frames, output video frames + detection
    // results
    Json::Value inputItem(Json::objectValue);
    inputItem["type"] = "video_frames";
    inputItem["description"] = "Video frames from previous node";
    inputItem["format"] = "BGR/RGB image frames";
    input.append(inputItem);

    Json::Value outputItem1(Json::objectValue);
    outputItem1["type"] = "video_frames";
    outputItem1["description"] =
        "Video frames (passed through) - can connect to multiple nodes";
    outputItem1["format"] = "BGR/RGB image frames";
    outputItem1["multiple"] = true;
    outputItem1["maxOutputs"] =
        -1; // Unlimited - can connect to multiple processors/destinations
    output.append(outputItem1);

    Json::Value outputItem2(Json::objectValue);
    outputItem2["type"] = "detection_results";
    outputItem2["description"] = "Detection results (bounding boxes, classes, "
                                 "scores) - can connect to multiple brokers";
    outputItem2["format"] = "JSON metadata with detections";
    outputItem2["multiple"] = true;
    outputItem2["maxOutputs"] =
        -1; // Unlimited - can connect to multiple brokers
    output.append(outputItem2);
  } else if (category == "processor") {
    // Special handling for nodes that support multiple inputs
    if (nodeType == "sync") {
      // Sync node: can accept multiple video frame inputs from different
      // channels
      Json::Value inputItem1(Json::objectValue);
      inputItem1["type"] = "video_frames";
      inputItem1["description"] = "Video frames from multiple channels/nodes "
                                  "(supports multiple inputs)";
      inputItem1["format"] = "BGR/RGB image frames";
      inputItem1["multiple"] = true;
      inputItem1["minInputs"] = 2;
      inputItem1["maxInputs"] = -1; // Unlimited
      input.append(inputItem1);

      Json::Value inputItem2(Json::objectValue);
      inputItem2["type"] = "metadata";
      inputItem2["description"] = "Metadata from previous nodes (optional)";
      inputItem2["format"] = "JSON metadata";
      inputItem2["optional"] = true;
      input.append(inputItem2);

      Json::Value outputItem1(Json::objectValue);
      outputItem1["type"] = "video_frames";
      outputItem1["description"] = "Synchronized video frames from multiple "
                                   "channels - can connect to multiple nodes";
      outputItem1["format"] = "BGR/RGB image frames";
      outputItem1["multiple"] = true;
      outputItem1["maxOutputs"] = -1; // Unlimited
      output.append(outputItem1);

      Json::Value outputItem2(Json::objectValue);
      outputItem2["type"] = "metadata";
      outputItem2["description"] =
          "Synchronized metadata - can connect to multiple brokers";
      outputItem2["format"] = "JSON metadata";
      outputItem2["multiple"] = true;
      outputItem2["maxOutputs"] = -1; // Unlimited
      output.append(outputItem2);
    } else if (nodeType == "frame_fusion") {
      // Frame fusion node: fuses frames from multiple channels
      Json::Value inputItem1(Json::objectValue);
      inputItem1["type"] = "video_frames";
      inputItem1["description"] = "Video frames from multiple channels/nodes "
                                  "(supports multiple inputs)";
      inputItem1["format"] = "BGR/RGB image frames";
      inputItem1["multiple"] = true;
      inputItem1["minInputs"] = 2;
      inputItem1["maxInputs"] = -1; // Unlimited
      input.append(inputItem1);

      Json::Value inputItem2(Json::objectValue);
      inputItem2["type"] = "metadata";
      inputItem2["description"] = "Metadata from previous nodes (optional)";
      inputItem2["format"] = "JSON metadata";
      inputItem2["optional"] = true;
      input.append(inputItem2);

      Json::Value outputItem1(Json::objectValue);
      outputItem1["type"] = "video_frames";
      outputItem1["description"] = "Fused video frames from multiple channels "
                                   "- can connect to multiple nodes";
      outputItem1["format"] = "BGR/RGB image frames";
      outputItem1["multiple"] = true;
      outputItem1["maxOutputs"] = -1; // Unlimited
      output.append(outputItem1);

      Json::Value outputItem2(Json::objectValue);
      outputItem2["type"] = "metadata";
      outputItem2["description"] =
          "Fused metadata - can connect to multiple brokers";
      outputItem2["format"] = "JSON metadata";
      outputItem2["multiple"] = true;
      outputItem2["maxOutputs"] = -1; // Unlimited
      output.append(outputItem2);
    } else if (nodeType == "split") {
      // Split node: can split input into multiple outputs
      Json::Value inputItem1(Json::objectValue);
      inputItem1["type"] = "video_frames";
      inputItem1["description"] =
          "Video frames from previous node (can contain multiple channels)";
      inputItem1["format"] = "BGR/RGB image frames";
      input.append(inputItem1);

      Json::Value inputItem2(Json::objectValue);
      inputItem2["type"] = "metadata";
      inputItem2["description"] = "Metadata from previous nodes";
      inputItem2["format"] = "JSON metadata";
      input.append(inputItem2);

      Json::Value outputItem1(Json::objectValue);
      outputItem1["type"] = "video_frames";
      outputItem1["description"] = "Split video frames (can output multiple "
                                   "channels when split_by_channel=true)";
      outputItem1["format"] = "BGR/RGB image frames";
      outputItem1["multiple"] = true;
      outputItem1["maxOutputs"] = -1; // Unlimited when split_by_channel=true
      output.append(outputItem1);

      Json::Value outputItem2(Json::objectValue);
      outputItem2["type"] = "metadata";
      outputItem2["description"] = "Metadata (passed through or split)";
      outputItem2["format"] = "JSON metadata";
      output.append(outputItem2);
    } else {
      // Standard processor nodes: input video frames + metadata, output
      // processed video frames + metadata
      Json::Value inputItem1(Json::objectValue);
      inputItem1["type"] = "video_frames";
      inputItem1["description"] = "Video frames from previous node";
      inputItem1["format"] = "BGR/RGB image frames";
      input.append(inputItem1);

      Json::Value inputItem2(Json::objectValue);
      inputItem2["type"] = "metadata";
      inputItem2["description"] =
          "Metadata from previous nodes (detections, tracking, etc.)";
      inputItem2["format"] = "JSON metadata";
      input.append(inputItem2);

      Json::Value outputItem1(Json::objectValue);
      outputItem1["type"] = "video_frames";
      outputItem1["description"] =
          "Processed video frames - can connect to multiple nodes";
      outputItem1["format"] = "BGR/RGB image frames";
      outputItem1["multiple"] = true;
      outputItem1["maxOutputs"] =
          -1; // Unlimited - can connect to multiple processors/destinations
      output.append(outputItem1);

      Json::Value outputItem2(Json::objectValue);
      outputItem2["type"] = "metadata";
      outputItem2["description"] =
          "Processed/updated metadata - can connect to multiple brokers";
      outputItem2["format"] = "JSON metadata";
      outputItem2["multiple"] = true;
      outputItem2["maxOutputs"] =
          -1; // Unlimited - can connect to multiple brokers
      output.append(outputItem2);
    }
  } else if (category == "destination") {
    // Destination nodes: input video frames + metadata, output to external
    Json::Value inputItem1(Json::objectValue);
    inputItem1["type"] = "video_frames";
    inputItem1["description"] = "Video frames from previous node";
    inputItem1["format"] = "BGR/RGB image frames";
    input.append(inputItem1);

    Json::Value inputItem2(Json::objectValue);
    inputItem2["type"] = "metadata";
    inputItem2["description"] = "Metadata from previous nodes";
    inputItem2["format"] = "JSON metadata";
    input.append(inputItem2);

    // Destination-specific output details
    Json::Value outputItem(Json::objectValue);
    if (nodeType == "rtsp_des") {
      outputItem["type"] = "rtsp_stream";
      outputItem["description"] = "RTSP video stream";
      outputItem["protocol"] = "RTSP";
      std::string port = "8000";
      std::string streamName = "stream";
      if (params.isMember("port")) {
        port = params["port"].asString();
      }
      if (params.isMember("stream_name")) {
        streamName = params["stream_name"].asString();
      }
      outputItem["accessInfo"] =
          "Access via RTSP URL: rtsp://<host>:" + port + "/" + streamName +
          " (default: rtsp://localhost:" + port + "/" + streamName + ")";
      outputItem["example"] = "rtsp://localhost:" + port + "/" + streamName;

      // Add configurable parameters that affect output
      Json::Value configurableParams(Json::arrayValue);
      if (templateOpt.has_value()) {
        // Check if port is in optional parameters
        bool hasPort = false;
        for (const auto &param : templateOpt.value().optionalParameters) {
          if (param == "port") {
            hasPort = true;
            Json::Value portParam(Json::objectValue);
            portParam["name"] = "port";
            portParam["description"] = "RTSP server port";
            portParam["default"] = "8000";
            portParam["affects"] = "RTSP URL port";
            configurableParams.append(portParam);
            break;
          }
        }
        // Check if stream_name is in optional parameters
        for (const auto &param : templateOpt.value().optionalParameters) {
          if (param == "stream_name") {
            Json::Value streamParam(Json::objectValue);
            streamParam["name"] = "stream_name";
            streamParam["description"] = "RTSP stream name/path";
            streamParam["default"] = "stream";
            streamParam["affects"] = "RTSP URL path";
            configurableParams.append(streamParam);
            break;
          }
        }
      }
      if (configurableParams.size() > 0) {
        outputItem["configurableParameters"] = configurableParams;
      }
    } else if (nodeType == "rtmp_des") {
      outputItem["type"] = "rtmp_stream";
      outputItem["description"] = "RTMP video stream";
      outputItem["protocol"] = "RTMP";
      outputItem["accessInfo"] =
          "Stream available at RTMP URL specified in 'rtmp_url' parameter";
      outputItem["example"] = "rtmp://localhost:1935/live/stream";
    } else if (nodeType == "file_des") {
      outputItem["type"] = "video_file";
      outputItem["description"] = "Video file saved to disk";
      outputItem["format"] = "MP4 video file";
      outputItem["accessInfo"] =
          "Video saved to directory specified in 'save_dir' parameter";
    } else if (nodeType == "screen_des") {
      outputItem["type"] = "screen_display";
      outputItem["description"] = "Video displayed on screen";
      outputItem["accessInfo"] = "Video displayed in a window";
    } else if (nodeType == "app_des") {
      outputItem["type"] = "application_callback";
      outputItem["description"] =
          "Video frames available via application callback";
      outputItem["accessInfo"] =
          "Frames can be captured via GET "
          "/v1/core/instances/{instanceId}/frame endpoint";
    } else {
      outputItem["type"] = "external_output";
      outputItem["description"] = "Output to external destination";
    }
    output.append(outputItem);
  } else if (category == "broker") {
    // Broker nodes: input metadata/detection results, output to external
    Json::Value inputItem1(Json::objectValue);
    inputItem1["type"] = "metadata";
    inputItem1["description"] = "Metadata from previous nodes";
    inputItem1["format"] = "JSON metadata";
    input.append(inputItem1);

    Json::Value inputItem2(Json::objectValue);
    inputItem2["type"] = "detection_results";
    inputItem2["description"] = "Detection results";
    inputItem2["format"] = "JSON detection data";
    input.append(inputItem2);

    // Broker-specific output details
    Json::Value outputItem(Json::objectValue);
    if (nodeType.find("mqtt") != std::string::npos) {
      outputItem["type"] = "mqtt_messages";
      outputItem["description"] = "MQTT messages published to broker";
      outputItem["protocol"] = "MQTT";
      outputItem["accessInfo"] =
          "Subscribe to MQTT topic specified in 'mqtt_topic' parameter on "
          "broker at 'mqtt_broker'";
    } else if (nodeType.find("kafka") != std::string::npos) {
      outputItem["type"] = "kafka_messages";
      outputItem["description"] = "Kafka messages published to topic";
      outputItem["protocol"] = "Kafka";
      outputItem["accessInfo"] =
          "Subscribe to Kafka topic specified in 'kafka_topic' parameter on "
          "broker at 'kafka_broker'";
    } else if (nodeType.find("socket") != std::string::npos) {
      outputItem["type"] = "socket_messages";
      outputItem["description"] = "Messages sent via socket";
      outputItem["protocol"] = "TCP/UDP Socket";
      std::string host = "localhost";
      std::string port = "8080";
      if (params.isMember("socket_host")) {
        host = params["socket_host"].asString();
      }
      if (params.isMember("socket_port")) {
        port = params["socket_port"].asString();
      }
      outputItem["accessInfo"] = "Connect to socket at " + host + ":" + port;
    } else if (nodeType.find("console") != std::string::npos) {
      outputItem["type"] = "console_output";
      outputItem["description"] = "JSON output to console/stdout";
      outputItem["accessInfo"] = "View in application logs/console";
    } else if (nodeType.find("file") != std::string::npos &&
               nodeType.find("broker") != std::string::npos) {
      outputItem["type"] = "file_output";
      outputItem["description"] = "Output saved to file";
      outputItem["accessInfo"] = "File path specified in 'file_path' parameter";
    } else {
      outputItem["type"] = "external_output";
      outputItem["description"] = "Output to external broker/destination";
    }
    output.append(outputItem);
  }

  json["input"] = input;
  json["output"] = output;

  // Timestamp
  // Convert steady_clock to system_clock for display
  // Calculate elapsed time since creation and subtract from current system time
  auto steady_now = std::chrono::steady_clock::now();
  auto elapsed = steady_now - node.createdAt;
  auto system_now = std::chrono::system_clock::now();
  auto system_time = system_now - elapsed;
  auto time_t = std::chrono::system_clock::to_time_t(system_time);
  std::stringstream ss;
  ss << std::put_time(std::gmtime(&time_t), "%Y-%m-%dT%H:%M:%SZ");
  json["createdAt"] = ss.str();

  return json;
}

Json::Value NodeHandler::templateToJson(
    const NodePoolManager::NodeTemplate &nodeTemplate) const {
  Json::Value json;
  json["templateId"] = nodeTemplate.templateId;
  json["nodeType"] = nodeTemplate.nodeType;
  json["displayName"] = nodeTemplate.displayName;
  json["description"] = nodeTemplate.description;
  json["category"] = nodeTemplate.category;
  json["isPreConfigured"] = nodeTemplate.isPreConfigured;

  // Default parameters
  Json::Value defaultParams(Json::objectValue);
  for (const auto &[key, value] : nodeTemplate.defaultParameters) {
    defaultParams[key] = value;
  }
  json["defaultParameters"] = defaultParams;

  // Required parameters
  Json::Value requiredParams(Json::arrayValue);
  for (const auto &param : nodeTemplate.requiredParameters) {
    requiredParams.append(param);
  }
  json["requiredParameters"] = requiredParams;

  // Optional parameters
  Json::Value optionalParams(Json::arrayValue);
  for (const auto &param : nodeTemplate.optionalParameters) {
    optionalParams.append(param);
  }
  json["optionalParameters"] = optionalParams;

  return json;
}

// Helper functions for parameter metadata generation
std::string NodeHandler::inferParameterType(const std::string &paramName,
                                            const std::string &nodeType) const {
  // Infer type from parameter name
  if (paramName.find("threshold") != std::string::npos ||
      paramName.find("ratio") != std::string::npos ||
      paramName.find("interval") != std::string::npos ||
      paramName.find("fps") != std::string::npos) {
    return "number";
  }
  if (paramName.find("port") != std::string::npos ||
      paramName.find("channel") != std::string::npos ||
      paramName.find("top_k") != std::string::npos) {
    return "integer";
  }
  if (paramName.find("enabled") != std::string::npos ||
      paramName.find("osd") != std::string::npos ||
      paramName.find("cycle") != std::string::npos ||
      paramName.find("deep_copy") != std::string::npos ||
      paramName.find("split_by_channel") != std::string::npos) {
    return "boolean";
  }
  return "string";
}

std::string NodeHandler::getInputType(const std::string &paramName,
                                      const std::string &paramType) const {
  if (paramType == "number" || paramType == "integer") {
    return "number";
  }
  if (paramType == "boolean") {
    return "checkbox";
  }
  if (paramName.find("url") != std::string::npos ||
      paramName.find("URL") != std::string::npos) {
    return "url";
  }
  if (paramName.find("path") != std::string::npos ||
      paramName.find("PATH") != std::string::npos ||
      paramName.find("dir") != std::string::npos ||
      paramName.find("location") != std::string::npos) {
    return "file";
  }
  if (paramName.find("port") != std::string::npos) {
    return "number";
  }
  return "text";
}

std::string NodeHandler::getWidgetType(const std::string &paramName,
                                       const std::string &paramType) const {
  if (paramType == "boolean") {
    return "switch";
  }
  if (paramName.find("threshold") != std::string::npos) {
    return "slider";
  }
  if (paramName.find("ratio") != std::string::npos) {
    return "slider";
  }
  if (paramName.find("url") != std::string::npos ||
      paramName.find("URL") != std::string::npos) {
    return "url-input";
  }
  if (paramName.find("path") != std::string::npos ||
      paramName.find("PATH") != std::string::npos) {
    return "file-picker";
  }
  return "input";
}

std::string NodeHandler::getPlaceholder(const std::string &paramName,
                                        const std::string &nodeType) const {
  if (paramName == "rtsp_url" || paramName == "RTSP_URL") {
    return "rtsp://camera-ip:8554/stream";
  }
  if (paramName == "rtmp_url" || paramName == "RTMP_URL") {
    return "rtmp://localhost:1935/live/stream";
  }
  if (paramName == "file_path" || paramName == "FILE_PATH") {
    return "/path/to/video.mp4";
  }
  if (paramName == "model_path" || paramName == "MODEL_PATH") {
    return "/opt/edgeos-api/models/example.onnx";
  }
  if (paramName == "port") {
    if (nodeType.find("rtsp") != std::string::npos) {
      return "8000";
    }
    return "8554";
  }
  if (paramName == "channel") {
    return "0";
  }
  if (paramName.find("threshold") != std::string::npos) {
    return "0.5";
  }
  if (paramName.find("ratio") != std::string::npos) {
    return "1.0";
  }
  return "";
}

void NodeHandler::addValidationRules(Json::Value &validation,
                                     const std::string &paramName,
                                     const std::string &paramType,
                                     const std::string &nodeType) const {
  if (paramType == "number" || paramType == "integer") {
    if (paramName.find("threshold") != std::string::npos) {
      validation["min"] = 0.0;
      validation["max"] = 1.0;
      validation["step"] = 0.01;
    }
    if (paramName.find("ratio") != std::string::npos) {
      validation["min"] = 0.0;
      validation["max"] = 1.0;
      validation["step"] = 0.1;
    }
    if (paramName.find("port") != std::string::npos) {
      validation["min"] = 1;
      validation["max"] = 65535;
    }
    if (paramName.find("channel") != std::string::npos) {
      validation["min"] = 0;
      validation["max"] = 15;
    }
    if (paramName.find("interval") != std::string::npos ||
        paramName.find("skip_interval") != std::string::npos) {
      validation["min"] = 0;
    }
  }

  if (paramType == "string") {
    if (paramName.find("url") != std::string::npos ||
        paramName.find("URL") != std::string::npos) {
      validation["pattern"] = "^(rtsp|rtmp|http|https|file|udp)://.+";
      validation["patternDescription"] =
          "Must be a valid URL (rtsp://, rtmp://, http://, https://, file://, "
          "or udp://)";
    }
    if (paramName.find("path") != std::string::npos ||
        paramName.find("PATH") != std::string::npos ||
        paramName.find("dir") != std::string::npos) {
      validation["pattern"] = "^[^\\0]+$";
      validation["patternDescription"] = "Must be a valid file path";
    }
  }

  if (paramName == "broke_for") {
    Json::Value enumValues(Json::arrayValue);
    enumValues.append("NORMAL");
    enumValues.append("ALL");
    enumValues.append("DETECTION_ONLY");
    validation["enum"] = enumValues;
  }
}

std::string
NodeHandler::getParameterDescription(const std::string &paramName,
                                     const std::string &nodeType) const {
  // Generate descriptions based on parameter name
  if (paramName == "rtsp_url" || paramName == "RTSP_URL") {
    return "RTSP stream URL (e.g., rtsp://camera-ip:8554/stream)";
  }
  if (paramName == "rtmp_url" || paramName == "RTMP_URL") {
    return "RTMP stream URL (e.g., rtmp://localhost:1935/live/stream)";
  }
  if (paramName == "file_path" || paramName == "FILE_PATH") {
    return "Path to video file or URL (supports file://, rtsp://, rtmp://, "
           "http://)";
  }
  if (paramName == "model_path" || paramName == "MODEL_PATH") {
    return "Path to model file (.onnx, .trt, .rknn, etc.)";
  }
  if (paramName == "port") {
    return "Network port number (1-65535)";
  }
  if (paramName == "channel") {
    return "Channel number for multi-channel processing (0-15)";
  }
  if (paramName.find("threshold") != std::string::npos) {
    return "Confidence threshold (0.0-1.0). Higher values = fewer detections "
           "but more accurate";
  }
  if (paramName.find("ratio") != std::string::npos) {
    return "Resize ratio (0.0-1.0). 1.0 = no resize, smaller values = "
           "downscale";
  }
  if (paramName == "save_dir") {
    return "Directory path to save output files";
  }
  if (paramName == "stream_name") {
    return "Stream name/path for RTSP server";
  }
  if (paramName.find("mqtt") != std::string::npos) {
    if (paramName.find("broker") != std::string::npos) {
      return "MQTT broker address (hostname or IP)";
    }
    if (paramName.find("topic") != std::string::npos) {
      return "MQTT topic to publish messages to";
    }
    if (paramName.find("port") != std::string::npos) {
      return "MQTT broker port (default: 1883)";
    }
  }
  if (paramName.find("socket") != std::string::npos) {
    if (paramName.find("host") != std::string::npos) {
      return "Socket host address";
    }
    if (paramName.find("port") != std::string::npos) {
      return "Socket port number";
    }
  }
  return "Parameter: " + paramName;
}

std::vector<std::string>
NodeHandler::getParameterExamples(const std::string &paramName,
                                  const std::string &nodeType) const {
  std::vector<std::string> examples;

  if (paramName == "rtsp_url" || paramName == "RTSP_URL") {
    examples.push_back("rtsp://192.168.1.100:8554/stream1");
    examples.push_back("rtsp://admin:password@camera-ip:554/stream");
  }
  if (paramName == "rtmp_url" || paramName == "RTMP_URL") {
    examples.push_back("rtmp://localhost:1935/live/stream");
    examples.push_back("rtmp://youtube.com/live2/stream-key");
  }
  if (paramName == "file_path" || paramName == "FILE_PATH") {
    examples.push_back("/path/to/video.mp4");
    examples.push_back("file:///path/to/video.mp4");
    examples.push_back("rtsp://camera-ip:8554/stream");
  }
  if (paramName == "model_path" || paramName == "MODEL_PATH") {
    examples.push_back("/opt/edgeos-api/models/face/yunet.onnx");
    examples.push_back("/opt/edgeos-api/models/trt/yolov8.engine");
  }
  if (paramName == "save_dir") {
    examples.push_back("./output/{instanceId}");
    examples.push_back("/tmp/recordings");
  }

  return examples;
}

std::string
NodeHandler::getParameterCategory(const std::string &paramName,
                                  const std::string &nodeType) const {
  // Categorize parameters for UI grouping
  if (paramName.find("url") != std::string::npos ||
      paramName.find("URL") != std::string::npos ||
      paramName.find("path") != std::string::npos ||
      paramName.find("PATH") != std::string::npos ||
      paramName.find("port") != std::string::npos ||
      paramName.find("host") != std::string::npos) {
    return "connection";
  }
  if (paramName.find("threshold") != std::string::npos ||
      paramName.find("ratio") != std::string::npos ||
      paramName.find("interval") != std::string::npos) {
    return "performance";
  }
  if (paramName.find("model") != std::string::npos ||
      paramName.find("MODEL") != std::string::npos ||
      paramName.find("weights") != std::string::npos ||
      paramName.find("config") != std::string::npos ||
      paramName.find("labels") != std::string::npos) {
    return "model";
  }
  if (paramName.find("mqtt") != std::string::npos ||
      paramName.find("kafka") != std::string::npos ||
      paramName.find("socket") != std::string::npos ||
      paramName.find("topic") != std::string::npos ||
      paramName.find("broker") != std::string::npos) {
    return "output";
  }
  if (paramName.find("osd") != std::string::npos ||
      paramName.find("font") != std::string::npos) {
    return "display";
  }
  return "general";
}
