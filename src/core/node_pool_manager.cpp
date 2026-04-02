#include "core/node_pool_manager.h"
#include "core/node_storage.h"
#include "core/node_template_registry.h"
#include "core/pipeline_builder.h"
#include "models/create_instance_request.h"
#include "solutions/solution_registry.h"
#include <chrono>
#include <iomanip>
#include <iostream>
#include <optional>
#include <random>
#include <set>
#include <sstream>

NodePoolManager &NodePoolManager::getInstance() {
  static NodePoolManager instance;
  return instance;
}

void NodePoolManager::initializeDefaultTemplates() {
  std::unique_lock<std::shared_timed_mutex> lock(mutex_);

  // Step 1: Import templates from SDK node types registry
  // This automatically imports all node types supported in PipelineBuilder
  auto importedTemplates = NodeTemplateRegistry::importTemplatesFromSDK();
  for (const auto &template_ : importedTemplates) {
    templates_[template_.templateId] = template_;
  }

  std::cerr << "[NodePoolManager] Imported " << importedTemplates.size()
            << " node templates from SDK registry" << std::endl;

  // Step 2: Override with manual templates for special cases or enhanced
  // defaults These templates may have more detailed default parameters or
  // special configurations
  // ========== SOURCE NODES ==========

  // RTSP Source
  NodeTemplate rtspSrc;
  rtspSrc.templateId = "rtsp_src_template";
  rtspSrc.nodeType = "rtsp_src";
  rtspSrc.displayName = "RTSP Source";
  rtspSrc.description = "Receive video stream from RTSP URL";
  rtspSrc.category = "source";
  rtspSrc.defaultParameters["channel"] = "0";
  rtspSrc.defaultParameters["resize_ratio"] = "1.0";
  rtspSrc.requiredParameters = {"rtsp_url"};
  rtspSrc.optionalParameters = {"channel", "resize_ratio"};
  rtspSrc.isPreConfigured = false;
  templates_[rtspSrc.templateId] = rtspSrc;

  // File Source
  NodeTemplate fileSrc;
  fileSrc.templateId = "file_src_template";
  fileSrc.nodeType = "file_src";
  fileSrc.displayName = "File Source";
  fileSrc.description = "Read video from file";
  fileSrc.category = "source";
  fileSrc.defaultParameters["channel"] = "0";
  fileSrc.defaultParameters["resize_ratio"] = "1.0";
  fileSrc.requiredParameters = {"file_path"};
  fileSrc.optionalParameters = {"channel", "resize_ratio"};
  fileSrc.isPreConfigured = false;
  templates_[fileSrc.templateId] = fileSrc;

  // ========== DETECTOR NODES ==========

  // YOLO Detector
  NodeTemplate yoloDetector;
  yoloDetector.templateId = "yolo_detector_template";
  yoloDetector.nodeType = "yolo_detector";
  yoloDetector.displayName = "YOLO Detector";
  yoloDetector.description = "Object detection using YOLO";
  yoloDetector.category = "detector";
  yoloDetector.requiredParameters = {"weights_path", "config_path"};
  yoloDetector.optionalParameters = {"labels_path"};
  yoloDetector.isPreConfigured = false;
  templates_[yoloDetector.templateId] = yoloDetector;

  // ========== PROCESSOR NODES ==========

  // SFace Feature Encoder
  NodeTemplate sfaceEncoder;
  sfaceEncoder.templateId = "sface_feature_encoder_template";
  sfaceEncoder.nodeType = "sface_feature_encoder";
  sfaceEncoder.displayName = "SFace Feature Encoder";
  sfaceEncoder.description = "Extract face features using SFace";
  sfaceEncoder.category = "processor";
  sfaceEncoder.requiredParameters = {"model_path"};
  sfaceEncoder.isPreConfigured = false;
  templates_[sfaceEncoder.templateId] = sfaceEncoder;

  // SORT Tracker
  NodeTemplate sortTrack;
  sortTrack.templateId = "sort_track_template";
  sortTrack.nodeType = "sort_track";
  sortTrack.displayName = "SORT Tracker";
  sortTrack.description = "Track objects using SORT algorithm";
  sortTrack.category = "processor";
  sortTrack.isPreConfigured = true; // No parameters needed
  templates_[sortTrack.templateId] = sortTrack;

  // ========== DESTINATION NODES ==========

  // File Destination
  NodeTemplate fileDes;
  fileDes.templateId = "file_des_template";
  fileDes.nodeType = "file_des";
  fileDes.displayName = "File Destination";
  fileDes.description = "Save video to file";
  fileDes.category = "destination";
  fileDes.defaultParameters["osd"] = "true";
  fileDes.requiredParameters = {"save_dir"};
  fileDes.optionalParameters = {"name_prefix", "osd"};
  fileDes.isPreConfigured = false;
  templates_[fileDes.templateId] = fileDes;

  // RTMP Destination
  NodeTemplate rtmpDes;
  rtmpDes.templateId = "rtmp_des_template";
  rtmpDes.nodeType = "rtmp_des";
  rtmpDes.displayName = "RTMP Destination";
  rtmpDes.description = "Stream video via RTMP";
  rtmpDes.category = "destination";
  rtmpDes.defaultParameters["channel"] = "0";
  rtmpDes.requiredParameters = {"rtmp_url"};
  rtmpDes.optionalParameters = {"channel"};
  rtmpDes.isPreConfigured = false;
  templates_[rtmpDes.templateId] = rtmpDes;

  // Screen Destination
  NodeTemplate screenDes;
  screenDes.templateId = "screen_des_template";
  screenDes.nodeType = "screen_des";
  screenDes.displayName = "Screen Destination";
  screenDes.description = "Display video on screen";
  screenDes.category = "destination";
  screenDes.isPreConfigured = true; // No required parameters
  templates_[screenDes.templateId] = screenDes;

  // ========== ADDITIONAL SOURCE NODES ==========

  // App Source
  NodeTemplate appSrc;
  appSrc.templateId = "app_src_template";
  appSrc.nodeType = "app_src";
  appSrc.displayName = "App Source";
  appSrc.description = "Receive video frames from application";
  appSrc.category = "source";
  appSrc.defaultParameters["channel"] = "0";
  appSrc.requiredParameters = {}; // No required parameters
  appSrc.optionalParameters = {"channel"};
  appSrc.isPreConfigured = true;
  templates_[appSrc.templateId] = appSrc;

  // Image Source
  NodeTemplate imageSrc;
  imageSrc.templateId = "image_src_template";
  imageSrc.nodeType = "image_src";
  imageSrc.displayName = "Image Source";
  imageSrc.description = "Read images from file or UDP port";
  imageSrc.category = "source";
  imageSrc.defaultParameters["interval"] = "1";
  imageSrc.defaultParameters["resize_ratio"] = "1.0";
  imageSrc.defaultParameters["cycle"] = "true";
  imageSrc.requiredParameters = {"port_or_location"};
  imageSrc.optionalParameters = {"interval", "resize_ratio", "cycle"};
  imageSrc.isPreConfigured = false;
  templates_[imageSrc.templateId] = imageSrc;

  // RTMP Source
  NodeTemplate rtmpSrc;
  rtmpSrc.templateId = "rtmp_src_template";
  rtmpSrc.nodeType = "rtmp_src";
  rtmpSrc.displayName = "RTMP Source";
  rtmpSrc.description = "Receive video stream from RTMP URL";
  rtmpSrc.category = "source";
  rtmpSrc.defaultParameters["channel"] = "0";
  rtmpSrc.defaultParameters["resize_ratio"] = "1.0";
  rtmpSrc.defaultParameters["skip_interval"] = "0";
  rtmpSrc.requiredParameters = {"rtmp_url"};
  rtmpSrc.optionalParameters = {"channel", "resize_ratio", "skip_interval",
                                "gst_decoder_name"};
  rtmpSrc.isPreConfigured = false;
  templates_[rtmpSrc.templateId] = rtmpSrc;

  // UDP Source
  NodeTemplate udpSrc;
  udpSrc.templateId = "udp_src_template";
  udpSrc.nodeType = "udp_src";
  udpSrc.displayName = "UDP Source";
  udpSrc.description = "Receive video stream via UDP";
  udpSrc.category = "source";
  udpSrc.defaultParameters["resize_ratio"] = "1.0";
  udpSrc.defaultParameters["skip_interval"] = "0";
  udpSrc.requiredParameters = {"port"};
  udpSrc.optionalParameters = {"resize_ratio", "skip_interval"};
  udpSrc.isPreConfigured = false;
  templates_[udpSrc.templateId] = udpSrc;

  // ========== ADDITIONAL PROCESSOR NODES ==========

  // Face OSD v2
  NodeTemplate faceOSD;
  faceOSD.templateId = "face_osd_v2_template";
  faceOSD.nodeType = "face_osd_v2";
  faceOSD.displayName = "Face OSD v2";
  faceOSD.description = "Overlay face detection results";
  faceOSD.category = "processor";
  faceOSD.isPreConfigured = true; // No parameters needed
  templates_[faceOSD.templateId] = faceOSD;

  // OSD v3 (for masks and labels)
  NodeTemplate osdV3;
  osdV3.templateId = "osd_v3_template";
  osdV3.nodeType = "osd_v3";
  osdV3.displayName = "OSD v3";
  osdV3.description =
      "Overlay masks and labels (for Mask R-CNN, segmentation, etc.)";
  osdV3.category = "processor";
  osdV3.defaultParameters["font_path"] = "";
  osdV3.requiredParameters = {};
  osdV3.optionalParameters = {"font_path"};
  osdV3.isPreConfigured = true;
  templates_[osdV3.templateId] = osdV3;

  // BA Crossline
  NodeTemplate baCrossline;
  baCrossline.templateId = "ba_crossline_template";
  baCrossline.nodeType = "ba_crossline";
  baCrossline.displayName = "BA Crossline";
  baCrossline.description = "Behavior analysis - crossline detection";
  baCrossline.category = "processor";
  baCrossline.defaultParameters["line_channel"] = "0";
  baCrossline.defaultParameters["line_start_x"] = "0";
  baCrossline.defaultParameters["line_start_y"] = "250";
  baCrossline.defaultParameters["line_end_x"] = "700";
  baCrossline.defaultParameters["line_end_y"] = "220";
  baCrossline.requiredParameters = {};
  baCrossline.optionalParameters = {"line_channel", "line_start_x",
                                    "line_start_y", "line_end_x", "line_end_y"};
  baCrossline.isPreConfigured = true;
  templates_[baCrossline.templateId] = baCrossline;

  // BA Crossline OSD
  NodeTemplate baCrosslineOSD;
  baCrosslineOSD.templateId = "ba_crossline_osd_template";
  baCrosslineOSD.nodeType = "ba_crossline_osd";
  baCrosslineOSD.displayName = "BA Crossline OSD";
  baCrosslineOSD.description = "Overlay crossline detection results";
  baCrosslineOSD.category = "processor";
  baCrosslineOSD.isPreConfigured = true; // No parameters needed
  templates_[baCrosslineOSD.templateId] = baCrosslineOSD;

  // BA Jam
  NodeTemplate baJam;
  baJam.templateId = "ba_jam_template";
  baJam.nodeType = "ba_jam";
  baJam.displayName = "BA Jam";
  baJam.description = "Behavior analysis - jam detection";
  baJam.category = "processor";
  baJam.defaultParameters["jam_channel"] = "0";
  baJam.defaultParameters["jam_region_x_1"] = "20";
  baJam.defaultParameters["jam_region_y_1"] = "360";
  baJam.defaultParameters["jam_region_x_2"] = "400";
  baJam.defaultParameters["jam_region_y_2"] = "250";
  baJam.defaultParameters["jam_region_x_3"] = "535";
  baJam.defaultParameters["jam_region_y_3"] = "250";
  baJam.defaultParameters["jam_region_x_4"] = "555";
  baJam.defaultParameters["jam_region_y_4"] = "560";
  baJam.defaultParameters["jam_region_x_5"] = "30";
  baJam.defaultParameters["jam_region_y_5"] = "550";
  baJam.requiredParameters = {};
  baJam.optionalParameters = {"jam_channel", "jam_region_x_1",
                              "jam_region_y_1", "jam_region_x_2", "jam_region_y_2",
                              "jam_region_x_3", "jam_region_y_3", "jam_region_x_4", "jam_region_y_4",
                              "jam_region_x_5", "jam_region_y_5"};
  baJam.isPreConfigured = true;
  templates_[baJam.templateId] = baJam;

  // BA Jam OSD
  NodeTemplate baJamOSD;
  baJamOSD.templateId = "ba_jam_osd_template";
  baJamOSD.nodeType = "ba_jam_osd";
  baJamOSD.displayName = "BA Jam OSD";
  baJamOSD.description = "Overlay jam detection results";
  baJamOSD.category = "processor";
  baJamOSD.isPreConfigured = true; // No parameters needed
  templates_[baJamOSD.templateId] = baJamOSD;

  // Classifier
  NodeTemplate classifier;
  classifier.templateId = "classifier_template";
  classifier.nodeType = "classifier";
  classifier.displayName = "Classifier";
  classifier.description = "Image classification";
  classifier.category = "processor";
  classifier.requiredParameters = {"model_path"};
  classifier.optionalParameters = {};
  classifier.isPreConfigured = false;
  templates_[classifier.templateId] = classifier;

  // Lane Detector
  NodeTemplate laneDetector;
  laneDetector.templateId = "lane_detector_template";
  laneDetector.nodeType = "lane_detector";
  laneDetector.displayName = "Lane Detector";
  laneDetector.description = "Detect lanes in road images";
  laneDetector.category = "processor";
  laneDetector.requiredParameters = {"model_path"};
  laneDetector.optionalParameters = {};
  laneDetector.isPreConfigured = false;
  templates_[laneDetector.templateId] = laneDetector;

  // Restoration
  NodeTemplate restoration;
  restoration.templateId = "restoration_template";
  restoration.nodeType = "restoration";
  restoration.displayName = "Restoration";
  restoration.description = "Image restoration/enhancement";
  restoration.category = "processor";
  restoration.requiredParameters = {"model_path"};
  restoration.optionalParameters = {};
  restoration.isPreConfigured = false;
  templates_[restoration.templateId] = restoration;

  // ENet Segmentation
  NodeTemplate enetSeg;
  enetSeg.templateId = "enet_seg_template";
  enetSeg.nodeType = "enet_seg";
  enetSeg.displayName = "ENet Segmentation";
  enetSeg.description = "Semantic segmentation using ENet";
  enetSeg.category = "detector";
  enetSeg.requiredParameters = {"model_path"};
  enetSeg.optionalParameters = {};
  enetSeg.isPreConfigured = false;
  templates_[enetSeg.templateId] = enetSeg;

  // Mask R-CNN Detector
  NodeTemplate maskRCNN;
  maskRCNN.templateId = "mask_rcnn_detector_template";
  maskRCNN.nodeType = "mask_rcnn_detector";
  maskRCNN.displayName = "Mask R-CNN Detector";
  maskRCNN.description = "Object detection and segmentation using Mask R-CNN";
  maskRCNN.category = "detector";
  maskRCNN.requiredParameters = {"model_path"};
  maskRCNN.optionalParameters = {};
  maskRCNN.isPreConfigured = false;
  templates_[maskRCNN.templateId] = maskRCNN;

  // OpenPose Detector
  NodeTemplate openPose;
  openPose.templateId = "openpose_detector_template";
  openPose.nodeType = "openpose_detector";
  openPose.displayName = "OpenPose Detector";
  openPose.description = "Human pose estimation using OpenPose";
  openPose.category = "detector";
  openPose.requiredParameters = {"model_path"};
  openPose.optionalParameters = {};
  openPose.isPreConfigured = false;
  templates_[openPose.templateId] = openPose;

  // ========== TENSORRT NODES (conditional compilation) ==========
  // Note: These will only be available if CVEDIX_WITH_TRT is defined

  // TensorRT YOLOv8 Detector
  NodeTemplate trtYOLOv8Detector;
  trtYOLOv8Detector.templateId = "trt_yolov8_detector_template";
  trtYOLOv8Detector.nodeType = "trt_yolov8_detector";
  trtYOLOv8Detector.displayName = "TensorRT YOLOv8 Detector";
  trtYOLOv8Detector.description = "Object detection using TensorRT YOLOv8";
  trtYOLOv8Detector.category = "detector";
  trtYOLOv8Detector.requiredParameters = {"model_path"};
  trtYOLOv8Detector.optionalParameters = {};
  trtYOLOv8Detector.isPreConfigured = false;
  templates_[trtYOLOv8Detector.templateId] = trtYOLOv8Detector;

  // TensorRT YOLOv8 Segmentation Detector
  NodeTemplate trtYOLOv8Seg;
  trtYOLOv8Seg.templateId = "trt_yolov8_seg_detector_template";
  trtYOLOv8Seg.nodeType = "trt_yolov8_seg_detector";
  trtYOLOv8Seg.displayName = "TensorRT YOLOv8 Segmentation";
  trtYOLOv8Seg.description = "Instance segmentation using TensorRT YOLOv8";
  trtYOLOv8Seg.category = "detector";
  trtYOLOv8Seg.requiredParameters = {"model_path"};
  trtYOLOv8Seg.optionalParameters = {};
  trtYOLOv8Seg.isPreConfigured = false;
  templates_[trtYOLOv8Seg.templateId] = trtYOLOv8Seg;

  // TensorRT YOLOv8 Pose Detector
  NodeTemplate trtYOLOv8Pose;
  trtYOLOv8Pose.templateId = "trt_yolov8_pose_detector_template";
  trtYOLOv8Pose.nodeType = "trt_yolov8_pose_detector";
  trtYOLOv8Pose.displayName = "TensorRT YOLOv8 Pose";
  trtYOLOv8Pose.description = "Pose estimation using TensorRT YOLOv8";
  trtYOLOv8Pose.category = "detector";
  trtYOLOv8Pose.requiredParameters = {"model_path"};
  trtYOLOv8Pose.optionalParameters = {};
  trtYOLOv8Pose.isPreConfigured = false;
  templates_[trtYOLOv8Pose.templateId] = trtYOLOv8Pose;

  // TensorRT YOLOv8 Classifier
  NodeTemplate trtYOLOv8Classifier;
  trtYOLOv8Classifier.templateId = "trt_yolov8_classifier_template";
  trtYOLOv8Classifier.nodeType = "trt_yolov8_classifier";
  trtYOLOv8Classifier.displayName = "TensorRT YOLOv8 Classifier";
  trtYOLOv8Classifier.description =
      "Image classification using TensorRT YOLOv8";
  trtYOLOv8Classifier.category = "detector";
  trtYOLOv8Classifier.requiredParameters = {"model_path"};
  trtYOLOv8Classifier.optionalParameters = {};
  trtYOLOv8Classifier.isPreConfigured = false;
  templates_[trtYOLOv8Classifier.templateId] = trtYOLOv8Classifier;

  // TensorRT Vehicle Detector
  NodeTemplate trtVehicleDetector;
  trtVehicleDetector.templateId = "trt_vehicle_detector_template";
  trtVehicleDetector.nodeType = "trt_vehicle_detector";
  trtVehicleDetector.displayName = "TensorRT Vehicle Detector";
  trtVehicleDetector.description = "Vehicle detection using TensorRT";
  trtVehicleDetector.category = "detector";
  trtVehicleDetector.requiredParameters = {"model_path"};
  trtVehicleDetector.optionalParameters = {};
  trtVehicleDetector.isPreConfigured = false;
  templates_[trtVehicleDetector.templateId] = trtVehicleDetector;

  // TensorRT Vehicle Plate Detector
  NodeTemplate trtVehiclePlate;
  trtVehiclePlate.templateId = "trt_vehicle_plate_detector_template";
  trtVehiclePlate.nodeType = "trt_vehicle_plate_detector";
  trtVehiclePlate.displayName = "TensorRT Vehicle Plate Detector";
  trtVehiclePlate.description = "License plate detection using TensorRT";
  trtVehiclePlate.category = "detector";
  trtVehiclePlate.requiredParameters = {"model_path"};
  trtVehiclePlate.optionalParameters = {};
  trtVehiclePlate.isPreConfigured = false;
  templates_[trtVehiclePlate.templateId] = trtVehiclePlate;

  // TensorRT Vehicle Feature Encoder
  NodeTemplate trtVehicleFeatureEncoder;
  trtVehicleFeatureEncoder.templateId = "trt_vehicle_feature_encoder_template";
  trtVehicleFeatureEncoder.nodeType = "trt_vehicle_feature_encoder";
  trtVehicleFeatureEncoder.displayName = "TensorRT Vehicle Feature Encoder";
  trtVehicleFeatureEncoder.description =
      "Extract vehicle features using TensorRT";
  trtVehicleFeatureEncoder.category = "processor";
  trtVehicleFeatureEncoder.requiredParameters = {"model_path"};
  trtVehicleFeatureEncoder.optionalParameters = {};
  trtVehicleFeatureEncoder.isPreConfigured = false;
  templates_[trtVehicleFeatureEncoder.templateId] = trtVehicleFeatureEncoder;

  // TensorRT Vehicle Color Classifier
  NodeTemplate trtVehicleColorClassifier;
  trtVehicleColorClassifier.templateId =
      "trt_vehicle_color_classifier_template";
  trtVehicleColorClassifier.nodeType = "trt_vehicle_color_classifier";
  trtVehicleColorClassifier.displayName = "TensorRT Vehicle Color Classifier";
  trtVehicleColorClassifier.description =
      "Classify vehicle color using TensorRT";
  trtVehicleColorClassifier.category = "processor";
  trtVehicleColorClassifier.requiredParameters = {"model_path"};
  trtVehicleColorClassifier.optionalParameters = {};
  trtVehicleColorClassifier.isPreConfigured = false;
  templates_[trtVehicleColorClassifier.templateId] = trtVehicleColorClassifier;

  // TensorRT Vehicle Type Classifier
  NodeTemplate trtVehicleTypeClassifier;
  trtVehicleTypeClassifier.templateId = "trt_vehicle_type_classifier_template";
  trtVehicleTypeClassifier.nodeType = "trt_vehicle_type_classifier";
  trtVehicleTypeClassifier.displayName = "TensorRT Vehicle Type Classifier";
  trtVehicleTypeClassifier.description = "Classify vehicle type using TensorRT";
  trtVehicleTypeClassifier.category = "processor";
  trtVehicleTypeClassifier.requiredParameters = {"model_path"};
  trtVehicleTypeClassifier.optionalParameters = {};
  trtVehicleTypeClassifier.isPreConfigured = false;
  templates_[trtVehicleTypeClassifier.templateId] = trtVehicleTypeClassifier;

  // TensorRT Vehicle Scanner
  NodeTemplate trtVehicleScanner;
  trtVehicleScanner.templateId = "trt_vehicle_scanner_template";
  trtVehicleScanner.nodeType = "trt_vehicle_scanner";
  trtVehicleScanner.displayName = "TensorRT Vehicle Scanner";
  trtVehicleScanner.description =
      "Vehicle scanning and analysis using TensorRT";
  trtVehicleScanner.category = "processor";
  trtVehicleScanner.requiredParameters = {"model_path"};
  trtVehicleScanner.optionalParameters = {};
  trtVehicleScanner.isPreConfigured = false;
  templates_[trtVehicleScanner.templateId] = trtVehicleScanner;

  // ========== RKNN NODES (conditional compilation) ==========
  // Note: These will only be available if CVEDIX_WITH_RKNN is defined

  // RKNN YOLOv8 Detector
  NodeTemplate rknnYOLOv8Detector;
  rknnYOLOv8Detector.templateId = "rknn_yolov8_detector_template";
  rknnYOLOv8Detector.nodeType = "rknn_yolov8_detector";
  rknnYOLOv8Detector.displayName = "RKNN YOLOv8 Detector";
  rknnYOLOv8Detector.description = "Object detection using RKNN YOLOv8";
  rknnYOLOv8Detector.category = "detector";
  rknnYOLOv8Detector.requiredParameters = {"model_path"};
  rknnYOLOv8Detector.optionalParameters = {};
  rknnYOLOv8Detector.isPreConfigured = false;
  templates_[rknnYOLOv8Detector.templateId] = rknnYOLOv8Detector;



  // ========== PADDLE NODES (conditional compilation) ==========
  // Note: These will only be available if CVEDIX_WITH_PADDLE is defined

  // PaddleOCR Text Detector
  NodeTemplate ppocrTextDetector;
  ppocrTextDetector.templateId = "ppocr_text_detector_template";
  ppocrTextDetector.nodeType = "ppocr_text_detector";
  ppocrTextDetector.displayName = "PaddleOCR Text Detector";
  ppocrTextDetector.description = "Text detection using PaddleOCR";
  ppocrTextDetector.category = "detector";
  ppocrTextDetector.requiredParameters = {"model_path"};
  ppocrTextDetector.optionalParameters = {};
  ppocrTextDetector.isPreConfigured = false;
  templates_[ppocrTextDetector.templateId] = ppocrTextDetector;

  // ========== BROKER NODES (currently disabled) ==========
  // Note: Broker nodes are temporarily disabled due to cereal dependency issue
  // Templates are created but nodes won't be usable until broker nodes are
  // re-enabled

  // JSON Console Broker
  NodeTemplate jsonConsoleBroker;
  jsonConsoleBroker.templateId = "json_console_broker_template";
  jsonConsoleBroker.nodeType = "json_console_broker";
  jsonConsoleBroker.displayName = "JSON Console Broker";
  jsonConsoleBroker.description = "Output detection results to console as JSON";
  jsonConsoleBroker.category = "broker";
  jsonConsoleBroker.defaultParameters["broke_for"] = "NORMAL";
  jsonConsoleBroker.requiredParameters = {};
  jsonConsoleBroker.optionalParameters = {"broke_for"};
  jsonConsoleBroker.isPreConfigured = true;
  templates_[jsonConsoleBroker.templateId] = jsonConsoleBroker;

  // Note: Other broker nodes can be added similarly when re-enabled
}

