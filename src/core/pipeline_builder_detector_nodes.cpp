#include "core/pipeline_builder_detector_nodes.h"
#include "core/pipeline_builder_model_resolver.h"
#include "config/system_config.h"
#include "core/cvedix_validator.h"
#include "core/platform_detector.h"
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <algorithm>
#include <cctype>
#include <cstring>
#include <opencv2/dnn.hpp>
#ifdef CVEDIX_USE_SFACE_FEATURE_ENCODER
#include <cvedix/nodes/infers/cvedix_sface_feature_encoder_node.h>
#else
#include <cvedix/nodes/infers/cvedix_feature_encoder_node.h>
#endif
#include <cvedix/nodes/infers/cvedix_face_detector_node.h>
#include <cvedix/nodes/infers/cvedix_yolo_detector_node.h>
#include <cvedix/nodes/infers/cvedix_classifier_node.h>
#include <cvedix/nodes/infers/cvedix_enet_seg_node.h>
#include <cvedix/nodes/infers/cvedix_mask_rcnn_detector_node.h>
#ifdef CVEDIX_HAS_OPENPOSE
#include <cvedix/nodes/infers/cvedix_openpose_detector_node.h>
#endif
#ifdef CVEDIX_HAS_FACE_SWAP
#include <cvedix/nodes/infers/cvedix_face_swap_node.h>
#endif
#ifdef CVEDIX_HAS_FACENET
#include <cvedix/nodes/infers/cvedix_facenet_node.h>
#endif
#include <cvedix/nodes/infers/cvedix_lane_detector_node.h>
#ifdef CVEDIX_HAS_RESTORATION
#include <cvedix/nodes/infers/cvedix_restoration_node.h>
#endif
// YOLOv11 ONNX nodes
// Note: This header may not be available in all SDK versions
// If compilation fails, this node will not be available
// #include <cvedix/nodes/infers/cvedix_yolov11_plate_detector_node.h>

// TensorRT Inference Nodes
#ifdef CVEDIX_WITH_TRT
#include <cvedix/nodes/infers/cvedix_trt_vehicle_color_classifier.h>
#include <cvedix/nodes/infers/cvedix_trt_vehicle_detector.h>
#include <cvedix/nodes/infers/cvedix_trt_vehicle_feature_encoder.h>
#include <cvedix/nodes/infers/cvedix_trt_vehicle_plate_detector.h>
#include <cvedix/nodes/infers/cvedix_trt_vehicle_plate_detector_v2.h>
#include <cvedix/nodes/infers/cvedix_trt_vehicle_scanner.h>
#include <cvedix/nodes/infers/cvedix_trt_vehicle_type_classifier.h>
#include <cvedix/nodes/infers/cvedix_trt_yolov8_classifier.h>
#include <cvedix/nodes/infers/cvedix_trt_yolov8_detector.h>
#include <cvedix/nodes/infers/cvedix_trt_yolov8_pose_detector.h>
#include <cvedix/nodes/infers/cvedix_trt_yolov8_seg_detector.h>
#include <cvedix/nodes/infers/cvedix_trt_insight_face_recognition_node.h>
// YOLOv11 TensorRT face (plate uses plugin-based cvedix_yolo_detector_node)
#include <cvedix/nodes/infers/cvedix_trt_yolov11_face_detector_node.h>
#endif

// RKNN Inference Nodes
#ifdef CVEDIX_WITH_RKNN
#include <cvedix/nodes/infers/cvedix_rknn_face_detector_node.h>
#include <cvedix/nodes/infers/cvedix_rknn_yolov11_detector_node.h>
#include <cvedix/nodes/infers/cvedix_rknn_yolov8_detector_node.h>
#endif

#ifdef CVEDIX_WITH_LLM
#include <cvedix/nodes/infers/cvedix_mllm_analyser_node.h>
#endif

#ifdef CVEDIX_WITH_PADDLE
#include <cvedix/nodes/infers/cvedix_ppocr_text_detector_node.h>
#endif

namespace fs = std::filesystem;

// Helper function to log GPU availability
void PipelineBuilderDetectorNodes::logGPUAvailability() {
  std::cerr << "[PipelineBuilderDetectorNodes] ========================================"
            << std::endl;
  std::cerr << "[PipelineBuilderDetectorNodes] Checking GPU availability for inference..."
            << std::endl;

  bool hasGPU = false;

  // Check NVIDIA GPU
  if (PlatformDetector::isNVIDIA()) {
    std::cerr << "[PipelineBuilderDetectorNodes] ✓ NVIDIA GPU detected" << std::endl;
    std::cerr << "[PipelineBuilderDetectorNodes]   → TensorRT devices (tensorrt.1, "
                 "tensorrt.2) may be available"
              << std::endl;
    hasGPU = true;
  }

  // Check Intel GPU
  if (PlatformDetector::isVAAPI()) {
    std::cerr << "[PipelineBuilderDetectorNodes] ✓ Intel GPU (VAAPI) detected" << std::endl;
    hasGPU = true;
  }

  if (PlatformDetector::isMSDK()) {
    std::cerr << "[PipelineBuilderDetectorNodes] ✓ Intel GPU (MSDK) detected" << std::endl;
    hasGPU = true;
  }

  // Check Jetson
  if (PlatformDetector::isJetson()) {
    std::cerr << "[PipelineBuilderDetectorNodes] ✓ NVIDIA Jetson detected" << std::endl;
    std::cerr << "[PipelineBuilderDetectorNodes]   → TensorRT devices may be available"
              << std::endl;
    hasGPU = true;
  }

  if (!hasGPU) {
    std::cerr << "[PipelineBuilderDetectorNodes] ⚠ No GPU detected - inference will use CPU"
              << std::endl;
    std::cerr << "[PipelineBuilderDetectorNodes]   → CPU inference is slower and may cause "
                 "queue overflow"
              << std::endl;
    std::cerr << "[PipelineBuilderDetectorNodes]   → Consider using frame dropping (already "
                 "enabled) or reducing FPS"
              << std::endl;
  } else {
    std::cerr << "[PipelineBuilderDetectorNodes] ✓ GPU detected - check config.json "
                 "auto_device_list to ensure GPU is prioritized"
              << std::endl;
    std::cerr << "[PipelineBuilderDetectorNodes]   → GPU devices should be listed before "
                 "CPU in auto_device_list"
              << std::endl;
  }

  // Check auto_device_list configuration
  try {
    auto &systemConfig = SystemConfig::getInstance();
    auto deviceList = systemConfig.getAutoDeviceList();
    if (!deviceList.empty()) {
      std::cerr << "[PipelineBuilderDetectorNodes] Current auto_device_list (first 5): ";
      size_t count = 0;
      for (const auto &device : deviceList) {
        if (count++ < 5) {
          std::cerr << device << " ";
        }
      }
      std::cerr << "..." << std::endl;
      std::cerr << "[PipelineBuilderDetectorNodes] TIP: Ensure GPU devices (openvino.GPU, "
                   "tensorrt.1, etc.) are listed before CPU"
                << std::endl;
    }
  } catch (...) {
    // Ignore errors
  }

  std::cerr << "[PipelineBuilderDetectorNodes] ========================================"
            << std::endl;
}

// CPU device names (used for both CPU-first and GPU-first reordering)
static const std::vector<std::string> &getCPUDeviceNames() {
  static const std::vector<std::string> cpuDevices = {
      "openvino.CPU", "armnn.CpuAcc", "armnn.CpuRef", "memx.cpu"};
  return cpuDevices;
}

static bool isCPUDevice(const std::string &device) {
  const auto &cpuDevices = getCPUDeviceNames();
  return std::find(cpuDevices.begin(), cpuDevices.end(), device) != cpuDevices.end();
}

// Helper function to ensure CPU devices are prioritized in auto_device_list
// This helps prevent CUDA compatibility issues by falling back to CPU
static void ensureCPUFirstInDeviceList() {
  try {
    auto &systemConfig = SystemConfig::getInstance();
    auto deviceList = systemConfig.getAutoDeviceList();

    if (deviceList.empty()) {
      return; // Use default
    }

    bool cpuFirst = false;
    for (const auto &cpuDevice : getCPUDeviceNames()) {
      if (!deviceList.empty() && deviceList[0] == cpuDevice) {
        cpuFirst = true;
        break;
      }
    }

    const char *force_cpu = std::getenv("FORCE_CPU_INFERENCE");
    if (force_cpu && (std::string(force_cpu) == "1" || std::string(force_cpu) == "true")) {
      if (!cpuFirst) {
        std::cerr << "[PipelineBuilderDetectorNodes] FORCE_CPU_INFERENCE=1 detected - "
                     "prioritizing CPU devices"
                  << std::endl;

        std::vector<std::string> newDeviceList;
        for (const auto &cpuDevice : getCPUDeviceNames()) {
          if (std::find(deviceList.begin(), deviceList.end(), cpuDevice) != deviceList.end()) {
            newDeviceList.push_back(cpuDevice);
          }
        }
        for (const auto &device : deviceList) {
          if (!isCPUDevice(device)) {
            newDeviceList.push_back(device);
          }
        }

        systemConfig.setAutoDeviceList(newDeviceList);
        std::cerr << "[PipelineBuilderDetectorNodes] ✓ Updated auto_device_list to prioritize CPU"
                  << std::endl;
      }
    }
  } catch (...) {
    // Ignore errors - use default config
  }
}

// Helper function to ensure GPU/accelerator devices are prioritized over CPU in auto_device_list
// (default behavior: use GPU first when FORCE_CPU_INFERENCE is not set)
static void ensureGPUFirstInDeviceList() {
  try {
    const char *force_cpu = std::getenv("FORCE_CPU_INFERENCE");
    if (force_cpu && (std::string(force_cpu) == "1" || std::string(force_cpu) == "true")) {
      return; // CPU already prioritized by ensureCPUFirstInDeviceList
    }

    auto &systemConfig = SystemConfig::getInstance();
    auto deviceList = systemConfig.getAutoDeviceList();
    if (deviceList.empty()) {
      return;
    }

    // If first device is already non-CPU, list is already GPU-first
    if (!isCPUDevice(deviceList[0])) {
      return;
    }

    std::cerr << "[PipelineBuilderDetectorNodes] Prioritizing GPU/accelerator over CPU in "
                 "auto_device_list"
              << std::endl;

    std::vector<std::string> gpuFirst;
    for (const auto &device : deviceList) {
      if (!isCPUDevice(device)) {
        gpuFirst.push_back(device);
      }
    }
    for (const auto &device : deviceList) {
      if (isCPUDevice(device)) {
        gpuFirst.push_back(device);
      }
    }

    systemConfig.setAutoDeviceList(gpuFirst);
    std::cerr << "[PipelineBuilderDetectorNodes] ✓ Updated auto_device_list to prioritize GPU"
              << std::endl;
  } catch (...) {
    // Ignore errors - use default config
  }
}

// Helper function to check if error is CUDA-related
static bool isCUDAError(const std::exception &e) {
  std::string errorMsg = e.what();
  std::transform(errorMsg.begin(), errorMsg.end(), errorMsg.begin(), ::tolower);
  
  return errorMsg.find("cuda") != std::string::npos ||
         errorMsg.find("gpu") != std::string::npos ||
         errorMsg.find("tensorrt") != std::string::npos ||
         errorMsg.find("forward compatibility") != std::string::npos ||
         errorMsg.find("managedptr") != std::string::npos;
}



