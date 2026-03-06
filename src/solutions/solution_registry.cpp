#include "solutions/solution_registry.h"
#include "core/env_config.h"
#include <algorithm>
#include <iostream>

void SolutionRegistry::registerSolution(const SolutionConfig &config) {
  std::lock_guard<std::mutex> lock(mutex_);

  // Check if solution already exists and is a default solution
  auto it = solutions_.find(config.solutionId);
  if (it != solutions_.end() && it->second.isDefault) {
    // Cannot override default solutions
    std::cerr << "[SolutionRegistry] Warning: Attempted to override default "
                 "solution: "
              << config.solutionId << ". Ignoring registration." << std::endl;
    return;
  }

  solutions_[config.solutionId] = config;
}

std::optional<SolutionConfig>
SolutionRegistry::getSolution(const std::string &solutionId) const {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = solutions_.find(solutionId);
  if (it != solutions_.end()) {
    return it->second;
  }
  return std::nullopt;
}

std::vector<std::string> SolutionRegistry::listSolutions() const {
  std::lock_guard<std::mutex> lock(mutex_);
  std::vector<std::string> result;
  result.reserve(solutions_.size());
  for (const auto &pair : solutions_) {
    result.push_back(pair.first);
  }
  return result;
}

bool SolutionRegistry::hasSolution(const std::string &solutionId) const {
  std::lock_guard<std::mutex> lock(mutex_);
  return solutions_.find(solutionId) != solutions_.end();
}

std::unordered_map<std::string, SolutionConfig>
SolutionRegistry::getAllSolutions() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return solutions_;
}

bool SolutionRegistry::updateSolution(const SolutionConfig &config) {
  std::lock_guard<std::mutex> lock(mutex_);

  auto it = solutions_.find(config.solutionId);
  if (it == solutions_.end()) {
    return false; // Solution doesn't exist
  }

  // Check if it's a default solution - default solutions cannot be updated
  if (it->second.isDefault) {
    return false;
  }

  // Update the solution
  solutions_[config.solutionId] = config;
  return true;
}

bool SolutionRegistry::deleteSolution(const std::string &solutionId) {
  std::lock_guard<std::mutex> lock(mutex_);

  auto it = solutions_.find(solutionId);
  if (it == solutions_.end()) {
    return false; // Solution doesn't exist
  }

  // Check if it's a default solution - default solutions cannot be deleted
  if (it->second.isDefault) {
    return false;
  }

  // Delete the solution
  solutions_.erase(it);
  return true;
}

bool SolutionRegistry::isDefaultSolution(const std::string &solutionId) const {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = solutions_.find(solutionId);
  if (it != solutions_.end()) {
    return it->second.isDefault;
  }
  return false;
}

void SolutionRegistry::initializeDefaultSolutions() {
  registerFaceDetectionSolution();
  registerFaceDetectionRTMPSolution(); // face_detection_rtmp
  registerObjectDetectionSolution();   // Add YOLO-based solution
  registerBACrosslineSolution(); // Add behavior analysis crossline solution
  registerBAStopSolution(); // Add behavior analysis stop detection solution
  registerBAJamSolution(); // Add behavior analysis traffic jam solution

  // Register new node-based solutions
  registerYOLOv11DetectionSolution();
  registerFaceSwapSolution();
  registerInsightFaceRecognitionSolution();
  registerMLLMAnalysisSolution();
  registerMaskRCNNDetectionSolution(); // Add MaskRCNN detection solution
  registerMaskRCNNRTMPSolution();      // Add MaskRCNN with RTMP streaming

  // Register all _default solutions
  registerFaceDetectionFileSolution();          // face_detection_file_default
  registerFaceDetectionDefaultSolution();       // face_detection_default
  registerFaceDetectionRTMPDefaultSolution();   // face_detection_rtmp_default
  registerFaceDetectionRTSPDefaultSolution();   // face_detection_rtsp_default
  registerRTSPFaceDetectionDefaultSolution();   // rtsp_face_detection_default
  registerMinimalDefaultSolution();             // minimal_default
  registerObjectDetectionYOLODefaultSolution(); // object_detection_yolo_default
  registerBACrosslineDefaultSolution();         // ba_crossline_default
  registerBACrosslineMQTTDefaultSolution();     // ba_crossline_mqtt_default
  registerBAJamDefaultSolution();               // ba_jam_default
  registerBAJamMQTTDefaultSolution();           // ba_jam_mqtt_default
  registerBAStopDefaultSolution();              // ba_stop_default
  registerBAStopMQTTDefaultSolution();          // ba_stop_mqtt_default
  registerBALoiteringSolution();                // ba_loitering
  registerBAAreaEnterExitSolution();            // ba_area_enter_exit
  registerBALineCountingSolution();            // ba_line_counting
  registerBACrowdingSolution();                 // ba_crowding
  registerSecuRTSolution();                     // securt

  // Register specialized detection solutions
  registerFireSmokeDetectionSolution();          // fire_smoke_detection
  registerObstacleDetectionSolution();           // obstacle_detection
  registerWrongWayDetectionSolution();           // wrong_way_detection

#ifdef CVEDIX_WITH_RKNN
  registerRKNNYOLOv11DetectionSolution();
#endif

#ifdef CVEDIX_WITH_TRT
  registerTRTInsightFaceRecognitionSolution();
#endif
}

void SolutionRegistry::registerFaceDetectionSolution() {
  SolutionConfig config;
  config.solutionId = "face_detection";
  config.solutionName = "Face Detection";
  config.solutionType = "face_detection";
  config.isDefault = true;

  // File Source Node (supports flexible input: file, RTSP, RTMP, HLS via
  // auto-detection)
  SolutionConfig::NodeConfig fileSrc;
  fileSrc.nodeType = "file_src";
  fileSrc.nodeName = "file_src_{instanceId}";
  // Support both FILE_PATH and RTSP_URL for backward compatibility
  // Pipeline builder will auto-detect input type from FILE_PATH or RTSP_SRC_URL
  fileSrc.parameters["file_path"] =
      "${FILE_PATH}"; // Can be file path or RTSP/RTMP URL
  fileSrc.parameters["channel"] = "0";
  fileSrc.parameters["resize_ratio"] =
      "1.0"; // Use resize_ratio instead of fps (must be > 0 and <= 1.0)
  config.pipeline.push_back(fileSrc);

  // YuNet Face Detector Node
  SolutionConfig::NodeConfig faceDetector;
  faceDetector.nodeType = "yunet_face_detector";
  faceDetector.nodeName = "face_detector_{instanceId}";
  // Use ${MODEL_PATH} placeholder - can be overridden via
  // additionalParams["MODEL_PATH"] in request Default to yunet.onnx if not
  // provided
  faceDetector.parameters["model_path"] = "${MODEL_PATH}";
  faceDetector.parameters["score_threshold"] = "${detectionSensitivity}";
  faceDetector.parameters["nms_threshold"] = "0.5";
  faceDetector.parameters["top_k"] = "50";
  config.pipeline.push_back(faceDetector);

  // File Destination Node
  SolutionConfig::NodeConfig fileDes;
  fileDes.nodeType = "file_des";
  fileDes.nodeName = "file_des_{instanceId}";
  fileDes.parameters["save_dir"] = "./output/{instanceId}";
  fileDes.parameters["name_prefix"] = "face_detection";
  fileDes.parameters["osd"] = "true";
  config.pipeline.push_back(fileDes);

  // Default configurations
  config.defaults["detectorMode"] = "SmartDetection";
  config.defaults["detectionSensitivity"] = "0.7";
  config.defaults["sensorModality"] = "RGB";

  registerSolution(config);
}

void SolutionRegistry::registerFaceDetectionFileSolution() {
  SolutionConfig config;
  config.solutionId = "face_detection_file_default";
  config.solutionName = "Face Detection - File Source";
  config.solutionType = "face_detection";
  config.isDefault = true;

  // File Source Node
  SolutionConfig::NodeConfig fileSrc;
  fileSrc.nodeType = "file_src";
  fileSrc.nodeName = "file_src_{instanceId}";
  fileSrc.parameters["file_path"] = "${FILE_PATH}";
  fileSrc.parameters["channel"] = "0";
  fileSrc.parameters["resize_ratio"] = "${RESIZE_RATIO}";
  config.pipeline.push_back(fileSrc);

  // YuNet Face Detector Node
  SolutionConfig::NodeConfig faceDetector;
  faceDetector.nodeType = "yunet_face_detector";
  faceDetector.nodeName = "face_detector_{instanceId}";
  faceDetector.parameters["model_path"] = "${MODEL_PATH}";
  faceDetector.parameters["score_threshold"] = "${detectionSensitivity}";
  faceDetector.parameters["nms_threshold"] = "0.5";
  faceDetector.parameters["top_k"] = "50";
  config.pipeline.push_back(faceDetector);

  // File Destination Node
  SolutionConfig::NodeConfig fileDes;
  fileDes.nodeType = "file_des";
  fileDes.nodeName = "file_des_{instanceId}";
  fileDes.parameters["save_dir"] = "./output/{instanceId}";
  fileDes.parameters["name_prefix"] = "face_detection";
  fileDes.parameters["osd"] = "true";
  config.pipeline.push_back(fileDes);

  // Default configurations
  config.defaults["detectorMode"] = "SmartDetection";
  config.defaults["detectionSensitivity"] = "0.7";
  config.defaults["sensorModality"] = "RGB";
  config.defaults["RESIZE_RATIO"] = "1.0";

  registerSolution(config);
}

void SolutionRegistry::registerObjectDetectionSolution() {
  SolutionConfig config;
  config.solutionId = "object_detection";
  config.solutionName = "Object Detection (YOLO)";
  config.solutionType = "object_detection";
  config.isDefault = true;

  // File Source Node (supports flexible input: file, RTSP, RTMP, HLS via
  // auto-detection)
  SolutionConfig::NodeConfig fileSrc;
  fileSrc.nodeType = "file_src";
  fileSrc.nodeName = "file_src_{instanceId}";
  // Support both FILE_PATH and RTSP_URL for backward compatibility
  // Pipeline builder will auto-detect input type from FILE_PATH or RTSP_SRC_URL
  fileSrc.parameters["file_path"] =
      "${FILE_PATH}"; // Can be file path or RTSP/RTMP URL
  fileSrc.parameters["channel"] = "0";
  fileSrc.parameters["resize_ratio"] = "1.0";
  config.pipeline.push_back(fileSrc);

  // YOLO Detector Node (commented out - need to implement
  // createYOLODetectorNode) To use YOLO, you need to:
  // 1. Add "yolo_detector" case in PipelineBuilder::createNode()
  // 2. Implement createYOLODetectorNode() in PipelineBuilder
  // 3. Uncomment the code below
  /*
  SolutionConfig::NodeConfig yoloDetector;
  yoloDetector.nodeType = "yolo_detector";
  yoloDetector.nodeName = "yolo_detector_{instanceId}";
  yoloDetector.parameters["weights_path"] = "${MODEL_PATH}";
  yoloDetector.parameters["config_path"] = "${CONFIG_PATH}";
  yoloDetector.parameters["labels_path"] = "${LABELS_PATH}";
  config.pipeline.push_back(yoloDetector);
  */

  // File Destination Node
  SolutionConfig::NodeConfig fileDes;
  fileDes.nodeType = "file_des";
  fileDes.nodeName = "file_des_{instanceId}";
  fileDes.parameters["save_dir"] = "./output/{instanceId}";
  fileDes.parameters["name_prefix"] = "object_detection";
  fileDes.parameters["osd"] = "true";
  config.pipeline.push_back(fileDes);

  // Default configurations
  config.defaults["detectorMode"] = "SmartDetection";
  config.defaults["detectionSensitivity"] = "0.7";
  config.defaults["sensorModality"] = "RGB";

  registerSolution(config);
}

