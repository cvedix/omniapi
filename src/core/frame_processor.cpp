#include "core/frame_processor.h"
#include "core/frame_decoder.h"
#include "instances/instance_manager.h"
#include "instances/inprocess_instance_manager.h"
#include <cvedix/nodes/src/cvedix_app_src_node.h>
#include <iostream>
#include <chrono>

FrameProcessor::FrameProcessor() : global_stop_(false) {
}

FrameProcessor::~FrameProcessor() {
  stopAll();
}

FrameProcessor& FrameProcessor::getInstance() {
  static FrameProcessor instance;
  return instance;
}

void FrameProcessor::startProcessing(const std::string& instanceId, IInstanceManager* instanceManager) {
  std::lock_guard<std::mutex> lock(mutex_);
  
  // Check if already processing
  if (processing_threads_.find(instanceId) != processing_threads_.end()) {
    if (processing_threads_[instanceId].joinable()) {
      // Already processing
      return;
    }
  }
  
  // Create stop flag
  stop_flags_[instanceId] = false;
  
  // Start processing thread
  processing_threads_[instanceId] = std::thread(&FrameProcessor::processFrames, this, instanceId, instanceManager);
  
  std::cout << "[FrameProcessor] Started processing frames for instance: " << instanceId << std::endl;
}

void FrameProcessor::stopProcessing(const std::string& instanceId) {
  std::lock_guard<std::mutex> lock(mutex_);
  
  // Set stop flag
  auto it = stop_flags_.find(instanceId);
  if (it != stop_flags_.end()) {
    it->second = true;
  }
  
  // Wait for thread to finish
  auto threadIt = processing_threads_.find(instanceId);
  if (threadIt != processing_threads_.end()) {
    if (threadIt->second.joinable()) {
      threadIt->second.join();
    }
    processing_threads_.erase(threadIt);
  }
  
  stop_flags_.erase(instanceId);
  
  std::cout << "[FrameProcessor] Stopped processing frames for instance: " << instanceId << std::endl;
}

bool FrameProcessor::isProcessing(const std::string& instanceId) const {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = processing_threads_.find(instanceId);
  return it != processing_threads_.end() && it->second.joinable();
}

void FrameProcessor::stopAll() {
  std::lock_guard<std::mutex> lock(mutex_);
  global_stop_ = true;
  
  // Set all stop flags
  for (auto& [id, flag] : stop_flags_) {
    flag = true;
  }
  
  // Wait for all threads
  for (auto& [id, thread] : processing_threads_) {
    if (thread.joinable()) {
      thread.join();
    }
  }
  
  processing_threads_.clear();
  stop_flags_.clear();
}

void FrameProcessor::processFrames(const std::string& instanceId, IInstanceManager* instanceManager) {
  auto& queueManager = FrameInputQueueManager::getInstance();
  auto& queue = queueManager.getQueue(instanceId);
  
  FrameDecoder decoder;
  
  // Find app_src node once
  auto appSrcNode = findAppSrcNode(instanceId, instanceManager);
  if (!appSrcNode) {
    std::cerr << "[FrameProcessor] Warning: No app_src node found for instance " << instanceId 
              << ". Frames will be queued but not processed." << std::endl;
    // Continue anyway - frames will be queued and can be processed later when node is available
  }
  
  // Use blocking pop with timeout to wake as soon as a frame arrives (no fixed 10ms sleep).
  constexpr int kPopTimeoutMs = 100;

  while (!global_stop_.load()) {
    // Check instance-specific stop flag
    auto it = stop_flags_.find(instanceId);
    if (it != stop_flags_.end() && it->second.load()) {
      break;
    }

    FrameData frameData;
    if (!queue.pop(frameData, kPopTimeoutMs)) {
      // Timeout or empty: re-check stop and loop (no busy-wait, no fixed 10ms delay)
      continue;
    }

    // Decode frame (single decode path)
    cv::Mat decodedFrame;
    bool decodeSuccess = false;

    if (frameData.type == FrameType::ENCODED) {
      decodeSuccess = decoder.decodeEncodedFrame(frameData.data, frameData.codecId, decodedFrame);
    } else if (frameData.type == FrameType::COMPRESSED) {
      decodeSuccess = decoder.decodeCompressedFrame(frameData.data, decodedFrame);
    }

    if (decodeSuccess && !decodedFrame.empty()) {
      if (!appSrcNode) {
        appSrcNode = findAppSrcNode(instanceId, instanceManager);
      }

      if (appSrcNode) {
        if (pushFrameToNode(appSrcNode, decodedFrame)) {
          static thread_local uint64_t frame_count = 0;
          frame_count++;
          if (frame_count <= 5 || frame_count % 100 == 0) {
            std::cout << "[FrameProcessor] Pushed frame #" << frame_count
                      << " to instance " << instanceId << std::endl;
          }
        } else {
          std::cerr << "[FrameProcessor] Failed to push frame to app_src node for instance "
                    << instanceId << std::endl;
        }
      } else {
        static thread_local bool logged_warning = false;
        if (!logged_warning) {
          std::cerr << "[FrameProcessor] Warning: No app_src node for instance " << instanceId
                    << ". Frames are queued but not being processed. "
                    << "Instance must use app_src node to process pushed frames." << std::endl;
          logged_warning = true;
        }
      }
    } else {
      std::cerr << "[FrameProcessor] Failed to decode frame for instance " << instanceId << std::endl;
    }
  }
  
  std::cout << "[FrameProcessor] Processing thread stopped for instance: " << instanceId << std::endl;
}

