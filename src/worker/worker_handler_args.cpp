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

WorkerArgs WorkerArgs::parse(int argc, char *argv[]) {
  WorkerArgs args;

  static struct option long_options[] = {
      {"instance-id", required_argument, 0, 'i'},
      {"socket", required_argument, 0, 's'},
      {"config", required_argument, 0, 'c'},
      {"help", no_argument, 0, 'h'},
      {0, 0, 0, 0}};

  int opt;
  int option_index = 0;

  // Reset getopt
  optind = 1;

  while ((opt = getopt_long(argc, argv, "i:s:c:h", long_options,
                            &option_index)) != -1) {
    switch (opt) {
    case 'i':
      args.instance_id = optarg;
      break;
    case 's':
      args.socket_path = optarg;
      break;
    case 'c': {
      // Parse JSON config
      Json::CharReaderBuilder builder;
      std::istringstream stream(optarg);
      std::string errors;
      if (!Json::parseFromStream(builder, stream, &args.config, &errors)) {
        args.error = "Failed to parse config JSON: " + errors;
        return args;
      }
      break;
    }
    case 'h':
      args.error = "Usage: edgeos-worker --instance-id <id> --socket <path> "
                   "[--config <json>]";
      return args;
    default:
      args.error = "Unknown option";
      return args;
    }
  }

  // Validate required arguments
  if (args.instance_id.empty()) {
    args.error = "Missing required argument: --instance-id";
    return args;
  }

  if (args.socket_path.empty()) {
    args.error = "Missing required argument: --socket";
    return args;
  }

  args.valid = true;
  return args;
}

} // namespace worker
