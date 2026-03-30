/**
 * @file frame_router.cpp
 * @brief Routes frames to persistent output leg; atomic pipeline swap for zero-downtime.
 */

#include "core/frame_router.h"
#include "core/persistent_output_leg.h"
#include <iostream>

namespace edgeos {

FrameRouter::FrameRouter(std::shared_ptr<PersistentOutputLeg> outputLeg)
    : output_leg_(std::move(outputLeg)) {
  if (!output_leg_) {
    throw std::invalid_argument("FrameRouter: outputLeg cannot be null");
  }
}

FrameRouter::PipelineSnapshotPtr FrameRouter::setActivePipeline(PipelineSnapshotPtr snapshot) {
  PipelineSnapshotPtr old = std::atomic_load_explicit(&active_pipeline_, std::memory_order_acquire);
  std::atomic_store_explicit(&active_pipeline_, std::move(snapshot), std::memory_order_release);
  return old;
}

FrameRouter::PipelineSnapshotPtr FrameRouter::getActivePipeline() const {
  return std::atomic_load_explicit(&active_pipeline_, std::memory_order_acquire);
}

void FrameRouter::submitFrame(const cv::Mat& frame) {
  if (frame.empty()) return;

  {
    std::lock_guard<std::mutex> lock(last_frame_mutex_);
    last_frame_ = std::make_shared<cv::Mat>(frame.clone());
  }

  if (last_frame_pump_active_.load()) {
    return;
  }

  if (!output_leg_) return;

  // Throttle inject so rtmp_des input queue does not grow unbounded (e.g. 200+ frames).
  // rtmp_des encode+push is slower than pipeline rate; cap inject at ~20 fps.
  auto now = std::chrono::steady_clock::now();
  {
    std::lock_guard<std::mutex> lock(inject_mutex_);
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        now - last_inject_time_).count();
    if (last_inject_time_.time_since_epoch().count() != 0 && elapsed < kMinInjectIntervalMs) {
      return;
    }
    last_inject_time_ = now;
  }
  output_leg_->injectFrame(frame);
}

void FrameRouter::setLastFrame(const cv::Mat& frame) {
  if (frame.empty()) return;
  std::lock_guard<std::mutex> lock(last_frame_mutex_);
  last_frame_ = std::make_shared<cv::Mat>(frame.clone());
}

cv::Mat FrameRouter::getLastFrameCopy() const {
  std::lock_guard<std::mutex> lock(last_frame_mutex_);
  if (last_frame_ && !last_frame_->empty()) {
    return last_frame_->clone();
  }
  return cv::Mat();
}

}  // namespace edgeos
