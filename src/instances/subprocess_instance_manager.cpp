#include "instances/subprocess_instance_manager.h"
#include "core/resource_manager.h"
#include "core/runtime_update_log.h"
#include "core/timeout_constants.h"
#include "core/uuid_generator.h"
#include "models/solution_config.h"
#include <chrono>
#include <future>
#include <iostream>
#include <thread>

// Static member initialization
InstanceStateManager SubprocessInstanceManager::state_manager_;

SubprocessInstanceManager::SubprocessInstanceManager(
    SolutionRegistry &solutionRegistry, InstanceStorage &instanceStorage,
    const std::string &workerExecutable)
    : solution_registry_(solutionRegistry), instance_storage_(instanceStorage) {

  supervisor_ = std::make_unique<worker::WorkerSupervisor>(workerExecutable);

  // Set up callbacks
  supervisor_->setStateChangeCallback([this](const std::string &id,
                                             worker::WorkerState oldState,
                                             worker::WorkerState newState) {
    onWorkerStateChange(id, oldState, newState);
  });

  supervisor_->setErrorCallback(
      [this](const std::string &id, const std::string &error) {
        onWorkerError(id, error);
      });

  // Start supervisor monitoring
  supervisor_->start();

  // Initialize ResourceManager for GPU allocation
  // Default: allow up to 4 concurrent instances per GPU
  auto &resourceManager = ResourceManager::getInstance();
  resourceManager.initialize(4);
  
  std::cout << "[SubprocessInstanceManager] Initialized with worker: "
            << workerExecutable << std::endl;
}

SubprocessInstanceManager::~SubprocessInstanceManager() { stopAllWorkers(); }

std::string
SubprocessInstanceManager::createInstance(const CreateInstanceRequest &req) {
  // Generate instance ID
  std::string instanceId = UUIDGenerator::generateUUID();

  // Validate solution if provided
  if (!req.solution.empty()) {
    auto optSolution = solution_registry_.getSolution(req.solution);
    if (!optSolution.has_value()) {
      throw std::invalid_argument("Solution not found: " + req.solution);
    }
  }

  // Build config for worker
  Json::Value config = buildWorkerConfig(req);

  // Allocate GPU and spawn worker
  if (!allocateGPUAndSpawnWorker(instanceId, config)) {
    throw std::runtime_error("Failed to spawn worker for instance: " +
                             instanceId);
  }

  // Create local instance info
  InstanceInfo info;
  info.instanceId = instanceId;
  info.displayName = req.name.empty() ? instanceId : req.name;
  info.group = req.group;
  info.solutionId = req.solution;
  info.running = false;
  info.loaded = true;
  info.autoStart = req.autoStart;
  info.autoRestart = req.autoRestart;
  info.persistent = req.persistent;
  info.frameRateLimit = req.frameRateLimit;
  info.metadataMode = req.metadataMode;
  info.statisticsMode = req.statisticsMode;
  info.diagnosticsMode = req.diagnosticsMode;
  info.debugMode = req.debugMode;
  info.detectorMode = req.detectorMode;
  info.detectionSensitivity = req.detectionSensitivity;
  info.movementSensitivity = req.movementSensitivity;
  info.sensorModality = req.sensorModality;
  info.inputOrientation = req.inputOrientation;
  info.inputPixelLimit = req.inputPixelLimit;
  info.detectorModelFile = req.detectorModelFile;
  info.detectorThermalModelFile = req.detectorThermalModelFile;
  info.animalConfidenceThreshold = req.animalConfidenceThreshold;
  info.personConfidenceThreshold = req.personConfidenceThreshold;
  info.vehicleConfidenceThreshold = req.vehicleConfidenceThreshold;
  info.faceConfidenceThreshold = req.faceConfidenceThreshold;
  info.licensePlateConfidenceThreshold = req.licensePlateConfidenceThreshold;
  info.confThreshold = req.confThreshold;
  info.performanceMode = req.performanceMode;
  info.recommendedFrameRate = req.recommendedFrameRate;
  // FPS configuration: default to 5 FPS if not specified (fps == 0)
  info.configuredFps = (req.fps > 0) ? req.fps : 5;
  info.fps = 0.0;
  info.startTime = std::chrono::steady_clock::now();
  info.lastActivityTime = info.startTime;
  info.hasReceivedData = false;
  info.retryCount = 0;
  info.retryLimitReached = false;
  
  // Get version from CVEDIX SDK
#ifdef CVEDIX_VERSION_STRING
  info.version = CVEDIX_VERSION_STRING;
#else
  info.version = "2026.0.1.1"; // Default version
#endif
  
  // Get solution name if solution is provided
  if (!req.solution.empty()) {
    auto optSolution = solution_registry_.getSolution(req.solution);
    if (optSolution.has_value()) {
      info.solutionName = optSolution.value().solutionName;
    }
  }

  // Copy additional params (including source URLs)
  info.additionalParams = req.additionalParams;
  
  // Extract RTSP URL - check RTSP_SRC_URL first, then RTSP_URL
  if (req.additionalParams.count("RTSP_SRC_URL")) {
    info.rtspUrl = req.additionalParams.at("RTSP_SRC_URL");
  } else if (req.additionalParams.count("RTSP_URL")) {
    info.rtspUrl = req.additionalParams.at("RTSP_URL");
  }
  
  // Extract RTMP URL - check RTMP_DES_URL first, then RTMP_URL
  // Helper function to trim whitespace
  auto trim = [](const std::string &str) -> std::string {
    if (str.empty())
      return str;
    size_t first = str.find_first_not_of(" \t\n\r\f\v");
    if (first == std::string::npos)
      return "";
    size_t last = str.find_last_not_of(" \t\n\r\f\v");
    return str.substr(first, (last - first + 1));
  };
  
  if (req.additionalParams.count("RTMP_DES_URL")) {
    info.rtmpUrl = trim(req.additionalParams.at("RTMP_DES_URL"));
  } else if (req.additionalParams.count("RTMP_URL")) {
    info.rtmpUrl = trim(req.additionalParams.at("RTMP_URL"));
  }
  
  // Generate RTSP URL from RTMP URL if RTSP URL is not already set
  // This allows RTSP stream to be available when RTMP output is configured
  // RTSP URL will have the same stream key as RTMP URL (including instanceId if present)
  // Pattern: rtmp://host:1935/live/stream_key -> rtsp://host:8554/live/stream_key
  if (info.rtspUrl.empty() && !info.rtmpUrl.empty()) {
    std::string rtmpUrl = info.rtmpUrl;
    
    // Replace RTMP protocol and port with RTSP
    size_t protocolPos = rtmpUrl.find("rtmp://");
    if (protocolPos != std::string::npos) {
      std::string rtspUrl = rtmpUrl;
      
      // Replace protocol
      rtspUrl.replace(protocolPos, 7, "rtsp://");
      
      // Replace port 1935 with 8554 (common RTSP port for conversion)
      size_t portPos = rtspUrl.find(":1935");
      if (portPos != std::string::npos) {
        rtspUrl.replace(portPos, 5, ":8554");
      }
      
      // Keep the same stream key as RTMP URL (no modification needed)
      // RTMP URL already includes instanceId and "_0" suffix if applicable
      info.rtspUrl = rtspUrl;
      std::cerr << "[SubprocessInstanceManager] Generated RTSP URL from RTMP URL (same stream key): '" 
                << rtspUrl << "'" << std::endl;
    }
  }
  
  if (req.additionalParams.count("FILE_PATH")) {
    info.filePath = req.additionalParams.at("FILE_PATH");
  }

  {
    std::lock_guard<std::mutex> lock(instances_mutex_);
    instances_[instanceId] = info;
  }

  // Save to storage for all instances (for debugging and inspection)
  // Only persistent instances will be loaded on server restart
  bool saved = instance_storage_.saveInstance(instanceId, info);
  if (saved) {
    if (req.persistent) {
      std::cerr << "[SubprocessInstanceManager] Instance configuration saved "
                   "(persistent - will be loaded on restart)"
                << std::endl;
    } else {
      std::cerr << "[SubprocessInstanceManager] Instance configuration saved "
                   "(non-persistent - for inspection only)"
                << std::endl;
    }
  } else {
    std::cerr << "[SubprocessInstanceManager] Warning: Failed to save instance "
                 "configuration to file"
              << std::endl;
  }

  // Wait for worker to be ready and pipeline to be built
  // Pipeline is built automatically when worker starts if config has Solution
  // But we need to wait for it to complete before starting
  if (req.autoStart) {
    // Wait for worker to become ready (up to 5 seconds)
    const int maxWaitRetries = 50; // 50 retries * 100ms = 5 seconds
    const int waitDelayMs = 100;
    bool workerReady = false;
    
    for (int retry = 0; retry < maxWaitRetries; retry++) {
      auto workerState = supervisor_->getWorkerState(instanceId);
      if (workerState == worker::WorkerState::READY || 
          workerState == worker::WorkerState::BUSY) {
        workerReady = true;
        break;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(waitDelayMs));
    }
    
    if (!workerReady) {
      std::cerr << "[SubprocessInstanceManager] Worker not ready after "
                << (maxWaitRetries * waitDelayMs / 1000) << " seconds for instance: "
                << instanceId << std::endl;
    } else {
      // Additional wait to ensure pipeline is built (if buildPipeline was called)
      // Pipeline build happens async, so give it a bit more time
      std::this_thread::sleep_for(std::chrono::milliseconds(500));
      
      // Now start the instance
      startInstance(instanceId);
    }
  }

  std::cout << "[SubprocessInstanceManager] Created instance: " << instanceId
            << std::endl;
  return instanceId;
}

