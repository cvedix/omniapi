/**
 * @file persistent_output_leg.cpp
 * @brief Persistent RTMP output leg (proxy + rtmp_des) for zero-downtime pipeline swap.
 */

#include "core/persistent_output_leg.h"
#include "core/rtmp_lastframe_fallback_proxy_node.h"
#include <cvedix/nodes/des/cvedix_rtmp_des_node.h>
#include <cvedix/objects/shapes/cvedix_size.h>
#include <iostream>

namespace edgeos {

PersistentOutputLeg::PersistentOutputLeg(const std::string& instanceId,
                                         const std::string& rtmpUrl,
                                         const PersistentOutputLegParams& params) {
  if (rtmpUrl.empty()) {
    throw std::invalid_argument("PersistentOutputLeg: RTMP URL cannot be empty");
  }

  std::string rtmpNodeName = "rtmp_des_persistent_" + instanceId;
  std::string proxyName = "rtmp_lastframe_proxy_" + instanceId;

  if (params.enableOSD) {
    cvedix_objects::cvedix_size resolution = {};
    int bitrate = params.bitrate <= 0 ? 1024 : params.bitrate;
    rtmp_des_ = std::make_shared<cvedix_nodes::cvedix_rtmp_des_node>(
        rtmpNodeName, params.channel, rtmpUrl, resolution, bitrate,
        true /* enable OSD overlay */);
  } else {
    rtmp_des_ = std::make_shared<cvedix_nodes::cvedix_rtmp_des_node>(
        rtmpNodeName, params.channel, rtmpUrl);
  }

  proxy_ = std::make_shared<RtmpLastFrameFallbackProxyNode>(proxyName);
  rtmp_des_->attach_to({proxy_});

  instance_id_ = instanceId;
  std::cerr << "[PersistentOutputLeg:" << instance_id_
            << "] Created (proxy + rtmp_des) for URL: " << rtmpUrl << std::endl;
}

PersistentOutputLeg::~PersistentOutputLeg() {
  // NOTE: rtmp_des teardown (via detach/destroy) must send TCP FIN: shutdown(socket_fd, SHUT_RDWR); close(socket_fd); (SDK requirement, see ZERO_DOWNTIME_ATOMIC_PIPELINE_SWAP_DESIGN.md §12).
  if (proxy_) {
    try {
      proxy_->detach_recursively();
    } catch (...) {
      // Ignore during teardown
    }
  }
  std::cerr << "[PersistentOutputLeg:" << instance_id_ << "] Destroyed" << std::endl;
}

void PersistentOutputLeg::injectFrame(const cv::Mat& frame) {
  if (proxy_ && !frame.empty()) {
    proxy_->inject_frame(frame);
  }
}

}  // namespace edgeos
