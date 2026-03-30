#pragma once

#include <opencv2/core.hpp>
#include <memory>
#include <string>

namespace edgeos {
class RtmpLastFrameFallbackProxyNode;
}
namespace cvedix_nodes {
class cvedix_rtmp_des_node;
}
namespace cvedix_objects {
struct cvedix_size;
}

namespace edgeos {

/**
 * @brief Parameters for creating the persistent RTMP output leg (proxy + rtmp_des).
 */
struct PersistentOutputLegParams {
  int channel = 0;
  bool enableOSD = true;
  int bitrate = 1024;
};

/**
 * @brief Persistent output leg: proxy + rtmp_des, kept alive across pipeline swaps.
 *
 * Created once per instance when RTMP output is used; frames are fed via
 * injectFrame() from FrameRouter or last-frame pump. Never torn down during
 * hot swap so RTMP connection (ZLMediaKit) is not dropped.
 */
class PersistentOutputLeg {
 public:
  using ProxyPtr = std::shared_ptr<RtmpLastFrameFallbackProxyNode>;
  using RtmpDesPtr = std::shared_ptr<cvedix_nodes::cvedix_rtmp_des_node>;

  PersistentOutputLeg(const std::string& instanceId,
                      const std::string& rtmpUrl,
                      const PersistentOutputLegParams& params);

  ~PersistentOutputLeg();

  PersistentOutputLeg(const PersistentOutputLeg&) = delete;
  PersistentOutputLeg& operator=(const PersistentOutputLeg&) = delete;

  ProxyPtr proxy() const { return proxy_; }
  RtmpDesPtr rtmpDes() const { return rtmp_des_; }

  /** Push a frame into the proxy (from FrameRouter or last-frame pump). Thread-safe. */
  void injectFrame(const cv::Mat& frame);

 private:
  std::string instance_id_;
  ProxyPtr proxy_;
  RtmpDesPtr rtmp_des_;
};

}  // namespace edgeos
