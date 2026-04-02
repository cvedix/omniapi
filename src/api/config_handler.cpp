#include "api/config_handler.h"
#include "config/system_config.h"
#include "core/logger.h"
#include "core/logging_flags.h"
#include "core/metrics_interceptor.h"
#include <algorithm>
#include <chrono>
#include <drogon/HttpResponse.h>
#include <drogon/drogon.h>
#include <sstream>
#include <thread>

void ConfigHandler::getConfig(
    const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback) {
  // Set handler start time for accurate metrics
  MetricsInterceptor::setHandlerStartTime(req);

  // Check if path query parameter exists - if yes, route to getConfigSection
  std::string pathParam = req->getParameter("path");
  if (!pathParam.empty()) {
    getConfigSection(req, std::move(callback));
    return;
  }

  auto start_time = std::chrono::steady_clock::now();

  if (isApiLoggingEnabled()) {
    PLOG_INFO << "[API] GET /v1/securt/config - Get full configuration";
  }

  try {
    auto &config = SystemConfig::getInstance();
    Json::Value configJson = config.getConfigJson();

    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time);

    if (isApiLoggingEnabled()) {
      PLOG_INFO << "[API] GET /v1/securt/config - Success - " << duration.count()
                << "ms";
    }

    auto resp = createSuccessResponse(configJson);
    MetricsInterceptor::callWithMetrics(req, resp, std::move(callback));

  } catch (const std::exception &e) {
    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time);

    if (isApiLoggingEnabled()) {
      PLOG_ERROR << "[API] GET /v1/securt/config - Exception: " << e.what()
                 << " - " << duration.count() << "ms";
    }

    auto resp = createErrorResponse(500, "Internal server error", e.what());
    MetricsInterceptor::callWithMetrics(req, resp, std::move(callback));
  } catch (...) {
    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time);

    if (isApiLoggingEnabled()) {
      PLOG_ERROR << "[API] GET /v1/securt/config - Unknown exception - "
                 << duration.count() << "ms";
    }

    auto resp = createErrorResponse(500, "Internal server error",
                                    "Unknown error occurred");
    MetricsInterceptor::callWithMetrics(req, resp, std::move(callback));
  }
}

void ConfigHandler::getConfigSection(
    const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback) {
  auto start_time = std::chrono::steady_clock::now();

  // Extract path from query parameter (preferred) or URL path
  std::string path = req->getParameter("path");
  if (path.empty()) {
    path = extractPath(req);
  }

  if (isApiLoggingEnabled()) {
    PLOG_INFO << "[API] GET /v1/securt/config/" << path
              << " - Get configuration section";
  }

  try {
    if (path.empty()) {
      if (isApiLoggingEnabled()) {
        PLOG_WARNING << "[API] GET /v1/securt/config/{path} - Empty path";
      }
      callback(createErrorResponse(400, "Invalid request",
                                   "Path parameter is required"));
      return;
    }

    auto &config = SystemConfig::getInstance();
    Json::Value section = config.getConfigSection(path);

    if (section.isNull()) {
      auto end_time = std::chrono::steady_clock::now();
      auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
          end_time - start_time);

      if (isApiLoggingEnabled()) {
        PLOG_WARNING << "[API] GET /v1/securt/config/" << path
                     << " - Not found - " << duration.count() << "ms";
      }

      callback(createErrorResponse(404, "Not found",
                                   "Configuration section not found: " + path));
      return;
    }

    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time);

    if (isApiLoggingEnabled()) {
      PLOG_INFO << "[API] GET /v1/securt/config/" << path << " - Success - "
                << duration.count() << "ms";
    }

    callback(createSuccessResponse(section));

  } catch (const std::exception &e) {
    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time);

    if (isApiLoggingEnabled()) {
      PLOG_ERROR << "[API] GET /v1/securt/config/" << path
                 << " - Exception: " << e.what() << " - " << duration.count()
                 << "ms";
    }

    callback(createErrorResponse(500, "Internal server error", e.what()));
  } catch (...) {
    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time);

    if (isApiLoggingEnabled()) {
      PLOG_ERROR << "[API] GET /v1/securt/config/" << path
                 << " - Unknown exception - " << duration.count() << "ms";
    }

    callback(createErrorResponse(500, "Internal server error",
                                 "Unknown error occurred"));
  }
}