bool SubprocessInstanceManager::deleteInstance(const std::string &instanceId) {
  // Terminate worker
  if (!supervisor_->terminateWorker(instanceId, false)) {
    // Worker might not exist, but we should still clean up local state
    std::cerr << "[SubprocessInstanceManager] Worker not found for: "
              << instanceId << std::endl;
  }

  // Release GPU allocation if exists
  {
    std::lock_guard<std::mutex> lock(gpu_allocations_mutex_);
    auto it = gpu_allocations_.find(instanceId);
    if (it != gpu_allocations_.end()) {
      auto &resourceManager = ResourceManager::getInstance();
      resourceManager.releaseGPU(it->second);
      gpu_allocations_.erase(it);
      std::cout << "[SubprocessInstanceManager] Released GPU allocation for instance: " 
                << instanceId << std::endl;
    }
  }

  // Remove from local cache
  {
    std::lock_guard<std::mutex> lock(instances_mutex_);
    instances_.erase(instanceId);
  }

  // Remove from storage
  instance_storage_.deleteInstance(instanceId);

  std::cout << "[SubprocessInstanceManager] Deleted instance: " << instanceId
            << std::endl;
  return true;
}

bool SubprocessInstanceManager::startInstance(const std::string &instanceId,
                                              bool /*skipAutoStop*/) {
  // Check worker state - accept both READY and BUSY states
  // BUSY is OK because worker can handle multiple commands
  auto workerState = supervisor_->getWorkerState(instanceId);
  if (workerState != worker::WorkerState::READY &&
      workerState != worker::WorkerState::BUSY) {
    std::cerr << "[SubprocessInstanceManager] Worker not ready: " << instanceId
              << " (state: " << static_cast<int>(workerState) << ")"
              << std::endl;

    // If worker doesn't exist, try to check if instance exists in storage
    // and spawn worker if needed
    if (workerState == worker::WorkerState::STOPPED) {
      auto optStoredInfo = instance_storage_.loadInstance(instanceId);
      if (optStoredInfo.has_value()) {
        std::cout << "[SubprocessInstanceManager] Instance found in storage "
                     "but worker not running. "
                  << "Spawning worker for instance: " << instanceId
                  << std::endl;

        const auto &info = optStoredInfo.value();
        Json::Value config = buildWorkerConfigFromInstanceInfo(info);
        
        if (allocateGPUAndSpawnWorker(instanceId, config)) {
          // Add to local cache
          {
            std::lock_guard<std::mutex> lock(instances_mutex_);
            instances_[instanceId] = info;
          }
          std::cout
              << "[SubprocessInstanceManager] Worker spawned for instance: "
              << instanceId << std::endl;
          
          // Wait for worker to be ready and pipeline to be built
          const int maxWaitRetries = 50; // 50 retries * 100ms = 5 seconds
          const int waitDelayMs = 100;
          for (int retry = 0; retry < maxWaitRetries; retry++) {
            workerState = supervisor_->getWorkerState(instanceId);
            if (workerState == worker::WorkerState::READY || 
                workerState == worker::WorkerState::BUSY) {
              break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(waitDelayMs));
          }
          
          // Additional wait to ensure pipeline is built
          std::this_thread::sleep_for(std::chrono::milliseconds(500));
          
          // Continue to start instance below
        } else {
          std::cerr << "[SubprocessInstanceManager] Failed to spawn worker for "
                       "instance: "
                    << instanceId << std::endl;
          return false;
        }
      } else {
        return false;
      }
    } else {
      return false;
    }
  }

  // Send START command to worker
  // Use configurable timeout for start operation (default: 10 seconds)
  worker::IPCMessage msg;
  msg.type = worker::MessageType::START_INSTANCE;
  msg.payload["instance_id"] = instanceId;

  auto response = supervisor_->sendToWorker(
      instanceId, msg, TimeoutConstants::getIpcStartStopTimeoutMs());

  // If start failed with "No pipeline configured", build pipeline first
  if (response.type == worker::MessageType::START_INSTANCE_RESPONSE &&
      !response.payload.get("success", false).asBool()) {
    std::string error = response.payload.get("error", "").asString();
    if (error.find("No pipeline configured") != std::string::npos ||
        error.find("pipeline configured") != std::string::npos) {
      std::cout << "[SubprocessInstanceManager] Pipeline not built, building it "
                   "now for instance: "
                << instanceId << std::endl;
      
      // Get instance info to build config
      std::lock_guard<std::mutex> lock(instances_mutex_);
      auto it = instances_.find(instanceId);
      if (it != instances_.end()) {
        const auto &info = it->second;
        Json::Value config = buildWorkerConfigFromInstanceInfo(info);
        
        // Send CREATE_INSTANCE to build pipeline
        worker::IPCMessage createMsg;
        createMsg.type = worker::MessageType::CREATE_INSTANCE;
        createMsg.payload["config"] = config;
        
        auto createResponse = supervisor_->sendToWorker(
            instanceId, createMsg, TimeoutConstants::getIpcStartStopTimeoutMs());
        
        if (createResponse.type != worker::MessageType::CREATE_INSTANCE_RESPONSE ||
            !createResponse.payload.get("success", false).asBool()) {
          std::cerr << "[SubprocessInstanceManager] Failed to create pipeline "
                       "for instance: "
                    << instanceId << std::endl;
          std::string createError = createResponse.payload.get("error", "Unknown error").asString();
          std::cerr << "[SubprocessInstanceManager] Error: " << createError << std::endl;
          return false;
        }
        
        std::cout << "[SubprocessInstanceManager] Pipeline created successfully, "
                     "retrying start for instance: "
                  << instanceId << std::endl;
        
        // Retry START_INSTANCE after pipeline is built
        msg.type = worker::MessageType::START_INSTANCE;
        msg.payload["instance_id"] = instanceId;
        
        response = supervisor_->sendToWorker(
            instanceId, msg, TimeoutConstants::getIpcStartStopTimeoutMs());
      } else {
        std::cerr << "[SubprocessInstanceManager] Instance not found in cache: "
                  << instanceId << std::endl;
        return false;
      }
    }
  }

  if (response.type == worker::MessageType::START_INSTANCE_RESPONSE &&
      response.payload.get("success", false).asBool()) {

    // Verify instance is actually running by querying status
    // Wait for pipeline to start (up to 15 seconds with retries)
    // Note: Pipeline starts async, so we need more time for RTSP connection
    const int maxRetries = 30; // 30 retries * 500ms = 15 seconds
    const int retryDelayMs = 500;
    bool verified = false;

    for (int retry = 0; retry < maxRetries; retry++) {
      // Query instance status
      worker::IPCMessage statusMsg;
      statusMsg.type = worker::MessageType::GET_INSTANCE_STATUS;
      statusMsg.payload["instance_id"] = instanceId;

      // Use configurable timeout for status check (default: 3 seconds)
      auto statusResponse = supervisor_->sendToWorker(
          instanceId, statusMsg, TimeoutConstants::getIpcStatusTimeoutMs());

      if (statusResponse.type ==
          worker::MessageType::GET_INSTANCE_STATUS_RESPONSE) {
        if (statusResponse.payload.isMember("data")) {
          const auto &data = statusResponse.payload["data"];
          bool running = data.get("running", false).asBool();
          std::string state = data.get("state", "").asString();

          // Accept both "running" and "starting" states (pipeline is in
          // process)
          if (running || state == "starting") {
            // If still starting, continue waiting
            if (state == "starting") {
              if (retry < maxRetries - 1) {
                // Don't break yet, wait for it to become running
                std::this_thread::sleep_for(
                    std::chrono::milliseconds(retryDelayMs));
                continue;
              } else {
                // Timeout but state is "starting", consider it OK (will become
                // running soon)
                verified = true;
                std::cout << "[SubprocessInstanceManager] Instance "
                          << instanceId
                          << " is starting (will become running soon)"
                          << std::endl;
                break;
              }
            } else if (running) {
              verified = true;
              std::cout << "[SubprocessInstanceManager] ✓ Verified instance "
                        << instanceId << " is running (retry " << (retry + 1)
                        << "/" << maxRetries << ")" << std::endl;
              break;
            }
          } else {
            // Check if there's an error
            if (data.isMember("last_error") &&
                !data["last_error"].asString().empty()) {
              std::cerr << "[SubprocessInstanceManager] Instance " << instanceId
                        << " start failed with error: "
                        << data["last_error"].asString() << std::endl;
              // Update local state to reflect failure
              {
                std::lock_guard<std::mutex> lock(instances_mutex_);
                if (instances_.count(instanceId)) {
                  instances_[instanceId].running = false;
                }
              }
              return false;
            }
          }
        }
      }

      // Wait before retry
      if (retry < maxRetries - 1) {
        std::this_thread::sleep_for(std::chrono::milliseconds(retryDelayMs));
      }
    }

    if (!verified) {
      std::cerr << "[SubprocessInstanceManager] ✗ Failed to verify instance "
                << instanceId << " is running after start command" << std::endl;
      // Update local state to reflect failure
      {
        std::lock_guard<std::mutex> lock(instances_mutex_);
        if (instances_.count(instanceId)) {
          instances_[instanceId].running = false;
        }
      }
      return false;
    }

    // Update local state - ensure instance exists in cache
    {
      std::lock_guard<std::mutex> lock(instances_mutex_);
      if (instances_.count(instanceId)) {
        instances_[instanceId].running = true;
      } else {
        // Instance not in local cache but worker exists and is running
        // This can happen if instance was loaded from storage but cache was
        // cleared or instance was created elsewhere. Create basic instance info
        // from worker.
        std::cerr << "[SubprocessInstanceManager] Warning: Instance "
                  << instanceId
                  << " not in local cache but worker is running. Creating "
                     "cache entry..."
                  << std::endl;

        // Try to get worker info to create instance info
        auto optWorkerInfo = supervisor_->getWorkerInfo(instanceId);
        if (optWorkerInfo.has_value()) {
          InstanceInfo info;
          info.instanceId = instanceId;
          info.displayName = instanceId; // Default name
          info.running = true;
          info.loaded = true;
          info.autoStart = false;
          info.persistent = false;
          info.startTime = std::chrono::steady_clock::now();

          // Try to load from storage if available
          auto optStoredInfo = instance_storage_.loadInstance(instanceId);
          if (optStoredInfo.has_value()) {
            const auto &storedInfo = optStoredInfo.value();
            info.displayName = storedInfo.displayName;
            info.solutionId = storedInfo.solutionId;
            info.autoStart = storedInfo.autoStart;
            info.persistent = storedInfo.persistent;
            info.additionalParams = storedInfo.additionalParams;
            info.rtspUrl = storedInfo.rtspUrl;
            info.rtmpUrl = storedInfo.rtmpUrl;
            info.filePath = storedInfo.filePath;
            std::cout << "[SubprocessInstanceManager] Loaded instance info "
                         "from storage for "
                      << instanceId << std::endl;
          }

          instances_[instanceId] = info;
          std::cout
              << "[SubprocessInstanceManager] Created cache entry for instance "
              << instanceId << std::endl;
        } else {
          std::cerr << "[SubprocessInstanceManager] Error: Cannot create cache "
                       "entry for "
                    << instanceId << " - worker info not available"
                    << std::endl;
        }
      }
    }

    std::cout << "[SubprocessInstanceManager] ✓ Started and verified instance: "
              << instanceId << std::endl;
    return true;
  }

  std::cerr << "[SubprocessInstanceManager] Failed to start: " << instanceId
            << " - "
            << response.payload.get("error", "Unknown error").asString()
            << std::endl;
  return false;
}

