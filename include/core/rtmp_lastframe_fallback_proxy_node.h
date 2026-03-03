/**
 * @file rtmp_lastframe_fallback_proxy_node.h
 * @brief MID node: when frame is empty, forwards last valid frame to rtmp_des
 *        to keep RTMP connection alive (e.g. ZLMediaKit keepAlive).
 */

#pragma once

#include <memory>
#include <string>
#include <opencv2/core.hpp>
#include "cvedix/nodes/common/cvedix_node.h"
#include "cvedix/objects/cvedix_frame_meta.h"

namespace edgeos {

/**
 * Proxy between OSD and rtmp_des. If incoming frame is empty, sends
 * last valid frame so the RTMP destination always has data to push.
 */
class RtmpLastFrameFallbackProxyNode : public cvedix_nodes::cvedix_node {
 public:
  explicit RtmpLastFrameFallbackProxyNode(const std::string& node_name);
  ~RtmpLastFrameFallbackProxyNode() override;

  cvedix_nodes::cvedix_node_type node_type() override;

 protected:
  std::shared_ptr<cvedix_objects::cvedix_meta> handle_frame_meta(
      std::shared_ptr<cvedix_objects::cvedix_frame_meta> meta) override;
  std::shared_ptr<cvedix_objects::cvedix_meta> handle_control_meta(
      std::shared_ptr<cvedix_objects::cvedix_control_meta> meta) override;

 private:
  cv::Mat last_frame_;
  std::mutex last_frame_mutex_;
};

}  // namespace edgeos