std::shared_ptr<cvedix_nodes::cvedix_node>
PipelineBuilderDetectorNodes::createFaceDetectorNode(
    const std::string &nodeName,
    const std::map<std::string, std::string> &params,
    const CreateInstanceRequest &req) {

  try {
    // Get parameters with defaults - resolve path intelligently
    std::string modelPath = params.count("model_path")
                                ? params.at("model_path")
                                : PipelineBuilderModelResolver::resolveModelPath("models/face/yunet.onnx");
    float scoreThreshold =
        params.count("score_threshold")
            ? static_cast<float>(std::stod(params.at("score_threshold")))
            : static_cast<float>(
                  PipelineBuilderModelResolver::mapDetectionSensitivity(req.detectionSensitivity));
    float nmsThreshold =
        params.count("nms_threshold")
            ? static_cast<float>(std::stod(params.at("nms_threshold")))
            : 0.5f;
    int topK = params.count("top_k") ? std::stoi(params.at("top_k")) : 50;

    // Validate node name
    if (nodeName.empty()) {
      throw std::invalid_argument("Node name cannot be empty");
    }

    // Validate model path
    if (modelPath.empty()) {
      throw std::invalid_argument("Model path cannot be empty");
    }

    // Pre-validate model file access before calling SDK constructor
    // This provides better error messages and fail-fast behavior
    try {
      CVEDIXValidator::preCheckBeforeNodeCreation(modelPath);
    } catch (const std::runtime_error &e) {
      std::cerr << "[PipelineBuilderDetectorNodes] ========================================"
                << std::endl;
      std::cerr << "[PipelineBuilderDetectorNodes] ✗ Pre-validation failed" << std::endl;
      std::cerr << "[PipelineBuilderDetectorNodes] " << e.what() << std::endl;
      std::cerr << "[PipelineBuilderDetectorNodes] ========================================"
                << std::endl;
      throw; // Re-throw to let caller handle
    }

    // Check if model file exists (for informational logging)
    fs::path modelFilePath(modelPath);
    if (!fs::exists(modelFilePath)) {
      std::cerr << "[PipelineBuilderDetectorNodes] ========================================"
                << std::endl;
      std::cerr << "[PipelineBuilderDetectorNodes] WARNING: Model file not found!"
                << std::endl;
      std::cerr << "[PipelineBuilderDetectorNodes] Expected path: " << modelPath
                << std::endl;
      std::cerr << "[PipelineBuilderDetectorNodes] Absolute path: "
                << fs::absolute(modelFilePath).string() << std::endl;
      std::cerr << "[PipelineBuilderDetectorNodes] ========================================"
                << std::endl;
      std::cerr << "[PipelineBuilderDetectorNodes] SOLUTION: Copy your yunet.onnx file to "
                   "one of these locations:"
                << std::endl;
      std::cerr << "[PipelineBuilderDetectorNodes]   1. System-wide location - RECOMMENDED "
                   "(FHS standard):"
                << std::endl;
      std::cerr << "[PipelineBuilderDetectorNodes]      "
                   "/usr/share/cvedix/cvedix_data/models/face/yunet.onnx"
                << std::endl;
      std::cerr << "[PipelineBuilderDetectorNodes]      (Create: sudo mkdir -p "
                   "/usr/share/cvedix/cvedix_data/models/face)"
                << std::endl;
      std::cerr << "[PipelineBuilderDetectorNodes]      (Copy: sudo cp /path/to/yunet.onnx "
                   "/usr/share/cvedix/cvedix_data/models/face/)"
                << std::endl;
      std::cerr << "[PipelineBuilderDetectorNodes]      NOTE: /usr/share/ is for data files "
                   "(FHS standard)"
                << std::endl;
      std::cerr << "[PipelineBuilderDetectorNodes]   1b. Alternative (not recommended, but "
                   "supported):"
                << std::endl;
      std::cerr << "[PipelineBuilderDetectorNodes]      "
                   "/usr/include/cvedix/cvedix_data/models/face/yunet.onnx"
                << std::endl;
      std::cerr << "[PipelineBuilderDetectorNodes]      (Create: sudo mkdir -p "
                   "/usr/include/cvedix/cvedix_data/models/face)"
                << std::endl;
      std::cerr << "[PipelineBuilderDetectorNodes]      NOTE: /usr/include/ is for header "
                   "files, not data files"
                << std::endl;
      std::cerr << "[PipelineBuilderDetectorNodes]   2. SDK source location (relative to "
                   "API directory):"
                << std::endl;
      std::cerr << "[PipelineBuilderDetectorNodes]      "
                   "../edge_ai_sdk/cvedix_data/models/face/yunet.onnx"
                << std::endl;
      std::cerr << "[PipelineBuilderDetectorNodes]      (Copy: cp /path/to/yunet.onnx "
                   "../edge_ai_sdk/cvedix_data/models/face/)"
                << std::endl;
      std::cerr << "[PipelineBuilderDetectorNodes]   3. API working directory: "
                   "./cvedix_data/models/face/yunet.onnx"
                << std::endl;
      std::cerr << "[PipelineBuilderDetectorNodes]      (Create: mkdir -p "
                   "./cvedix_data/models/face)"
                << std::endl;
      std::cerr << "[PipelineBuilderDetectorNodes]   4. Set environment variable "
                   "CVEDIX_DATA_ROOT=/path/to/cvedix_data"
                << std::endl;
      std::cerr << "[PipelineBuilderDetectorNodes]   2. Upload via API: POST "
                   "/v1/core/model/upload"
                << std::endl;
      std::cerr << "[PipelineBuilderDetectorNodes]      Then use MODEL_PATH in "
                   "additionalParams when creating instance"
                << std::endl;
      std::cerr << "[PipelineBuilderDetectorNodes]      Example: additionalParams: "
                   "{\"MODEL_PATH\": \"./models/yunet.onnx\"}"
                << std::endl;
      std::cerr << "[PipelineBuilderDetectorNodes] ========================================"
                << std::endl;
      std::cerr << "[PipelineBuilderDetectorNodes] NOTE: Face detection will NOT work until "
                   "model file is available"
                << std::endl;
      std::cerr << "[PipelineBuilderDetectorNodes] NOTE: Pipeline will continue but face "
                   "detection will fail"
                << std::endl;
      std::cerr << "[PipelineBuilderDetectorNodes] ========================================"
                << std::endl;
    } else {
      std::cerr << "[PipelineBuilderDetectorNodes] ✓ Model file found: "
                << fs::canonical(modelFilePath).string() << std::endl;
      std::cerr << "[PipelineBuilderDetectorNodes]   File size: "
                << fs::file_size(modelFilePath) << " bytes" << std::endl;
    }

    // Validate thresholds
    if (scoreThreshold < 0.0f || scoreThreshold > 1.0f) {
      std::cerr
          << "[PipelineBuilderDetectorNodes] Warning: score_threshold out of range [0,1]: "
          << scoreThreshold << ", clamping to [0,1]" << std::endl;
      scoreThreshold = std::max(0.0f, std::min(1.0f, scoreThreshold));
    }
    if (nmsThreshold < 0.0f || nmsThreshold > 1.0f) {
      std::cerr
          << "[PipelineBuilderDetectorNodes] Warning: nms_threshold out of range [0,1]: "
          << nmsThreshold << ", clamping to [0,1]" << std::endl;
      nmsThreshold = std::max(0.0f, std::min(1.0f, nmsThreshold));
    }
    if (topK < 0) {
      std::cerr << "[PipelineBuilderDetectorNodes] Warning: top_k must be >= 0, got: "
                << topK << ", using default 50" << std::endl;
      topK = 50;
    }

    std::cerr << "[PipelineBuilderDetectorNodes] Creating YuNet face detector node:"
              << std::endl;
    std::cerr << "  Name: '" << nodeName << "'" << std::endl;
    std::cerr << "  Model path: '" << modelPath << "'" << std::endl;
    std::cerr << "  Score threshold: " << scoreThreshold << std::endl;
    std::cerr << "  NMS threshold: " << nmsThreshold << std::endl;
    std::cerr << "  Top K: " << topK << std::endl;

    // Create the YuNet face detector node
    // Log GPU availability before creating node
    PipelineBuilderDetectorNodes::logGPUAvailability();

    // Ensure CPU first only when FORCE_CPU_INFERENCE=1; otherwise prioritize GPU
    ensureCPUFirstInDeviceList();
    ensureGPUFirstInDeviceList();

    std::shared_ptr<cvedix_nodes::cvedix_face_detector_node> node;
    try {
      std::cerr << "[PipelineBuilderDetectorNodes] Calling cvedix_face_detector_node "
                   "constructor..."
                << std::endl;
      std::cerr << "[PipelineBuilderDetectorNodes] NOTE: Device selection is handled by "
                   "CVEDIX SDK based on auto_device_list in config.json"
                << std::endl;
      std::cerr << "[PipelineBuilderDetectorNodes] NOTE: Check CVEDIX SDK logs to see which "
                   "device is selected"
                << std::endl;
      node = std::make_shared<cvedix_nodes::cvedix_face_detector_node>(
          nodeName, modelPath, scoreThreshold, nmsThreshold, topK);
      std::cerr
          << "[PipelineBuilderDetectorNodes] ✓ YuNet face detector node created successfully"
          << std::endl;
      std::cerr
          << "[PipelineBuilderDetectorNodes] TIP: If queue full errors occur, check if "
             "GPU is being used (check nvidia-smi or GPU monitoring)"
          << std::endl;
      if (!fs::exists(modelFilePath)) {
        std::cerr << "[PipelineBuilderDetectorNodes] ⚠ WARNING: Model file was not found, "
                     "node created but may fail during inference"
                  << std::endl;
      }
    } catch (const std::filesystem::filesystem_error &e) {
      // Handle filesystem errors (including permission denied)
      if (e.code() == std::errc::permission_denied) {
        std::cerr << "[PipelineBuilderDetectorNodes] ========================================"
                  << std::endl;
        std::cerr << "[PipelineBuilderDetectorNodes] ✗ Permission denied accessing model file"
                  << std::endl;
        std::cerr << "[PipelineBuilderDetectorNodes] Model path: " << modelPath << std::endl;
        std::cerr << "[PipelineBuilderDetectorNodes] Error: " << e.what() << std::endl;
        std::cerr << "[PipelineBuilderDetectorNodes] ========================================"
                  << std::endl;
        std::cerr << "[PipelineBuilderDetectorNodes] SOLUTION:" << std::endl;
        std::cerr << "[PipelineBuilderDetectorNodes]   1. Check file permissions:" << std::endl;
        std::cerr << "[PipelineBuilderDetectorNodes]      ls -la " << modelPath << std::endl;
        std::cerr << "[PipelineBuilderDetectorNodes]   2. File should be readable (644 or 664)"
                  << std::endl;
        std::cerr << "[PipelineBuilderDetectorNodes]   3. Directory should be traversable (755 or 775)"
                  << std::endl;
        std::cerr << "[PipelineBuilderDetectorNodes]   4. Fix permissions and symlinks:" << std::endl;
        std::cerr << "[PipelineBuilderDetectorNodes]      sudo systemctl restart edgeos-api"
                  << std::endl;
        std::cerr << "[PipelineBuilderDetectorNodes]   5. Restart service:" << std::endl;
        std::cerr << "[PipelineBuilderDetectorNodes]      sudo systemctl restart edgeos-api"
                  << std::endl;
        std::cerr << "[PipelineBuilderDetectorNodes] ========================================"
                  << std::endl;
        throw std::runtime_error("Permission denied accessing model file: " +
                                 modelPath + ". See logs for fix instructions.");
      }
      // Re-throw other filesystem errors
      std::cerr << "[PipelineBuilderDetectorNodes] Filesystem error in constructor: "
                << e.what() << std::endl;
      throw;
    } catch (const std::bad_alloc &e) {
      std::cerr << "[PipelineBuilderDetectorNodes] Memory allocation failed: " << e.what()
                << std::endl;
      throw;
    } catch (const std::exception &e) {
      std::cerr << "[PipelineBuilderDetectorNodes] Standard exception in constructor: "
                << e.what() << std::endl;
      
      // Check if this is a CUDA error and suggest CPU fallback
      if (isCUDAError(e)) {
        std::cerr << "[PipelineBuilderDetectorNodes] ========================================"
                  << std::endl;
        std::cerr << "[PipelineBuilderDetectorNodes] ⚠ CUDA/GPU error detected!"
                  << std::endl;
        std::cerr << "[PipelineBuilderDetectorNodes] Error: " << e.what() << std::endl;
        std::cerr << "[PipelineBuilderDetectorNodes] ========================================"
                  << std::endl;
        std::cerr << "[PipelineBuilderDetectorNodes] SOLUTION:" << std::endl;
        std::cerr << "[PipelineBuilderDetectorNodes]   1. Set environment variable to force CPU:"
                  << std::endl;
        std::cerr << "[PipelineBuilderDetectorNodes]      export FORCE_CPU_INFERENCE=1"
                  << std::endl;
        std::cerr << "[PipelineBuilderDetectorNodes]   2. Or modify config.json to prioritize CPU:"
                  << std::endl;
        std::cerr << "[PipelineBuilderDetectorNodes]      Move 'openvino.CPU' to first in auto_device_list"
                  << std::endl;
        std::cerr << "[PipelineBuilderDetectorNodes]   3. Restart the service"
                  << std::endl;
        std::cerr << "[PipelineBuilderDetectorNodes] ========================================"
                  << std::endl;
      }
      throw;
    } catch (...) {
      std::cerr << "[PipelineBuilderDetectorNodes] Non-standard exception in "
                   "cvedix_face_detector_node constructor"
                << std::endl;
      std::cerr << "[PipelineBuilderDetectorNodes] Parameters were: name='" << nodeName
                << "', model_path='" << modelPath
                << "', score_threshold=" << scoreThreshold
                << ", nms_threshold=" << nmsThreshold << ", top_k=" << topK
                << std::endl;
      throw std::runtime_error("Failed to create YuNet face detector node - "
                               "check parameters and CVEDIX SDK");
    }

    return node;
  } catch (const std::exception &e) {
    std::cerr << "[PipelineBuilderDetectorNodes] Exception in createFaceDetectorNode: "
              << e.what() << std::endl;
    throw;
  } catch (...) {
    std::cerr << "[PipelineBuilderDetectorNodes] Unknown exception in createFaceDetectorNode"
              << std::endl;
    throw std::runtime_error("Unknown error creating face detector node");
  }
}