bool NodePoolManager::registerTemplate(const NodeTemplate &nodeTemplate) {
  std::unique_lock<std::shared_timed_mutex> lock(mutex_);

  if (templates_.find(nodeTemplate.templateId) != templates_.end()) {
    return false; // Template already exists
  }

  templates_[nodeTemplate.templateId] = nodeTemplate;
  return true;
}

std::vector<NodePoolManager::NodeTemplate>
NodePoolManager::getAllTemplates() const {
  std::shared_lock<std::shared_timed_mutex> lock(mutex_);

  std::vector<NodeTemplate> result;
  result.reserve(templates_.size());

  for (const auto &[id, nodeTemplate] : templates_) {
    result.push_back(nodeTemplate);
  }

  return result;
}

std::vector<NodePoolManager::NodeTemplate>
NodePoolManager::getTemplatesByCategory(const std::string &category) const {
  std::shared_lock<std::shared_timed_mutex> lock(mutex_);

  std::vector<NodeTemplate> result;

  for (const auto &[id, nodeTemplate] : templates_) {
    if (nodeTemplate.category == category) {
      result.push_back(nodeTemplate);
    }
  }

  return result;
}

std::optional<NodePoolManager::NodeTemplate>
NodePoolManager::getTemplate(const std::string &templateId) const {
  std::shared_lock<std::shared_timed_mutex> lock(mutex_);

  auto it = templates_.find(templateId);
  if (it != templates_.end()) {
    return it->second;
  }

  return std::nullopt;
}

