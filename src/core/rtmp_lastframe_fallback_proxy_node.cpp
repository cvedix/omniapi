/**
 * @file rtmp_lastframe_fallback_proxy_node.cpp
 * @brief When frame is empty, forward last valid frame to rtmp_des to keep connection.
 */

#include "core/rtmp_lastframe_fallback_proxy_node.h"
#include <cvedix/objects/cvedix_frame_meta.h>
#include <mutex>

namespace edgeos {

RtmpLastFrameFallbackProxyNode::RtmpLastFrameFallbackProxyNode(
    const std::string& node_name)
    : cvedix_nodes::cvedix_node(node_name) {
  initialized();
}

RtmpLastFrameFallbackProxyNode::~RtmpLastFrameFallbackProxyNode() {
  deinitialized();
}

cvedix_nodes::cvedix_node_type
RtmpLastFrameFallbackProxyNode::node_type() {
  return cvedix_nodes::cvedix_node_type::MID;
}

std::shared_ptr<cvedix_objects::cvedix_meta>
RtmpLastFrameFallbackProxyNode::handle_frame_meta(
    std::shared_ptr<cvedix_objects::cvedix_frame_meta> meta) {
  if (!meta) {
    return nullptr;
  }

  const cv::Mat* frame_to_use = nullptr;
  bool use_last = false;

  if (!meta->osd_frame.empty()) {
    frame_to_use = &meta->osd_frame;
  } else if (!meta->frame.empty()) {
    frame_to_use = &meta->frame;
  } else {
    std::lock_guard<std::mutex> lock(last_frame_mutex_);
    if (!last_frame_.empty()) {
      frame_to_use = &last_frame_;
      use_last = true;
    }
  }

  if (!frame_to_use || frame_to_use->empty()) {
    return nullptr;
  }

  std::shared_ptr<cvedix_objects::cvedix_frame_meta> out_meta;

  if (use_last) {
    std::lock_guard<std::mutex> lock(last_frame_mutex_);
    out_meta = std::make_shared<cvedix_objects::cvedix_frame_meta>(*meta);
    out_meta->frame = last_frame_.clone();
    out_meta->osd_frame = last_frame_.clone();
  } else {
    {
      std::lock_guard<std::mutex> lock(last_frame_mutex_);
      frame_to_use->copyTo(last_frame_);
    }
    out_meta = meta;
  }

  // cvedix_rtmp_des_node requires meta->fps > 0 (OpenCV GStreamer assertion).
  if (out_meta && out_meta->fps <= 0) {
    out_meta->fps = 25.0;
  }

  return out_meta;
}

std::shared_ptr<cvedix_objects::cvedix_meta>
RtmpLastFrameFallbackProxyNode::handle_control_meta(
    std::shared_ptr<cvedix_objects::cvedix_control_meta> meta) {
  return meta;
}

void RtmpLastFrameFallbackProxyNode::inject_frame(const cv::Mat& frame) {
  if (frame.empty()) {
    return;
  }
  cv::Mat frame_copy;
  {
    std::lock_guard<std::mutex> lock(last_frame_mutex_);
    frame.copyTo(last_frame_);
    frame_copy = last_frame_.clone();
  }
  int w = frame_copy.cols;
  int h = frame_copy.rows;
  auto meta = std::make_shared<cvedix_objects::cvedix_frame_meta>(
      frame_copy, -1, -1, w, h, 0);
  meta->osd_frame = frame_copy;
  if (meta->fps <= 0) {
    meta->fps = 25.0;  // rtmp_des requires fps > 0
  }
  meta_flow(meta);
}

}  // namespace edgeos
