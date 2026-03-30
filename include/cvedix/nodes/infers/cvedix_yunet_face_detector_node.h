#pragma once

// Compatibility shim for SDK variants that do not ship
// cvedix_yunet_face_detector_node.h.

#include <cvedix/nodes/common/cvedix_node.h>

namespace cvedix_nodes {

class cvedix_yunet_face_detector_node : public cvedix_node {
public:
  explicit cvedix_yunet_face_detector_node(const std::string& node_name)
      : cvedix_node(node_name) {}
  ~cvedix_yunet_face_detector_node() override = default;

  cvedix_node_type node_type() override { return cvedix_node_type::MID; }
};

}  // namespace cvedix_nodes
