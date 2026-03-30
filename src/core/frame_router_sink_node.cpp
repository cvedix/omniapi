/**
 * @file frame_router_sink_node.cpp
 * @brief Sink node that forwards frames to FrameRouter for zero-downtime output.
 */

#include "core/frame_router_sink_node.h"
#include "core/frame_router.h"
#include <opencv2/core.hpp>

namespace edgeos {

FrameRouterSinkNode::FrameRouterSinkNode(const std::string& node_name,
                                       FrameRouter* frame_router)
    : cvedix_nodes::cvedix_node(node_name), frame_router_(frame_router) {
  initialized();
}

FrameRouterSinkNode::~FrameRouterSinkNode() {
  deinitialized();
}

cvedix_nodes::cvedix_node_type FrameRouterSinkNode::node_type() {
  return cvedix_nodes::cvedix_node_type::MID;
}

std::shared_ptr<cvedix_objects::cvedix_meta> FrameRouterSinkNode::handle_frame_meta(
    std::shared_ptr<cvedix_objects::cvedix_frame_meta> meta) {
  if (!meta || !frame_router_) {
    return meta;
  }

  const cv::Mat* frame_to_send = nullptr;
  if (!meta->osd_frame.empty()) {
    frame_to_send = &meta->osd_frame;
  } else if (!meta->frame.empty()) {
    frame_to_send = &meta->frame;
  }

  if (frame_to_send && !frame_to_send->empty()) {
    frame_router_->submitFrame(*frame_to_send);
  }

  return meta;
}

std::shared_ptr<cvedix_objects::cvedix_meta> FrameRouterSinkNode::handle_control_meta(
    std::shared_ptr<cvedix_objects::cvedix_control_meta> meta) {
  return meta;
}

}  // namespace edgeos
