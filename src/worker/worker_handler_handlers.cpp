#include "worker/worker_handler.h"
#include "worker/worker_json_utils.h"
#include "core/env_config.h"
#include "core/pipeline_builder.h"
#include "core/pipeline_builder_request_utils.h"
#include "core/pipeline_snapshot.h"
#include "core/runtime_update_log.h"
#include "core/timeout_constants.h"
#include "models/create_instance_request.h"
#include "solutions/solution_registry.h"
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <sys/socket.h>
#include <unistd.h>
#include <cvedix/nodes/common/cvedix_node.h>
#include <cvedix/nodes/des/cvedix_app_des_node.h>
#include <cvedix/nodes/des/cvedix_rtmp_des_node.h>
#include <cvedix/nodes/osd/cvedix_ba_line_crossline_osd_node.h>
#include <cvedix/nodes/ba/cvedix_ba_line_crossline_node.h>
#include <cvedix/objects/shapes/cvedix_line.h>
#include <cvedix/objects/shapes/cvedix_point.h>
#include <cvedix/nodes/osd/cvedix_ba_area_jam_osd_node.h>
#include <cvedix/nodes/osd/cvedix_ba_stop_osd_node.h>
#include <cvedix/nodes/osd/cvedix_face_osd_node_v2.h>
#include <cvedix/nodes/osd/cvedix_osd_node_v3.h>
#include <cvedix/nodes/src/cvedix_app_src_node.h>
#include <cvedix/nodes/src/cvedix_file_src_node.h>
#include <cvedix/nodes/src/cvedix_image_src_node.h>
#include <cvedix/nodes/src/cvedix_rtmp_src_node.h>
#include <cvedix/nodes/src/cvedix_rtsp_src_node.h>
#include <cvedix/nodes/src/cvedix_udp_src_node.h>
#include <cvedix/objects/cvedix_frame_meta.h>
#include <cvedix/objects/cvedix_meta.h>
#include <filesystem>
#include <future>
#include <mutex>
#include <set>
#include <getopt.h>
#include <iostream>
#include <iomanip>
#include <opencv2/imgcodecs.hpp>
#include <sstream>
#include <thread>