std::shared_ptr<void> FrameProcessor::findAppSrcNode(const std::string& instanceId, IInstanceManager* instanceManager) {
  if (!instanceManager) {
    return nullptr;
  }
  
  // Only works in in-process mode
  if (instanceManager->isSubprocessMode()) {
    // In subprocess mode, we can't directly access nodes
    // Frames would need to be sent via IPC
    return nullptr;
  }
  
  try {
    // Cast to InProcessInstanceManager to access registry
    auto* inProcessManager = dynamic_cast<InProcessInstanceManager*>(instanceManager);
    if (!inProcessManager) {
      return nullptr;
    }
    
    // Get nodes from registry
    auto& registry = inProcessManager->getRegistry();
    auto nodes = registry.getInstanceNodes(instanceId);
    
    if (nodes.empty()) {
      return nullptr;
    }
    
    // Search for app_src node
    for (const auto& node : nodes) {
      if (!node) continue;
      
      // Try to cast to app_src_node
      auto appSrcNode = std::dynamic_pointer_cast<cvedix_nodes::cvedix_app_src_node>(node);
      if (appSrcNode) {
        std::cout << "[FrameProcessor] Found app_src node for instance " << instanceId << std::endl;
        // Return as void* to avoid exposing CVEDIX types in header
        return std::static_pointer_cast<void>(appSrcNode);
      }
    }
    
    return nullptr;
  } catch (const std::exception& e) {
    std::cerr << "[FrameProcessor] Exception finding app_src node: " << e.what() << std::endl;
    return nullptr;
  }
}

bool FrameProcessor::pushFrameToNode(std::shared_ptr<void> appSrcNode, const cv::Mat& frame) {
  if (!appSrcNode || frame.empty()) {
    return false;
  }
  
  try {
    // Cast back to app_src_node
    auto node = std::static_pointer_cast<cvedix_nodes::cvedix_app_src_node>(appSrcNode);
    if (!node) {
      return false;
    }
    
    // Push frame using push_frames() method
    // Note: The exact API may vary depending on CVEDIX SDK version
    // TODO: Verify the exact method signature for cvedix_app_src_node::push_frames()
    // For now, we'll try to call it - if it doesn't exist, this will need to be adjusted
    try {
      // Try to call push_frames - method signature may be:
      // void push_frames(const cv::Mat& frame)
      // or similar
      // If compilation fails, check CVEDIX SDK documentation for correct API
      node->push_frames(frame);
    } catch (const std::exception& e) {
      std::cerr << "[FrameProcessor] Exception calling push_frames: " << e.what() << std::endl;
      return false;
    }
    
    return true;
  } catch (const std::exception& e) {
    std::cerr << "[FrameProcessor] Exception pushing frame: " << e.what() << std::endl;
    return false;
  }
}

