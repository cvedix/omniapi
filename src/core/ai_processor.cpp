#include "core/ai_processor.h"
#include "core/inference_session.h"
#include <chrono>
#include <cmath>
#include <iostream>
#include <json/json.h>
#include <sstream>
#include <thread>

struct AIProcessor::SessionHolder {
  std::unique_ptr<core::InferenceSession> session;
  cv::Mat pending_frame;
  std::mutex frame_mutex;
};

AIProcessor::AIProcessor()
    : status_(Status::Stopped), should_stop_(false),
      last_fps_calc_time_(std::chrono::steady_clock::now()),
      frame_count_since_last_calc_(0),
      session_holder_(std::make_unique<SessionHolder>()) {
  metrics_.frames_processed = 0;
  metrics_.frames_dropped = 0;
  metrics_.fps = 0.0;
  metrics_.avg_latency_ms = 0.0;
  metrics_.max_latency_ms = 0.0;
  metrics_.memory_usage_mb = 0;
  metrics_.error_count = 0;
  metrics_.status = Status::Stopped;
}

AIProcessor::~AIProcessor() { stop(true); }

bool AIProcessor::start(const std::string &config, ResultCallback callback) {
  if (status_.load() != Status::Stopped) {
    std::cerr << "[AIProcessor] Already running or starting" << std::endl;
    return false;
  }

  config_ = config;
  result_callback_ = callback;
  status_.store(Status::Starting);
  should_stop_.store(false);

  // Initialize SDK
  if (!initializeSDK(config)) {
    status_.store(Status::Error);
    return false;
  }

  // Start processing thread
  processing_thread_ =
      std::make_unique<std::thread>(&AIProcessor::processingLoop, this);

  // Wait a bit to ensure thread started
  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  if (status_.load() == Status::Running) {
    std::cout << "[AIProcessor] Started successfully" << std::endl;
    return true;
  } else {
    std::cerr << "[AIProcessor] Failed to start" << std::endl;
    return false;
  }
}

void AIProcessor::stop(bool wait) {
  if (status_.load() == Status::Stopped) {
    return;
  }

  std::cout << "[AIProcessor] Stopping..." << std::endl;
  status_.store(Status::Stopping);
  should_stop_.store(true);

  if (processing_thread_ && processing_thread_->joinable()) {
    if (wait) {
      processing_thread_->join();
    } else {
      processing_thread_->detach();
    }
  }

  cleanupSDK();
  status_.store(Status::Stopped);
  std::cout << "[AIProcessor] Stopped" << std::endl;
}

void AIProcessor::processingLoop() {
  std::cout << "[AIProcessor] Processing thread started" << std::endl;

  status_.store(Status::Running);
  last_fps_calc_time_ = std::chrono::steady_clock::now();
  frame_count_since_last_calc_ = 0;

  while (!should_stop_.load() && status_.load() == Status::Running) {
    try {
      auto frame_start = std::chrono::steady_clock::now();

      // Process frame
      processFrame();

      auto frame_end = std::chrono::steady_clock::now();
      auto latency = std::chrono::duration_cast<std::chrono::milliseconds>(
                         frame_end - frame_start)
                         .count();

      // Update metrics
      {
        std::lock_guard<std::mutex> lock(metrics_mutex_);
        metrics_.frames_processed++;
        metrics_.last_frame_time = frame_end;

        // Update latency
        if (metrics_.frames_processed > 0) {
          metrics_.avg_latency_ms =
              (metrics_.avg_latency_ms * (metrics_.frames_processed - 1) +
               latency) /
              metrics_.frames_processed;
        }

        if (latency > metrics_.max_latency_ms) {
          metrics_.max_latency_ms = latency;
        }

        // Calculate FPS
        frame_count_since_last_calc_++;
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                           now - last_fps_calc_time_)
                           .count();

        if (elapsed >= 1) {
          double fps =
              static_cast<double>(frame_count_since_last_calc_) / elapsed;
          metrics_.fps = std::round(fps);
          frame_count_since_last_calc_ = 0;
          last_fps_calc_time_ = now;
        }
      }

      // Adaptive sleep: Only sleep if processing is too fast
      // Calculate target sleep time based on target FPS (if available)
      // For high FPS scenarios, let OS scheduler handle CPU usage instead of
      // fixed sleep This removes artificial bottleneck and allows higher
      // throughput
      auto processing_time =
          std::chrono::duration_cast<std::chrono::microseconds>(frame_end -
                                                                frame_start)
              .count();

      // Only sleep if processing is very fast (< 1ms) to prevent busy-waiting
      // For normal processing times, OS scheduler will handle CPU sharing
      if (processing_time < 1000) { // Less than 1ms processing time
        std::this_thread::sleep_for(
            std::chrono::microseconds(100)); // Minimal yield
      } else {
        // For longer processing, yield to other threads without fixed delay
        std::this_thread::yield();
      }

    } catch (const std::exception &e) {
      {
        std::lock_guard<std::mutex> lock(error_mutex_);
        last_error_ = e.what();
      }
      {
        std::lock_guard<std::mutex> lock(metrics_mutex_);
        metrics_.error_count++;
      }
      std::cerr << "[AIProcessor] Error in processing loop: " << e.what()
                << std::endl;

      // Continue processing despite error
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
  }

  std::cout << "[AIProcessor] Processing thread stopped" << std::endl;
}

