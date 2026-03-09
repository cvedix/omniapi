#include <cstdlib>
#include <iostream>
#include <map>
#include <memory>
#include <string>

// CVEDIX SDK public headers
// #include <cvedix/cvedix_version.h>  // File not available in edgeos-sdk
#include <cvedix/nodes/ba/cvedix_ba_line_crossline_node.h>
#include <cvedix/nodes/des/cvedix_rtmp_des_node.h>
#include <cvedix/nodes/des/cvedix_screen_des_node.h>
#include <cvedix/nodes/infers/cvedix_yolo_detector_node.h>
#include <cvedix/nodes/osd/cvedix_ba_line_crossline_osd_node.h>
#include <cvedix/nodes/src/cvedix_rtsp_src_node.h>
#include <cvedix/nodes/track/cvedix_sort_track_node.h>
#include <cvedix/objects/shapes/cvedix_line.h>
#include <cvedix/objects/shapes/cvedix_point.h>
#include <cvedix/objects/shapes/cvedix_size.h>
#include <cvedix/utils/analysis_board/cvedix_analysis_board.h>
#include <cvedix/utils/cvedix_utils.h>

/**
 * Ví dụ: RTSP Behaviour Analysis - Crossline với CVEDIX SDK
 *
 * Pipeline:
 *   RTSP Source -> YOLO Detector -> SORT Tracker -> Crossline BA -> Crossline
 * OSD -> Screen/RTMP
 *
 * Yêu cầu:
 *   - Tải dataset/mô hình: cvedix_data (xem README)
 *   - Đặt biến môi trường CVEDIX_DATA_ROOT hoặc chỉnh sửa đường dẫn bên dưới
 *
 * Build & run:
 *   mkdir build && cd build
 *   cmake ..
 *   make
 *   ./example_using_sdk
 */

static std::string resolve_path(const std::string &relative) {
  const char *root = std::getenv("CVEDIX_DATA_ROOT");
  if (root == nullptr) {
    return "./cvedix_data/" + relative;
  }
  std::string base(root);
  if (!base.empty() && base.back() != '/') {
    base += '/';
  }
  return base + relative;
}

static bool has_display() {
#if defined(_WIN32)
  return true;
#else
  const char *display = std::getenv("DISPLAY");
  const char *wayland = std::getenv("WAYLAND_DISPLAY");
  return (display && display[0] != '\0') || (wayland && wayland[0] != '\0');
#endif
}

static bool has_gstreamer_element(const std::string &element) {
#if defined(_WIN32)
  (void)element;
  return true;
#else
  std::string command = "gst-inspect-1.0 " + element + " >/dev/null 2>&1";
  int status = std::system(command.c_str());
  return status == 0;
#endif
}

