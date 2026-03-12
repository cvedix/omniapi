#pragma once

#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <opencv2/core.hpp>

/**
 * @brief AI Processor Manager
 *
 * Manages AI SDK processing on a separate thread to avoid blocking REST API.
 * Provides status, metrics, and control for AI processing.
 */
class AIProcessor {
public:
  /**
   * @brief Processing status
   */
  enum class Status { Stopped, Starting, Running, Stopping, Error };

  /**
   * @brief Processing metrics
   */
  struct Metrics {
    uint64_t frames_processed;
    uint64_t frames_dropped;
    double fps;            // Frames per second
    double avg_latency_ms; // Average processing latency
    double max_latency_ms;
    size_t memory_usage_mb;
    uint64_t error_count;
    std::chrono::steady_clock::time_point last_frame_time;
    Status status;
  };

  /**
   * @brief Callback for processing results
   */
  using ResultCallback = std::function<void(const std::string &result)>;

  /**
   * @brief Constructor
   */
  AIProcessor();

  /**
   * @brief Destructor - stops processing
   */
  ~AIProcessor();

  /**
   * @brief Start AI processing
   * @param config Configuration string (JSON or custom format)
   * @param callback Optional callback for results
   * @return true if started successfully
   */
  bool start(const std::string &config = "", ResultCallback callback = nullptr);

  /**
   * @brief Stop AI processing
   * @param wait Wait for processing to stop
   */
  void stop(bool wait = true);

  /**
   * @brief Check if processing is running
   */
  bool isRunning() const { return status_.load() == Status::Running; }

  /**
   * @brief Get current status
   */
  Status getStatus() const { return status_.load(); }

  /**
   * @brief Get processing metrics
   */
  Metrics getMetrics() const;

  /**
   * @brief Get last error message
   */
  std::string getLastError() const;

  /**
   * @brief Check if AI processing is healthy
   * @param max_latency_ms Maximum acceptable latency
   * @param min_fps Minimum acceptable FPS
   * @return true if healthy
   */
  bool isHealthy(uint64_t max_latency_ms = 1000, double min_fps = 1.0) const;

  /**
   * @brief Submit a frame for inference (optional). Next processFrame() will run infer on it.
   * @param frame BGR image (e.g. from camera or push API)
   */
  void submitFrame(const cv::Mat &frame);

private:
  /**
   * @brief AI processing loop (runs on separate thread)
   */
  void processingLoop();

  /**
   * @brief Process a single frame/batch
   * Override this in derived class or use callback
   */
  virtual void processFrame();

  /**
   * @brief Initialize AI SDK
   */
  virtual bool initializeSDK(const std::string &config);

  /**
   * @brief Cleanup AI SDK
   */
  virtual void cleanupSDK();

  // Thread management
  std::atomic<Status> status_;
  std::unique_ptr<std::thread> processing_thread_;
  std::atomic<bool> should_stop_;

  // Configuration
  std::string config_;
  ResultCallback result_callback_;

  // Metrics
  mutable std::mutex metrics_mutex_;
  Metrics metrics_;

  // Error tracking
  mutable std::mutex error_mutex_;
  std::string last_error_;

  // Timing for FPS calculation
  std::chrono::steady_clock::time_point last_fps_calc_time_;
  uint64_t frame_count_since_last_calc_;

  // Phase 3: InferenceSession for face inference
  struct SessionHolder;
  std::unique_ptr<SessionHolder> session_holder_;
};
