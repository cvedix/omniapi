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

CreateInstanceRequest WorkerHandler::parseCreateRequest(const Json::Value &config) const {
  CreateInstanceRequest req;

  req.name = config.get("Name", instance_id_).asString();
  req.group = config.get("Group", "").asString();

  // Get solution ID
  if (config.isMember("Solution")) {
    if (config["Solution"].isString()) {
      req.solution = config["Solution"].asString();
    } else if (config["Solution"].isObject()) {
      req.solution = config["Solution"].get("SolutionId", "").asString();
    }
  }
  req.solution = config.get("SolutionId", req.solution).asString();

  // Flags
  req.persistent = config.get("Persistent", false).asBool();
  req.autoStart = config.get("AutoStart", false).asBool();
  req.autoRestart = config.get("AutoRestart", false).asBool();
  req.frameRateLimit = config.get("FrameRateLimit", 0).asInt();

  // Additional parameters (source URLs, model paths, etc.)
  // Support both nested structure (input/output) and flat structure
  // Support both "additionalParams" (lowercase, API format) and "AdditionalParams" (capital, internal format)
  Json::Value params;
  if (config.isMember("additionalParams") &&
      config["additionalParams"].isObject()) {
    params = config["additionalParams"];
  } else if (config.isMember("AdditionalParams") &&
             config["AdditionalParams"].isObject()) {
    params = config["AdditionalParams"];
  }
  
  if (!params.isNull()) {
    
    // Check if using new structure (input/output)
    if (params.isMember("input") && params["input"].isObject()) {
      // New structure: parse input section
      for (const auto &key : params["input"].getMemberNames()) {
        if (params["input"][key].isString()) {
          req.additionalParams[key] = params["input"][key].asString();
        }
      }
    }

    if (params.isMember("output") && params["output"].isObject()) {
      // New structure: parse output section
      for (const auto &key : params["output"].getMemberNames()) {
        if (params["output"][key].isString()) {
          req.additionalParams[key] = params["output"][key].asString();
        }
      }
    }

    // Backward compatibility: if no input/output sections, parse as flat
    // structure. Also parse flat keys (RTMP_URL, RTSP_URL, etc.) even when input/output exist.
    if (!params.isMember("input") && !params.isMember("output")) {
      for (const auto &key : params.getMemberNames()) {
        if (params[key].isString()) {
          req.additionalParams[key] = params[key].asString();
        }
      }
    } else {
      for (const auto &key : params.getMemberNames()) {
        if (key != "input" && key != "output" && params[key].isString()) {
          req.additionalParams[key] = params[key].asString();
        }
      }
    }
  }

  // Direct URL parameters (top-level fields for backward compatibility)
  // CRITICAL: These are set by SubprocessInstanceManager to ensure RTMP_URL is available
  if (config.isMember("RtspUrl")) {
    req.additionalParams["RTSP_URL"] = config["RtspUrl"].asString();
    std::cout << "[Worker:" << instance_id_ << "] Found RTSP_URL in top-level RtspUrl: '"
              << req.additionalParams["RTSP_URL"] << "'" << std::endl;
  }
  if (config.isMember("RtmpUrl")) {
    req.additionalParams["RTMP_URL"] = config["RtmpUrl"].asString();
    std::cout << "[Worker:" << instance_id_ << "] Found RTMP_URL in top-level RtmpUrl: '"
              << req.additionalParams["RTMP_URL"] << "'" << std::endl;
  }
  if (config.isMember("FilePath")) {
    req.additionalParams["FILE_PATH"] = config["FilePath"].asString();
    std::cout << "[Worker:" << instance_id_ << "] Found FILE_PATH in top-level FilePath: '"
              << req.additionalParams["FILE_PATH"] << "'" << std::endl;
  }

  // Debug: Log final additionalParams to verify RTMP_URL is present
  std::cout << "[Worker:" << instance_id_ << "] Final additionalParams keys: ";
  for (const auto &[key, value] : req.additionalParams) {
    if (key.find("RTMP") != std::string::npos || key.find("RTSP") != std::string::npos) {
      std::cout << key << "='" << value << "' ";
    }
  }
  std::cout << std::endl;

  return req;
}

bool WorkerHandler::buildPipeline() {
  std::cout << "[Worker:" << instance_id_
            << "] Building pipeline from config..." << std::endl;

  if (!pipeline_builder_) {
    last_error_ = "Pipeline builder not initialized";
    return false;
  }

  try {
    // Parse config to CreateInstanceRequest
    CreateInstanceRequest req = parseCreateRequest(config_);

    if (req.solution.empty()) {
      last_error_ = "No solution specified in config";
      return false;
    }

    // Get solution config from singleton
    auto optSolution =
        SolutionRegistry::getInstance().getSolution(req.solution);
    if (!optSolution.has_value()) {
      last_error_ = "Solution not found: " + req.solution;
      return false;
    }

    // CVEDIX logger must be initialized before creating any cvedix nodes (e.g. PersistentOutputLeg/rtmp_des)
    if (pipeline_builder_) {
      pipeline_builder_->ensureCVEDIXInitialized();
    }
    if (!ensureOutputLegForRtmp(req)) {
      return false;
    }

    // Build pipeline (with frame_router_ when RTMP → zero-downtime swap)
    std::vector<std::shared_ptr<cvedix_nodes::cvedix_node>> nodes =
        pipeline_builder_->buildPipeline(optSolution.value(), req,
                                         instance_id_, {}, frame_router_.get());

    if (nodes.empty()) {
      last_error_ = "Pipeline builder returned empty pipeline";
      return false;
    }

    setActivePipeline(
        std::make_shared<PipelineSnapshot>(std::move(nodes)));
    {
      std::lock_guard<std::shared_mutex> lock(state_mutex_);
      current_state_ = "created";
    }
    std::cout << "[Worker:" << instance_id_ << "] Pipeline built with "
              << getActivePipeline()->size() << " nodes" << std::endl;
    return true;

  } catch (const char* msg) {
    last_error_ = std::string("Pipeline build (SDK/lib threw string): ") + (msg ? msg : "(null)");
    std::cerr << "[Worker:" << instance_id_
              << "] Failed to build pipeline: " << (msg ? msg : "(null)") << std::endl;
    return false;
  } catch (const std::exception &e) {
    last_error_ = e.what();
    std::cerr << "[Worker:" << instance_id_
              << "] Failed to build pipeline: " << e.what() << std::endl;
    return false;
  } catch (...) {
    last_error_ = "Pipeline build failed (unknown exception, e.g. SDK threw non-std::exception)";
    std::cerr << "[Worker:" << instance_id_
              << "] Failed to build pipeline: unknown exception" << std::endl;
    return false;
  }
}

} // namespace worker