bool SubprocessInstanceManager::stopInstance(const std::string &instanceId) {
  // Check worker state - accept both READY and BUSY states
  // BUSY is OK because worker can handle multiple commands
  auto workerState = supervisor_->getWorkerState(instanceId);
  if (workerState != worker::WorkerState::READY &&
      workerState != worker::WorkerState::BUSY) {
    std::cerr << "[SubprocessInstanceManager] Worker not ready: " << instanceId
              << " (state: " << static_cast<int>(workerState) << ")"
              << std::endl;
    return false;
  }

  // Send STOP command to worker
  // Use configurable timeout for stop operation (default: 10 seconds)
  worker::IPCMessage msg;
  msg.type = worker::MessageType::STOP_INSTANCE;
  msg.payload["instance_id"] = instanceId;

  auto response = supervisor_->sendToWorker(
      instanceId, msg, TimeoutConstants::getIpcStartStopTimeoutMs());

  if (response.type == worker::MessageType::STOP_INSTANCE_RESPONSE &&
      response.payload.get("success", false).asBool()) {

    // Update local state
    {
      std::lock_guard<std::mutex> lock(instances_mutex_);
      if (instances_.count(instanceId)) {
        instances_[instanceId].running = false;
      }
    }

    std::cout << "[SubprocessInstanceManager] Stopped instance: " << instanceId
              << std::endl;
    return true;
  }

  std::cerr << "[SubprocessInstanceManager] Failed to stop: " << instanceId
            << std::endl;
  return false;
}

bool SubprocessInstanceManager::updateInstance(const std::string &instanceId,
                                               const Json::Value &configJson) {
  logRuntimeUpdate(instanceId, "api: sending UPDATE_INSTANCE");
  // Check worker state - accept both READY and BUSY states
  // BUSY is OK because worker can handle multiple commands
  auto workerState = supervisor_->getWorkerState(instanceId);
  if (workerState != worker::WorkerState::READY &&
      workerState != worker::WorkerState::BUSY) {
    std::cerr << "[SubprocessInstanceManager] Worker not ready: " << instanceId
              << " (state: " << static_cast<int>(workerState) << ")"
              << std::endl;
    return false;
  }

  // Send UPDATE command to worker
  // Use configurable timeout for update operation (default: 10 seconds)
  worker::IPCMessage msg;
  msg.type = worker::MessageType::UPDATE_INSTANCE;
  msg.payload["instance_id"] = instanceId;
  msg.payload["config"] = configJson;

  auto response = supervisor_->sendToWorker(
      instanceId, msg, TimeoutConstants::getIpcStartStopTimeoutMs());

  if (response.type != worker::MessageType::UPDATE_INSTANCE_RESPONSE) {
    logRuntimeUpdate(instanceId, "api: invalid response type");
    std::cerr
        << "[SubprocessInstanceManager] Invalid response type for update: "
        << static_cast<int>(response.type) << std::endl;
    return false;
  }

  bool success = response.payload.get("success", false).asBool();
  if (success) {
    logRuntimeUpdate(instanceId, "api: update success");
  } else {
    std::string error =
        response.payload.get("error", "Unknown error").asString();
    logRuntimeUpdate(instanceId, "api: update failed: " + error);
  }

  if (success) {
    // Update local cache with new config
    std::lock_guard<std::mutex> lock(instances_mutex_);
    auto it = instances_.find(instanceId);
    if (it != instances_.end()) {
      // Update instance info from config if possible
      // Note: Full config merge is handled by worker, we just update local
      // cache
      if (configJson.isMember("DisplayName")) {
        it->second.displayName = configJson["DisplayName"].asString();
      }
      if (configJson.isMember("AdditionalParams")) {
        const auto &params = configJson["AdditionalParams"];
        if (params.isObject()) {
          for (const auto &key : params.getMemberNames()) {
            it->second.additionalParams[key] = params[key].asString();
          }
          // Update URLs from AdditionalParams
          // Check RTSP_DES_URL first (for output), then RTSP_SRC_URL (for input), then RTSP_URL (backward compatibility)
          if (it->second.additionalParams.count("RTSP_DES_URL")) {
            it->second.rtspUrl = it->second.additionalParams.at("RTSP_DES_URL");
          } else if (it->second.additionalParams.count("RTSP_SRC_URL")) {
            it->second.rtspUrl = it->second.additionalParams.at("RTSP_SRC_URL");
          } else if (it->second.additionalParams.count("RTSP_URL")) {
            it->second.rtspUrl = it->second.additionalParams.at("RTSP_URL");
          }
          // Check RTMP_DES_URL first (new format), then RTMP_URL (backward compatibility)
          if (it->second.additionalParams.count("RTMP_DES_URL")) {
            it->second.rtmpUrl = it->second.additionalParams.at("RTMP_DES_URL");
          } else if (it->second.additionalParams.count("RTMP_URL")) {
            it->second.rtmpUrl = it->second.additionalParams.at("RTMP_URL");
          }
          if (it->second.additionalParams.count("FILE_PATH")) {
            it->second.filePath = it->second.additionalParams.at("FILE_PATH");
          }
        }
      }
    }
    std::cout << "[SubprocessInstanceManager] Updated instance: " << instanceId
              << std::endl;
  } else {
    std::string error =
        response.payload.get("error", "Unknown error").asString();
    std::cerr << "[SubprocessInstanceManager] Failed to update instance "
              << instanceId << ": " << error << std::endl;
  }

  return success;
}

std::optional<InstanceInfo>
SubprocessInstanceManager::getInstance(const std::string &instanceId) const {
  std::lock_guard<std::mutex> lock(instances_mutex_);
  auto it = instances_.find(instanceId);
  if (it != instances_.end()) {
    // Return cached InstanceInfo only. Do NOT call getInstanceStatistics()
    // here: it performs sendToWorker(GET_INSTANCE_STATUS) and can block the
    // calling thread for up to IPC_STATUS_TIMEOUT_MS (default 3s). When
    // instances are running, that caused all API handlers (GET instance,
    // list, etc.) to block and made the server appear unresponsive.
    // Use GET /v1/core/instance/{id}/statistics for fresh FPS when needed.
    return it->second;
  }
  return std::nullopt;
}

