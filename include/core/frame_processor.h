#pragma once

#include "core/frame_input_queue.h"
#include <opencv2/opencv.hpp>
#include <string>
#include <thread>
#include <atomic>
#include <memory>

// Forward declarations
class IInstanceManager;
class InstanceRegistry;

/**
 * @brief Frame Processor
 * 
 * Processes frames from FrameInputQueue and injects them into instance pipelines.
 * Runs background threads to consume frames from queues and push to app_src nodes.
 */
class FrameProcessor {
public:
  /**
   * @brief Get singleton instance
   */
  static FrameProcessor& getInstance();
  
  /**
   * @brief Start processing frames for an instance
   * @param instanceId Instance ID
   * @param instanceManager Instance manager to access pipeline nodes
   */
  void startProcessing(const std::string& instanceId, IInstanceManager* instanceManager);
  
  /**
   * @brief Stop processing frames for an instance
   * @param instanceId Instance ID
   */
  void stopProcessing(const std::string& instanceId);
  
  /**
   * @brief Check if processing is active for an instance
   * @param instanceId Instance ID
   * @return true if processing is active
   */
  bool isProcessing(const std::string& instanceId) const;
  
  /**
   * @brief Stop all processing
   */
  void stopAll();

private:
  FrameProcessor();
  ~FrameProcessor();
  FrameProcessor(const FrameProcessor&) = delete;
  FrameProcessor& operator=(const FrameProcessor&) = delete;
  
  /**
   * @brief Process frames for an instance (runs in background thread)
   * @param instanceId Instance ID
   * @param instanceManager Instance manager
   */
  void processFrames(const std::string& instanceId, IInstanceManager* instanceManager);
  
  /**
   * @brief Find app_src node in instance pipeline
   * @param instanceId Instance ID
   * @param instanceManager Instance manager
   * @return Shared pointer to app_src node, or nullptr if not found
   */
  std::shared_ptr<void> findAppSrcNode(const std::string& instanceId, IInstanceManager* instanceManager);
  
  /**
   * @brief Push frame to app_src node
   * @param appSrcNode App source node
   * @param frame Decoded frame (BGR format)
   * @return true if successful
   */
  bool pushFrameToNode(std::shared_ptr<void> appSrcNode, const cv::Mat& frame);
  
  mutable std::mutex mutex_;
  std::unordered_map<std::string, std::thread> processing_threads_;
  std::unordered_map<std::string, std::atomic<bool>> stop_flags_;
  std::atomic<bool> global_stop_;
};