std::string NodePoolManager::createPreConfiguredNode(
    const std::string &templateId,
    const std::map<std::string, std::string> &parameters) {

  std::unique_lock<std::shared_timed_mutex> lock(mutex_);

  auto templateIt = templates_.find(templateId);
  if (templateIt == templates_.end()) {
    return ""; // Template not found
  }

  const auto &nodeTemplate = templateIt->second;

  // Merge parameters
  std::map<std::string, std::string> finalParams =
      nodeTemplate.defaultParameters;
  for (const auto &[key, value] : parameters) {
    finalParams[key] = value;
  }

  // Validate required parameters
  for (const auto &required : nodeTemplate.requiredParameters) {
    if (finalParams.find(required) == finalParams.end()) {
      return ""; // Missing required parameter
    }
  }

  // Create node instance (this would need PipelineBuilder integration)
  // For now, we'll just store the configuration
  PreConfiguredNode node;
  node.nodeId = generateNodeId();
  node.templateId = templateId;
  node.parameters = finalParams;
  node.inUse = false;
  node.createdAt = std::chrono::steady_clock::now();
  // node.node = createNodeInstance(nodeTemplate, finalParams);  // TODO:
  // Implement

  preConfiguredNodes_[node.nodeId] = node;

  return node.nodeId;
}