std::vector<std::string> SubprocessInstanceManager::listInstances() const {
  std::lock_guard<std::mutex> lock(instances_mutex_);
  std::vector<std::string> ids;
  ids.reserve(instances_.size());
  for (const auto &[id, _] : instances_) {
    ids.push_back(id);
  }
  return ids;
}

std::vector<InstanceInfo> SubprocessInstanceManager::getAllInstances() const {
  // CRITICAL: Sync workers with cache before returning instances
  // This ensures all running workers are included even if cache was cleared
  // or instance was started without being added to cache

  // Get all worker IDs from supervisor (these are actual running instances)
  auto workerIds = supervisor_->getWorkerIds();

  // Ensure all workers are in cache
  for (const auto &instanceId : workerIds) {
    {
      std::lock_guard<std::mutex> lock(instances_mutex_);
      // If instance not in cache, restore it
      if (instances_.count(instanceId) == 0) {
        // Try to load from storage first
        auto optStoredInfo = instance_storage_.loadInstance(instanceId);
        if (optStoredInfo.has_value()) {
          instances_[instanceId] = optStoredInfo.value();
          // Update running status based on worker state
          auto workerState = supervisor_->getWorkerState(instanceId);
          instances_[instanceId].running =
              (workerState == worker::WorkerState::READY ||
               workerState == worker::WorkerState::BUSY);
          std::cout << "[SubprocessInstanceManager] Restored instance "
                    << instanceId
                    << " from storage to cache in getAllInstances()"
                    << std::endl;
        } else {
          // Create minimal instance info from worker
          InstanceInfo info;
          info.instanceId = instanceId;
          info.displayName = instanceId;
          auto workerState = supervisor_->getWorkerState(instanceId);
          info.running = (workerState == worker::WorkerState::READY ||
                          workerState == worker::WorkerState::BUSY);
          info.loaded = true;
          info.autoStart = false;
          info.persistent = false;
          info.startTime = std::chrono::steady_clock::now();
          instances_[instanceId] = info;
          std::cout << "[SubprocessInstanceManager] Created minimal instance "
                       "entry for "
                    << instanceId << " in getAllInstances()" << std::endl;
        }
      }
    }
  }

  // Now get all instances from cache (after syncing with workers)
  // CRITICAL: Do NOT call getInstance() for each instance as it calls
  // getInstanceStatistics() which can block for up to 5 seconds per instance.
  // Instead, return cached info directly and only update FPS if it's safe to do
  // so (non-blocking or with timeout)
  std::vector<InstanceInfo> result;
  std::vector<std::string> instanceIds;
  {
    std::lock_guard<std::mutex> lock(instances_mutex_);
    instanceIds.reserve(instances_.size());
    for (const auto &[instanceId, _] : instances_) {
      instanceIds.push_back(instanceId);
    }
    // Copy all instances while holding lock (fast operation)
    result.reserve(instanceIds.size());
    for (const auto &instanceId : instanceIds) {
      result.push_back(instances_.at(instanceId));
    }
  }

  // Try to update FPS for running instances using async with timeout
  // This prevents getAllInstances() from blocking if statistics call hangs
  for (auto &info : result) {
    if (info.running) {
      // Try to get FPS asynchronously with short timeout (500ms)
      // If it fails or times out, just use cached FPS value
      try {
        auto future = std::async(std::launch::async, [this, &info]() -> double {
          try {
            auto optStats = const_cast<SubprocessInstanceManager *>(this)
                                ->getInstanceStatistics(info.instanceId);
            if (optStats.has_value()) {
              return optStats.value().current_framerate;
            }
          } catch (...) {
            // Ignore errors, return cached FPS
          }
          return info.fps; // Return cached FPS on error
        });

        auto status = future.wait_for(std::chrono::milliseconds(500));
        if (status == std::future_status::ready) {
          info.fps = future.get();
        }
        // If timeout, just use cached FPS - don't block
      } catch (...) {
        // Ignore exceptions, use cached FPS
      }
    }
  }

  return result;
}

bool SubprocessInstanceManager::hasInstance(
    const std::string &instanceId) const {
  std::lock_guard<std::mutex> lock(instances_mutex_);
  return instances_.count(instanceId) > 0;
}

int SubprocessInstanceManager::getInstanceCount() const {
  std::lock_guard<std::mutex> lock(instances_mutex_);
  return static_cast<int>(instances_.size());
}

