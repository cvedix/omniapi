#pragma once

// Compatibility shim for SDK variants that do not ship
// cvedix_openpose_detector_node.h.
//
// The codebase uses dynamic_pointer_cast to this type in a few places.
// A lightweight declaration is sufficient for compile-time type checks.

#include <cvedix/nodes/common/cvedix_node.h>

namespace cvedix_nodes {

class cvedix_openpose_detector_node : public cvedix_node {
public:
  explicit cvedix_openpose_detector_node(const std::string& node_name)
      : cvedix_node(node_name) {}
  ~cvedix_openpose_detector_node() override = default;

  cvedix_node_type node_type() override { return cvedix_node_type::MID; }
};

}  // namespace cvedix_nodes
