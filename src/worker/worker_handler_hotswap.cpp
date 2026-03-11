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
#include <fstream>
#include <chrono>

namespace worker {

bool WorkerHandler::hotSwapPipeline(const Json::Value &newConfig) {
  if (!pipeline_running_.load() || !getActivePipeline() || getActivePipeline()->empty()) {
    return buildPipeline();
  }

  // Merge newConfig into a copy of config_ so nested objects (e.g. AdditionalParams)
  // are merged, not replaced. Avoids losing params when PATCH sends only
  // AdditionalParams.CrossingLines and hot-swap path is taken.
  Json::Value tempConfig = config_;
  worker::mergeJsonInto(tempConfig, newConfig);
  // CRITICAL: Ensure tempConfig has RTMP output so preBuildPipeline builds with
  // FrameRouterSinkNode / output_leg_ and stream is not lost after swap.
  if (config_.isMember("additionalParams") && config_["additionalParams"].isObject() &&
      config_["additionalParams"].isMember("output") &&
      config_["additionalParams"]["output"].isObject()) {
    const auto& out = config_["additionalParams"]["output"];
    bool hasRtmp = (out.isMember("RTMP_DES_URL") && !out["RTMP_DES_URL"].asString().empty()) ||
                   (out.isMember("RTMP_URL") && !out["RTMP_URL"].asString().empty());
    if (hasRtmp) {
      if (!tempConfig.isMember("additionalParams") || !tempConfig["additionalParams"].isObject()) {
        tempConfig["additionalParams"] = Json::Value(Json::objectValue);
      }
      Json::Value& tOut = tempConfig["additionalParams"]["output"];
      if (!tOut.isObject() || (tOut.get("RTMP_DES_URL", "").asString().empty() &&
                               tOut.get("RTMP_URL", "").asString().empty())) {
        tempConfig["additionalParams"]["output"] = config_["additionalParams"]["output"];
        std::cout << "[Worker:" << instance_id_
                  << "] Hot-swap: preserved RTMP output in tempConfig (avoid stream loss)"
                  << std::endl;
      }
    }
  }
  if (config_.isMember("RtmpUrl") && !config_["RtmpUrl"].asString().empty() &&
      (!tempConfig.isMember("RtmpUrl") || tempConfig["RtmpUrl"].asString().empty())) {
    tempConfig["RtmpUrl"] = config_["RtmpUrl"];
    if (tempConfig["additionalParams"].isObject()) {
      tempConfig["additionalParams"]["RTMP_URL"] = config_["RtmpUrl"];
    }
  }

  // #region agent log
  {
    auto ts = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    const char* logPath = "/home/cvedix/Data/DEV/DEV_1/api/.cursor/debug-408f41.log";
    std::ofstream df(logPath, std::ios::app);
    if (!df) { logPath = "/tmp/debug-408f41.log"; df.open(logPath, std::ios::app); }
    if (df) df << "{\"sessionId\":\"408f41\",\"location\":\"worker_handler_hotswap.cpp:path_check\",\"message\":\"hotswap_path\",\"data\":{\"has_frame_router\":"
               << (frame_router_ ? "true" : "false") << ",\"has_output_leg\":" << (output_leg_ ? "true" : "false")
               << ",\"zero_downtime_path\":" << (frame_router_ && output_leg_ ? "true" : "false") << "},\"timestamp\":" << ts << ",\"hypothesisId\":\"H2\"}\n";
  }
  // #endregion
  // Zero-downtime path: persistent output leg + atomic swap (no RTMP reconnect)
  if (frame_router_ && output_leg_) {
    std::cout << "[Worker:" << instance_id_
              << "] Zero-downtime pipeline swap (build new → atomic swap → drain old)..."
              << std::endl;
    auto totalStartTime = std::chrono::steady_clock::now();

    // 1) Capture last frame and start pump so RTMP never sees gap
    frame_router_->setLastFramePumpActive(true);
    if (has_frame_ && last_frame_ && !last_frame_->empty()) {
      frame_router_->setLastFrame(*last_frame_);
      startLastFramePumpThread();
    }

    // 2) Build new pipeline (reuses frame_router_ → FrameRouterSinkNode)
    building_new_pipeline_.store(true);
    new_pipeline_nodes_.clear();
    if (!preBuildPipeline(tempConfig)) {
      building_new_pipeline_.store(false);
      stopLastFramePumpThread();
      frame_router_->setLastFramePumpActive(false);
      return false;
    }
    building_new_pipeline_.store(false);
    PipelineSnapshotPtr newSnapshot =
        std::make_shared<PipelineSnapshot>(std::move(new_pipeline_nodes_));
    new_pipeline_nodes_.clear();

    // 3) Atomic swap: new becomes active; old is returned for stop + drain
    PipelineSnapshotPtr oldSnapshot = frame_router_->setActivePipeline(newSnapshot);

    // 4) Stop old pipeline source (no more frames into old graph); do not release yet
    stopSourceNodeForSnapshot(oldSnapshot, false);

    // 5) Drain in-flight frames in old graph, then release
    drainPipelineSnapshot(oldSnapshot);
    oldSnapshot.reset();

    // 6) Start new pipeline source immediately (no delay here: delay before start
    //    caused 5s of only last-frame pump -> server could close RTMP -> stream lost)
    config_ = tempConfig;
    setupFrameCaptureHook();
    setupQueueSizeTrackingHook();
    {
      std::lock_guard<std::mutex> lock(start_pipeline_mutex_);
      start_time_ = std::chrono::steady_clock::now();
      last_fps_update_ = start_time_;
      frames_processed_.store(0);
      dropped_frames_.store(0);
      current_fps_.store(0.0);
    }
    if (!startSourceNodeForSnapshot(newSnapshot)) {
      stopLastFramePumpThread();
      frame_router_->setLastFramePumpActive(false);
      return false;
    }

    // 7) Keep last-frame pump running briefly so new pipeline has time to produce first frame
    //    (avoids gap where neither pump nor new pipeline sends -> stream stays continuous)
    constexpr std::chrono::milliseconds kPumpOverlapMs(500);
    std::cout << "[Worker:" << instance_id_
              << "] Pump overlap " << kPumpOverlapMs.count() << "ms (ensure new pipeline feeds output before stopping pump)"
              << std::endl;
    std::this_thread::sleep_for(kPumpOverlapMs);
    stopLastFramePumpThread();
    frame_router_->setLastFramePumpActive(false);

    // Optional test delay AFTER new pipeline is running (so RTMP keeps receiving real frames; delay is for testing only)
    {
      const char* env = std::getenv("EDGE_AI_HOTSWAP_DELAY_SEC");
      int delaySec = env ? std::atoi(env) : 0;
      if (delaySec > 0) {
        std::cout << "[Worker:" << instance_id_
                  << "] Hot-swap test delay: sleeping " << delaySec << "s (new pipeline already running)..."
                  << std::endl;
        std::this_thread::sleep_for(std::chrono::seconds(delaySec));
        std::cout << "[Worker:" << instance_id_
                  << "] Hot-swap test delay: done"
                  << std::endl;
      }
    }

    {
      std::lock_guard<std::shared_mutex> lock(state_mutex_);
      current_state_ = "running";
    }

    auto totalDuration = std::chrono::duration_cast<std::chrono::milliseconds>(
                             std::chrono::steady_clock::now() - totalStartTime).count();
    std::cout << "[Worker:" << instance_id_
              << "] ✓ Zero-downtime swap done in " << totalDuration << "ms (no RTMP reconnect)"
              << std::endl;
    std::cout << "[Worker:" << instance_id_
              << "] New pipeline nodes=" << newSnapshot->size()
              << ", output_leg_ active -> RTMP stream via FrameRouter"
              << std::endl;
    return true;
  }

  // Legacy path: stop old → [optional delay] → build new → start new (short gap, RTMP reconnects)
  std::cout << "[Worker:" << instance_id_
            << "] Pipeline swap (stop old → build new → start new)..." << std::endl;

  auto totalStartTime = std::chrono::steady_clock::now();
  PipelineSnapshotPtr oldSnapshot = setActivePipeline(nullptr);
  {
    std::lock_guard<std::mutex> lock(start_pipeline_mutex_);
    pipeline_running_.store(false);
  }
  stopSourceNodeForSnapshot(oldSnapshot);

  // Optional test delay BEFORE build so gap is explicit; new pipeline connects to RTMP only when started (no idle connection during delay)
  {
    const char* env = std::getenv("EDGE_AI_HOTSWAP_DELAY_SEC");
    int delaySec = env ? std::atoi(env) : 0;
    if (delaySec > 0) {
      std::cout << "[Worker:" << instance_id_
                << "] Hot-swap test delay: sleeping " << delaySec << "s before building new pipeline..."
                << std::endl;
      std::this_thread::sleep_for(std::chrono::seconds(delaySec));
      std::cout << "[Worker:" << instance_id_
                << "] Hot-swap test delay: done, building new pipeline"
                << std::endl;
    }
  }

  building_new_pipeline_.store(true);
  new_pipeline_nodes_.clear();
  if (!preBuildPipeline(tempConfig)) {
    building_new_pipeline_.store(false);
    std::cerr << "[Worker:" << instance_id_
              << "] Pre-build failed after stopping old pipeline; instance needs restart"
              << std::endl;
    return false;
  }
  building_new_pipeline_.store(false);
  auto buildEndTime = std::chrono::steady_clock::now();

  PipelineSnapshotPtr newSnapshot =
      std::make_shared<PipelineSnapshot>(std::move(new_pipeline_nodes_));
  new_pipeline_nodes_.clear();

  setActivePipeline(newSnapshot);
  config_ = tempConfig;
  setupFrameCaptureHook();
  setupQueueSizeTrackingHook();

  if (!startSourceNodeForSnapshot(newSnapshot)) {
    std::cerr << "[Worker:" << instance_id_
              << "] Failed to start new pipeline source after swap" << std::endl;
    return false;
  }

  {
    std::lock_guard<std::mutex> lock(start_pipeline_mutex_);
    pipeline_running_.store(true);
    start_time_ = std::chrono::steady_clock::now();
    last_fps_update_ = start_time_;
    frames_processed_.store(0);
    dropped_frames_.store(0);
    current_fps_.store(0.0);
  }
  {
    std::lock_guard<std::shared_mutex> lock(state_mutex_);
    current_state_ = "running";
  }

  auto totalDuration = std::chrono::duration_cast<std::chrono::milliseconds>(
                           std::chrono::steady_clock::now() - totalStartTime).count();
  auto buildDuration = std::chrono::duration_cast<std::chrono::milliseconds>(
                           buildEndTime - totalStartTime).count();
  std::cout << "[Worker:" << instance_id_
            << "] ✓ Pipeline swap done in " << totalDuration << "ms (build: "
            << buildDuration << "ms); stream output reconnects after old pipeline stopped)"
            << std::endl;
  return true;
}

bool WorkerHandler::preBuildPipeline(const Json::Value &newConfig) {
  if (!pipeline_builder_) {
    last_error_ = "Pipeline builder not initialized";
    return false;
  }

  try {
    // Parse config to CreateInstanceRequest
    CreateInstanceRequest req = parseCreateRequest(newConfig);

    // Zero-downtime: when we already have frame_router_ (persistent output leg),
    // the new pipeline MUST feed it via FrameRouterSinkNode. The builder only
    // adds FrameRouterSinkNode when req has RTMP URL. If newConfig omitted or
    // wiped output (e.g. PATCH only line but payload shape dropped RTMP), inject
    // RTMP URL from current running config_ so stream is not lost after swap.
    std::string rtmpFromReq = PipelineBuilderRequestUtils::getRTMPUrl(req);
    bool injected = false;
    if (frame_router_ && rtmpFromReq.empty()) {
      std::string fromConfig = PipelineBuilderRequestUtils::getRTMPUrl(parseCreateRequest(config_));
      if (!fromConfig.empty()) {
        req.additionalParams["RTMP_URL"] = fromConfig;
        req.additionalParams["RTMP_DES_URL"] = fromConfig;
        injected = true;
        std::cout << "[Worker:" << instance_id_
                  << "] Pre-build: injected RTMP URL from running config (zero-downtime stream preservation)"
                  << std::endl;
      }
    }
    // #region agent log
    {
      auto ts = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
      const char* logPath = "/home/cvedix/Data/DEV/DEV_1/api/.cursor/debug-408f41.log";
      std::ofstream df(logPath, std::ios::app);
      if (!df) { logPath = "/tmp/debug-408f41.log"; df.open(logPath, std::ios::app); }
      if (df) df << "{\"sessionId\":\"408f41\",\"location\":\"worker_handler_hotswap.cpp:preBuild_rtmp\",\"message\":\"preBuildPipeline RTMP\",\"data\":{\"getRTMPUrl_empty\":"
                 << (rtmpFromReq.empty() ? "true" : "false") << ",\"injected_from_config\":" << (injected ? "true" : "false") << "},\"timestamp\":" << ts << ",\"hypothesisId\":\"H3\"}\n";
    }
    // #endregion

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

    if (pipeline_builder_) {
      pipeline_builder_->ensureCVEDIXInitialized();
    }
    if (!ensureOutputLegForRtmp(req)) {
      return false;
    }

    // Build new pipeline (don't start it); use frame_router_ when RTMP for zero-downtime
    new_pipeline_nodes_ = pipeline_builder_->buildPipeline(optSolution.value(),
                                                           req, instance_id_, {},
                                                           frame_router_.get());

    if (new_pipeline_nodes_.empty()) {
      last_error_ = "Pipeline builder returned empty pipeline";
      return false;
    }

    std::cout << "[Worker:" << instance_id_ << "] Pre-built pipeline with "
              << new_pipeline_nodes_.size() << " nodes" << std::endl;
    return true;

  } catch (const char* msg) {
    last_error_ = std::string("Pre-build (SDK/lib threw string): ") + (msg ? msg : "(null)");
    std::cerr << "[Worker:" << instance_id_
              << "] Failed to pre-build pipeline: " << (msg ? msg : "(null)") << std::endl;
    return false;
  } catch (const std::exception &e) {
    last_error_ = e.what();
    std::cerr << "[Worker:" << instance_id_
              << "] Failed to pre-build pipeline: " << e.what() << std::endl;
    return false;
  } catch (...) {
    last_error_ = "Pre-build failed (unknown exception)";
    std::cerr << "[Worker:" << instance_id_
              << "] Failed to pre-build pipeline: unknown exception" << std::endl;
    return false;
  }
}

} // namespace worker