std::optional<InstanceStatistics>
SubprocessInstanceManager::getInstanceStatistics(
    const std::string &instanceId) {
  // Flush output immediately to ensure logs appear
  std::cout.flush();
  std::cerr.flush();

  std::cout
      << "[SubprocessInstanceManager] ===== getInstanceStatistics START ====="
      << std::endl;
  std::cout << "[SubprocessInstanceManager] Thread ID: "
            << std::this_thread::get_id() << std::endl;
  std::cout << "[SubprocessInstanceManager] Instance ID: " << instanceId
            << std::endl;
  std::cout.flush();

  // Check worker state first - if worker exists and is running, we can get
  // statistics even if instance is not in local cache (e.g., cache was cleared
  // but worker is still running)
  std::cout << "[SubprocessInstanceManager] About to call "
               "supervisor_->getWorkerState()..."
            << std::endl;
  std::cout.flush();
  auto getState_start = std::chrono::steady_clock::now();
  auto workerState = supervisor_->getWorkerState(instanceId);
  auto getState_end = std::chrono::steady_clock::now();
  auto getState_duration =
      std::chrono::duration_cast<std::chrono::milliseconds>(getState_end -
                                                            getState_start)
          .count();

  std::cout << "[SubprocessInstanceManager] getWorkerState() completed in "
            << getState_duration << "ms" << std::endl;
  std::cout << "[SubprocessInstanceManager] Worker state: "
            << static_cast<int>(workerState) << std::endl;

  // If worker doesn't exist at all, check if instance exists in cache/storage
  // If worker exists and is running, we can proceed even without instance in
  // cache
  if (workerState == worker::WorkerState::STOPPED) {
    // Worker doesn't exist - check if instance exists in cache or storage
    bool instanceExists = false;
    {
      std::lock_guard<std::mutex> lock(instances_mutex_);
      instanceExists = instances_.find(instanceId) != instances_.end();
    }

    if (!instanceExists) {
      auto optStoredInfo = instance_storage_.loadInstance(instanceId);
      instanceExists = optStoredInfo.has_value();
    }

    if (!instanceExists) {
      std::cerr << "[SubprocessInstanceManager] Instance not found: "
                << instanceId
                << " (not in cache or storage, and worker doesn't exist)"
                << std::endl;
      return std::nullopt;
    }
  }

  // Check if instance exists in cache or storage (for restoring running flag
  // later)
  bool instanceExists = false;
  bool instanceRunning = false;
  {
    std::lock_guard<std::mutex> lock(instances_mutex_);
    auto it = instances_.find(instanceId);
    if (it != instances_.end()) {
      instanceExists = true;
      instanceRunning = it->second.running;
      std::cout
          << "[SubprocessInstanceManager] Instance found in cache, running: "
          << instanceRunning << std::endl;
    }
  }

  if (!instanceExists) {
    // Try to load from storage
    auto optStoredInfo = instance_storage_.loadInstance(instanceId);
    if (optStoredInfo.has_value()) {
      instanceExists = true;
      instanceRunning = optStoredInfo.value().running;
      std::cout
          << "[SubprocessInstanceManager] Instance found in storage, running: "
          << instanceRunning << std::endl;
    }
  }

  // If worker is STOPPED or CRASHED, but instance exists and was running
  // Try to spawn worker if needed (similar to startInstance logic)
  if (workerState == worker::WorkerState::STOPPED ||
      workerState == worker::WorkerState::CRASHED) {
    std::cerr << "[SubprocessInstanceManager] Worker for instance "
              << instanceId << " is "
              << (workerState == worker::WorkerState::CRASHED ? "crashed"
                                                              : "stopped")
              << " but instance exists" << std::endl;

    // If instance was running, try to restore worker
    if (instanceRunning) {
      std::cout << "[SubprocessInstanceManager] Instance was running, "
                   "attempting to restore worker..."
                << std::endl;
      // Try to spawn worker from storage
      auto optStoredInfo = instance_storage_.loadInstance(instanceId);
      if (optStoredInfo.has_value()) {
        const auto &info = optStoredInfo.value();
        Json::Value config;
        config["SolutionId"] = info.solutionId;
        config["DisplayName"] = info.displayName;
        config["RtspUrl"] = info.rtspUrl;
        config["RtmpUrl"] = info.rtmpUrl;
        config["FilePath"] = info.filePath;

        if (!info.additionalParams.empty()) {
          Json::Value params;
          for (const auto &[key, value] : info.additionalParams) {
            params[key] = value;
          }
          config["AdditionalParams"] = params;
        }

        if (allocateGPUAndSpawnWorker(instanceId, config)) {
          // Add to cache
          {
            std::lock_guard<std::mutex> lock(instances_mutex_);
            instances_[instanceId] = info;
          }
          std::cout << "[SubprocessInstanceManager] Worker spawned, waiting "
                       "for ready..."
                    << std::endl;
          // Wait for worker to become ready (with timeout)
          const int maxWaitRetries = 20; // 20 retries * 250ms = 5 seconds
          const int waitDelayMs = 250;
          bool workerReady = false;
          for (int retry = 0; retry < maxWaitRetries; retry++) {
            workerState = supervisor_->getWorkerState(instanceId);
            if (workerState == worker::WorkerState::READY ||
                workerState == worker::WorkerState::BUSY) {
              workerReady = true;
              std::cout
                  << "[SubprocessInstanceManager] Worker became ready after "
                  << (retry * waitDelayMs) << "ms" << std::endl;
              break;
            }
            if (retry < maxWaitRetries - 1) {
              std::this_thread::sleep_for(
                  std::chrono::milliseconds(waitDelayMs));
            }
          }
          if (!workerReady) {
            std::cerr << "[SubprocessInstanceManager] Worker did not become "
                         "ready after spawn"
                      << std::endl;
          }
        }
      }
    }

    // If still STOPPED or CRASHED after trying to restore, return nullopt
    if (workerState == worker::WorkerState::STOPPED ||
        workerState == worker::WorkerState::CRASHED) {
      std::cerr << "[SubprocessInstanceManager] Cannot get statistics: Worker "
                   "for instance "
                << instanceId << " is "
                << (workerState == worker::WorkerState::CRASHED ? "crashed"
                                                                : "stopped")
                << std::endl;
      return std::nullopt;
    }
  }

  // CRITICAL: If worker is in STARTING state, wait for it to become READY
  // sendToWorker() only accepts READY or BUSY states
  if (workerState == worker::WorkerState::STARTING) {
    std::cout << "[SubprocessInstanceManager] Worker is STARTING, waiting for "
                 "READY..."
              << std::endl;
    const int maxWaitRetries = 20; // 20 retries * 250ms = 5 seconds
    const int waitDelayMs = 250;
    bool workerReady = false;
    for (int retry = 0; retry < maxWaitRetries; retry++) {
      workerState = supervisor_->getWorkerState(instanceId);
      if (workerState == worker::WorkerState::READY ||
          workerState == worker::WorkerState::BUSY) {
        workerReady = true;
        std::cout << "[SubprocessInstanceManager] Worker became ready after "
                  << (retry * waitDelayMs) << "ms" << std::endl;
        break;
      }
      if (workerState == worker::WorkerState::STOPPED ||
          workerState == worker::WorkerState::CRASHED) {
        std::cerr << "[SubprocessInstanceManager] Worker stopped/crashed while "
                     "waiting for READY"
                  << std::endl;
        return std::nullopt;
      }
      if (retry < maxWaitRetries - 1) {
        std::this_thread::sleep_for(std::chrono::milliseconds(waitDelayMs));
      }
    }
    if (!workerReady) {
      std::cerr << "[SubprocessInstanceManager] Worker did not become ready "
                   "after waiting (timeout)"
                << std::endl;
      return std::nullopt;
    }
  }

  // If worker is STOPPING, we cannot get statistics
  if (workerState == worker::WorkerState::STOPPING) {
    std::cerr << "[SubprocessInstanceManager] Cannot get statistics: Worker "
                 "for instance "
              << instanceId << " is stopping" << std::endl;
    return std::nullopt;
  }

  // Worker exists and is in a valid state (READY or BUSY)
  // Try to get statistics from worker
  // Note: Even if instance is not in local cache, we can still get statistics
  // from worker

  // If instance not in cache but worker exists, try to restore cache entry
  {
    std::lock_guard<std::mutex> lock(instances_mutex_);
    if (instances_.count(instanceId) == 0) {
      // Instance not in cache but worker exists - restore from storage
      std::cerr << "[SubprocessInstanceManager] Warning: Instance "
                << instanceId
                << " not in local cache but worker exists. Attempting to "
                   "restore from storage..."
                << std::endl;

      auto optStoredInfo = instance_storage_.loadInstance(instanceId);
      if (optStoredInfo.has_value()) {
        instances_[instanceId] = optStoredInfo.value();
        std::cout << "[SubprocessInstanceManager] Restored instance "
                  << instanceId << " from storage to cache" << std::endl;
      } else {
        // Create minimal instance info from worker
        // First, try to get actual status from worker to determine if instance
        // is running
        bool isRunning = false;
        try {
          worker::IPCMessage statusMsg;
          statusMsg.type = worker::MessageType::GET_INSTANCE_STATUS;
          statusMsg.payload["instance_id"] = instanceId;
          auto statusResponse = supervisor_->sendToWorker(
              instanceId, statusMsg, TimeoutConstants::getIpcStatusTimeoutMs());
          if (statusResponse.type ==
              worker::MessageType::GET_INSTANCE_STATUS_RESPONSE) {
            if (statusResponse.payload.isMember("data")) {
              const auto &data = statusResponse.payload["data"];
              isRunning = data.get("running", false).asBool();
            }
          }
        } catch (...) {
          // If we can't get status, assume running if worker is READY/BUSY
          isRunning = (workerState == worker::WorkerState::READY ||
                       workerState == worker::WorkerState::BUSY);
        }

        InstanceInfo info;
        info.instanceId = instanceId;
        info.displayName = instanceId;
        info.running = isRunning;
        info.loaded = true;
        info.autoStart = false;
        info.persistent = false;
        info.startTime = std::chrono::steady_clock::now();
        instances_[instanceId] = info;
        std::cout
            << "[SubprocessInstanceManager] Created minimal instance entry for "
            << instanceId << " (running: " << (isRunning ? "true" : "false")
            << ")" << std::endl;
      }
    }
  }

  // Don't check isWorkerReady here - sendToWorker handles worker state check
  // and accepts both READY and BUSY states, so statistics can be retrieved
  // even while pipeline is starting or other operations are in progress

  // Send GET_STATISTICS command to worker
  // Use configurable timeout for API calls (default: 5 seconds)
  worker::IPCMessage msg;
  msg.type = worker::MessageType::GET_STATISTICS;
  msg.payload["instance_id"] = instanceId;

  auto timeoutMs = TimeoutConstants::getIpcApiTimeoutMs();
  std::cout << "[SubprocessInstanceManager] ===== SENDING GET_STATISTICS TO "
               "WORKER ====="
            << std::endl;
  std::cout << "[SubprocessInstanceManager] Instance ID: " << instanceId
            << std::endl;
  std::cout << "[SubprocessInstanceManager] Timeout: " << timeoutMs << "ms"
            << std::endl;
  std::cout
      << "[SubprocessInstanceManager] Calling supervisor_->sendToWorker()..."
      << std::endl;

  auto send_start = std::chrono::steady_clock::now();
  auto response = supervisor_->sendToWorker(instanceId, msg, timeoutMs);
  auto send_end = std::chrono::steady_clock::now();
  auto send_duration = std::chrono::duration_cast<std::chrono::milliseconds>(
                           send_end - send_start)
                           .count();

  std::cout
      << "[SubprocessInstanceManager] ===== RECEIVED RESPONSE FROM WORKER ====="
      << std::endl;
  std::cout << "[SubprocessInstanceManager] Duration: " << send_duration << "ms"
            << std::endl;
  std::cout << "[SubprocessInstanceManager] Response type: "
            << static_cast<int>(response.type) << " (expected: "
            << static_cast<int>(worker::MessageType::GET_STATISTICS_RESPONSE)
            << ")" << std::endl;

  // Debug logging for better troubleshooting
  if (response.type != worker::MessageType::GET_STATISTICS_RESPONSE) {
    std::cerr
        << "[SubprocessInstanceManager] Failed to get statistics for instance "
        << instanceId << ": Invalid response type "
        << static_cast<int>(response.type) << " (expected: "
        << static_cast<int>(worker::MessageType::GET_STATISTICS_RESPONSE) << ")"
        << std::endl;

    // Check for specific error responses
    if (response.type == worker::MessageType::ERROR_RESPONSE) {
      if (response.payload.isMember("error")) {
        std::cerr << "[SubprocessInstanceManager] Worker error: "
                  << response.payload["error"].asString() << std::endl;
      }
      if (response.payload.isMember("message")) {
        std::cerr << "[SubprocessInstanceManager] Worker message: "
                  << response.payload["message"].asString() << std::endl;
      }
    } else if (response.type == worker::MessageType::PONG) {
      std::cerr << "[SubprocessInstanceManager] Received PONG instead of "
                   "statistics - worker may be busy or not ready"
                << std::endl;
    }
    return std::nullopt;
  }

  // Check success field - also accept if success field is missing but status is
  // OK Worker response format: { "success": true/false, "status": 0, "data":
  // {...} }
  bool hasSuccess = response.payload.isMember("success");
  bool success = response.payload.get("success", true)
                     .asBool(); // Default to true if missing
  bool hasStatus = response.payload.isMember("status");
  int status =
      response.payload.get("status", 0).asInt(); // Default to 0 (OK) if missing

  std::cout << "[SubprocessInstanceManager] Response check - success: "
            << success << " (has field: " << hasSuccess
            << "), status: " << status << " (has field: " << hasStatus << ")"
            << std::endl;

  // Check if response indicates error
  // Accept if: success=true OR (status=0 AND success field missing)
  if (hasSuccess && !success) {
    // Explicit failure
    std::string errorMsg =
        response.payload.get("error", "Unknown error").asString();
    std::string message = response.payload.get("message", "").asString();
    std::cerr
        << "[SubprocessInstanceManager] Statistics request failed for instance "
        << instanceId << ": " << errorMsg;
    if (!message.empty()) {
      std::cerr << " (" << message << ")";
    }
    std::cerr << " (status: " << status << ", success: " << success << ")"
              << std::endl;
    return std::nullopt;
  }

  if (hasStatus && status != 0) {
    // Status indicates error
    std::string errorMsg =
        response.payload.get("error", "Unknown error").asString();
    std::string message = response.payload.get("message", "").asString();
    std::cerr
        << "[SubprocessInstanceManager] Statistics request failed for instance "
        << instanceId << ": " << errorMsg;
    if (!message.empty()) {
      std::cerr << " (" << message << ")";
    }
    std::cerr << " (status: " << status << ", success: " << success << ")"
              << std::endl;
    return std::nullopt;
  }

  // Response is OK, parse statistics
  // Check for "data" field first (preferred format)
  if (response.payload.isMember("data")) {
    std::cout
        << "[SubprocessInstanceManager] Parsing statistics from 'data' field"
        << std::endl;
    InstanceStatistics stats;
    const auto &data = response.payload["data"];
    stats.frames_processed = data.get("frames_processed", 0).asUInt64();
    stats.start_time = data.get("start_time", 0).asInt64();
    stats.current_framerate = data.get("current_framerate", 0.0).asDouble();
    stats.source_framerate = data.get("source_framerate", 0.0).asDouble();
    stats.latency = data.get("latency", 0.0).asDouble();
    stats.input_queue_size = data.get("input_queue_size", 0).asUInt64();
    stats.dropped_frames_count = data.get("dropped_frames_count", 0).asUInt64();
    stats.resolution = data.get("resolution", "").asString();
    stats.source_resolution = data.get("source_resolution", "").asString();
    stats.format = data.get("format", "").asString();
    std::cout << "[SubprocessInstanceManager] Successfully parsed statistics "
                 "for instance "
              << instanceId << std::endl;
    return stats;
  }

  // Fallback: check if statistics fields are at root level
  if (response.payload.isMember("frames_processed") ||
      response.payload.isMember("current_framerate")) {
    std::cout << "[SubprocessInstanceManager] Parsing statistics from root "
                 "level fields"
              << std::endl;
    InstanceStatistics stats;
    stats.frames_processed =
        response.payload.get("frames_processed", 0).asUInt64();
    stats.start_time = response.payload.get("start_time", 0).asInt64();
    stats.current_framerate =
        response.payload.get("current_framerate", 0.0).asDouble();
    stats.source_framerate =
        response.payload.get("source_framerate", 0.0).asDouble();
    stats.latency = response.payload.get("latency", 0.0).asDouble();
    stats.input_queue_size =
        response.payload.get("input_queue_size", 0).asUInt64();
    stats.dropped_frames_count =
        response.payload.get("dropped_frames_count", 0).asUInt64();
    stats.resolution = response.payload.get("resolution", "").asString();
    stats.source_resolution =
        response.payload.get("source_resolution", "").asString();
    stats.format = response.payload.get("format", "").asString();
    std::cout << "[SubprocessInstanceManager] Successfully parsed statistics "
                 "from root level for instance "
              << instanceId << std::endl;
    return stats;
  }

  // No data field and no statistics fields - return empty statistics
  std::cerr << "[SubprocessInstanceManager] Warning: Statistics response OK "
               "but no data/statistics fields for instance "
            << instanceId << std::endl;
  std::cerr << "[SubprocessInstanceManager] Response payload keys: ";
  for (const auto &key : response.payload.getMemberNames()) {
    std::cerr << key << " ";
  }
  std::cerr << std::endl;
  InstanceStatistics stats;
  return stats;
}

