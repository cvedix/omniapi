#pragma once

#include "models/create_instance_request.h"
#include <memory>
#include <map>
#include <string>

// Forward declarations
namespace cvedix_nodes {
class cvedix_node;
}

/**
 * @brief Detector Node Factory for PipelineBuilder
 * 
 * Handles creation of all detector/inference nodes (YOLO, TensorRT, RKNN, Face, Vehicle, etc.)
 */
class PipelineBuilderDetectorNodes {
public:
  // ========== Face Detection Nodes ==========

  /**
   * @brief Create face detector node (YuNet)
   */
  static std::shared_ptr<cvedix_nodes::cvedix_node>
  createFaceDetectorNode(const std::string &nodeName,
                         const std::map<std::string, std::string> &params,
                         const CreateInstanceRequest &req);

  /**
   * @brief Create SFace feature encoder node
   */
  static std::shared_ptr<cvedix_nodes::cvedix_node>
  createSFaceEncoderNode(const std::string &nodeName,
                         const std::map<std::string, std::string> &params,
                         const CreateInstanceRequest &req);

  // ========== TensorRT Inference Nodes ==========

#ifdef CVEDIX_WITH_TRT
  /**
   * @brief Create TensorRT YOLOv8 detector node
   */
  static std::shared_ptr<cvedix_nodes::cvedix_node>
  createTRTYOLOv8DetectorNode(const std::string &nodeName,
                              const std::map<std::string, std::string> &params,
                              const CreateInstanceRequest &req);

  /**
   * @brief Create TensorRT YOLOv8 segmentation detector node
   */
  static std::shared_ptr<cvedix_nodes::cvedix_node> createTRTYOLOv8SegDetectorNode(
      const std::string &nodeName,
      const std::map<std::string, std::string> &params,
      const CreateInstanceRequest &req);

  /**
   * @brief Create TensorRT YOLOv8 pose detector node
   */
  static std::shared_ptr<cvedix_nodes::cvedix_node> createTRTYOLOv8PoseDetectorNode(
      const std::string &nodeName,
      const std::map<std::string, std::string> &params,
      const CreateInstanceRequest &req);

  /**
   * @brief Create TensorRT YOLOv8 classifier node
   */
  static std::shared_ptr<cvedix_nodes::cvedix_node> createTRTYOLOv8ClassifierNode(
      const std::string &nodeName,
      const std::map<std::string, std::string> &params,
      const CreateInstanceRequest &req);

  /**
   * @brief Create TensorRT vehicle detector node
   */
  static std::shared_ptr<cvedix_nodes::cvedix_node>
  createTRTVehicleDetectorNode(const std::string &nodeName,
                               const std::map<std::string, std::string> &params,
                               const CreateInstanceRequest &req);

  /**
   * @brief Create TensorRT vehicle plate detector node
   */
  static std::shared_ptr<cvedix_nodes::cvedix_node> createTRTVehiclePlateDetectorNode(
      const std::string &nodeName,
      const std::map<std::string, std::string> &params,
      const CreateInstanceRequest &req);

  /**
   * @brief Create TensorRT vehicle plate detector v2 node
   */
  static std::shared_ptr<cvedix_nodes::cvedix_node>
  createTRTVehiclePlateDetectorV2Node(
      const std::string &nodeName,
      const std::map<std::string, std::string> &params,
      const CreateInstanceRequest &req);

  /**
   * @brief Create TensorRT vehicle color classifier node
   */
  static std::shared_ptr<cvedix_nodes::cvedix_node>
  createTRTVehicleColorClassifierNode(
      const std::string &nodeName,
      const std::map<std::string, std::string> &params,
      const CreateInstanceRequest &req);

  /**
   * @brief Create TensorRT vehicle type classifier node
   */
  static std::shared_ptr<cvedix_nodes::cvedix_node> createTRTVehicleTypeClassifierNode(
      const std::string &nodeName,
      const std::map<std::string, std::string> &params,
      const CreateInstanceRequest &req);

  /**
   * @brief Create TensorRT vehicle feature encoder node
   */
  static std::shared_ptr<cvedix_nodes::cvedix_node> createTRTVehicleFeatureEncoderNode(
      const std::string &nodeName,
      const std::map<std::string, std::string> &params,
      const CreateInstanceRequest &req);

  /**
   * @brief Create TensorRT vehicle scanner node
   */
  static std::shared_ptr<cvedix_nodes::cvedix_node>
  createTRTVehicleScannerNode(const std::string &nodeName,
                              const std::map<std::string, std::string> &params,
                              const CreateInstanceRequest &req);

  /**
   * @brief Create TensorRT InsightFace recognition node
   */
  static std::shared_ptr<cvedix_nodes::cvedix_node>
  createTRTInsightFaceRecognitionNode(
      const std::string &nodeName,
      const std::map<std::string, std::string> &params,
      const CreateInstanceRequest &req);

  /**
   * @brief Create TensorRT YOLOv11 face detector node
   */
  static std::shared_ptr<cvedix_nodes::cvedix_node>
  createTRTYOLOv11FaceDetectorNode(
      const std::string &nodeName,
      const std::map<std::string, std::string> &params,
      const CreateInstanceRequest &req);