std::shared_ptr<cvedix_nodes::cvedix_node>
PipelineBuilderDetectorNodes::createSFaceEncoderNode(
    const std::string &nodeName,
    const std::map<std::string, std::string> &params,
    const CreateInstanceRequest &req) {

  try {
    // Get model path - resolve intelligently
    std::string modelPath =
        params.count("model_path")
            ? params.at("model_path")
            : PipelineBuilderModelResolver::resolveModelPath(
                  "models/face/face_recognition_sface_2021dec.onnx");

    // Check MODEL_NAME or MODEL_PATH in additionalParams
    auto modelNameIt = req.additionalParams.find("SFACE_MODEL_NAME");
    if (modelNameIt != req.additionalParams.end() &&
        !modelNameIt->second.empty()) {
      std::string modelName = modelNameIt->second;
      std::string category = "face";
      size_t colonPos = modelName.find(':');
      if (colonPos != std::string::npos) {
        category = modelName.substr(0, colonPos);
        modelName = modelName.substr(colonPos + 1);
      }
      std::string resolvedPath = PipelineBuilderModelResolver::resolveModelByName(modelName, category);
      if (!resolvedPath.empty()) {
        modelPath = resolvedPath;
      }
    } else {
      auto it = req.additionalParams.find("SFACE_MODEL_PATH");
      if (it != req.additionalParams.end() && !it->second.empty()) {
        modelPath = it->second;
      }
    }

    // Validate parameters
    if (nodeName.empty()) {
      throw std::invalid_argument("Node name cannot be empty");
    }
    if (modelPath.empty()) {
      throw std::invalid_argument("Model path cannot be empty");
    }

    std::cerr << "[PipelineBuilderDetectorNodes] Creating SFace encoder node:" << std::endl;
    std::cerr << "  Name: '" << nodeName << "'" << std::endl;
    std::cerr << "  Model path: '" << modelPath << "'" << std::endl;

#ifdef CVEDIX_USE_SFACE_FEATURE_ENCODER
    auto node =
        std::make_shared<cvedix_nodes::cvedix_sface_feature_encoder_node>(
            nodeName, modelPath);
    std::cerr << "[PipelineBuilderDetectorNodes] ✓ SFace encoder node created successfully"
              << std::endl;
    return node;
#else
    throw std::runtime_error(
        "sface_feature_encoder is not available: this CVEDIX/EdgeOS SDK does "
        "not include cvedix_sface_feature_encoder_node.h (only an abstract "
        "cvedix_feature_encoder_node base is present). Use a full CVEDIX SDK "
        "build or trt_vehicle_feature_encoder where applicable.");
#endif
  } catch (const std::exception &e) {
    std::cerr << "[PipelineBuilderDetectorNodes] Exception in createSFaceEncoderNode: "
              << e.what() << std::endl;
    throw;
  }
}

#ifdef CVEDIX_WITH_TRT
std::shared_ptr<cvedix_nodes::cvedix_node>
PipelineBuilderDetectorNodes::createTRTYOLOv8DetectorNode(
    const std::string &nodeName,
    const std::map<std::string, std::string> &params,
    const CreateInstanceRequest &req) {

  try {
    std::string modelPath =
        params.count("model_path") ? params.at("model_path") : "";
    std::string labelsPath =
        params.count("labels_path") ? params.at("labels_path") : "";

    // Get from additionalParams if not in params
    if (modelPath.empty()) {
      auto it = req.additionalParams.find("MODEL_PATH");
      if (it != req.additionalParams.end() && !it->second.empty()) {
        modelPath = it->second;
      }
    }
    if (labelsPath.empty()) {
      auto it = req.additionalParams.find("LABELS_PATH");
      if (it != req.additionalParams.end() && !it->second.empty()) {
        labelsPath = it->second;
      }
    }

    if (nodeName.empty()) {
      throw std::invalid_argument("Node name cannot be empty");
    }
    if (modelPath.empty()) {
      throw std::invalid_argument(
          "Model path cannot be empty for TRT YOLOv8 detector");
    }

    std::cerr << "[PipelineBuilderDetectorNodes] Creating TRT YOLOv8 detector node:"
              << std::endl;
    std::cerr << "  Name: '" << nodeName << "'" << std::endl;
    std::cerr << "  Model path: '" << modelPath << "'" << std::endl;
    if (!labelsPath.empty()) {
      std::cerr << "  Labels path: '" << labelsPath << "'" << std::endl;
    }

    auto node = std::make_shared<cvedix_nodes::cvedix_trt_yolov8_detector>(
        nodeName, modelPath, labelsPath);

    std::cerr
        << "[PipelineBuilderDetectorNodes] ✓ TRT YOLOv8 detector node created successfully"
        << std::endl;
    return node;
  } catch (const std::exception &e) {
    std::cerr << "[PipelineBuilderDetectorNodes] Exception in createTRTYOLOv8DetectorNode: "
              << e.what() << std::endl;
    throw;
  }
}

std::shared_ptr<cvedix_nodes::cvedix_node>
PipelineBuilderDetectorNodes::createTRTYOLOv8SegDetectorNode(
    const std::string &nodeName,
    const std::map<std::string, std::string> &params,
    const CreateInstanceRequest &req) {

  try {
    std::string modelPath =
        params.count("model_path") ? params.at("model_path") : "";
    std::string labelsPath =
        params.count("labels_path") ? params.at("labels_path") : "";

    if (modelPath.empty()) {
      auto it = req.additionalParams.find("MODEL_PATH");
      if (it != req.additionalParams.end() && !it->second.empty()) {
        modelPath = it->second;
      }
    }
    if (labelsPath.empty()) {
      auto it = req.additionalParams.find("LABELS_PATH");
      if (it != req.additionalParams.end() && !it->second.empty()) {
        labelsPath = it->second;
      }
    }

    if (nodeName.empty() || modelPath.empty()) {
      throw std::invalid_argument("Node name and model path are required");
    }

    std::cerr
        << "[PipelineBuilderDetectorNodes] Creating TRT YOLOv8 segmentation detector node:"
        << std::endl;
    std::cerr << "  Name: '" << nodeName << "'" << std::endl;
    std::cerr << "  Model path: '" << modelPath << "'" << std::endl;

    auto node = std::make_shared<cvedix_nodes::cvedix_trt_yolov8_seg_detector>(
        nodeName, modelPath, labelsPath);

    std::cerr << "[PipelineBuilderDetectorNodes] ✓ TRT YOLOv8 segmentation detector node "
                 "created successfully"
              << std::endl;
    return node;
  } catch (const std::exception &e) {
    std::cerr
        << "[PipelineBuilderDetectorNodes] Exception in createTRTYOLOv8SegDetectorNode: "
        << e.what() << std::endl;
    throw;
  }
}

std::shared_ptr<cvedix_nodes::cvedix_node>
PipelineBuilderDetectorNodes::createTRTYOLOv8PoseDetectorNode(
    const std::string &nodeName,
    const std::map<std::string, std::string> &params,
    const CreateInstanceRequest &req) {

  try {
    std::string modelPath =
        params.count("model_path") ? params.at("model_path") : "";
    if (modelPath.empty()) {
      auto it = req.additionalParams.find("MODEL_PATH");
      if (it != req.additionalParams.end() && !it->second.empty()) {
        modelPath = it->second;
      }
    }

    if (nodeName.empty() || modelPath.empty()) {
      throw std::invalid_argument("Node name and model path are required");
    }

    std::cerr << "[PipelineBuilderDetectorNodes] Creating TRT YOLOv8 pose detector node:"
              << std::endl;
    std::cerr << "  Name: '" << nodeName << "'" << std::endl;
    std::cerr << "  Model path: '" << modelPath << "'" << std::endl;

    auto node = std::make_shared<cvedix_nodes::cvedix_trt_yolov8_pose_detector>(
        nodeName, modelPath);

    std::cerr << "[PipelineBuilderDetectorNodes] ✓ TRT YOLOv8 pose detector node created "
                 "successfully"
              << std::endl;
    return node;
  } catch (const std::exception &e) {
    std::cerr
        << "[PipelineBuilderDetectorNodes] Exception in createTRTYOLOv8PoseDetectorNode: "
        << e.what() << std::endl;
    throw;
  }
}

std::shared_ptr<cvedix_nodes::cvedix_node>
PipelineBuilderDetectorNodes::createTRTYOLOv8ClassifierNode(
    const std::string &nodeName,
    const std::map<std::string, std::string> &params,
    const CreateInstanceRequest &req) {

  try {
    std::string modelPath =
        params.count("model_path") ? params.at("model_path") : "";
    std::string labelsPath =
        params.count("labels_path") ? params.at("labels_path") : "";

    // Parse class_ids_applied_to from params (comma-separated)
    std::vector<int> classIdsAppliedTo;
    if (params.count("class_ids_applied_to")) {
      std::string idsStr = params.at("class_ids_applied_to");
      std::istringstream iss(idsStr);
      std::string token;
      while (std::getline(iss, token, ',')) {
        try {
          classIdsAppliedTo.push_back(std::stoi(token));
        } catch (...) {
          // Ignore invalid tokens
        }
      }
    }

    int minWidth = params.count("min_width_applied_to")
                       ? std::stoi(params.at("min_width_applied_to"))
                       : 0;
    int minHeight = params.count("min_height_applied_to")
                        ? std::stoi(params.at("min_height_applied_to"))
                        : 0;

    if (modelPath.empty()) {
      auto it = req.additionalParams.find("MODEL_PATH");
      if (it != req.additionalParams.end() && !it->second.empty()) {
        modelPath = it->second;
      }
    }

    if (nodeName.empty() || modelPath.empty()) {
      throw std::invalid_argument("Node name and model path are required");
    }

    std::cerr << "[PipelineBuilderDetectorNodes] Creating TRT YOLOv8 classifier node:"
              << std::endl;
    std::cerr << "  Name: '" << nodeName << "'" << std::endl;
    std::cerr << "  Model path: '" << modelPath << "'" << std::endl;

    auto node = std::make_shared<cvedix_nodes::cvedix_trt_yolov8_classifier>(
        nodeName, modelPath, labelsPath, classIdsAppliedTo, minWidth,
        minHeight);

    std::cerr
        << "[PipelineBuilderDetectorNodes] ✓ TRT YOLOv8 classifier node created successfully"
        << std::endl;
    return node;
  } catch (const std::exception &e) {
    std::cerr
        << "[PipelineBuilderDetectorNodes] Exception in createTRTYOLOv8ClassifierNode: "
        << e.what() << std::endl;
    throw;
  }
}

std::shared_ptr<cvedix_nodes::cvedix_node>
PipelineBuilderDetectorNodes::createTRTVehicleDetectorNode(
    const std::string &nodeName,
    const std::map<std::string, std::string> &params,
    const CreateInstanceRequest &req) {

  try {
    std::string modelPath =
        params.count("model_path") ? params.at("model_path") : "";
    if (modelPath.empty()) {
      auto it = req.additionalParams.find("MODEL_PATH");
      if (it != req.additionalParams.end() && !it->second.empty()) {
        modelPath = it->second;
      }
    }

    if (nodeName.empty() || modelPath.empty()) {
      throw std::invalid_argument("Node name and model path are required");
    }

    std::cerr << "[PipelineBuilderDetectorNodes] Creating TRT vehicle detector node:"
              << std::endl;
    std::cerr << "  Name: '" << nodeName << "'" << std::endl;
    std::cerr << "  Model path: '" << modelPath << "'" << std::endl;

    auto node = std::make_shared<cvedix_nodes::cvedix_trt_vehicle_detector>(
        nodeName, modelPath);

    std::cerr
        << "[PipelineBuilderDetectorNodes] ✓ TRT vehicle detector node created successfully"
        << std::endl;
    return node;
  } catch (const std::exception &e) {
    std::cerr << "[PipelineBuilderDetectorNodes] Exception in createTRTVehicleDetectorNode: "
              << e.what() << std::endl;
    throw;
  }
}