void ConfigHandler::createOrUpdateConfig(
    const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback) {
  auto start_time = std::chrono::steady_clock::now();

  if (isApiLoggingEnabled()) {
    PLOG_INFO << "[API] POST /v1/securt/config - Create or update configuration";
  }

  try {
    auto json = req->getJsonObject();
    if (!json) {
      if (isApiLoggingEnabled()) {
        PLOG_WARNING << "[API] POST /v1/securt/config - Error: Invalid JSON body";
      }
      callback(createErrorResponse(400, "Invalid request",
                                   "Request body must be valid JSON"));
      return;
    }

    std::string error;
    if (!validateConfigJson(*json, error)) {
      if (isApiLoggingEnabled()) {
        PLOG_WARNING << "[API] POST /v1/securt/config - Validation failed: "
                     << error;
      }
      callback(createErrorResponse(400, "Validation failed", error));
      return;
    }

    auto &config = SystemConfig::getInstance();

    // Get auto_restart option from request
    bool autoRestartRequested = getAutoRestartOption(req, json.get());

    // Get old web server config before update (for comparison)
    auto oldWebServerConfig = config.getWebServerConfig();

    if (!config.updateConfig(*json)) {
      if (isApiLoggingEnabled()) {
        PLOG_ERROR
            << "[API] POST /v1/securt/config - Failed to update configuration";
      }
      callback(createErrorResponse(500, "Internal server error",
                                   "Failed to update configuration"));
      return;
    }

    // Save to file
    if (!config.saveConfig()) {
      if (isApiLoggingEnabled()) {
        PLOG_WARNING << "[API] POST /v1/securt/config - Updated but failed to "
                        "save to file";
      }
    }

    // Check if web server config changed (port/host)
    auto newWebServerConfig = config.getWebServerConfig();
    bool configChanged = config.hasWebServerConfigChanged(newWebServerConfig);

    // Determine if restart is needed:
    // Only restart when auto_restart is explicitly set to true in the request
    bool needsRestart = autoRestartRequested;

    if (configChanged && !autoRestartRequested) {
      if (isApiLoggingEnabled()) {
        PLOG_INFO << "[API] POST /v1/securt/config - Web server config changed "
                     "(port/host), but auto_restart not requested. "
                     "Server restart required manually.";
      }
    }

    if (needsRestart) {
      std::string reason =
          "[API] POST /v1/securt/config - auto_restart requested";
      if (configChanged) {
        reason += " (web server config changed)";
        if (isApiLoggingEnabled()) {
          PLOG_WARNING << "[API] Old config: " << oldWebServerConfig.ipAddress
                       << ":" << oldWebServerConfig.port;
          PLOG_WARNING << "[API] New config: " << newWebServerConfig.ipAddress
                       << ":" << newWebServerConfig.port;
        }
      }
      scheduleRestartIfNeeded(true, reason, 3.0);
    } else {
      // Update current server config if no restart needed
      config.initializeCurrentServerConfig(newWebServerConfig);
    }

    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time);

    if (isApiLoggingEnabled()) {
      PLOG_INFO << "[API] POST /v1/securt/config - Success - " << duration.count()
                << "ms";
    }

    Json::Value response;
    response["message"] = "Configuration updated successfully";
    response["config"] = config.getConfigJson();

    if (needsRestart) {
      response["warning"] =
          "Server will restart in 3 seconds (auto_restart requested).";
      response["restart_scheduled"] = true;
    } else if (configChanged && !autoRestartRequested) {
      response["info"] =
          "Web server port/host changed. Please restart server manually to "
          "apply changes, or set auto_restart=true to restart automatically.";
      response["restart_required"] = true;
    }

    callback(createSuccessResponse(response, 200));

  } catch (const std::exception &e) {
    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time);

    if (isApiLoggingEnabled()) {
      PLOG_ERROR << "[API] POST /v1/securt/config - Exception: " << e.what()
                 << " - " << duration.count() << "ms";
    }

    callback(createErrorResponse(500, "Internal server error", e.what()));
  } catch (...) {
    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time);

    if (isApiLoggingEnabled()) {
      PLOG_ERROR << "[API] POST /v1/securt/config - Unknown exception - "
                 << duration.count() << "ms";
    }

    callback(createErrorResponse(500, "Internal server error",
                                 "Unknown error occurred"));
  }
}

