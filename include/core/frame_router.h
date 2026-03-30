#pragma once

#include "core/pipeline_snapshot.h"
#include <opencv2/core.hpp>
#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>

namespace edgeos {
class PersistentOutputLeg;
}

namespace edgeos {

/**
 * @brief Routes frames from the active pipeline (or last-frame pump) to the persistent output leg.
 *
 * Holds the active PipelineSnapshot pointer; swap is a single write so the frame path
 * stays lock-free for readers. Used for zero-downtime pipeline swap: AI leg is swappable,
 * output leg (proxy + rtmp_des) is persistent.
 */
class FrameRouter {
 public:
  using PipelineSnapshotPtr = std::shared_ptr<PipelineSnapshot>;

  explicit FrameRouter(std::shared_ptr<PersistentOutputLeg> outputLeg);

  /** Set active pipeline; returns the previous snapshot (for stop + drain). */
  PipelineSnapshotPtr setActivePipeline(PipelineSnapshotPtr snapshot);
  PipelineSnapshotPtr getActivePipeline() const;

  /**
   * Submit a frame to the output leg (from pipeline terminal node or last-frame pump).
   * When last-frame pump is active, still updates last_frame_ but does not forward to proxy
   * (pump drives the proxy). Thread-safe.
   */
  void submitFrame(const cv::Mat& frame);

  void setLastFramePumpActive(bool active) { last_frame_pump_active_.store(active); }
  bool isLastFramePumpActive() const { return last_frame_pump_active_.load(); }

  /** Set last frame (e.g. for pump to use). Thread-safe. */
  void setLastFrame(const cv::Mat& frame);

  /** Get a copy of the last frame (e.g. for pump). Returns empty Mat if none. */
  cv::Mat getLastFrameCopy() const;

  std::shared_ptr<PersistentOutputLeg> outputLeg() const { return output_leg_; }

 private:
  std::shared_ptr<PersistentOutputLeg> output_leg_;
  PipelineSnapshotPtr active_pipeline_;
  std::atomic<bool> last_frame_pump_active_{false};
  mutable std::mutex last_frame_mutex_;
  std::shared_ptr<cv::Mat> last_frame_;
  /// Throttle inject to output leg so rtmp_des queue does not grow unbounded (~20 fps max)
  mutable std::mutex inject_mutex_;
  mutable std::chrono::steady_clock::time_point last_inject_time_{};
  static constexpr int kMinInjectIntervalMs = 50;
};

}  // namespace edgeos
