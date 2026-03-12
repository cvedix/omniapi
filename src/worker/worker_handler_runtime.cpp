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
#include <fstream>
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

Json::Value WorkerHandler::getParamsFromConfig(const Json::Value &config) const {
  Json::Value result(Json::objectValue);
  Json::Value base;
  if (config.isMember("AdditionalParams") &&
      config["AdditionalParams"].isObject()) {
    base = config["AdditionalParams"];
  } else if (config.isMember("additionalParams") &&
             config["additionalParams"].isObject()) {
    base = config["additionalParams"];
  }
  if (base.isObject()) {
    for (const auto &key : base.getMemberNames()) {
      if (key == "input" && base[key].isObject()) {
        for (const auto &k : base[key].getMemberNames()) {
          result[k] = base[key][k];
        }
      } else if (key == "output" && base[key].isObject()) {
        for (const auto &k : base[key].getMemberNames()) {
          result[k] = base[key][k];
        }
      } else {
        result[key] = base[key];
      }
    }
  }
  // Support direct PascalCase Input/Output (e.g. PUT with top-level Input)
  if (config.isMember("Input") && config["Input"].isObject()) {
    for (const auto &k : config["Input"].getMemberNames()) {
      result[k] = config["Input"][k];
    }
  }
  if (config.isMember("Output") && config["Output"].isObject()) {
    for (const auto &k : config["Output"].getMemberNames()) {
      result[k] = config["Output"][k];
    }
  }
  return result;
}

bool WorkerHandler::applyLinesFromParamsToPipeline(const Json::Value &params) {
  if (!pipeline_running_.load() || !getActivePipeline() || getActivePipeline()->empty()) {
    return true; // No pipeline, nothing to apply
  }
  std::shared_ptr<cvedix_nodes::cvedix_ba_line_crossline_node> baCrosslineNode;
  auto pLines = getActivePipeline();
  if (pLines) {
    for (const auto &node : pLines->nodes()) {
      if (!node) continue;
      auto crosslineNode =
          std::dynamic_pointer_cast<cvedix_nodes::cvedix_ba_line_crossline_node>(
              node);
      if (crosslineNode) {
        baCrosslineNode = crosslineNode;
        break;
      }
    }
  }
  if (!baCrosslineNode) {
    std::cout << "[Worker:" << instance_id_
              << "] applyLinesFromParamsToPipeline: no ba_crossline_node"
              << std::endl;
    return true; // Not a crossline pipeline, no lines to apply
  }

  std::map<int, cvedix_objects::cvedix_line> lines;

  // Priority 1: CrossingLines (JSON array string)
  if (params.isMember("CrossingLines") && params["CrossingLines"].isString()) {
    std::string linesStr = params["CrossingLines"].asString();
    if (linesStr.empty()) {
      bool ok = baCrosslineNode->set_lines(lines);
      if (ok) {
        std::cout << "[Worker:" << instance_id_
                  << "] Applied empty CrossingLines (runtime, no restart)"
                  << std::endl;
      }
      return ok;
    }
    Json::CharReaderBuilder builder;
    Json::Value linesArray;
    std::istringstream ss(linesStr);
    std::string errs;
    if (!Json::parseFromStream(builder, ss, &linesArray, &errs) ||
        !linesArray.isArray()) {
      std::cerr << "[Worker:" << instance_id_
                << "] Failed to parse CrossingLines JSON" << std::endl;
      return false;
    }
    for (Json::ArrayIndex i = 0; i < linesArray.size(); ++i) {
      const Json::Value &lineObj = linesArray[i];
      if (!lineObj.isMember("coordinates") ||
          !lineObj["coordinates"].isArray() ||
          lineObj["coordinates"].size() < 2) {
        continue;
      }
      const Json::Value &coords = lineObj["coordinates"];
      const Json::Value &startCoord = coords[0];
      const Json::Value &endCoord = coords[coords.size() - 1];
      if (!startCoord.isMember("x") || !startCoord.isMember("y") ||
          !endCoord.isMember("x") || !endCoord.isMember("y")) {
        continue;
      }
      int start_x = startCoord["x"].asInt();
      int start_y = startCoord["y"].asInt();
      int end_x = endCoord["x"].asInt();
      int end_y = endCoord["y"].asInt();
      cvedix_objects::cvedix_point start(start_x, start_y);
      cvedix_objects::cvedix_point end(end_x, end_y);
      lines[static_cast<int>(i)] = cvedix_objects::cvedix_line(start, end);
    }
  } else {
    // Priority 2: Legacy CROSSLINE_START_X/Y, CROSSLINE_END_X/Y (single line)
    auto sx = params.get("CROSSLINE_START_X", Json::Value());
    auto sy = params.get("CROSSLINE_START_Y", Json::Value());
    auto ex = params.get("CROSSLINE_END_X", Json::Value());
    auto ey = params.get("CROSSLINE_END_Y", Json::Value());
    if (sx.isString() && sy.isString() && ex.isString() && ey.isString()) {
      int start_x = std::stoi(sx.asString());
      int start_y = std::stoi(sy.asString());
      int end_x = std::stoi(ex.asString());
      int end_y = std::stoi(ey.asString());
      cvedix_objects::cvedix_point start(start_x, start_y);
      cvedix_objects::cvedix_point end(end_x, end_y);
      lines[0] = cvedix_objects::cvedix_line(start, end);
    }
  }

  if (lines.empty()) {
    logRuntimeUpdate(instance_id_, "applyLinesFromParamsToPipeline: lines.empty() -> skip");
    return true; // No line config, nothing to apply
  }

  logRuntimeUpdate(instance_id_, "applyLinesFromParamsToPipeline: lines.size()=" + std::to_string(lines.size()) + " calling set_lines()");
  bool ok = baCrosslineNode->set_lines(lines);
  if (ok) {
    std::cout << "[Worker:" << instance_id_ << "] Applied " << lines.size()
              << " line(s) at runtime (no restart)" << std::endl;
    logRuntimeUpdate(instance_id_, "applyLinesFromParamsToPipeline: set_lines()=ok");
  } else {
    std::cerr << "[Worker:" << instance_id_
              << "] set_lines() failed in applyLinesFromParamsToPipeline"
              << std::endl;
    logRuntimeUpdate(instance_id_, "applyLinesFromParamsToPipeline: set_lines()=failed");
  }
  return ok;
}