void AIProcessor::submitFrame(const cv::Mat &frame) {
  if (!session_holder_)
    return;
  std::lock_guard<std::mutex> lock(session_holder_->frame_mutex);
  frame.copyTo(session_holder_->pending_frame);
}

void AIProcessor::processFrame() {
  if (!session_holder_ || !session_holder_->session ||
      !session_holder_->session->isLoaded()) {
    return;
  }
  cv::Mat frame;
  {
    std::lock_guard<std::mutex> lock(session_holder_->frame_mutex);
    if (session_holder_->pending_frame.empty())
      return;
    frame = session_holder_->pending_frame.clone();
  }
  core::InferenceInput input;
  input.frame = frame;
  input.model_id = "face";
  input.options["extract_embedding"] = true;

  core::InferenceResult ir = session_holder_->session->infer(input);
  if (!ir.success) {
    std::lock_guard<std::mutex> lock(error_mutex_);
    last_error_ = ir.error;
    return;
  }
  if (result_callback_ && ir.success && !ir.data.isNull()) {
    Json::StreamWriterBuilder wb;
    wb["indentation"] = "";
    std::string json_str = Json::writeString(wb, ir.data);
    result_callback_(json_str);
  }
}

bool AIProcessor::initializeSDK(const std::string &config) {
  session_holder_->session = std::make_unique<core::InferenceSession>();
  if (config.empty())
    return true;

  Json::CharReaderBuilder rb;
  std::istringstream ss(config);
  std::string errs;
  Json::Value root;
  if (!Json::parseFromStream(rb, ss, &root, &errs)) {
    std::cerr << "[AIProcessor] Invalid config JSON: " << errs << std::endl;
    return true;  // still start without session loaded
  }
  std::string detector_path =
      root.isMember("detector_path") ? root["detector_path"].asString() : "";
  std::string recognizer_path =
      root.isMember("recognizer_path") ? root["recognizer_path"].asString()
                                       : "";
  if (detector_path.empty()) {
    return true;
  }
  if (!session_holder_->session->load(detector_path, recognizer_path)) {
    std::cerr << "[AIProcessor] Failed to load InferenceSession"
              << std::endl;
    return true;  // allow start; processFrame will no-op until loaded
  }
  return true;
}

void AIProcessor::cleanupSDK() {
  if (session_holder_ && session_holder_->session) {
    session_holder_->session->unload();
    session_holder_->session.reset();
  }
}

AIProcessor::Metrics AIProcessor::getMetrics() const {
  std::lock_guard<std::mutex> lock(metrics_mutex_);
  Metrics m = metrics_;
  m.status = status_.load();
  return m;
}

std::string AIProcessor::getLastError() const {
  std::lock_guard<std::mutex> lock(error_mutex_);
  return last_error_;
}

bool AIProcessor::isHealthy(uint64_t max_latency_ms, double min_fps) const {
  auto metrics = getMetrics();

  if (metrics.status != Status::Running) {
    return false;
  }

  if (metrics.avg_latency_ms > max_latency_ms) {
    return false;
  }

  if (metrics.fps < min_fps) {
    return false;
  }

  return true;
}