void ConfigHandler::replaceConfig(
    const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback) {
  auto start_time = std::chrono::steady_clock::now();

  if (isApiLoggingEnabled()) {
    PLOG_INFO << "[API] PUT /v1/securt/config - Replace entire configuration";
  }

  try {
    auto json = req->getJsonObject();
    if (!json) {
      if (isApiLoggingEnabled()) {
        PLOG_WARNING << "[API] PUT /v1/securt/config - Error: Invalid JSON body";
      }
      callback(createErrorResponse(400, "Invalid request",
                                   "Request body must be valid JSON"));
      return;
    }

    std::string error;
    if (!validateConfigJson(*json, error)) {
      if (isApiLoggingEnabled()) {
        PLOG_WARNING << "[API] PUT /v1/securt/config - Validation failed: "
                     << error;
      }
      callback(createErrorResponse(400, "Validation failed", error));
      return;
    }

    auto &config = SystemConfig::getInstance();

    // Get auto_restart option from request
    bool autoRestartRequested = getAutoRestartOption(req, json.get());

    // Get old web server config before update (for comparison)
    auto oldWebServerConfig = config.getWebServerConfig();

    if (!config.replaceConfig(*json)) {
      if (isApiLoggingEnabled()) {
        PLOG_ERROR
            << "[API] PUT /v1/securt/config - Failed to replace configuration";
      }
      callback(createErrorResponse(500, "Internal server error",
                                   "Failed to replace configuration"));
      return;
    }

    // Save to file
    if (!config.saveConfig()) {
      if (isApiLoggingEnabled()) {
        PLOG_WARNING << "[API] PUT /v1/securt/config - Replaced but failed to "
                        "save to file";
      }
    }

    // Check if web server config changed (port/host)
    auto newWebServerConfig = config.getWebServerConfig();
    bool configChanged = config.hasWebServerConfigChanged(newWebServerConfig);

    // Determine if restart is needed:
    // Only restart when auto_restart is explicitly set to true in the request
    bool needsRestart = autoRestartRequested;

    if (configChanged && !autoRestartRequested) {
      if (isApiLoggingEnabled()) {
        PLOG_INFO << "[API] PUT /v1/securt/config - Web server config changed "
                     "(port/host), but auto_restart not requested. "
                     "Server restart required manually.";
      }
    }

    if (needsRestart) {
      std::string reason = "[API] PUT /v1/securt/config - auto_restart requested";
      if (configChanged) {
        reason += " (web server config changed)";
        if (isApiLoggingEnabled()) {
          PLOG_WARNING << "[API] Old config: " << oldWebServerConfig.ipAddress
                       << ":" << oldWebServerConfig.port;
          PLOG_WARNING << "[API] New config: " << newWebServerConfig.ipAddress
                       << ":" << newWebServerConfig.port;
        }
      }
      scheduleRestartIfNeeded(true, reason, 3.0);
    } else {
      // Update current server config if no restart needed
      config.initializeCurrentServerConfig(newWebServerConfig);
    }

    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time);

    if (isApiLoggingEnabled()) {
      PLOG_INFO << "[API] PUT /v1/securt/config - Success - " << duration.count()
                << "ms";
    }

    Json::Value response;
    response["message"] = "Configuration replaced successfully";
    response["config"] = config.getConfigJson();

    if (needsRestart) {
      response["warning"] =
          "Server will restart in 3 seconds (auto_restart requested).";
      response["restart_scheduled"] = true;
    } else if (configChanged && !autoRestartRequested) {
      response["info"] =
          "Web server port/host changed. Please restart server manually to "
          "apply changes, or set auto_restart=true to restart automatically.";
      response["restart_required"] = true;
    }

    callback(createSuccessResponse(response, 200));

  } catch (const std::exception &e) {
    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time);

    if (isApiLoggingEnabled()) {
      PLOG_ERROR << "[API] PUT /v1/securt/config - Exception: " << e.what()
                 << " - " << duration.count() << "ms";
    }

    callback(createErrorResponse(500, "Internal server error", e.what()));
  } catch (...) {
    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time);

    if (isApiLoggingEnabled()) {
      PLOG_ERROR << "[API] PUT /v1/securt/config - Unknown exception - "
                 << duration.count() << "ms";
    }

    callback(createErrorResponse(500, "Internal server error",
                                 "Unknown error occurred"));
  }
}