bool WorkerHandler::checkIfNeedsRebuild(const Json::Value &oldConfig,
                                        const Json::Value &newConfig) const {
  // Check if solution changed (structural change)
  if (oldConfig.isMember("Solution") && newConfig.isMember("Solution")) {
    std::string oldSolutionId;
    std::string newSolutionId;

    if (oldConfig["Solution"].isString()) {
      oldSolutionId = oldConfig["Solution"].asString();
    } else if (oldConfig["Solution"].isObject()) {
      oldSolutionId = oldConfig["Solution"].get("SolutionId", "").asString();
    }

    if (newConfig["Solution"].isString()) {
      newSolutionId = newConfig["Solution"].asString();
    } else if (newConfig["Solution"].isObject()) {
      newSolutionId = newConfig["Solution"].get("SolutionId", "").asString();
    }

    if (oldSolutionId != newSolutionId) {
      std::cout << "[Worker:" << instance_id_
                << "] Solution changed: " << oldSolutionId << " -> "
                << newSolutionId << " (requires rebuild)" << std::endl;
      return true;
    }
  } else if (oldConfig.isMember("Solution") != newConfig.isMember("Solution")) {
    // Solution added or removed
    std::cout << "[Worker:" << instance_id_
              << "] Solution presence changed (requires rebuild)" << std::endl;
    return true;
  }

  // Check if SolutionId changed (alternative field)
  if (oldConfig.isMember("SolutionId") && newConfig.isMember("SolutionId")) {
    if (oldConfig["SolutionId"].asString() !=
        newConfig["SolutionId"].asString()) {
      std::cout << "[Worker:" << instance_id_
                << "] SolutionId changed (requires rebuild)" << std::endl;
      return true;
    }
  }

  // Check for model path changes (requires rebuild as models are loaded at
  // pipeline creation). Use getParamsFromConfig to support both
  // AdditionalParams and additionalParams (API format).
  Json::Value oldParams = getParamsFromConfig(oldConfig);
  Json::Value newParams = getParamsFromConfig(newConfig);

  if (oldParams.isObject() && newParams.isObject()) {
    // Line params (CrossingLines, CROSSLINE_*) are runtime-updatable via
    // set_lines() in applyConfigToPipeline; do NOT require rebuild here.
    // Check detector model file
    if (oldParams.isMember("DETECTOR_MODEL_FILE") !=
            newParams.isMember("DETECTOR_MODEL_FILE") ||
        (oldParams.isMember("DETECTOR_MODEL_FILE") &&
         newParams.isMember("DETECTOR_MODEL_FILE") &&
         oldParams["DETECTOR_MODEL_FILE"].asString() !=
             newParams["DETECTOR_MODEL_FILE"].asString())) {
      std::cout << "[Worker:" << instance_id_
                << "] Detector model file changed (requires rebuild)"
                << std::endl;
      return true;
    }

    // Check thermal model file
    if (oldParams.isMember("DETECTOR_THERMAL_MODEL_FILE") !=
            newParams.isMember("DETECTOR_THERMAL_MODEL_FILE") ||
        (oldParams.isMember("DETECTOR_THERMAL_MODEL_FILE") &&
         newParams.isMember("DETECTOR_THERMAL_MODEL_FILE") &&
         oldParams["DETECTOR_THERMAL_MODEL_FILE"].asString() !=
             newParams["DETECTOR_THERMAL_MODEL_FILE"].asString())) {
      std::cout << "[Worker:" << instance_id_
                << "] Thermal model file changed (requires rebuild)"
                << std::endl;
      return true;
    }

    // Check other model paths (MODEL_PATH, SFACE_MODEL_PATH, etc.)
    std::vector<std::string> modelPathKeys = {"MODEL_PATH", "SFACE_MODEL_PATH",
                                              "WEIGHTS_PATH", "CONFIG_PATH"};
    for (const auto &key : modelPathKeys) {
      if (oldParams.isMember(key) != newParams.isMember(key) ||
          (oldParams.isMember(key) && newParams.isMember(key) &&
           oldParams[key].asString() != newParams[key].asString())) {
        std::cout << "[Worker:" << instance_id_ << "] " << key
                  << " changed (requires rebuild)" << std::endl;
        return true;
      }
    }

    // CrossingLines and CROSSLINE_* are runtime-updatable via set_lines() - do
    // NOT require rebuild (applied in applyConfigToPipeline for 99% uptime).
  }

  // Other structural changes that require rebuild can be added here
  return false;
}