void SolutionRegistry::registerFaceDetectionRTMPSolution() {
  SolutionConfig config;
  config.solutionId = "face_detection_rtmp";
  config.solutionName = "Face Detection with RTMP Streaming";
  config.solutionType = "face_detection";
  config.isDefault = true;

  // File Source Node
  SolutionConfig::NodeConfig fileSrc;
  fileSrc.nodeType = "file_src";
  fileSrc.nodeName = "file_src_{instanceId}";
  fileSrc.parameters["file_path"] = "${FILE_PATH}";
  fileSrc.parameters["channel"] = "0";
  // IMPORTANT: Use resize_ratio = 1.0 (no resize) if video already has fixed
  // resolution This prevents double-resizing which can cause shape mismatch
  // errors If your video is already re-encoded with fixed resolution (e.g.,
  // 640x360), use 1.0 If your video has variable resolution, re-encode it
  // first, then use 1.0
  //
  // Alternative: If you must resize, use ratios that produce even dimensions:
  // - 0.5 = 1280x720 -> 640x360 (for original 1280x720 videos)
  // - 0.25 = 1280x720 -> 320x180 (smaller, faster)
  // - 0.125 = 1280x720 -> 160x90 (very small)
  //
  // CRITICAL: The best solution is to re-encode video with fixed resolution,
  // then use resize_ratio = 1.0 to avoid any resize operations
  fileSrc.parameters["resize_ratio"] = "1.0";
  config.pipeline.push_back(fileSrc);

  // YuNet Face Detector Node
  // NOTE: YuNet 2022mar model may have issues with dynamic input sizes
  // If you encounter shape mismatch errors, consider using YuNet 2023mar model
  // which has better support for variable input sizes
  SolutionConfig::NodeConfig faceDetector;
  faceDetector.nodeType = "yunet_face_detector";
  faceDetector.nodeName = "yunet_face_detector_{instanceId}";
  faceDetector.parameters["model_path"] = "${MODEL_PATH}";
  faceDetector.parameters["score_threshold"] = "${detectionSensitivity}";
  faceDetector.parameters["nms_threshold"] = "0.5";
  faceDetector.parameters["top_k"] = "50";
  config.pipeline.push_back(faceDetector);

  // SFace Feature Encoder Node
  SolutionConfig::NodeConfig sfaceEncoder;
  sfaceEncoder.nodeType = "sface_feature_encoder";
  sfaceEncoder.nodeName = "sface_face_encoder_{instanceId}";
  sfaceEncoder.parameters["model_path"] = "${SFACE_MODEL_PATH}";
  config.pipeline.push_back(sfaceEncoder);

  // Face OSD v2 Node
  SolutionConfig::NodeConfig faceOSD;
  faceOSD.nodeType = "face_osd_v2";
  faceOSD.nodeName = "osd_{instanceId}";
  config.pipeline.push_back(faceOSD);

  // RTMP Destination Node
  SolutionConfig::NodeConfig rtmpDes;
  rtmpDes.nodeType = "rtmp_des";
  rtmpDes.nodeName = "rtmp_des_{instanceId}";
  rtmpDes.parameters["rtmp_url"] =
      "${RTMP_DES_URL}"; // Support RTMP_DES_URL (new) and RTMP_URL (backward
                         // compatibility via pipeline builder)
  rtmpDes.parameters["channel"] = "0";
  config.pipeline.push_back(rtmpDes);

  // Default configurations
  config.defaults["detectorMode"] = "SmartDetection";
  config.defaults["detectionSensitivity"] = "Low";
  config.defaults["sensorModality"] = "RGB";

  registerSolution(config);
}

void SolutionRegistry::registerBACrosslineSolution() {
  SolutionConfig config;
  config.solutionId = "ba_crossline";
  config.solutionName = "Behavior Analysis - Crossline Detection";
  config.solutionType = "behavior_analysis";
  config.isDefault = true;

  // File Source Node
  SolutionConfig::NodeConfig fileSrc;
  fileSrc.nodeType = "file_src";
  fileSrc.nodeName = "file_src_{instanceId}";
  fileSrc.parameters["file_path"] = "${FILE_PATH}";
  fileSrc.parameters["channel"] = "0";
  fileSrc.parameters["resize_ratio"] = "0.4";
  config.pipeline.push_back(fileSrc);

  // YOLO Detector Node
  SolutionConfig::NodeConfig yoloDetector;
  yoloDetector.nodeType = "yolo_detector";
  yoloDetector.nodeName = "yolo_detector_{instanceId}";
  yoloDetector.parameters["weights_path"] = "${WEIGHTS_PATH}";
  yoloDetector.parameters["config_path"] = "${CONFIG_PATH}";
  yoloDetector.parameters["labels_path"] = "${LABELS_PATH}";
  config.pipeline.push_back(yoloDetector);

  // SORT Tracker Node
  SolutionConfig::NodeConfig sortTrack;
  sortTrack.nodeType = "sort_track";
  sortTrack.nodeName = "sort_tracker_{instanceId}";
  config.pipeline.push_back(sortTrack);

  // BA Crossline Node (use placeholders so instance additionalParams.input
  // CROSSLINE_* are applied; fallback in pipeline builder also reads CROSSLINE_*
  // from additionalParams when solution has no line_channel)
  SolutionConfig::NodeConfig baCrossline;
  baCrossline.nodeType = "ba_crossline";
  baCrossline.nodeName = "ba_crossline_{instanceId}";
  baCrossline.parameters["line_channel"] = "0";
  baCrossline.parameters["line_start_x"] = "${CROSSLINE_START_X}";
  baCrossline.parameters["line_start_y"] = "${CROSSLINE_START_Y}";
  baCrossline.parameters["line_end_x"] = "${CROSSLINE_END_X}";
  baCrossline.parameters["line_end_y"] = "${CROSSLINE_END_Y}";
  config.pipeline.push_back(baCrossline);

  // BA Crossline OSD Node
  SolutionConfig::NodeConfig baCrosslineOSD;
  baCrosslineOSD.nodeType = "ba_crossline_osd";
  baCrosslineOSD.nodeName = "osd_{instanceId}";
  config.pipeline.push_back(baCrosslineOSD);

  // Screen Destination Node (optional - can be disabled via ENABLE_SCREEN_DES
  // parameter)
  SolutionConfig::NodeConfig screenDes;
  screenDes.nodeType = "screen_des";
  screenDes.nodeName = "screen_des_{instanceId}";
  screenDes.parameters["channel"] = "0";
  screenDes.parameters["enabled"] =
      "${ENABLE_SCREEN_DES}"; // Default: empty (enabled if display available),
                              // set to "false" to disable
  config.pipeline.push_back(screenDes);

  // RTMP Destination Node
  SolutionConfig::NodeConfig rtmpDes;
  rtmpDes.nodeType = "rtmp_des";
  rtmpDes.nodeName = "rtmp_des_{instanceId}";
  rtmpDes.parameters["rtmp_url"] =
      "${RTMP_DES_URL}"; // Support RTMP_DES_URL (new) and RTMP_URL (backward
                         // compatibility via pipeline builder)
  rtmpDes.parameters["channel"] = "0";
  config.pipeline.push_back(rtmpDes);

  // Default configurations
  config.defaults["detectorMode"] = "SmartDetection";
  config.defaults["detectionSensitivity"] = "0.7";
  config.defaults["sensorModality"] = "RGB";

  registerSolution(config);
}

void SolutionRegistry::registerBAStopSolution() {
  SolutionConfig config;
  config.solutionId = "ba_stop";
  config.solutionName = "Behavior Analysis - Stop Detection";
  config.solutionType = "behavior_analysis";
  config.isDefault = true;

  // File Source Node
  SolutionConfig::NodeConfig fileSrc;
  fileSrc.nodeType = "file_src";
  fileSrc.nodeName = "file_src_{instanceId}";
  fileSrc.parameters["file_path"] = "${FILE_PATH}";
  fileSrc.parameters["channel"] = "0";
  fileSrc.parameters["resize_ratio"] = "0.4";
  config.pipeline.push_back(fileSrc);

  // YOLO Detector Node
  SolutionConfig::NodeConfig yoloDetector;
  yoloDetector.nodeType = "yolo_detector";
  yoloDetector.nodeName = "yolo_detector_{instanceId}";
  yoloDetector.parameters["weights_path"] = "${WEIGHTS_PATH}";
  yoloDetector.parameters["config_path"] = "${CONFIG_PATH}";
  yoloDetector.parameters["labels_path"] = "${LABELS_PATH}";
  config.pipeline.push_back(yoloDetector);

  // SORT Tracker Node
  SolutionConfig::NodeConfig sortTrack;
  sortTrack.nodeType = "sort_track";
  sortTrack.nodeName = "sort_tracker_{instanceId}";
  config.pipeline.push_back(sortTrack);

  // BA Stop Node
  SolutionConfig::NodeConfig baStop;
  baStop.nodeType = "ba_stop";
  baStop.nodeName = "ba_stop_{instanceId}";
  baStop.parameters["StopZones"] = "[{\"id\":\"a6e6270f-5662-42af-bc45-59b7131f7a5d\",\"name\":\"Stop Zone 1\",\"roi\":[{\"x\":0,\"y\":0},{\"x\":10,\"y\":0},{\"x\":10,\"y\":10}]}]";
  config.pipeline.push_back(baStop);

  // BA Stop OSD Node
  SolutionConfig::NodeConfig baStopOSD;
  baStopOSD.nodeType = "ba_stop_osd";
  baStopOSD.nodeName = "osd_{instanceId}";
  config.pipeline.push_back(baStopOSD);

  // JSON MQTT Broker Node (optional)
  SolutionConfig::NodeConfig jsonMqtt;
  jsonMqtt.nodeType = "json_mqtt_broker";
  jsonMqtt.nodeName = "json_mqtt_broker_{instanceId}";
  config.pipeline.push_back(jsonMqtt);

  // Screen Destination Node
  SolutionConfig::NodeConfig screenDes;
  screenDes.nodeType = "screen_des";
  screenDes.nodeName = "screen_des_{instanceId}";
  screenDes.parameters["channel"] = "0";
  screenDes.parameters["enabled"] = "${ENABLE_SCREEN_DES}";
  config.pipeline.push_back(screenDes);

  // RTMP Destination Node
  SolutionConfig::NodeConfig rtmpDes;
  rtmpDes.nodeType = "rtmp_des";
  rtmpDes.nodeName = "rtmp_des_{instanceId}";
  rtmpDes.parameters["rtmp_url"] = "${RTMP_DES_URL}";
  rtmpDes.parameters["channel"] = "0";
  config.pipeline.push_back(rtmpDes);

  // Default configurations
  config.defaults["detectorMode"] = "SmartDetection";
  config.defaults["detectionSensitivity"] = "0.7";
  config.defaults["sensorModality"] = "RGB";
  // Detection tuning (MIN_STOP_SECONDS) is specified per-zone (StopZones/JamZones); no instance-level default provided here
  (void)0;

  registerSolution(config);
}

void SolutionRegistry::registerBAJamSolution() {
  SolutionConfig config;
  config.solutionId = "ba_jam";
  config.solutionName = "Behavior Analysis - Traffic Jam Detection";
  config.solutionType = "behavior_analysis";
  config.isDefault = true;

  // File Source Node
  SolutionConfig::NodeConfig fileSrc;
  fileSrc.nodeType = "file_src";
  fileSrc.nodeName = "file_src_{instanceId}";
  fileSrc.parameters["file_path"] = "${FILE_PATH}";
  fileSrc.parameters["channel"] = "0";
  fileSrc.parameters["resize_ratio"] = "0.4";
  config.pipeline.push_back(fileSrc);

  // YOLO Detector Node (vehicle detection)
  SolutionConfig::NodeConfig yoloDetector;
  yoloDetector.nodeType = "yolo_detector";
  yoloDetector.nodeName = "yolo_detector_{instanceId}";
  yoloDetector.parameters["weights_path"] = "${WEIGHTS_PATH}";
  yoloDetector.parameters["config_path"] = "${CONFIG_PATH}";
  yoloDetector.parameters["labels_path"] = "${LABELS_PATH}";
  config.pipeline.push_back(yoloDetector);

  // SORT Tracker Node
  SolutionConfig::NodeConfig sortTrack;
  sortTrack.nodeType = "sort_track";
  sortTrack.nodeName = "sort_tracker_{instanceId}";
  config.pipeline.push_back(sortTrack);

  // BA Jam Node
  SolutionConfig::NodeConfig baJam;
  baJam.nodeType = "ba_jam";
  baJam.nodeName = "ba_jam_{instanceId}";
  baJam.parameters["JamZones"] = "[{\"id\":\"eff3827d-0154-45ce-bb36-dfbf34ab0ae0\",\"name\":\"Downtown Jam Zone\",\"roi\":[{\"x\":0,\"y\":100},{\"x\":1920,\"y\":100},{\"x\":1920,\"y\":400},{\"x\":0,\"y\":400}],\"enabled\":true,\"check_interval_frames\":20,\"check_min_hit_frames\":50,\"check_max_distance\":8,\"check_min_stops\":8,\"check_notify_interval\":10}]";
  config.pipeline.push_back(baJam);

  // BA Jam OSD Node
  SolutionConfig::NodeConfig baJamOSD;
  baJamOSD.nodeType = "ba_jam_osd";
  baJamOSD.nodeName = "osd_{instanceId}";
  config.pipeline.push_back(baJamOSD);

  // JSON MQTT Broker Node (optional)
  SolutionConfig::NodeConfig jsonMqtt;
  jsonMqtt.nodeType = "json_mqtt_broker";
  jsonMqtt.nodeName = "json_mqtt_broker_{instanceId}";
  config.pipeline.push_back(jsonMqtt);

  // Screen Destination Node (optional)
  SolutionConfig::NodeConfig screenDes;
  screenDes.nodeType = "screen_des";
  screenDes.nodeName = "screen_des_{instanceId}";
  screenDes.parameters["channel"] = "0";
  screenDes.parameters["enabled"] = "${ENABLE_SCREEN_DES}";
  config.pipeline.push_back(screenDes);

  // RTMP Destination Node
  SolutionConfig::NodeConfig rtmpDes;
  rtmpDes.nodeType = "rtmp_des";
  rtmpDes.nodeName = "rtmp_des_{instanceId}";
  rtmpDes.parameters["rtmp_url"] = "${RTMP_DES_URL}";
  rtmpDes.parameters["channel"] = "0";
  config.pipeline.push_back(rtmpDes);

  // Default configurations
  config.defaults["detectorMode"] = "SmartDetection";
  config.defaults["detectionSensitivity"] = "0.7";
  config.defaults["sensorModality"] = "RGB";

  registerSolution(config);
}

