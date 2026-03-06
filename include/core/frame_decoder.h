#pragma once

#include "core/frame_input_queue.h"
#include <opencv2/opencv.hpp>
#include <string>
#include <vector>
#include <memory>

/**
 * @brief Frame Decoder
 * 
 * Decodes encoded frames (H.264/H.265) and compressed frames (JPEG/PNG).
 * Supports hardware and software decoders.
 */
class FrameDecoder {
public:
  /**
   * @brief Constructor
   */
  FrameDecoder();
  
  /**
   * @brief Destructor
   */
  ~FrameDecoder() = default;
  
  /**
   * @brief Decode encoded frame (H.264/H.265)
   * @param data Encoded frame data
   * @param codecId Codec ID ("h264", "h265", etc.)
   * @param frame Output decoded frame (BGR format)
   * @return true if successful
   */
  bool decodeEncodedFrame(const std::vector<uint8_t>& data,
                         const std::string& codecId,
                         cv::Mat& frame);
  
  /**
   * @brief Decode compressed frame (JPEG, PNG, BMP, etc.)
   * @param data Compressed frame data
   * @param frame Output decoded frame (BGR format)
   * @return true if successful
   */
  bool decodeCompressedFrame(const std::vector<uint8_t>& data,
                            cv::Mat& frame);
  
  /**
   * @brief Detect compressed image format from data
   * @param data Image data
   * @return Format string ("jpeg", "png", "bmp", etc.) or empty if unknown
   */
  std::string detectImageFormat(const std::vector<uint8_t>& data) const;

private:
  /**
   * @brief Decode H.264 frame using OpenCV VideoWriter/VideoCapture
   * Note: This is a simplified implementation. For production, consider
   * using FFmpeg or GStreamer for better codec support.
   * @param data H.264 encoded data
   * @param frame Output decoded frame
   * @return true if successful
   */
  bool decodeH264(const std::vector<uint8_t>& data, cv::Mat& frame);
  
  /**
   * @brief Decode H.265/HEVC frame
   * Note: Similar to H.264, simplified implementation
   * @param data H.265 encoded data
   * @param frame Output decoded frame
   * @return true if successful
   */
  bool decodeH265(const std::vector<uint8_t>& data, cv::Mat& frame);
  
  /**
   * @brief Validate encoded frame data
   * @param data Frame data
   * @param codecId Codec ID
   * @return true if data appears valid
   */
  bool validateEncodedData(const std::vector<uint8_t>& data,
                          const std::string& codecId) const;
};