int main() {
  std::cout << "========================================" << std::endl;
  std::cout << "CVEDIX SDK - RTSP Crossline Sample" << std::endl;
  std::cout << "========================================" << std::endl;
  // Version info not available in edgeos-sdk
  // std::cout << "Version: " << CVEDIX_VERSION << std::endl;
  // std::cout << "Build Time: " << CVEDIX_BUILD_TIME << std::endl;
  // std::cout << "Git Commit: " << CVEDIX_GIT_COMMIT << std::endl;
  std::cout << std::endl;

  // RTSP / RTMP configuration
  // SECURITY: Require environment variables - no hardcoded URLs
  const char *env_rtsp = std::getenv("CVEDIX_RTSP_URL");
  const char *env_rtmp = std::getenv("CVEDIX_RTMP_URL");

  if (!env_rtsp || strlen(env_rtsp) == 0) {
    std::cerr << "[ERROR] CVEDIX_RTSP_URL environment variable is required"
              << std::endl;
    std::cerr << "[ERROR] Please set it before running: export "
                 "CVEDIX_RTSP_URL=rtsp://your-server:port/stream"
              << std::endl;
    return EXIT_FAILURE;
  }

  if (!env_rtmp || strlen(env_rtmp) == 0) {
    std::cerr << "[ERROR] CVEDIX_RTMP_URL environment variable is required"
              << std::endl;
    std::cerr << "[ERROR] Please set it before running: export "
                 "CVEDIX_RTMP_URL=rtmp://your-server:port/live/stream"
              << std::endl;
    return EXIT_FAILURE;
  }

  const std::string rtsp_url = env_rtsp;
  const std::string rtmp_url = env_rtmp;

  // Chuẩn bị đường dẫn dữ liệu/mô hình
  const std::string weights_path =
      resolve_path("models/det_cls/yolov3-tiny-2022-0721_best.weights");
  const std::string config_path =
      resolve_path("models/det_cls/yolov3-tiny-2022-0721.cfg");
  const std::string labels_path =
      resolve_path("models/det_cls/yolov3_tiny_5classes.txt");

  std::cout << "RTSP URL:     " << rtsp_url << std::endl;
  std::cout << "RTMP URL:     " << rtmp_url << std::endl;
  std::cout << "Weights:      " << weights_path << std::endl;
  std::cout << "Config:       " << config_path << std::endl;
  std::cout << "Labels:       " << labels_path << std::endl;
  std::cout << std::endl;

  // Khởi tạo logger
  CVEDIX_SET_LOG_LEVEL(cvedix_utils::cvedix_log_level::INFO);
  CVEDIX_LOGGER_INIT();

  try {
    const bool display_available = has_display();
    const bool textoverlay_available = has_gstreamer_element("textoverlay");

    if (!display_available) {
      std::cerr << "[WARN] DISPLAY/WAYLAND not found. Screen DES node will be "
                   "skipped."
                << std::endl;
    }
    if (!textoverlay_available) {
      std::cerr << "[WARN] GStreamer element 'textoverlay' not found. Screen "
                   "DES node requires this plugin and will be disabled."
                << std::endl;
    }

    // 1. Tạo các node trong pipeline
    auto rtsp_src_0 = std::make_shared<cvedix_nodes::cvedix_rtsp_src_node>(
        "rtsp_src_0", 0, rtsp_url,
        0.6f // scale factor
    );

    auto yolo_detector =
        std::make_shared<cvedix_nodes::cvedix_yolo_detector_node>(
            "yolo_detector", weights_path, config_path, labels_path);

    auto tracker =
        std::make_shared<cvedix_nodes::cvedix_sort_track_node>("sort_tracker");

    cvedix_objects::cvedix_point start(0, 250);
    cvedix_objects::cvedix_point end(700, 220);
    std::map<int, cvedix_objects::cvedix_line> lines = {
        {0, cvedix_objects::cvedix_line(start, end)}};
    auto ba_crossline =
        std::make_shared<cvedix_nodes::cvedix_ba_line_crossline_node>("ba_crossline",
                                                                 lines);

    auto osd =
        std::make_shared<cvedix_nodes::cvedix_ba_line_crossline_osd_node>("osd");

    std::shared_ptr<cvedix_nodes::cvedix_screen_des_node> screen_des_0;
    if (display_available && textoverlay_available) {
      screen_des_0 = std::make_shared<cvedix_nodes::cvedix_screen_des_node>(
          "screen_des_0", 0);
    }

    std::shared_ptr<cvedix_nodes::cvedix_rtmp_des_node> rtmp_des_0;
    if (textoverlay_available) {
      rtmp_des_0 = std::make_shared<cvedix_nodes::cvedix_rtmp_des_node>(
          "rtmp_des_0", 0, rtmp_url);
    } else {
      rtmp_des_0 = std::make_shared<cvedix_nodes::cvedix_rtmp_des_node>(
          "rtmp_des_0", 0, rtmp_url, cvedix_objects::cvedix_size{}, 1024,
          false);
    }

    // 2. Kết nối pipeline
    yolo_detector->attach_to({rtsp_src_0});
    tracker->attach_to({yolo_detector});
    ba_crossline->attach_to({tracker});
    osd->attach_to({ba_crossline});
    if (screen_des_0) {
      screen_des_0->attach_to({osd});
    }
    rtmp_des_0->attach_to({osd});

    // 3. Khởi động pipeline
    rtsp_src_0->start();

    if (screen_des_0) {
      std::cout
          << "Pipeline started. Screen DES will display the stream locally."
          << std::endl;
    } else {
      std::cout << "Pipeline started without local display (screen disabled)."
                << std::endl;
    }
    std::cout << "RTMP output streaming to: " << rtmp_url << std::endl;
    std::cout << "Press ENTER to stop..." << std::endl;

    // 4. Hiển thị bảng phân tích để debug / theo dõi pipeline
    cvedix_utils::cvedix_analysis_board board({rtsp_src_0});
    if (screen_des_0) {
      board.display(1, false); // refresh mỗi 1s, không auto-close
    }

    // 5. Chờ người dùng kết thúc
    std::string wait;
    std::getline(std::cin, wait);

    // 6. Giải phóng pipeline
    rtsp_src_0->detach_recursively();
    std::cout << "Pipeline stopped." << std::endl;
  } catch (const std::exception &ex) {
    std::cerr << "[ERROR] " << ex.what() << std::endl;
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}