void ConfigHandler::updateConfigSection(
    const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback) {
  auto start_time = std::chrono::steady_clock::now();

  // Extract path from query parameter (preferred) or URL path
  std::string path = req->getParameter("path");
  if (path.empty()) {
    path = extractPath(req);
  }

  if (isApiLoggingEnabled()) {
    PLOG_INFO << "[API] PATCH /v1/securt/config/" << path
              << " - Update configuration section";
  }

  try {
    if (path.empty()) {
      if (isApiLoggingEnabled()) {
        PLOG_WARNING << "[API] PATCH /v1/securt/config/{path} - Empty path";
      }
      callback(createErrorResponse(400, "Invalid request",
                                   "Path parameter is required"));
      return;
    }

    auto json = req->getJsonObject();
    if (!json) {
      if (isApiLoggingEnabled()) {
        PLOG_WARNING << "[API] PATCH /v1/securt/config/" << path
                     << " - Error: Invalid JSON body";
      }
      callback(createErrorResponse(400, "Invalid request",
                                   "Request body must be valid JSON"));
      return;
    }

    auto &config = SystemConfig::getInstance();

    // Get auto_restart option from request
    bool autoRestartRequested = getAutoRestartOption(req, json.get());

    // Get old web server config before update (for comparison)
    // Only needed if path is related to web_server
    bool checkWebServer = (path.find("web_server") != std::string::npos ||
                           path.find("system") != std::string::npos);
    auto oldWebServerConfig = checkWebServer ? config.getWebServerConfig()
                                             : SystemConfig::WebServerConfig();

    if (!config.updateConfigSection(path, *json)) {
      if (isApiLoggingEnabled()) {
        PLOG_WARNING << "[API] PATCH /v1/securt/config/" << path
                     << " - Failed to update section";
      }
      callback(createErrorResponse(500, "Internal server error",
                                   "Failed to update configuration section"));
      return;
    }

    // Save to file
    if (!config.saveConfig()) {
      if (isApiLoggingEnabled()) {
        PLOG_WARNING << "[API] PATCH /v1/securt/config/" << path
                     << " - Updated but failed to save to file";
      }
    }

    // Check if web server config changed (port/host)
    // Only check if the path is related to web_server
    bool needsRestart = false;
    bool configChanged = false;

    if (checkWebServer) {
      auto newWebServerConfig = config.getWebServerConfig();
      configChanged = config.hasWebServerConfigChanged(newWebServerConfig);

      // Determine if restart is needed:
      // Only restart when auto_restart is explicitly set to true in the request
      needsRestart = autoRestartRequested;

      if (configChanged && !autoRestartRequested) {
        if (isApiLoggingEnabled()) {
          PLOG_INFO << "[API] PATCH /v1/securt/config/" << path
                    << " - Web server config changed (port/host), "
                       "but auto_restart not requested. "
                       "Server restart required manually.";
        }
      }

      if (needsRestart) {
        std::string reason =
            "[API] PATCH /v1/securt/config/" + path + " - auto_restart requested";
        if (configChanged) {
          reason += " (web server config changed)";
          if (isApiLoggingEnabled()) {
            PLOG_WARNING << "[API] Old config: " << oldWebServerConfig.ipAddress
                         << ":" << oldWebServerConfig.port;
            PLOG_WARNING << "[API] New config: " << newWebServerConfig.ipAddress
                         << ":" << newWebServerConfig.port;
          }
        }
        scheduleRestartIfNeeded(true, reason, 3.0);
      } else {
        // Update current server config if no restart needed
        auto newWebServerConfig = config.getWebServerConfig();
        config.initializeCurrentServerConfig(newWebServerConfig);
      }
    } else if (autoRestartRequested) {
      // If auto_restart is requested but path is not web_server related, still
      // restart
      std::string reason =
          "[API] PATCH /v1/securt/config/" + path + " - auto_restart requested";
      scheduleRestartIfNeeded(true, reason, 3.0);
      needsRestart = true;
    }

    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time);

    if (isApiLoggingEnabled()) {
      PLOG_INFO << "[API] PATCH /v1/securt/config/" << path << " - Success - "
                << duration.count() << "ms";
    }

    Json::Value response;
    response["message"] = "Configuration section updated successfully";
    response["path"] = path;
    response["value"] = config.getConfigSection(path);

    if (needsRestart) {
      response["warning"] =
          "Server will restart in 3 seconds (auto_restart requested).";
      response["restart_scheduled"] = true;
    } else if (checkWebServer && configChanged && !autoRestartRequested) {
      response["info"] =
          "Web server port/host changed. Please restart server manually to "
          "apply changes, or set auto_restart=true to restart automatically.";
      response["restart_required"] = true;
    }

    callback(createSuccessResponse(response, 200));

  } catch (const std::exception &e) {
    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time);

    if (isApiLoggingEnabled()) {
      PLOG_ERROR << "[API] PATCH /v1/securt/config/" << path
                 << " - Exception: " << e.what() << " - " << duration.count()
                 << "ms";
    }

    callback(createErrorResponse(500, "Internal server error", e.what()));
  } catch (...) {
    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time);

    if (isApiLoggingEnabled()) {
      PLOG_ERROR << "[API] PATCH /v1/securt/config/" << path
                 << " - Unknown exception - " << duration.count() << "ms";
    }

    callback(createErrorResponse(500, "Internal server error",
                                 "Unknown error occurred"));
  }
}