void SolutionRegistry::registerFaceSwapSolution() {
  SolutionConfig config;
  config.solutionId = "face_swap";
  config.solutionName = "Face Swap";
  config.solutionType = "face_processing";
  config.isDefault = true;

  // File Source Node (supports flexible input: file, RTSP, RTMP, HLS via
  // auto-detection)
  SolutionConfig::NodeConfig fileSrc;
  fileSrc.nodeType = "file_src";
  fileSrc.nodeName = "file_src_{instanceId}";
  // Support both FILE_PATH and RTSP_URL for backward compatibility
  // Pipeline builder will auto-detect input type from FILE_PATH or RTSP_SRC_URL
  fileSrc.parameters["file_path"] =
      "${FILE_PATH}"; // Can be file path or RTSP/RTMP URL
  fileSrc.parameters["channel"] = "0";
  fileSrc.parameters["resize_ratio"] = "1.0";
  config.pipeline.push_back(fileSrc);

  // YuNet Face Detector Node (for face detection in face swap)
  SolutionConfig::NodeConfig faceDetector;
  faceDetector.nodeType = "yunet_face_detector";
  faceDetector.nodeName = "face_detector_{instanceId}";
  faceDetector.parameters["model_path"] = "${FACE_DETECTION_MODEL_PATH}";
  faceDetector.parameters["score_threshold"] = "0.7";
  faceDetector.parameters["nms_threshold"] = "0.5";
  faceDetector.parameters["top_k"] = "50";
  config.pipeline.push_back(faceDetector);

  // Face Swap Node
  SolutionConfig::NodeConfig faceSwap;
  faceSwap.nodeType = "face_swap";
  faceSwap.nodeName = "face_swap_{instanceId}";
  faceSwap.parameters["yunet_face_detect_model"] =
      "${FACE_DETECTION_MODEL_PATH}";
  faceSwap.parameters["buffalo_l_face_encoding_model"] =
      "${BUFFALO_L_FACE_ENCODING_MODEL}";
  faceSwap.parameters["emap_file_for_embeddings"] =
      "${EMAP_FILE_FOR_EMBEDDINGS}";
  faceSwap.parameters["insightface_swap_model"] = "${FACE_SWAP_MODEL_PATH}";
  faceSwap.parameters["swap_source_image"] = "${SWAP_SOURCE_IMAGE}";
  faceSwap.parameters["swap_source_face_index"] = "0";
  faceSwap.parameters["min_face_w_h"] = "50";
  faceSwap.parameters["swap_on_osd"] = "true";
  faceSwap.parameters["act_as_primary_detector"] = "false";
  config.pipeline.push_back(faceSwap);

  // RTMP Destination Node
  SolutionConfig::NodeConfig rtmpDes;
  rtmpDes.nodeType = "rtmp_des";
  rtmpDes.nodeName = "destination_{instanceId}";
  rtmpDes.parameters["rtmp_url"] =
      "${RTMP_DES_URL}"; // Support RTMP_DES_URL (new) and RTMP_URL (backward
                         // compatibility via pipeline builder)
  rtmpDes.parameters["channel"] = "0";
  config.pipeline.push_back(rtmpDes);

  // Default configurations (development defaults - should be overridden in
  // production) SECURITY: These are development defaults using localhost. In
  // production, always provide actual URLs via request or environment
  // variables.
  config.defaults["RTSP_URL"] = "rtsp://localhost:8554/stream";
  config.defaults["FACE_DETECTION_MODEL_PATH"] =
      "/opt/edgeos-api/models/face/yunet.onnx";
  config.defaults["BUFFALO_L_FACE_ENCODING_MODEL"] =
      "/opt/edgeos-api/models/face/buffalo_l.onnx";
  config.defaults["EMAP_FILE_FOR_EMBEDDINGS"] =
      "/opt/edgeos-api/models/face/emap.onnx";
  config.defaults["FACE_SWAP_MODEL_PATH"] =
      "/opt/edgeos-api/models/face/face_swap.onnx";
  config.defaults["SWAP_SOURCE_IMAGE"] =
      "/opt/edgeos-api/models/face/source_face.jpg";
  config.defaults["RTMP_URL"] = "rtmp://localhost:1935/live/stream";
  config.defaults["detectorMode"] = "SmartDetection";
  config.defaults["detectionSensitivity"] = "0.7";
  config.defaults["sensorModality"] = "RGB";

  registerSolution(config);
}

void SolutionRegistry::registerInsightFaceRecognitionSolution() {
  SolutionConfig config;
  config.solutionId = "insightface_recognition";
  config.solutionName = "InsightFace Recognition";
  config.solutionType = "face_recognition";
  config.isDefault = true;

  // File Source Node (supports flexible input: file, RTSP, RTMP, HLS via
  // auto-detection)
  SolutionConfig::NodeConfig fileSrc;
  fileSrc.nodeType = "file_src";
  fileSrc.nodeName = "file_src_{instanceId}";
  // Support both FILE_PATH and RTSP_URL for backward compatibility
  // Pipeline builder will auto-detect input type from FILE_PATH or RTSP_SRC_URL
  fileSrc.parameters["file_path"] =
      "${FILE_PATH}"; // Can be file path or RTSP/RTMP URL
  fileSrc.parameters["channel"] = "0";
  fileSrc.parameters["resize_ratio"] = "1.0";
  config.pipeline.push_back(fileSrc);

  // YuNet Face Detector Node
  SolutionConfig::NodeConfig faceDetector;
  faceDetector.nodeType = "yunet_face_detector";
  faceDetector.nodeName = "face_detector_{instanceId}";
  faceDetector.parameters["model_path"] = "${FACE_DETECTION_MODEL_PATH}";
  faceDetector.parameters["score_threshold"] = "0.7";
  faceDetector.parameters["nms_threshold"] = "0.5";
  faceDetector.parameters["top_k"] = "50";
  config.pipeline.push_back(faceDetector);

  // InsightFace Recognition Node
  SolutionConfig::NodeConfig insightFaceRecognition;
  insightFaceRecognition.nodeType = "insight_face_recognition";
  insightFaceRecognition.nodeName = "face_recognition_{instanceId}";
  insightFaceRecognition.parameters["model_path"] =
      "${FACE_RECOGNITION_MODEL_PATH}";
  config.pipeline.push_back(insightFaceRecognition);

  // File Destination Node
  SolutionConfig::NodeConfig fileDes;
  fileDes.nodeType = "file_des";
  fileDes.nodeName = "destination_{instanceId}";
  fileDes.parameters["save_dir"] = "${SAVE_DIR}";
  fileDes.parameters["name_prefix"] = "insightface_recognition";
  fileDes.parameters["osd"] = "true";
  config.pipeline.push_back(fileDes);

  // Default configurations
  config.defaults["RTSP_URL"] = "rtsp://localhost:8554/stream";
  config.defaults["FACE_DETECTION_MODEL_PATH"] =
      "/opt/edgeos-api/models/face/yunet.onnx";
  config.defaults["FACE_RECOGNITION_MODEL_PATH"] =
      "/opt/edgeos-api/models/face/insightface.onnx";
  config.defaults["SAVE_DIR"] = "/tmp/output";
  config.defaults["detectorMode"] = "SmartDetection";
  config.defaults["detectionSensitivity"] = "0.7";
  config.defaults["sensorModality"] = "RGB";

  registerSolution(config);
}

void SolutionRegistry::registerMLLMAnalysisSolution() {
  SolutionConfig config;
  config.solutionId = "mllm_analysis";
  config.solutionName = "MLLM Analysis";
  config.solutionType = "multimodal_analysis";
  config.isDefault = true;

  // File Source Node (supports flexible input: file, RTSP, RTMP, HLS via
  // auto-detection)
  SolutionConfig::NodeConfig fileSrc;
  fileSrc.nodeType = "file_src";
  fileSrc.nodeName = "file_src_{instanceId}";
  // Support both FILE_PATH and RTSP_URL for backward compatibility
  // Pipeline builder will auto-detect input type from FILE_PATH or RTSP_SRC_URL
  fileSrc.parameters["file_path"] =
      "${FILE_PATH}"; // Can be file path or RTSP/RTMP URL
  fileSrc.parameters["channel"] = "0";
  fileSrc.parameters["resize_ratio"] = "1.0";
  config.pipeline.push_back(fileSrc);

  // MLLM Analyser Node
  SolutionConfig::NodeConfig mllmAnalyser;
  mllmAnalyser.nodeType = "mllm_analyser";
  mllmAnalyser.nodeName = "mllm_analyser_{instanceId}";
  mllmAnalyser.parameters["model_name"] = "${MODEL_NAME}";
  mllmAnalyser.parameters["prompt"] = "${PROMPT}";
  mllmAnalyser.parameters["api_base_url"] = "${API_BASE_URL}";
  mllmAnalyser.parameters["api_key"] = "${API_KEY}";
  mllmAnalyser.parameters["backend_type"] = "${BACKEND_TYPE}";
  config.pipeline.push_back(mllmAnalyser);

  // JSON Console Broker Node
  SolutionConfig::NodeConfig jsonBroker;
  jsonBroker.nodeType = "json_console_broker";
  jsonBroker.nodeName = "broker_{instanceId}";
  config.pipeline.push_back(jsonBroker);

  // Default configurations
  config.defaults["RTSP_URL"] = "rtsp://localhost:8554/stream";
  config.defaults["MODEL_NAME"] = "llava";
  config.defaults["PROMPT"] = "Describe what you see in this image";
  config.defaults["API_BASE_URL"] = "http://localhost:11434";
  config.defaults["API_KEY"] = "";
  config.defaults["BACKEND_TYPE"] = "ollama";
  config.defaults["detectorMode"] = "SmartDetection";
  config.defaults["detectionSensitivity"] = "0.7";
  config.defaults["sensorModality"] = "RGB";

  registerSolution(config);
}

#ifdef CVEDIX_WITH_RKNN
void SolutionRegistry::registerRKNNYOLOv11DetectionSolution() {
  SolutionConfig config;
  config.solutionId = "rknn_yolov11_detection";
  config.solutionName = "RKNN YOLOv11 Object Detection";
  config.solutionType = "object_detection";
  config.isDefault = true;

  // File Source Node (supports flexible input: file, RTSP, RTMP, HLS via
  // auto-detection)
  SolutionConfig::NodeConfig fileSrc;
  fileSrc.nodeType = "file_src";
  fileSrc.nodeName = "file_src_{instanceId}";
  // Support both FILE_PATH and RTSP_URL for backward compatibility
  // Pipeline builder will auto-detect input type from FILE_PATH or RTSP_SRC_URL
  fileSrc.parameters["file_path"] =
      "${FILE_PATH}"; // Can be file path or RTSP/RTMP URL
  fileSrc.parameters["channel"] = "0";
  fileSrc.parameters["resize_ratio"] = "1.0";
  config.pipeline.push_back(fileSrc);

  // RKNN YOLOv11 Detector Node
  SolutionConfig::NodeConfig rknnYolov11Detector;
  rknnYolov11Detector.nodeType = "rknn_yolov11_detector";
  rknnYolov11Detector.nodeName = "detector_{instanceId}";
  rknnYolov11Detector.parameters["model_path"] = "${MODEL_PATH}";
  rknnYolov11Detector.parameters["score_threshold"] = "0.5";
  rknnYolov11Detector.parameters["nms_threshold"] = "0.5";
  rknnYolov11Detector.parameters["input_width"] = "640";
  rknnYolov11Detector.parameters["input_height"] = "640";
  rknnYolov11Detector.parameters["num_classes"] = "80";
  config.pipeline.push_back(rknnYolov11Detector);

  // File Destination Node
  SolutionConfig::NodeConfig fileDes;
  fileDes.nodeType = "file_des";
  fileDes.nodeName = "destination_{instanceId}";
  fileDes.parameters["save_dir"] = "${SAVE_DIR}";
  fileDes.parameters["name_prefix"] = "rknn_yolov11_detection";
  fileDes.parameters["osd"] = "true";
  config.pipeline.push_back(fileDes);

  // Default configurations
  config.defaults["RTSP_URL"] = "rtsp://localhost:8554/stream";
  config.defaults["MODEL_PATH"] = "/opt/edgeos-api/models/yolov11/yolov11n.rknn";
  config.defaults["SAVE_DIR"] = "/tmp/output";
  config.defaults["detectorMode"] = "SmartDetection";
  config.defaults["detectionSensitivity"] = "0.7";
  config.defaults["sensorModality"] = "RGB";

  registerSolution(config);
}
#endif // CVEDIX_WITH_RKNN