std::optional<NodePoolManager::PreConfiguredNode>
NodePoolManager::getPreConfiguredNode(const std::string &nodeId) const {
  std::shared_lock<std::shared_timed_mutex> lock(mutex_);

  auto it = preConfiguredNodes_.find(nodeId);
  if (it != preConfiguredNodes_.end()) {
    return it->second;
  }

  return std::nullopt;
}

std::vector<NodePoolManager::PreConfiguredNode>
NodePoolManager::getAllPreConfiguredNodes() const {
  std::shared_lock<std::shared_timed_mutex> lock(mutex_);

  std::vector<PreConfiguredNode> result;
  result.reserve(preConfiguredNodes_.size());

  for (const auto &[id, node] : preConfiguredNodes_) {
    result.push_back(node);
  }

  return result;
}

std::vector<NodePoolManager::PreConfiguredNode>
NodePoolManager::getAvailableNodes() const {
  std::shared_lock<std::shared_timed_mutex> lock(mutex_);

  std::vector<PreConfiguredNode> result;

  for (const auto &[id, node] : preConfiguredNodes_) {
    if (!node.inUse) {
      result.push_back(node);
    }
  }

  return result;
}

bool NodePoolManager::markNodeInUse(const std::string &nodeId) {
  std::unique_lock<std::shared_timed_mutex> lock(mutex_);

  auto it = preConfiguredNodes_.find(nodeId);
  if (it != preConfiguredNodes_.end() && !it->second.inUse) {
    it->second.inUse = true;
    return true;
  }

  return false;
}