  /**
   * @brief Create TensorRT YOLOv11 plate detector node
   */
  static std::shared_ptr<cvedix_nodes::cvedix_node>
  createTRTYOLOv11PlateDetectorNode(
      const std::string &nodeName,
      const std::map<std::string, std::string> &params,
      const CreateInstanceRequest &req);
#endif

  // ========== RKNN Inference Nodes ==========

#ifdef CVEDIX_WITH_RKNN
  /**
   * @brief Create RKNN YOLOv8 detector node
   */
  static std::shared_ptr<cvedix_nodes::cvedix_node>
  createRKNNYOLOv8DetectorNode(const std::string &nodeName,
                               const std::map<std::string, std::string> &params,
                               const CreateInstanceRequest &req);

  /**
   * @brief Create RKNN face detector node
   */
  static std::shared_ptr<cvedix_nodes::cvedix_node>
  createRKNNFaceDetectorNode(const std::string &nodeName,
                             const std::map<std::string, std::string> &params,
                             const CreateInstanceRequest &req);

  /**
   * @brief Create RKNN YOLOv11 detector node
   */
  static std::shared_ptr<cvedix_nodes::cvedix_node> createRKNNYOLOv11DetectorNode(
      const std::string &nodeName,
      const std::map<std::string, std::string> &params,
      const CreateInstanceRequest &req);
#endif

  // ========== Other Inference Nodes ==========

  /**
   * @brief Create YOLO detector node (OpenCV DNN)
   */
  static std::shared_ptr<cvedix_nodes::cvedix_node>
  createYOLODetectorNode(const std::string &nodeName,
                         const std::map<std::string, std::string> &params,
                         const CreateInstanceRequest &req);

  /**
   * @brief Create ENet segmentation node
   */
  static std::shared_ptr<cvedix_nodes::cvedix_node>
  createENetSegNode(const std::string &nodeName,
                    const std::map<std::string, std::string> &params,
                    const CreateInstanceRequest &req);

  /**
   * @brief Create Mask RCNN detector node
   */
  static std::shared_ptr<cvedix_nodes::cvedix_node>
  createMaskRCNNDetectorNode(const std::string &nodeName,
                             const std::map<std::string, std::string> &params,
                             const CreateInstanceRequest &req);

  /**
   * @brief Create OpenPose detector node
   */
  static std::shared_ptr<cvedix_nodes::cvedix_node>
  createOpenPoseDetectorNode(const std::string &nodeName,
                             const std::map<std::string, std::string> &params,
                             const CreateInstanceRequest &req);

  /**
   * @brief Create classifier node (generic)
   */
  static std::shared_ptr<cvedix_nodes::cvedix_node>
  createClassifierNode(const std::string &nodeName,
                       const std::map<std::string, std::string> &params,
                       const CreateInstanceRequest &req);

  /**
   * @brief Create lane detector node
   */
  static std::shared_ptr<cvedix_nodes::cvedix_node>
  createLaneDetectorNode(const std::string &nodeName,
                         const std::map<std::string, std::string> &params,
                         const CreateInstanceRequest &req);

#ifdef CVEDIX_WITH_PADDLE
  /**
   * @brief Create PaddleOCR text detector node
   */
  static std::shared_ptr<cvedix_nodes::cvedix_node> createPaddleOCRTextDetectorNode(
      const std::string &nodeName,
      const std::map<std::string, std::string> &params,
      const CreateInstanceRequest &req);
#endif

  /**
   * @brief Create restoration node (Real-ESRGAN)
   */
  static std::shared_ptr<cvedix_nodes::cvedix_node>
  createRestorationNode(const std::string &nodeName,
                        const std::map<std::string, std::string> &params,
                        const CreateInstanceRequest &req);

  /**
   * @brief Create YOLOv11 detector node
   */
  static std::shared_ptr<cvedix_nodes::cvedix_node>
  createYOLOv11DetectorNode(const std::string &nodeName,
                            const std::map<std::string, std::string> &params,
                            const CreateInstanceRequest &req);

  /**
   * @brief Create YOLOv11 plate detector node (ONNX)
   */
  static std::shared_ptr<cvedix_nodes::cvedix_node>
  createYOLOv11PlateDetectorNode(const std::string &nodeName,
                                 const std::map<std::string, std::string> &params,
                                 const CreateInstanceRequest &req);

  /**
   * @brief Create face swap node
   */
  static std::shared_ptr<cvedix_nodes::cvedix_node>
  createFaceSwapNode(const std::string &nodeName,
                     const std::map<std::string, std::string> &params,
                     const CreateInstanceRequest &req);

  /**
   * @brief Create InsightFace recognition node
   */
  static std::shared_ptr<cvedix_nodes::cvedix_node> createInsightFaceRecognitionNode(
      const std::string &nodeName,
      const std::map<std::string, std::string> &params,
      const CreateInstanceRequest &req);

#ifdef CVEDIX_WITH_LLM
  /**
   * @brief Create MLLM analyser node
   */
  static std::shared_ptr<cvedix_nodes::cvedix_node>
  createMLLMAnalyserNode(const std::string &nodeName,
                         const std::map<std::string, std::string> &params,
                         const CreateInstanceRequest &req);
#endif

private:
  /**
   * @brief Helper function to log GPU availability
   */
  static void logGPUAvailability();
};

