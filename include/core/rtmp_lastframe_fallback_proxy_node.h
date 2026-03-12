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
 *
 * inject_frame() allows an external "last-frame pump" to push a frame
 * (e.g. during instance update / hot swap) so the RTMP connection is
 * kept alive and the stream server does not drop the stream key.
 */
class RtmpLastFrameFallbackProxyNode : public cvedix_nodes::cvedix_node {
 public:
  explicit RtmpLastFrameFallbackProxyNode(const std::string& node_name);
  ~RtmpLastFrameFallbackProxyNode() override;

  cvedix_nodes::cvedix_node_type node_type() override;

  /**
   * Inject a frame from outside (e.g. last captured frame) so it is
   * forwarded to rtmp_des. Use during update/hot-swap to keep sending
   * while the new pipeline is building, avoiding stream disconnect.
   * Thread-safe; no-op if frame is empty.
   */
  void inject_frame(const cv::Mat& frame);

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