std::shared_ptr<cvedix_nodes::cvedix_node>
PipelineBuilderDetectorNodes::createTRTVehiclePlateDetectorNode(
    const std::string &nodeName,
    const std::map<std::string, std::string> &params,
    const CreateInstanceRequest &req) {

  try {
    std::string detModelPath =
        params.count("det_model_path") ? params.at("det_model_path") : "";
    std::string recModelPath =
        params.count("rec_model_path") ? params.at("rec_model_path") : "";

    if (detModelPath.empty()) {
      auto it = req.additionalParams.find("DET_MODEL_PATH");
      if (it != req.additionalParams.end() && !it->second.empty()) {
        detModelPath = it->second;
      }
    }
    if (recModelPath.empty()) {
      auto it = req.additionalParams.find("REC_MODEL_PATH");
      if (it != req.additionalParams.end() && !it->second.empty()) {
        recModelPath = it->second;
      }
    }

    if (nodeName.empty() || detModelPath.empty()) {
      throw std::invalid_argument(
          "Node name and detection model path are required");
    }

    std::cerr << "[PipelineBuilderDetectorNodes] Creating TRT vehicle plate detector node:"
              << std::endl;
    std::cerr << "  Name: '" << nodeName << "'" << std::endl;
    std::cerr << "  Detection model path: '" << detModelPath << "'"
              << std::endl;
    if (!recModelPath.empty()) {
      std::cerr << "  Recognition model path: '" << recModelPath << "'"
                << std::endl;
    }

    auto node =
        std::make_shared<cvedix_nodes::cvedix_trt_vehicle_plate_detector>(
            nodeName, detModelPath, recModelPath);

    std::cerr << "[PipelineBuilderDetectorNodes] ✓ TRT vehicle plate detector node created "
                 "successfully"
              << std::endl;
    return node;
  } catch (const std::exception &e) {
    std::cerr
        << "[PipelineBuilderDetectorNodes] Exception in createTRTVehiclePlateDetectorNode: "
        << e.what() << std::endl;
    throw;
  }
}

std::shared_ptr<cvedix_nodes::cvedix_node>
PipelineBuilderDetectorNodes::createTRTVehiclePlateDetectorV2Node(
    const std::string &nodeName,
    const std::map<std::string, std::string> &params,
    const CreateInstanceRequest &req) {

  try {
    std::string detModelPath =
        params.count("det_model_path") ? params.at("det_model_path") : "";
    std::string recModelPath =
        params.count("rec_model_path") ? params.at("rec_model_path") : "";

    if (detModelPath.empty()) {
      auto it = req.additionalParams.find("DET_MODEL_PATH");
      if (it != req.additionalParams.end() && !it->second.empty()) {
        detModelPath = it->second;
      }
    }
    if (recModelPath.empty()) {
      auto it = req.additionalParams.find("REC_MODEL_PATH");
      if (it != req.additionalParams.end() && !it->second.empty()) {
        recModelPath = it->second;
      }
    }

    if (nodeName.empty() || detModelPath.empty()) {
      throw std::invalid_argument(
          "Node name and detection model path are required");
    }

    std::cerr
        << "[PipelineBuilderDetectorNodes] Creating TRT vehicle plate detector v2 node:"
        << std::endl;
    std::cerr << "  Name: '" << nodeName << "'" << std::endl;
    std::cerr << "  Detection model path: '" << detModelPath << "'"
              << std::endl;

    auto node =
        std::make_shared<cvedix_nodes::cvedix_trt_vehicle_plate_detector_v2>(
            nodeName, detModelPath, recModelPath);

    std::cerr << "[PipelineBuilderDetectorNodes] ✓ TRT vehicle plate detector v2 node "
                 "created successfully"
              << std::endl;
    return node;
  } catch (const std::exception &e) {
    std::cerr << "[PipelineBuilderDetectorNodes] Exception in "
                 "createTRTVehiclePlateDetectorV2Node: "
              << e.what() << std::endl;
    throw;
  }
}

std::shared_ptr<cvedix_nodes::cvedix_node>
PipelineBuilderDetectorNodes::createTRTVehicleColorClassifierNode(
    const std::string &nodeName,
    const std::map<std::string, std::string> &params,
    const CreateInstanceRequest &req) {

  try {
    std::string modelPath =
        params.count("model_path") ? params.at("model_path") : "";

    // Parse class_ids_applied_to
    std::vector<int> classIdsAppliedTo;
    if (params.count("class_ids_applied_to")) {
      std::string idsStr = params.at("class_ids_applied_to");
      std::istringstream iss(idsStr);
      std::string token;
      while (std::getline(iss, token, ',')) {
        try {
          classIdsAppliedTo.push_back(std::stoi(token));
        } catch (...) {
        }
      }
    }

    int minWidth = params.count("min_width_applied_to")
                       ? std::stoi(params.at("min_width_applied_to"))
                       : 0;
    int minHeight = params.count("min_height_applied_to")
                        ? std::stoi(params.at("min_height_applied_to"))
                        : 0;

    if (modelPath.empty()) {
      auto it = req.additionalParams.find("MODEL_PATH");
      if (it != req.additionalParams.end() && !it->second.empty()) {
        modelPath = it->second;
      }
    }

    if (nodeName.empty() || modelPath.empty()) {
      throw std::invalid_argument("Node name and model path are required");
    }

    std::cerr << "[PipelineBuilderDetectorNodes] Creating TRT vehicle color classifier node:"
              << std::endl;
    std::cerr << "  Name: '" << nodeName << "'" << std::endl;
    std::cerr << "  Model path: '" << modelPath << "'" << std::endl;

    auto node =
        std::make_shared<cvedix_nodes::cvedix_trt_vehicle_color_classifier>(
            nodeName, modelPath, classIdsAppliedTo, minWidth, minHeight);

    std::cerr << "[PipelineBuilderDetectorNodes] ✓ TRT vehicle color classifier node "
                 "created successfully"
              << std::endl;
    return node;
  } catch (const std::exception &e) {
    std::cerr << "[PipelineBuilderDetectorNodes] Exception in "
                 "createTRTVehicleColorClassifierNode: "
              << e.what() << std::endl;
    throw;
  }
}

std::shared_ptr<cvedix_nodes::cvedix_node>
PipelineBuilderDetectorNodes::createTRTVehicleTypeClassifierNode(
    const std::string &nodeName,
    const std::map<std::string, std::string> &params,
    const CreateInstanceRequest &req) {

  try {
    std::string modelPath =
        params.count("model_path") ? params.at("model_path") : "";

    std::vector<int> classIdsAppliedTo;
    if (params.count("class_ids_applied_to")) {
      std::string idsStr = params.at("class_ids_applied_to");
      std::istringstream iss(idsStr);
      std::string token;
      while (std::getline(iss, token, ',')) {
        try {
          classIdsAppliedTo.push_back(std::stoi(token));
        } catch (...) {
        }
      }
    }

    int minWidth = params.count("min_width_applied_to")
                       ? std::stoi(params.at("min_width_applied_to"))
                       : 0;
    int minHeight = params.count("min_height_applied_to")
                        ? std::stoi(params.at("min_height_applied_to"))
                        : 0;

    if (modelPath.empty()) {
      auto it = req.additionalParams.find("MODEL_PATH");
      if (it != req.additionalParams.end() && !it->second.empty()) {
        modelPath = it->second;
      }
    }

    if (nodeName.empty() || modelPath.empty()) {
      throw std::invalid_argument("Node name and model path are required");
    }

    std::cerr << "[PipelineBuilderDetectorNodes] Creating TRT vehicle type classifier node:"
              << std::endl;
    std::cerr << "  Name: '" << nodeName << "'" << std::endl;
    std::cerr << "  Model path: '" << modelPath << "'" << std::endl;

    auto node =
        std::make_shared<cvedix_nodes::cvedix_trt_vehicle_type_classifier>(
            nodeName, modelPath, classIdsAppliedTo, minWidth, minHeight);

    std::cerr << "[PipelineBuilderDetectorNodes] ✓ TRT vehicle type classifier node created "
                 "successfully"
              << std::endl;
    return node;
  } catch (const std::exception &e) {
    std::cerr
        << "[PipelineBuilderDetectorNodes] Exception in createTRTVehicleTypeClassifierNode: "
        << e.what() << std::endl;
    throw;
  }
}

std::shared_ptr<cvedix_nodes::cvedix_node>
PipelineBuilderDetectorNodes::createTRTVehicleFeatureEncoderNode(
    const std::string &nodeName,
    const std::map<std::string, std::string> &params,
    const CreateInstanceRequest &req) {

  try {
    std::string modelPath =
        params.count("model_path") ? params.at("model_path") : "";

    std::vector<int> classIdsAppliedTo;
    if (params.count("class_ids_applied_to")) {
      std::string idsStr = params.at("class_ids_applied_to");
      std::istringstream iss(idsStr);
      std::string token;
      while (std::getline(iss, token, ',')) {
        try {
          classIdsAppliedTo.push_back(std::stoi(token));
        } catch (...) {
        }
      }
    }

    int minWidth = params.count("min_width_applied_to")
                       ? std::stoi(params.at("min_width_applied_to"))
                       : 0;
    int minHeight = params.count("min_height_applied_to")
                        ? std::stoi(params.at("min_height_applied_to"))
                        : 0;

    if (modelPath.empty()) {
      auto it = req.additionalParams.find("MODEL_PATH");
      if (it != req.additionalParams.end() && !it->second.empty()) {
        modelPath = it->second;
      }
    }

    if (nodeName.empty() || modelPath.empty()) {
      throw std::invalid_argument("Node name and model path are required");
    }

    std::cerr << "[PipelineBuilderDetectorNodes] Creating TRT vehicle feature encoder node:"
              << std::endl;
    std::cerr << "  Name: '" << nodeName << "'" << std::endl;
    std::cerr << "  Model path: '" << modelPath << "'" << std::endl;

    auto node =
        std::make_shared<cvedix_nodes::cvedix_trt_vehicle_feature_encoder>(
            nodeName, modelPath, classIdsAppliedTo, minWidth, minHeight);

    std::cerr << "[PipelineBuilderDetectorNodes] ✓ TRT vehicle feature encoder node created "
                 "successfully"
              << std::endl;
    return node;
  } catch (const std::exception &e) {
    std::cerr
        << "[PipelineBuilderDetectorNodes] Exception in createTRTVehicleFeatureEncoderNode: "
        << e.what() << std::endl;
    throw;
  }
}

std::shared_ptr<cvedix_nodes::cvedix_node>
PipelineBuilderDetectorNodes::createTRTVehicleScannerNode(
    const std::string &nodeName,
    const std::map<std::string, std::string> &params,
    const CreateInstanceRequest &req) {

  try {
    std::string modelPath =
        params.count("model_path") ? params.at("model_path") : "";
    if (modelPath.empty()) {
      auto it = req.additionalParams.find("MODEL_PATH");
      if (it != req.additionalParams.end() && !it->second.empty()) {
        modelPath = it->second;
      }
    }

    if (nodeName.empty() || modelPath.empty()) {
      throw std::invalid_argument("Node name and model path are required");
    }

    std::cerr << "[PipelineBuilderDetectorNodes] Creating TRT vehicle scanner node:"
              << std::endl;
    std::cerr << "  Name: '" << nodeName << "'" << std::endl;
    std::cerr << "  Model path: '" << modelPath << "'" << std::endl;

    auto node = std::make_shared<cvedix_nodes::cvedix_trt_vehicle_scanner>(
        nodeName, modelPath);

    std::cerr
        << "[PipelineBuilderDetectorNodes] ✓ TRT vehicle scanner node created successfully"
        << std::endl;
    return node;
  } catch (const std::exception &e) {
    std::cerr << "[PipelineBuilderDetectorNodes] Exception in createTRTVehicleScannerNode: "
              << e.what() << std::endl;
    throw;
  }
}

std::shared_ptr<cvedix_nodes::cvedix_node>
PipelineBuilderDetectorNodes::createTRTInsightFaceRecognitionNode(
    const std::string &nodeName,
    const std::map<std::string, std::string> &params,
    const CreateInstanceRequest &req) {

  try {
    std::string modelPath =
        params.count("model_path") ? params.at("model_path") : "";

    if (modelPath.empty()) {
      auto it = req.additionalParams.find("MODEL_PATH");
      if (it != req.additionalParams.end() && !it->second.empty()) {
        modelPath = it->second;
      }
    }

    if (nodeName.empty() || modelPath.empty()) {
      throw std::invalid_argument("Node name and model path are required");
    }

    std::cerr
        << "[PipelineBuilderDetectorNodes] Creating TensorRT InsightFace recognition node:"
        << std::endl;
    std::cerr << "  Name: '" << nodeName << "'" << std::endl;
    std::cerr << "  Model path: '" << modelPath << "'" << std::endl;

    auto node = std::make_shared<
        cvedix_nodes::cvedix_trt_insight_face_recognition_node>(nodeName,
                                                                modelPath);

    std::cerr << "[PipelineBuilderDetectorNodes] ✓ TensorRT InsightFace recognition node "
                 "created successfully"
              << std::endl;
    return node;
  } catch (const std::exception &e) {
    std::cerr << "[PipelineBuilderDetectorNodes] Exception in "
                 "createTRTInsightFaceRecognitionNode: "
              << e.what() << std::endl;
    throw;
  }
}