void ConfigHandler::deleteConfigSection(
    const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback) {
  auto start_time = std::chrono::steady_clock::now();

  // Extract path from query parameter (preferred) or URL path
  // Path supports both forward slashes (/) and dots (.) as separators
  // Examples: "system/web_server" or "system.web_server"
  std::string path = req->getParameter("path");
  if (path.empty()) {
    path = extractPath(req);
  }

  if (isApiLoggingEnabled()) {
    PLOG_INFO << "[API] DELETE /v1/securt/config/" << path
              << " - Delete configuration section";
  }

  try {
    if (path.empty()) {
      if (isApiLoggingEnabled()) {
        PLOG_WARNING << "[API] DELETE /v1/securt/config/{path} - Empty path";
      }
      callback(createErrorResponse(400, "Invalid request",
                                   "Path parameter is required"));
      return;
    }

    auto &config = SystemConfig::getInstance();

    // Get auto_restart option from request (DELETE doesn't have JSON body, only
    // query param)
    bool autoRestartRequested = getAutoRestartOption(req, nullptr);

    // Check if path is related to web_server before deletion
    bool checkWebServer = (path.find("web_server") != std::string::npos ||
                           path.find("system") != std::string::npos);
    auto oldWebServerConfig = checkWebServer ? config.getWebServerConfig()
                                             : SystemConfig::WebServerConfig();

    if (!config.deleteConfigSection(path)) {
      auto end_time = std::chrono::steady_clock::now();
      auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
          end_time - start_time);

      if (isApiLoggingEnabled()) {
        PLOG_WARNING << "[API] DELETE /v1/securt/config/" << path
                     << " - Not found - " << duration.count() << "ms";
      }

      callback(createErrorResponse(404, "Not found",
                                   "Configuration section not found: " + path));
      return;
    }

    // Save to file
    if (!config.saveConfig()) {
      if (isApiLoggingEnabled()) {
        PLOG_WARNING << "[API] DELETE /v1/securt/config/" << path
                     << " - Deleted but failed to save to file";
      }
    }

    // Check if web server config changed after deletion
    bool needsRestart = false;
    bool configChanged = false;

    if (checkWebServer) {
      auto newWebServerConfig = config.getWebServerConfig();
      configChanged = config.hasWebServerConfigChanged(newWebServerConfig);

      // Determine if restart is needed:
      // Only restart when auto_restart is explicitly set to true in the request
      needsRestart = autoRestartRequested;

      if (configChanged && !autoRestartRequested) {
        if (isApiLoggingEnabled()) {
          PLOG_INFO << "[API] DELETE /v1/securt/config/" << path
                    << " - Web server config changed (port/host), "
                       "but auto_restart not requested. "
                       "Server restart required manually.";
        }
      }

      if (needsRestart) {
        std::string reason = "[API] DELETE /v1/securt/config/" + path +
                             " - auto_restart requested";
        if (configChanged) {
          reason += " (web server config changed)";
          if (isApiLoggingEnabled()) {
            PLOG_WARNING << "[API] Old config: " << oldWebServerConfig.ipAddress
                         << ":" << oldWebServerConfig.port;
            PLOG_WARNING << "[API] New config: " << newWebServerConfig.ipAddress
                         << ":" << newWebServerConfig.port;
          }
        }
        scheduleRestartIfNeeded(true, reason, 3.0);
      }
    } else if (autoRestartRequested) {
      // If auto_restart is requested but path is not web_server related, still
      // restart
      std::string reason =
          "[API] DELETE /v1/securt/config/" + path + " - auto_restart requested";
      scheduleRestartIfNeeded(true, reason, 3.0);
      needsRestart = true;
    }

    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time);

    if (isApiLoggingEnabled()) {
      PLOG_INFO << "[API] DELETE /v1/securt/config/" << path << " - Success - "
                << duration.count() << "ms";
    }

    Json::Value response;
    response["message"] = "Configuration section deleted successfully";
    response["path"] = path;

    if (needsRestart) {
      response["warning"] =
          "Server will restart in 3 seconds (auto_restart requested).";
      response["restart_scheduled"] = true;
    } else if (checkWebServer && configChanged && !autoRestartRequested) {
      response["info"] =
          "Web server port/host changed. Please restart server manually to "
          "apply changes, or set auto_restart=true to restart automatically.";
      response["restart_required"] = true;
    }

    callback(createSuccessResponse(response, 200));

  } catch (const std::exception &e) {
    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time);

    if (isApiLoggingEnabled()) {
      PLOG_ERROR << "[API] DELETE /v1/securt/config/" << path
                 << " - Exception: " << e.what() << " - " << duration.count()
                 << "ms";
    }

    callback(createErrorResponse(500, "Internal server error", e.what()));
  } catch (...) {
    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time);

    if (isApiLoggingEnabled()) {
      PLOG_ERROR << "[API] DELETE /v1/securt/config/" << path
                 << " - Unknown exception - " << duration.count() << "ms";
    }

    callback(createErrorResponse(500, "Internal server error",
                                 "Unknown error occurred"));
  }
}

