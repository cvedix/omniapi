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

bool WorkerHandler::startSourceNodeForSnapshot(PipelineSnapshotPtr snapshot) {
  if (!snapshot || snapshot->empty()) {
    return false;
  }
  auto sourceNode = snapshot->sourceNode();
  auto rtspNode =
      std::dynamic_pointer_cast<cvedix_nodes::cvedix_rtsp_src_node>(sourceNode);
  if (rtspNode) {
    rtspNode->start();
    return true;
  }
  auto rtmpNode =
      std::dynamic_pointer_cast<cvedix_nodes::cvedix_rtmp_src_node>(sourceNode);
  if (rtmpNode) {
    rtmpNode->start();
    return true;
  }
  auto fileNode =
      std::dynamic_pointer_cast<cvedix_nodes::cvedix_file_src_node>(sourceNode);
  if (fileNode) {
    fileNode->start();
    return true;
  }
  auto imageNode =
      std::dynamic_pointer_cast<cvedix_nodes::cvedix_image_src_node>(sourceNode);
  if (imageNode) {
    imageNode->start();
    return true;
  }
  auto udpNode =
      std::dynamic_pointer_cast<cvedix_nodes::cvedix_udp_src_node>(sourceNode);
  if (udpNode) {
    udpNode->start();
    return true;
  }
  auto appSrcNode =
      std::dynamic_pointer_cast<cvedix_nodes::cvedix_app_src_node>(sourceNode);
  if (appSrcNode) {
    appSrcNode->start();
    return true;
  }
  return false;
}

void WorkerHandler::stopSourceNodeForSnapshot(PipelineSnapshotPtr &snapshot,
                                              bool releaseSnapshot) {
  if (!snapshot || snapshot->empty()) {
    return;
  }
  auto node = snapshot->sourceNode();
  if (!node) {
    if (releaseSnapshot) snapshot.reset();
    return;
  }
  try {
    auto rtspNode =
        std::dynamic_pointer_cast<cvedix_nodes::cvedix_rtsp_src_node>(node);
    if (rtspNode) {
      rtspNode->stop();
    } else {
      auto rtmpNode =
          std::dynamic_pointer_cast<cvedix_nodes::cvedix_rtmp_src_node>(node);
      if (rtmpNode) {
        rtmpNode->stop();
      } else {
        auto imageNode =
            std::dynamic_pointer_cast<cvedix_nodes::cvedix_image_src_node>(node);
        if (imageNode) {
          imageNode->stop();
        } else {
          auto udpNode =
              std::dynamic_pointer_cast<cvedix_nodes::cvedix_udp_src_node>(node);
          if (udpNode) {
            udpNode->stop();
          } else {
            auto appSrcNode =
                std::dynamic_pointer_cast<cvedix_nodes::cvedix_app_src_node>(node);
            if (appSrcNode) {
              appSrcNode->stop();
            } else {
              auto fileNode =
                  std::dynamic_pointer_cast<cvedix_nodes::cvedix_file_src_node>(node);
              if (fileNode) {
                fileNode->stop();
              }
            }
          }
        }
      }
    }
    node->detach_recursively();
  } catch (const std::exception &e) {
    std::cerr << "[Worker:" << instance_id_
              << "] Error stopping old pipeline source: " << e.what() << std::endl;
  }
  if (releaseSnapshot) {
    snapshot.reset();
  }
}

} // namespace worker