// YOLOv11 TensorRT Face Detector
// Note: Uncomment the include at top of file when SDK supports this node
std::shared_ptr<cvedix_nodes::cvedix_node>
PipelineBuilderDetectorNodes::createTRTYOLOv11FaceDetectorNode(
    const std::string &nodeName,
    const std::map<std::string, std::string> &params,
    const CreateInstanceRequest &req) {

  try {
    std::string modelPath =
        params.count("model_path") ? params.at("model_path") : "";
    float confThreshold = params.count("conf_threshold")
                             ? std::stof(params.at("conf_threshold"))
                             : 0.5f;
    float nmsThreshold = params.count("nms_threshold")
                            ? std::stof(params.at("nms_threshold"))
                            : 0.45f;

    if (modelPath.empty()) {
      auto it = req.additionalParams.find("MODEL_PATH");
      if (it != req.additionalParams.end() && !it->second.empty()) {
        modelPath = it->second;
      }
    }

    if (nodeName.empty() || modelPath.empty()) {
      throw std::invalid_argument("Node name and model path are required");
    }

    std::cerr << "[PipelineBuilderDetectorNodes] Creating TRT YOLOv11 face detector node:"
              << std::endl;
    std::cerr << "  Name: '" << nodeName << "'" << std::endl;
    std::cerr << "  Model path: '" << modelPath << "'" << std::endl;
    std::cerr << "  Confidence threshold: " << confThreshold << std::endl;
    std::cerr << "  NMS threshold: " << nmsThreshold << std::endl;

    auto node = std::make_shared<cvedix_nodes::cvedix_trt_yolov11_face_detector_node>(
        nodeName, modelPath, confThreshold, nmsThreshold);
    std::cerr << "[PipelineBuilderDetectorNodes] ✓ TRT YOLOv11 face detector node created "
                 "successfully"
              << std::endl;
    return node;
  } catch (const std::exception &e) {
    std::cerr << "[PipelineBuilderDetectorNodes] Exception in createTRTYOLOv11FaceDetectorNode: "
              << e.what() << std::endl;
    throw;
  }
}

std::shared_ptr<cvedix_nodes::cvedix_node>
PipelineBuilderDetectorNodes::createTRTYOLOv11PlateDetectorNode(
    const std::string &nodeName,
    const std::map<std::string, std::string> &params,
    const CreateInstanceRequest &req) {

  try {
    std::string modelPath =
        params.count("model_path") ? params.at("model_path") : "";
    float confThreshold = params.count("conf_threshold")
                             ? std::stof(params.at("conf_threshold"))
                             : 0.25f;
    float nmsThreshold = params.count("nms_threshold")
                            ? std::stof(params.at("nms_threshold"))
                            : 0.45f;

    if (modelPath.empty()) {
      auto it = req.additionalParams.find("MODEL_PATH");
      if (it != req.additionalParams.end() && !it->second.empty()) {
        modelPath = it->second;
      }
    }

    if (nodeName.empty() || modelPath.empty()) {
      throw std::invalid_argument("Node name and model path are required");
    }

    std::cerr << "[PipelineBuilderDetectorNodes] Creating TRT YOLOv11 plate detector node "
                 "(plugin yolo_detector / TensorRT):"
              << std::endl;
    std::cerr << "  Name: '" << nodeName << "'" << std::endl;
    std::cerr << "  Model path: '" << modelPath << "'" << std::endl;
    std::cerr << "  Confidence threshold: " << confThreshold << std::endl;
    std::cerr << "  NMS threshold: " << nmsThreshold << std::endl;

#ifdef CVEDIX_YOLO_DETECTOR_PLUGIN_API
    std::string labelsPath =
        params.count("labels_path") ? params.at("labels_path") : "";
    if (labelsPath.empty()) {
      auto lit = req.additionalParams.find("LABELS_PATH");
      if (lit != req.additionalParams.end() && !lit->second.empty()) {
        labelsPath = lit->second;
      }
    }
    auto node = std::make_shared<cvedix_nodes::cvedix_yolo_detector_node>(
        nodeName, modelPath, labelsPath, confThreshold, nmsThreshold, 0,
        cvedix_nodes::BackendType::TENSORRT);
    std::cerr << "[PipelineBuilderDetectorNodes] ✓ TRT plate detector node created "
                 "successfully"
              << std::endl;
    return node;
#else
    throw std::runtime_error(
        "trt_yolov11_plate_detector requires CVEDIX_YOLO_DETECTOR_PLUGIN_API "
        "(plugin-based yolo_detector).");
#endif
  } catch (const std::exception &e) {
    std::cerr << "[PipelineBuilderDetectorNodes] Exception in createTRTYOLOv11PlateDetectorNode: "
              << e.what() << std::endl;
    throw;
  }
}
#endif // CVEDIX_WITH_TRT

// YOLOv11 Plate Detector (ONNX)
// Note: Uncomment the include at top of file when SDK supports this node
std::shared_ptr<cvedix_nodes::cvedix_node>
PipelineBuilderDetectorNodes::createYOLOv11PlateDetectorNode(
    const std::string &nodeName,
    const std::map<std::string, std::string> &params,
    const CreateInstanceRequest &req) {

  try {
    std::string modelPath =
        params.count("model_path") ? params.at("model_path") : "";
    int inputWidth =
        params.count("input_width") ? std::stoi(params.at("input_width")) : 640;
    int inputHeight = params.count("input_height")
                         ? std::stoi(params.at("input_height"))
                         : 640;
    int numClasses =
        params.count("num_classes") ? std::stoi(params.at("num_classes")) : 1;
    float scoreThreshold = params.count("score_threshold")
                              ? std::stof(params.at("score_threshold"))
                              : 0.25f;
    float nmsThreshold = params.count("nms_threshold")
                            ? std::stof(params.at("nms_threshold"))
                            : 0.45f;

    if (modelPath.empty()) {
      auto it = req.additionalParams.find("MODEL_PATH");
      if (it != req.additionalParams.end() && !it->second.empty()) {
        modelPath = it->second;
      }
    }

    if (nodeName.empty() || modelPath.empty()) {
      throw std::invalid_argument("Node name and model path are required");
    }

    std::cerr << "[PipelineBuilderDetectorNodes] Creating YOLOv11 plate detector node "
                 "(plugin yolo_detector / ONNX):"
              << std::endl;
    std::cerr << "  Name: '" << nodeName << "'" << std::endl;
    std::cerr << "  Model path: '" << modelPath << "'" << std::endl;
    std::cerr << "  Input size: " << inputWidth << "x" << inputHeight
              << std::endl;
    std::cerr << "  Num classes: " << numClasses << std::endl;
    std::cerr << "  Score threshold: " << scoreThreshold << std::endl;
    std::cerr << "  NMS threshold: " << nmsThreshold << std::endl;

#ifdef CVEDIX_YOLO_DETECTOR_PLUGIN_API
    (void)numClasses;
    (void)inputWidth;
    (void)inputHeight;
    std::string labelsPath =
        params.count("labels_path") ? params.at("labels_path") : "";
    if (labelsPath.empty()) {
      auto lit = req.additionalParams.find("LABELS_PATH");
      if (lit != req.additionalParams.end() && !lit->second.empty()) {
        labelsPath = lit->second;
      }
    }
    auto node = std::make_shared<cvedix_nodes::cvedix_yolo_detector_node>(
        nodeName, modelPath, labelsPath, scoreThreshold, nmsThreshold, 0,
        cvedix_nodes::BackendType::ONNX);
    std::cerr << "[PipelineBuilderDetectorNodes] ✓ YOLOv11 plate detector node created "
                 "successfully"
              << std::endl;
    return node;
#else
    throw std::runtime_error(
        "yolov11_plate_detector requires CVEDIX_YOLO_DETECTOR_PLUGIN_API "
        "(plugin-based yolo_detector).");
#endif
  } catch (const std::exception &e) {
    std::cerr << "[PipelineBuilderDetectorNodes] Exception in createYOLOv11PlateDetectorNode: "
              << e.what() << std::endl;
    throw;
  }
}

#ifdef CVEDIX_WITH_RKNN
std::shared_ptr<cvedix_nodes::cvedix_node>
PipelineBuilderDetectorNodes::createRKNNYOLOv8DetectorNode(
    const std::string &nodeName,
    const std::map<std::string, std::string> &params,
    const CreateInstanceRequest &req) {

  try {
    std::string modelPath =
        params.count("model_path") ? params.at("model_path") : "";
    float scoreThreshold = params.count("score_threshold")
                               ? std::stof(params.at("score_threshold"))
                               : 0.5f;
    float nmsThreshold = params.count("nms_threshold")
                             ? std::stof(params.at("nms_threshold"))
                             : 0.5f;
    int inputWidth =
        params.count("input_width") ? std::stoi(params.at("input_width")) : 640;
    int inputHeight = params.count("input_height")
                          ? std::stoi(params.at("input_height"))
                          : 640;
    int numClasses =
        params.count("num_classes") ? std::stoi(params.at("num_classes")) : 80;

    if (modelPath.empty()) {
      auto it = req.additionalParams.find("MODEL_PATH");
      if (it != req.additionalParams.end() && !it->second.empty()) {
        modelPath = it->second;
      }
    }

    if (nodeName.empty() || modelPath.empty()) {
      throw std::invalid_argument("Node name and model path are required");
    }

    std::cerr << "[PipelineBuilderDetectorNodes] Creating RKNN YOLOv8 detector node:"
              << std::endl;
    std::cerr << "  Name: '" << nodeName << "'" << std::endl;
    std::cerr << "  Model path: '" << modelPath << "'" << std::endl;
    std::cerr << "  Score threshold: " << scoreThreshold << std::endl;
    std::cerr << "  NMS threshold: " << nmsThreshold << std::endl;
    std::cerr << "  Input size: " << inputWidth << "x" << inputHeight
              << std::endl;
    std::cerr << "  Num classes: " << numClasses << std::endl;

    auto node =
        std::make_shared<cvedix_nodes::cvedix_rknn_yolov8_detector_node>(
            nodeName, modelPath, scoreThreshold, nmsThreshold, inputWidth,
            inputHeight, numClasses);

    std::cerr
        << "[PipelineBuilderDetectorNodes] ✓ RKNN YOLOv8 detector node created successfully"
        << std::endl;
    return node;
  } catch (const std::exception &e) {
    std::cerr << "[PipelineBuilderDetectorNodes] Exception in createRKNNYOLOv8DetectorNode: "
              << e.what() << std::endl;
    throw;
  }
}

std::shared_ptr<cvedix_nodes::cvedix_node>
PipelineBuilderDetectorNodes::createRKNNFaceDetectorNode(
    const std::string &nodeName,
    const std::map<std::string, std::string> &params,
    const CreateInstanceRequest &req) {

  try {
    std::string modelPath =
        params.count("model_path") ? params.at("model_path") : "";
    float scoreThreshold = params.count("score_threshold")
                               ? std::stof(params.at("score_threshold"))
                               : 0.5f;
    float nmsThreshold = params.count("nms_threshold")
                             ? std::stof(params.at("nms_threshold"))
                             : 0.5f;
    int inputWidth =
        params.count("input_width") ? std::stoi(params.at("input_width")) : 640;
    int inputHeight = params.count("input_height")
                          ? std::stoi(params.at("input_height"))
                          : 640;
    int topK = params.count("top_k") ? std::stoi(params.at("top_k")) : 50;

    if (modelPath.empty()) {
      auto it = req.additionalParams.find("MODEL_PATH");
      if (it != req.additionalParams.end() && !it->second.empty()) {
        modelPath = it->second;
      }
    }

    if (nodeName.empty() || modelPath.empty()) {
      throw std::invalid_argument("Node name and model path are required");
    }

    std::cerr << "[PipelineBuilderDetectorNodes] Creating RKNN face detector node:"
              << std::endl;
    std::cerr << "  Name: '" << nodeName << "'" << std::endl;
    std::cerr << "  Model path: '" << modelPath << "'" << std::endl;
    std::cerr << "  Score threshold: " << scoreThreshold << std::endl;
    std::cerr << "  NMS threshold: " << nmsThreshold << std::endl;
    std::cerr << "  Input size: " << inputWidth << "x" << inputHeight
              << std::endl;
    std::cerr << "  Top K: " << topK << std::endl;

    auto node = std::make_shared<cvedix_nodes::cvedix_rknn_face_detector_node>(
        nodeName, modelPath, scoreThreshold, nmsThreshold, inputWidth,
        inputHeight, topK);

    std::cerr
        << "[PipelineBuilderDetectorNodes] ✓ RKNN face detector node created successfully"
        << std::endl;
    return node;
  } catch (const std::exception &e) {
    std::cerr << "[PipelineBuilderDetectorNodes] Exception in createRKNNFaceDetectorNode: "
              << e.what() << std::endl;
    throw;
  }
}

