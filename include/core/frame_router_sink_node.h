#pragma once

#include <string>
#include "cvedix/nodes/common/cvedix_node.h"
#include "cvedix/objects/cvedix_frame_meta.h"

namespace edgeos {
class FrameRouter;
}

namespace edgeos {

/**
 * @brief Terminal node that feeds the pipeline output into FrameRouter (persistent output leg).
 *
 * Replaces rtmp_des in the pipeline when using zero-downtime swap: OSD → this node → FrameRouter
 * → proxy → rtmp_des. Does not own FrameRouter; caller must ensure router outlives this node.
 */
class FrameRouterSinkNode : public cvedix_nodes::cvedix_node {
 public:
  FrameRouterSinkNode(const std::string& node_name, FrameRouter* frame_router);
  ~FrameRouterSinkNode() override;

  cvedix_nodes::cvedix_node_type node_type() override;

 protected:
  std::shared_ptr<cvedix_objects::cvedix_meta> handle_frame_meta(
      std::shared_ptr<cvedix_objects::cvedix_frame_meta> meta) override;
  std::shared_ptr<cvedix_objects::cvedix_meta> handle_control_meta(
      std::shared_ptr<cvedix_objects::cvedix_control_meta> meta) override;

 private:
  FrameRouter* frame_router_;
};

}  // namespace edgeos