bool NodePoolManager::markNodeAvailable(const std::string &nodeId) {
  std::unique_lock<std::shared_timed_mutex> lock(mutex_);

  auto it = preConfiguredNodes_.find(nodeId);
  if (it != preConfiguredNodes_.end() && it->second.inUse) {
    it->second.inUse = false;
    return true;
  }

  return false;
}

bool NodePoolManager::removePreConfiguredNode(const std::string &nodeId) {
  std::unique_lock<std::shared_timed_mutex> lock(mutex_);

  auto it = preConfiguredNodes_.find(nodeId);
  if (it != preConfiguredNodes_.end() && !it->second.inUse) {
    preConfiguredNodes_.erase(it);
    return true;
  }

  return false;
}

NodePoolManager::NodeStats NodePoolManager::getStats() const {
  std::shared_lock<std::shared_timed_mutex> lock(mutex_);

  NodeStats stats;
  stats.totalTemplates = templates_.size();
  stats.totalPreConfiguredNodes = preConfiguredNodes_.size();
  stats.availableNodes = 0;
  stats.inUseNodes = 0;

  for (const auto &[id, node] : preConfiguredNodes_) {
    if (node.inUse) {
      stats.inUseNodes++;
    } else {
      stats.availableNodes++;
    }
  }

  // Count by category
  for (const auto &[id, nodeTemplate] : templates_) {
    stats.nodesByCategory[nodeTemplate.category]++;
  }

  return stats;
}

std::optional<SolutionConfig>
NodePoolManager::buildSolutionFromNodes(const std::vector<std::string> &nodeIds,
                                        const std::string &solutionId,
                                        const std::string &solutionName) {

  std::shared_lock<std::shared_timed_mutex> lock(mutex_);

  SolutionConfig config;
  config.solutionId = solutionId;
  config.solutionName = solutionName;
  config.solutionType = "custom";
  config.isDefault = false;

  for (const auto &nodeId : nodeIds) {
    auto nodeIt = preConfiguredNodes_.find(nodeId);
    if (nodeIt == preConfiguredNodes_.end()) {
      return std::nullopt; // Node not found
    }

    const auto &preNode = nodeIt->second;
    auto templateIt = templates_.find(preNode.templateId);
    if (templateIt == templates_.end()) {
      return std::nullopt; // Template not found
    }

    const auto &nodeTemplate = templateIt->second;

    SolutionConfig::NodeConfig nodeConfig;
    nodeConfig.nodeType = nodeTemplate.nodeType;
    nodeConfig.nodeName = nodeTemplate.displayName + "_{instanceId}";
    nodeConfig.parameters = preNode.parameters;

    config.pipeline.push_back(nodeConfig);
  }

  return config;
}

std::string NodePoolManager::generateNodeId() const {
  std::random_device rd;
  std::mt19937 gen(rd());
  std::uniform_int_distribution<> dis(0, 15);

  std::stringstream ss;
  ss << "node_";
  for (int i = 0; i < 8; ++i) {
    ss << std::hex << dis(gen);
  }

  return ss.str();
}

std::string
NodePoolManager::generateDefaultNodeId(const std::string &nodeType) const {
  std::stringstream ss;
  ss << "node_" << nodeType << "_default";
  return ss.str();
}

