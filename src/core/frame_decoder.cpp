#include "core/frame_decoder.h"
#include <iostream>
#include <fstream>
#include <cstdlib>
#include <ctime>
#include <random>

FrameDecoder::FrameDecoder() {
}

bool FrameDecoder::decodeEncodedFrame(const std::vector<uint8_t>& data,
                                     const std::string& codecId,
                                     cv::Mat& frame) {
  if (data.empty()) {
    std::cerr << "[FrameDecoder] Error: Empty encoded frame data" << std::endl;
    return false;
  }
  
  // Validate data
  if (!validateEncodedData(data, codecId)) {
    std::cerr << "[FrameDecoder] Error: Invalid encoded frame data for codec: " 
              << codecId << std::endl;
    return false;
  }
  
  // Normalize codec ID
  std::string normalizedCodec = codecId;
  std::transform(normalizedCodec.begin(), normalizedCodec.end(),
                 normalizedCodec.begin(), ::tolower);
  
  // Decode based on codec
  if (normalizedCodec == "h264") {
    return decodeH264(data, frame);
  } else if (normalizedCodec == "h265" || normalizedCodec == "hevc") {
    return decodeH265(data, frame);
  } else {
    std::cerr << "[FrameDecoder] Error: Unsupported codec: " << codecId 
              << std::endl;
    return false;
  }
}

bool FrameDecoder::decodeCompressedFrame(const std::vector<uint8_t>& data,
                                        cv::Mat& frame) {
  if (data.empty()) {
    std::cerr << "[FrameDecoder] Error: Empty compressed frame data" 
              << std::endl;
    return false;
  }
  
  // Use OpenCV imdecode to decode compressed image
  // This supports JPEG, PNG, BMP, TIFF, WebP, etc.
  try {
    frame = cv::imdecode(data, cv::IMREAD_COLOR);
    
    if (frame.empty()) {
      std::cerr << "[FrameDecoder] Error: Failed to decode compressed frame" 
                << std::endl;
      return false;
    }
    
    // Ensure BGR format (imdecode returns BGR by default)
    if (frame.channels() == 3) {
      // Already BGR, good
      return true;
    } else if (frame.channels() == 1) {
      // Convert grayscale to BGR
      cv::cvtColor(frame, frame, cv::COLOR_GRAY2BGR);
      return true;
    } else {
      std::cerr << "[FrameDecoder] Warning: Unexpected channel count: " 
                << frame.channels() << std::endl;
      return false;
    }
  } catch (const cv::Exception& e) {
    std::cerr << "[FrameDecoder] OpenCV exception: " << e.what() << std::endl;
    return false;
  } catch (const std::exception& e) {
    std::cerr << "[FrameDecoder] Exception: " << e.what() << std::endl;
    return false;
  }
}

std::string FrameDecoder::detectImageFormat(const std::vector<uint8_t>& data) const {
  if (data.size() < 4) {
    return "";
  }
  
  // Check magic bytes for common formats
  // JPEG: FF D8 FF
  if (data.size() >= 3 && data[0] == 0xFF && data[1] == 0xD8 && data[2] == 0xFF) {
    return "jpeg";
  }
  
  // PNG: 89 50 4E 47
  if (data.size() >= 4 && data[0] == 0x89 && data[1] == 0x50 && 
      data[2] == 0x4E && data[3] == 0x47) {
    return "png";
  }
  
  // BMP: 42 4D
  if (data.size() >= 2 && data[0] == 0x42 && data[1] == 0x4D) {
    return "bmp";
  }
  
  // TIFF: 49 49 2A 00 or 4D 4D 00 2A
  if (data.size() >= 4 && 
      ((data[0] == 0x49 && data[1] == 0x49 && data[2] == 0x2A && data[3] == 0x00) ||
       (data[0] == 0x4D && data[1] == 0x4D && data[2] == 0x00 && data[3] == 0x2A))) {
    return "tiff";
  }
  
  return "";
}

