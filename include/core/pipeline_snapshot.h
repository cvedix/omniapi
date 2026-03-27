#pragma once

#include <memory>
#include <vector>

namespace cvedix_nodes {
class cvedix_node;
}

/**
 * @brief Immutable snapshot of a pipeline (graph of nodes).
 *
 * Used by the Pipeline Engine for atomic pipeline swap (zero-downtime update).
 * Once built, the node list is immutable. The runtime always reads the
 * active pipeline via an atomic/shared pointer; swap is O(1) and never
 * blocks the worker loop.
 *
 * Memory safety: When the last shared_ptr to a PipelineSnapshot is released,
 * the destructor detaches all nodes recursively so resources are freed.
 */
class PipelineSnapshot {
public:
  using NodePtr = std::shared_ptr<cvedix_nodes::cvedix_node>;
  using NodeVector = std::vector<NodePtr>;

  /** Build snapshot from vector of nodes (takes ownership of the vector). */
  explicit PipelineSnapshot(NodeVector nodes);

  ~PipelineSnapshot();

  PipelineSnapshot(const PipelineSnapshot &) = delete;
  PipelineSnapshot &operator=(const PipelineSnapshot &) = delete;

  bool empty() const { return nodes_.empty(); }
  size_t size() const { return nodes_.size(); }

  /** Const reference to the node list. */
  const NodeVector &nodes() const { return nodes_; }

  /** First node (source node), or nullptr if empty. */
  NodePtr sourceNode() const;

private:
  NodeVector nodes_;
};