bool NodePoolManager::hasDefaultNodes(
    SolutionRegistry &solutionRegistry) const {
  std::shared_lock<std::shared_timed_mutex> lock(mutex_);

  // Get all solutions
  auto allSolutions = solutionRegistry.getAllSolutions();

  // Collect all node types from default solutions
  std::set<std::string> requiredNodeTypes;
  for (const auto &[solutionId, solutionConfig] : allSolutions) {
    if (!solutionConfig.isDefault) {
      continue;
    }
    for (const auto &nodeConfig : solutionConfig.pipeline) {
      requiredNodeTypes.insert(nodeConfig.nodeType);
    }
  }

  // Check if we have nodes for all required types
  std::set<std::string> existingNodeTypes;
  for (const auto &[nodeId, node] : preConfiguredNodes_) {
    auto templateIt = templates_.find(node.templateId);
    if (templateIt != templates_.end()) {
      existingNodeTypes.insert(templateIt->second.nodeType);
    }
  }

  // Check if all required types exist
  for (const auto &nodeType : requiredNodeTypes) {
    if (existingNodeTypes.find(nodeType) == existingNodeTypes.end()) {
      return false; // Missing at least one default node type
    }
  }

  return true; // All default node types exist
}

size_t NodePoolManager::createDefaultNodesFromTemplates() {
  std::unique_lock<std::shared_timed_mutex> lock(mutex_);

  // Map to track node types we've already created nodes for
  std::set<std::string> createdNodeTypes;

  // Check existing nodes to see what types we already have
  for (const auto &[nodeId, node] : preConfiguredNodes_) {
    auto templateIt = templates_.find(node.templateId);
    if (templateIt != templates_.end()) {
      createdNodeTypes.insert(templateIt->second.nodeType);
    }
  }

  size_t nodesCreated = 0;

  // Iterate through all templates and create nodes for those that can be
  // created
  for (const auto &[templateId, nodeTemplate] : templates_) {
    // Skip if we've already created a node for this type
    if (createdNodeTypes.find(nodeTemplate.nodeType) !=
        createdNodeTypes.end()) {
      continue;
    }

    // Check if we can create a node (all required parameters must have defaults
    // or be optional)
    bool canCreate = true;
    std::map<std::string, std::string> nodeParams;

    // Use default parameters from template
    nodeParams = nodeTemplate.defaultParameters;

    // Check required parameters - if any required parameter doesn't have a
    // default, use placeholder values for common parameters
    for (const auto &required : nodeTemplate.requiredParameters) {
      if (nodeParams.find(required) == nodeParams.end()) {
        // Try to provide placeholder values for common required parameters
        if (required == "file_path" || required == "port_or_location" ||
            required == "location" || required == "uri") {
          // Use empty string as placeholder - user will need to set it
          nodeParams[required] = "";
        } else if (required == "rtsp_url" || required == "rtmp_url" ||
                   required == "rtmp_des_url") {
          // Use placeholder URL
          nodeParams[required] = "";
        } else if (required == "model_path" || required == "weights_path" ||
                   required == "config_path" || required == "labels_path" ||
                   required == "model_config_path") {
          // Use empty string for model paths - user will need to set it
          nodeParams[required] = "";
        } else if (required == "save_dir") {
          // Use default save directory
          nodeParams[required] = "./output/{instanceId}";
        } else if (required == "port") {
          // Use default port based on node type
          if (nodeTemplate.nodeType == "rtsp_des") {
            nodeParams[required] = "8000";
          } else {
            nodeParams[required] = "8554";
          }
        } else if (required == "socket_host" || required == "mqtt_broker" ||
                   required == "kafka_broker") {
          // Use localhost as default
          nodeParams[required] = "localhost";
        } else if (required == "socket_port" || required == "mqtt_port") {
          // Use default ports
          if (required == "mqtt_port") {
            nodeParams[required] = "1883";
          } else {
            nodeParams[required] = "8080";
          }
        } else if (required == "mqtt_topic" || required == "kafka_topic") {
          // Use default topic
          nodeParams[required] = "detections";
        } else {
          // Unknown required parameter without default - cannot create node
          canCreate = false;
          break;
        }
      }
    }

    if (!canCreate) {
      std::cerr << "[NodePoolManager] Skipping template " << templateId
                << " (type: " << nodeTemplate.nodeType
                << ") - missing required parameters without defaults"
                << std::endl;
      continue;
    }

    // Create pre-configured node
    PreConfiguredNode node;
    node.nodeId = generateDefaultNodeId(nodeTemplate.nodeType);
    node.templateId = templateId;
    node.parameters = nodeParams;
    node.inUse = false;
    node.createdAt = std::chrono::steady_clock::now();
    node.node = nullptr; // Will be created when needed

    preConfiguredNodes_[node.nodeId] = node;
    createdNodeTypes.insert(nodeTemplate.nodeType);
    nodesCreated++;

    std::cerr << "[NodePoolManager] Created default node: " << node.nodeId
              << " (type: " << nodeTemplate.nodeType
              << ", template: " << templateId << ")" << std::endl;
  }

  std::cerr << "[NodePoolManager] Created " << nodesCreated
            << " default nodes from templates" << std::endl;
  return nodesCreated;
}

