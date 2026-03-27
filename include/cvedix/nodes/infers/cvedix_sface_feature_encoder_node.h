#pragma once

// Compatibility shim for SDK variants that do not ship
// cvedix_sface_feature_encoder_node.h.

#include <cvedix/nodes/common/cvedix_node.h>

namespace cvedix_nodes {

class cvedix_sface_feature_encoder_node : public cvedix_node {
public:
  explicit cvedix_sface_feature_encoder_node(const std::string& node_name)
      : cvedix_node(node_name) {}
  ~cvedix_sface_feature_encoder_node() override = default;

  cvedix_node_type node_type() override { return cvedix_node_type::MID; }
};

}  // namespace cvedix_nodes