void ConfigHandler::resetConfig(
    const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback) {
  auto start_time = std::chrono::steady_clock::now();

  if (isApiLoggingEnabled()) {
    PLOG_INFO
        << "[API] POST /v1/securt/config/reset - Reset configuration to defaults";
  }

  try {
    auto &config = SystemConfig::getInstance();

    // Get auto_restart option from request (can be in query param or JSON body)
    auto json = req->getJsonObject();
    bool autoRestartRequested = getAutoRestartOption(req, json.get());

    // Get old web server config before reset (for comparison)
    auto oldWebServerConfig = config.getWebServerConfig();

    if (!config.resetToDefaults()) {
      auto end_time = std::chrono::steady_clock::now();
      auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
          end_time - start_time);

      if (isApiLoggingEnabled()) {
        PLOG_WARNING << "[API] POST /v1/securt/config/reset - Failed to reset "
                        "configuration - "
                     << duration.count() << "ms";
      }

      callback(
          createErrorResponse(500, "Internal server error",
                              "Failed to reset configuration to defaults"));
      return;
    }

    // Save to file
    if (!config.saveConfig()) {
      if (isApiLoggingEnabled()) {
        PLOG_WARNING << "[API] POST /v1/securt/config/reset - Reset but failed "
                        "to save to file";
      }
    }

    // Check if web server config changed after reset
    auto newWebServerConfig = config.getWebServerConfig();
    bool configChanged = config.hasWebServerConfigChanged(newWebServerConfig);

    // Determine if restart is needed:
    // Only restart when auto_restart is explicitly set to true in the request
    bool needsRestart = autoRestartRequested;

    if (configChanged && !autoRestartRequested) {
      if (isApiLoggingEnabled()) {
        PLOG_INFO
            << "[API] POST /v1/securt/config/reset - Web server config changed "
               "(port/host), but auto_restart not requested. "
               "Server restart required manually.";
      }
    }

    if (needsRestart) {
      std::string reason =
          "[API] POST /v1/securt/config/reset - auto_restart requested";
      if (configChanged) {
        reason += " (web server config changed)";
        if (isApiLoggingEnabled()) {
          PLOG_WARNING << "[API] Old config: " << oldWebServerConfig.ipAddress
                       << ":" << oldWebServerConfig.port;
          PLOG_WARNING << "[API] New config: " << newWebServerConfig.ipAddress
                       << ":" << newWebServerConfig.port;
        }
      }
      scheduleRestartIfNeeded(true, reason, 3.0);
    } else {
      // Update current server config if no restart needed
      config.initializeCurrentServerConfig(newWebServerConfig);
    }

    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time);

    if (isApiLoggingEnabled()) {
      PLOG_INFO << "[API] POST /v1/securt/config/reset - Success - "
                << duration.count() << "ms";
    }

    Json::Value response;
    response["message"] = "Configuration reset to defaults successfully";
    response["config"] = config.getConfigJson();

    if (needsRestart) {
      response["warning"] =
          "Server will restart in 3 seconds (auto_restart requested).";
      response["restart_scheduled"] = true;
    } else if (configChanged && !autoRestartRequested) {
      response["info"] =
          "Web server port/host changed. Please restart server manually to "
          "apply changes, or set auto_restart=true to restart automatically.";
      response["restart_required"] = true;
    }

    callback(createSuccessResponse(response, 200));

  } catch (const std::exception &e) {
    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time);

    if (isApiLoggingEnabled()) {
      PLOG_ERROR << "[API] POST /v1/securt/config/reset - Exception: " << e.what()
                 << " - " << duration.count() << "ms";
    }

    callback(createErrorResponse(500, "Internal server error", e.what()));
  } catch (...) {
    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time);

    if (isApiLoggingEnabled()) {
      PLOG_ERROR << "[API] POST /v1/securt/config/reset - Unknown exception - "
                 << duration.count() << "ms";
    }

    callback(createErrorResponse(500, "Internal server error",
                                 "Unknown error occurred"));
  }
}