std::shared_ptr<cvedix_nodes::cvedix_node>
PipelineBuilderDetectorNodes::createRKNNYOLOv11DetectorNode(
    const std::string &nodeName,
    const std::map<std::string, std::string> &params,
    const CreateInstanceRequest &req) {

  try {
    std::string modelPath =
        params.count("model_path") ? params.at("model_path") : "";
    float scoreThreshold = params.count("score_threshold")
                               ? std::stof(params.at("score_threshold"))
                               : 0.5f;
    float nmsThreshold = params.count("nms_threshold")
                             ? std::stof(params.at("nms_threshold"))
                             : 0.5f;
    int inputWidth =
        params.count("input_width") ? std::stoi(params.at("input_width")) : 640;
    int inputHeight = params.count("input_height")
                          ? std::stoi(params.at("input_height"))
                          : 640;
    int numClasses =
        params.count("num_classes") ? std::stoi(params.at("num_classes")) : 80;

    if (modelPath.empty()) {
      auto it = req.additionalParams.find("MODEL_PATH");
      if (it != req.additionalParams.end() && !it->second.empty()) {
        modelPath = it->second;
      }
    }

    if (nodeName.empty() || modelPath.empty()) {
      throw std::invalid_argument("Node name and model path are required");
    }

    std::cerr << "[PipelineBuilderDetectorNodes] Creating RKNN YOLOv11 detector node:"
              << std::endl;
    std::cerr << "  Name: '" << nodeName << "'" << std::endl;
    std::cerr << "  Model path: '" << modelPath << "'" << std::endl;
    std::cerr << "  Score threshold: " << scoreThreshold << std::endl;
    std::cerr << "  NMS threshold: " << nmsThreshold << std::endl;
    std::cerr << "  Input size: " << inputWidth << "x" << inputHeight
              << std::endl;
    std::cerr << "  Num classes: " << numClasses << std::endl;

    auto node =
        std::make_shared<cvedix_nodes::cvedix_rknn_yolov11_detector_node>(
            nodeName, modelPath, scoreThreshold, nmsThreshold, inputWidth,
            inputHeight, numClasses);

    std::cerr
        << "[PipelineBuilderDetectorNodes] ✓ RKNN YOLOv11 detector node created successfully"
        << std::endl;
    return node;
  } catch (const std::exception &e) {
    std::cerr
        << "[PipelineBuilderDetectorNodes] Exception in createRKNNYOLOv11DetectorNode: "
        << e.what() << std::endl;
    throw;
  }
}
#endif // CVEDIX_WITH_RKNN

std::shared_ptr<cvedix_nodes::cvedix_node>
PipelineBuilderDetectorNodes::createYOLODetectorNode(
    const std::string &nodeName,
    const std::map<std::string, std::string> &params,
    const CreateInstanceRequest &req) {

  try {
    // Support both naming conventions: weights_path/config_path and
    // model_path/model_config_path
    std::string modelPath = "";
    if (params.count("weights_path")) {
      modelPath = params.at("weights_path");
    } else if (params.count("model_path")) {
      modelPath = params.at("model_path");
    }

    std::string modelConfigPath = "";
    if (params.count("config_path")) {
      modelConfigPath = params.at("config_path");
    } else if (params.count("model_config_path")) {
      modelConfigPath = params.at("model_config_path");
    }

    std::string labelsPath =
        params.count("labels_path") ? params.at("labels_path") : "";
    int inputWidth =
        params.count("input_width") ? std::stoi(params.at("input_width")) : 416;
    int inputHeight = params.count("input_height")
                          ? std::stoi(params.at("input_height"))
                          : 416;
    int batchSize =
        params.count("batch_size") ? std::stoi(params.at("batch_size")) : 1;
    int classIdOffset = params.count("class_id_offset")
                            ? std::stoi(params.at("class_id_offset"))
                            : 0;
    float scoreThreshold = params.count("score_threshold")
                               ? std::stof(params.at("score_threshold"))
                               : 0.5f;
    float confidenceThreshold =
        params.count("confidence_threshold")
            ? std::stof(params.at("confidence_threshold"))
            : 0.5f;
    float nmsThreshold = params.count("nms_threshold")
                             ? std::stof(params.at("nms_threshold"))
                             : 0.5f;

    // Try to get from additionalParams if still empty
    if (modelPath.empty()) {
      auto it = req.additionalParams.find("WEIGHTS_PATH");
      if (it != req.additionalParams.end() && !it->second.empty()) {
        modelPath = it->second;
      } else {
        it = req.additionalParams.find("MODEL_PATH");
        if (it != req.additionalParams.end() && !it->second.empty()) {
          modelPath = it->second;
        }
      }
    }
    if (modelConfigPath.empty()) {
      auto it = req.additionalParams.find("CONFIG_PATH");
      if (it != req.additionalParams.end() && !it->second.empty()) {
        modelConfigPath = it->second;
      }
    }
    if (labelsPath.empty()) {
      auto it = req.additionalParams.find("LABELS_PATH");
      if (it != req.additionalParams.end() && !it->second.empty()) {
        labelsPath = it->second;
      }
    }

    if (nodeName.empty() || modelPath.empty()) {
      throw std::invalid_argument("Node name and model path are required");
    }

    std::cerr << "[PipelineBuilderDetectorNodes] Creating YOLO detector node:" << std::endl;
    std::cerr << "  Name: '" << nodeName << "'" << std::endl;
    std::cerr << "  Model path: '" << modelPath << "'" << std::endl;

    try {
#ifdef CVEDIX_YOLO_DETECTOR_PLUGIN_API
      float confThresh =
          params.count("score_threshold") ? scoreThreshold : confidenceThreshold;
      cvedix_nodes::BackendType backend = cvedix_nodes::BackendType::AUTO;
      if (params.count("backend_type")) {
        std::string bt = params.at("backend_type");
        std::transform(bt.begin(), bt.end(), bt.begin(), ::tolower);
        if (bt == "tensorrt") {
          backend = cvedix_nodes::BackendType::TENSORRT;
        } else if (bt == "openvino") {
          backend = cvedix_nodes::BackendType::OPENVINO;
        } else if (bt == "onnx") {
          backend = cvedix_nodes::BackendType::ONNX;
        }
      }
      if (!modelConfigPath.empty() || inputWidth != 416 || inputHeight != 416 ||
          batchSize != 1) {
        std::cerr
            << "[PipelineBuilderDetectorNodes] Note: plugin-based yolo_detector "
               "ignores config_path, input_width, input_height, batch_size; "
               "the backend uses the model file."
            << std::endl;
      }
      auto node = std::make_shared<cvedix_nodes::cvedix_yolo_detector_node>(
          nodeName, modelPath, labelsPath, confThresh, nmsThreshold, classIdOffset,
          backend);
#else
      auto node = std::make_shared<cvedix_nodes::cvedix_yolo_detector_node>(
          nodeName, modelPath, modelConfigPath, labelsPath, inputWidth,
          inputHeight, batchSize, classIdOffset, scoreThreshold,
          confidenceThreshold, nmsThreshold);
#endif

      std::cerr << "[PipelineBuilderDetectorNodes] ✓ YOLO detector node created successfully"
                << std::endl;
      return node;
    } catch (const cv::Exception& cv_e) {
      // Check if it's a cuDNN-related error
      std::string error_msg = cv_e.what();
      std::transform(error_msg.begin(), error_msg.end(), error_msg.begin(), ::tolower);
      
      if (error_msg.find("cudnn") != std::string::npos ||
          error_msg.find("cuda") != std::string::npos ||
          cv_e.code == cv::Error::GpuApiCallError) {
        std::cerr << "[PipelineBuilderDetectorNodes] ERROR: cuDNN/CUDA error when creating YOLO detector node: "
                  << cv_e.what() << std::endl;
        std::cerr << "[PipelineBuilderDetectorNodes] Error code: " << cv_e.code 
                  << ", Error: " << cv_e.err << std::endl;
        std::cerr << "[PipelineBuilderDetectorNodes] This is likely due to:" << std::endl;
        std::cerr << "  1. cuDNN version mismatch (OpenCV built with different cuDNN version)" << std::endl;
        std::cerr << "  2. CUDA context not properly initialized" << std::endl;
        std::cerr << "[PipelineBuilderDetectorNodes] Suggestion: Set OPENCV_DNN_BACKEND=OPENCV and OPENCV_DNN_TARGET=CPU" << std::endl;
        throw;
      } else {
        // Re-throw other OpenCV exceptions
        throw;
      }
    }
  } catch (const std::exception &e) {
    std::cerr << "[PipelineBuilderDetectorNodes] Exception in createYOLODetectorNode: "
              << e.what() << std::endl;
    throw;
  }
}

std::shared_ptr<cvedix_nodes::cvedix_node>
PipelineBuilderDetectorNodes::createYOLOv11DetectorNode(
    const std::string &nodeName,
    const std::map<std::string, std::string> &params,
    const CreateInstanceRequest &req) {

  // Note: cvedix_yolov11_detector_node is not available in CVEDIX SDK
  // Use rknn_yolov11_detector (with CVEDIX_WITH_RKNN) or yolo_detector instead
  throw std::runtime_error("yolov11_detector node type is not available. "
                           "Please use 'rknn_yolov11_detector' (requires "
                           "CVEDIX_WITH_RKNN) or 'yolo_detector' instead.");
}

std::shared_ptr<cvedix_nodes::cvedix_node> PipelineBuilderDetectorNodes::createENetSegNode(
    const std::string &nodeName,
    const std::map<std::string, std::string> &params,
    const CreateInstanceRequest &req) {

  try {
    std::string modelPath =
        params.count("model_path") ? params.at("model_path") : "";
    std::string modelConfigPath =
        params.count("model_config_path") ? params.at("model_config_path") : "";
    std::string labelsPath =
        params.count("labels_path") ? params.at("labels_path") : "";
    int inputWidth = params.count("input_width")
                         ? std::stoi(params.at("input_width"))
                         : 1024;
    int inputHeight = params.count("input_height")
                          ? std::stoi(params.at("input_height"))
                          : 512;
    int batchSize =
        params.count("batch_size") ? std::stoi(params.at("batch_size")) : 1;
    int classIdOffset = params.count("class_id_offset")
                            ? std::stoi(params.at("class_id_offset"))
                            : 0;

    if (modelPath.empty()) {
      auto it = req.additionalParams.find("MODEL_PATH");
      if (it != req.additionalParams.end() && !it->second.empty()) {
        modelPath = it->second;
      }
    }

    if (nodeName.empty() || modelPath.empty()) {
      throw std::invalid_argument("Node name and model path are required");
    }

    std::cerr << "[PipelineBuilderDetectorNodes] Creating ENet segmentation node:"
              << std::endl;
    std::cerr << "  Name: '" << nodeName << "'" << std::endl;
    std::cerr << "  Model path: '" << modelPath << "'" << std::endl;

    auto node = std::make_shared<cvedix_nodes::cvedix_enet_seg_node>(
        nodeName, modelPath, modelConfigPath, labelsPath, inputWidth,
        inputHeight, batchSize, classIdOffset);

    std::cerr
        << "[PipelineBuilderDetectorNodes] ✓ ENet segmentation node created successfully"
        << std::endl;
    return node;
  } catch (const std::exception &e) {
    std::cerr << "[PipelineBuilderDetectorNodes] Exception in createENetSegNode: "
              << e.what() << std::endl;
    throw;
  }
}

