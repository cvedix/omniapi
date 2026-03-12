#include "core/pipeline_snapshot.h"
#include <cvedix/nodes/common/cvedix_node.h>

PipelineSnapshot::PipelineSnapshot(NodeVector nodes)
    : nodes_(std::move(nodes)) {}

PipelineSnapshot::~PipelineSnapshot() {
  for (auto it = nodes_.rbegin(); it != nodes_.rend(); ++it) {
    auto &node = *it;
    if (node) {
      try {
        node->detach_recursively();
      } catch (...) {
        // Ignore during teardown
      }
    }
  }
}

PipelineSnapshot::NodePtr PipelineSnapshot::sourceNode() const {
  return nodes_.empty() ? nullptr : nodes_.front();
}