#ifdef CVEDIX_WITH_TRT
void SolutionRegistry::registerTRTInsightFaceRecognitionSolution() {
  SolutionConfig config;
  config.solutionId = "trt_insightface_recognition";
  config.solutionName = "TensorRT InsightFace Recognition";
  config.solutionType = "face_recognition";
  config.isDefault = true;

  // File Source Node (supports flexible input: file, RTSP, RTMP, HLS via
  // auto-detection)
  SolutionConfig::NodeConfig fileSrc;
  fileSrc.nodeType = "file_src";
  fileSrc.nodeName = "file_src_{instanceId}";
  // Support both FILE_PATH and RTSP_URL for backward compatibility
  // Pipeline builder will auto-detect input type from FILE_PATH or RTSP_SRC_URL
  fileSrc.parameters["file_path"] =
      "${FILE_PATH}"; // Can be file path or RTSP/RTMP URL
  fileSrc.parameters["channel"] = "0";
  fileSrc.parameters["resize_ratio"] = "1.0";
  config.pipeline.push_back(fileSrc);

  // YuNet Face Detector Node
  SolutionConfig::NodeConfig faceDetector;
  faceDetector.nodeType = "yunet_face_detector";
  faceDetector.nodeName = "face_detector_{instanceId}";
  faceDetector.parameters["model_path"] = "${FACE_DETECTION_MODEL_PATH}";
  faceDetector.parameters["score_threshold"] = "0.7";
  faceDetector.parameters["nms_threshold"] = "0.5";
  faceDetector.parameters["top_k"] = "50";
  config.pipeline.push_back(faceDetector);

  // TensorRT InsightFace Recognition Node
  SolutionConfig::NodeConfig trtInsightFaceRecognition;
  trtInsightFaceRecognition.nodeType = "trt_insight_face_recognition";
  trtInsightFaceRecognition.nodeName = "face_recognition_{instanceId}";
  trtInsightFaceRecognition.parameters["model_path"] =
      "${FACE_RECOGNITION_MODEL_PATH}";
  config.pipeline.push_back(trtInsightFaceRecognition);

  // File Destination Node
  SolutionConfig::NodeConfig fileDes;
  fileDes.nodeType = "file_des";
  fileDes.nodeName = "destination_{instanceId}";
  fileDes.parameters["save_dir"] = "${SAVE_DIR}";
  fileDes.parameters["name_prefix"] = "trt_insightface_recognition";
  fileDes.parameters["osd"] = "true";
  config.pipeline.push_back(fileDes);

  // Default configurations
  config.defaults["RTSP_URL"] = "rtsp://localhost:8554/stream";
  config.defaults["FACE_DETECTION_MODEL_PATH"] =
      "/opt/edgeos-api/models/face/yunet.onnx";
  config.defaults["FACE_RECOGNITION_MODEL_PATH"] =
      "/opt/edgeos-api/models/face/insightface.trt";
  config.defaults["SAVE_DIR"] = "/tmp/output";
  config.defaults["detectorMode"] = "SmartDetection";
  config.defaults["detectionSensitivity"] = "0.7";
  config.defaults["sensorModality"] = "RGB";

  registerSolution(config);
}
#endif // CVEDIX_WITH_TRT

void SolutionRegistry::registerMaskRCNNDetectionSolution() {
  SolutionConfig config;
  config.solutionId = "mask_rcnn_detection_default";
  config.solutionName = "MaskRCNN Detection Solution";
  config.solutionType = "segmentation";
  config.isDefault = true;

  // File Source Node
  SolutionConfig::NodeConfig fileSrc;
  fileSrc.nodeType = "file_src";
  fileSrc.nodeName = "file_src_{instanceId}";
  fileSrc.parameters["file_path"] = "${FILE_PATH}";
  fileSrc.parameters["channel"] = "0";
  fileSrc.parameters["resize_ratio"] = "${RESIZE_RATIO}";
  config.pipeline.push_back(fileSrc);

  // MaskRCNN Detector Node
  SolutionConfig::NodeConfig maskRCNN;
  maskRCNN.nodeType = "mask_rcnn_detector";
  maskRCNN.nodeName = "mask_rcnn_detector_{instanceId}";
  maskRCNN.parameters["model_path"] = "${MODEL_PATH}";
  maskRCNN.parameters["model_config_path"] = "${MODEL_CONFIG_PATH}";
  maskRCNN.parameters["labels_path"] = "${LABELS_PATH}";
  maskRCNN.parameters["input_width"] = "${INPUT_WIDTH}";
  maskRCNN.parameters["input_height"] = "${INPUT_HEIGHT}";
  maskRCNN.parameters["score_threshold"] = "${SCORE_THRESHOLD}";
  config.pipeline.push_back(maskRCNN);

  // OSD v3 Node (for displaying masks and labels)
  SolutionConfig::NodeConfig osd;
  osd.nodeType = "osd_v3";
  osd.nodeName = "osd_v3_{instanceId}";
  // Use default font from environment variable if available, otherwise use
  // relative path Pipeline builder will resolve this using
  // resolveDefaultFontPath() if empty
  std::string defaultFont = EnvConfig::resolveDefaultFontPath();
  if (!defaultFont.empty()) {
    osd.parameters["font_path"] = defaultFont;
  } else {
    // Fallback to relative path (will be resolved by pipeline builder)
    osd.parameters["font_path"] = "./cvedix_data/font/NotoSansCJKsc-Medium.otf";
  }
  config.pipeline.push_back(osd);

  // File Destination Node
  SolutionConfig::NodeConfig fileDes;
  fileDes.nodeType = "file_des";
  fileDes.nodeName = "file_des_{instanceId}";
  fileDes.parameters["save_dir"] = "./output/{instanceId}";
  fileDes.parameters["name_prefix"] = "mask_rcnn_detection";
  fileDes.parameters["osd"] = "true";
  config.pipeline.push_back(fileDes);

  // Default configurations
  config.defaults["detectorMode"] = "SmartDetection";
  config.defaults["detectionSensitivity"] = "Medium";
  config.defaults["sensorModality"] = "RGB";
  config.defaults["RESIZE_RATIO"] = "1.0";
  config.defaults["INPUT_WIDTH"] = "416";
  config.defaults["INPUT_HEIGHT"] = "416";
  config.defaults["SCORE_THRESHOLD"] = "0.5";

  registerSolution(config);
}

void SolutionRegistry::registerMaskRCNNRTMPSolution() {
  SolutionConfig config;
  config.solutionId = "mask_rcnn_rtmp_default";
  config.solutionName = "MaskRCNN Detection with RTMP Streaming";
  config.solutionType = "segmentation";
  config.isDefault = true;

  // File Source Node
  SolutionConfig::NodeConfig fileSrc;
  fileSrc.nodeType = "file_src";
  fileSrc.nodeName = "file_src_{instanceId}";
  fileSrc.parameters["file_path"] = "${FILE_PATH}";
  fileSrc.parameters["channel"] = "0";
  // IMPORTANT: Use resize_ratio = 1.0 (no resize) if video already has fixed
  // resolution This prevents double-resizing which can cause shape mismatch
  // errors
  fileSrc.parameters["resize_ratio"] = "${RESIZE_RATIO}";
  config.pipeline.push_back(fileSrc);

  // MaskRCNN Detector Node
  SolutionConfig::NodeConfig maskRCNN;
  maskRCNN.nodeType = "mask_rcnn_detector";
  maskRCNN.nodeName = "mask_rcnn_detector_{instanceId}";
  maskRCNN.parameters["model_path"] = "${MODEL_PATH}";
  maskRCNN.parameters["model_config_path"] = "${MODEL_CONFIG_PATH}";
  maskRCNN.parameters["labels_path"] = "${LABELS_PATH}";
  maskRCNN.parameters["input_width"] = "${INPUT_WIDTH}";
  maskRCNN.parameters["input_height"] = "${INPUT_HEIGHT}";
  maskRCNN.parameters["score_threshold"] = "${SCORE_THRESHOLD}";
  config.pipeline.push_back(maskRCNN);

  // OSD v3 Node (for displaying masks and labels)
  SolutionConfig::NodeConfig osd;
  osd.nodeType = "osd_v3";
  osd.nodeName = "osd_v3_{instanceId}";
  // Use default font from environment variable if available, otherwise use
  // relative path Pipeline builder will resolve this using
  // resolveDefaultFontPath() if empty
  std::string defaultFont = EnvConfig::resolveDefaultFontPath();
  if (!defaultFont.empty()) {
    osd.parameters["font_path"] = defaultFont;
  } else {
    // Fallback to relative path (will be resolved by pipeline builder)
    osd.parameters["font_path"] = "./cvedix_data/font/NotoSansCJKsc-Medium.otf";
  }
  config.pipeline.push_back(osd);

  // RTMP Destination Node
  SolutionConfig::NodeConfig rtmpDes;
  rtmpDes.nodeType = "rtmp_des";
  rtmpDes.nodeName = "rtmp_des_{instanceId}";
  rtmpDes.parameters["rtmp_url"] =
      "${RTMP_URL}"; // Support RTMP_URL (backward compatibility)
  rtmpDes.parameters["channel"] = "0";
  config.pipeline.push_back(rtmpDes);

  // Default configurations
  config.defaults["detectorMode"] = "SmartDetection";
  config.defaults["detectionSensitivity"] = "Medium";
  config.defaults["sensorModality"] = "RGB";
  config.defaults["RESIZE_RATIO"] = "1.0";
  config.defaults["INPUT_WIDTH"] = "416";
  config.defaults["INPUT_HEIGHT"] = "416";
  config.defaults["SCORE_THRESHOLD"] = "0.5";

  registerSolution(config);
}

void SolutionRegistry::registerFaceDetectionRTMPDefaultSolution() {
  SolutionConfig config;
  config.solutionId = "face_detection_rtmp_default";
  config.solutionName = "Face Detection - RTMP Streaming";
  config.solutionType = "face_detection";
  config.isDefault = true;

  // File Source Node
  SolutionConfig::NodeConfig fileSrc;
  fileSrc.nodeType = "file_src";
  fileSrc.nodeName = "file_src_{instanceId}";
  fileSrc.parameters["file_path"] = "${FILE_PATH}";
  fileSrc.parameters["channel"] = "0";
  fileSrc.parameters["resize_ratio"] = "${RESIZE_RATIO}";
  config.pipeline.push_back(fileSrc);

  // YuNet Face Detector Node
  SolutionConfig::NodeConfig faceDetector;
  faceDetector.nodeType = "yunet_face_detector";
  faceDetector.nodeName = "face_detector_{instanceId}";
  faceDetector.parameters["model_path"] = "${MODEL_PATH}";
  faceDetector.parameters["score_threshold"] = "${detectionSensitivity}";
  faceDetector.parameters["nms_threshold"] = "0.5";
  faceDetector.parameters["top_k"] = "50";
  config.pipeline.push_back(faceDetector);

  // Face OSD v2 Node
  SolutionConfig::NodeConfig faceOsd;
  faceOsd.nodeType = "face_osd_v2";
  faceOsd.nodeName = "face_osd_{instanceId}";
  config.pipeline.push_back(faceOsd);

  // RTMP Destination Node
  SolutionConfig::NodeConfig rtmpDes;
  rtmpDes.nodeType = "rtmp_des";
  rtmpDes.nodeName = "rtmp_des_{instanceId}";
  rtmpDes.parameters["rtmp_url"] = "${RTMP_URL}";
  rtmpDes.parameters["channel"] = "0";
  config.pipeline.push_back(rtmpDes);

  // Default configurations
  config.defaults["detectorMode"] = "SmartDetection";
  config.defaults["detectionSensitivity"] = "0.7";
  config.defaults["sensorModality"] = "RGB";
  config.defaults["RESIZE_RATIO"] = "1.0";

  registerSolution(config);
}