std::string
SubprocessInstanceManager::getLastFrame(const std::string &instanceId) const {
  // Don't check isWorkerReady here - sendToWorker handles worker state check
  // and accepts both READY and BUSY states, so frame can be retrieved
  // even while pipeline is starting or other operations are in progress

  std::cout
      << "[SubprocessInstanceManager] getLastFrame() called for instance: "
      << instanceId << std::endl;

  // Send GET_LAST_FRAME command to worker
  // Use configurable timeout for API calls (default: 5 seconds)
  worker::IPCMessage msg;
  msg.type = worker::MessageType::GET_LAST_FRAME;
  msg.payload["instance_id"] = instanceId;

  std::cout << "[SubprocessInstanceManager] Sending GET_LAST_FRAME IPC message "
               "to worker for instance: "
            << instanceId << std::endl;

  auto response = const_cast<worker::WorkerSupervisor *>(supervisor_.get())
                      ->sendToWorker(instanceId, msg,
                                     TimeoutConstants::getIpcApiTimeoutMs());

  std::cout << "[SubprocessInstanceManager] Received response from worker for "
               "instance: "
            << instanceId
            << ", response type: " << static_cast<int>(response.type)
            << std::endl;

  if (response.type == worker::MessageType::GET_LAST_FRAME_RESPONSE &&
      response.payload.get("success", false).asBool()) {
    std::string frameBase64 =
        response.payload["data"].get("frame", "").asString();
    bool hasFrame = response.payload["data"].get("has_frame", false).asBool();

    std::cout << "[SubprocessInstanceManager] Worker response: success=true, "
                 "has_frame="
              << hasFrame << ", frame_size=" << frameBase64.length() << " chars"
              << std::endl;

    return frameBase64;
  }

  std::cout << "[SubprocessInstanceManager] Worker response: success=false or "
               "invalid response type"
            << std::endl;
  return "";
}

bool SubprocessInstanceManager::updateLines(const std::string &instanceId,
                                            const Json::Value &linesArray) {
  // Check worker state - accept both READY and BUSY states
  auto workerState = supervisor_->getWorkerState(instanceId);
  if (workerState != worker::WorkerState::READY &&
      workerState != worker::WorkerState::BUSY) {
    std::cerr << "[SubprocessInstanceManager] Worker not ready: " << instanceId
              << " (state: " << static_cast<int>(workerState) << ")"
              << std::endl;
    return false;
  }

  // Send UPDATE_LINES command to worker
  // Use configurable timeout for API calls (default: 5 seconds)
  worker::IPCMessage msg;
  msg.type = worker::MessageType::UPDATE_LINES;
  msg.payload["instance_id"] = instanceId;
  msg.payload["lines"] = linesArray;

  std::cout << "[SubprocessInstanceManager] Sending UPDATE_LINES IPC message "
               "to worker for instance: "
            << instanceId << std::endl;

  auto response = supervisor_->sendToWorker(
      instanceId, msg, TimeoutConstants::getIpcApiTimeoutMs());

  std::cout << "[SubprocessInstanceManager] Received response from worker for "
               "instance: "
            << instanceId
            << ", response type: " << static_cast<int>(response.type)
            << std::endl;

  if (response.type == worker::MessageType::UPDATE_LINES_RESPONSE &&
      response.payload.get("success", false).asBool()) {
    std::cout << "[SubprocessInstanceManager] ✓ Lines updated successfully via "
                 "hot reload (no restart needed)"
              << std::endl;
    return true;
  }

  std::cerr << "[SubprocessInstanceManager] Failed to update lines: "
            << response.payload.get("error", "Unknown error").asString()
            << std::endl;
  return false;
}

bool SubprocessInstanceManager::updateJams(const std::string &instanceId,
                                          const Json::Value &jamsJson) {
  auto workerState = supervisor_->getWorkerState(instanceId);
  if (workerState != worker::WorkerState::READY &&
      workerState != worker::WorkerState::BUSY) {
    std::cerr << "[SubprocessInstanceManager] Worker not ready: " << instanceId
              << std::endl;
    return false;
  }
  worker::IPCMessage msg;
  msg.type = worker::MessageType::UPDATE_JAMS;
  msg.payload["instance_id"] = instanceId;
  msg.payload["jams"] = jamsJson;
  auto response = supervisor_->sendToWorker(
      instanceId, msg, TimeoutConstants::getIpcApiTimeoutMs());
  if (response.type == worker::MessageType::UPDATE_JAMS_RESPONSE &&
      response.payload.get("success", false).asBool()) {
    return true;
  }
  std::cerr << "[SubprocessInstanceManager] Failed to update jams: "
            << response.payload.get("error", "Unknown error").asString()
            << std::endl;
  return false;
}

