#include "core/pipeline_builder.h"
#include "solutions/solution_registry.h"
#include <gtest/gtest.h>
#include <json/json.h>
#include <cvedix/nodes/ba/cvedix_ba_area_jam_node.h>

using namespace std;

TEST(BAJamPipelineTest, CreateBAJamNode) {
  // Build a minimal solution that includes a file source, sort tracker, ba_jam and file destination
  SolutionConfig config;
  config.solutionId = "test_ba_jam_pipeline";

  SolutionConfig::NodeConfig fileSrc;
  fileSrc.nodeType = "file_src";
  fileSrc.nodeName = "file_src_{instanceId}";
  fileSrc.parameters["file_path"] = "/tmp/test_video.mp4";
  fileSrc.parameters["channel"] = "0";
  config.pipeline.push_back(fileSrc);

  SolutionConfig::NodeConfig sortTrack;
  sortTrack.nodeType = "sort_track";
  sortTrack.nodeName = "sort_{instanceId}";
  config.pipeline.push_back(sortTrack);

  SolutionConfig::NodeConfig baJam;
  baJam.nodeType = "ba_jam";
  baJam.nodeName = "ba_jam_{instanceId}";
  baJam.parameters["JamZones"] = "[]"; // empty zones
  config.pipeline.push_back(baJam);

  SolutionConfig::NodeConfig fileDes;
  fileDes.nodeType = "file_des";
  fileDes.nodeName = "file_des_{instanceId}";
  fileDes.parameters["save_dir"] = "/tmp";
  config.pipeline.push_back(fileDes);

  PipelineBuilder builder;
  CreateInstanceRequest req;
  req.additionalParams["FILE_PATH"] = "/tmp/test_video.mp4";

  std::string instanceId = "test_123";
  auto nodes = builder.buildPipeline(config, req, instanceId);

  bool found_ba_jam = false;
  for (const auto &n : nodes) {
    if (dynamic_cast<cvedix_nodes::cvedix_ba_area_jam_node *>(n.get()) != nullptr) {
      found_ba_jam = true;
      break;
    }
  }

  EXPECT_TRUE(found_ba_jam);
}