void SolutionRegistry::registerFaceDetectionRTSPDefaultSolution() {
  SolutionConfig config;
  config.solutionId = "face_detection_rtsp_default";
  config.solutionName = "Face Detection - RTSP Stream";
  config.solutionType = "face_detection";
  config.isDefault = true;

  // RTSP Source Node
  SolutionConfig::NodeConfig rtspSrc;
  rtspSrc.nodeType = "rtsp_src";
  rtspSrc.nodeName = "rtsp_src_{instanceId}";
  rtspSrc.parameters["rtsp_url"] = "${RTSP_URL}";
  rtspSrc.parameters["channel"] = "0";
  rtspSrc.parameters["resize_ratio"] = "${RESIZE_RATIO}";
  config.pipeline.push_back(rtspSrc);

  // YuNet Face Detector Node
  SolutionConfig::NodeConfig faceDetector;
  faceDetector.nodeType = "yunet_face_detector";
  faceDetector.nodeName = "face_detector_{instanceId}";
  faceDetector.parameters["model_path"] = "${MODEL_PATH}";
  faceDetector.parameters["score_threshold"] = "${detectionSensitivity}";
  faceDetector.parameters["nms_threshold"] = "0.5";
  faceDetector.parameters["top_k"] = "50";
  config.pipeline.push_back(faceDetector);

  // File Destination Node
  SolutionConfig::NodeConfig fileDes;
  fileDes.nodeType = "file_des";
  fileDes.nodeName = "file_des_{instanceId}";
  fileDes.parameters["save_dir"] = "./output/{instanceId}";
  fileDes.parameters["name_prefix"] = "face_detection_rtsp";
  fileDes.parameters["osd"] = "true";
  config.pipeline.push_back(fileDes);

  // Default configurations
  config.defaults["detectorMode"] = "SmartDetection";
  config.defaults["detectionSensitivity"] = "0.6";
  config.defaults["sensorModality"] = "RGB";
  config.defaults["RESIZE_RATIO"] = "1.0";

  registerSolution(config);
}

void SolutionRegistry::registerMinimalDefaultSolution() {
  SolutionConfig config;
  config.solutionId = "minimal_default";
  config.solutionName = "Minimal Solution";
  config.solutionType = "face_detection";
  config.isDefault = true;

  // File Source Node
  SolutionConfig::NodeConfig fileSrc;
  fileSrc.nodeType = "file_src";
  fileSrc.nodeName = "file_src_{instanceId}";
  fileSrc.parameters["file_path"] = "${FILE_PATH}";
  fileSrc.parameters["channel"] = "0";
  fileSrc.parameters["resize_ratio"] = "0.5";
  config.pipeline.push_back(fileSrc);

  // YuNet Face Detector Node
  SolutionConfig::NodeConfig faceDetector;
  faceDetector.nodeType = "yunet_face_detector";
  faceDetector.nodeName = "face_detector_{instanceId}";
  faceDetector.parameters["model_path"] = "${MODEL_PATH}";
  faceDetector.parameters["score_threshold"] = "0.5";
  faceDetector.parameters["nms_threshold"] = "0.4";
  faceDetector.parameters["top_k"] = "30";
  config.pipeline.push_back(faceDetector);

  // File Destination Node
  SolutionConfig::NodeConfig fileDes;
  fileDes.nodeType = "file_des";
  fileDes.nodeName = "file_des_{instanceId}";
  fileDes.parameters["save_dir"] = "./output/{instanceId}";
  fileDes.parameters["name_prefix"] = "minimal_test";
  fileDes.parameters["osd"] = "false";
  config.pipeline.push_back(fileDes);

  // Default configurations
  config.defaults["detectorMode"] = "SmartDetection";
  config.defaults["detectionSensitivity"] = "0.5";
  config.defaults["sensorModality"] = "RGB";

  registerSolution(config);
}

void SolutionRegistry::registerFaceDetectionDefaultSolution() {
  SolutionConfig config;
  config.solutionId = "face_detection_default";
  config.solutionName = "Face Detection Solution";
  config.solutionType = "face_detection";
  config.isDefault = true;

  // File Source Node
  SolutionConfig::NodeConfig fileSrc;
  fileSrc.nodeType = "file_src";
  fileSrc.nodeName = "file_src_{instanceId}";
  fileSrc.parameters["file_path"] = "${FILE_PATH}";
  fileSrc.parameters["channel"] = "0";
  fileSrc.parameters["resize_ratio"] = "1.0";
  config.pipeline.push_back(fileSrc);

  // YuNet Face Detector Node
  SolutionConfig::NodeConfig faceDetector;
  faceDetector.nodeType = "yunet_face_detector";
  faceDetector.nodeName = "face_detector_{instanceId}";
  faceDetector.parameters["model_path"] = "${MODEL_PATH}";
  faceDetector.parameters["score_threshold"] = "${detectionSensitivity}";
  faceDetector.parameters["nms_threshold"] = "0.5";
  faceDetector.parameters["top_k"] = "50";
  config.pipeline.push_back(faceDetector);

  // File Destination Node
  SolutionConfig::NodeConfig fileDes;
  fileDes.nodeType = "file_des";
  fileDes.nodeName = "file_des_{instanceId}";
  fileDes.parameters["save_dir"] = "./output/{instanceId}";
  fileDes.parameters["name_prefix"] = "test_face_detection";
  fileDes.parameters["osd"] = "true";
  config.pipeline.push_back(fileDes);

  // Default configurations
  config.defaults["detectorMode"] = "SmartDetection";
  config.defaults["detectionSensitivity"] = "0.7";
  config.defaults["sensorModality"] = "RGB";

  registerSolution(config);
}

void SolutionRegistry::registerRTSPFaceDetectionDefaultSolution() {
  SolutionConfig config;
  config.solutionId = "rtsp_face_detection_default";
  config.solutionName = "RTSP Face Detection Solution";
  config.solutionType = "face_detection";
  config.isDefault = true;

  // RTSP Source Node
  SolutionConfig::NodeConfig rtspSrc;
  rtspSrc.nodeType = "rtsp_src";
  rtspSrc.nodeName = "rtsp_src_{instanceId}";
  rtspSrc.parameters["rtsp_url"] = "${RTSP_URL}";
  rtspSrc.parameters["channel"] = "0";
  rtspSrc.parameters["resize_ratio"] = "1.0";
  config.pipeline.push_back(rtspSrc);

  // YuNet Face Detector Node
  SolutionConfig::NodeConfig faceDetector;
  faceDetector.nodeType = "yunet_face_detector";
  faceDetector.nodeName = "face_detector_{instanceId}";
  faceDetector.parameters["model_path"] = "${MODEL_PATH}";
  faceDetector.parameters["score_threshold"] = "${detectionSensitivity}";
  faceDetector.parameters["nms_threshold"] = "0.5";
  faceDetector.parameters["top_k"] = "50";
  config.pipeline.push_back(faceDetector);

  // File Destination Node
  SolutionConfig::NodeConfig fileDes;
  fileDes.nodeType = "file_des";
  fileDes.nodeName = "file_des_{instanceId}";
  fileDes.parameters["save_dir"] = "./output/{instanceId}";
  fileDes.parameters["name_prefix"] = "test_rtsp_face";
  fileDes.parameters["osd"] = "true";
  config.pipeline.push_back(fileDes);

  // Default configurations
  config.defaults["detectorMode"] = "SmartDetection";
  config.defaults["detectionSensitivity"] = "0.6";
  config.defaults["sensorModality"] = "RGB";
  config.defaults["RTSP_URL"] = "rtsp://localhost/stream";

  registerSolution(config);
}

void SolutionRegistry::registerObjectDetectionYOLODefaultSolution() {
  SolutionConfig config;
  config.solutionId = "object_detection_yolo_default";
  config.solutionName = "Object Detection - YOLO";
  config.solutionType = "object_detection";
  config.isDefault = true;

  // File Source Node
  SolutionConfig::NodeConfig fileSrc;
  fileSrc.nodeType = "file_src";
  fileSrc.nodeName = "file_src_{instanceId}";
  fileSrc.parameters["file_path"] = "${FILE_PATH}";
  fileSrc.parameters["channel"] = "0";
  fileSrc.parameters["resize_ratio"] = "${RESIZE_RATIO}";
  config.pipeline.push_back(fileSrc);

  // YOLO Detector Node
  SolutionConfig::NodeConfig yoloDetector;
  yoloDetector.nodeType = "yolo_detector";
  yoloDetector.nodeName = "yolo_detector_{instanceId}";
  yoloDetector.parameters["weights_path"] = "${MODEL_PATH}";
  yoloDetector.parameters["config_path"] = "${CONFIG_PATH}";
  yoloDetector.parameters["labels_path"] = "${LABELS_PATH}";
  yoloDetector.parameters["score_threshold"] = "${detectionSensitivity}";
  yoloDetector.parameters["nms_threshold"] = "0.4";
  config.pipeline.push_back(yoloDetector);

  // OSD v3 Node
  SolutionConfig::NodeConfig osd;
  osd.nodeType = "osd_v3";
  osd.nodeName = "osd_{instanceId}";
  osd.parameters["labels_path"] = "${LABELS_PATH}";
  config.pipeline.push_back(osd);

  // File Destination Node
  SolutionConfig::NodeConfig fileDes;
  fileDes.nodeType = "file_des";
  fileDes.nodeName = "file_des_{instanceId}";
  fileDes.parameters["save_dir"] = "./output/{instanceId}";
  fileDes.parameters["name_prefix"] = "object_detection";
  fileDes.parameters["osd"] = "true";
  config.pipeline.push_back(fileDes);

  // Default configurations
  config.defaults["detectorMode"] = "SmartDetection";
  config.defaults["detectionSensitivity"] = "0.5";
  config.defaults["sensorModality"] = "RGB";
  config.defaults["RESIZE_RATIO"] = "1.0";

  registerSolution(config);
}

void SolutionRegistry::registerBACrosslineDefaultSolution() {
  SolutionConfig config;
  config.solutionId = "ba_crossline_default";
  config.solutionName =
      "Behavior Analysis - Crossline Detection (Flexible Input/Output)";
  config.solutionType = "behavior_analysis";
  config.isDefault = true;

  // File Source Node
  SolutionConfig::NodeConfig fileSrc;
  fileSrc.nodeType = "file_src";
  fileSrc.nodeName = "file_src_{instanceId}";
  fileSrc.parameters["file_path"] = "${FILE_PATH}";
  fileSrc.parameters["channel"] = "0";
  fileSrc.parameters["resize_ratio"] = "${RESIZE_RATIO}";
  config.pipeline.push_back(fileSrc);

  // YOLO Detector Node
  SolutionConfig::NodeConfig yoloDetector;
  yoloDetector.nodeType = "yolo_detector";
  yoloDetector.nodeName = "yolo_detector_{instanceId}";
  yoloDetector.parameters["weights_path"] = "${WEIGHTS_PATH}";
  yoloDetector.parameters["config_path"] = "${CONFIG_PATH}";
  yoloDetector.parameters["labels_path"] = "${LABELS_PATH}";
  config.pipeline.push_back(yoloDetector);

  // SORT Tracker Node
  SolutionConfig::NodeConfig sortTrack;
  sortTrack.nodeType = "sort_track";
  sortTrack.nodeName = "sort_tracker_{instanceId}";
  config.pipeline.push_back(sortTrack);

  // BA Crossline Node
  SolutionConfig::NodeConfig baCrossline;
  baCrossline.nodeType = "ba_crossline";
  baCrossline.nodeName = "ba_crossline_{instanceId}";
  baCrossline.parameters["line_channel"] = "0";
  baCrossline.parameters["line_start_x"] = "${CROSSLINE_START_X}";
  baCrossline.parameters["line_start_y"] = "${CROSSLINE_START_Y}";
  baCrossline.parameters["line_end_x"] = "${CROSSLINE_END_X}";
  baCrossline.parameters["line_end_y"] = "${CROSSLINE_END_Y}";
  config.pipeline.push_back(baCrossline);

  // JSON Crossline MQTT Broker Node
  SolutionConfig::NodeConfig jsonMqtt;
  jsonMqtt.nodeType = "json_crossline_mqtt_broker";
  jsonMqtt.nodeName = "json_crossline_mqtt_broker_{instanceId}";
  config.pipeline.push_back(jsonMqtt);

  // BA Crossline OSD Node
  SolutionConfig::NodeConfig baOsd;
  baOsd.nodeType = "ba_crossline_osd";
  baOsd.nodeName = "osd_{instanceId}";
  config.pipeline.push_back(baOsd);

  // Screen Destination Node
  SolutionConfig::NodeConfig screenDes;
  screenDes.nodeType = "screen_des";
  screenDes.nodeName = "screen_des_{instanceId}";
  screenDes.parameters["channel"] = "0";
  screenDes.parameters["enabled"] = "${ENABLE_SCREEN_DES}";
  config.pipeline.push_back(screenDes);

  // RTMP Destination Node
  SolutionConfig::NodeConfig rtmpDes;
  rtmpDes.nodeType = "rtmp_des";
  rtmpDes.nodeName = "rtmp_des_{instanceId}";
  rtmpDes.parameters["rtmp_url"] = "${RTMP_URL}";
  rtmpDes.parameters["channel"] = "0";
  config.pipeline.push_back(rtmpDes);

  // Default configurations
  config.defaults["detectorMode"] = "SmartDetection";
  config.defaults["detectionSensitivity"] = "0.7";
  config.defaults["sensorModality"] = "RGB";

  registerSolution(config);
}