namespace worker {

IPCMessage WorkerHandler::handleCreateInstance(const IPCMessage &msg) {
  IPCMessage response;
  response.type = MessageType::CREATE_INSTANCE_RESPONSE;

  if (getActivePipeline() && !getActivePipeline()->empty()) {
    response.payload = createErrorResponse("Instance already exists",
                                           ResponseStatus::ALREADY_EXISTS);
    return response;
  }

  if (msg.payload.isMember("config")) {
    config_ = msg.payload["config"];
  }

  if (!buildPipeline()) {
    response.payload =
        createErrorResponse("Failed to build pipeline: " + last_error_,
                            ResponseStatus::INTERNAL_ERROR);
    return response;
  }

  response.payload = createResponse(ResponseStatus::OK, "Instance created");
  response.payload["data"]["instance_id"] = instance_id_;
  return response;
}

IPCMessage WorkerHandler::handleDeleteInstance(const IPCMessage & /*msg*/) {
  IPCMessage response;
  response.type = MessageType::DELETE_INSTANCE_RESPONSE;

  stopPipeline();
  cleanupPipeline();

  response.payload = createResponse(ResponseStatus::OK, "Instance deleted");
  shutdown_requested_.store(true);
  return response;
}

IPCMessage WorkerHandler::handleStartInstance(const IPCMessage & /*msg*/) {
  std::cout << "[Worker:" << instance_id_ << "] Received START_INSTANCE request"
            << std::endl;

  IPCMessage response;
  response.type = MessageType::START_INSTANCE_RESPONSE;

  if (!getActivePipeline() || getActivePipeline()->empty()) {
    if (!config_.isNull() &&
        (config_.isMember("Solution") || config_.isMember("SolutionId") ||
         (config_.isMember("AdditionalParams") && config_["AdditionalParams"].isObject()) ||
         (config_.isMember("additionalParams") && config_["additionalParams"].isObject()))) {
      std::cout << "[Worker:" << instance_id_
                << "] START_INSTANCE: No pipeline yet, building from config first"
                << std::endl;
      if (!buildPipeline()) {
        std::cerr << "[Worker:" << instance_id_
                  << "] START_INSTANCE: Build failed: " << last_error_
                  << std::endl;
        response.payload = createErrorResponse(
            "Failed to build pipeline: " + last_error_,
            ResponseStatus::INTERNAL_ERROR);
        return response;
      }
      std::cout << "[Worker:" << instance_id_
                << "] START_INSTANCE: Pipeline built, proceeding to start"
                << std::endl;
    } else {
      std::cout << "[Worker:" << instance_id_
                << "] START_INSTANCE: No pipeline configured and no config to build"
                << std::endl;
      response.payload = createErrorResponse("No pipeline configured",
                                             ResponseStatus::NOT_FOUND);
      return response;
    }
  }

  bool already_running = false;
  bool already_starting = false;
  {
    std::lock_guard<std::mutex> lock(start_pipeline_mutex_);
    already_running = pipeline_running_.load();
    if (already_running) {
      std::cout << "[Worker:" << instance_id_
                << "] START_INSTANCE: Pipeline already running, returning error"
                << std::endl;
    } else {
      already_starting = starting_pipeline_.load();
      if (already_starting) {
        std::cout << "[Worker:" << instance_id_
                  << "] START_INSTANCE: Pipeline already starting, returning error"
                  << std::endl;
      } else {
        starting_pipeline_.store(true);
        std::cout << "[Worker:" << instance_id_
                  << "] START_INSTANCE: Starting pipeline async" << std::endl;
      }
    }
  }

  if (already_running) {
    response.payload = createErrorResponse("Pipeline already running",
                                           ResponseStatus::ALREADY_EXISTS);
    return response;
  }
  if (already_starting) {
    response.payload = createErrorResponse("Pipeline is already starting",
                                           ResponseStatus::ALREADY_EXISTS);
    return response;
  }

  {
    std::lock_guard<std::shared_mutex> lock(state_mutex_);
    current_state_ = "starting";
  }
  startPipelineAsync();

  response.payload = createResponse(ResponseStatus::OK, "Instance starting");
  std::cout << "[Worker:" << instance_id_
            << "] START_INSTANCE: Response sent, pipeline starting in background"
            << std::endl;
  return response;
}

IPCMessage WorkerHandler::handleStopInstance(const IPCMessage & /*msg*/) {
  std::cout << "[Worker:" << instance_id_ << "] Received STOP_INSTANCE request"
            << std::endl;

  IPCMessage response;
  response.type = MessageType::STOP_INSTANCE_RESPONSE;

  bool already_stopping = false;
  bool not_running = false;
  {
    std::lock_guard<std::mutex> lock(stop_pipeline_mutex_);
    not_running = !pipeline_running_.load();
    if (!not_running) {
      already_stopping = stopping_pipeline_.load();
      if (!already_stopping) {
        stopping_pipeline_.store(true);
        std::cout << "[Worker:" << instance_id_
                  << "] STOP_INSTANCE: Stopping pipeline async" << std::endl;
      }
    }
  }

  if (not_running) {
    response.payload =
        createErrorResponse("Pipeline not running", ResponseStatus::NOT_FOUND);
    return response;
  }
  if (already_stopping) {
    response.payload = createErrorResponse("Pipeline is already stopping",
                                           ResponseStatus::ALREADY_EXISTS);
    return response;
  }

  {
    std::lock_guard<std::shared_mutex> lock(state_mutex_);
    current_state_ = "stopping";
  }
  stopPipelineAsync();

  response.payload = createResponse(ResponseStatus::OK, "Instance stopping");
  std::cout << "[Worker:" << instance_id_
            << "] STOP_INSTANCE: Response sent, pipeline stopping in background"
            << std::endl;
  return response;
}

IPCMessage WorkerHandler::handleUpdateInstance(const IPCMessage &msg) {
  // RUNTIME UPDATE GUARANTEE (no restart): When (1) the only changed params
  // are line-related (CrossingLines, CROSSLINE_START_X/Y, CROSSLINE_END_X/Y)
  // and (2) pipeline has ba_crossline_node, we apply via set_lines() and
  // return OK without rebuild/hot-swap. All other changes go through
  // checkIfNeedsRebuild / applyConfigToPipeline and may trigger hot-swap or
  // rebuild. See INSTANCE_UPDATE_HOT_RELOAD_MANUAL_TEST.md "Phân tích thuật toán".
  logRuntimeUpdate(instance_id_, "UPDATE_INSTANCE received");
  IPCMessage response;
  response.type = MessageType::UPDATE_INSTANCE_RESPONSE;

  if (!msg.payload.isMember("config")) {
    response.payload = createErrorResponse("No config provided",
                                           ResponseStatus::INVALID_REQUEST);
    return response;
  }

  // Store old config for comparison
  Json::Value oldConfig = config_;

  // PATCH semantics: only update fields present in the body; leave everything else unchanged.
  const auto &newConfig = msg.payload["config"];
  worker::mergeJsonInto(config_, newConfig);
  if (config_.isMember("additionalParams") && config_["additionalParams"].isObject()) {
    if (!config_.isMember("AdditionalParams") || !config_["AdditionalParams"].isObject()) {
      config_["AdditionalParams"] = Json::Value(Json::objectValue);
    }
    worker::mergeJsonInto(config_["AdditionalParams"], config_["additionalParams"]);
    worker::mergeJsonInto(config_["additionalParams"], config_["AdditionalParams"]);
  } else if (config_.isMember("AdditionalParams") && config_["AdditionalParams"].isObject()) {
    if (!config_.isMember("additionalParams") || !config_["additionalParams"].isObject()) {
      config_["additionalParams"] = Json::Value(Json::objectValue);
    }
    worker::mergeJsonInto(config_["additionalParams"], config_["AdditionalParams"]);
  }
  if (oldConfig.isMember("RtmpUrl") && !oldConfig["RtmpUrl"].asString().empty()) {
    if (!config_.isMember("RtmpUrl") || config_["RtmpUrl"].asString().empty()) {
      config_["RtmpUrl"] = oldConfig["RtmpUrl"];
      if (config_["AdditionalParams"].isObject()) config_["AdditionalParams"]["RTMP_URL"] = oldConfig["RtmpUrl"];
      if (config_["additionalParams"].isObject()) config_["additionalParams"]["RTMP_URL"] = oldConfig["RtmpUrl"];
    }
  }
  if (oldConfig.isMember("RtspUrl") && !oldConfig["RtspUrl"].asString().empty()) {
    if (!config_.isMember("RtspUrl") || config_["RtspUrl"].asString().empty()) {
      config_["RtspUrl"] = oldConfig["RtspUrl"];
      if (config_["AdditionalParams"].isObject()) config_["AdditionalParams"]["RTSP_URL"] = oldConfig["RtspUrl"];
      if (config_["additionalParams"].isObject()) config_["additionalParams"]["RTSP_URL"] = oldConfig["RtspUrl"];
    }
  }
  if (oldConfig.isMember("AdditionalParams") && oldConfig["AdditionalParams"].isObject()) {
    for (const char* key : {"RTMP_URL", "RTMP_DES_URL", "RTSP_URL"}) {
      if (oldConfig["AdditionalParams"].isMember(key) && !oldConfig["AdditionalParams"][key].asString().empty()) {
        if (!config_["AdditionalParams"].isMember(key) || config_["AdditionalParams"][key].asString().empty()) {
          config_["AdditionalParams"][key] = oldConfig["AdditionalParams"][key];
          if (config_["additionalParams"].isObject()) config_["additionalParams"][key] = oldConfig["AdditionalParams"][key];
        }
      }
    }
  }
  if (oldConfig.isMember("additionalParams") && oldConfig["additionalParams"].isObject() &&
      oldConfig["additionalParams"].isMember("output") && oldConfig["additionalParams"]["output"].isObject()) {
    const auto& oldOutput = oldConfig["additionalParams"]["output"];
    bool oldHasRtmp = (oldOutput.isMember("RTMP_DES_URL") && !oldOutput["RTMP_DES_URL"].asString().empty()) ||
                      (oldOutput.isMember("RTMP_URL") && !oldOutput["RTMP_URL"].asString().empty());
    if (oldHasRtmp) {
      if (!config_.isMember("additionalParams") || !config_["additionalParams"].isObject()) {
        config_["additionalParams"] = Json::Value(Json::objectValue);
      }
      Json::Value& newOutput = config_["additionalParams"]["output"];
      std::string newRtmp = newOutput.isObject() && newOutput.isMember("RTMP_DES_URL")
          ? newOutput["RTMP_DES_URL"].asString()
          : (newOutput.isObject() && newOutput.isMember("RTMP_URL") ? newOutput["RTMP_URL"].asString() : "");
      bool needPreserve = !newOutput.isObject() || newOutput.empty() ||
                          (newRtmp.empty() && oldHasRtmp);
      if (needPreserve) {
        config_["additionalParams"]["output"] = oldOutput;
        if (config_.isMember("AdditionalParams") && config_["AdditionalParams"].isObject()) {
          if (oldOutput.isMember("RTMP_DES_URL") && !oldOutput["RTMP_DES_URL"].asString().empty()) {
            config_["AdditionalParams"]["RTMP_DES_URL"] = oldOutput["RTMP_DES_URL"];
            config_["AdditionalParams"]["RTMP_URL"] = oldOutput["RTMP_DES_URL"];
          }
          if (oldOutput.isMember("RTMP_URL") && !oldOutput["RTMP_URL"].asString().empty()) {
            config_["AdditionalParams"]["RTMP_URL"] = oldOutput["RTMP_URL"];
          }
        }
        if (!config_.isMember("RtmpUrl") || config_["RtmpUrl"].asString().empty()) {
          if (oldOutput.isMember("RTMP_DES_URL") && !oldOutput["RTMP_DES_URL"].asString().empty()) {
            config_["RtmpUrl"] = oldOutput["RTMP_DES_URL"];
          } else if (oldOutput.isMember("RTMP_URL") && !oldOutput["RTMP_URL"].asString().empty()) {
            config_["RtmpUrl"] = oldOutput["RTMP_URL"];
          }
        }
      }
    }
  }
  logRuntimeUpdate(instance_id_, "config merged (deep merge)");

  if (!pipeline_running_.load() || !getActivePipeline() || getActivePipeline()->empty()) {
    std::cout << "[Worker:" << instance_id_
              << "] Config updated (pipeline not running, will apply on start)"
              << std::endl;
    response.payload = createResponse(ResponseStatus::OK, "Instance updated");
    return response;
  }

  {
    Json::Value oldParams = getParamsFromConfig(oldConfig);
    Json::Value newParams = getParamsFromConfig(config_);
    static const std::set<std::string> lineKeys = {
        "CrossingLines", "CROSSLINE_START_X", "CROSSLINE_START_Y",
        "CROSSLINE_END_X", "CROSSLINE_END_Y"};
    bool onlyLineParamsChanged = true;
    for (const auto &key : newParams.getMemberNames()) {
      if (lineKeys.count(key)) continue;
      std::string oldVal = oldParams.isMember(key) ? oldParams[key].asString() : "";
      std::string newVal = newParams[key].asString();
      if (oldVal != newVal) { onlyLineParamsChanged = false; break; }
    }
    if (onlyLineParamsChanged) {
      for (const auto &key : oldParams.getMemberNames()) {
        if (lineKeys.count(key)) continue;
        if (!newParams.isMember(key)) continue;
        if (newParams[key].asString() != oldParams[key].asString()) {
          onlyLineParamsChanged = false; break;
        }
      }
    }
    if (!onlyLineParamsChanged) {
      logRuntimeUpdate(instance_id_, "update=not_line_only -> may hot-swap");
    }
    if (onlyLineParamsChanged) {
      // Workaround: when we have persistent RTMP output (frame_router_/output_leg_),
      // set_lines() can block >IPC timeout and/or cause RTMP stream to stop (SDK behavior).
      // Use hot-swap instead so the new pipeline is built with new CrossingLines from config_
      // and the persistent output leg stays connected (zero-downtime).
      if (frame_router_ && output_leg_) {
        std::cout << "[Worker:" << instance_id_
                  << "] Line-only update but RTMP output present -> hot-swap to preserve stream (avoid set_lines() blocking/losing output)"
                  << std::endl;
        logRuntimeUpdate(instance_id_, "update=line_only with RTMP -> hot_swap to preserve stream");
        if (pipeline_running_.load() && hotSwapPipeline(config_)) {
          response.payload = createResponse(ResponseStatus::OK, "Instance updated (hot swap, lines applied)");
          return response;
        }
        // If hot-swap failed, try set_lines() as fallback
        if (applyLinesFromParamsToPipeline(newParams)) {
          response.payload = createResponse(ResponseStatus::OK, "Instance updated (runtime)");
          return response;
        }
        response.payload = createResponse(ResponseStatus::OK, "Instance updated (lines will apply on restart)");
        return response;
      } else {
        std::cout << "[Worker:" << instance_id_
                  << "] Line-only update: applying CrossingLines at runtime (no hot-swap)"
                  << std::endl;
        logRuntimeUpdate(instance_id_, "update=line_only (no hot swap)");
        bool linesApplied = applyLinesFromParamsToPipeline(newParams);
        if (linesApplied) {
          response.payload = createResponse(ResponseStatus::OK, "Instance updated (runtime)");
          return response;
        }
        std::cerr << "[Worker:" << instance_id_
                  << "] Failed to apply lines at runtime (config saved, will apply on next start)"
                  << std::endl;
        response.payload = createResponse(ResponseStatus::OK, "Instance updated (lines will apply on restart)");
        return response;
      }
    }
  }

  std::cout << "[Worker:" << instance_id_
            << "] Applying config changes to running pipeline..." << std::endl;
  bool needsRebuild = checkIfNeedsRebuild(oldConfig, config_);
  logRuntimeUpdate(instance_id_, "checkIfNeedsRebuild=" + std::string(needsRebuild ? "true" : "false"));
  bool canApplyRuntime = applyConfigToPipeline(oldConfig, config_);
  logRuntimeUpdate(instance_id_, "applyConfigToPipeline=" + std::string(canApplyRuntime ? "true" : "false"));

  if (needsRebuild || !canApplyRuntime) {
    std::cout << "[Worker:" << instance_id_
              << "] Config changes require pipeline rebuild, using hot swap..."
              << std::endl;
    logRuntimeUpdate(instance_id_, "decision=hot_swap_or_rebuild");
    if (pipeline_running_.load()) {
      if (hotSwapPipeline(config_)) {
        std::cout << "[Worker:" << instance_id_
                  << "] ✓ Pipeline hot-swapped successfully (zero downtime)"
                  << std::endl;
        logRuntimeUpdate(instance_id_, "result=hot_swap_ok");
        response.payload = createResponse(ResponseStatus::OK, "Instance updated (hot swap)");
      } else {
        std::cerr << "[Worker:" << instance_id_
                  << "] Hot swap failed, falling back to traditional rebuild"
                  << std::endl;
        stopPipeline();
        if (!buildPipeline()) {
          last_error_ = "Failed to rebuild pipeline: " + last_error_;
          std::cerr << "[Worker:" << instance_id_ << "] " << last_error_ << std::endl;
          response.payload = createErrorResponse(last_error_, ResponseStatus::INTERNAL_ERROR);
          return response;
        }
        if (!startPipeline()) {
          last_error_ = "Failed to restart pipeline: " + last_error_;
          std::cerr << "[Worker:" << instance_id_ << "] " << last_error_ << std::endl;
          response.payload = createErrorResponse(last_error_, ResponseStatus::INTERNAL_ERROR);
          return response;
        }
        logRuntimeUpdate(instance_id_, "result=rebuild_fallback (hot_swap failed)");
        response.payload = createResponse(ResponseStatus::OK, "Instance updated (rebuild fallback)");
      }
    } else {
      std::cout << "[Worker:" << instance_id_ << "] Rebuilding pipeline..." << std::endl;
      if (!buildPipeline()) {
        last_error_ = "Failed to rebuild pipeline: " + last_error_;
        std::cerr << "[Worker:" << instance_id_ << "] " << last_error_ << std::endl;
        response.payload = createErrorResponse(last_error_, ResponseStatus::INTERNAL_ERROR);
        return response;
      }
      std::cout << "[Worker:" << instance_id_
                << "] ✓ Pipeline rebuilt successfully (was not running)"
                << std::endl;
      response.payload = createResponse(ResponseStatus::OK, "Instance updated and pipeline rebuilt");
    }
    return response;
  }

  std::cout << "[Worker:" << instance_id_
            << "] ✓ Config changes applied successfully (runtime update)"
            << std::endl;
  logRuntimeUpdate(instance_id_, "result=runtime_ok (no restart)");
  response.payload = createResponse(ResponseStatus::OK, "Instance updated (runtime)");
  return response;
}

IPCMessage WorkerHandler::handleUpdateLines(const IPCMessage &msg) {
  IPCMessage response;
  response.type = MessageType::UPDATE_LINES_RESPONSE;

  if (!msg.payload.isMember("lines")) {
    response.payload = createErrorResponse("No lines provided",
                                           ResponseStatus::INVALID_REQUEST);
    return response;
  }

  if (!pipeline_running_.load() || !getActivePipeline() || getActivePipeline()->empty()) {
    std::cout << "[Worker:" << instance_id_
              << "] Cannot update lines: pipeline not running" << std::endl;
    response.payload = createErrorResponse("Pipeline not running",
                                           ResponseStatus::ERROR);
    return response;
  }

  std::shared_ptr<cvedix_nodes::cvedix_ba_line_crossline_node> baCrosslineNode = nullptr;
  auto pipeline = getActivePipeline();
  if (pipeline) {
    for (const auto &node : pipeline->nodes()) {
      if (!node) continue;
      auto crosslineNode =
          std::dynamic_pointer_cast<cvedix_nodes::cvedix_ba_line_crossline_node>(node);
      if (crosslineNode) {
        baCrosslineNode = crosslineNode;
        break;
      }
    }
  }

  if (!baCrosslineNode) {
    std::cout << "[Worker:" << instance_id_
              << "] ba_crossline_node not found in pipeline" << std::endl;
    response.payload = createErrorResponse("ba_crossline_node not found",
                                           ResponseStatus::NOT_FOUND);
    return response;
  }

  const Json::Value &linesArray = msg.payload["lines"];
  if (!linesArray.isArray()) {
    response.payload = createErrorResponse("Lines must be an array",
                                           ResponseStatus::INVALID_REQUEST);
    return response;
  }

  std::map<int, cvedix_objects::cvedix_line> lines;
  for (Json::ArrayIndex i = 0; i < linesArray.size(); ++i) {
    const Json::Value &lineObj = linesArray[i];
    if (!lineObj.isMember("coordinates") || !lineObj["coordinates"].isArray()) continue;
    const Json::Value &coordinates = lineObj["coordinates"];
    if (coordinates.size() < 2) continue;
    const Json::Value &startCoord = coordinates[0];
    const Json::Value &endCoord = coordinates[coordinates.size() - 1];
    if (!startCoord.isMember("x") || !startCoord.isMember("y") ||
        !endCoord.isMember("x") || !endCoord.isMember("y")) continue;
    if (!startCoord["x"].isNumeric() || !startCoord["y"].isNumeric() ||
        !endCoord["x"].isNumeric() || !endCoord["y"].isNumeric()) continue;
    int start_x = startCoord["x"].asInt();
    int start_y = startCoord["y"].asInt();
    int end_x = endCoord["x"].asInt();
    int end_y = endCoord["y"].asInt();
    cvedix_objects::cvedix_point start(start_x, start_y);
    cvedix_objects::cvedix_point end(end_x, end_y);
    int channel = static_cast<int>(i);
    lines[channel] = cvedix_objects::cvedix_line(start, end);
  }

  try {
    std::cout << "[Worker:" << instance_id_ << "] Updating " << lines.size()
              << " line(s) via SDK set_lines() API" << std::endl;
    bool success = baCrosslineNode->set_lines(lines);
    if (success) {
      std::cout << "[Worker:" << instance_id_
                << "] ✓ Successfully updated lines via hot reload (no restart needed)"
                << std::endl;
      response.payload = createResponse(ResponseStatus::OK,
                                        "Lines updated successfully (runtime)");
      Json::Value data;
      data["lines_count"] = static_cast<int>(lines.size());
      response.payload["data"] = data;
      return response;
    }
    // set_lines() failed: fallback to hot swap (zero downtime, no restart)
    std::cout << "[Worker:" << instance_id_
              << "] set_lines() failed, applying via hot swap (zero downtime)"
              << std::endl;
    Json::Value oldConfig = config_;
    if (!config_.isMember("AdditionalParams") || !config_["AdditionalParams"].isObject()) {
      config_["AdditionalParams"] = Json::Value(Json::objectValue);
    }
    Json::StreamWriterBuilder wb;
    wb["indentation"] = "";
    config_["AdditionalParams"]["CrossingLines"] =
        Json::writeString(wb, msg.payload["lines"]);
    if (hotSwapPipeline(config_)) {
      std::cout << "[Worker:" << instance_id_
                << "] ✓ Lines updated via hot swap (zero downtime)" << std::endl;
      response.payload = createResponse(ResponseStatus::OK,
                                          "Lines updated via hot swap (zero downtime)");
      Json::Value data;
      data["lines_count"] = static_cast<int>(lines.size());
      response.payload["data"] = data;
    } else {
      config_ = oldConfig;
      std::cerr << "[Worker:" << instance_id_
                << "] Hot swap fallback failed for lines update" << std::endl;
      response.payload = createErrorResponse(
          "Failed to update lines (hot swap fallback failed)",
          ResponseStatus::INTERNAL_ERROR);
    }
    return response;
  } catch (const std::exception &e) {
    std::cerr << "[Worker:" << instance_id_
              << "] Exception updating lines: " << e.what()
              << ", trying hot swap fallback" << std::endl;
    Json::Value oldConfig = config_;
    if (!config_.isMember("AdditionalParams") || !config_["AdditionalParams"].isObject()) {
      config_["AdditionalParams"] = Json::Value(Json::objectValue);
    }
    Json::StreamWriterBuilder wb;
    wb["indentation"] = "";
    config_["AdditionalParams"]["CrossingLines"] =
        Json::writeString(wb, msg.payload["lines"]);
    if (hotSwapPipeline(config_)) {
      std::cout << "[Worker:" << instance_id_
                << "] ✓ Lines updated via hot swap after exception (zero downtime)"
                << std::endl;
      response.payload = createResponse(ResponseStatus::OK,
                                          "Lines updated via hot swap (zero downtime)");
      Json::Value data;
      data["lines_count"] = static_cast<int>(lines.size());
      response.payload["data"] = data;
    } else {
      config_ = oldConfig;
      response.payload = createErrorResponse(
          "Exception updating lines: " + std::string(e.what()),
          ResponseStatus::INTERNAL_ERROR);
    }
    return response;
  } catch (...) {
    std::cerr << "[Worker:" << instance_id_
              << "] Unknown exception updating lines, trying hot swap fallback"
              << std::endl;
    Json::Value oldConfig = config_;
    if (!config_.isMember("AdditionalParams") || !config_["AdditionalParams"].isObject()) {
      config_["AdditionalParams"] = Json::Value(Json::objectValue);
    }
    Json::StreamWriterBuilder wb;
    wb["indentation"] = "";
    config_["AdditionalParams"]["CrossingLines"] =
        Json::writeString(wb, msg.payload["lines"]);
    if (hotSwapPipeline(config_)) {
      std::cout << "[Worker:" << instance_id_
                << "] ✓ Lines updated via hot swap after exception (zero downtime)"
                << std::endl;
      response.payload = createResponse(ResponseStatus::OK,
                                        "Lines updated via hot swap (zero downtime)");
      Json::Value data;
      data["lines_count"] = static_cast<int>(lines.size());
      response.payload["data"] = data;
    } else {
      config_ = oldConfig;
      response.payload = createErrorResponse("Unknown exception updating lines",
                                             ResponseStatus::INTERNAL_ERROR);
    }
    return response;
  }
}

IPCMessage WorkerHandler::handleUpdateJams(const IPCMessage &msg) {
  IPCMessage response;
  response.type = MessageType::UPDATE_JAMS_RESPONSE;

  if (!msg.payload.isMember("jams")) {
    response.payload = createErrorResponse("No jams provided",
                                           ResponseStatus::INVALID_REQUEST);
    return response;
  }

  if (!pipeline_running_.load() || !getActivePipeline() || getActivePipeline()->empty()) {
    response.payload = createErrorResponse("Pipeline not running", ResponseStatus::ERROR);
    return response;
  }

  Json::Value oldConfig = config_;
  if (!config_.isMember("AdditionalParams") || !config_["AdditionalParams"].isObject()) {
    config_["AdditionalParams"] = Json::Value(Json::objectValue);
  }
  Json::StreamWriterBuilder wb;
  wb["indentation"] = "";
  config_["AdditionalParams"]["JamZones"] = Json::writeString(wb, msg.payload["jams"]);

  std::cout << "[Worker:" << instance_id_
            << "] Updating jam zones via hot swap (config merge)" << std::endl;
  if (hotSwapPipeline(config_)) {
    std::cout << "[Worker:" << instance_id_
              << "] ✓ Jam zones updated successfully (hot swap)" << std::endl;
    response.payload = createResponse(ResponseStatus::OK,
                                      "Jam zones updated successfully (runtime)");
    Json::Value data;
    data["jams_count"] = msg.payload["jams"].isArray() ?
        static_cast<int>(msg.payload["jams"].size()) : 0;
    response.payload["data"] = data;
  } else {
    std::cerr << "[Worker:" << instance_id_
              << "] Failed to hot swap pipeline for jam zones update" << std::endl;
    config_ = oldConfig;
    response.payload = createErrorResponse("Failed to apply jam zones (hot swap failed)",
                                           ResponseStatus::INTERNAL_ERROR);
  }
  return response;
}

IPCMessage WorkerHandler::handleUpdateStops(const IPCMessage &msg) {
  IPCMessage response;
  response.type = MessageType::UPDATE_STOPS_RESPONSE;

  if (!msg.payload.isMember("stops")) {
    response.payload = createErrorResponse("No stops provided",
                                           ResponseStatus::INVALID_REQUEST);
    return response;
  }

  if (!pipeline_running_.load() || !getActivePipeline() || getActivePipeline()->empty()) {
    response.payload = createErrorResponse("Pipeline not running", ResponseStatus::ERROR);
    return response;
  }

  Json::Value oldConfig = config_;
  if (!config_.isMember("AdditionalParams") || !config_["AdditionalParams"].isObject()) {
    config_["AdditionalParams"] = Json::Value(Json::objectValue);
  }
  Json::StreamWriterBuilder wb;
  wb["indentation"] = "";
  config_["AdditionalParams"]["StopZones"] = Json::writeString(wb, msg.payload["stops"]);

  std::cout << "[Worker:" << instance_id_
            << "] Updating stop zones via hot swap (config merge)" << std::endl;
  if (hotSwapPipeline(config_)) {
    std::cout << "[Worker:" << instance_id_
              << "] ✓ Stop zones updated successfully (hot swap)" << std::endl;
    response.payload = createResponse(ResponseStatus::OK,
                                      "Stop zones updated successfully (runtime)");
    Json::Value data;
    data["stops_count"] = msg.payload["stops"].isArray() ?
        static_cast<int>(msg.payload["stops"].size()) : 0;
    response.payload["data"] = data;
  } else {
    std::cerr << "[Worker:" << instance_id_
              << "] Failed to hot swap pipeline for stop zones update" << std::endl;
    config_ = oldConfig;
    response.payload = createErrorResponse("Failed to apply stop zones (hot swap failed)",
                                           ResponseStatus::INTERNAL_ERROR);
  }
  return response;
}

} // namespace worker
