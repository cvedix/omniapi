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

WorkerHandler::PipelineSnapshotPtr WorkerHandler::getActivePipeline() const {
  if (frame_router_) {
    return frame_router_->getActivePipeline();
  }
  std::shared_lock<std::shared_mutex> lock(active_pipeline_mutex_);
  return active_pipeline_;
}

WorkerHandler::PipelineSnapshotPtr
WorkerHandler::setActivePipeline(PipelineSnapshotPtr newPipeline) {
  if (frame_router_) {
    return frame_router_->setActivePipeline(std::move(newPipeline));
  }
  std::unique_lock<std::shared_mutex> lock(active_pipeline_mutex_);
  PipelineSnapshotPtr old = std::move(active_pipeline_);
  active_pipeline_ = std::move(newPipeline);
  return old;
}

bool WorkerHandler::ensureOutputLegForRtmp(const CreateInstanceRequest &req) {
  std::string rtmpUrl = PipelineBuilderRequestUtils::getRTMPUrl(req);
  if (rtmpUrl.empty()) {
    return true;
  }
  if (output_leg_) {
    return true;
  }
  try {
    edgeos::PersistentOutputLegParams params;
    params.channel = 0;
    params.enableOSD = true;
    params.bitrate = 1024;
    output_leg_ = std::make_shared<edgeos::PersistentOutputLeg>(
        instance_id_, rtmpUrl, params);
    frame_router_ = std::make_unique<edgeos::FrameRouter>(output_leg_);
    std::cout << "[Worker:" << instance_id_
              << "] Created persistent output leg + frame router (zero-downtime RTMP)"
              << std::endl;
    return true;
  } catch (const char* msg) {
    std::cerr << "[Worker:" << instance_id_
              << "] Failed to create output leg (SDK/lib threw string): "
              << (msg ? msg : "(null)") << std::endl;
    last_error_ = std::string("RTMP output leg: ") + (msg ? msg : "unknown");
    return false;
  } catch (const std::exception &e) {
    std::cerr << "[Worker:" << instance_id_
              << "] Failed to create output leg: " << e.what() << std::endl;
    last_error_ = e.what();
    return false;
  } catch (...) {
    std::cerr << "[Worker:" << instance_id_
              << "] Failed to create output leg: unknown exception (e.g. SDK threw non-std::exception)"
              << std::endl;
    last_error_ = "Failed to create RTMP output leg (SDK/library threw non-standard exception)";
    return false;
  }
}

void WorkerHandler::startLastFramePumpThread() {
  if (last_frame_pump_running_.exchange(true)) {
    return;
  }
  last_frame_pump_stop_.store(false);
  last_frame_pump_thread_ = std::thread([this]() {
    const auto interval = std::chrono::milliseconds(33);  // ~30 fps
    while (!last_frame_pump_stop_.load() && output_leg_ && frame_router_) {
      cv::Mat frame = frame_router_->getLastFrameCopy();
      if (!frame.empty()) {
        output_leg_->injectFrame(frame);
      }
      std::this_thread::sleep_for(interval);
    }
    last_frame_pump_running_.store(false);
  });
  std::cout << "[Worker:" << instance_id_ << "] Last-frame pump started"
            << std::endl;
}

void WorkerHandler::stopLastFramePumpThread() {
  last_frame_pump_stop_.store(true);
  if (last_frame_pump_thread_.joinable()) {
    last_frame_pump_thread_.join();
  }
  std::cout << "[Worker:" << instance_id_ << "] Last-frame pump stopped"
            << std::endl;
}

void WorkerHandler::drainPipelineSnapshot(PipelineSnapshotPtr &snapshot) {
  if (!snapshot || snapshot->empty()) {
    return;
  }
  const auto drainMs = std::chrono::milliseconds(150);
  std::this_thread::sleep_for(drainMs);
}

void WorkerHandler::sendReadySignal() {
  std::cout << "[Worker:" << instance_id_ << "] Sending ready signal"
            << std::endl;
}

} // namespace worker