std::shared_ptr<cvedix_nodes::cvedix_node>
PipelineBuilderDetectorNodes::createMaskRCNNDetectorNode(
    const std::string &nodeName,
    const std::map<std::string, std::string> &params,
    const CreateInstanceRequest &req) {

  try {
    std::string modelPath =
        params.count("model_path") ? params.at("model_path") : "";
    std::string modelConfigPath =
        params.count("model_config_path") ? params.at("model_config_path") : "";
    std::string labelsPath =
        params.count("labels_path") ? params.at("labels_path") : "";
    int inputWidth =
        params.count("input_width") ? std::stoi(params.at("input_width")) : 416;
    int inputHeight = params.count("input_height")
                          ? std::stoi(params.at("input_height"))
                          : 416;
    int batchSize =
        params.count("batch_size") ? std::stoi(params.at("batch_size")) : 1;
    int classIdOffset = params.count("class_id_offset")
                            ? std::stoi(params.at("class_id_offset"))
                            : 0;
    float scoreThreshold = params.count("score_threshold")
                               ? std::stof(params.at("score_threshold"))
                               : 0.5f;

    if (modelPath.empty()) {
      auto it = req.additionalParams.find("MODEL_PATH");
      if (it != req.additionalParams.end() && !it->second.empty()) {
        modelPath = it->second;
      }
    }

    if (nodeName.empty() || modelPath.empty()) {
      throw std::invalid_argument("Node name and model path are required");
    }

    std::cerr << "[PipelineBuilderDetectorNodes] Creating Mask RCNN detector node:"
              << std::endl;
    std::cerr << "  Name: '" << nodeName << "'" << std::endl;
    std::cerr << "  Model path: '" << modelPath << "'" << std::endl;

    auto node = std::make_shared<cvedix_nodes::cvedix_mask_rcnn_detector_node>(
        nodeName, modelPath, modelConfigPath, labelsPath, inputWidth,
        inputHeight, batchSize, classIdOffset, scoreThreshold);

    std::cerr
        << "[PipelineBuilderDetectorNodes] ✓ Mask RCNN detector node created successfully"
        << std::endl;
    return node;
  } catch (const std::exception &e) {
    std::cerr << "[PipelineBuilderDetectorNodes] Exception in createMaskRCNNDetectorNode: "
              << e.what() << std::endl;
    throw;
  }
}

#ifdef CVEDIX_HAS_OPENPOSE
std::shared_ptr<cvedix_nodes::cvedix_node>
PipelineBuilderDetectorNodes::createOpenPoseDetectorNode(
    const std::string &nodeName,
    const std::map<std::string, std::string> &params,
    const CreateInstanceRequest &req) {

  try {
    std::string modelPath =
        params.count("model_path") ? params.at("model_path") : "";
    std::string modelConfigPath =
        params.count("model_config_path") ? params.at("model_config_path") : "";
    std::string labelsPath =
        params.count("labels_path") ? params.at("labels_path") : "";
    int inputWidth =
        params.count("input_width") ? std::stoi(params.at("input_width")) : 368;
    int inputHeight = params.count("input_height")
                          ? std::stoi(params.at("input_height"))
                          : 368;
    int batchSize =
        params.count("batch_size") ? std::stoi(params.at("batch_size")) : 1;
    int classIdOffset = params.count("class_id_offset")
                            ? std::stoi(params.at("class_id_offset"))
                            : 0;
    float scoreThreshold = params.count("score_threshold")
                               ? std::stof(params.at("score_threshold"))
                               : 0.1f;

    if (modelPath.empty()) {
      auto it = req.additionalParams.find("MODEL_PATH");
      if (it != req.additionalParams.end() && !it->second.empty()) {
        modelPath = it->second;
      }
    }

    if (nodeName.empty() || modelPath.empty()) {
      throw std::invalid_argument("Node name and model path are required");
    }

    std::cerr << "[PipelineBuilderDetectorNodes] Creating OpenPose detector node:"
              << std::endl;
    std::cerr << "  Name: '" << nodeName << "'" << std::endl;
    std::cerr << "  Model path: '" << modelPath << "'" << std::endl;

    auto node = std::make_shared<cvedix_nodes::cvedix_openpose_detector_node>(
        nodeName, modelPath, modelConfigPath, labelsPath, inputWidth,
        inputHeight, batchSize, classIdOffset, scoreThreshold);

    std::cerr
        << "[PipelineBuilderDetectorNodes] ✓ OpenPose detector node created successfully"
        << std::endl;
    return node;
  } catch (const std::exception &e) {
    std::cerr << "[PipelineBuilderDetectorNodes] Exception in createOpenPoseDetectorNode: "
              << e.what() << std::endl;
    throw;
  }
}
#else
std::shared_ptr<cvedix_nodes::cvedix_node>
PipelineBuilderDetectorNodes::createOpenPoseDetectorNode(
    const std::string &nodeName,
    const std::map<std::string, std::string> &params,
    const CreateInstanceRequest &req) {
  (void)nodeName;
  (void)params;
  (void)req;
  throw std::runtime_error(
      "openpose_detector is not available: this EdgeOS/CVEDIX SDK build does "
      "not ship cvedix_openpose_detector_node.h");
}
#endif

std::shared_ptr<cvedix_nodes::cvedix_node>
PipelineBuilderDetectorNodes::createClassifierNode(
    const std::string &nodeName,
    const std::map<std::string, std::string> &params,
    const CreateInstanceRequest &req) {

  try {
    std::string modelPath =
        params.count("model_path") ? params.at("model_path") : "";
    std::string modelConfigPath =
        params.count("model_config_path") ? params.at("model_config_path") : "";
    std::string labelsPath =
        params.count("labels_path") ? params.at("labels_path") : "";
    int inputWidth =
        params.count("input_width") ? std::stoi(params.at("input_width")) : 128;
    int inputHeight = params.count("input_height")
                          ? std::stoi(params.at("input_height"))
                          : 128;
    int batchSize =
        params.count("batch_size") ? std::stoi(params.at("batch_size")) : 1;

    // Parse class_ids_applied_to
    std::vector<int> classIdsAppliedTo;
    if (params.count("class_ids_applied_to")) {
      std::string idsStr = params.at("class_ids_applied_to");
      std::istringstream iss(idsStr);
      std::string token;
      while (std::getline(iss, token, ',')) {
        try {
          classIdsAppliedTo.push_back(std::stoi(token));
        } catch (...) {
        }
      }
    }

    int minWidth = params.count("min_width_applied_to")
                       ? std::stoi(params.at("min_width_applied_to"))
                       : 0;
    int minHeight = params.count("min_height_applied_to")
                        ? std::stoi(params.at("min_height_applied_to"))
                        : 0;
    int cropPadding = params.count("crop_padding")
                          ? std::stoi(params.at("crop_padding"))
                          : 10;
    bool needSoftmax = params.count("need_softmax")
                           ? (params.at("need_softmax") == "true" ||
                              params.at("need_softmax") == "1")
                           : true;

    if (modelPath.empty()) {
      auto it = req.additionalParams.find("MODEL_PATH");
      if (it != req.additionalParams.end() && !it->second.empty()) {
        modelPath = it->second;
      }
    }

    if (nodeName.empty() || modelPath.empty()) {
      throw std::invalid_argument("Node name and model path are required");
    }

    std::cerr << "[PipelineBuilderDetectorNodes] Creating classifier node:" << std::endl;
    std::cerr << "  Name: '" << nodeName << "'" << std::endl;
    std::cerr << "  Model path: '" << modelPath << "'" << std::endl;

    auto node = std::make_shared<cvedix_nodes::cvedix_classifier_node>(
        nodeName, modelPath, modelConfigPath, labelsPath, inputWidth,
        inputHeight, batchSize, classIdsAppliedTo, minWidth, minHeight,
        cropPadding, needSoftmax);

    std::cerr << "[PipelineBuilderDetectorNodes] ✓ Classifier node created successfully"
              << std::endl;
    return node;
  } catch (const std::exception &e) {
    std::cerr << "[PipelineBuilderDetectorNodes] Exception in createClassifierNode: "
              << e.what() << std::endl;
    throw;
  }
}

std::shared_ptr<cvedix_nodes::cvedix_node>
PipelineBuilderDetectorNodes::createLaneDetectorNode(
    const std::string &nodeName,
    const std::map<std::string, std::string> &params,
    const CreateInstanceRequest &req) {

  try {
    std::string modelPath =
        params.count("model_path") ? params.at("model_path") : "";
    std::string modelConfigPath =
        params.count("model_config_path") ? params.at("model_config_path") : "";
    std::string labelsPath =
        params.count("labels_path") ? params.at("labels_path") : "";
    int inputWidth =
        params.count("input_width") ? std::stoi(params.at("input_width")) : 736;
    int inputHeight = params.count("input_height")
                          ? std::stoi(params.at("input_height"))
                          : 416;
    int batchSize =
        params.count("batch_size") ? std::stoi(params.at("batch_size")) : 1;
    int classIdOffset = params.count("class_id_offset")
                            ? std::stoi(params.at("class_id_offset"))
                            : 0;

    if (modelPath.empty()) {
      auto it = req.additionalParams.find("MODEL_PATH");
      if (it != req.additionalParams.end() && !it->second.empty()) {
        modelPath = it->second;
      }
    }

    if (nodeName.empty() || modelPath.empty()) {
      throw std::invalid_argument("Node name and model path are required");
    }

    std::cerr << "[PipelineBuilderDetectorNodes] Creating lane detector node:" << std::endl;
    std::cerr << "  Name: '" << nodeName << "'" << std::endl;
    std::cerr << "  Model path: '" << modelPath << "'" << std::endl;

    auto node = std::make_shared<cvedix_nodes::cvedix_lane_detector_node>(
        nodeName, modelPath, modelConfigPath, labelsPath, inputWidth,
        inputHeight, batchSize, classIdOffset);

    std::cerr << "[PipelineBuilderDetectorNodes] ✓ Lane detector node created successfully"
              << std::endl;
    return node;
  } catch (const std::exception &e) {
    std::cerr << "[PipelineBuilderDetectorNodes] Exception in createLaneDetectorNode: "
              << e.what() << std::endl;
    throw;
  }
}

#ifdef CVEDIX_WITH_PADDLE
std::shared_ptr<cvedix_nodes::cvedix_node>
PipelineBuilderDetectorNodes::createPaddleOCRTextDetectorNode(
    const std::string &nodeName,
    const std::map<std::string, std::string> &params,
    const CreateInstanceRequest &req) {

  try {
    std::string detModelDir =
        params.count("det_model_dir") ? params.at("det_model_dir") : "";
    std::string clsModelDir =
        params.count("cls_model_dir") ? params.at("cls_model_dir") : "";
    std::string recModelDir =
        params.count("rec_model_dir") ? params.at("rec_model_dir") : "";
    std::string recCharDictPath = params.count("rec_char_dict_path")
                                      ? params.at("rec_char_dict_path")
                                      : "";

    if (nodeName.empty()) {
      throw std::invalid_argument("Node name is required");
    }

    std::cerr << "[PipelineBuilderDetectorNodes] Creating PaddleOCR text detector node:"
              << std::endl;
    std::cerr << "  Name: '" << nodeName << "'" << std::endl;
    if (!detModelDir.empty()) {
      std::cerr << "  Detection model dir: '" << detModelDir << "'"
                << std::endl;
    }
    if (!recModelDir.empty()) {
      std::cerr << "  Recognition model dir: '" << recModelDir << "'"
                << std::endl;
    }

    auto node = std::make_shared<cvedix_nodes::cvedix_ppocr_text_detector_node>(
        nodeName, detModelDir, clsModelDir, recModelDir, recCharDictPath);

    std::cerr << "[PipelineBuilderDetectorNodes] ✓ PaddleOCR text detector node created "
                 "successfully"
              << std::endl;
    return node;
  } catch (const std::exception &e) {
    std::cerr
        << "[PipelineBuilderDetectorNodes] Exception in createPaddleOCRTextDetectorNode: "
        << e.what() << std::endl;
    throw;
  }
}
#endif // CVEDIX_WITH_PADDLE