void SolutionRegistry::registerBACrosslineMQTTDefaultSolution() {
  SolutionConfig config;
  config.solutionId = "ba_crossline_mqtt_default";
  config.solutionName = "Behavior Analysis - Crossline with MQTT";
  config.solutionType = "behavior_analysis";
  config.isDefault = true;

  // File Source Node
  SolutionConfig::NodeConfig fileSrc;
  fileSrc.nodeType = "file_src";
  fileSrc.nodeName = "file_src_{instanceId}";
  fileSrc.parameters["file_path"] = "${FILE_PATH}";
  fileSrc.parameters["channel"] = "0";
  fileSrc.parameters["resize_ratio"] = "${RESIZE_RATIO}";
  config.pipeline.push_back(fileSrc);

  // YOLO Detector Node
  SolutionConfig::NodeConfig yoloDetector;
  yoloDetector.nodeType = "yolo_detector";
  yoloDetector.nodeName = "yolo_detector_{instanceId}";
  yoloDetector.parameters["weights_path"] = "${WEIGHTS_PATH}";
  yoloDetector.parameters["config_path"] = "${CONFIG_PATH}";
  yoloDetector.parameters["labels_path"] = "${LABELS_PATH}";
  yoloDetector.parameters["score_threshold"] = "${detectionSensitivity}";
  yoloDetector.parameters["nms_threshold"] = "0.4";
  config.pipeline.push_back(yoloDetector);

  // SORT Tracker Node
  SolutionConfig::NodeConfig sortTrack;
  sortTrack.nodeType = "sort_track";
  sortTrack.nodeName = "sort_tracker_{instanceId}";
  config.pipeline.push_back(sortTrack);

  // BA Crossline Node
  SolutionConfig::NodeConfig baCrossline;
  baCrossline.nodeType = "ba_crossline";
  baCrossline.nodeName = "ba_crossline_{instanceId}";
  baCrossline.parameters["line_channel"] = "0";
  baCrossline.parameters["line_start_x"] = "${CROSSLINE_START_X}";
  baCrossline.parameters["line_start_y"] = "${CROSSLINE_START_Y}";
  baCrossline.parameters["line_end_x"] = "${CROSSLINE_END_X}";
  baCrossline.parameters["line_end_y"] = "${CROSSLINE_END_Y}";
  config.pipeline.push_back(baCrossline);

  // JSON Crossline MQTT Broker Node
  SolutionConfig::NodeConfig jsonMqtt;
  jsonMqtt.nodeType = "json_crossline_mqtt_broker";
  jsonMqtt.nodeName = "json_crossline_mqtt_broker_{instanceId}";
  config.pipeline.push_back(jsonMqtt);

  // BA Crossline OSD Node
  SolutionConfig::NodeConfig baOsd;
  baOsd.nodeType = "ba_crossline_osd";
  baOsd.nodeName = "osd_{instanceId}";
  config.pipeline.push_back(baOsd);

  // File Destination Node
  SolutionConfig::NodeConfig fileDes;
  fileDes.nodeType = "file_des";
  fileDes.nodeName = "file_des_{instanceId}";
  fileDes.parameters["save_dir"] = "./output/{instanceId}";
  fileDes.parameters["name_prefix"] = "ba_crossline_mqtt";
  fileDes.parameters["osd"] = "true";
  config.pipeline.push_back(fileDes);

  // Default configurations
  config.defaults["detectorMode"] = "SmartDetection";
  config.defaults["detectionSensitivity"] = "0.7";
  config.defaults["sensorModality"] = "RGB";
  config.defaults["RESIZE_RATIO"] = "1.0";

  registerSolution(config);
}

void SolutionRegistry::registerBAJamDefaultSolution() {
  SolutionConfig config;
  config.solutionId = "ba_jam_default";
  config.solutionName = "Behavior Analysis - Traffic Jam (Default)";
  config.solutionType = "behavior_analysis";
  config.isDefault = true;

  // File Source Node
  SolutionConfig::NodeConfig fileSrc;
  fileSrc.nodeType = "file_src";
  fileSrc.nodeName = "file_src_{instanceId}";
  fileSrc.parameters["file_path"] = "${FILE_PATH}";
  fileSrc.parameters["channel"] = "0";
  fileSrc.parameters["resize_ratio"] = "${RESIZE_RATIO}";
  config.pipeline.push_back(fileSrc);

  // YOLO Detector Node
  SolutionConfig::NodeConfig yoloDetector;
  yoloDetector.nodeType = "yolo_detector";
  yoloDetector.nodeName = "yolo_detector_{instanceId}";
  yoloDetector.parameters["weights_path"] = "${WEIGHTS_PATH}";
  yoloDetector.parameters["config_path"] = "${CONFIG_PATH}";
  yoloDetector.parameters["labels_path"] = "${LABELS_PATH}";
  config.pipeline.push_back(yoloDetector);

  // SORT Tracker Node
  SolutionConfig::NodeConfig sortTrack;
  sortTrack.nodeType = "sort_track";
  sortTrack.nodeName = "sort_tracker_{instanceId}";
  config.pipeline.push_back(sortTrack);

  // BA Jam Node
  SolutionConfig::NodeConfig baJam;
  baJam.nodeType = "ba_jam";
  baJam.nodeName = "ba_jam_{instanceId}";
  baJam.parameters["channel"] = "0";
  baJam.parameters["sensitivity"] = "0.6";
  baJam.parameters["JamZones"] = "${JAM_ZONES_JSON}";
  config.pipeline.push_back(baJam);

  // JSON MQTT Broker Node for jam events
  SolutionConfig::NodeConfig jsonMqtt;
  jsonMqtt.nodeType = "json_crossline_mqtt_broker"; // reuse broker for simplicity
  jsonMqtt.nodeName = "json_jam_mqtt_broker_{instanceId}";
  config.pipeline.push_back(jsonMqtt);

  // BA Jam OSD Node
  SolutionConfig::NodeConfig baJamOSD;
  baJamOSD.nodeType = "ba_jam_osd";
  baJamOSD.nodeName = "osd_{instanceId}";
  config.pipeline.push_back(baJamOSD);

  // File Destination Node
  SolutionConfig::NodeConfig fileDes;
  fileDes.nodeType = "file_des";
  fileDes.nodeName = "file_des_{instanceId}";
  fileDes.parameters["save_dir"] = "./output/{instanceId}";
  fileDes.parameters["name_prefix"] = "ba_jam";
  fileDes.parameters["osd"] = "true";
  config.pipeline.push_back(fileDes);

  // Default configurations
  config.defaults["detectorMode"] = "SmartDetection";
  config.defaults["detectionSensitivity"] = "0.7";
  config.defaults["sensorModality"] = "RGB";

  registerSolution(config);
}

void SolutionRegistry::registerBAJamMQTTDefaultSolution() {
  // For now, reuse the same pipeline as ba_jam_default (broker already included)
  registerBAJamDefaultSolution();
}

void SolutionRegistry::registerYOLOv11DetectionSolution() {
  SolutionConfig config;
  config.solutionId = "yolov11_detection";
  config.solutionName = "YOLOv11 Object Detection";
  config.solutionType = "object_detection";
  config.isDefault = true;

  // File Source Node (supports flexible input: file, RTSP, RTMP, HLS via
  // auto-detection)
  SolutionConfig::NodeConfig fileSrc;
  fileSrc.nodeType = "file_src";
  fileSrc.nodeName = "file_src_{instanceId}";
  // Support both FILE_PATH and RTSP_URL for backward compatibility
  // Pipeline builder will auto-detect input type from FILE_PATH or RTSP_SRC_URL
  fileSrc.parameters["file_path"] =
      "${FILE_PATH}"; // Can be file path or RTSP/RTMP URL
  fileSrc.parameters["channel"] = "0";
  fileSrc.parameters["resize_ratio"] = "1.0";
  config.pipeline.push_back(fileSrc);

  // YOLOv11 Detector Node
  SolutionConfig::NodeConfig yolov11Detector;
  yolov11Detector.nodeType = "yolov11_detector";
  yolov11Detector.nodeName = "detector_{instanceId}";
  yolov11Detector.parameters["model_path"] = "${MODEL_PATH}";
  config.pipeline.push_back(yolov11Detector);

  // File Destination Node
  SolutionConfig::NodeConfig fileDes;
  fileDes.nodeType = "file_des";
  fileDes.nodeName = "destination_{instanceId}";
  fileDes.parameters["save_dir"] = "${SAVE_DIR}";
  fileDes.parameters["name_prefix"] = "yolov11_detection";
  fileDes.parameters["osd"] = "true";
  config.pipeline.push_back(fileDes);

  // Default configurations
  config.defaults["RTSP_URL"] = "rtsp://localhost:8554/stream";
  config.defaults["MODEL_PATH"] = "/opt/edgeos-api/models/yolov11/yolov11n.onnx";
  config.defaults["SAVE_DIR"] = "/tmp/output";
  config.defaults["detectorMode"] = "SmartDetection";
  config.defaults["detectionSensitivity"] = "0.7";
  config.defaults["sensorModality"] = "RGB";

  registerSolution(config);
}

void SolutionRegistry::registerBAStopDefaultSolution() {
  SolutionConfig config;
  config.solutionId = "ba_stop_default";
  config.solutionName = "Behavior Analysis - Stop Detection (Flexible Input/Output)";
  config.solutionType = "behavior_analysis";
  config.isDefault = false;

  // file_src
  SolutionConfig::NodeConfig fileSrc;
  fileSrc.nodeType = "file_src";
  fileSrc.nodeName = "file_src_{instanceId}";
  fileSrc.parameters["file_path"] = "${FILE_PATH}";
  fileSrc.parameters["channel"] = "0";
  fileSrc.parameters["resize_ratio"] = "${RESIZE_RATIO}";
  config.pipeline.push_back(fileSrc);

  // yolo_detector
  SolutionConfig::NodeConfig yoloDetector;
  yoloDetector.nodeType = "yolo_detector";
  yoloDetector.nodeName = "yolo_detector_{instanceId}";
  yoloDetector.parameters["weights_path"] = "${WEIGHTS_PATH}";
  yoloDetector.parameters["config_path"] = "${CONFIG_PATH}";
  yoloDetector.parameters["labels_path"] = "${LABELS_PATH}";
  config.pipeline.push_back(yoloDetector);

  // sort_track
  SolutionConfig::NodeConfig sortTrack;
  sortTrack.nodeType = "sort_track";
  sortTrack.nodeName = "sort_tracker_{instanceId}";
  config.pipeline.push_back(sortTrack);

  // ba_stop
  SolutionConfig::NodeConfig baStop;
  baStop.nodeType = "ba_stop";
  baStop.nodeName = "ba_stop_{instanceId}";
  baStop.parameters["min_stop_seconds"] = "${MIN_STOP_SECONDS}";
  baStop.parameters["StopZones"] = "${STOP_ZONES_JSON}";
  config.pipeline.push_back(baStop);

  // json_mqtt_broker
  SolutionConfig::NodeConfig jsonMqtt;
  jsonMqtt.nodeType = "json_mqtt_broker";
  jsonMqtt.nodeName = "json_mqtt_broker_{instanceId}";
  config.pipeline.push_back(jsonMqtt);

  // ba_stop_osd
  SolutionConfig::NodeConfig baStopOSD;
  baStopOSD.nodeType = "ba_stop_osd";
  baStopOSD.nodeName = "osd_{instanceId}";
  config.pipeline.push_back(baStopOSD);

  // screen_des
  SolutionConfig::NodeConfig screenDes;
  screenDes.nodeType = "screen_des";
  screenDes.nodeName = "screen_des_{instanceId}";
  screenDes.parameters["channel"] = "0";
  screenDes.parameters["enabled"] = "${ENABLE_SCREEN_DES}";
  config.pipeline.push_back(screenDes);

  // rtmp_des
  SolutionConfig::NodeConfig rtmpDes;
  rtmpDes.nodeType = "rtmp_des";
  rtmpDes.nodeName = "rtmp_des_{instanceId}";
  rtmpDes.parameters["rtmp_url"] = "${RTMP_URL}";
  rtmpDes.parameters["channel"] = "0";
  config.pipeline.push_back(rtmpDes);

  // Detection tuning is specified per-zone (StopZones/JamZones); no instance-level defaults provided
  (void)0;

  registerSolution(config);
}

