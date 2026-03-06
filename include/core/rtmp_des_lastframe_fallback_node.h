#pragma once

#ifdef CVEDIX_WITH_GSTREAMER
#include <opencv2/core.hpp>
#include <opencv2/videoio.hpp>
#include <string>
#include <mutex>
#include "cvedix/nodes/common/cvedix_des_node.h"
#include "cvedix/objects/cvedix_frame_meta.h"
#include "cvedix/objects/shapes/cvedix_size.h"

namespace edgeos {

class RtmpDesLastFrameFallbackNode : public cvedix_nodes::cvedix_des_node {
 public:
  RtmpDesLastFrameFallbackNode(std::string node_name, int channel_index,
                               std::string rtmp_url,
                               cvedix_objects::cvedix_size resolution_w_h = {},
                               int bitrate = 1024, bool osd = true,
                               std::string gst_encoder_name = "x264enc");
  ~RtmpDesLastFrameFallbackNode() override;
  std::string to_string() override;

  std::string rtmp_url;
  cvedix_objects::cvedix_size resolution_w_h;
  int bitrate = 1024;
  bool osd = true;
  std::string gst_encoder_name = "x264enc";

 protected:
  std::shared_ptr<cvedix_objects::cvedix_meta> handle_frame_meta(
      std::shared_ptr<cvedix_objects::cvedix_frame_meta> meta) override;
  std::shared_ptr<cvedix_objects::cvedix_meta> handle_control_meta(
      std::shared_ptr<cvedix_objects::cvedix_control_meta> meta) override;

 private:
  bool ensureWriterOpen(int width, int height, double fps = 25.0);
  void closeWriter();

  std::string gst_pipeline_;
  cv::VideoWriter rtmp_writer_;
  cv::Mat last_frame_;
  std::mutex writer_mutex_;
  bool writer_open_ = false;
  int last_width_ = 0;
  int last_height_ = 0;
};

}
#endif