bool SubprocessInstanceManager::updateStops(const std::string &instanceId,
                                           const Json::Value &stopsJson) {
  auto workerState = supervisor_->getWorkerState(instanceId);
  if (workerState != worker::WorkerState::READY &&
      workerState != worker::WorkerState::BUSY) {
    std::cerr << "[SubprocessInstanceManager] Worker not ready: " << instanceId
              << std::endl;
    return false;
  }
  worker::IPCMessage msg;
  msg.type = worker::MessageType::UPDATE_STOPS;
  msg.payload["instance_id"] = instanceId;
  msg.payload["stops"] = stopsJson;
  auto response = supervisor_->sendToWorker(
      instanceId, msg, TimeoutConstants::getIpcApiTimeoutMs());
  if (response.type == worker::MessageType::UPDATE_STOPS_RESPONSE &&
      response.payload.get("success", false).asBool()) {
    return true;
  }
  std::cerr << "[SubprocessInstanceManager] Failed to update stops: "
            << response.payload.get("error", "Unknown error").asString()
            << std::endl;
  return false;
}

bool SubprocessInstanceManager::pushFrame(const std::string &instanceId,
                                           const std::string &frameBase64,
                                           const std::string &codec) {
  auto workerState = supervisor_->getWorkerState(instanceId);
  if (workerState != worker::WorkerState::READY &&
      workerState != worker::WorkerState::BUSY) {
    std::cerr << "[SubprocessInstanceManager] Worker not ready for pushFrame: "
              << instanceId << std::endl;
    return false;
  }
  worker::IPCMessage msg;
  msg.type = worker::MessageType::PUSH_FRAME;
  msg.payload["instance_id"] = instanceId;
  msg.payload["frame_base64"] = frameBase64;
  msg.payload["codec"] = codec;
  auto response = supervisor_->sendToWorker(
      instanceId, msg, TimeoutConstants::getIpcApiTimeoutMs());
  if (response.type == worker::MessageType::PUSH_FRAME_RESPONSE &&
      response.payload.get("success", false).asBool()) {
    return true;
  }
  std::cerr << "[SubprocessInstanceManager] pushFrame failed: "
            << response.payload.get("error", "Unknown error").asString()
            << std::endl;
  return false;
}

Json::Value SubprocessInstanceManager::getInstanceConfig(
    const std::string &instanceId) const {
  std::lock_guard<std::mutex> lock(instances_mutex_);
  auto it = instances_.find(instanceId);
  if (it != instances_.end()) {
    // Build config from InstanceInfo
    Json::Value config;
    config["InstanceId"] = it->second.instanceId;
    config["DisplayName"] = it->second.displayName;
    config["SolutionId"] = it->second.solutionId;
    config["Running"] = it->second.running;
    config["Loaded"] = it->second.loaded;
    config["RtspUrl"] = it->second.rtspUrl;
    config["RtmpUrl"] = it->second.rtmpUrl;
    config["FilePath"] = it->second.filePath;
    return config;
  }
  return Json::Value();
}

bool SubprocessInstanceManager::updateInstanceFromConfig(
    const std::string &instanceId, const Json::Value &configJson) {
  return updateInstance(instanceId, configJson);
}

bool SubprocessInstanceManager::hasRTMPOutput(
    const std::string &instanceId) const {
  std::lock_guard<std::mutex> lock(instances_mutex_);
  auto it = instances_.find(instanceId);
  if (it != instances_.end()) {
    return !it->second.rtmpUrl.empty();
  }
  return false;
}

void SubprocessInstanceManager::loadPersistentInstances() {
  // Get list of instance IDs from storage directory
  auto instanceIds = instance_storage_.loadAllInstances();
  if (instanceIds.empty()) {
    std::cout << "[SubprocessInstanceManager] No instances found in storage"
              << std::endl;
    return;
  }

  int loadedCount = 0;
  int skippedCount = 0;
  for (const auto &instanceId : instanceIds) {
    auto optInfo = instance_storage_.loadInstance(instanceId);
    if (!optInfo.has_value()) {
      continue;
    }

    const auto &info = optInfo.value();

    // Only load instances that are marked as persistent
    if (!info.persistent) {
      skippedCount++;
      std::cerr << "[SubprocessInstanceManager] Skipping non-persistent instance: "
                << instanceId << " (set persistent: true to auto-load on restart)"
                << std::endl;
      continue;
    }

    // Build config for worker
    Json::Value config;
    config["SolutionId"] = info.solutionId;
    config["DisplayName"] = info.displayName;
    config["RtspUrl"] = info.rtspUrl;
    config["RtmpUrl"] = info.rtmpUrl;
    config["FilePath"] = info.filePath;
    config["Persistent"] = info.persistent;

    // Spawn worker
    if (allocateGPUAndSpawnWorker(instanceId, config)) {
      std::lock_guard<std::mutex> lock(instances_mutex_);
      instances_[instanceId] = info;
      loadedCount++;

      // Auto-start if was running before
      if (info.autoStart || info.running) {
        startInstance(instanceId);
      }
    } else {
      std::cerr << "[SubprocessInstanceManager] Failed to restore instance: "
                << instanceId << std::endl;
    }
  }

  std::cout << "[SubprocessInstanceManager] Loaded " << loadedCount
            << " persistent instances";
  if (skippedCount > 0) {
    std::cout << " (skipped " << skippedCount << " non-persistent instances)";
  }
  std::cout << std::endl;
}

int SubprocessInstanceManager::checkAndHandleRetryLimits() {
  // In subprocess mode, retry limits are handled by WorkerSupervisor
  // Workers that crash are automatically restarted up to a limit
  // This method checks for instances that have exceeded retry limits

  std::lock_guard<std::mutex> lock(instances_mutex_);
  int stoppedCount = 0;

  // Check each instance for retry limit
  for (auto &[instanceId, info] : instances_) {
    // Check if instance has exceeded retry limit
    // In subprocess mode, retry limit is typically handled by WorkerSupervisor
    // But we can check local retry count if needed
    if (info.retryCount > 0 && info.retryLimitReached) {
      // Stop the instance
      stopInstance(instanceId);
      stoppedCount++;
    }
  }

  return stoppedCount;
}

bool SubprocessInstanceManager::loadInstance(const std::string &instanceId) {
  // Check if instance exists
  auto optInfo = getInstance(instanceId);
  if (!optInfo.has_value()) {
    std::cerr << "[SubprocessInstanceManager] Cannot load instance " << instanceId
              << ": Instance not found" << std::endl;
    return false;
  }

  InstanceInfo info = optInfo.value();

  // Check if already loaded (worker exists and state initialized) - make operation idempotent
  auto workerState = supervisor_->getWorkerState(instanceId);
  if (workerState != worker::WorkerState::STOPPED &&
      state_manager_.hasState(instanceId)) {
    std::cout << "[SubprocessInstanceManager] Instance " << instanceId
              << " is already loaded (worker state: " << static_cast<int>(workerState)
              << ", has state: true) - idempotent operation" << std::endl;
    return true; // Already loaded - idempotent success
  }

  // If worker doesn't exist, spawn it
  if (workerState == worker::WorkerState::STOPPED) {
    Json::Value config = buildWorkerConfigFromInstanceInfo(info);
    if (!allocateGPUAndSpawnWorker(instanceId, config)) {
      std::cerr << "[SubprocessInstanceManager] Cannot load instance " << instanceId
                << ": Failed to spawn worker process" << std::endl;
      return false; // Failed to spawn worker
    }
  }

  // Initialize state storage (if not already initialized)
  if (!state_manager_.hasState(instanceId)) {
    state_manager_.initializeState(instanceId);
  }

  // Update loaded flag in cache
  {
    std::lock_guard<std::mutex> lock(instances_mutex_);
    auto it = instances_.find(instanceId);
    if (it != instances_.end()) {
      it->second.loaded = true;
    }
  }

  std::cout << "[SubprocessInstanceManager] Successfully loaded instance "
            << instanceId << std::endl;
  return true;
}

bool SubprocessInstanceManager::unloadInstance(const std::string &instanceId) {
  // Check if instance exists
  auto optInfo = getInstance(instanceId);
  if (!optInfo.has_value()) {
    return false;
  }

  // Check if loaded
  if (!state_manager_.hasState(instanceId)) {
    return false; // Not loaded
  }

  // Stop instance if running
  auto workerState = supervisor_->getWorkerState(instanceId);
  if (workerState == worker::WorkerState::READY ||
      workerState == worker::WorkerState::BUSY) {
    stopInstance(instanceId);
  }

  // Terminate worker
  supervisor_->terminateWorker(instanceId, false);

  // Clear state storage
  state_manager_.clearState(instanceId);

  // Update loaded flag in cache
  {
    std::lock_guard<std::mutex> lock(instances_mutex_);
    auto it = instances_.find(instanceId);
    if (it != instances_.end()) {
      it->second.loaded = false;
    }
  }

  return true;
}

Json::Value
SubprocessInstanceManager::getInstanceState(const std::string &instanceId) {
  return state_manager_.getState(instanceId);
}