void SolutionRegistry::registerBAStopMQTTDefaultSolution() {
  SolutionConfig config;
  config.solutionId = "ba_stop_mqtt_default";
  config.solutionName = "Behavior Analysis - Stop Detection with MQTT";
  config.solutionType = "behavior_analysis";
  config.isDefault = false;

  // file_src
  SolutionConfig::NodeConfig fileSrc;
  fileSrc.nodeType = "file_src";
  fileSrc.nodeName = "file_src_{instanceId}";
  fileSrc.parameters["file_path"] = "${FILE_PATH}";
  fileSrc.parameters["channel"] = "0";
  fileSrc.parameters["resize_ratio"] = "${RESIZE_RATIO}";
  config.pipeline.push_back(fileSrc);

  // yolo_detector
  SolutionConfig::NodeConfig yoloDetector;
  yoloDetector.nodeType = "yolo_detector";
  yoloDetector.nodeName = "yolo_detector_{instanceId}";
  yoloDetector.parameters["weights_path"] = "${WEIGHTS_PATH}";
  yoloDetector.parameters["config_path"] = "${CONFIG_PATH}";
  yoloDetector.parameters["labels_path"] = "${LABELS_PATH}";
  config.pipeline.push_back(yoloDetector);

  // sort_track
  SolutionConfig::NodeConfig sortTrack;
  sortTrack.nodeType = "sort_track";
  sortTrack.nodeName = "sort_tracker_{instanceId}";
  config.pipeline.push_back(sortTrack);

  // ba_stop
  SolutionConfig::NodeConfig baStop2;
  baStop2.nodeType = "ba_stop";
  baStop2.nodeName = "ba_stop_{instanceId}";
  baStop2.parameters["min_stop_seconds"] = "${MIN_STOP_SECONDS}";
  baStop2.parameters["StopZones"] = "${STOP_ZONES_JSON}";
  config.pipeline.push_back(baStop2);

  // json_mqtt_broker
  SolutionConfig::NodeConfig jsonMqtt2;
  jsonMqtt2.nodeType = "json_mqtt_broker";
  jsonMqtt2.nodeName = "json_mqtt_broker_{instanceId}";
  config.pipeline.push_back(jsonMqtt2);

  // ba_stop_osd
  SolutionConfig::NodeConfig baStopOSD2;
  baStopOSD2.nodeType = "ba_stop_osd";
  baStopOSD2.nodeName = "osd_{instanceId}";
  config.pipeline.push_back(baStopOSD2);

  config.defaults["MIN_STOP_SECONDS"] = "3";

  registerSolution(config);
}

void SolutionRegistry::registerBALoiteringSolution() {
  SolutionConfig config;
  config.solutionId = "ba_loitering";
  config.solutionName = "Behavior Analysis - Loitering Detection";
  config.solutionType = "behavior_analysis";
  config.isDefault = true;

  // File Source Node
  SolutionConfig::NodeConfig fileSrc;
  fileSrc.nodeType = "file_src";
  fileSrc.nodeName = "file_src_{instanceId}";
  fileSrc.parameters["file_path"] = "${FILE_PATH}";
  fileSrc.parameters["channel"] = "0";
  fileSrc.parameters["resize_ratio"] = "${RESIZE_RATIO}";
  config.pipeline.push_back(fileSrc);

  // YOLO Detector Node
  SolutionConfig::NodeConfig yoloDetector;
  yoloDetector.nodeType = "yolo_detector";
  yoloDetector.nodeName = "yolo_detector_{instanceId}";
  yoloDetector.parameters["weights_path"] = "${WEIGHTS_PATH}";
  yoloDetector.parameters["config_path"] = "${CONFIG_PATH}";
  yoloDetector.parameters["labels_path"] = "${LABELS_PATH}";
  config.pipeline.push_back(yoloDetector);

  // SORT Tracker Node
  SolutionConfig::NodeConfig sortTrack;
  sortTrack.nodeType = "sort_track";
  sortTrack.nodeName = "sort_tracker_{instanceId}";
  config.pipeline.push_back(sortTrack);

  // BA Loitering Node
  SolutionConfig::NodeConfig baLoitering;
  baLoitering.nodeType = "ba_loitering";
  baLoitering.nodeName = "ba_loitering_{instanceId}";
  baLoitering.parameters["LoiteringAreas"] = "${LOITERING_AREAS_JSON}";
  baLoitering.parameters["alarm_seconds"] = "${ALARM_SECONDS}";
  baLoitering.parameters["check_interval"] = "${CHECK_INTERVAL}";
  config.pipeline.push_back(baLoitering);

  // BA Stop OSD Node (ba_loitering uses ba_stop_osd_node)
  SolutionConfig::NodeConfig baLoiteringOSD;
  baLoiteringOSD.nodeType = "ba_loitering_osd";
  baLoiteringOSD.nodeName = "osd_{instanceId}";
  config.pipeline.push_back(baLoiteringOSD);

  // NOTE: json_mqtt_broker node removed - it's disabled due to crash issues
  // MQTT events are handled by instance_registry instead

  // Screen Destination Node
  SolutionConfig::NodeConfig screenDes;
  screenDes.nodeType = "screen_des";
  screenDes.nodeName = "screen_des_{instanceId}";
  screenDes.parameters["channel"] = "0";
  screenDes.parameters["enabled"] = "${ENABLE_SCREEN_DES}";
  config.pipeline.push_back(screenDes);

  // File Destination Node
  SolutionConfig::NodeConfig fileDes;
  fileDes.nodeType = "file_des";
  fileDes.nodeName = "file_des_{instanceId}";
  fileDes.parameters["save_dir"] = "./output/{instanceId}";
  fileDes.parameters["name_prefix"] = "ba_loitering";
  fileDes.parameters["osd"] = "true";
  config.pipeline.push_back(fileDes);

  // Default configurations
  config.defaults["detectorMode"] = "SmartDetection";
  config.defaults["detectionSensitivity"] = "0.7";
  config.defaults["sensorModality"] = "RGB";
  config.defaults["RESIZE_RATIO"] = "0.6";
  config.defaults["CHECK_INTERVAL"] = "30";

  registerSolution(config);
}

void SolutionRegistry::registerBAAreaEnterExitSolution() {
  SolutionConfig config;
  config.solutionId = "ba_area_enter_exit";
  config.solutionName = "Behavior Analysis - Area Enter/Exit Detection";
  config.solutionType = "behavior_analysis";
  config.isDefault = true;

  // File Source Node
  SolutionConfig::NodeConfig fileSrc;
  fileSrc.nodeType = "file_src";
  fileSrc.nodeName = "file_src_{instanceId}";
  fileSrc.parameters["file_path"] = "${FILE_PATH}";
  fileSrc.parameters["channel"] = "0";
  fileSrc.parameters["resize_ratio"] = "${RESIZE_RATIO}";
  config.pipeline.push_back(fileSrc);

  // YOLO Detector Node
  SolutionConfig::NodeConfig yoloDetector;
  yoloDetector.nodeType = "yolo_detector";
  yoloDetector.nodeName = "yolo_detector_{instanceId}";
  yoloDetector.parameters["weights_path"] = "${WEIGHTS_PATH}";
  yoloDetector.parameters["config_path"] = "${CONFIG_PATH}";
  yoloDetector.parameters["labels_path"] = "${LABELS_PATH}";
  config.pipeline.push_back(yoloDetector);

  // ByteTrack Tracker Node
  SolutionConfig::NodeConfig byteTrack;
  byteTrack.nodeType = "bytetrack";
  byteTrack.nodeName = "bytetrack_{instanceId}";
  config.pipeline.push_back(byteTrack);

  // BA Area Enter/Exit Node
  SolutionConfig::NodeConfig baAreaEnterExit;
  baAreaEnterExit.nodeType = "ba_area_enter_exit";
  baAreaEnterExit.nodeName = "ba_area_enter_exit_{instanceId}";
  baAreaEnterExit.parameters["Areas"] = "${AREAS_JSON}";
  baAreaEnterExit.parameters["AreaConfigs"] = "${AREA_CONFIGS_JSON}";
  config.pipeline.push_back(baAreaEnterExit);

  // BA Area Enter/Exit OSD Node
  SolutionConfig::NodeConfig baAreaOSD;
  baAreaOSD.nodeType = "ba_area_enter_exit_osd";
  baAreaOSD.nodeName = "osd_{instanceId}";
  config.pipeline.push_back(baAreaOSD);

  // Screen Destination Node
  SolutionConfig::NodeConfig screenDes;
  screenDes.nodeType = "screen_des";
  screenDes.nodeName = "screen_des_{instanceId}";
  screenDes.parameters["channel"] = "0";
  screenDes.parameters["enabled"] = "${ENABLE_SCREEN_DES}";
  config.pipeline.push_back(screenDes);

  // RTMP Destination Node (optional)
  SolutionConfig::NodeConfig rtmpDes;
  rtmpDes.nodeType = "rtmp_des";
  rtmpDes.nodeName = "rtmp_des_{instanceId}";
  rtmpDes.parameters["rtmp_url"] = "${RTMP_DES_URL}";
  rtmpDes.parameters["channel"] = "0";
  config.pipeline.push_back(rtmpDes);

  // Default configurations
  config.defaults["detectorMode"] = "SmartDetection";
  config.defaults["detectionSensitivity"] = "0.7";
  config.defaults["sensorModality"] = "RGB";
  config.defaults["RESIZE_RATIO"] = "0.6";

  registerSolution(config);
}

void SolutionRegistry::registerBALineCountingSolution() {
  SolutionConfig config;
  config.solutionId = "ba_line_counting";
  config.solutionName = "Behavior Analysis - Multiple Line Counting";
  config.solutionType = "behavior_analysis";
  config.isDefault = true;

  // File Source Node
  SolutionConfig::NodeConfig fileSrc;
  fileSrc.nodeType = "file_src";
  fileSrc.nodeName = "file_src_{instanceId}";
  fileSrc.parameters["file_path"] = "${FILE_PATH}";
  fileSrc.parameters["channel"] = "0";
  fileSrc.parameters["resize_ratio"] = "${RESIZE_RATIO}";
  config.pipeline.push_back(fileSrc);

  // YOLO Detector Node
  SolutionConfig::NodeConfig yoloDetector;
  yoloDetector.nodeType = "yolo_detector";
  yoloDetector.nodeName = "yolo_detector_{instanceId}";
  yoloDetector.parameters["weights_path"] = "${WEIGHTS_PATH}";
  yoloDetector.parameters["config_path"] = "${CONFIG_PATH}";
  yoloDetector.parameters["labels_path"] = "${LABELS_PATH}";
  config.pipeline.push_back(yoloDetector);

  // ByteTrack Tracker Node
  SolutionConfig::NodeConfig byteTrack;
  byteTrack.nodeType = "bytetrack";
  byteTrack.nodeName = "bytetrack_{instanceId}";
  config.pipeline.push_back(byteTrack);

  // BA Line Counting Node (multiple lines with direction)
  SolutionConfig::NodeConfig baLineCounting;
  baLineCounting.nodeType = "ba_line_counting";
  baLineCounting.nodeName = "ba_line_counting_{instanceId}";
  baLineCounting.parameters["LineSettings"] = "${LINE_SETTINGS_JSON}";
  config.pipeline.push_back(baLineCounting);

  // BA Crossline OSD Node (reuse for visualization)
  SolutionConfig::NodeConfig baCrosslineOSD;
  baCrosslineOSD.nodeType = "ba_crossline_osd";
  baCrosslineOSD.nodeName = "osd_{instanceId}";
  config.pipeline.push_back(baCrosslineOSD);

  // Screen Destination Node
  SolutionConfig::NodeConfig screenDes;
  screenDes.nodeType = "screen_des";
  screenDes.nodeName = "screen_des_{instanceId}";
  screenDes.parameters["channel"] = "0";
  screenDes.parameters["enabled"] = "${ENABLE_SCREEN_DES}";
  config.pipeline.push_back(screenDes);

  // RTMP Destination Node (optional)
  SolutionConfig::NodeConfig rtmpDes;
  rtmpDes.nodeType = "rtmp_des";
  rtmpDes.nodeName = "rtmp_des_{instanceId}";
  rtmpDes.parameters["rtmp_url"] = "${RTMP_DES_URL}";
  rtmpDes.parameters["channel"] = "0";
  config.pipeline.push_back(rtmpDes);

  // Default configurations
  config.defaults["detectorMode"] = "SmartDetection";
  config.defaults["detectionSensitivity"] = "0.7";
  config.defaults["sensorModality"] = "RGB";
  config.defaults["RESIZE_RATIO"] = "0.4";

  registerSolution(config);
}