bool WorkerHandler::applyConfigToPipeline(const Json::Value &oldConfig,
                                          const Json::Value &newConfig) {
  // Decides whether config can be applied at runtime (true) or must trigger
  // rebuild/hot-swap (false). For line-only changes + ba_crossline_node we
  // apply via set_lines() and return true (no restart).
  try {
    // Extract params (supports AdditionalParams and additionalParams + nested input)
    Json::Value oldParams = getParamsFromConfig(oldConfig);
    Json::Value newParams = getParamsFromConfig(newConfig);

    // Check for source URL changes (RTSP, RTMP, FILE_PATH)
    bool sourceUrlChanged = false;
    std::string newRtspUrl;
    std::string newRtmpUrl;
    std::string newFilePath;

    if (newParams.isMember("RTSP_SRC_URL") || newParams.isMember("RTSP_URL")) {
      std::string oldUrl = "";
      if (oldParams.isMember("RTSP_SRC_URL")) {
        oldUrl = oldParams["RTSP_SRC_URL"].asString();
      } else if (oldParams.isMember("RTSP_URL")) {
        oldUrl = oldParams["RTSP_URL"].asString();
      }

      if (newParams.isMember("RTSP_SRC_URL")) {
        newRtspUrl = newParams["RTSP_SRC_URL"].asString();
      } else if (newParams.isMember("RTSP_URL")) {
        newRtspUrl = newParams["RTSP_URL"].asString();
      }

      if (oldUrl != newRtspUrl) {
        sourceUrlChanged = true;
        std::cout << "[Worker:" << instance_id_
                  << "] RTSP URL changed: " << oldUrl << " -> " << newRtspUrl
                  << std::endl;
      }
    }

    if (newParams.isMember("RTMP_SRC_URL") || newParams.isMember("RTMP_URL")) {
      std::string oldUrl = "";
      if (oldParams.isMember("RTMP_SRC_URL")) {
        oldUrl = oldParams["RTMP_SRC_URL"].asString();
      } else if (oldParams.isMember("RTMP_URL")) {
        oldUrl = oldParams["RTMP_URL"].asString();
      }

      if (newParams.isMember("RTMP_SRC_URL")) {
        newRtmpUrl = newParams["RTMP_SRC_URL"].asString();
      } else if (newParams.isMember("RTMP_URL")) {
        newRtmpUrl = newParams["RTMP_URL"].asString();
      }

      if (oldUrl != newRtmpUrl) {
        sourceUrlChanged = true;
        std::cout << "[Worker:" << instance_id_
                  << "] RTMP URL changed: " << oldUrl << " -> " << newRtmpUrl
                  << std::endl;
      }
    }

    if (newParams.isMember("FILE_PATH")) {
      std::string oldPath = oldParams.get("FILE_PATH", "").asString();
      newFilePath = newParams["FILE_PATH"].asString();

      if (oldPath != newFilePath) {
        sourceUrlChanged = true;
        std::cout << "[Worker:" << instance_id_
                  << "] FILE_PATH changed: " << oldPath << " -> " << newFilePath
                  << std::endl;
      }
    }

    // If source URL changed, we need to rebuild pipeline
    // CVEDIX source nodes don't support changing URL/path at runtime
    if (sourceUrlChanged) {
      std::cout << "[Worker:" << instance_id_
                << "] Source URL changed, requires pipeline rebuild"
                << std::endl;
      logRuntimeUpdate(instance_id_, "applyConfigToPipeline: sourceUrlChanged=true -> rebuild");
      return false; // Trigger rebuild
    }

    // CrossingLines / CROSSLINE_*: apply at runtime (no restart)
    bool linesChanged = false;
    if (newParams.isMember("CrossingLines") &&
        oldParams.get("CrossingLines", "").asString() !=
            newParams["CrossingLines"].asString()) {
      linesChanged = true;
    }
    if (!linesChanged &&
        (newParams.isMember("CROSSLINE_START_X") ||
         newParams.isMember("CROSSLINE_END_X"))) {
      if (oldParams.get("CROSSLINE_START_X", "").asString() !=
              newParams.get("CROSSLINE_START_X", "").asString() ||
          oldParams.get("CROSSLINE_START_Y", "").asString() !=
              newParams.get("CROSSLINE_START_Y", "").asString() ||
          oldParams.get("CROSSLINE_END_X", "").asString() !=
              newParams.get("CROSSLINE_END_X", "").asString() ||
          oldParams.get("CROSSLINE_END_Y", "").asString() !=
              newParams.get("CROSSLINE_END_Y", "").asString()) {
        linesChanged = true;
      }
    }
    if (linesChanged) {
      logRuntimeUpdate(instance_id_, "applyConfigToPipeline: linesChanged=true, calling applyLinesFromParamsToPipeline");
      if (applyLinesFromParamsToPipeline(newParams)) {
        std::cout << "[Worker:" << instance_id_
                  << "] CrossingLines/CROSSLINE_* applied at runtime (no restart)"
                  << std::endl;
        logRuntimeUpdate(instance_id_, "applyConfigToPipeline: applyLinesFromParamsToPipeline=ok");
        // Continue to check other params; if only lines changed we'll return true below
      } else {
        // Only line change but set_lines() failed: do NOT trigger rebuild so the
        // instance keeps running. Config is already merged; new lines will apply
        // on next start. Avoids stopping the pipeline when SDK rejects the line update.
        std::cerr << "[Worker:" << instance_id_
                  << "] Failed to apply lines at runtime (config saved, will apply on next start)"
                  << std::endl;
        logRuntimeUpdate(instance_id_, "applyConfigToPipeline: applyLinesFromParamsToPipeline=failed (config saved, no restart)");
        // Fall through: if no other param changes we return true (no restart)
      }
    }

    // Check for Zone changes (if applicable)
    if (newConfig.isMember("Zone") || oldConfig.isMember("Zone")) {
      if (newConfig["Zone"].toStyledString() !=
          oldConfig["Zone"].toStyledString()) {
        std::cout << "[Worker:" << instance_id_
                  << "] Zone configuration changed, requires rebuild"
                  << std::endl;
        logRuntimeUpdate(instance_id_, "applyConfigToPipeline: Zone changed -> rebuild");
        return false; // Trigger rebuild
      }
    }

    // For other parameter changes, log them (exclude runtime-updatable: lines)
    // Most CVEDIX nodes don't support runtime parameter updates.
    // Use normalized string comparison so int vs string or missing key don't
    // cause false "changed" and spurious rebuild for line-only updates.
    static const std::set<std::string> runtimeLineKeys = {
        "CrossingLines", "CROSSLINE_START_X", "CROSSLINE_START_Y",
        "CROSSLINE_END_X", "CROSSLINE_END_Y"};
    std::vector<std::string> changedParams;
    for (const auto &key : newParams.getMemberNames()) {
      if (key != "RTSP_SRC_URL" && key != "RTSP_URL" && key != "RTMP_SRC_URL" &&
          key != "RTMP_URL" && key != "FILE_PATH" &&
          runtimeLineKeys.count(key) == 0) {
        std::string oldStr =
            oldParams.isMember(key) ? oldParams[key].asString() : "";
        std::string newStr = newParams[key].asString();
        if (oldStr != newStr) {
          changedParams.push_back(key);
        }
      }
    }

    if (!changedParams.empty()) {
      std::cout << "[Worker:" << instance_id_
                << "] Parameter changes detected: ";
      for (size_t i = 0; i < changedParams.size(); ++i) {
        std::cout << changedParams[i];
        if (i < changedParams.size() - 1) {
          std::cout << ", ";
        }
      }
      std::cout << std::endl;
    }

    // For parameters that don't require rebuild, config is merged
    // However, since CVEDIX nodes don't support runtime updates for most
    // params, we return false to trigger rebuild so changes take effect
    // immediately This ensures ALL config changes are applied, not just merged
    if (!changedParams.empty()) {
      std::cout << "[Worker:" << instance_id_
                << "] Parameters changed, rebuilding to apply changes"
                << std::endl;
      std::string changedStr;
      for (size_t i = 0; i < changedParams.size(); ++i) {
        if (i) changedStr += ",";
        changedStr += changedParams[i];
      }
      logRuntimeUpdate(instance_id_, "applyConfigToPipeline: changedParams=[" + changedStr + "] -> rebuild");
      return false; // Trigger rebuild to apply parameter changes
    }

    // No significant changes detected
    std::cout << "[Worker:" << instance_id_
              << "] No significant parameter changes detected" << std::endl;
    logRuntimeUpdate(instance_id_, "applyConfigToPipeline: no significant changes -> return true");
    return true;

  } catch (const std::exception &e) {
    std::cerr << "[Worker:" << instance_id_
              << "] Error applying config to pipeline: " << e.what()
              << std::endl;
    return false;
  }
}

} // namespace worker