size_t NodePoolManager::createNodesFromDefaultSolutions(
    SolutionRegistry &solutionRegistry) {
  std::unique_lock<std::shared_timed_mutex> lock(mutex_);

  // Get all solutions
  auto allSolutions = solutionRegistry.getAllSolutions();

  // Map to track node types we've already created nodes for
  std::set<std::string> createdNodeTypes;

  // Check existing nodes to see what types we already have
  for (const auto &[nodeId, node] : preConfiguredNodes_) {
    auto templateIt = templates_.find(node.templateId);
    if (templateIt != templates_.end()) {
      createdNodeTypes.insert(templateIt->second.nodeType);
    }
  }

  // Map nodeType to templateId
  std::map<std::string, std::string> nodeTypeToTemplateId;
  for (const auto &[templateId, template_] : templates_) {
    nodeTypeToTemplateId[template_.nodeType] = templateId;
  }

  size_t nodesCreated = 0;

  // Iterate through all solutions (especially default solutions)
  for (const auto &[solutionId, solutionConfig] : allSolutions) {
    // Only process default solutions
    if (!solutionConfig.isDefault) {
      continue;
    }

    // Extract unique node types from this solution's pipeline
    for (const auto &nodeConfig : solutionConfig.pipeline) {
      const std::string &nodeType = nodeConfig.nodeType;

      // Skip if we've already created a node for this type
      if (createdNodeTypes.find(nodeType) != createdNodeTypes.end()) {
        continue;
      }

      // Find template for this node type
      auto templateIt = nodeTypeToTemplateId.find(nodeType);
      if (templateIt == nodeTypeToTemplateId.end()) {
        std::cerr
            << "[NodePoolManager] Warning: No template found for node type: "
            << nodeType << std::endl;
        continue;
      }

      const std::string &templateId = templateIt->second;
      // Direct access to templates_ since we already have unique_lock
      auto templateIt2 = templates_.find(templateId);
      if (templateIt2 == templates_.end()) {
        std::cerr << "[NodePoolManager] Warning: Template not found: "
                  << templateId << std::endl;
        continue;
      }

      const auto &nodeTemplate = templateIt2->second;

      // Check if we can create a node (skip if required parameters have
      // placeholders)
      bool canCreate = true;
      std::map<std::string, std::string> nodeParams;

      // Use parameters from solution config, but replace placeholders with
      // defaults
      for (const auto &[key, value] : nodeConfig.parameters) {
        // Skip placeholders like ${RTSP_URL}, ${MODEL_PATH}, etc.
        if (value.size() > 2 && value[0] == '$' && value[1] == '{') {
          // This is a placeholder, use default from template if available
          if (nodeTemplate.defaultParameters.find(key) !=
              nodeTemplate.defaultParameters.end()) {
            nodeParams[key] = nodeTemplate.defaultParameters.at(key);
          } else {
            // Check if this is a supported placeholder that will be resolved
            // when creating instances (e.g., RTMP_DES_URL, RTSP_URL, etc.)
            // These placeholders are resolved by PipelineBuilder when creating
            // instances, so we allow the node to be created with the placeholder
            // Extract placeholder name from ${NAME} format
            std::string placeholderName;
            if (value.size() > 3 && value.back() == '}') {
              placeholderName = value.substr(2, value.length() - 3); // Extract ${...}
            } else {
              // Invalid placeholder format - skip this node
              canCreate = false;
              break;
            }
            // Allow placeholders that will be resolved by PipelineBuilder's generic
            // placeholder substitution or explicit handlers. This includes:
            // - Input/Output URLs: RTMP_DES_URL, RTMP_URL, RTSP_URL, RTSP_SRC_URL,
            //   RTMP_SRC_URL, HLS_URL, HTTP_URL, FILE_PATH
            // - Model paths: MODEL_PATH, SFACE_MODEL_PATH, WEIGHTS_PATH, CONFIG_PATH,
            //   LABELS_PATH, MODEL_CONFIG_PATH, TRT_PATH
            // - Configuration: RESIZE_RATIO, ENABLE_SCREEN_DES, SCORE_THRESHOLD,
            //   INPUT_WIDTH, INPUT_HEIGHT
            // - Behavior Analysis: CHECK_INTERVAL_FRAMES, CHECK_MIN_HIT_FRAMES,
            //   CHECK_MAX_DISTANCE, CHECK_MIN_STOPS, CHECK_NOTIFY_INTERVAL
            // - Crossline: CROSSLINE_START_X, CROSSLINE_START_Y, CROSSLINE_END_X,
            //   CROSSLINE_END_Y
            // - Other: Any placeholder that can be resolved from additionalParams
            // Generic placeholder substitution in PipelineBuilder will handle all
            // placeholders from additionalParams, so we allow all placeholders here
            nodeParams[key] = value; // Keep placeholder as-is - will be resolved when
                                     // creating instances
          }
        } else {
          nodeParams[key] = value;
        }
      }

      // Add default parameters from template
      for (const auto &[key, value] : nodeTemplate.defaultParameters) {
        if (nodeParams.find(key) == nodeParams.end()) {
          nodeParams[key] = value;
        }
      }

      // Check required parameters
      for (const auto &required : nodeTemplate.requiredParameters) {
        if (nodeParams.find(required) == nodeParams.end()) {
          // Missing required parameter - skip this node
          canCreate = false;
          break;
        }
      }

      if (!canCreate) {
        std::cerr << "[NodePoolManager] Skipping node type " << nodeType
                  << " - missing required parameters or placeholders"
                  << std::endl;
        continue;
      }

      // Create pre-configured node
      PreConfiguredNode node;
      node.nodeId = generateDefaultNodeId(nodeType);
      node.templateId = templateId;
      node.parameters = nodeParams;
      node.inUse = false;
      node.createdAt = std::chrono::steady_clock::now();
      node.node = nullptr; // Will be created when needed

      preConfiguredNodes_[node.nodeId] = node;
      createdNodeTypes.insert(nodeType);
      nodesCreated++;

      std::cerr << "[NodePoolManager] Created pre-configured node: "
                << node.nodeId << " (type: " << nodeType
                << ", template: " << templateId << ")" << std::endl;
    }
  }

  std::cerr << "[NodePoolManager] Created " << nodesCreated
            << " pre-configured nodes from default solutions" << std::endl;
  return nodesCreated;
}