void SolutionRegistry::registerBACrowdingSolution() {
  SolutionConfig config;
  config.solutionId = "ba_crowding";
  config.solutionName = "Behavior Analysis - Crowding Detection";
  config.solutionType = "behavior_analysis";
  config.isDefault = true;

  // File Source Node
  SolutionConfig::NodeConfig fileSrc;
  fileSrc.nodeType = "file_src";
  fileSrc.nodeName = "file_src_{instanceId}";
  fileSrc.parameters["file_path"] = "${FILE_PATH}";
  fileSrc.parameters["channel"] = "0";
  fileSrc.parameters["resize_ratio"] = "${RESIZE_RATIO}";
  config.pipeline.push_back(fileSrc);

  // YOLO Detector Node
  SolutionConfig::NodeConfig yoloDetector;
  yoloDetector.nodeType = "yolo_detector";
  yoloDetector.nodeName = "yolo_detector_{instanceId}";
  yoloDetector.parameters["weights_path"] = "${WEIGHTS_PATH}";
  yoloDetector.parameters["config_path"] = "${CONFIG_PATH}";
  yoloDetector.parameters["labels_path"] = "${LABELS_PATH}";
  config.pipeline.push_back(yoloDetector);

  // SORT Tracker Node
  SolutionConfig::NodeConfig sortTrack;
  sortTrack.nodeType = "sort_track";
  sortTrack.nodeName = "sort_tracker_{instanceId}";
  config.pipeline.push_back(sortTrack);

  // BA Crowding Node
  SolutionConfig::NodeConfig baCrowding;
  baCrowding.nodeType = "ba_crowding";
  baCrowding.nodeName = "ba_crowding_{instanceId}";
  baCrowding.parameters["CrowdingZones"] = "${CROWDING_ZONES_JSON}";
  baCrowding.parameters["check_interval"] = "${CROWDING_CHECK_INTERVAL}";
  config.pipeline.push_back(baCrowding);

  // BA Crowding OSD Node
  SolutionConfig::NodeConfig baCrowdingOSD;
  baCrowdingOSD.nodeType = "ba_crowding_osd";
  baCrowdingOSD.nodeName = "ba_crowding_osd_{instanceId}";
  config.pipeline.push_back(baCrowdingOSD);

  // File Destination Node
  SolutionConfig::NodeConfig fileDes;
  fileDes.nodeType = "file_des";
  fileDes.nodeName = "file_des_{instanceId}";
  fileDes.parameters["save_dir"] = "./output/{instanceId}";
  fileDes.parameters["name_prefix"] = "ba_crowding";
  fileDes.parameters["osd"] = "true";
  config.pipeline.push_back(fileDes);

  // Default configurations
  config.defaults["detectorMode"] = "SmartDetection";
  config.defaults["detectionSensitivity"] = "0.5";
  config.defaults["sensorModality"] = "RGB";
  config.defaults["RESIZE_RATIO"] = "1.0";
  config.defaults["CROWDING_CHECK_INTERVAL"] = "30";

  registerSolution(config);
}

void SolutionRegistry::registerSecuRTSolution() {
  SolutionConfig config;
  config.solutionId = "securt";
  config.solutionName = "SecuRT Solution";
  config.solutionType = "securt";
  config.isDefault = true;

  // File Source Node (supports flexible input: file, RTSP, RTMP, HLS via
  // auto-detection)
  SolutionConfig::NodeConfig fileSrc;
  fileSrc.nodeType = "file_src";
  fileSrc.nodeName = "file_src_{instanceId}";
  fileSrc.parameters["file_path"] =
      "${FILE_PATH}"; // Can be file path or RTSP/RTMP URL
  fileSrc.parameters["channel"] = "0";
  fileSrc.parameters["resize_ratio"] = "${RESIZE_RATIO}";
  config.pipeline.push_back(fileSrc);

  // YOLO Detector Node (for object detection)
  SolutionConfig::NodeConfig yoloDetector;
  yoloDetector.nodeType = "yolo_detector";
  yoloDetector.nodeName = "yolo_detector_{instanceId}";
  yoloDetector.parameters["weights_path"] = "${WEIGHTS_PATH}";
  yoloDetector.parameters["config_path"] = "${CONFIG_PATH}";
  yoloDetector.parameters["labels_path"] = "${LABELS_PATH}";
  yoloDetector.parameters["score_threshold"] = "${detectionSensitivity}";
  yoloDetector.parameters["nms_threshold"] = "0.4";
  config.pipeline.push_back(yoloDetector);

  // SORT Tracker Node (for object tracking)
  SolutionConfig::NodeConfig sortTrack;
  sortTrack.nodeType = "sort_track";
  sortTrack.nodeName = "sort_tracker_{instanceId}";
  config.pipeline.push_back(sortTrack);

  // BA Crossline Node (for line-based analytics - counting, crossing, tailgating)
  // Lines will be passed via CrossingLines parameter from SecuRT lines
  SolutionConfig::NodeConfig baCrossline;
  baCrossline.nodeType = "ba_crossline";
  baCrossline.nodeName = "ba_crossline_{instanceId}";
  // Lines will be dynamically set from SecuRT lines via additionalParams["CrossingLines"]
  baCrossline.parameters["line_channel"] = "0";
  baCrossline.parameters["line_start_x"] = "${CROSSLINE_START_X}";
  baCrossline.parameters["line_start_y"] = "${CROSSLINE_START_Y}";
  baCrossline.parameters["line_end_x"] = "${CROSSLINE_END_X}";
  baCrossline.parameters["line_end_y"] = "${CROSSLINE_END_Y}";
  config.pipeline.push_back(baCrossline);

  // BA Crossline OSD Node (for visualization)
  SolutionConfig::NodeConfig baCrosslineOSD;
  baCrosslineOSD.nodeType = "ba_crossline_osd";
  baCrosslineOSD.nodeName = "osd_{instanceId}";
  config.pipeline.push_back(baCrosslineOSD);

  // File Destination Node
  SolutionConfig::NodeConfig fileDes;
  fileDes.nodeType = "file_des";
  fileDes.nodeName = "file_des_{instanceId}";
  fileDes.parameters["save_dir"] = "./output/{instanceId}";
  fileDes.parameters["name_prefix"] = "securt";
  fileDes.parameters["osd"] = "true";
  config.pipeline.push_back(fileDes);

  // Default configurations
  config.defaults["detectorMode"] = "SmartDetection";
  config.defaults["detectionSensitivity"] = "Medium";
  config.defaults["movementSensitivity"] = "Medium";
  config.defaults["sensorModality"] = "RGB";
  config.defaults["RESIZE_RATIO"] = "1.0";

  registerSolution(config);
}

void SolutionRegistry::registerFireSmokeDetectionSolution() {
  SolutionConfig config;
  config.solutionId = "fire_smoke_detection";
  config.solutionName = "Fire/Smoke Detection";
  config.solutionType = "object_detection";
  config.isDefault = true;

  // File Source Node
  SolutionConfig::NodeConfig fileSrc;
  fileSrc.nodeType = "file_src";
  fileSrc.nodeName = "file_src_{instanceId}";
  fileSrc.parameters["file_path"] = "${FILE_PATH}";
  fileSrc.parameters["channel"] = "0";
  fileSrc.parameters["resize_ratio"] = "${RESIZE_RATIO}";
  config.pipeline.push_back(fileSrc);

  // YOLO Detector Node (for fire/smoke detection)
  SolutionConfig::NodeConfig yoloDetector;
  yoloDetector.nodeType = "yolo_detector";
  yoloDetector.nodeName = "fire_smoke_detector_{instanceId}";
  yoloDetector.parameters["weights_path"] = "${MODEL_PATH}";
  yoloDetector.parameters["config_path"] = "${CONFIG_PATH}";
  yoloDetector.parameters["labels_path"] = "${LABELS_PATH}";
  yoloDetector.parameters["score_threshold"] = "${detectionSensitivity}";
  yoloDetector.parameters["nms_threshold"] = "0.4";
  config.pipeline.push_back(yoloDetector);

  // OSD v3 Node
  SolutionConfig::NodeConfig osd;
  osd.nodeType = "osd_v3";
  osd.nodeName = "osd_{instanceId}";
  osd.parameters["labels_path"] = "${LABELS_PATH}";
  config.pipeline.push_back(osd);

  // File Destination Node
  SolutionConfig::NodeConfig fileDes;
  fileDes.nodeType = "file_des";
  fileDes.nodeName = "file_des_{instanceId}";
  fileDes.parameters["save_dir"] = "./output/{instanceId}";
  fileDes.parameters["name_prefix"] = "fire_smoke_detection";
  fileDes.parameters["osd"] = "true";
  config.pipeline.push_back(fileDes);

  // Default configurations
  config.defaults["detectorMode"] = "SmartDetection";
  config.defaults["detectionSensitivity"] = "0.5";
  config.defaults["sensorModality"] = "RGB";
  config.defaults["RESIZE_RATIO"] = "1.0";

  registerSolution(config);
}

void SolutionRegistry::registerObstacleDetectionSolution() {
  SolutionConfig config;
  config.solutionId = "obstacle_detection";
  config.solutionName = "Obstacle Detection";
  config.solutionType = "object_detection";
  config.isDefault = true;

  // File Source Node
  SolutionConfig::NodeConfig fileSrc;
  fileSrc.nodeType = "file_src";
  fileSrc.nodeName = "file_src_{instanceId}";
  fileSrc.parameters["file_path"] = "${FILE_PATH}";
  fileSrc.parameters["channel"] = "0";
  fileSrc.parameters["resize_ratio"] = "${RESIZE_RATIO}";
  config.pipeline.push_back(fileSrc);

  // YOLO Detector Node (for obstacle detection)
  SolutionConfig::NodeConfig yoloDetector;
  yoloDetector.nodeType = "yolo_detector";
  yoloDetector.nodeName = "obstacle_detector_{instanceId}";
  yoloDetector.parameters["weights_path"] = "${MODEL_PATH}";
  yoloDetector.parameters["config_path"] = "${CONFIG_PATH}";
  yoloDetector.parameters["labels_path"] = "${LABELS_PATH}";
  yoloDetector.parameters["score_threshold"] = "${detectionSensitivity}";
  yoloDetector.parameters["nms_threshold"] = "0.4";
  config.pipeline.push_back(yoloDetector);

  // OSD v3 Node
  SolutionConfig::NodeConfig osd;
  osd.nodeType = "osd_v3";
  osd.nodeName = "osd_{instanceId}";
  osd.parameters["labels_path"] = "${LABELS_PATH}";
  config.pipeline.push_back(osd);

  // File Destination Node
  SolutionConfig::NodeConfig fileDes;
  fileDes.nodeType = "file_des";
  fileDes.nodeName = "file_des_{instanceId}";
  fileDes.parameters["save_dir"] = "./output/{instanceId}";
  fileDes.parameters["name_prefix"] = "obstacle_detection";
  fileDes.parameters["osd"] = "true";
  config.pipeline.push_back(fileDes);

  // Default configurations
  config.defaults["detectorMode"] = "SmartDetection";
  config.defaults["detectionSensitivity"] = "0.5";
  config.defaults["sensorModality"] = "RGB";
  config.defaults["RESIZE_RATIO"] = "1.0";

  registerSolution(config);
}

void SolutionRegistry::registerWrongWayDetectionSolution() {
  SolutionConfig config;
  config.solutionId = "wrong_way_detection";
  config.solutionName = "Wrong Way Detection";
  config.solutionType = "object_detection";
  config.isDefault = true;

  // File Source Node
  SolutionConfig::NodeConfig fileSrc;
  fileSrc.nodeType = "file_src";
  fileSrc.nodeName = "file_src_{instanceId}";
  fileSrc.parameters["file_path"] = "${FILE_PATH}";
  fileSrc.parameters["channel"] = "0";
  fileSrc.parameters["resize_ratio"] = "${RESIZE_RATIO}";
  config.pipeline.push_back(fileSrc);

  // YOLO Detector Node (for vehicle detection)
  SolutionConfig::NodeConfig yoloDetector;
  yoloDetector.nodeType = "yolo_detector";
  yoloDetector.nodeName = "vehicle_detector_{instanceId}";
  yoloDetector.parameters["weights_path"] = "${MODEL_PATH}";
  yoloDetector.parameters["config_path"] = "${CONFIG_PATH}";
  yoloDetector.parameters["labels_path"] = "${LABELS_PATH}";
  yoloDetector.parameters["score_threshold"] = "${detectionSensitivity}";
  yoloDetector.parameters["nms_threshold"] = "0.4";
  config.pipeline.push_back(yoloDetector);

  // SORT Tracker Node (for tracking vehicles)
  SolutionConfig::NodeConfig sortTrack;
  sortTrack.nodeType = "sort_track";
  sortTrack.nodeName = "vehicle_tracker_{instanceId}";
  config.pipeline.push_back(sortTrack);

  // OSD v3 Node
  SolutionConfig::NodeConfig osd;
  osd.nodeType = "osd_v3";
  osd.nodeName = "osd_{instanceId}";
  osd.parameters["labels_path"] = "${LABELS_PATH}";
  config.pipeline.push_back(osd);

  // File Destination Node
  SolutionConfig::NodeConfig fileDes;
  fileDes.nodeType = "file_des";
  fileDes.nodeName = "file_des_{instanceId}";
  fileDes.parameters["save_dir"] = "./output/{instanceId}";
  fileDes.parameters["name_prefix"] = "wrong_way_detection";
  fileDes.parameters["osd"] = "true";
  config.pipeline.push_back(fileDes);

  // Default configurations
  config.defaults["detectorMode"] = "SmartDetection";
  config.defaults["detectionSensitivity"] = "0.5";
  config.defaults["sensorModality"] = "RGB";
  config.defaults["RESIZE_RATIO"] = "1.0";

  registerSolution(config);
}