bool SubprocessInstanceManager::setInstanceState(const std::string &instanceId,
                                                  const std::string &path,
                                                  const Json::Value &value) {
  // Check if instance exists
  auto optInfo = getInstance(instanceId);
  if (!optInfo.has_value()) {
    return false;
  }

  InstanceInfo info = optInfo.value();

  // Instance must be loaded or running
  auto workerState = supervisor_->getWorkerState(instanceId);
  bool isLoaded = state_manager_.hasState(instanceId);
  bool isRunning = (workerState == worker::WorkerState::READY ||
                    workerState == worker::WorkerState::BUSY);

  if (!isLoaded && !isRunning) {
    return false;
  }

  // If not loaded but running, initialize state
  if (!isLoaded) {
    state_manager_.initializeState(instanceId);
  }

  return state_manager_.setState(instanceId, path, value);
}

void SubprocessInstanceManager::stopAllWorkers() {
  auto workerIds = supervisor_->getWorkerIds();
  for (const auto &id : workerIds) {
    supervisor_->terminateWorker(id, false);
  }
  supervisor_->stop();
}

Json::Value SubprocessInstanceManager::buildWorkerConfig(
    const CreateInstanceRequest &req) const {
  Json::Value config;

  // Get solution config if specified
  if (!req.solution.empty()) {
    auto optSolution = solution_registry_.getSolution(req.solution);
    if (optSolution.has_value()) {
      // Serialize solution config manually
      const auto &sol = optSolution.value();
      Json::Value solJson;
      solJson["SolutionId"] = sol.solutionId;
      solJson["SolutionName"] = sol.solutionName;
      solJson["SolutionType"] = sol.solutionType;
      solJson["IsDefault"] = sol.isDefault;
      config["Solution"] = solJson;
    }
  }

  // Add instance-specific config
  config["Name"] = req.name;
  config["Group"] = req.group;
  config["AutoStart"] = req.autoStart;
  config["Persistent"] = req.persistent;

  // Add additional parameters (includes source URLs)
  // CRITICAL: Serialize as flat structure (not nested) to ensure worker can parse correctly
  // The worker expects flat structure in AdditionalParams, not nested input/output
  for (const auto &[key, value] : req.additionalParams) {
    config["AdditionalParams"][key] = value;
  }

  // CRITICAL: Also set RTMP_URL at top level for backward compatibility
  // Some workers may check top-level RtmpUrl field
  if (req.additionalParams.count("RTMP_URL")) {
    config["RtmpUrl"] = req.additionalParams.at("RTMP_URL");
  } else if (req.additionalParams.count("RTMP_DES_URL")) {
    config["RtmpUrl"] = req.additionalParams.at("RTMP_DES_URL");
  }

  // Debug: Log RTMP_URL if present
  if (req.additionalParams.count("RTMP_URL")) {
    std::cout << "[SubprocessInstanceManager] RTMP_URL found in additionalParams: '"
              << req.additionalParams.at("RTMP_URL") << "'" << std::endl;
    std::cout << "[SubprocessInstanceManager] RTMP_URL also set in config[RtmpUrl]: '"
              << config["RtmpUrl"].asString() << "'" << std::endl;
  } else if (req.additionalParams.count("RTMP_DES_URL")) {
    std::cout << "[SubprocessInstanceManager] RTMP_DES_URL found in additionalParams: '"
              << req.additionalParams.at("RTMP_DES_URL") << "'" << std::endl;
    std::cout << "[SubprocessInstanceManager] RTMP_DES_URL also set in config[RtmpUrl]: '"
              << config["RtmpUrl"].asString() << "'" << std::endl;
  } else {
    std::cout << "[SubprocessInstanceManager] WARNING: No RTMP_URL or RTMP_DES_URL found in additionalParams"
              << std::endl;
    std::cout << "[SubprocessInstanceManager] Available additionalParams keys: ";
    for (const auto &[key, value] : req.additionalParams) {
      std::cout << key << " ";
    }
    std::cout << std::endl;
  }
  
  // Debug: Log serialized config to verify RTMP_URL is included
  std::cout << "[SubprocessInstanceManager] Serialized config AdditionalParams keys: ";
  if (config.isMember("AdditionalParams") && config["AdditionalParams"].isObject()) {
    for (const auto &key : config["AdditionalParams"].getMemberNames()) {
      std::cout << key << " ";
    }
  }
  std::cout << std::endl;

  return config;
}

Json::Value SubprocessInstanceManager::buildWorkerConfigFromInstanceInfo(
    const InstanceInfo &info) const {
  Json::Value config;

  // Get solution config if specified
  if (!info.solutionId.empty()) {
    auto optSolution = solution_registry_.getSolution(info.solutionId);
    if (optSolution.has_value()) {
      // Serialize solution config manually
      const auto &sol = optSolution.value();
      Json::Value solJson;
      solJson["SolutionId"] = sol.solutionId;
      solJson["SolutionName"] = sol.solutionName;
      solJson["SolutionType"] = sol.solutionType;
      solJson["IsDefault"] = sol.isDefault;
      config["Solution"] = solJson;
    }
  }

  // Add instance-specific config
  config["Name"] = info.displayName;
  config["Group"] = info.group;
  config["AutoStart"] = info.autoStart;
  config["Persistent"] = info.persistent;

  // Add URLs and file paths
  if (!info.rtspUrl.empty()) {
    config["AdditionalParams"]["RTSP_URL"] = info.rtspUrl;
  }
  if (!info.rtmpUrl.empty()) {
    config["AdditionalParams"]["RTMP_URL"] = info.rtmpUrl;
  }
  if (!info.filePath.empty()) {
    config["AdditionalParams"]["FILE_PATH"] = info.filePath;
  }

  // Add all additional parameters
  for (const auto &[key, value] : info.additionalParams) {
    config["AdditionalParams"][key] = value;
  }

  return config;
}

void SubprocessInstanceManager::updateInstanceCache(
    const std::string &instanceId, const worker::IPCMessage &response) {
  if (!response.payload.isMember("data")) {
    return;
  }

  std::lock_guard<std::mutex> lock(instances_mutex_);
  auto it = instances_.find(instanceId);
  if (it == instances_.end()) {
    return;
  }

  const auto &data = response.payload["data"];
  if (data.isMember("running")) {
    it->second.running = data["running"].asBool();
  }
}

void SubprocessInstanceManager::onWorkerStateChange(
    const std::string &instanceId, worker::WorkerState oldState,
    worker::WorkerState newState) {
  std::cout << "[SubprocessInstanceManager] Worker " << instanceId
            << " state: " << static_cast<int>(oldState) << " -> "
            << static_cast<int>(newState) << std::endl;

  // Update local state based on worker state
  std::lock_guard<std::mutex> lock(instances_mutex_);
  auto it = instances_.find(instanceId);
  if (it == instances_.end()) {
    return;
  }

  switch (newState) {
  case worker::WorkerState::READY:
    // READY means worker is ready to accept commands, NOT that instance stopped
    // Don't modify running flag here - it's managed by
    // startInstance/stopInstance
    it->second.loaded = true;
    break;
  case worker::WorkerState::BUSY:
    // BUSY means worker is processing a command
    // Don't modify running flag here either
    break;
  case worker::WorkerState::CRASHED:
    it->second.running = false;
    it->second.loaded = false;
    it->second.retryLimitReached = true;
    break;
  case worker::WorkerState::STOPPED:
    it->second.running = false;
    it->second.loaded = false;
    break;
  case worker::WorkerState::STOPPING:
    // Don't modify running flag - instance might still be running during stop
    break;
  default:
    break;
  }
}

void SubprocessInstanceManager::onWorkerError(const std::string &instanceId,
                                              const std::string &error) {
  std::cerr << "[SubprocessInstanceManager] Worker " << instanceId
            << " error: " << error << std::endl;

  std::lock_guard<std::mutex> lock(instances_mutex_);
  auto it = instances_.find(instanceId);
  if (it != instances_.end()) {
    it->second.running = false;
    it->second.retryCount++;
  }
}

bool SubprocessInstanceManager::allocateGPUAndSpawnWorker(const std::string &instanceId, 
                                                         const Json::Value &config) {
  // Allocate GPU resource for this instance
  // Estimate memory requirement: 1.5GB per instance (can be adjusted)
  int gpu_device_id = -1;
  auto &resourceManager = ResourceManager::getInstance();
  auto gpu_allocation = resourceManager.allocateGPU(1536); // 1.5GB
  
  if (gpu_allocation) {
    gpu_device_id = gpu_allocation->device_id;
    {
      std::lock_guard<std::mutex> lock(gpu_allocations_mutex_);
      gpu_allocations_[instanceId] = gpu_allocation;
    }
    std::cout << "[SubprocessInstanceManager] Allocated GPU " << gpu_device_id 
              << " for instance " << instanceId << std::endl;
  } else {
    std::cout << "[SubprocessInstanceManager] No GPU available for instance " 
              << instanceId << " - will use CPU" << std::endl;
  }

  // Spawn worker process with GPU device ID
  if (!supervisor_->spawnWorker(instanceId, config, gpu_device_id)) {
    // Release GPU allocation if worker spawn failed
    if (gpu_allocation) {
      resourceManager.releaseGPU(gpu_allocation);
      std::lock_guard<std::mutex> lock(gpu_allocations_mutex_);
      gpu_allocations_.erase(instanceId);
    }
    return false;
  }
  
  return true;
}