void ConfigHandler::handleOptions(
    const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback) {
  // Set handler start time for accurate metrics
  MetricsInterceptor::setHandlerStartTime(req);

  auto resp = HttpResponse::newHttpResponse();
  resp->setStatusCode(k200OK);
  resp->addHeader("Access-Control-Allow-Origin", "*");
  resp->addHeader("Access-Control-Allow-Methods",
                  "GET, POST, PUT, PATCH, DELETE, OPTIONS");
  resp->addHeader("Access-Control-Allow-Headers",
                  "Content-Type, Authorization");
  resp->addHeader("Access-Control-Max-Age", "3600");

  // Record metrics and call callback
  MetricsInterceptor::callWithMetrics(req, resp, std::move(callback));
}

std::string ConfigHandler::extractPath(const HttpRequestPtr &req) const {
  // First, try query parameter (most reliable for paths with slashes)
  std::string path = req->getParameter("path");

  // If not in query parameter, try to extract from URL path
  if (path.empty()) {
    std::string fullPath = req->getPath();
    size_t configPos = fullPath.find("/config/");

    if (configPos != std::string::npos) {
      size_t start = configPos + 8; // length of "/config/"
      path = fullPath.substr(start);
    }
  }

  // URL decode the path if it contains encoded characters
  if (!path.empty()) {
    std::string decoded;
    decoded.reserve(path.length());
    for (size_t i = 0; i < path.length(); ++i) {
      if (path[i] == '%' && i + 2 < path.length()) {
        // Try to decode hex value
        char hex[3] = {path[i + 1], path[i + 2], '\0'};
        char *end;
        unsigned long value = std::strtoul(hex, &end, 16);
        if (*end == '\0' && value <= 255) {
          decoded += static_cast<char>(value);
          i += 2; // Skip the hex digits
        } else {
          decoded += path[i]; // Invalid encoding, keep as-is
        }
      } else {
        decoded += path[i];
      }
    }
    path = decoded;
  }

  return path;
}