#ifdef CVEDIX_HAS_RESTORATION
std::shared_ptr<cvedix_nodes::cvedix_node>
PipelineBuilderDetectorNodes::createRestorationNode(
    const std::string &nodeName,
    const std::map<std::string, std::string> &params,
    const CreateInstanceRequest &req) {

  try {
    std::string bgRestorationModel = params.count("bg_restoration_model")
                                         ? params.at("bg_restoration_model")
                                         : "";
    std::string faceRestorationModel = params.count("face_restoration_model")
                                           ? params.at("face_restoration_model")
                                           : "";
    bool restorationToOSD = params.count("restoration_to_osd")
                                ? (params.at("restoration_to_osd") == "true" ||
                                   params.at("restoration_to_osd") == "1")
                                : true;

    if (bgRestorationModel.empty()) {
      auto it = req.additionalParams.find("BG_RESTORATION_MODEL");
      if (it != req.additionalParams.end() && !it->second.empty()) {
        bgRestorationModel = it->second;
      }
    }

    if (nodeName.empty() || bgRestorationModel.empty()) {
      throw std::invalid_argument(
          "Node name and background restoration model path are required");
    }

    std::cerr << "[PipelineBuilderDetectorNodes] Creating restoration node:" << std::endl;
    std::cerr << "  Name: '" << nodeName << "'" << std::endl;
    std::cerr << "  Background restoration model: '" << bgRestorationModel
              << "'" << std::endl;
    if (!faceRestorationModel.empty()) {
      std::cerr << "  Face restoration model: '" << faceRestorationModel << "'"
                << std::endl;
    }

    auto node = std::make_shared<cvedix_nodes::cvedix_restoration_node>(
        nodeName, bgRestorationModel, faceRestorationModel, restorationToOSD);

    std::cerr << "[PipelineBuilderDetectorNodes] ✓ Restoration node created successfully"
              << std::endl;
    return node;
  } catch (const std::exception &e) {
    std::cerr << "[PipelineBuilderDetectorNodes] Exception in createRestorationNode: "
              << e.what() << std::endl;
    throw;
  }
}
#else
std::shared_ptr<cvedix_nodes::cvedix_node>
PipelineBuilderDetectorNodes::createRestorationNode(
    const std::string &nodeName,
    const std::map<std::string, std::string> &params,
    const CreateInstanceRequest &req) {
  (void)nodeName;
  (void)params;
  (void)req;
  throw std::runtime_error(
      "restoration is not available: this EdgeOS/CVEDIX SDK build does not "
      "ship cvedix_restoration_node.h");
}
#endif

#ifdef CVEDIX_HAS_FACE_SWAP
std::shared_ptr<cvedix_nodes::cvedix_node> PipelineBuilderDetectorNodes::createFaceSwapNode(
    const std::string &nodeName,
    const std::map<std::string, std::string> &params,
    const CreateInstanceRequest &req) {

  try {
    std::string yunetFaceDetectModel =
        params.count("yunet_face_detect_model")
            ? params.at("yunet_face_detect_model")
            : "";
    std::string buffaloLFaceEncodingModel =
        params.count("buffalo_l_face_encoding_model")
            ? params.at("buffalo_l_face_encoding_model")
            : "";
    std::string emapFileForEmbeddings =
        params.count("emap_file_for_embeddings")
            ? params.at("emap_file_for_embeddings")
            : "";
    std::string insightfaceSwapModel = params.count("insightface_swap_model")
                                           ? params.at("insightface_swap_model")
                                           : "";
    std::string swapSourceImage =
        params.count("swap_source_image") ? params.at("swap_source_image") : "";
    int swapSourceFaceIndex =
        params.count("swap_source_face_index")
            ? std::stoi(params.at("swap_source_face_index"))
            : 0;
    int minFaceWH = params.count("min_face_w_h")
                        ? std::stoi(params.at("min_face_w_h"))
                        : 50;
    bool swapOnOSD = params.count("swap_on_osd")
                         ? (params.at("swap_on_osd") == "true" ||
                            params.at("swap_on_osd") == "1")
                         : true;
    bool actAsPrimaryDetector =
        params.count("act_as_primary_detector")
            ? (params.at("act_as_primary_detector") == "true" ||
               params.at("act_as_primary_detector") == "1")
            : false;

    // Try to get from additionalParams if still empty
    if (yunetFaceDetectModel.empty()) {
      auto it = req.additionalParams.find("YUNET_FACE_DETECT_MODEL");
      if (it != req.additionalParams.end() && !it->second.empty()) {
        yunetFaceDetectModel = it->second;
      }
    }
    if (buffaloLFaceEncodingModel.empty()) {
      auto it = req.additionalParams.find("BUFFALO_L_FACE_ENCODING_MODEL");
      if (it != req.additionalParams.end() && !it->second.empty()) {
        buffaloLFaceEncodingModel = it->second;
      }
    }
    if (emapFileForEmbeddings.empty()) {
      auto it = req.additionalParams.find("EMAP_FILE_FOR_EMBEDDINGS");
      if (it != req.additionalParams.end() && !it->second.empty()) {
        emapFileForEmbeddings = it->second;
      }
    }
    if (insightfaceSwapModel.empty()) {
      auto it = req.additionalParams.find("INSIGHTFACE_SWAP_MODEL");
      if (it != req.additionalParams.end() && !it->second.empty()) {
        insightfaceSwapModel = it->second;
      }
    }
    if (swapSourceImage.empty()) {
      auto it = req.additionalParams.find("SWAP_SOURCE_IMAGE");
      if (it != req.additionalParams.end() && !it->second.empty()) {
        swapSourceImage = it->second;
      }
    }

    if (nodeName.empty() || yunetFaceDetectModel.empty() ||
        buffaloLFaceEncodingModel.empty() || emapFileForEmbeddings.empty() ||
        insightfaceSwapModel.empty() || swapSourceImage.empty()) {
      throw std::invalid_argument(
          "Node name and all model paths are required for face swap node");
    }

    std::cerr << "[PipelineBuilderDetectorNodes] Creating face swap node:" << std::endl;
    std::cerr << "  Name: '" << nodeName << "'" << std::endl;
    std::cerr << "  YUNet face detect model: '" << yunetFaceDetectModel << "'"
              << std::endl;
    std::cerr << "  Buffalo L face encoding model: '"
              << buffaloLFaceEncodingModel << "'" << std::endl;
    std::cerr << "  EMap file for embeddings: '" << emapFileForEmbeddings << "'"
              << std::endl;
    std::cerr << "  InsightFace swap model: '" << insightfaceSwapModel << "'"
              << std::endl;
    std::cerr << "  Swap source image: '" << swapSourceImage << "'"
              << std::endl;

    auto node = std::make_shared<cvedix_nodes::cvedix_face_swap_node>(
        nodeName, yunetFaceDetectModel, buffaloLFaceEncodingModel,
        emapFileForEmbeddings, insightfaceSwapModel, swapSourceImage,
        swapSourceFaceIndex, minFaceWH, swapOnOSD, actAsPrimaryDetector);

    std::cerr << "[PipelineBuilderDetectorNodes] ✓ Face swap node created successfully"
              << std::endl;
    return node;
  } catch (const std::exception &e) {
    std::cerr << "[PipelineBuilderDetectorNodes] Exception in createFaceSwapNode: "
              << e.what() << std::endl;
    throw;
  }
}
#else
std::shared_ptr<cvedix_nodes::cvedix_node> PipelineBuilderDetectorNodes::createFaceSwapNode(
    const std::string &nodeName,
    const std::map<std::string, std::string> &params,
    const CreateInstanceRequest &req) {
  (void)nodeName;
  (void)params;
  (void)req;
  throw std::runtime_error(
      "face_swap is not available: this EdgeOS/CVEDIX SDK build does not ship "
      "cvedix_face_swap_node.h");
}
#endif

#ifdef CVEDIX_HAS_FACENET
std::shared_ptr<cvedix_nodes::cvedix_node>
PipelineBuilderDetectorNodes::createInsightFaceRecognitionNode(
    const std::string &nodeName,
    const std::map<std::string, std::string> &params,
    const CreateInstanceRequest &req) {

  try {
    std::string modelPath =
        params.count("model_path") ? params.at("model_path") : "";

    if (modelPath.empty()) {
      auto it = req.additionalParams.find("MODEL_PATH");
      if (it != req.additionalParams.end() && !it->second.empty()) {
        modelPath = it->second;
      }
    }

    if (nodeName.empty() || modelPath.empty()) {
      throw std::invalid_argument("Node name and model path are required");
    }

    // SDK may provide cvedix_face_recognition_node (InsightFace, multi-backend) in newer
    // versions. Current libcvedix_core.so only exports cvedix_facenet_node (FaceNet).
    // Use facenet node: (node_name, model_path, input_width, input_height,
    // enable_alignment, pretrained_dataset). Use 112x112 for InsightFace-style models.
    int inputW = 112;
    int inputH = 112;
    if (params.count("input_width")) inputW = std::stoi(params.at("input_width"));
    if (params.count("input_height")) inputH = std::stoi(params.at("input_height"));
    bool enableAlign = true;
    if (params.count("enable_alignment")) {
      std::string v = params.at("enable_alignment");
      enableAlign = (v != "0" && v != "false");
    }

    std::cerr << "[PipelineBuilderDetectorNodes] Creating face recognition node (facenet):"
              << std::endl;
    std::cerr << "  Name: '" << nodeName << "'" << std::endl;
    std::cerr << "  Model path: '" << modelPath << "'" << std::endl;

    auto node = std::make_shared<cvedix_nodes::cvedix_facenet_node>(
        nodeName, modelPath, inputW, inputH, enableAlign, "vggface2");

    std::cerr << "[PipelineBuilderDetectorNodes] ✓ Face recognition node created successfully"
              << std::endl;
    return node;
  } catch (const std::exception &e) {
    std::cerr
        << "[PipelineBuilderDetectorNodes] Exception in createInsightFaceRecognitionNode: "
        << e.what() << std::endl;
    throw;
  }
}
#else
std::shared_ptr<cvedix_nodes::cvedix_node>
PipelineBuilderDetectorNodes::createInsightFaceRecognitionNode(
    const std::string &nodeName,
    const std::map<std::string, std::string> &params,
    const CreateInstanceRequest &req) {
  (void)nodeName;
  (void)params;
  (void)req;
  throw std::runtime_error(
      "insight_face_recognition is not available: this EdgeOS/CVEDIX SDK "
      "build does not ship cvedix_facenet_node.h");
}
#endif

#ifdef CVEDIX_WITH_LLM
std::shared_ptr<cvedix_nodes::cvedix_node>
PipelineBuilderDetectorNodes::createMLLMAnalyserNode(
    const std::string &nodeName,
    const std::map<std::string, std::string> &params,
    const CreateInstanceRequest &req) {
  try {
    std::string modelName =
        params.count("model_name") ? params.at("model_name") : "";
    std::string prompt = params.count("prompt") ? params.at("prompt") : "";
    std::string apiBaseUrl =
        params.count("api_base_url") ? params.at("api_base_url") : "";
    std::string apiKey = params.count("api_key") ? params.at("api_key") : "";

    // Try to get from additionalParams if still empty
    if (modelName.empty()) {
      auto it = req.additionalParams.find("MODEL_NAME");
      if (it != req.additionalParams.end() && !it->second.empty()) {
        modelName = it->second;
      }
    }
    if (prompt.empty()) {
      auto it = req.additionalParams.find("PROMPT");
      if (it != req.additionalParams.end() && !it->second.empty()) {
        prompt = it->second;
      }
    }
    if (apiBaseUrl.empty()) {
      auto it = req.additionalParams.find("API_BASE_URL");
      if (it != req.additionalParams.end() && !it->second.empty()) {
        apiBaseUrl = it->second;
      }
    }
    if (apiKey.empty()) {
      auto it = req.additionalParams.find("API_KEY");
      if (it != req.additionalParams.end() && !it->second.empty()) {
        apiKey = it->second;
      }
    }

    // Parse backend type
    std::string backendTypeStr =
        params.count("backend_type") ? params.at("backend_type") : "ollama";
    llmlib::LLMBackendType backendType = llmlib::LLMBackendType::Ollama;
    if (backendTypeStr == "openai") {
      backendType = llmlib::LLMBackendType::OpenAI;
    } else if (backendTypeStr == "anthropic") {
      backendType = llmlib::LLMBackendType::Anthropic;
    }

    if (nodeName.empty() || modelName.empty() || prompt.empty() ||
        apiBaseUrl.empty()) {
      throw std::invalid_argument(
          "Node name, model name, prompt, and API base URL are required");
    }

    std::cerr << "[PipelineBuilderDetectorNodes] Creating MLLM analyser node:" << std::endl;
    std::cerr << "  Name: '" << nodeName << "'" << std::endl;
    std::cerr << "  Model name: '" << modelName << "'" << std::endl;
    std::cerr << "  Prompt: '" << prompt << "'" << std::endl;
    std::cerr << "  API base URL: '" << apiBaseUrl << "'" << std::endl;

    auto node = std::make_shared<cvedix_nodes::cvedix_mllm_analyser_node>(
        nodeName, modelName, prompt, apiBaseUrl, apiKey, backendType);

    std::cerr << "[PipelineBuilderDetectorNodes] ✓ MLLM analyser node created successfully"
              << std::endl;
    return node;
  } catch (const std::exception &e) {
    std::cerr << "[PipelineBuilderDetectorNodes] Exception in createMLLMAnalyserNode: "
              << e.what() << std::endl;
    throw;
  }
}
#endif // CVEDIX_WITH_LLM