bool FrameDecoder::decodeH264(const std::vector<uint8_t>& data, cv::Mat& frame) {
  if (data.empty()) {
    std::cerr << "[FrameDecoder] Error: Empty H.264 data" << std::endl;
    return false;
  }
  
  // Use OpenCV with GStreamer backend to decode H.264
  // GStreamer pipeline: filesrc -> h264parse -> avdec_h264 -> videoconvert -> appsink
  // Note: Using temporary file approach for simplicity. For production, consider using
  // appsrc with memory buffer or named pipe for better performance.
  
  try {
    // Generate unique temporary file name
    static std::random_device rd;
    static std::mt19937 gen(rd());
    std::string tempFile = "/tmp/h264_frame_" + std::to_string(std::time(nullptr)) + "_" + 
                          std::to_string(gen()) + ".h264";
    
    // Write H.264 data to temporary file
    std::ofstream outFile(tempFile, std::ios::binary);
    if (!outFile.is_open()) {
      std::cerr << "[FrameDecoder] Error: Cannot create temporary file for H.264 decoding" << std::endl;
      return false;
    }
    outFile.write(reinterpret_cast<const char*>(data.data()), data.size());
    outFile.close();
    
    // Build GStreamer pipeline string
    // filesrc reads from file, h264parse parses NAL units, avdec_h264 decodes, videoconvert converts to BGR
    std::string pipeline = "filesrc location=" + tempFile + 
                          " ! h264parse ! avdec_h264 ! videoconvert ! video/x-raw,format=BGR ! appsink";
    
    // Use OpenCV VideoCapture with GStreamer backend
    cv::VideoCapture cap(pipeline, cv::CAP_GSTREAMER);
    
    bool success = false;
    if (cap.isOpened()) {
      // Read frame
      success = cap.read(frame);
      cap.release();
    } else {
      std::cerr << "[FrameDecoder] Error: Cannot open GStreamer pipeline for H.264 decoding. "
                << "Make sure GStreamer plugins (h264parse, avdec_h264, videoconvert) are installed." << std::endl;
    }
    
    // Clean up temp file
    std::remove(tempFile.c_str());
    
    if (!success || frame.empty()) {
      std::cerr << "[FrameDecoder] Error: Failed to decode H.264 frame" << std::endl;
      return false;
    }
    
    return true;
  } catch (const cv::Exception& e) {
    std::cerr << "[FrameDecoder] OpenCV exception in H.264 decoding: " << e.what() << std::endl;
    return false;
  } catch (const std::exception& e) {
    std::cerr << "[FrameDecoder] Exception in H.264 decoding: " << e.what() << std::endl;
    return false;
  }
}

bool FrameDecoder::decodeH265(const std::vector<uint8_t>& data, cv::Mat& frame) {
  if (data.empty()) {
    std::cerr << "[FrameDecoder] Error: Empty H.265 data" << std::endl;
    return false;
  }
  
  // Use OpenCV with GStreamer backend to decode H.265/HEVC
  // GStreamer pipeline: filesrc -> h265parse -> avdec_h265 -> videoconvert -> appsink
  // Note: Using temporary file approach for simplicity. For production, consider using
  // appsrc with memory buffer or named pipe for better performance.
  
  try {
    // Generate unique temporary file name
    static std::random_device rd;
    static std::mt19937 gen(rd());
    std::string tempFile = "/tmp/h265_frame_" + std::to_string(std::time(nullptr)) + "_" + 
                          std::to_string(gen()) + ".h265";
    
    // Write H.265 data to temporary file
    std::ofstream outFile(tempFile, std::ios::binary);
    if (!outFile.is_open()) {
      std::cerr << "[FrameDecoder] Error: Cannot create temporary file for H.265 decoding" << std::endl;
      return false;
    }
    outFile.write(reinterpret_cast<const char*>(data.data()), data.size());
    outFile.close();
    
    // Build GStreamer pipeline string
    // filesrc reads from file, h265parse parses NAL units, avdec_h265 decodes, videoconvert converts to BGR
    std::string pipeline = "filesrc location=" + tempFile + 
                          " ! h265parse ! avdec_h265 ! videoconvert ! video/x-raw,format=BGR ! appsink";
    
    // Use OpenCV VideoCapture with GStreamer backend
    cv::VideoCapture cap(pipeline, cv::CAP_GSTREAMER);
    
    bool success = false;
    if (cap.isOpened()) {
      // Read frame
      success = cap.read(frame);
      cap.release();
    } else {
      std::cerr << "[FrameDecoder] Error: Cannot open GStreamer pipeline for H.265 decoding. "
                << "Make sure GStreamer plugins (h265parse, avdec_h265, videoconvert) are installed." << std::endl;
    }
    
    // Clean up temp file
    std::remove(tempFile.c_str());
    
    if (!success || frame.empty()) {
      std::cerr << "[FrameDecoder] Error: Failed to decode H.265 frame" << std::endl;
      return false;
    }
    
    return true;
  } catch (const cv::Exception& e) {
    std::cerr << "[FrameDecoder] OpenCV exception in H.265 decoding: " << e.what() << std::endl;
    return false;
  } catch (const std::exception& e) {
    std::cerr << "[FrameDecoder] Exception in H.265 decoding: " << e.what() << std::endl;
    return false;
  }
}

bool FrameDecoder::validateEncodedData(const std::vector<uint8_t>& data,
                                       const std::string& /* codecId */) const {
  if (data.size() < 4) {
    return false;
  }
  
  // Basic validation: check for NAL unit start codes
  // H.264/H.265 NAL units typically start with 0x00 0x00 0x00 0x01 or 0x00 0x00 0x01
  bool hasStartCode = false;
  
  // Check for 4-byte start code: 0x00 0x00 0x00 0x01
  if (data.size() >= 4 && data[0] == 0x00 && data[1] == 0x00 && 
      data[2] == 0x00 && data[3] == 0x01) {
    hasStartCode = true;
  }
  
  // Check for 3-byte start code: 0x00 0x00 0x01
  if (!hasStartCode && data.size() >= 3 && data[0] == 0x00 && 
      data[1] == 0x00 && data[2] == 0x01) {
    hasStartCode = true;
  }
  
  // If no start code found, data might still be valid (e.g., Annex-B format)
  // For now, accept any non-empty data
  return !data.empty();
}