HttpResponsePtr
ConfigHandler::createErrorResponse(int statusCode, const std::string &error,
                                   const std::string &message) const {
  Json::Value errorJson;
  errorJson["error"] = error;
  if (!message.empty()) {
    errorJson["message"] = message;
  }

  auto resp = HttpResponse::newHttpJsonResponse(errorJson);
  resp->setStatusCode(static_cast<HttpStatusCode>(statusCode));

  // Add CORS headers to error responses
  resp->addHeader("Access-Control-Allow-Origin", "*");
  resp->addHeader("Access-Control-Allow-Methods",
                  "GET, POST, PUT, PATCH, DELETE, OPTIONS");
  resp->addHeader("Access-Control-Allow-Headers",
                  "Content-Type, Authorization");

  return resp;
}

HttpResponsePtr ConfigHandler::createSuccessResponse(const Json::Value &data,
                                                     int statusCode) const {
  auto resp = HttpResponse::newHttpJsonResponse(data);
  resp->setStatusCode(static_cast<HttpStatusCode>(statusCode));

  // Add CORS headers
  resp->addHeader("Access-Control-Allow-Origin", "*");
  resp->addHeader("Access-Control-Allow-Methods",
                  "GET, POST, PUT, PATCH, DELETE, OPTIONS");
  resp->addHeader("Access-Control-Allow-Headers",
                  "Content-Type, Authorization");

  return resp;
}

bool ConfigHandler::validateConfigJson(const Json::Value &json,
                                       std::string &error) const {
  // Basic validation - must be an object
  if (!json.isObject()) {
    error = "Configuration must be a JSON object";
    return false;
  }

  // Optional: Add more specific validations here
  // For now, we allow any valid JSON object

  return true;
}

bool ConfigHandler::getAutoRestartOption(const HttpRequestPtr &req,
                                         const Json::Value *jsonBody) const {
  // First, check query parameter
  std::string autoRestartParam = req->getParameter("auto_restart");
  if (!autoRestartParam.empty()) {
    // Convert to lowercase for case-insensitive comparison
    std::transform(autoRestartParam.begin(), autoRestartParam.end(),
                   autoRestartParam.begin(), ::tolower);
    return (autoRestartParam == "true" || autoRestartParam == "1" ||
            autoRestartParam == "yes");
  }

  // If not in query param, check JSON body
  if (jsonBody && jsonBody->isObject()) {
    if (jsonBody->isMember("auto_restart")) {
      const Json::Value &autoRestart = (*jsonBody)["auto_restart"];
      if (autoRestart.isBool()) {
        return autoRestart.asBool();
      } else if (autoRestart.isString()) {
        std::string value = autoRestart.asString();
        std::transform(value.begin(), value.end(), value.begin(), ::tolower);
        return (value == "true" || value == "1" || value == "yes");
      } else if (autoRestart.isInt()) {
        return autoRestart.asInt() != 0;
      }
    }
  }

  // Default: no auto restart
  return false;
}

void ConfigHandler::scheduleRestartIfNeeded(bool shouldRestart,
                                            const std::string &reason,
                                            double delaySeconds) const {
  if (!shouldRestart) {
    return;
  }

  if (isApiLoggingEnabled()) {
    if (!reason.empty()) {
      PLOG_WARNING << "[API] " << reason << ", scheduling graceful restart in "
                   << delaySeconds << " seconds";
    } else {
      PLOG_WARNING << "[API] Scheduling graceful restart in " << delaySeconds
                   << " seconds";
    }
  }

  // Schedule graceful restart after delay (allow response to be sent)
  auto *loop = drogon::app().getLoop();
  if (loop) {
    loop->runAfter(delaySeconds, []() {
      PLOG_INFO << "[Server] Graceful restart triggered by config change";
      PLOG_INFO << "[Server] Note: If using systemd/service manager, server "
                   "will auto-restart";
      auto &app = drogon::app();
      app.quit();
    });
  }
}