size_t
NodePoolManager::createNodesFromSolution(const SolutionConfig &solutionConfig) {
  std::unique_lock<std::shared_timed_mutex> lock(mutex_);

  // Map to track node types we've already created nodes for
  std::set<std::string> createdNodeTypes;

  // Check existing nodes to see what types we already have
  for (const auto &[nodeId, node] : preConfiguredNodes_) {
    auto templateIt = templates_.find(node.templateId);
    if (templateIt != templates_.end()) {
      createdNodeTypes.insert(templateIt->second.nodeType);
    }
  }

  // Map nodeType to templateId
  std::map<std::string, std::string> nodeTypeToTemplateId;
  for (const auto &[templateId, template_] : templates_) {
    nodeTypeToTemplateId[template_.nodeType] = templateId;
  }

  size_t nodesCreated = 0;

  // Extract unique node types from this solution's pipeline
  for (const auto &nodeConfig : solutionConfig.pipeline) {
    const std::string &nodeType = nodeConfig.nodeType;

    // Skip if we've already created a node for this type
    if (createdNodeTypes.find(nodeType) != createdNodeTypes.end()) {
      continue;
    }

    // Find template for this node type
    auto templateIt = nodeTypeToTemplateId.find(nodeType);
    if (templateIt == nodeTypeToTemplateId.end()) {
      std::cerr
          << "[NodePoolManager] Warning: No template found for node type: "
          << nodeType << " in solution: " << solutionConfig.solutionId
          << std::endl;
      continue;
    }

    const std::string &templateId = templateIt->second;
    auto templateIt2 = templates_.find(templateId);
    if (templateIt2 == templates_.end()) {
      std::cerr << "[NodePoolManager] Warning: Template not found: "
                << templateId << std::endl;
      continue;
    }

    const auto &nodeTemplate = templateIt2->second;

    // Check if we can create a node (skip if required parameters have
    // placeholders)
    bool canCreate = true;
    std::map<std::string, std::string> nodeParams;

    // Use parameters from solution config, but replace placeholders with
    // defaults
    for (const auto &[key, value] : nodeConfig.parameters) {
      // Skip placeholders like ${RTSP_URL}, ${MODEL_PATH}, etc.
      if (value.size() > 2 && value[0] == '$' && value[1] == '{') {
        // This is a placeholder, use default from template if available
        if (nodeTemplate.defaultParameters.find(key) !=
            nodeTemplate.defaultParameters.end()) {
          nodeParams[key] = nodeTemplate.defaultParameters.at(key);
        } else {
          // Check if this is a supported placeholder that will be resolved
          // when creating instances (e.g., RTMP_DES_URL, RTSP_URL, etc.)
          // These placeholders are resolved by PipelineBuilder when creating
          // instances, so we allow the node to be created with the placeholder
          // Extract placeholder name from ${NAME} format
          std::string placeholderName;
          if (value.size() > 3 && value.back() == '}') {
            placeholderName = value.substr(2, value.length() - 3); // Extract ${...}
          } else {
            // Invalid placeholder format - skip this node
            canCreate = false;
            break;
          }
          // Allow placeholders that will be resolved by PipelineBuilder's generic
          // placeholder substitution or explicit handlers. This includes:
          // - Input/Output URLs: RTMP_DES_URL, RTMP_URL, RTSP_URL, RTSP_SRC_URL,
          //   RTMP_SRC_URL, HLS_URL, HTTP_URL, FILE_PATH
          // - Model paths: MODEL_PATH, SFACE_MODEL_PATH, WEIGHTS_PATH, CONFIG_PATH,
          //   LABELS_PATH, MODEL_CONFIG_PATH, TRT_PATH
          // - Configuration: RESIZE_RATIO, ENABLE_SCREEN_DES, SCORE_THRESHOLD,
          //   INPUT_WIDTH, INPUT_HEIGHT
          // - Behavior Analysis: CHECK_INTERVAL_FRAMES, CHECK_MIN_HIT_FRAMES,
          //   CHECK_MAX_DISTANCE, CHECK_MIN_STOPS, CHECK_NOTIFY_INTERVAL
          // - Crossline: CROSSLINE_START_X, CROSSLINE_START_Y, CROSSLINE_END_X,
          //   CROSSLINE_END_Y
          // - Other: Any placeholder that can be resolved from additionalParams
          // Generic placeholder substitution in PipelineBuilder will handle all
          // placeholders from additionalParams, so we allow all placeholders here
          nodeParams[key] = value; // Keep placeholder as-is - will be resolved when
                                   // creating instances
        }
      } else {
        nodeParams[key] = value;
      }
    }

    // Add default parameters from template
    for (const auto &[key, value] : nodeTemplate.defaultParameters) {
      if (nodeParams.find(key) == nodeParams.end()) {
        nodeParams[key] = value;
      }
    }

    // Check required parameters
    for (const auto &required : nodeTemplate.requiredParameters) {
      if (nodeParams.find(required) == nodeParams.end()) {
        // Missing required parameter - skip this node
        canCreate = false;
        break;
      }
    }

    if (!canCreate) {
      std::cerr << "[NodePoolManager] Skipping node type " << nodeType
                << " in solution " << solutionConfig.solutionId
                << " - missing required parameters or placeholders"
                << std::endl;
      continue;
    }

    // Create pre-configured node
    PreConfiguredNode node;
    // Use default node ID only if this is a default solution
    // User-created solutions should create regular nodes (not default nodes)
    if (solutionConfig.isDefault) {
      node.nodeId = generateDefaultNodeId(nodeType);
    } else {
      // For custom solutions, create regular node ID (user-created node)
      node.nodeId = generateNodeId();
    }
    node.templateId = templateId;
    node.parameters = nodeParams;
    node.inUse = false;
    node.createdAt = std::chrono::steady_clock::now();
    node.node = nullptr; // Will be created when needed

    preConfiguredNodes_[node.nodeId] = node;
    createdNodeTypes.insert(nodeType);
    nodesCreated++;

    if (solutionConfig.isDefault) {
      std::cerr << "[NodePoolManager] Created default node from solution: "
                << node.nodeId << " (type: " << nodeType
                << ", template: " << templateId
                << ", solution: " << solutionConfig.solutionId << ")"
                << std::endl;
    } else {
      std::cerr << "[NodePoolManager] Created node from custom solution: "
                << node.nodeId << " (type: " << nodeType
                << ", template: " << templateId
                << ", solution: " << solutionConfig.solutionId << ")"
                << std::endl;
    }
  }

  if (nodesCreated > 0) {
    if (solutionConfig.isDefault) {
      std::cerr << "[NodePoolManager] Created " << nodesCreated
                << " default nodes from solution: " << solutionConfig.solutionId
                << std::endl;
    } else {
      std::cerr << "[NodePoolManager] Created " << nodesCreated
                << " nodes from custom solution: " << solutionConfig.solutionId
                << std::endl;
    }
  }
  return nodesCreated;
}

size_t NodePoolManager::loadNodesFromStorage(NodeStorage &nodeStorage) {
  std::unique_lock<std::shared_timed_mutex> lock(mutex_);

  auto loadedNodes = nodeStorage.loadAllNodes();
  size_t loadedCount = 0;
  size_t skippedCount = 0;

  for (const auto &node : loadedNodes) {
    // Check if template exists - direct access since we already have
    // unique_lock
    auto templateIt = templates_.find(node.templateId);
    if (templateIt == templates_.end()) {
      std::cerr << "[NodePoolManager] Warning: Template not found for node "
                << node.nodeId << ": " << node.templateId << ". Skipping."
                << std::endl;
      skippedCount++;
      continue;
    }

    // Check if node already exists (don't overwrite existing nodes)
    if (preConfiguredNodes_.find(node.nodeId) != preConfiguredNodes_.end()) {
      // Node already exists, skip (could be default node or already loaded)
      skippedCount++;
      continue;
    }

    // Add node (this is a user-created node)
    preConfiguredNodes_[node.nodeId] = node;
    loadedCount++;
    std::cerr << "[NodePoolManager] Loaded user-created node: " << node.nodeId
              << " (template: " << node.templateId << ")" << std::endl;
  }

  std::cerr << "[NodePoolManager] Loaded " << loadedCount
            << " user-created nodes from storage" << std::endl;
  if (skippedCount > 0) {
    std::cerr << "[NodePoolManager] Skipped " << skippedCount
              << " nodes (already exist or invalid)" << std::endl;
  }
  return loadedCount;
}

bool NodePoolManager::saveNodesToStorage(NodeStorage &nodeStorage) const {
  std::shared_lock<std::shared_timed_mutex> lock(mutex_);

  std::vector<PreConfiguredNode> nodes;
  nodes.reserve(preConfiguredNodes_.size());

  for (const auto &[id, node] : preConfiguredNodes_) {
    nodes.push_back(node);
  }

  return nodeStorage.saveAllNodes(nodes);
}
