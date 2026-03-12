#include "api/recognition_handler.h"
#include "config/system_config.h"
#include "core/ai_runtime_facade.h"
#include "core/inference_session.h"
#include "core/logger.h"
#include "core/logging_flags.h"
#include "core/metrics_interceptor.h"
#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <drogon/HttpResponse.h>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <json/json.h>
#include <map>
#include <opencv2/dnn.hpp>
#include <opencv2/objdetect.hpp>
#include <opencv2/opencv.hpp>
#include <random>
#include <set>
#include <sstream>
#include <sys/wait.h>
#include <unistd.h>

// Static storage members
std::unordered_map<std::string, std::vector<std::string>>
    RecognitionHandler::face_subjects_storage_;
std::unordered_map<std::string, std::string>
    RecognitionHandler::image_id_to_subject_;
std::unordered_map<std::string, std::string>
    RecognitionHandler::face_images_storage_;
std::mutex RecognitionHandler::storage_mutex_;

HttpResponsePtr
RecognitionHandler::createErrorResponse(int statusCode,
                                        const std::string &error,
                                        const std::string &message) const {
  Json::Value errorResponse;
  errorResponse["error"] = error;
  if (!message.empty()) {
    errorResponse["message"] = message;
  }

  auto resp = HttpResponse::newHttpJsonResponse(errorResponse);
  resp->setStatusCode(static_cast<HttpStatusCode>(statusCode));
  resp->addHeader("Access-Control-Allow-Origin", "*");
  resp->addHeader("Access-Control-Allow-Methods", "POST, OPTIONS");
  resp->addHeader("Access-Control-Allow-Headers", "Content-Type");

  return resp;
}

// Helper function: cosine similarity
static float cosine_similarity(const std::vector<float> &a,
                               const std::vector<float> &b) {
  if (a.size() != b.size() || a.empty())
    return 0.0f;

  float dot_product = 0.0f;
  float norm_a = 0.0f;
  float norm_b = 0.0f;

  for (size_t i = 0; i < a.size(); i++) {
    dot_product += a[i] * b[i];
    norm_a += a[i] * a[i];
    norm_b += b[i] * b[i];
  }

  float denominator = std::sqrt(norm_a) * std::sqrt(norm_b);
  if (denominator < 1e-6)
    return 0.0f;

  return dot_product / denominator;
}

// Helper function: Average embeddings
static std::vector<float>
average_embeddings(const std::vector<std::vector<float>> &embeddings) {
  if (embeddings.empty())
    return std::vector<float>();

  // Pick the first non-empty embedding as the reference dimension
  size_t dim = 0;
  for (const auto &emb : embeddings) {
    if (!emb.empty()) {
      dim = emb.size();
      break;
    }
  }
  if (dim == 0)
    return std::vector<float>();

  std::vector<float> avg_embedding(dim, 0.0f);

  size_t used_count = 0;
  for (const auto &emb : embeddings) {
    if (emb.size() != dim)
      continue;
    for (size_t i = 0; i < dim; i++) {
      avg_embedding[i] += emb[i];
    }
    used_count++;
  }

  if (used_count == 0)
    return std::vector<float>();

  float count = static_cast<float>(used_count);
  for (size_t i = 0; i < dim; i++) {
    avg_embedding[i] /= count;
  }

  // L2 normalize
  float norm = 0.0f;
  for (float val : avg_embedding) {
    norm += val * val;
  }
  norm = std::sqrt(norm);
  if (norm > 1e-6) {
    for (float &val : avg_embedding) {
      val /= norm;
    }
  }

  return avg_embedding;
}

// AIRuntimeFacade for decode + infer (used by processFaceRecognition)
static core::AIRuntimeFacade &getFaceRuntimeFacade() {
  static core::AIRuntimeFacade facade;
  return facade;
}

// Helper function: Face alignment using landmarks
static cv::Mat align_face_using_landmarks(const cv::Mat &image,
                                          const cv::Mat &faces, int face_idx) {
  // YuNet format: (x, y, w, h, re_x, re_y, le_x, le_y, nt_x, nt_y, rcm_x,
  // rcm_y, lcm_x, lcm_y, score)
  float re_x = faces.at<float>(face_idx, 4);
  float re_y = faces.at<float>(face_idx, 5);
  float le_x = faces.at<float>(face_idx, 6);
  float le_y = faces.at<float>(face_idx, 7);
  float nt_x = faces.at<float>(face_idx, 8);
  float nt_y = faces.at<float>(face_idx, 9);
  float rcm_x = faces.at<float>(face_idx, 10);
  float rcm_y = faces.at<float>(face_idx, 11);
  float lcm_x = faces.at<float>(face_idx, 12);
  float lcm_y = faces.at<float>(face_idx, 13);

  // Standard face template for 112x112 (InsightFace)
  float dst[5][2] = {
      {38.2946f, 51.6963f}, // right eye
      {73.5318f, 51.5014f}, // left eye
      {56.0252f, 71.7366f}, // nose tip
      {41.5493f, 92.3655f}, // right mouth corner
      {70.7299f, 92.2041f}  // left mouth corner
  };

  float src[5][2] = {
      {re_x, re_y}, {le_x, le_y}, {nt_x, nt_y}, {rcm_x, rcm_y}, {lcm_x, lcm_y}};

  // Compute similarity transform matrix
  float src_mean[2] = {
      (src[0][0] + src[1][0] + src[2][0] + src[3][0] + src[4][0]) / 5.0f,
      (src[0][1] + src[1][1] + src[2][1] + src[3][1] + src[4][1]) / 5.0f};
  float dst_mean[2] = {56.0262f, 71.9008f};

  float src_demean[5][2], dst_demean[5][2];
  for (int i = 0; i < 5; i++) {
    src_demean[i][0] = src[i][0] - src_mean[0];
    src_demean[i][1] = src[i][1] - src_mean[1];
    dst_demean[i][0] = dst[i][0] - dst_mean[0];
    dst_demean[i][1] = dst[i][1] - dst_mean[1];
  }

  double A00 = 0.0, A01 = 0.0, A10 = 0.0, A11 = 0.0;
  for (int i = 0; i < 5; i++) {
    A00 += dst_demean[i][0] * src_demean[i][0];
    A01 += dst_demean[i][0] * src_demean[i][1];
    A10 += dst_demean[i][1] * src_demean[i][0];
    A11 += dst_demean[i][1] * src_demean[i][1];
  }
  A00 /= 5.0;
  A01 /= 5.0;
  A10 /= 5.0;
  A11 /= 5.0;

  double detA = A00 * A11 - A01 * A10;
  double d[2] = {1.0, (detA < 0) ? -1.0 : 1.0};

  cv::Mat A = (cv::Mat_<double>(2, 2) << A00, A01, A10, A11);
  cv::Mat s, u, vt;
  cv::SVD::compute(A, s, u, vt);

  double smax = std::max(s.at<double>(0), s.at<double>(1));
  double tol = smax * 2 * FLT_MIN;
  int rank = 0;
  if (s.at<double>(0) > tol)
    rank++;
  if (s.at<double>(1) > tol)
    rank++;

  if (rank == 0) {
    cv::Mat aligned;
    cv::resize(image, aligned, cv::Size(112, 112));
    return aligned;
  }

  cv::Mat T = u * cv::Mat::diag(cv::Mat(cv::Vec2d(d[0], d[1]))) * vt;

  double var1 = 0.0, var2 = 0.0;
  for (int i = 0; i < 5; i++) {
    var1 += src_demean[i][0] * src_demean[i][0];
    var2 += src_demean[i][1] * src_demean[i][1];
  }
  var1 /= 5.0;
  var2 /= 5.0;

  double scale =
      1.0 / (var1 + var2) * (s.at<double>(0) * d[0] + s.at<double>(1) * d[1]);
  double TS[2] = {
      T.at<double>(0, 0) * src_mean[0] + T.at<double>(0, 1) * src_mean[1],
      T.at<double>(1, 0) * src_mean[0] + T.at<double>(1, 1) * src_mean[1]};

  cv::Mat transform_mat =
      (cv::Mat_<double>(2, 3) << T.at<double>(0, 0) * scale,
       T.at<double>(0, 1) * scale, dst_mean[0] - scale * TS[0],
       T.at<double>(1, 0) * scale, T.at<double>(1, 1) * scale,
       dst_mean[1] - scale * TS[1]);

  cv::Mat aligned;
  cv::warpAffine(image, aligned, transform_mat, cv::Size(112, 112),
                 cv::INTER_LINEAR);
  return aligned;
}

// Helper function: extract embedding from aligned face image
static std::vector<float>
extract_embedding_from_image(const cv::Mat &aligned_face,
                             const std::string &onnx_model_path) {
  try {
    cv::dnn::Net net = cv::dnn::readNetFromONNX(onnx_model_path);
    if (net.empty()) {
      if (isApiLoggingEnabled()) {
        PLOG_WARNING << "[RecognitionHandler] Failed to load ONNX model from: "
                     << onnx_model_path;
      }
      return std::vector<float>();
    }

#ifdef CVEDIX_WITH_CUDA
    net.setPreferableBackend(cv::dnn::DNN_BACKEND_CUDA);
    net.setPreferableTarget(cv::dnn::DNN_TARGET_CUDA);
#else
    net.setPreferableBackend(cv::dnn::DNN_BACKEND_OPENCV);
    net.setPreferableTarget(cv::dnn::DNN_TARGET_CPU);
#endif

    cv::Mat rgb;
    cv::cvtColor(aligned_face, rgb, cv::COLOR_BGR2RGB);

    if (rgb.rows != 112 || rgb.cols != 112) {
      cv::resize(rgb, rgb, cv::Size(112, 112), 0, 0, cv::INTER_LINEAR);
    }

    cv::Mat blob;
    cv::dnn::blobFromImage(rgb, blob, 1.0f / 128.0f, cv::Size(),
                           cv::Scalar(127.5f, 127.5f, 127.5f), false, false,
                           CV_32F);

    net.setInput(blob);
    std::vector<cv::Mat> outputs;
    
    // Wrap forward() in try-catch to handle OpenCV DNN shape mismatch errors
    try {
      net.forward(outputs, net.getUnconnectedOutLayersNames());
    } catch (const cv::Exception &e) {
      // Check if this is a shape mismatch error (Eltwise layer issue)
      std::string error_msg = e.what();
      bool is_shape_mismatch =
          (error_msg.find("getMemoryShapes") != std::string::npos ||
           error_msg.find("eltwise_layer") != std::string::npos ||
           error_msg.find("Assertion failed") != std::string::npos ||
           error_msg.find("inputs[vecIdx][j]") != std::string::npos ||
           error_msg.find("Eltwise") != std::string::npos ||
           e.code == cv::Error::StsAssert);
      
      if (is_shape_mismatch) {
        if (isApiLoggingEnabled()) {
          PLOG_ERROR << "[RecognitionHandler] OpenCV DNN shape mismatch error "
                        "in ONNX model forward pass";
          PLOG_ERROR << "[RecognitionHandler] Error code: " << e.code
                     << ", Error message: " << e.msg;
          PLOG_ERROR << "[RecognitionHandler] This may indicate an "
                        "incompatibility between the ONNX model and OpenCV DNN";
          PLOG_ERROR << "[RecognitionHandler] Model path: " << onnx_model_path;
          PLOG_ERROR << "[RecognitionHandler] Input image size: " << rgb.cols
                     << "x" << rgb.rows;
        }
      } else {
        if (isApiLoggingEnabled()) {
          PLOG_ERROR << "[RecognitionHandler] OpenCV DNN forward error: "
                     << e.msg;
          PLOG_ERROR << "[RecognitionHandler] Error code: " << e.code;
        }
      }
      return std::vector<float>();
    } catch (const std::exception &e) {
      if (isApiLoggingEnabled()) {
        PLOG_ERROR << "[RecognitionHandler] Exception during ONNX model "
                      "forward pass: "
                   << e.what();
      }
      return std::vector<float>();
    }

    if (outputs.empty()) {
      if (isApiLoggingEnabled()) {
        PLOG_WARNING << "[RecognitionHandler] ONNX model forward returned "
                        "empty outputs";
      }
      return std::vector<float>();
    }

    const cv::Mat &output = outputs[0];
    int emb_dim = (output.dims == 2) ? output.size[1] : output.size[0];

    std::vector<float> embedding(emb_dim);
    const float *output_ptr = output.ptr<float>();
    std::copy(output_ptr, output_ptr + emb_dim, embedding.begin());

    // L2 normalize
    float norm = 0.0f;
    for (float val : embedding) {
      norm += val * val;
    }
    norm = std::sqrt(norm);
    if (norm > 1e-6) {
      for (float &val : embedding) {
        val /= norm;
      }
    }

    return embedding;
  } catch (const cv::Exception &e) {
    if (isApiLoggingEnabled()) {
      PLOG_ERROR << "[RecognitionHandler] OpenCV exception in "
                    "extract_embedding_from_image: "
                 << e.msg;
      PLOG_ERROR << "[RecognitionHandler] Error code: " << e.code;
    }
    return std::vector<float>();
  } catch (const std::exception &e) {
    if (isApiLoggingEnabled()) {
      PLOG_ERROR << "[RecognitionHandler] Exception in "
                    "extract_embedding_from_image: "
                 << e.what();
    }
    return std::vector<float>();
  } catch (...) {
    if (isApiLoggingEnabled()) {
      PLOG_ERROR << "[RecognitionHandler] Unknown exception in "
                    "extract_embedding_from_image";
    }
    return std::vector<float>();
  }
}

// Helper function: Check if face database connection is enabled
static bool isDatabaseConnectionEnabled() {
  try {
    auto &systemConfig = SystemConfig::getInstance();
    Json::Value faceDbConfig = systemConfig.getConfigSection("face_database");

    if (isApiLoggingEnabled()) {
      PLOG_DEBUG << "[isDatabaseConnectionEnabled] Config section: "
                 << (faceDbConfig.isNull() ? "null" : "exists");
      if (!faceDbConfig.isNull()) {
        PLOG_DEBUG << "[isDatabaseConnectionEnabled] Has 'enabled': "
                   << faceDbConfig.isMember("enabled");
        if (faceDbConfig.isMember("enabled")) {
          PLOG_DEBUG << "[isDatabaseConnectionEnabled] Enabled value: "
                     << faceDbConfig["enabled"].asBool();
        }
      }
    }

    if (faceDbConfig.isNull() || !faceDbConfig.isMember("enabled")) {
      if (isApiLoggingEnabled()) {
        PLOG_DEBUG << "[isDatabaseConnectionEnabled] Database not enabled "
                      "(config null or missing enabled field)";
      }
      return false;
    }

    bool enabled = faceDbConfig["enabled"].asBool();
    if (isApiLoggingEnabled()) {
      PLOG_DEBUG << "[isDatabaseConnectionEnabled] Database enabled: "
                 << enabled;
    }
    return enabled;
  } catch (const std::exception &e) {
    if (isApiLoggingEnabled()) {
      PLOG_ERROR << "[isDatabaseConnectionEnabled] Exception: " << e.what();
    }
    return false;
  } catch (...) {
    if (isApiLoggingEnabled()) {
      PLOG_ERROR << "[isDatabaseConnectionEnabled] Unknown exception";
    }
    return false;
  }
}

// Helper function: Get face database connection config
static Json::Value getDatabaseConnectionConfig() {
  try {
    auto &systemConfig = SystemConfig::getInstance();
    Json::Value faceDbConfig = systemConfig.getConfigSection("face_database");

    if (faceDbConfig.isNull() || !faceDbConfig.isMember("enabled") ||
        !faceDbConfig["enabled"].asBool()) {
      return Json::Value(Json::nullValue);
    }

    if (faceDbConfig.isMember("connection")) {
      return faceDbConfig["connection"];
    }

    return Json::Value(Json::nullValue);
  } catch (...) {
    return Json::Value(Json::nullValue);
  }
}

// Helper function: Resolve database file path with 3-tier fallback
// Following DIRECTORY_CREATION_GUIDE.md pattern
static std::string resolveDatabasePath() {
  // Priority 1: Environment variable (highest priority)
  const char *env_db = std::getenv("FACE_DATABASE_PATH");
  if (env_db && strlen(env_db) > 0) {
    std::string path = std::string(env_db);
    try {
      // Create parent directory if needed
      std::filesystem::path filePath(path);
      if (filePath.has_parent_path()) {
        std::filesystem::create_directories(filePath.parent_path());
      }
      if (isApiLoggingEnabled()) {
        PLOG_INFO
            << "[FaceDatabase] Using database path from FACE_DATABASE_PATH: "
            << path;
      }
      return path;
    } catch (const std::filesystem::filesystem_error &e) {
      if (e.code() == std::errc::permission_denied) {
        if (isApiLoggingEnabled()) {
          PLOG_WARNING << "[FaceDatabase] Cannot create " << path
                       << " (permission denied), trying fallback...";
        }
      }
    } catch (...) {
      if (isApiLoggingEnabled()) {
        PLOG_WARNING << "[FaceDatabase] Error with FACE_DATABASE_PATH, trying "
                        "fallback...";
      }
    }
  }

  // Priority 2: Production path (/opt/edgeos-api/data/face_database.txt)
  std::string production_path = "/opt/edgeos-api/data/face_database.txt";
  if (std::filesystem::exists(production_path)) {
    // Check if we have write permission by trying to open in write mode
    std::ofstream test_file(production_path, std::ios::out | std::ios::app);
    if (test_file.is_open() && test_file.good()) {
      test_file.close();
      if (isApiLoggingEnabled()) {
        PLOG_INFO << "[FaceDatabase] Found existing database: "
                  << production_path << " (production, writable)";
      }
      return production_path;
    } else {
      // File exists but we can't write to it, try fallback
      if (isApiLoggingEnabled()) {
        PLOG_WARNING << "[FaceDatabase] Found existing database but no write "
                        "permission: "
                     << production_path << ", trying fallback...";
      }
    }
  } else {
    // Try to create production directory
    try {
      std::filesystem::path filePath(production_path);
      if (filePath.has_parent_path()) {
        std::filesystem::create_directories(filePath.parent_path());
      }
      // Test write permission by creating a test file
      std::ofstream test_file(production_path);
      if (test_file.is_open() && test_file.good()) {
        test_file.close();
        std::filesystem::remove(production_path); // Remove test file
        if (isApiLoggingEnabled()) {
          PLOG_INFO << "[FaceDatabase] Created directory and will use: "
                    << production_path << " (production)";
        }
        return production_path;
      } else {
        if (isApiLoggingEnabled()) {
          PLOG_WARNING << "[FaceDatabase] Cannot create test file in "
                       << production_path << ", trying fallback...";
        }
      }
    } catch (const std::filesystem::filesystem_error &e) {
      if (e.code() == std::errc::permission_denied) {
        if (isApiLoggingEnabled()) {
          PLOG_WARNING << "[FaceDatabase] Cannot create " << production_path
                       << " (permission denied), trying fallback...";
        }
      }
    } catch (...) {
      if (isApiLoggingEnabled()) {
        PLOG_WARNING << "[FaceDatabase] Error creating " << production_path
                     << ", trying fallback...";
      }
    }
  }

  // Priority 3: User directory (~/.local/share/edgeos-api/face_database.txt)
  const char *home = std::getenv("HOME");
  if (home) {
    std::string user_path =
        std::string(home) + "/.local/share/edgeos-api/face_database.txt";
    try {
      std::filesystem::path filePath(user_path);
      if (filePath.has_parent_path()) {
        std::filesystem::create_directories(filePath.parent_path());
      }
      if (isApiLoggingEnabled()) {
        PLOG_INFO << "[FaceDatabase] Using fallback user directory: "
                  << user_path;
      }
      return user_path;
    } catch (...) {
      if (isApiLoggingEnabled()) {
        PLOG_WARNING << "[FaceDatabase] Cannot create user directory, using "
                        "last resort...";
      }
    }
  }

  // Last resort: Current directory (always works)
  std::string last_resort = "./face_database.txt";
  if (isApiLoggingEnabled()) {
    PLOG_INFO << "[FaceDatabase] Using last resort: " << last_resort
              << " (current directory)";
    PLOG_INFO << "[FaceDatabase] Note: To use production path, run: sudo mkdir "
                 "-p /opt/edgeos-api/data";
  }
  return last_resort;
}

// Face Database Class
class FaceDatabase {
private:
  std::map<std::string, std::vector<float>> database_;
  std::string db_file_path_;
  std::string onnx_model_path_;
  std::string detector_model_path_;

  std::string resolve_model_path(const std::string &relative_path) {
    std::filesystem::path full_path =
        std::filesystem::path(".") / relative_path;
    if (std::filesystem::exists(full_path))
      return full_path.string();

    std::filesystem::path current_path =
        std::filesystem::current_path() / relative_path;
    if (std::filesystem::exists(current_path))
      return current_path.string();
    if (std::filesystem::exists(relative_path))
      return relative_path;

    return full_path.string();
  }

  void load_database() {
    std::ifstream file(db_file_path_);
    if (!file.is_open()) {
      if (isApiLoggingEnabled()) {
        PLOG_DEBUG << "[FaceDatabase] Database file does not exist, creating "
                      "new file: "
                   << db_file_path_;
      }
      std::ofstream create_file(db_file_path_);
      return;
    }

    int line_number = 0;
    int loaded_count = 0;
    int error_count = 0;
    std::string line;

    while (std::getline(file, line)) {
      line_number++;

      // Trim whitespace
      line.erase(0, line.find_first_not_of(" \t\r\n"));
      line.erase(line.find_last_not_of(" \t\r\n") + 1);

      if (line.empty())
        continue;

      size_t pos = line.find('|');
      if (pos == std::string::npos) {
        if (isApiLoggingEnabled()) {
          PLOG_WARNING << "[FaceDatabase] Invalid format at line "
                       << line_number << ": missing '|' separator. Line: "
                       << line.substr(0, 50) << "...";
        }
        error_count++;
        continue;
      }

      std::string name = line.substr(0, pos);
      std::string embedding_str = line.substr(pos + 1);

      // Trim name
      name.erase(0, name.find_first_not_of(" \t"));
      name.erase(name.find_last_not_of(" \t") + 1);

      if (name.empty()) {
        if (isApiLoggingEnabled()) {
          PLOG_WARNING << "[FaceDatabase] Empty subject name at line "
                       << line_number;
        }
        error_count++;
        continue;
      }

      std::vector<float> embedding;
      std::stringstream ss(embedding_str);
      std::string value;
      bool parse_error = false;

      while (std::getline(ss, value, ',')) {
        // Trim value
        value.erase(0, value.find_first_not_of(" \t\r\n"));
        value.erase(value.find_last_not_of(" \t\r\n") + 1);

        if (value.empty())
          continue;

        try {
          float fval = std::stof(value);
          embedding.push_back(fval);
        } catch (const std::exception &e) {
          if (isApiLoggingEnabled()) {
            PLOG_WARNING << "[FaceDatabase] Failed to parse float value '"
                         << value << "' for subject '" << name << "' at line "
                         << line_number << ": " << e.what();
          }
          parse_error = true;
          break;
        }
      }

      if (parse_error) {
        error_count++;
        continue;
      }

      if (embedding.empty()) {
        if (isApiLoggingEnabled()) {
          PLOG_WARNING << "[FaceDatabase] Empty embedding for subject '" << name
                       << "' at line " << line_number;
        }
        error_count++;
        continue;
      }

      database_[name] = embedding;
      loaded_count++;

      if (isApiLoggingEnabled() && line_number <= 5) {
        PLOG_DEBUG << "[FaceDatabase] Loaded subject '" << name << "' with "
                   << embedding.size() << " dimensions";
      }
    }

    if (isApiLoggingEnabled()) {
      PLOG_INFO << "[FaceDatabase] Loaded " << loaded_count << " face(s) from "
                << db_file_path_;
      if (error_count > 0) {
        PLOG_WARNING << "[FaceDatabase] Encountered " << error_count
                     << " error(s) while loading database";
      }
    }
  }

  void save_database() {
    // Ensure parent directory exists
    try {
      std::filesystem::path filePath(db_file_path_);
      if (filePath.has_parent_path()) {
        std::filesystem::create_directories(filePath.parent_path());
      }
    } catch (const std::exception &e) {
      if (isApiLoggingEnabled()) {
        PLOG_WARNING << "[FaceDatabase] Could not create parent directory for "
                     << db_file_path_ << ": " << e.what();
      }
    }

    std::ofstream file(db_file_path_, std::ios::out | std::ios::trunc);
    if (!file.is_open()) {
      if (isApiLoggingEnabled()) {
        PLOG_ERROR
            << "[FaceDatabase] Failed to open database file for writing: "
            << db_file_path_;
        PLOG_ERROR << "[FaceDatabase] Check file permissions and ensure the "
                      "directory exists";
      }
      return;
    }

    for (const auto &[name, embedding] : database_) {
      file << name << "|";
      for (size_t i = 0; i < embedding.size(); i++) {
        file << std::fixed << std::setprecision(6) << embedding[i];
        if (i < embedding.size() - 1)
          file << ",";
      }
      file << "\n";
    }

    // Explicitly flush and close to ensure data is written to disk
    file.flush();
    if (!file.good()) {
      if (isApiLoggingEnabled()) {
        PLOG_ERROR << "[FaceDatabase] Error flushing database file: "
                   << db_file_path_;
      }
    }
    file.close();

    // Verify file was written successfully
    if (!std::filesystem::exists(db_file_path_)) {
      if (isApiLoggingEnabled()) {
        PLOG_ERROR << "[FaceDatabase] Database file was not created: "
                   << db_file_path_;
      }
      return;
    }

    if (isApiLoggingEnabled()) {
      PLOG_INFO << "[FaceDatabase] Successfully saved " << database_.size()
                << " face(s) to " << db_file_path_;
      try {
        auto file_size = std::filesystem::file_size(db_file_path_);
        PLOG_DEBUG << "[FaceDatabase] Database file size: " << file_size
                   << " bytes";
      } catch (...) {
        // Ignore file size check errors
      }
    }
  }

public:
  FaceDatabase(const std::string &db_path = "")
      : db_file_path_(db_path.empty() ? resolveDatabasePath() : db_path) {

    if (isApiLoggingEnabled()) {
      PLOG_INFO << "[FaceDatabase] Initializing FaceDatabase with db_path: "
                << db_file_path_;
    }

    load_database();

    // Find ONNX model (env/config override: FACE_RECOGNIZER_PATH)
    const char *env_recognizer = std::getenv("FACE_RECOGNIZER_PATH");
    std::vector<std::string> model_paths;
    if (env_recognizer && env_recognizer[0] != '\0') {
      model_paths.push_back(env_recognizer);
    }
    model_paths.insert(model_paths.end(), {
        "/opt/edgeos-api/models/face/face_recognition_sface_2021dec.onnx",
        "./models/face/face_recognition_sface_2021dec.onnx",
        "../models/face/face_recognition_sface_2021dec.onnx"});

    bool found_onnx = false;
    for (const auto &path : model_paths) {
      if (std::filesystem::exists(path)) {
        onnx_model_path_ = path;
        found_onnx = true;
        if (isApiLoggingEnabled()) {
          PLOG_INFO << "[FaceDatabase] Found recognition model: " << path;
        }
        break;
      }
    }

    if (!found_onnx && isApiLoggingEnabled()) {
      PLOG_WARNING
          << "[FaceDatabase] Recognition model not found. Checked paths:";
      for (const auto &path : model_paths) {
        PLOG_WARNING << "[FaceDatabase]   - " << path;
      }
    }

    // Find detector model (env override: FACE_DETECTOR_PATH, then defaults)
    const char *env_detector = std::getenv("FACE_DETECTOR_PATH");
    std::vector<std::string> detector_paths;
    if (env_detector && env_detector[0] != '\0') {
      detector_paths.push_back(env_detector);
    }
    detector_paths.insert(detector_paths.end(), {
        "/opt/edgeos-api/models/face/face_detection_yunet_2023mar.onnx",
        "/opt/edgeos-api/models/face/face_detection_yunet_2023mar_int8.onnx",
        "./models/face/face_detection_yunet_2023mar.onnx",
        "./models/face/face_detection_yunet_2023mar_int8.onnx",
        "../models/face/face_detection_yunet_2023mar.onnx",
        "../models/face/face_detection_yunet_2023mar_int8.onnx"});

    bool found_detector = false;
    for (const auto &path : detector_paths) {
      if (std::filesystem::exists(path)) {
        detector_model_path_ = path;
        found_detector = true;
        if (isApiLoggingEnabled()) {
          PLOG_INFO << "[FaceDatabase] Found detector model: " << path;
        }
        break;
      }
    }

    if (!found_detector && isApiLoggingEnabled()) {
      PLOG_WARNING << "[FaceDatabase] Detector model not found. Checked paths:";
      for (const auto &path : detector_paths) {
        PLOG_WARNING << "[FaceDatabase]   - " << path;
      }
    }
  }

  bool register_face_from_image(const std::vector<unsigned char> &imageData,
                                const std::string &person_name,
                                double detProbThreshold,
                                std::string &error_msg) {
    if (imageData.empty()) {
      error_msg = "Image data is empty";
      if (isApiLoggingEnabled()) {
        PLOG_ERROR << "[FaceDatabase] " << error_msg
                   << " for person: " << person_name;
      }
      return false;
    }

    cv::Mat image = cv::imdecode(imageData, cv::IMREAD_COLOR);
    if (image.empty()) {
      error_msg = "Failed to decode image data. Image may be corrupted or in "
                  "unsupported format.";
      if (isApiLoggingEnabled()) {
        PLOG_ERROR << "[FaceDatabase] " << error_msg
                   << ", image data size: " << imageData.size()
                   << " bytes, person: " << person_name;
      }
      return false;
    }

    if (isApiLoggingEnabled()) {
      PLOG_DEBUG << "[FaceDatabase] Decoded image: " << image.cols << "x"
                 << image.rows << " pixels";
    }

    if (detector_model_path_.empty()) {
      error_msg = "Face detector model not found. Please ensure "
                  "face_detection_yunet_2023mar.onnx or "
                  "face_detection_yunet_2023mar_int8.onnx exists in "
                  "/opt/edgeos-api/models/face/";
      if (isApiLoggingEnabled()) {
        PLOG_ERROR << "[FaceDatabase] " << error_msg;
      }
      return false;
    }

    cv::Ptr<cv::FaceDetectorYN> face_detector;
    try {
      face_detector = cv::FaceDetectorYN::create(
          detector_model_path_, "", cv::Size(320, 320),
          static_cast<float>(detProbThreshold), 0.3f, 5000,
          cv::dnn::DNN_BACKEND_OPENCV, cv::dnn::DNN_TARGET_CPU);
    } catch (const cv::Exception &e) {
      error_msg = "Failed to create face detector: " + std::string(e.what());
      if (isApiLoggingEnabled()) {
        PLOG_ERROR << "[FaceDatabase] " << error_msg;
        PLOG_ERROR << "[FaceDatabase] OpenCV error code: " << e.code;
      }
      return false;
    } catch (const std::exception &e) {
      error_msg = "Failed to create face detector: " + std::string(e.what());
      if (isApiLoggingEnabled()) {
        PLOG_ERROR << "[FaceDatabase] " << error_msg;
      }
      return false;
    }

    if (face_detector.empty()) {
      error_msg = "Face detector creation returned empty";
      if (isApiLoggingEnabled()) {
        PLOG_ERROR << "[FaceDatabase] " << error_msg
                   << ", model path: " << detector_model_path_;
      }
      return false;
    }

    face_detector->setInputSize(image.size());
    cv::Mat faces;
    try {
      face_detector->detect(image, faces);
    } catch (const cv::Exception &e) {
      error_msg = "Face detection exception: " + std::string(e.what());
      if (isApiLoggingEnabled()) {
        PLOG_ERROR << "[FaceDatabase] " << error_msg;
        PLOG_ERROR << "[FaceDatabase] OpenCV error code: " << e.code
                   << ", image size: " << image.cols << "x" << image.rows;
      }
      return false;
    }

    if (faces.rows == 0 || faces.empty()) {
      error_msg = "No face detected in image. Please ensure the image contains "
                  "a clear face. Image size: " +
                  std::to_string(image.cols) + "x" +
                  std::to_string(image.rows) +
                  ", detection threshold: " + std::to_string(detProbThreshold);
      return false;
    }

    if (isApiLoggingEnabled()) {
      PLOG_DEBUG << "[FaceDatabase] Detected " << faces.rows
                 << " face(s) in image";
    }

    float x = faces.at<float>(0, 0), y = faces.at<float>(0, 1);
    float w = faces.at<float>(0, 2), h = faces.at<float>(0, 3);

    x = std::max(0.0f, std::min(x, (float)(image.cols - 1)));
    y = std::max(0.0f, std::min(y, (float)(image.rows - 1)));
    w = std::max(1.0f, std::min(w, (float)(image.cols - x)));
    h = std::max(1.0f, std::min(h, (float)(image.rows - y)));

    cv::Mat aligned_face;
    if (faces.cols >= 15) {
      aligned_face = align_face_using_landmarks(image, faces, 0);
    } else {
      cv::Mat face_roi =
          image(cv::Rect((int)x, (int)y, (int)w, (int)h)).clone();
      cv::resize(face_roi, aligned_face, cv::Size(112, 112));
    }

    if (onnx_model_path_.empty()) {
      error_msg = "Face recognition model not found. Please ensure "
                  "face_recognition_sface_2021dec.onnx exists in "
                  "/opt/edgeos-api/models/face/";
      if (isApiLoggingEnabled()) {
        PLOG_ERROR << "[FaceDatabase] " << error_msg;
      }
      return false;
    }

    // Data augmentation
    std::vector<std::vector<float>> embeddings;

    std::vector<float> emb1 =
        extract_embedding_from_image(aligned_face, onnx_model_path_);
    if (!emb1.empty())
      embeddings.push_back(emb1);

    cv::Mat flipped;
    cv::flip(aligned_face, flipped, 1);
    std::vector<float> emb2 =
        extract_embedding_from_image(flipped, onnx_model_path_);
    if (!emb2.empty())
      embeddings.push_back(emb2);

    cv::Mat bright;
    aligned_face.convertTo(bright, -1, 1.0, 15);
    std::vector<float> emb3 =
        extract_embedding_from_image(bright, onnx_model_path_);
    if (!emb3.empty())
      embeddings.push_back(emb3);

    cv::Mat dark;
    aligned_face.convertTo(dark, -1, 1.0, -15);
    std::vector<float> emb4 =
        extract_embedding_from_image(dark, onnx_model_path_);
    if (!emb4.empty())
      embeddings.push_back(emb4);

    cv::Mat contrast;
    aligned_face.convertTo(contrast, -1, 1.1, 0);
    std::vector<float> emb5 =
        extract_embedding_from_image(contrast, onnx_model_path_);
    if (!emb5.empty())
      embeddings.push_back(emb5);

    if (embeddings.empty()) {
      error_msg = "Failed to extract face embeddings from image";
      if (isApiLoggingEnabled()) {
        PLOG_ERROR << "[FaceDatabase] " << error_msg
                   << ", model path: " << onnx_model_path_;
      }
      return false;
    }

    std::vector<float> final_embedding = average_embeddings(embeddings);
    database_[person_name] = final_embedding;
    save_database();
    return true;
  }

  std::vector<std::pair<std::string, std::vector<float>>>
  get_all_faces() const {
    std::vector<std::pair<std::string, std::vector<float>>> result;
    for (const auto &[name, embedding] : database_) {
      result.push_back({name, embedding});
    }
    return result;
  }

  bool remove_subject(const std::string &subject_name) {
    auto it = database_.find(subject_name);
    if (it == database_.end()) {
      return false;
    }
    database_.erase(it);
    save_database();
    if (isApiLoggingEnabled()) {
      PLOG_INFO << "[FaceDatabase] Removed subject '" << subject_name
                << "' from database";
    }
    return true;
  }

  void clear_all() {
    database_.clear();
    save_database();
    if (isApiLoggingEnabled()) {
      PLOG_INFO << "[FaceDatabase] Cleared all subjects from database";
    }
  }

  const std::string &get_detector_model_path() const {
    return detector_model_path_;
  }
  const std::string &get_onnx_model_path() const { return onnx_model_path_; }
  const std::string &get_database_path() const { return db_file_path_; }
  const std::map<std::string, std::vector<float>> &get_database() const {
    return database_;
  }

  size_t size() const { return database_.size(); }
};

// Global database instance
// Database Helper Class for MySQL/PostgreSQL operations
// TODO: Implement actual database connection using Drogon's DbClient or other
// library
class FaceDatabaseHelper {
private:
  Json::Value dbConfig_;
  bool enabled_;

public:
  FaceDatabaseHelper() { reloadConfig(); }

  void reloadConfig() {
    enabled_ = isDatabaseConnectionEnabled();
    if (enabled_) {
      dbConfig_ = getDatabaseConnectionConfig();
      if (dbConfig_.isNull()) {
        enabled_ = false;
        if (isApiLoggingEnabled()) {
          PLOG_WARNING
              << "[FaceDatabaseHelper] Database config is null, disabled";
        }
      } else {
        if (isApiLoggingEnabled()) {
          PLOG_INFO << "[FaceDatabaseHelper] Database connection enabled: "
                    << dbConfig_["type"].asString() << "://"
                    << dbConfig_["host"].asString() << ":"
                    << dbConfig_["port"].asInt() << "/"
                    << dbConfig_["database"].asString();
        }
      }
    } else {
      if (isApiLoggingEnabled()) {
        PLOG_DEBUG << "[FaceDatabaseHelper] Database connection not enabled";
      }
    }
  }

  bool isEnabled() const { return enabled_; }

  // Test database connection
  bool testConnection(std::string &error) {
    if (!enabled_) {
      error = "Database connection not enabled";
      return false;
    }

    std::string testSql = "SELECT 1";
    return executeMySQLCommand(testSql, error);
  }

  // Helper: Escape SQL string
  std::string escapeSqlString(const std::string &str) {
    std::string escaped;
    escaped.reserve(str.length() * 2);
    for (char c : str) {
      if (c == '\'') {
        escaped += "''";
      } else if (c == '\\') {
        escaped += "\\\\";
      } else {
        escaped += c;
      }
    }
    return escaped;
  }

  // Helper: Convert embedding vector to comma-separated string
  std::string embeddingToString(const std::vector<float> &embedding) {
    std::ostringstream oss;
    for (size_t i = 0; i < embedding.size(); i++) {
      if (i > 0)
        oss << ",";
      oss << std::fixed << std::setprecision(6) << embedding[i];
    }
    return oss.str();
  }

  // Helper: Execute MySQL command using temporary file for SQL and password
  bool executeMySQLCommand(const std::string &sql, std::string &error) {
    if (dbConfig_.isNull() || !dbConfig_.isMember("host") ||
        !dbConfig_.isMember("database") || !dbConfig_.isMember("username") ||
        !dbConfig_.isMember("password")) {
      error = "Database configuration incomplete";
      return false;
    }

    std::string host = dbConfig_["host"].asString();
    int port = dbConfig_.isMember("port") ? dbConfig_["port"].asInt() : 3306;
    std::string database = dbConfig_["database"].asString();
    std::string username = dbConfig_["username"].asString();
    std::string password = dbConfig_["password"].asString();

    // Create temporary file for SQL query
    char tmpFile[] = "/tmp/mysql_query_XXXXXX";
    int fd = mkstemp(tmpFile);
    if (fd == -1) {
      error = "Failed to create temporary file";
      return false;
    }

    // Write SQL to temp file
    ssize_t written = write(fd, sql.c_str(), sql.length());
    close(fd);

    if (written != static_cast<ssize_t>(sql.length())) {
      unlink(tmpFile);
      error = "Failed to write SQL to temporary file";
      return false;
    }

    // Build mysql command - use MYSQL_PWD environment variable for password
    // Use setenv to set password, then execute mysql command
    // This is safer than passing password in command line

    // Set environment variable
    if (setenv("MYSQL_PWD", password.c_str(), 1) != 0) {
      unlink(tmpFile);
      error = "Failed to set MYSQL_PWD environment variable";
      return false;
    }

    std::ostringstream cmd;
    cmd << "mysql -h " << host << " -P " << port << " -u " << username << " "
        << database << " < " << tmpFile << " 2>&1";

    if (isApiLoggingEnabled()) {
      PLOG_DEBUG << "[FaceDatabaseHelper] Executing MySQL command: mysql -h "
                 << host << " -P " << port << " -u " << username << " "
                 << database;
    }

    // Execute command
    FILE *pipe = popen(cmd.str().c_str(), "r");
    if (!pipe) {
      unlink(tmpFile);
      error = "Failed to execute MySQL command";
      return false;
    }

    char buffer[1024];
    std::string result;
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
      result += buffer;
    }

    int status = pclose(pipe);
    unlink(tmpFile);       // Clean up temp file
    unsetenv("MYSQL_PWD"); // Clear password from environment

    if (status != 0) {
      // Trim result
      if (!result.empty() && result.back() == '\n') {
        result.pop_back();
      }
      error = "MySQL command failed (exit code " + std::to_string(status) +
              "): " + result;
      if (isApiLoggingEnabled()) {
        PLOG_ERROR << "[FaceDatabaseHelper] MySQL error: " << error;
        PLOG_DEBUG << "[FaceDatabaseHelper] SQL (first 200 chars): "
                   << sql.substr(0, 200);
      }
      return false;
    }

    if (isApiLoggingEnabled() && !result.empty()) {
      PLOG_DEBUG << "[FaceDatabaseHelper] MySQL result: " << result;
    }

    return true;
  }

  // Helper: Execute MySQL SELECT query and return results
  bool executeMySQLQuery(const std::string &sql, std::string &result,
                         std::string &error) {
    if (dbConfig_.isNull() || !dbConfig_.isMember("host") ||
        !dbConfig_.isMember("database") || !dbConfig_.isMember("username") ||
        !dbConfig_.isMember("password")) {
      error = "Database configuration incomplete";
      return false;
    }

    std::string host = dbConfig_["host"].asString();
    int port = dbConfig_.isMember("port") ? dbConfig_["port"].asInt() : 3306;
    std::string database = dbConfig_["database"].asString();
    std::string username = dbConfig_["username"].asString();
    std::string password = dbConfig_["password"].asString();

    // Create temporary file for SQL query
    char tmpFile[] = "/tmp/mysql_query_XXXXXX";
    int fd = mkstemp(tmpFile);
    if (fd == -1) {
      error = "Failed to create temporary file";
      return false;
    }

    // Write SQL to temp file
    ssize_t written = write(fd, sql.c_str(), sql.length());
    close(fd);

    if (written != static_cast<ssize_t>(sql.length())) {
      unlink(tmpFile);
      error = "Failed to write SQL to temporary file";
      return false;
    }

    // Set environment variable for password
    if (setenv("MYSQL_PWD", password.c_str(), 1) != 0) {
      unlink(tmpFile);
      error = "Failed to set MYSQL_PWD environment variable";
      return false;
    }

    // Use -N (skip column names) and -B (batch mode, tab-separated) for clean
    // output
    std::ostringstream cmd;
    cmd << "mysql -h " << host << " -P " << port << " -u " << username
        << " -N -B " << database << " < " << tmpFile << " 2>&1";

    if (isApiLoggingEnabled()) {
      PLOG_DEBUG << "[FaceDatabaseHelper] Executing MySQL query: mysql -h "
                 << host << " -P " << port << " -u " << username << " "
                 << database;
    }

    // Execute command
    FILE *pipe = popen(cmd.str().c_str(), "r");
    if (!pipe) {
      unlink(tmpFile);
      error = "Failed to execute MySQL command";
      return false;
    }

    char buffer[1024];
    result.clear();
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
      result += buffer;
    }

    int status = pclose(pipe);
    unlink(tmpFile);       // Clean up temp file
    unsetenv("MYSQL_PWD"); // Clear password from environment

    if (status != 0) {
      // Trim result
      if (!result.empty() && result.back() == '\n') {
        result.pop_back();
      }
      error = "MySQL query failed (exit code " + std::to_string(status) +
              "): " + result;
      if (isApiLoggingEnabled()) {
        PLOG_ERROR << "[FaceDatabaseHelper] MySQL query error: " << error;
        PLOG_DEBUG << "[FaceDatabaseHelper] SQL (first 200 chars): "
                   << sql.substr(0, 200);
      }
      return false;
    }

    if (isApiLoggingEnabled() && !result.empty()) {
      PLOG_DEBUG << "[FaceDatabaseHelper] MySQL query result length: "
                 << result.length();
    }

    return true;
  }

  // Save face to database
  bool saveFace(const std::string &imageId, const std::string &subject,
                const std::string &base64Image,
                const std::vector<float> &embedding, std::string &error) {
    if (!enabled_) {
      error = "Database connection not enabled";
      return false;
    }

    if (dbConfig_.isNull()) {
      error = "Database configuration not available";
      return false;
    }

    // Escape strings for SQL
    std::string escapedImageId = escapeSqlString(imageId);
    std::string escapedSubject = escapeSqlString(subject);
    std::string escapedBase64 = escapeSqlString(base64Image);
    std::string embeddingStr = embeddingToString(embedding);
    std::string escapedEmbedding = escapeSqlString(embeddingStr);

    // Get current timestamp
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    std::ostringstream timestamp;
    timestamp << std::put_time(std::localtime(&time_t), "%Y-%m-%d %H:%M:%S");

    // Build SQL INSERT statement
    // Note: Column name is 'created_at' (with 'd'), not 'create_at'
    std::ostringstream sql;
    sql << "INSERT INTO face_libraries (image_id, subject, base64_image, "
           "embedding, created_at) VALUES ('"
        << escapedImageId << "', '" << escapedSubject << "', '" << escapedBase64
        << "', '" << escapedEmbedding << "', '" << timestamp.str() << "')";

    if (isApiLoggingEnabled()) {
      PLOG_INFO << "[FaceDatabaseHelper] Saving face to database: image_id="
                << imageId << ", subject=" << subject;
      PLOG_DEBUG
          << "[FaceDatabaseHelper] SQL: INSERT INTO face_libraries (image_id, "
             "subject, base64_image, embedding, created_at) VALUES (...)";
    }

    if (executeMySQLCommand(sql.str(), error)) {
      if (isApiLoggingEnabled()) {
        PLOG_INFO << "[FaceDatabaseHelper] Successfully saved face to "
                     "database: image_id="
                  << imageId << ", subject=" << subject;
      }
      return true;
    } else {
      if (isApiLoggingEnabled()) {
        PLOG_ERROR << "[FaceDatabaseHelper] Failed to save face to database: "
                   << error;
      }
    }

    return false;
  }

  // Load all faces from database
  bool loadAllFaces(std::map<std::string, std::vector<float>> &faces,
                    std::string &error) {
    if (!enabled_) {
      error = "Database connection not enabled";
      return false;
    }

    // Query all faces from database
    std::string sql = "SELECT subject, embedding FROM face_libraries";
    std::string result;

    if (!executeMySQLQuery(sql, result, error)) {
      if (isApiLoggingEnabled()) {
        PLOG_ERROR
            << "[FaceDatabaseHelper] Failed to load faces from database: "
            << error;
      }
      return false;
    }

    if (result.empty()) {
      // No faces in database
      faces.clear();
      if (isApiLoggingEnabled()) {
        PLOG_INFO << "[FaceDatabaseHelper] No faces found in database";
      }
      return true;
    }

    // Parse result - tab-separated values: subject\tembedding
    std::istringstream iss(result);
    std::string line;
    faces.clear();

    while (std::getline(iss, line)) {
      if (line.empty())
        continue;

      // Split by tab
      size_t tabPos = line.find('\t');
      if (tabPos == std::string::npos)
        continue;

      std::string subject = line.substr(0, tabPos);
      std::string embeddingStr = line.substr(tabPos + 1);

      // Trim whitespace
      subject.erase(0, subject.find_first_not_of(" \t\n\r"));
      subject.erase(subject.find_last_not_of(" \t\n\r") + 1);
      embeddingStr.erase(0, embeddingStr.find_first_not_of(" \t\n\r"));
      embeddingStr.erase(embeddingStr.find_last_not_of(" \t\n\r") + 1);

      // Parse embedding (comma-separated floats)
      std::vector<float> embedding;
      std::istringstream embStream(embeddingStr);
      std::string val;
      while (std::getline(embStream, val, ',')) {
        val.erase(0, val.find_first_not_of(" \t\n\r"));
        val.erase(val.find_last_not_of(" \t\n\r") + 1);
        if (val.empty())
          continue;
        try {
          embedding.push_back(std::stof(val));
        } catch (...) {
          // Skip invalid value
          continue;
        }
      }

      if (!embedding.empty() && !subject.empty()) {
        // For same subject, we keep the first embedding (or could merge)
        if (faces.find(subject) == faces.end()) {
          faces[subject] = embedding;
        }
      }
    }

    if (isApiLoggingEnabled()) {
      PLOG_INFO << "[FaceDatabaseHelper] Loaded " << faces.size()
                << " face subject(s) from database";
    }

    return true;
  }

  // Load all faces with embeddings and image data from database for
  // recognition/search
  bool loadAllFacesWithDetails(
      std::map<std::string, std::vector<float>> &embeddings,
      std::map<std::string, std::string> &base64Images,
      std::map<std::string, std::vector<std::string>> &subjectImageIds,
      std::string &error) {
    if (!enabled_) {
      error = "Database connection not enabled";
      return false;
    }

    // Query all faces from database with image_id, subject, embedding,
    // base64_image
    std::string sql =
        "SELECT image_id, subject, embedding, base64_image FROM face_libraries";
    std::string result;

    if (!executeMySQLQuery(sql, result, error)) {
      if (isApiLoggingEnabled()) {
        PLOG_ERROR << "[FaceDatabaseHelper] Failed to load faces with details "
                      "from database: "
                   << error;
      }
      return false;
    }

    embeddings.clear();
    base64Images.clear();
    subjectImageIds.clear();

    if (result.empty()) {
      if (isApiLoggingEnabled()) {
        PLOG_INFO << "[FaceDatabaseHelper] No faces found in database";
      }
      return true;
    }

    // Parse result - tab-separated values:
    // image_id\tsubject\tembedding\tbase64_image
    std::istringstream iss(result);
    std::string line;

    while (std::getline(iss, line)) {
      if (line.empty())
        continue;

      // Split by tab
      std::vector<std::string> fields;
      std::istringstream lineStream(line);
      std::string field;
      while (std::getline(lineStream, field, '\t')) {
        fields.push_back(field);
      }

      if (fields.size() < 3)
        continue; // Need at least image_id, subject, embedding

      std::string imageId = fields[0];
      std::string subject = fields[1];
      std::string embeddingStr = fields[2];
      std::string base64Image = fields.size() > 3 ? fields[3] : "";

      // Trim whitespace
      imageId.erase(0, imageId.find_first_not_of(" \t\n\r"));
      imageId.erase(imageId.find_last_not_of(" \t\n\r") + 1);
      subject.erase(0, subject.find_first_not_of(" \t\n\r"));
      subject.erase(subject.find_last_not_of(" \t\n\r") + 1);
      embeddingStr.erase(0, embeddingStr.find_first_not_of(" \t\n\r"));
      embeddingStr.erase(embeddingStr.find_last_not_of(" \t\n\r") + 1);

      if (imageId.empty() || subject.empty() || embeddingStr.empty())
        continue;

      // Parse embedding (comma-separated floats)
      std::vector<float> embedding;
      std::istringstream embStream(embeddingStr);
      std::string val;
      while (std::getline(embStream, val, ',')) {
        val.erase(0, val.find_first_not_of(" \t\n\r"));
        val.erase(val.find_last_not_of(" \t\n\r") + 1);
        if (val.empty())
          continue;
        try {
          embedding.push_back(std::stof(val));
        } catch (...) {
          continue;
        }
      }

      if (!embedding.empty()) {
        // Store embedding (keep first one for each subject, or could average)
        if (embeddings.find(subject) == embeddings.end()) {
          embeddings[subject] = embedding;
        }

        // Store base64 image (keep first one for each subject)
        if (!base64Image.empty() &&
            base64Images.find(subject) == base64Images.end()) {
          base64Images[subject] = base64Image;
        }

        // Store image_id mapping
        subjectImageIds[subject].push_back(imageId);
      }
    }

    if (isApiLoggingEnabled()) {
      PLOG_INFO << "[FaceDatabaseHelper] Loaded " << embeddings.size()
                << " face subject(s) with details from database";
    }

    return true;
  }

  // Get faces from database with pagination and optional subject filter
  bool
  getFacesFromDatabase(int page, int size, const std::string &subjectFilter,
                       std::vector<std::pair<std::string, std::string>> &faces,
                       int &totalCount, std::string &error) {
    if (!enabled_) {
      error = "Database connection not enabled";
      return false;
    }

    // Build WHERE clause
    std::string whereClause = "";
    if (!subjectFilter.empty()) {
      std::string escapedSubject = escapeSqlString(subjectFilter);
      whereClause = "WHERE subject = '" + escapedSubject + "'";
    }

    // Get total count
    std::string countSql = "SELECT COUNT(*) FROM face_libraries " + whereClause;
    std::string countResult;

    if (!executeMySQLQuery(countSql, countResult, error)) {
      if (isApiLoggingEnabled()) {
        PLOG_ERROR
            << "[FaceDatabaseHelper] Failed to get face count from database: "
            << error;
      }
      return false;
    }

    // Parse count
    try {
      totalCount = std::stoi(countResult);
    } catch (...) {
      totalCount = 0;
    }

    // Get paginated faces
    int offset = page * size;
    std::string sql = "SELECT image_id, subject FROM face_libraries " +
                      whereClause + " ORDER BY created_at DESC LIMIT " +
                      std::to_string(size) + " OFFSET " +
                      std::to_string(offset);

    std::string result;
    if (!executeMySQLQuery(sql, result, error)) {
      if (isApiLoggingEnabled()) {
        PLOG_ERROR << "[FaceDatabaseHelper] Failed to get faces from database: "
                   << error;
      }
      return false;
    }

    faces.clear();
    if (result.empty()) {
      return true;
    }

    // Parse result - tab-separated values: image_id\tsubject
    std::istringstream iss(result);
    std::string line;

    while (std::getline(iss, line)) {
      if (line.empty())
        continue;

      // Split by tab
      size_t tabPos = line.find('\t');
      if (tabPos == std::string::npos)
        continue;

      std::string imageId = line.substr(0, tabPos);
      std::string subject = line.substr(tabPos + 1);

      if (!imageId.empty() && !subject.empty()) {
        faces.push_back({imageId, subject});
      }
    }

    if (isApiLoggingEnabled()) {
      PLOG_DEBUG << "[FaceDatabaseHelper] Retrieved " << faces.size()
                 << " face(s) from database (page=" << page << ", size=" << size
                 << ")";
    }

    return true;
  }

  // Delete face from database by image_id
  bool deleteFace(const std::string &imageId, std::string &error) {
    if (!enabled_) {
      error = "Database connection not enabled";
      return false;
    }

    std::string escapedImageId = escapeSqlString(imageId);
    std::string sql =
        "DELETE FROM face_libraries WHERE image_id = '" + escapedImageId + "'";

    if (isApiLoggingEnabled()) {
      PLOG_INFO << "[FaceDatabaseHelper] Deleting face from database: image_id="
                << imageId;
    }

    if (executeMySQLCommand(sql, error)) {
      if (isApiLoggingEnabled()) {
        PLOG_INFO << "[FaceDatabaseHelper] Successfully deleted face from "
                     "database: image_id="
                  << imageId;
      }
      return true;
    } else {
      if (isApiLoggingEnabled()) {
        PLOG_ERROR
            << "[FaceDatabaseHelper] Failed to delete face from database: "
            << error;
      }
      return false;
    }
  }

  // Find face by image_id in database
  bool findFaceByImageId(const std::string &imageId, std::string &subject,
                         std::string &error) {
    if (!enabled_) {
      error = "Database connection not enabled";
      return false;
    }

    std::string escapedImageId = escapeSqlString(imageId);
    std::string sql = "SELECT subject FROM face_libraries WHERE image_id = '" +
                      escapedImageId + "' LIMIT 1";

    std::string result;
    if (!executeMySQLQuery(sql, result, error)) {
      if (isApiLoggingEnabled()) {
        PLOG_ERROR << "[FaceDatabaseHelper] Failed to find face by image_id: "
                   << error;
      }
      return false;
    }

    if (result.empty()) {
      return false; // Not found
    }

    // Parse result - should be just the subject name
    // MySQL -N -B returns tab-separated values, but for single column it's just
    // the value
    std::istringstream iss(result);
    std::string line;
    if (std::getline(iss, line)) {
      // Trim whitespace
      line.erase(0, line.find_first_not_of(" \t\n\r"));
      line.erase(line.find_last_not_of(" \t\n\r") + 1);
      if (!line.empty()) {
        subject = line;
        return true;
      }
    }

    return false;
  }

  // Find faces by subject name in database
  bool findFacesBySubject(const std::string &subjectName,
                          std::vector<std::string> &imageIds,
                          std::string &error) {
    if (!enabled_) {
      error = "Database connection not enabled";
      return false;
    }

    std::string escapedSubject = escapeSqlString(subjectName);
    std::string sql = "SELECT image_id FROM face_libraries WHERE subject = '" +
                      escapedSubject + "'";

    std::string result;
    if (!executeMySQLQuery(sql, result, error)) {
      if (isApiLoggingEnabled()) {
        PLOG_ERROR << "[FaceDatabaseHelper] Failed to find faces by subject: "
                   << error;
      }
      return false;
    }

    imageIds.clear();
    if (result.empty()) {
      return false; // Not found
    }

    // Parse result - should be list of image_ids
    // MySQL -N -B returns tab-separated values, but for single column it's just
    // the value per line
    std::istringstream iss(result);
    std::string line;
    while (std::getline(iss, line)) {
      // Trim whitespace
      line.erase(0, line.find_first_not_of(" \t\n\r"));
      line.erase(line.find_last_not_of(" \t\n\r") + 1);
      if (!line.empty()) {
        imageIds.push_back(line);
      }
    }

    return !imageIds.empty();
  }

  // Delete all faces for a subject from database
  bool deleteSubject(const std::string &subject, std::string &error) {
    if (!enabled_) {
      error = "Database connection not enabled";
      return false;
    }

    std::string escapedSubject = escapeSqlString(subject);
    std::string sql =
        "DELETE FROM face_libraries WHERE subject = '" + escapedSubject + "'";

    if (isApiLoggingEnabled()) {
      PLOG_INFO
          << "[FaceDatabaseHelper] Deleting subject from database: subject="
          << subject;
    }

    if (executeMySQLCommand(sql, error)) {
      if (isApiLoggingEnabled()) {
        PLOG_INFO << "[FaceDatabaseHelper] Successfully deleted subject from "
                     "database: subject="
                  << subject;
      }
      return true;
    } else {
      if (isApiLoggingEnabled()) {
        PLOG_ERROR
            << "[FaceDatabaseHelper] Failed to delete subject from database: "
            << error;
      }
      return false;
    }
  }

  // Delete all faces from database
  bool deleteAllFaces(std::string &error) {
    if (!enabled_) {
      error = "Database connection not enabled";
      return false;
    }

    std::string sql = "DELETE FROM face_libraries";

    if (isApiLoggingEnabled()) {
      PLOG_INFO << "[FaceDatabaseHelper] Deleting all faces from database";
    }

    if (executeMySQLCommand(sql, error)) {
      if (isApiLoggingEnabled()) {
        PLOG_INFO << "[FaceDatabaseHelper] Successfully deleted all faces from "
                     "database";
      }
      return true;
    } else {
      if (isApiLoggingEnabled()) {
        PLOG_ERROR
            << "[FaceDatabaseHelper] Failed to delete all faces from database: "
            << error;
      }
      return false;
    }
  }

  // Update subject name in database (rename subject)
  bool updateSubjectName(const std::string &oldSubjectName,
                         const std::string &newSubjectName,
                         bool mergeEmbeddings, std::string &error) {
    if (!enabled_) {
      error = "Database connection not enabled";
      return false;
    }

    std::string escapedOldSubject = escapeSqlString(oldSubjectName);
    std::string escapedNewSubject = escapeSqlString(newSubjectName);

    if (mergeEmbeddings) {
      // Check if new subject exists
      std::vector<std::string> existingImageIds;
      if (findFacesBySubject(newSubjectName, existingImageIds, error)) {
        // New subject exists - need to merge embeddings
        // For merge, we'll update all old subject records to new subject name
        // The embeddings will be averaged at application level if needed
        std::string sql = "UPDATE face_libraries SET subject = '" +
                          escapedNewSubject + "' WHERE subject = '" +
                          escapedOldSubject + "'";

        if (isApiLoggingEnabled()) {
          PLOG_INFO << "[FaceDatabaseHelper] Merging subjects in database: "
                    << oldSubjectName << " -> " << newSubjectName;
        }

        if (executeMySQLCommand(sql, error)) {
          if (isApiLoggingEnabled()) {
            PLOG_INFO << "[FaceDatabaseHelper] Successfully merged subjects in "
                         "database";
          }
          return true;
        } else {
          if (isApiLoggingEnabled()) {
            PLOG_ERROR
                << "[FaceDatabaseHelper] Failed to merge subjects in database: "
                << error;
          }
          return false;
        }
      }
    }

    // Simple rename (new subject doesn't exist or no merge needed)
    std::string sql = "UPDATE face_libraries SET subject = '" +
                      escapedNewSubject + "' WHERE subject = '" +
                      escapedOldSubject + "'";

    if (isApiLoggingEnabled()) {
      PLOG_INFO << "[FaceDatabaseHelper] Renaming subject in database: "
                << oldSubjectName << " -> " << newSubjectName;
    }

    if (executeMySQLCommand(sql, error)) {
      if (isApiLoggingEnabled()) {
        PLOG_INFO
            << "[FaceDatabaseHelper] Successfully renamed subject in database";
      }
      return true;
    } else {
      if (isApiLoggingEnabled()) {
        PLOG_ERROR
            << "[FaceDatabaseHelper] Failed to rename subject in database: "
            << error;
      }
      return false;
    }
  }

  // Log request to face_log table
  bool logRequest(const std::string &requestType, const std::string &clientIp,
                  const std::string &requestBody,
                  const std::string &responseBody, int responseCode,
                  const std::string &notes, std::string &error) {
    if (!enabled_) {
      // Logging is optional, don't return error
      return false;
    }

    // TODO: Implement actual database log operation

    if (isApiLoggingEnabled()) {
      PLOG_DEBUG << "[FaceDatabaseHelper] Would log request to database: "
                 << "type=" << requestType;
    }

    return false;
  }
};

static std::unique_ptr<FaceDatabase> g_database;
static std::unique_ptr<FaceDatabaseHelper> g_db_helper;

static FaceDatabaseHelper &get_db_helper() {
  if (!g_db_helper) {
    g_db_helper = std::make_unique<FaceDatabaseHelper>();
    if (isApiLoggingEnabled()) {
      if (g_db_helper->isEnabled()) {
        PLOG_INFO << "[RecognitionHandler] FaceDatabaseHelper created - "
                     "Database connection enabled";
      } else {
        PLOG_INFO << "[RecognitionHandler] FaceDatabaseHelper created - Using "
                     "file-based storage";
      }
    }
  } else {
    // Reload config in case it was changed
    g_db_helper->reloadConfig();
  }
  return *g_db_helper;
}

static FaceDatabase &get_database() {
  if (!g_database) {
    if (isApiLoggingEnabled()) {
      PLOG_INFO << "[RecognitionHandler] Creating FaceDatabase instance...";
    }
    // Use empty string to trigger automatic path resolution
    g_database = std::make_unique<FaceDatabase>("");
    if (isApiLoggingEnabled()) {
      PLOG_INFO << "[RecognitionHandler] FaceDatabase instance created";
      PLOG_INFO << "[RecognitionHandler] Database file: "
                << g_database->get_database_path();
      PLOG_INFO << "[RecognitionHandler] Detector model: "
                << (g_database->get_detector_model_path().empty()
                        ? "NOT FOUND"
                        : g_database->get_detector_model_path());
      PLOG_INFO << "[RecognitionHandler] Recognition model: "
                << (g_database->get_onnx_model_path().empty()
                        ? "NOT FOUND"
                        : g_database->get_onnx_model_path());
    }

    // Populate image_id_to_subject_ and face_subjects_storage_ from loaded
    // database
    // Check if database connection is enabled
    FaceDatabaseHelper &dbHelper = get_db_helper();
    if (dbHelper.isEnabled()) {
      // Try to load from database first
      std::map<std::string, std::vector<float>> dbFaces;
      std::string dbError;
      if (dbHelper.loadAllFaces(dbFaces, dbError)) {
        if (isApiLoggingEnabled()) {
          PLOG_INFO << "[RecognitionHandler] Loaded " << dbFaces.size()
                    << " faces from database";
        }
        RecognitionHandler::populateStorageFromDatabase(dbFaces);
      } else {
        // Database load failed, fallback to file
        if (isApiLoggingEnabled()) {
          PLOG_WARNING << "[RecognitionHandler] Database load failed: "
                       << dbError << ", falling back to file";
        }
        const auto &db_map = g_database->get_database();
        RecognitionHandler::populateStorageFromDatabase(db_map);
      }
    } else {
      // Use file-based storage
      const auto &db_map = g_database->get_database();
      RecognitionHandler::populateStorageFromDatabase(db_map);
    }
  }
  return *g_database;
}

void RecognitionHandler::populateStorageFromDatabase(
    const std::map<std::string, std::vector<float>> &db_map) {
  std::lock_guard<std::mutex> lock(storage_mutex_);
  for (const auto &[subject_name, embedding] : db_map) {
    // Generate a stable image_id based on subject name
    std::string imageId = generateImageIdForSubject(subject_name);
    image_id_to_subject_[imageId] = subject_name;
    face_subjects_storage_[subject_name].push_back(imageId);
  }
  if (isApiLoggingEnabled()) {
    PLOG_INFO << "[RecognitionHandler] Loaded " << db_map.size()
              << " subjects into memory";
  }
}

bool RecognitionHandler::isBase64(const std::string &str) const {
  if (str.empty())
    return false;

  // Base64 characters: A-Z, a-z, 0-9, +, /, and = for padding
  for (char c : str) {
    if (!std::isalnum(c) && c != '+' && c != '/' && c != '=' && c != '\n' &&
        c != '\r' && c != ' ') {
      return false;
    }
  }

  // Check for base64 padding pattern
  size_t paddingCount = 0;
  for (size_t i = str.length() - 1; i > 0 && str[i] == '='; --i) {
    paddingCount++;
  }

  return paddingCount <= 2;
}

bool RecognitionHandler::decodeBase64(
    const std::string &base64Str, std::vector<unsigned char> &output) const {
  // Remove whitespace
  std::string cleanBase64;
  for (char c : base64Str) {
    if (c != ' ' && c != '\n' && c != '\r' && c != '\t') {
      cleanBase64 += c;
    }
  }

  if (cleanBase64.empty()) {
    return false;
  }

  // Base64 decoding
  const std::string base64_chars =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

  int val = 0, valb = -8;
  for (unsigned char c : cleanBase64) {
    if (c == '=')
      break;

    size_t pos = base64_chars.find(c);
    if (pos == std::string::npos) {
      return false; // Invalid character
    }

    val = (val << 6) + pos;
    valb += 6;

    if (valb >= 0) {
      output.push_back((val >> valb) & 0xFF);
      valb -= 8;
    }
  }

  return true;
}

std::string
RecognitionHandler::encodeBase64(const std::vector<unsigned char> &data) const {
  const std::string base64_chars =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string encoded;
  int val = 0, valb = -6;

  for (unsigned char c : data) {
    val = (val << 8) + c;
    valb += 8;
    while (valb >= 0) {
      encoded.push_back(base64_chars[(val >> valb) & 0x3F]);
      valb -= 6;
    }
  }

  if (valb > -6) {
    encoded.push_back(base64_chars[((val << 8) >> (valb + 8)) & 0x3F]);
  }

  while (encoded.size() % 4) {
    encoded.push_back('=');
  }

  return encoded;
}

bool RecognitionHandler::validateImageFormatAndSize(
    const std::vector<unsigned char> &imageData, std::string &error) const {
  // Check size: max 5MB
  const size_t MAX_SIZE = 5 * 1024 * 1024; // 5MB
  if (imageData.size() > MAX_SIZE) {
    error = "Image file size exceeds maximum allowed size of 5MB. File size: " +
            std::to_string(imageData.size() / 1024 / 1024) + "MB";
    return false;
  }

  if (imageData.empty()) {
    error = "Image data is empty";
    return false;
  }

  // Log first few bytes for debugging
  if (isApiLoggingEnabled() && imageData.size() >= 4) {
    std::stringstream ss;
    ss << std::hex << std::setfill('0');
    for (size_t i = 0; i < std::min(size_t(16), imageData.size()); i++) {
      ss << std::setw(2) << static_cast<int>(imageData[i]) << " ";
    }
    PLOG_DEBUG << "[RecognitionHandler] First bytes of image data: "
               << ss.str();
  }

  // Check image format by magic bytes
  // Supported formats: jpeg, jpg, ico, png, bmp, gif, tif, tiff, webp
  bool isValidFormat = false;
  std::string detectedFormat;

  if (imageData.size() >= 2) {
    // JPEG: FF D8
    if (imageData[0] == 0xFF && imageData[1] == 0xD8) {
      isValidFormat = true;
      detectedFormat = "JPEG";
    }
    // PNG: 89 50 4E 47
    else if (imageData.size() >= 4 && imageData[0] == 0x89 &&
             imageData[1] == 0x50 && imageData[2] == 0x4E &&
             imageData[3] == 0x47) {
      isValidFormat = true;
      detectedFormat = "PNG";
    }
    // GIF: 47 49 46 38 (GIF8) or 47 49 46 39 (GIF9)
    else if (imageData.size() >= 4 && imageData[0] == 0x47 &&
             imageData[1] == 0x49 && imageData[2] == 0x46 &&
             (imageData[3] == 0x38 || imageData[3] == 0x39)) {
      isValidFormat = true;
      detectedFormat = "GIF";
    }
    // BMP: 42 4D
    else if (imageData[0] == 0x42 && imageData[1] == 0x4D) {
      isValidFormat = true;
      detectedFormat = "BMP";
    }
    // ICO: 00 00 01 00 or 00 00 02 00
    else if (imageData.size() >= 4 && imageData[0] == 0x00 &&
             imageData[1] == 0x00 &&
             (imageData[2] == 0x01 || imageData[2] == 0x02) &&
             imageData[3] == 0x00) {
      isValidFormat = true;
      detectedFormat = "ICO";
    }
    // TIFF: 49 49 2A 00 (little-endian) or 4D 4D 00 2A (big-endian)
    else if (imageData.size() >= 4 &&
             ((imageData[0] == 0x49 && imageData[1] == 0x49 &&
               imageData[2] == 0x2A && imageData[3] == 0x00) ||
              (imageData[0] == 0x4D && imageData[1] == 0x4D &&
               imageData[2] == 0x00 && imageData[3] == 0x2A))) {
      isValidFormat = true;
      detectedFormat = "TIFF";
    }
    // WebP: RIFF ... WEBP
    else if (imageData.size() >= 12 && imageData[0] == 0x52 &&
             imageData[1] == 0x49 && imageData[2] == 0x46 &&
             imageData[3] == 0x46 && imageData[8] == 0x57 &&
             imageData[9] == 0x45 && imageData[10] == 0x42 &&
             imageData[11] == 0x50) {
      isValidFormat = true;
      detectedFormat = "WebP";
    }
  }

  if (!isValidFormat) {
    // Log first bytes for debugging
    std::stringstream hexBytes;
    hexBytes << std::hex << std::setfill('0');
    size_t bytesToShow = std::min(size_t(8), imageData.size());
    for (size_t i = 0; i < bytesToShow; i++) {
      hexBytes << std::setw(2) << static_cast<int>(imageData[i]);
      if (i < bytesToShow - 1)
        hexBytes << " ";
    }

    error = "Unsupported image format. Supported formats: JPEG, JPG, PNG, BMP, "
            "GIF, ICO, TIFF, WebP. "
            "Detected format: Unknown (file may be corrupted or not an image). "
            "First bytes (hex): " +
            hexBytes.str();

    if (isApiLoggingEnabled()) {
      PLOG_WARNING << "[RecognitionHandler] Image format validation failed. "
                      "First bytes: "
                   << hexBytes.str();
    }
    return false;
  }

  if (isApiLoggingEnabled()) {
    PLOG_DEBUG << "[RecognitionHandler] Image format validated: "
               << detectedFormat << ", size: " << imageData.size() << " bytes";
  }

  return true;
}

bool RecognitionHandler::extractImageData(const HttpRequestPtr &req,
                                          std::vector<unsigned char> &imageData,
                                          std::string &error) const {
  std::string contentType = req->getHeader("Content-Type");
  bool isMultipart =
      contentType.find("multipart/form-data") != std::string::npos;

  if (!isMultipart) {
    error = "Content-Type must be multipart/form-data";
    return false;
  }

  // Get boundary from Content-Type header
  std::string boundary;
  size_t boundaryPos = contentType.find("boundary=");
  if (boundaryPos != std::string::npos) {
    boundaryPos += 9; // length of "boundary="
    size_t endPos = contentType.find_first_of("; \r\n", boundaryPos);
    if (endPos != std::string::npos) {
      boundary = contentType.substr(boundaryPos, endPos - boundaryPos);
    } else {
      boundary = contentType.substr(boundaryPos);
    }
    // Remove quotes if present
    if (!boundary.empty() && boundary.front() == '"' &&
        boundary.back() == '"') {
      boundary = boundary.substr(1, boundary.length() - 2);
    }
  }

  if (boundary.empty()) {
    error = "Could not find boundary in Content-Type header";
    return false;
  }

  // Get request body as binary data
  auto body = req->getBody();
  if (body.empty()) {
    error = "Request body is empty";
    return false;
  }

  // Convert body to string for parsing headers, but keep binary for file
  // content
  std::string bodyStr(reinterpret_cast<const char *>(body.data()), body.size());
  std::string boundaryMarker = "--" + boundary;

  // Find the part with name="file" - try different field name variations
  size_t partStart = bodyStr.find(boundaryMarker);
  if (partStart == std::string::npos) {
    error = "Could not find multipart boundary";
    return false;
  }

  // Find Content-Disposition header first (more reliable than searching for
  // field name)
  size_t contentDispositionPos =
      bodyStr.find("Content-Disposition:", partStart);
  if (contentDispositionPos == std::string::npos ||
      contentDispositionPos > partStart + 1024) {
    error = "Could not find Content-Disposition header in multipart data";
    return false;
  }

  // Search for file field name in Content-Disposition - try "file", "image",
  // "photo"
  size_t fileFieldPos = std::string::npos;
  std::vector<std::string> fieldNames = {
      "name=\"file\"",  "name='file'",  "name=file",
      "name=\"image\"", "name='image'", "name=image",
      "name=\"photo\"", "name='photo'", "name=photo"};

  for (const auto &fieldName : fieldNames) {
    fileFieldPos = bodyStr.find(fieldName, contentDispositionPos);
    if (fileFieldPos != std::string::npos &&
        fileFieldPos < contentDispositionPos + 512) {
      break;
    }
  }

  if (fileFieldPos == std::string::npos) {
    error = "Could not find file field in multipart form data. Expected field "
            "name: 'file', 'image', or 'photo'";
    return false;
  }

  // Find content start (after headers and blank line) - search from
  // Content-Disposition position
  size_t contentStart = bodyStr.find("\r\n\r\n", contentDispositionPos);
  if (contentStart == std::string::npos) {
    contentStart = bodyStr.find("\n\n", contentDispositionPos);
  }
  if (contentStart == std::string::npos) {
    error = "Could not find content start in multipart data";
    return false;
  }

  // Skip the blank line
  contentStart += 2; // Skip \r\n or \n
  if (contentStart < bodyStr.length() &&
      (bodyStr[contentStart] == '\r' || bodyStr[contentStart] == '\n')) {
    contentStart++;
  }

  // Skip any additional whitespace/newlines
  while (contentStart < bodyStr.length() &&
         (bodyStr[contentStart] == '\r' || bodyStr[contentStart] == '\n' ||
          bodyStr[contentStart] == ' ' || bodyStr[contentStart] == '\t')) {
    contentStart++;
  }

  if (contentStart >= bodyStr.length()) {
    error = "Content start position is beyond body length";
    return false;
  }

  // Find content end (before next boundary)
  size_t nextBoundary = bodyStr.find(boundaryMarker, contentStart);
  size_t contentEnd =
      (nextBoundary != std::string::npos) ? nextBoundary : bodyStr.length();

  // Remove trailing \r\n before boundary
  while (contentEnd > contentStart &&
         (bodyStr[contentEnd - 1] == '\r' || bodyStr[contentEnd - 1] == '\n')) {
    contentEnd--;
  }

  if (contentEnd <= contentStart) {
    error = "File field has no content";
    return false;
  }

  // Extract binary data directly from body (not from string to preserve binary
  // data) Use the same offsets for binary extraction
  size_t binaryStart = contentStart;
  size_t binaryEnd = contentEnd;

  if (isApiLoggingEnabled()) {
    PLOG_DEBUG << "[RecognitionHandler] Extracting binary data from position "
               << binaryStart << " to " << binaryEnd
               << " (size: " << (binaryEnd - binaryStart) << " bytes)";
  }

  // Copy binary data directly
  imageData.assign(body.begin() + binaryStart, body.begin() + binaryEnd);

  if (isApiLoggingEnabled()) {
    PLOG_DEBUG
        << "[RecognitionHandler] Extracted image data from multipart, size: "
        << imageData.size() << " bytes";
    if (imageData.size() >= 4) {
      std::stringstream ss;
      ss << std::hex << std::setfill('0');
      for (size_t i = 0; i < std::min(size_t(8), imageData.size()); i++) {
        ss << std::setw(2) << static_cast<int>(imageData[i]) << " ";
      }
      PLOG_DEBUG << "[RecognitionHandler] First bytes of extracted data (hex): "
                 << ss.str();
    }
  }

  // Check if the extracted data looks like base64 (text-based)
  // Base64 strings are typically longer and contain only base64 characters
  // Check if data is text-based (base64) or binary (image data)
  bool mightBeBase64 = false;

  // Check if data contains only printable ASCII characters (base64)
  bool isTextData = true;
  size_t checkSize =
      std::min(imageData.size(), size_t(1000)); // Check first 1000 bytes
  for (size_t i = 0; i < checkSize; i++) {
    unsigned char c = imageData[i];
    // Base64 characters: A-Z, a-z, 0-9, +, /, =, and whitespace
    if (c != '\r' && c != '\n' && c != ' ' && c != '\t' &&
        !((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
          (c >= '0' && c <= '9') || c == '+' || c == '/' || c == '=')) {
      isTextData = false;
      break;
    }
  }

  if (isApiLoggingEnabled()) {
    PLOG_DEBUG << "[RecognitionHandler] Data appears to be "
               << (isTextData ? "text/base64" : "binary");
  }

  // If it looks like text data and is reasonably long, try base64 decode
  if (isTextData && imageData.size() > 100) {
    std::string base64Str(reinterpret_cast<const char *>(imageData.data()),
                          imageData.size());
    // Remove whitespace
    std::string cleanBase64;
    for (char c : base64Str) {
      if (c != ' ' && c != '\n' && c != '\r' && c != '\t') {
        cleanBase64 += c;
      }
    }

    // Try to decode as base64
    std::vector<unsigned char> decoded;
    if (decodeBase64(cleanBase64, decoded) && !decoded.empty()) {
      // Verify decoded data looks like image (starts with image magic bytes)
      if (decoded.size() > 4) {
        // Check for common image formats: JPEG (FF D8), PNG (89 50 4E 47), etc.
        if ((decoded[0] == 0xFF && decoded[1] == 0xD8) || // JPEG
            (decoded[0] == 0x89 && decoded[1] == 0x50 && decoded[2] == 0x4E &&
             decoded[3] == 0x47) || // PNG
            (decoded[0] == 0x47 && decoded[1] == 0x49 &&
             decoded[2] == 0x46) ||                       // GIF
            (decoded[0] == 0x42 && decoded[1] == 0x4D)) { // BMP
          imageData = decoded;
          if (isApiLoggingEnabled()) {
            PLOG_DEBUG << "[RecognitionHandler] Successfully decoded base64 "
                          "image data, size: "
                       << imageData.size() << " bytes";
          }
        } else {
          // Decoded but doesn't look like image, try anyway (might be valid)
          imageData = decoded;
          if (isApiLoggingEnabled()) {
            PLOG_DEBUG << "[RecognitionHandler] Decoded base64 data (unknown "
                          "format), size: "
                       << imageData.size() << " bytes";
          }
        }
      }
    }
    // If base64 decode fails, keep original binary data (might be binary image)
  }

  if (imageData.empty()) {
    error = "Image data is empty after extraction";
    return false;
  }

  return true;
}

void RecognitionHandler::parseQueryParameters(const HttpRequestPtr &req,
                                              int &limit, int &predictionCount,
                                              double &detProbThreshold,
                                              double &similarityThreshold,
                                              std::string &facePlugins,
                                              std::string &status,
                                              bool &detectFaces) const {
  // Parse limit (default: 0 = no limit)
  std::string limitStr = req->getParameter("limit");
  if (!limitStr.empty()) {
    try {
      limit = std::stoi(limitStr);
    } catch (...) {
      limit = 0;
    }
  } else {
    limit = 0;
  }

  // Parse prediction_count
  std::string predictionCountStr = req->getParameter("prediction_count");
  if (!predictionCountStr.empty()) {
    try {
      predictionCount = std::stoi(predictionCountStr);
    } catch (...) {
      predictionCount = 1;
    }
  } else {
    predictionCount = 1;
  }

  // Parse det_prob_threshold
  std::string detProbThresholdStr = req->getParameter("det_prob_threshold");
  if (!detProbThresholdStr.empty()) {
    try {
      detProbThreshold = std::stod(detProbThresholdStr);
      // Validate threshold range (0.0 to 1.0)
      if (detProbThreshold < 0.0)
        detProbThreshold = 0.0;
      if (detProbThreshold > 1.0)
        detProbThreshold = 1.0;
    } catch (...) {
      detProbThreshold = 0.5; // Default on parse error
    }
  } else {
    detProbThreshold = 0.5; // Default when parameter is missing or empty
  }

  // Parse similarity threshold (optional)
  // If provided, recognize endpoint will filter predicted subjects to only those
  // with similarity >= threshold (0.0 - 1.0). If not provided, returns top-N
  // similarities without filtering (backward compatible).
  similarityThreshold = -1.0;
  std::string similarityThresholdStr = req->getParameter("threshold");
  if (!similarityThresholdStr.empty()) {
    try {
      similarityThreshold = std::stod(similarityThresholdStr);
      if (similarityThreshold < 0.0 || similarityThreshold > 1.0) {
        similarityThreshold = -1.0;
      }
    } catch (...) {
      similarityThreshold = -1.0;
    }
  }

  // Parse face_plugins
  facePlugins = req->getParameter("face_plugins");

  // Parse status
  status = req->getParameter("status");

  // Parse detect_faces
  std::string detectFacesStr = req->getParameter("detect_faces");
  if (!detectFacesStr.empty()) {
    std::transform(detectFacesStr.begin(), detectFacesStr.end(),
                   detectFacesStr.begin(), ::tolower);
    detectFaces = (detectFacesStr == "true" || detectFacesStr == "1" ||
                   detectFacesStr == "yes");
  } else {
    detectFaces = true; // Default to true
  }
}

Json::Value RecognitionHandler::processFaceRecognition(
    const std::vector<unsigned char> &imageData, int limit, int predictionCount,
    double detProbThreshold, double similarityThreshold,
    const std::string &facePlugins,
    bool detectFaces) const {
  Json::Value result(Json::arrayValue);

  try {
    if (!detectFaces) {
      return result;
    }

    // Get database instance (for model paths and embeddings)
    FaceDatabase &db = get_database();

    std::string detector_path = db.get_detector_model_path();
    std::string onnx_path = db.get_onnx_model_path();

    // Load embeddings from database or file
    std::map<std::string, std::vector<float>> database;
    FaceDatabaseHelper &dbHelper = get_db_helper();
    if (dbHelper.isEnabled()) {
      std::string dbError;
      if (!dbHelper.loadAllFaces(database, dbError)) {
        if (isApiLoggingEnabled()) {
          PLOG_WARNING
              << "[RecognitionHandler] Failed to load faces from database: "
              << dbError << ", falling back to file";
        }
        database = db.get_database();
      }
    } else {
      database = db.get_database();
    }

    if (detector_path.empty()) {
      if (isApiLoggingEnabled()) {
        PLOG_WARNING << "[RecognitionHandler] Face detector model not found.";
      }
      return result;
    }

    // AIRuntimeFacade: decode + infer (optional cache)
    core::AIRuntimeRequest req;
    req.payload = imageData;
    req.codec = "";
    req.model_key = "face";
    req.options["detector_path"] = detector_path;
    req.options["recognizer_path"] = onnx_path;
    req.options["det_prob_threshold"] = detProbThreshold;
    req.options["limit"] = limit;
    req.options["extract_embedding"] = !onnx_path.empty();

    core::AIRuntimeResponse response = getFaceRuntimeFacade().request(req);

    if (!response.success) {
      if (isApiLoggingEnabled()) {
        PLOG_WARNING << "[RecognitionHandler] AIRuntimeFacade request failed: "
                     << response.error;
      }
      return result;
    }

    if (!response.result.isMember("faces") || !response.result["faces"].isArray()) {
      return result;
    }

    const Json::Value &faces_array = response.result["faces"];
    uint64_t detector_time = response.inference_ms;

    for (Json::ArrayIndex i = 0; i < faces_array.size(); i++) {
      const Json::Value &face_obj = faces_array[i];
      Json::Value faceResult;
      faceResult["box"] = face_obj["box"];
      faceResult["landmarks"] = face_obj["landmarks"];

      std::vector<float> face_embedding;
      if (face_obj.isMember("embedding") && face_obj["embedding"].isArray()) {
        for (const auto &v : face_obj["embedding"])
          face_embedding.push_back(static_cast<float>(v.asDouble()));
      }

      Json::Value subjects(Json::arrayValue);
      if (!face_embedding.empty() && !onnx_path.empty() && database.size() > 0) {
        std::vector<std::pair<std::string, float>> similarities;
        for (const auto &[name, db_embedding] : database) {
          float sim = cosine_similarity(face_embedding, db_embedding);
          similarities.push_back({name, sim});
        }
        std::sort(similarities.begin(), similarities.end(),
                  [](const std::pair<std::string, float> &a,
                     const std::pair<std::string, float> &b) {
                    return a.second > b.second;
                  });

        if (similarityThreshold >= 0.0) {
          for (const auto &pair : similarities) {
            if (pair.second < static_cast<float>(similarityThreshold))
              continue;
            Json::Value subject;
            subject["subject"] = pair.first;
            subject["similarity"] = static_cast<double>(pair.second);
            subjects.append(subject);
            if (subjects.size() >=
                static_cast<Json::ArrayIndex>(std::max(0, predictionCount)))
              break;
          }
        } else {
          int top_n =
              std::min(predictionCount, static_cast<int>(similarities.size()));
          for (int j = 0; j < top_n; j++) {
            Json::Value subject;
            subject["subject"] = similarities[j].first;
            subject["similarity"] = static_cast<double>(similarities[j].second);
            subjects.append(subject);
          }
        }
      }
      faceResult["subjects"] = subjects;

      Json::Value executionTime;
      executionTime["decode"] = static_cast<double>(response.decode_ms);
      executionTime["detector"] = static_cast<double>(detector_time);
      executionTime["calculator"] = 0.0;
      executionTime["age"] = 0.0;
      executionTime["gender"] = 0.0;
      executionTime["mask"] = 0.0;
      faceResult["execution_time"] = executionTime;

      result.append(faceResult);
    }

    if (isApiLoggingEnabled()) {
      PLOG_INFO << "[RecognitionHandler] Returning " << result.size()
                << " face result(s)";
    }

  } catch (const std::exception &e) {
    if (isApiLoggingEnabled()) {
      PLOG_ERROR << "[RecognitionHandler] Error processing face recognition: "
                 << e.what();
    }
  } catch (...) {
    if (isApiLoggingEnabled()) {
      PLOG_ERROR << "[RecognitionHandler] Unknown error in face recognition";
    }
  }

  return result;
}

void RecognitionHandler::recognizeFaces(
    const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback) {
  // Set handler start time for accurate metrics
  MetricsInterceptor::setHandlerStartTime(req);

  auto start_time = std::chrono::steady_clock::now();

  if (isApiLoggingEnabled()) {
    PLOG_INFO << "[API] POST /v1/recognition/recognize - Recognize faces";
    PLOG_DEBUG << "[API] Request from: " << req->getPeerAddr().toIpPort();
  }

  try {
    // Extract image data
    std::vector<unsigned char> imageData;
    std::string imageError;
    if (!extractImageData(req, imageData, imageError)) {
      if (isApiLoggingEnabled()) {
        PLOG_WARNING << "[API] POST /v1/recognition/recognize - " << imageError;
      }
      callback(createErrorResponse(400, "Invalid request", imageError));
      return;
    }

    // Parse query parameters
    int limit = 0;
    int predictionCount = 1;
    double detProbThreshold = 0.5;
    double similarityThreshold = -1.0;
    std::string facePlugins;
    std::string status;
    bool detectFaces = true;

    parseQueryParameters(req, limit, predictionCount, detProbThreshold,
                         similarityThreshold, facePlugins, status, detectFaces);

    if (isApiLoggingEnabled()) {
      PLOG_DEBUG << "[API] Recognition parameters - limit: " << limit
                 << ", prediction_count: " << predictionCount
                 << ", det_prob_threshold: " << detProbThreshold
                 << ", threshold: "
                 << (similarityThreshold >= 0.0
                         ? std::to_string(similarityThreshold)
                         : std::string("disabled"))
                 << ", detect_faces: " << (detectFaces ? "true" : "false");
    }

    // Process face recognition
    Json::Value recognitionResult =
        processFaceRecognition(imageData, limit, predictionCount,
                               detProbThreshold, similarityThreshold,
                               facePlugins, detectFaces);

    // Build response
    Json::Value response;
    response["result"] = recognitionResult;

    auto resp = HttpResponse::newHttpJsonResponse(response);
    resp->setStatusCode(k200OK);

    // Add CORS headers
    resp->addHeader("Access-Control-Allow-Origin", "*");
    resp->addHeader("Access-Control-Allow-Methods", "POST, OPTIONS");
    resp->addHeader("Access-Control-Allow-Headers", "Content-Type");

    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time);

    if (isApiLoggingEnabled()) {
      PLOG_INFO << "[API] POST /v1/recognition/recognize - Success - "
                << duration.count() << "ms";
    }

    // Record metrics and call callback
    MetricsInterceptor::callWithMetrics(req, resp, std::move(callback));

  } catch (const std::exception &e) {
    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time);
    if (isApiLoggingEnabled()) {
      PLOG_ERROR << "[API] POST /v1/recognition/recognize - Exception: "
                 << e.what() << " - " << duration.count() << "ms";
    }
    callback(createErrorResponse(500, "Internal server error", e.what()));
  } catch (...) {
    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time);
    if (isApiLoggingEnabled()) {
      PLOG_ERROR
          << "[API] POST /v1/recognition/recognize - Unknown exception - "
          << duration.count() << "ms";
    }
    callback(createErrorResponse(500, "Internal server error",
                                 "Unknown error occurred"));
  }
}

void RecognitionHandler::handleOptions(
    const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback) {
  // Set handler start time for accurate metrics
  MetricsInterceptor::setHandlerStartTime(req);

  auto resp = HttpResponse::newHttpResponse();
  resp->setStatusCode(k200OK);
  resp->addHeader("Access-Control-Allow-Origin", "*");
  resp->addHeader("Access-Control-Allow-Methods", "POST, OPTIONS");
  resp->addHeader("Access-Control-Allow-Headers", "Content-Type");
  resp->addHeader("Access-Control-Max-Age", "3600");

  // Record metrics and call callback
  MetricsInterceptor::callWithMetrics(req, resp, std::move(callback));
}

std::string RecognitionHandler::generateImageId() const {
  // Generate UUID-like ID: xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx
  std::random_device rd;
  std::mt19937 gen(rd());
  std::uniform_int_distribution<> dis(0, 15);
  std::uniform_int_distribution<> dis2(8, 11);

  std::stringstream ss;
  int i;
  ss << std::hex;
  for (i = 0; i < 8; i++) {
    ss << dis(gen);
  }
  ss << "-";
  for (i = 0; i < 4; i++) {
    ss << dis(gen);
  }
  ss << "-";
  ss << dis2(gen);
  for (i = 0; i < 3; i++) {
    ss << dis(gen);
  }
  ss << "-";
  for (i = 0; i < 4; i++) {
    ss << dis(gen);
  }
  ss << "-";
  for (i = 0; i < 12; i++) {
    ss << dis(gen);
  }
  return ss.str();
}

std::string
RecognitionHandler::generateImageIdForSubject(const std::string &subject_name) {
  // Generate a deterministic UUID-like ID based on subject name
  // This ensures the same subject always gets the same image_id across restarts
  std::hash<std::string> hasher;
  size_t hash_value = hasher(subject_name);

  std::stringstream ss;
  ss << std::hex << std::setfill('0');

  // Format: xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx
  ss << std::setw(8) << ((hash_value >> 32) & 0xFFFFFFFF);
  ss << "-";
  ss << std::setw(4) << ((hash_value >> 16) & 0xFFFF);
  ss << "-";
  ss << std::setw(4) << (hash_value & 0xFFFF);
  ss << "-";
  // Use additional hash for remaining parts
  size_t hash2 = hasher(subject_name + "_salt");
  ss << std::setw(4) << ((hash2 >> 48) & 0xFFFF);
  ss << "-";
  ss << std::setw(12) << (hash2 & 0xFFFFFFFFFFFF);

  return ss.str();
}

bool RecognitionHandler::extractImageFromJson(
    const HttpRequestPtr &req, std::vector<unsigned char> &imageData,
    std::string &error) const {
  // Parse JSON body
  auto json = req->getJsonObject();
  if (!json) {
    error = "Request body must be valid JSON";
    return false;
  }

  // Check if file field exists
  if (!json->isMember("file") || !(*json)["file"].isString()) {
    error = "Missing required field: file (base64 encoded image)";
    return false;
  }

  std::string fileBase64 = (*json)["file"].asString();
  if (fileBase64.empty()) {
    error = "File field is empty";
    return false;
  }

  // Remove data URL prefix if present (e.g., "data:image/jpeg;base64,")
  size_t commaPos = fileBase64.find(',');
  if (commaPos != std::string::npos) {
    std::string prefix = fileBase64.substr(0, commaPos);
    if (prefix.find("base64") != std::string::npos) {
      fileBase64 = fileBase64.substr(commaPos + 1);
      if (isApiLoggingEnabled()) {
        PLOG_DEBUG << "[RecognitionHandler] Removed data URL prefix from "
                      "base64 string";
      }
    }
  }

  // Decode base64
  if (!decodeBase64(fileBase64, imageData)) {
    error = "Failed to decode base64 image data";
    return false;
  }

  if (imageData.empty()) {
    error = "Image data is empty after decoding";
    return false;
  }

  return true;
}

bool RecognitionHandler::registerSubject(
    const std::string &subjectName, const std::vector<unsigned char> &imageData,
    double detProbThreshold, std::string &imageId, std::string &error) const {
  try {
    // Validate image format and size (already validated in
    // extractImageFromRequest, but double-check here)
    if (!validateImageFormatAndSize(imageData, error)) {
      return false;
    }

    // Validate image can be decoded
    cv::Mat image = cv::imdecode(imageData, cv::IMREAD_COLOR);
    if (image.empty()) {
      error = "Invalid image format or corrupted image data. Please ensure the "
              "image is a valid JPEG, PNG, BMP, GIF, ICO, TIFF, or WebP file";
      return false;
    }

    imageId = generateImageId();

    // Check if database connection is enabled
    FaceDatabaseHelper &dbHelper = get_db_helper();
    bool useDatabase = dbHelper.isEnabled();

    if (isApiLoggingEnabled()) {
      PLOG_INFO << "[RecognitionHandler] Register subject: " << subjectName
                << ", Database enabled: " << (useDatabase ? "yes" : "no");
    }

    if (useDatabase) {
      // Test database connection first
      std::string testError;
      if (!dbHelper.testConnection(testError)) {
        if (isApiLoggingEnabled()) {
          PLOG_WARNING
              << "[RecognitionHandler] Database connection test failed: "
              << testError << ", falling back to file";
        }
        useDatabase = false;
      } else {
        if (isApiLoggingEnabled()) {
          PLOG_INFO << "[RecognitionHandler] Database connection test "
                       "successful, will save to database";
        }
      }
    } else {
      if (isApiLoggingEnabled()) {
        PLOG_INFO << "[RecognitionHandler] Database not enabled, using "
                     "file-based storage";
      }
    }

    if (useDatabase) {
      // Try to save to database first
      FaceDatabase &db = get_database();

      // Extract embedding using FaceDatabase
      // Note: register_face_from_image will save to file, but we'll remove it
      // if database save succeeds
      std::string dbError;
      if (!db.register_face_from_image(imageData, subjectName, detProbThreshold,
                                       dbError)) {
        // If embedding extraction fails, fallback to file (already saved)
        if (isApiLoggingEnabled()) {
          PLOG_WARNING << "[RecognitionHandler] Failed to extract embedding: "
                       << dbError;
        }
        error = "Failed to extract face embedding: " + dbError;
        return false;
      }

      // Get embedding for database save
      const auto &database = db.get_database();
      auto it = database.find(subjectName);
      if (it != database.end()) {
        std::string base64Image = encodeBase64(imageData);
        std::string dbSaveError;

        // Try to save to database
        if (isApiLoggingEnabled()) {
          PLOG_INFO << "[RecognitionHandler] Attempting to save to database: "
                       "image_id="
                    << imageId << ", subject=" << subjectName
                    << ", embedding_size=" << it->second.size();
        }

        if (dbHelper.saveFace(imageId, subjectName, base64Image, it->second,
                              dbSaveError)) {
          if (isApiLoggingEnabled()) {
            PLOG_INFO << "[RecognitionHandler] ✓ Face saved to database "
                         "successfully: image_id="
                      << imageId << ", subject=" << subjectName;
          }

          // Remove from file database since we're using database
          db.remove_subject(subjectName);

          if (isApiLoggingEnabled()) {
            PLOG_DEBUG
                << "[RecognitionHandler] Removed subject from file database";
          }

          // Store image_id -> subject_name mapping and face image in memory
          {
            std::lock_guard<std::mutex> lock(storage_mutex_);
            image_id_to_subject_[imageId] = subjectName;
            face_subjects_storage_[subjectName].push_back(imageId);
            face_images_storage_[subjectName] = base64Image;
          }

          return true;
        } else {
          // Database save failed, keep file-based storage (already saved by
          // register_face_from_image)
          if (isApiLoggingEnabled()) {
            PLOG_ERROR << "[RecognitionHandler] ✗ Database save FAILED: "
                       << dbSaveError;
            PLOG_WARNING
                << "[RecognitionHandler] Using file-based storage as fallback";
            PLOG_WARNING << "[RecognitionHandler] Check MySQL connection, "
                            "permissions, and logs";
          }
          // File already saved, just update memory storage
          {
            std::lock_guard<std::mutex> lock(storage_mutex_);
            image_id_to_subject_[imageId] = subjectName;
            face_subjects_storage_[subjectName].push_back(imageId);
            face_images_storage_[subjectName] = base64Image;
          }
          return true;
        }
      } else {
        // Embedding not found (should not happen)
        error = "Failed to get embedding after extraction";
        return false;
      }
    }

    // Use file-based storage (database not enabled)
    FaceDatabase &db = get_database();
    std::string dbError;
    if (!db.register_face_from_image(imageData, subjectName, detProbThreshold,
                                     dbError)) {
      error = "Failed to register face: " + dbError;
      return false;
    }

    // Store image_id -> subject_name mapping and face image
    {
      std::lock_guard<std::mutex> lock(storage_mutex_);
      image_id_to_subject_[imageId] = subjectName;
      face_subjects_storage_[subjectName].push_back(imageId);
      // Store face image as base64
      face_images_storage_[subjectName] = encodeBase64(imageData);
    }

    return true;

  } catch (const std::exception &e) {
    error = "Error processing image: " + std::string(e.what());
    return false;
  }
}

bool RecognitionHandler::extractImageFromRequest(
    const HttpRequestPtr &req, std::vector<unsigned char> &imageData,
    std::string &error) const {
  std::string contentType = req->getHeader("Content-Type");

  if (isApiLoggingEnabled()) {
    PLOG_DEBUG << "[RecognitionHandler] Content-Type: " << contentType;
  }

  // Check if it's JSON (application/json)
  if (contentType.find("application/json") != std::string::npos) {
    if (isApiLoggingEnabled()) {
      PLOG_DEBUG << "[RecognitionHandler] Extracting image from JSON (base64)";
    }
    // Try to extract from JSON (base64)
    if (extractImageFromJson(req, imageData, error)) {
      // Validate format and size
      if (!validateImageFormatAndSize(imageData, error)) {
        return false;
      }
      if (isApiLoggingEnabled()) {
        PLOG_DEBUG << "[RecognitionHandler] Successfully extracted and "
                      "validated image from JSON, size: "
                   << imageData.size() << " bytes";
      }
      return true;
    }
    return false;
  }

  // Check if it's multipart/form-data
  if (contentType.find("multipart/form-data") != std::string::npos) {
    if (isApiLoggingEnabled()) {
      PLOG_DEBUG
          << "[RecognitionHandler] Extracting image from multipart/form-data";
    }
    // Try to extract from multipart (binary or base64)
    if (extractImageData(req, imageData, error)) {
      // Validate format and size
      if (!validateImageFormatAndSize(imageData, error)) {
        return false;
      }

      // Convert to base64 for processing (as requested)
      // Note: The imageData is already in binary format, but we can encode it
      // to base64 if needed For now, we keep it as binary since OpenCV can
      // handle binary data directly If base64 is required for
      // storage/transmission, it can be encoded here

      if (isApiLoggingEnabled()) {
        PLOG_DEBUG << "[RecognitionHandler] Successfully extracted and "
                      "validated image from multipart, size: "
                   << imageData.size() << " bytes";
        // Optionally encode to base64 for logging/debugging
        std::string base64Encoded = encodeBase64(imageData);
        PLOG_DEBUG << "[RecognitionHandler] Image encoded to base64, length: "
                   << base64Encoded.length() << " characters";
      }
      return true;
    }
    return false;
  }

  // Try JSON first (some clients don't set Content-Type correctly)
  if (isApiLoggingEnabled()) {
    PLOG_DEBUG << "[RecognitionHandler] Content-Type not recognized, trying "
                  "JSON first";
  }
  if (extractImageFromJson(req, imageData, error)) {
    // Validate format and size
    if (!validateImageFormatAndSize(imageData, error)) {
      return false;
    }
    if (isApiLoggingEnabled()) {
      PLOG_DEBUG << "[RecognitionHandler] Successfully extracted and validated "
                    "image from JSON (fallback), size: "
                 << imageData.size() << " bytes";
    }
    return true;
  }

  // Try multipart
  if (isApiLoggingEnabled()) {
    PLOG_DEBUG << "[RecognitionHandler] Trying multipart/form-data (fallback)";
  }
  if (extractImageData(req, imageData, error)) {
    // Validate format and size
    if (!validateImageFormatAndSize(imageData, error)) {
      return false;
    }
    if (isApiLoggingEnabled()) {
      PLOG_DEBUG << "[RecognitionHandler] Successfully extracted and validated "
                    "image from multipart (fallback), size: "
                 << imageData.size() << " bytes";
    }
    return true;
  }

  error = "Unsupported Content-Type. Expected application/json (with base64) "
          "or multipart/form-data (with image file). Received: " +
          contentType;
  return false;
}

void RecognitionHandler::registerFaceSubject(
    const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback) {
  // Set handler start time for accurate metrics
  MetricsInterceptor::setHandlerStartTime(req);

  auto start_time = std::chrono::steady_clock::now();

  if (isApiLoggingEnabled()) {
    PLOG_INFO << "[API] POST /v1/recognition/faces - Register face subject";
    PLOG_DEBUG << "[API] Request from: " << req->getPeerAddr().toIpPort();
  }

  try {
    // Parse query parameters
    std::string subjectName = req->getParameter("subject");
    
    // URL decode the subject name if it contains encoded characters
    // (Drogon usually auto-decodes, but we do it explicitly for safety)
    if (!subjectName.empty()) {
      std::string decoded;
      decoded.reserve(subjectName.length());
      for (size_t i = 0; i < subjectName.length(); ++i) {
        if (subjectName[i] == '%' && i + 2 < subjectName.length()) {
          // Try to decode hex value
          char hex[3] = {subjectName[i + 1], subjectName[i + 2], '\0'};
          char *end;
          unsigned long value = std::strtoul(hex, &end, 16);
          if (*end == '\0' && value <= 255) {
            decoded += static_cast<char>(value);
            i += 2; // Skip the hex digits
          } else {
            decoded += subjectName[i]; // Invalid encoding, keep as-is
          }
        } else {
          decoded += subjectName[i];
        }
      }
      subjectName = decoded;
    }
    
    if (subjectName.empty()) {
      if (isApiLoggingEnabled()) {
        PLOG_WARNING << "[API] POST /v1/recognition/faces - Missing required "
                        "parameter: subject";
      }
      callback(createErrorResponse(
          400, "Invalid request", "Missing required query parameter: subject"));
      return;
    }

    std::string detProbThresholdStr = req->getParameter("det_prob_threshold");
    double detProbThreshold = 0.5; // Default
    if (!detProbThresholdStr.empty()) {
      try {
        detProbThreshold = std::stod(detProbThresholdStr);
      } catch (...) {
        detProbThreshold = 0.5;
      }
    }

    if (isApiLoggingEnabled()) {
      PLOG_DEBUG << "[API] Register subject: " << subjectName
                 << ", det_prob_threshold: " << detProbThreshold;
    }

    // Extract image data from request (supports both JSON base64 and
    // multipart/form-data)
    std::vector<unsigned char> imageData;
    std::string imageError;
    if (!extractImageFromRequest(req, imageData, imageError)) {
      if (isApiLoggingEnabled()) {
        PLOG_WARNING << "[API] POST /v1/recognition/faces - " << imageError;
      }
      callback(createErrorResponse(400, "Invalid request", imageError));
      return;
    }

    // Decode image first
    cv::Mat image = cv::imdecode(imageData, cv::IMREAD_COLOR);
    if (image.empty()) {
      if (isApiLoggingEnabled()) {
        PLOG_WARNING
            << "[API] POST /v1/recognition/faces - Failed to decode image";
        PLOG_WARNING << "[API] POST /v1/recognition/faces - Image data size: "
                     << imageData.size() << " bytes";
        PLOG_WARNING << "[API] POST /v1/recognition/faces - Subject: "
                     << subjectName;
      }
      callback(
          createErrorResponse(400, "Invalid request",
                              "Invalid image format or corrupted image data"));
      return;
    }

    if (isApiLoggingEnabled()) {
      PLOG_DEBUG << "[API] POST /v1/recognition/faces - Successfully decoded "
                    "image: "
                 << image.cols << "x" << image.rows << " pixels";
    }

    // Face detection node: Check if image contains a face before registration
    // This step is mandatory - images without detected faces will be rejected
    FaceDatabase &db = get_database();
    std::string detector_path = db.get_detector_model_path();
    if (detector_path.empty()) {
      if (isApiLoggingEnabled()) {
        PLOG_ERROR << "[API] POST /v1/recognition/faces - Face detector "
                      "model not found";
        PLOG_ERROR << "[API] POST /v1/recognition/faces - Subject: "
                   << subjectName;
        PLOG_ERROR << "[API] POST /v1/recognition/faces - Image size: "
                   << image.cols << "x" << image.rows;
      }
      callback(createErrorResponse(500, "Internal server error",
                                   "Face detector model not found"));
      return;
    }

    if (isApiLoggingEnabled()) {
      PLOG_DEBUG << "[API] POST /v1/recognition/faces - Using detector: "
                 << detector_path;
    }

    // Create face detector with actual image size for better accuracy
    cv::Ptr<cv::FaceDetectorYN> face_detector;
    // Use actual image size instead of fixed 320x320 for better detection
    // accuracy
    cv::Size inputSize = image.size();
    // Ensure minimum size for detector (some models require minimum
    // dimensions)
    if (inputSize.width < 320)
      inputSize.width = 320;
    if (inputSize.height < 320)
      inputSize.height = 320;

    try {
      face_detector = cv::FaceDetectorYN::create(
          detector_path, "", inputSize, static_cast<float>(detProbThreshold),
          0.3f, 5000, cv::dnn::DNN_BACKEND_OPENCV, cv::dnn::DNN_TARGET_CPU);
    } catch (const cv::Exception &e) {
      if (isApiLoggingEnabled()) {
        PLOG_ERROR << "[API] POST /v1/recognition/faces - Failed to create "
                      "face detector: "
                   << e.what();
        PLOG_ERROR << "[API] POST /v1/recognition/faces - OpenCV error code: "
                   << e.code << ", error message: " << e.msg;
        PLOG_ERROR << "[API] POST /v1/recognition/faces - Detector path: "
                   << detector_path;
        PLOG_ERROR << "[API] POST /v1/recognition/faces - Input size: "
                   << inputSize.width << "x" << inputSize.height
                   << ", threshold: " << detProbThreshold;
        PLOG_ERROR << "[API] POST /v1/recognition/faces - Subject: "
                   << subjectName;
      }
      callback(createErrorResponse(500, "Internal server error",
                                   "Failed to create face detector"));
      return;
    } catch (const std::exception &e) {
      if (isApiLoggingEnabled()) {
        PLOG_ERROR << "[API] POST /v1/recognition/faces - Failed to create "
                      "face detector: "
                   << e.what();
        PLOG_ERROR << "[API] POST /v1/recognition/faces - Detector path: "
                   << detector_path;
        PLOG_ERROR << "[API] POST /v1/recognition/faces - Input size: "
                   << inputSize.width << "x" << inputSize.height
                   << ", threshold: " << detProbThreshold;
        PLOG_ERROR << "[API] POST /v1/recognition/faces - Subject: "
                   << subjectName;
      }
      callback(createErrorResponse(500, "Internal server error",
                                   "Failed to create face detector"));
      return;
    }

    if (face_detector.empty()) {
      if (isApiLoggingEnabled()) {
        PLOG_ERROR << "[API] POST /v1/recognition/faces - Face detector "
                      "creation returned empty";
      }
      callback(createErrorResponse(500, "Internal server error",
                                   "Face detector creation failed"));
      return;
    }

    // Set input size to actual image size for detection
    cv::Size imageSize = image.size();
    if (isApiLoggingEnabled()) {
      PLOG_DEBUG << "[API] POST /v1/recognition/faces - Setting detector input "
                    "size to: "
                 << imageSize.width << "x" << imageSize.height;
    }
    
    try {
      face_detector->setInputSize(imageSize);
    } catch (const cv::Exception &e) {
      if (isApiLoggingEnabled()) {
        PLOG_ERROR << "[API] POST /v1/recognition/faces - Failed to set input "
                      "size: "
                   << e.what();
        PLOG_ERROR << "[API] POST /v1/recognition/faces - OpenCV error code: "
                   << e.code << ", error message: " << e.msg;
        PLOG_ERROR << "[API] POST /v1/recognition/faces - Image size: "
                   << imageSize.width << "x" << imageSize.height;
        PLOG_ERROR << "[API] POST /v1/recognition/faces - Subject: "
                   << subjectName;
      }
      callback(createErrorResponse(500, "Internal server error",
                                   "Failed to configure face detector"));
      return;
    } catch (const std::exception &e) {
      if (isApiLoggingEnabled()) {
        PLOG_ERROR << "[API] POST /v1/recognition/faces - Failed to set input "
                      "size: "
                   << e.what();
        PLOG_ERROR << "[API] POST /v1/recognition/faces - Image size: "
                   << imageSize.width << "x" << imageSize.height;
        PLOG_ERROR << "[API] POST /v1/recognition/faces - Subject: "
                   << subjectName;
      }
      callback(createErrorResponse(500, "Internal server error",
                                   "Failed to configure face detector"));
      return;
    }

    if (isApiLoggingEnabled()) {
      PLOG_DEBUG << "[API] POST /v1/recognition/faces - Running face detection "
                    "on image: "
                 << image.cols << "x" << image.rows
                 << " pixels, threshold: " << detProbThreshold;
    }

    // Detect faces in the image
    cv::Mat faces;
    try {
      face_detector->detect(image, faces);
    } catch (const cv::Exception &e) {
      if (isApiLoggingEnabled()) {
        PLOG_ERROR
            << "[API] POST /v1/recognition/faces - Face detection exception: "
            << e.what();
        PLOG_ERROR << "[API] POST /v1/recognition/faces - OpenCV error code: "
                   << e.code << ", error message: " << e.msg;
        PLOG_ERROR << "[API] POST /v1/recognition/faces - Detector path: "
                   << detector_path;
        PLOG_ERROR << "[API] POST /v1/recognition/faces - Image size: "
                   << image.cols << "x" << image.rows
                   << ", threshold: " << detProbThreshold;
        PLOG_ERROR << "[API] POST /v1/recognition/faces - Subject: "
                   << subjectName;
      }
      callback(createErrorResponse(500, "Internal server error",
                                   "Face detection failed"));
      return;
    } catch (const std::exception &e) {
      if (isApiLoggingEnabled()) {
        PLOG_ERROR
            << "[API] POST /v1/recognition/faces - Face detection exception: "
            << e.what();
        PLOG_ERROR << "[API] POST /v1/recognition/faces - Detector path: "
                   << detector_path;
        PLOG_ERROR << "[API] POST /v1/recognition/faces - Image size: "
                   << image.cols << "x" << image.rows
                   << ", threshold: " << detProbThreshold;
        PLOG_ERROR << "[API] POST /v1/recognition/faces - Subject: "
                   << subjectName;
      }
      callback(createErrorResponse(500, "Internal server error",
                                   "Face detection failed"));
      return;
    }

    // Validate: Reject image if no face is detected
    // This is a mandatory check - user must upload an image with a detectable
    // face
    if (faces.rows == 0 || faces.empty()) {
      if (isApiLoggingEnabled()) {
        PLOG_WARNING << "[API] POST /v1/recognition/faces - Face detection "
                        "node: No face detected in image. "
                     << "Image size: " << image.cols << "x" << image.rows
                     << ", detection threshold: " << detProbThreshold
                     << ". Registration rejected - user must upload an image "
                        "with a detectable face.";
      }
      callback(createErrorResponse(
          400, "Registration failed",
          "No face detected in the uploaded image. Please upload a different "
          "image that contains a clear, detectable face. The image must show a "
          "person's face clearly."));
      return;
    }

    // Additional validation: Check if detected faces meet minimum confidence
    // threshold
    int validFaceCount = 0;
    for (int i = 0; i < faces.rows; i++) {
      // faces matrix format: [x, y, w, h, x_re, y_re, x_le, y_le, x_nt, y_nt,
      // x_rcm, y_rcm, x_lcm, y_lcm, confidence]
      float confidence = faces.at<float>(i, 14);
      if (confidence >= static_cast<float>(detProbThreshold)) {
        validFaceCount++;
      }
    }

    if (validFaceCount == 0) {
      if (isApiLoggingEnabled()) {
        PLOG_WARNING << "[API] POST /v1/recognition/faces - Face detection "
                        "node: No faces with sufficient confidence. "
                     << "Detected " << faces.rows
                     << " face(s) but none met threshold " << detProbThreshold;
      }
      callback(
          createErrorResponse(400, "Registration failed",
                              "No face detected with sufficient confidence in "
                              "the uploaded image. Please upload a different "
                              "image with a clearer, more visible face."));
      return;
    }

    if (isApiLoggingEnabled()) {
      PLOG_INFO << "[API] POST /v1/recognition/faces - Face detection node: "
                   "Successfully detected "
                << validFaceCount << " valid face(s) out of " << faces.rows
                << " detected, proceeding with registration";
    }

    // Register subject
    std::string imageId;
    std::string registerError;
    if (!registerSubject(subjectName, imageData, detProbThreshold, imageId,
                         registerError)) {
      if (isApiLoggingEnabled()) {
        PLOG_WARNING << "[API] POST /v1/recognition/faces - " << registerError;
      }
      callback(createErrorResponse(400, "Registration failed", registerError));
      return;
    }

    // Build response
    Json::Value response;
    response["image_id"] = imageId;
    response["subject"] = subjectName;

    auto resp = HttpResponse::newHttpJsonResponse(response);
    resp->setStatusCode(k200OK);

    // Add CORS headers
    resp->addHeader("Access-Control-Allow-Origin", "*");
    resp->addHeader("Access-Control-Allow-Methods", "POST, OPTIONS");
    resp->addHeader("Access-Control-Allow-Headers", "Content-Type");

    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time);

    if (isApiLoggingEnabled()) {
      PLOG_INFO
          << "[API] POST /v1/recognition/faces - Success: Registered subject '"
          << subjectName << "' with image_id '" << imageId << "' - "
          << duration.count() << "ms";
    }

    // Record metrics and call callback
    MetricsInterceptor::callWithMetrics(req, resp, std::move(callback));

  } catch (const std::exception &e) {
    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time);
    if (isApiLoggingEnabled()) {
      PLOG_ERROR << "[API] POST /v1/recognition/faces - Exception: " << e.what()
                 << " - " << duration.count() << "ms";
    }
    callback(createErrorResponse(500, "Internal server error", e.what()));
  } catch (...) {
    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time);
    if (isApiLoggingEnabled()) {
      PLOG_ERROR << "[API] POST /v1/recognition/faces - Unknown exception - "
                 << duration.count() << "ms";
    }
    callback(createErrorResponse(500, "Internal server error",
                                 "Unknown error occurred"));
  }
}

void RecognitionHandler::handleOptionsFaces(
    const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback) {
  // Set handler start time for accurate metrics
  MetricsInterceptor::setHandlerStartTime(req);

  auto resp = HttpResponse::newHttpResponse();
  resp->setStatusCode(k200OK);
  resp->addHeader("Access-Control-Allow-Origin", "*");
  resp->addHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
  resp->addHeader("Access-Control-Allow-Headers", "Content-Type");
  resp->addHeader("Access-Control-Max-Age", "3600");

  // Record metrics and call callback
  MetricsInterceptor::callWithMetrics(req, resp, std::move(callback));
}

void RecognitionHandler::listFaceSubjects(
    const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback) {
  // Set handler start time for accurate metrics
  MetricsInterceptor::setHandlerStartTime(req);

  auto start_time = std::chrono::steady_clock::now();

  if (isApiLoggingEnabled()) {
    PLOG_INFO << "[API] GET /v1/recognition/faces - List face subjects";
    PLOG_DEBUG << "[API] Request from: " << req->getPeerAddr().toIpPort();
  }

  try {
    // Parse query parameters
    std::string pageStr = req->getParameter("page");
    std::string sizeStr = req->getParameter("size");
    std::string subjectFilter = req->getParameter("subject");

    int page = 0;  // Default
    int size = 20; // Default

    if (!pageStr.empty()) {
      try {
        page = std::stoi(pageStr);
        if (page < 0)
          page = 0;
      } catch (...) {
        page = 0;
      }
    }

    if (!sizeStr.empty()) {
      try {
        size = std::stoi(sizeStr);
        if (size < 1)
          size = 20;
        if (size > 100)
          size = 100; // Limit max page size
      } catch (...) {
        size = 20;
      }
    }

    if (isApiLoggingEnabled()) {
      PLOG_DEBUG << "[API] List parameters - page: " << page
                 << ", size: " << size << ", subject filter: "
                 << (subjectFilter.empty() ? "all" : subjectFilter);
    }

    // Get face subjects
    Json::Value response = getFaceSubjects(page, size, subjectFilter);

    auto resp = HttpResponse::newHttpJsonResponse(response);
    resp->setStatusCode(k200OK);

    // Add CORS headers
    resp->addHeader("Access-Control-Allow-Origin", "*");
    resp->addHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
    resp->addHeader("Access-Control-Allow-Headers", "Content-Type");

    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time);

    if (isApiLoggingEnabled()) {
      int totalElements = response.get("total_elements", 0).asInt();
      PLOG_INFO << "[API] GET /v1/recognition/faces - Success: Found "
                << totalElements << " face(s) - " << duration.count() << "ms";
    }

    // Record metrics and call callback
    MetricsInterceptor::callWithMetrics(req, resp, std::move(callback));

  } catch (const std::exception &e) {
    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time);
    if (isApiLoggingEnabled()) {
      PLOG_ERROR << "[API] GET /v1/recognition/faces - Exception: " << e.what()
                 << " - " << duration.count() << "ms";
    }
    callback(createErrorResponse(500, "Internal server error", e.what()));
  } catch (...) {
    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time);
    if (isApiLoggingEnabled()) {
      PLOG_ERROR << "[API] GET /v1/recognition/faces - Unknown exception - "
                 << duration.count() << "ms";
    }
    callback(createErrorResponse(500, "Internal server error",
                                 "Unknown error occurred"));
  }
}

Json::Value
RecognitionHandler::getFaceSubjects(int page, int size,
                                    const std::string &subjectFilter) const {
  Json::Value result;

  FaceDatabaseHelper &dbHelper = get_db_helper();

  // Check if database connection is enabled
  if (dbHelper.isEnabled()) {
    // Use database
    std::vector<std::pair<std::string, std::string>> faces;
    int totalCount = 0;
    std::string error;

    if (dbHelper.getFacesFromDatabase(page, size, subjectFilter, faces,
                                      totalCount, error)) {
      // Build response from database results
      Json::Value facesArray(Json::arrayValue);
      for (const auto &[imageId, subject] : faces) {
        Json::Value face;
        face["image_id"] = imageId;
        face["subject"] = subject;
        facesArray.append(face);
      }

      int totalPages = (totalCount + size - 1) / size; // Ceiling division

      result["faces"] = facesArray;
      result["page_number"] = page;
      result["page_size"] = size;
      result["total_pages"] = totalPages;
      result["total_elements"] = totalCount;

      return result;
    } else {
      // Database query failed, fallback to file-based storage
      if (isApiLoggingEnabled()) {
        PLOG_WARNING << "[API] Failed to get faces from database: " << error
                     << ", falling back to file storage";
      }
    }
  }

  // Use file-based storage (fallback or when database not enabled)
  // Ensure database is loaded before accessing storage
  get_database();

  // Collect all image_id -> subject mappings
  std::vector<std::pair<std::string, std::string>>
      all_faces; // (image_id, subject)

  {
    std::lock_guard<std::mutex> lock(storage_mutex_);

    if (subjectFilter.empty()) {
      // Get all faces
      for (const auto &[imageId, subjectName] : image_id_to_subject_) {
        all_faces.push_back({imageId, subjectName});
      }
    } else {
      // Get faces only from filtered subject
      auto it = face_subjects_storage_.find(subjectFilter);
      if (it != face_subjects_storage_.end()) {
        for (const auto &imageId : it->second) {
          all_faces.push_back({imageId, subjectFilter});
        }
      }
    }
  }

  // Calculate pagination
  int totalElements = static_cast<int>(all_faces.size());
  int totalPages = (totalElements + size - 1) / size; // Ceiling division

  // Apply pagination
  int start_idx = page * size;
  int end_idx = std::min(start_idx + size, totalElements);

  Json::Value faces(Json::arrayValue);
  for (int i = start_idx; i < end_idx; i++) {
    Json::Value face;
    face["image_id"] = all_faces[i].first;
    face["subject"] = all_faces[i].second;
    faces.append(face);
  }

  result["faces"] = faces;
  result["page_number"] = page;
  result["page_size"] = size;
  result["total_pages"] = totalPages;
  result["total_elements"] = totalElements;

  return result;
}

std::string
RecognitionHandler::extractSubjectFromPath(const HttpRequestPtr &req) const {
  // Try getParameter first (standard way)
  std::string subject = req->getParameter("subject");

  // Fallback: extract from path if getParameter doesn't work
  if (subject.empty()) {
    std::string path = req->getPath();
    size_t subjectsPos = path.find("/subjects/");
    if (subjectsPos != std::string::npos) {
      size_t start = subjectsPos + 10;    // length of "/subjects/"
      size_t end = path.find("?", start); // Stop at query string if present
      if (end == std::string::npos) {
        end = path.length();
      }
      subject = path.substr(start, end - start);
    }
  }

  // URL decode the subject name if it contains encoded characters
  if (!subject.empty()) {
    std::string decoded;
    decoded.reserve(subject.length());
    for (size_t i = 0; i < subject.length(); ++i) {
      if (subject[i] == '%' && i + 2 < subject.length()) {
        // Try to decode hex value
        char hex[3] = {subject[i + 1], subject[i + 2], '\0'};
        char *end;
        unsigned long value = std::strtoul(hex, &end, 16);
        if (*end == '\0' && value <= 255) {
          decoded += static_cast<char>(value);
          i += 2; // Skip the hex digits
        } else {
          decoded += subject[i]; // Invalid encoding, keep as-is
        }
      } else {
        decoded += subject[i];
      }
    }
    subject = decoded;
  }

  return subject;
}

bool RecognitionHandler::renameSubjectName(const std::string &oldSubjectName,
                                           const std::string &newSubjectName,
                                           std::string &error) const {
  if (oldSubjectName.empty()) {
    error = "Old subject name cannot be empty";
    return false;
  }

  if (newSubjectName.empty()) {
    error = "New subject name cannot be empty";
    return false;
  }

  if (oldSubjectName == newSubjectName) {
    // No change needed, but this is still considered successful
    return true;
  }

  // Check if database connection is enabled
  FaceDatabaseHelper &dbHelper = get_db_helper();
  bool useDatabase = dbHelper.isEnabled();

  bool db_has_old = false;
  bool need_merge = false;

  if (useDatabase) {
    // Check if old subject exists in database
    std::string dbError;
    std::vector<std::string> oldImageIds;
    if (dbHelper.findFacesBySubject(oldSubjectName, oldImageIds, dbError)) {
      db_has_old = true;

      // Check if new subject exists (for merge)
      std::vector<std::string> newImageIds;
      if (dbHelper.findFacesBySubject(newSubjectName, newImageIds, dbError)) {
        need_merge = true;
      }
    }
  }

  // Check if old subject exists in file-based storage or memory
  const std::map<std::string, std::vector<float>> *db_map = nullptr;
  FaceDatabase *db = nullptr;
  std::map<std::string, std::vector<float>>::const_iterator db_old_it;

  if (!useDatabase) {
    // Use file-based storage
    try {
      db = &get_database();
      db_map = &db->get_database();
      db_old_it = db_map->find(oldSubjectName);
      db_has_old = (db_map != nullptr && db_old_it != db_map->end());

      if (db_has_old) {
        // Check if new subject exists (for merge)
        auto db_new_it = db_map->find(newSubjectName);
        need_merge = (db_new_it != db_map->end());
      }
    } catch (const std::exception &e) {
      db_map = nullptr;
      db = nullptr;
      static const std::map<std::string, std::vector<float>> empty_map;
      db_old_it = empty_map.end();
    }
  }

  // Check if old subject exists in memory storage
  std::lock_guard<std::mutex> lock(storage_mutex_);
  auto oldIt = face_subjects_storage_.find(oldSubjectName);

  // Subject must exist in at least one place
  if (!db_has_old &&
      (oldIt == face_subjects_storage_.end() || oldIt->second.empty())) {
    error = "Subject '" + oldSubjectName + "' not found in face database";
    return false;
  }

  // Update database or file
  if (useDatabase && db_has_old) {
    // Update database
    std::string dbError;
    if (!dbHelper.updateSubjectName(oldSubjectName, newSubjectName, need_merge,
                                    dbError)) {
      if (isApiLoggingEnabled()) {
        PLOG_WARNING << "[RecognitionHandler] Failed to update subject name in "
                        "database: "
                     << dbError;
      }
      // Continue with memory storage update even if database update fails
    }
  } else if (db_has_old && db != nullptr && db_map != nullptr) {
    // Update file-based storage
    // Check if new subject already exists in database
    auto db_new_it = db_map->find(newSubjectName);
    bool need_merge = (db_new_it != db_map->end());
    std::vector<float> old_embedding = db_old_it->second;
    std::vector<float> new_embedding;
    if (need_merge) {
      new_embedding = db_new_it->second;
    }

    // Reload database file to modify it
    std::string db_path = db->get_database_path();
    std::ifstream in_file(db_path);
    std::map<std::string, std::vector<float>> updated_faces;

    if (in_file.is_open()) {
      std::string line;
      while (std::getline(in_file, line)) {
        if (line.empty())
          continue;
        size_t pos = line.find('|');
        if (pos == std::string::npos)
          continue;

        std::string name = line.substr(0, pos);
        std::string embedding_str = line.substr(pos + 1);

        // Trim name
        name.erase(0, name.find_first_not_of(" \t"));
        name.erase(name.find_last_not_of(" \t") + 1);

        if (name.empty())
          continue;

        std::vector<float> embedding;
        std::stringstream ss(embedding_str);
        std::string value;

        while (std::getline(ss, value, ',')) {
          value.erase(0, value.find_first_not_of(" \t\r\n"));
          value.erase(value.find_last_not_of(" \t\r\n") + 1);
          if (value.empty())
            continue;
          try {
            embedding.push_back(std::stof(value));
          } catch (...) {
            break;
          }
        }

        if (!embedding.empty()) {
          if (name == oldSubjectName) {
            // Skip old subject - will be renamed to new name
            continue;
          } else if (name == newSubjectName && need_merge) {
            // Merge embeddings: average old and new
            if (old_embedding.size() == embedding.size()) {
              std::vector<float> merged_embedding(embedding.size());
              for (size_t i = 0; i < embedding.size(); i++) {
                merged_embedding[i] = (old_embedding[i] + embedding[i]) / 2.0f;
              }
              // L2 normalize
              float norm = 0.0f;
              for (float val : merged_embedding) {
                norm += val * val;
              }
              norm = std::sqrt(norm);
              if (norm > 1e-6) {
                for (float &val : merged_embedding) {
                  val /= norm;
                }
              }
              updated_faces[newSubjectName] = merged_embedding;
            } else {
              // Size mismatch, keep existing
              updated_faces[newSubjectName] = embedding;
            }
          } else {
            // Keep other subjects as-is
            updated_faces[name] = embedding;
          }
        }
      }
      in_file.close();

      // If not merging, add old subject with new name
      if (!need_merge) {
        updated_faces[newSubjectName] = old_embedding;
      }

      // Save updated database
      std::ofstream out_file(db_path, std::ios::out | std::ios::trunc);
      if (out_file.is_open()) {
        for (const auto &[name, embedding] : updated_faces) {
          out_file << name << "|";
          for (size_t i = 0; i < embedding.size(); i++) {
            out_file << std::fixed << std::setprecision(6) << embedding[i];
            if (i < embedding.size() - 1)
              out_file << ",";
          }
          out_file << "\n";
        }
        out_file.close();
      }
    }
  }

  // Update storage mappings
  if (oldIt != face_subjects_storage_.end() && !oldIt->second.empty()) {
    // Update all image_id mappings
    for (const auto &imageId : oldIt->second) {
      image_id_to_subject_[imageId] = newSubjectName;
    }

    // Merge subjects if new subject already exists
    auto newIt = face_subjects_storage_.find(newSubjectName);
    if (newIt != face_subjects_storage_.end()) {
      // Merge: add all image IDs from old subject to new subject
      newIt->second.insert(newIt->second.end(), oldIt->second.begin(),
                           oldIt->second.end());
    } else {
      // Create new subject with old subject's image IDs
      face_subjects_storage_[newSubjectName] = oldIt->second;
    }

    // Remove old subject
    face_subjects_storage_.erase(oldIt);
  } else if (db_has_old) {
    // Subject exists in database but not in storage - create entry in storage
    // This can happen if subject was registered before storage tracking was
    // added
    face_subjects_storage_[newSubjectName] = std::vector<std::string>();
  }

  return true;
}

void RecognitionHandler::renameSubject(
    const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback) {
  auto start_time = std::chrono::steady_clock::now();

  std::string peerAddr = "unknown";
  if (req) {
    try {
      peerAddr = req->getPeerAddr().toIpPort();
    } catch (const std::exception &e) {
      // Ignore errors getting peer address
    }
  }

  if (isApiLoggingEnabled()) {
    PLOG_INFO
        << "[API] PUT /v1/recognition/subjects/{subject} - Rename face subject";
    PLOG_DEBUG << "[API] Request from: " << peerAddr;
  }

  try {
    // Extract old subject name from URL path
    std::string oldSubjectName = extractSubjectFromPath(req);
    if (oldSubjectName.empty()) {
      if (isApiLoggingEnabled()) {
        PLOG_WARNING << "[API] PUT /v1/recognition/subjects/{subject} - "
                        "Missing subject in path";
      }
      auto errorResp = createErrorResponse(400, "Invalid request",
                                           "Missing subject in URL path");
      errorResp->addHeader("Access-Control-Allow-Methods", "PUT, OPTIONS");
      errorResp->addHeader("Access-Control-Allow-Headers",
                           "Content-Type, x-api-key");
      callback(errorResp);
      return;
    }

    // Parse JSON body
    auto json = req->getJsonObject();
    if (!json) {
      if (isApiLoggingEnabled()) {
        PLOG_WARNING << "[API] PUT /v1/recognition/subjects/{subject} - "
                        "Invalid JSON body";
      }
      auto errorResp = createErrorResponse(400, "Invalid request",
                                           "Request body must be valid JSON");
      errorResp->addHeader("Access-Control-Allow-Methods", "PUT, OPTIONS");
      errorResp->addHeader("Access-Control-Allow-Headers",
                           "Content-Type, x-api-key");
      callback(errorResp);
      return;
    }

    // Validate required field: subject
    if (!json->isMember("subject") || !(*json)["subject"].isString()) {
      if (isApiLoggingEnabled()) {
        PLOG_WARNING << "[API] PUT /v1/recognition/subjects/{subject} - "
                        "Missing required field: subject";
      }
      auto errorResp = createErrorResponse(400, "Invalid request",
                                           "Missing required field: subject");
      errorResp->addHeader("Access-Control-Allow-Methods", "PUT, OPTIONS");
      errorResp->addHeader("Access-Control-Allow-Headers",
                           "Content-Type, x-api-key");
      callback(errorResp);
      return;
    }

    std::string newSubjectName = (*json)["subject"].asString();
    if (newSubjectName.empty()) {
      if (isApiLoggingEnabled()) {
        PLOG_WARNING << "[API] PUT /v1/recognition/subjects/{subject} - "
                        "Subject field is empty";
      }
      auto errorResp = createErrorResponse(400, "Invalid request",
                                           "Subject field cannot be empty");
      errorResp->addHeader("Access-Control-Allow-Methods", "PUT, OPTIONS");
      errorResp->addHeader("Access-Control-Allow-Headers",
                           "Content-Type, x-api-key");
      callback(errorResp);
      return;
    }

    if (isApiLoggingEnabled()) {
      PLOG_DEBUG << "[API] Rename subject: '" << oldSubjectName << "' -> '"
                 << newSubjectName << "'";
    }

    // Rename/merge subject
    std::string renameError;
    bool success =
        renameSubjectName(oldSubjectName, newSubjectName, renameError);

    // Build response
    Json::Value response;
    if (success) {
      response["updated"] = "true";
      response["old_subject"] = oldSubjectName;
      response["new_subject"] = newSubjectName;
      response["message"] = "Subject renamed successfully";
    } else {
      response["updated"] = "false";
      response["error"] = renameError;
      response["old_subject"] = oldSubjectName;
    }

    auto resp = HttpResponse::newHttpJsonResponse(response);
    resp->setStatusCode(success ? k200OK : k400BadRequest);

    // Add CORS headers
    resp->addHeader("Access-Control-Allow-Origin", "*");
    resp->addHeader("Access-Control-Allow-Methods", "PUT, OPTIONS");
    resp->addHeader("Access-Control-Allow-Headers", "Content-Type, x-api-key");

    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time);

    if (isApiLoggingEnabled()) {
      if (success) {
        PLOG_INFO << "[API] PUT /v1/recognition/subjects/{subject} - Success: "
                     "Renamed subject '"
                  << oldSubjectName << "' to '" << newSubjectName << "' - "
                  << duration.count() << "ms";
      } else {
        PLOG_WARNING
            << "[API] PUT /v1/recognition/subjects/{subject} - Failed: "
            << renameError << " - " << duration.count() << "ms";
      }
    }

    // Record metrics and call callback
    try {
      if (req) {
        MetricsInterceptor::callWithMetrics(req, resp, std::move(callback));
      } else {
        callback(resp);
      }
    } catch (const std::exception &e) {
      if (isApiLoggingEnabled()) {
        PLOG_ERROR << "[RecognitionHandler] Exception in MetricsInterceptor: " << e.what();
      }
      try {
        callback(resp);
      } catch (const std::exception &e_cb) {
        if (isApiLoggingEnabled()) {
          PLOG_ERROR << "[RecognitionHandler] Exception calling callback: " << e_cb.what();
        }
      }
    }

  } catch (const std::exception &e) {
    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time);
    if (isApiLoggingEnabled()) {
      PLOG_ERROR << "[API] PUT /v1/recognition/subjects/{subject} - Exception: "
                 << e.what() << " - " << duration.count() << "ms";
    }
    auto errorResp =
        createErrorResponse(500, "Internal server error", e.what());
    if (errorResp) {
      errorResp->addHeader("Access-Control-Allow-Methods", "PUT, OPTIONS");
      errorResp->addHeader("Access-Control-Allow-Headers",
                           "Content-Type, x-api-key");
      try {
        callback(errorResp);
      } catch (const std::exception &e_cb) {
        if (isApiLoggingEnabled()) {
          PLOG_ERROR << "[RecognitionHandler] Exception calling error callback: " << e_cb.what();
        }
      }
    }
  } catch (...) {
    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time);
    if (isApiLoggingEnabled()) {
      PLOG_ERROR << "[API] PUT /v1/recognition/subjects/{subject} - Unknown "
                    "exception - "
                 << duration.count() << "ms";
    }
    auto errorResp = createErrorResponse(500, "Internal server error",
                                         "Unknown error occurred");
    if (errorResp) {
      errorResp->addHeader("Access-Control-Allow-Methods", "PUT, OPTIONS");
      errorResp->addHeader("Access-Control-Allow-Headers",
                           "Content-Type, x-api-key");
      try {
        callback(errorResp);
      } catch (const std::exception &e_cb) {
        if (isApiLoggingEnabled()) {
          PLOG_ERROR << "[RecognitionHandler] Exception calling unknown error callback: " << e_cb.what();
        }
      }
    }
  }
}

void RecognitionHandler::handleOptionsSubjects(
    const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback) {
  // Set handler start time for accurate metrics
  MetricsInterceptor::setHandlerStartTime(req);

  auto resp = HttpResponse::newHttpResponse();
  resp->setStatusCode(k200OK);
  resp->addHeader("Access-Control-Allow-Origin", "*");
  resp->addHeader("Access-Control-Allow-Methods", "PUT, OPTIONS");
  resp->addHeader("Access-Control-Allow-Headers", "Content-Type, x-api-key");
  resp->addHeader("Access-Control-Max-Age", "3600");

  // Record metrics and call callback
  MetricsInterceptor::callWithMetrics(req, resp, std::move(callback));
}

bool RecognitionHandler::subjectExists(const std::string &subjectName) const {
  std::lock_guard<std::mutex> lock(storage_mutex_);
  auto it = face_subjects_storage_.find(subjectName);
  return (it != face_subjects_storage_.end() && !it->second.empty());
}

std::vector<std::string>
RecognitionHandler::getSubjectImageIds(const std::string &subjectName) const {
  std::lock_guard<std::mutex> lock(storage_mutex_);
  auto it = face_subjects_storage_.find(subjectName);
  if (it != face_subjects_storage_.end()) {
    return it->second;
  }
  return std::vector<std::string>();
}

void RecognitionHandler::addImageToSubject(const std::string &subjectName,
                                           const std::string &imageId) const {
  std::lock_guard<std::mutex> lock(storage_mutex_);
  face_subjects_storage_[subjectName].push_back(imageId);
}

void RecognitionHandler::removeSubject(const std::string &subjectName) const {
  std::lock_guard<std::mutex> lock(storage_mutex_);
  face_subjects_storage_.erase(subjectName);
}

void RecognitionHandler::mergeSubjects(
    const std::string &oldSubjectName,
    const std::string &newSubjectName) const {
  std::lock_guard<std::mutex> lock(storage_mutex_);

  auto oldIt = face_subjects_storage_.find(oldSubjectName);
  if (oldIt == face_subjects_storage_.end() || oldIt->second.empty()) {
    return; // Nothing to merge
  }

  auto newIt = face_subjects_storage_.find(newSubjectName);
  if (newIt == face_subjects_storage_.end()) {
    // New subject doesn't exist, create it
    face_subjects_storage_[newSubjectName] = oldIt->second;
  } else {
    // New subject exists, merge: append all image IDs from old to new
    newIt->second.insert(newIt->second.end(), oldIt->second.begin(),
                         oldIt->second.end());
  }

  // Remove old subject
  face_subjects_storage_.erase(oldIt);
}

void RecognitionHandler::renameSubjectInStorage(
    const std::string &oldSubjectName,
    const std::string &newSubjectName) const {
  std::lock_guard<std::mutex> lock(storage_mutex_);

  auto oldIt = face_subjects_storage_.find(oldSubjectName);
  if (oldIt == face_subjects_storage_.end()) {
    return; // Subject doesn't exist
  }

  // Move all image IDs to new subject name
  face_subjects_storage_[newSubjectName] = std::move(oldIt->second);

  // Remove old subject
  face_subjects_storage_.erase(oldIt);
}

std::string
RecognitionHandler::findSubjectByImageId(const std::string &imageId) const {
  std::lock_guard<std::mutex> lock(storage_mutex_);

  auto it = image_id_to_subject_.find(imageId);
  if (it != image_id_to_subject_.end()) {
    return it->second;
  }

  return std::string();
}

bool RecognitionHandler::removeImageFromSubject(
    const std::string &subjectName, const std::string &imageId) const {
  std::lock_guard<std::mutex> lock(storage_mutex_);

  auto it = face_subjects_storage_.find(subjectName);
  if (it == face_subjects_storage_.end()) {
    return false;
  }

  auto &imageIds = it->second;
  auto imageIt = std::find(imageIds.begin(), imageIds.end(), imageId);
  if (imageIt == imageIds.end()) {
    return false;
  }

  imageIds.erase(imageIt);

  // Remove subject if it has no more images
  if (imageIds.empty()) {
    face_subjects_storage_.erase(it);
  }

  return true;
}

bool RecognitionHandler::deleteImageFromStorage(
    const std::string &imageId, std::string &subjectName) const {
  std::lock_guard<std::mutex> lock(storage_mutex_);

  auto it = image_id_to_subject_.find(imageId);
  if (it == image_id_to_subject_.end()) {
    return false;
  }

  subjectName = it->second;

  // Remove from image_id_to_subject_
  image_id_to_subject_.erase(it);

  // Remove from face_subjects_storage_
  auto subjectIt = face_subjects_storage_.find(subjectName);
  if (subjectIt != face_subjects_storage_.end()) {
    auto &imageIds = subjectIt->second;
    auto imageIt = std::find(imageIds.begin(), imageIds.end(), imageId);
    if (imageIt != imageIds.end()) {
      imageIds.erase(imageIt);

      // Remove subject if it has no more images
      if (imageIds.empty()) {
        face_subjects_storage_.erase(subjectIt);
      }
    }
  }

  return true;
}

void RecognitionHandler::deleteFaceSubject(
    const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback) {
  // Set handler start time for accurate metrics
  MetricsInterceptor::setHandlerStartTime(req);

  auto start_time = std::chrono::steady_clock::now();

  if (isApiLoggingEnabled()) {
    PLOG_INFO << "[API] DELETE /v1/recognition/faces/{image_id} - Delete face "
                 "subject";
    PLOG_DEBUG << "[API] Request from: " << req->getPeerAddr().toIpPort();
  }

  try {
    // Extract identifier from URL path (can be image_id or subject name)
    std::string path = req->getPath();

    // Check if this is actually a face-database/connection request
    if (path.find("/face-database/connection") != std::string::npos) {
      // This should be handled by deleteFaceDatabaseConnection, not here
      if (isApiLoggingEnabled()) {
        PLOG_WARNING << "[API] DELETE /v1/recognition/faces/{image_id} - "
                        "Request path matches face-database/connection, "
                        "should be handled by deleteFaceDatabaseConnection";
      }
      callback(createErrorResponse(
          404, "Not Found",
          "Endpoint not found. Use DELETE "
          "/v1/recognition/face-database/connection instead"));
      return;
    }

    size_t facesPos = path.find("/faces/");
    if (facesPos == std::string::npos) {
      if (isApiLoggingEnabled()) {
        PLOG_WARNING
            << "[API] DELETE /v1/recognition/faces/{image_id} - Invalid path";
      }
      callback(createErrorResponse(400, "Invalid request", "Invalid URL path"));
      return;
    }

    size_t start = facesPos + 7; // length of "/faces/"
    size_t end = path.find("?", start);
    if (end == std::string::npos) {
      end = path.length();
    }
    std::string identifier = path.substr(start, end - start);

    if (identifier.empty()) {
      if (isApiLoggingEnabled()) {
        PLOG_WARNING << "[API] DELETE /v1/recognition/faces/{image_id} - "
                        "Missing identifier in path";
      }
      callback(createErrorResponse(400, "Invalid request",
                                   "Missing identifier in URL path"));
      return;
    }

    if (isApiLoggingEnabled()) {
      PLOG_DEBUG << "[API] Delete face subject with identifier: " << identifier;
    }

    FaceDatabaseHelper &dbHelper = get_db_helper();
    std::string subjectName;
    std::vector<std::string> deletedImageIds;

    // Check if database connection is enabled
    if (dbHelper.isEnabled()) {
      // Query from database
      std::string dbError;

      // Try to find by image_id first
      if (dbHelper.findFaceByImageId(identifier, subjectName, dbError)) {
        // Found by image_id
        deletedImageIds.push_back(identifier);
      } else {
        // Try to find by subject name
        if (dbHelper.findFacesBySubject(identifier, deletedImageIds, dbError)) {
          // Found by subject name
          subjectName = identifier;
        } else {
          // Not found in database
          if (isApiLoggingEnabled()) {
            PLOG_WARNING << "[API] DELETE /v1/recognition/faces/{image_id} - "
                            "Face not found in database: "
                         << identifier;
          }
          callback(createErrorResponse(404, "Not Found",
                                       "Face subject with identifier '" +
                                           identifier + "' not found"));
          return;
        }
      }
    } else {
      // Use file-based storage
      std::lock_guard<std::mutex> lock(storage_mutex_);

      auto imageIt = image_id_to_subject_.find(identifier);
      if (imageIt != image_id_to_subject_.end()) {
        // It's an image_id
        subjectName = imageIt->second;
        deletedImageIds.push_back(identifier);
        image_id_to_subject_.erase(imageIt);

        // Remove from face_subjects_storage_
        auto subjectIt = face_subjects_storage_.find(subjectName);
        if (subjectIt != face_subjects_storage_.end()) {
          auto &imageIds = subjectIt->second;
          auto idIt = std::find(imageIds.begin(), imageIds.end(), identifier);
          if (idIt != imageIds.end()) {
            imageIds.erase(idIt);
            if (imageIds.empty()) {
              face_subjects_storage_.erase(subjectIt);
            }
          }
        }
      } else {
        // Check if it's a subject name
        auto subjectIt = face_subjects_storage_.find(identifier);
        if (subjectIt != face_subjects_storage_.end()) {
          // It's a subject name - delete all images for this subject
          subjectName = identifier;
          deletedImageIds = subjectIt->second;

          // Remove all image_id mappings
          for (const auto &imageId : deletedImageIds) {
            image_id_to_subject_.erase(imageId);
          }

          // Remove subject
          face_subjects_storage_.erase(subjectIt);
        } else {
          if (isApiLoggingEnabled()) {
            PLOG_WARNING << "[API] DELETE /v1/recognition/faces/{image_id} - "
                            "Face not found: "
                         << identifier;
          }
          callback(createErrorResponse(404, "Not Found",
                                       "Face subject with identifier '" +
                                           identifier + "' not found"));
          return;
        }
      }
    }

    // Remove from database or file
    if (dbHelper.isEnabled()) {
      // Use database
      std::string dbError;
      if (deletedImageIds.size() == 1) {
        // Delete single image
        if (!dbHelper.deleteFace(deletedImageIds[0], dbError)) {
          if (isApiLoggingEnabled()) {
            PLOG_WARNING << "[API] Failed to delete face from database: "
                         << dbError;
          }
        }
      } else {
        // Delete all images for subject
        if (!dbHelper.deleteSubject(subjectName, dbError)) {
          if (isApiLoggingEnabled()) {
            PLOG_WARNING << "[API] Failed to delete subject from database: "
                         << dbError;
          }
        }
      }
    } else {
      // Use file-based storage
      FaceDatabase &db = get_database();
      db.remove_subject(subjectName);
    }

    // Build response
    Json::Value response;
    if (deletedImageIds.size() == 1) {
      response["image_id"] = deletedImageIds[0];
      response["subject"] = subjectName;
    } else {
      response["subject"] = subjectName;
      response["deleted_count"] = static_cast<int>(deletedImageIds.size());
      Json::Value imageIds(Json::arrayValue);
      for (const auto &id : deletedImageIds) {
        imageIds.append(id);
      }
      response["image_ids"] = imageIds;
    }

    auto resp = HttpResponse::newHttpJsonResponse(response);
    resp->setStatusCode(k200OK);

    // Add CORS headers
    resp->addHeader("Access-Control-Allow-Origin", "*");
    resp->addHeader("Access-Control-Allow-Methods", "DELETE, OPTIONS");
    resp->addHeader("Access-Control-Allow-Headers", "Content-Type, x-api-key");

    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time);

    if (isApiLoggingEnabled()) {
      if (deletedImageIds.size() == 1) {
        PLOG_INFO << "[API] DELETE /v1/recognition/faces/{image_id} - Success: "
                     "Deleted face subject '"
                  << deletedImageIds[0] << "' (subject: '" << subjectName
                  << "') - " << duration.count() << "ms";
      } else {
        PLOG_INFO << "[API] DELETE /v1/recognition/faces/{image_id} - Success: "
                     "Deleted "
                  << deletedImageIds.size() << " face(s) for subject '"
                  << subjectName << "' - " << duration.count() << "ms";
      }
    }

    // Record metrics and call callback
    MetricsInterceptor::callWithMetrics(req, resp, std::move(callback));

  } catch (const std::exception &e) {
    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time);
    if (isApiLoggingEnabled()) {
      PLOG_ERROR
          << "[API] DELETE /v1/recognition/faces/{image_id} - Exception: "
          << e.what() << " - " << duration.count() << "ms";
    }
    callback(createErrorResponse(500, "Internal server error", e.what()));
  } catch (...) {
    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time);
    if (isApiLoggingEnabled()) {
      PLOG_ERROR << "[API] DELETE /v1/recognition/faces/{image_id} - Unknown "
                    "exception - "
                 << duration.count() << "ms";
    }
    callback(createErrorResponse(500, "Internal server error",
                                 "Unknown error occurred"));
  }
}

void RecognitionHandler::handleOptionsSearch(
    const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback) {
  // Set handler start time for accurate metrics
  MetricsInterceptor::setHandlerStartTime(req);

  auto resp = HttpResponse::newHttpResponse();
  resp->setStatusCode(k200OK);
  resp->addHeader("Access-Control-Allow-Origin", "*");
  resp->addHeader("Access-Control-Allow-Methods", "POST, OPTIONS");
  resp->addHeader("Access-Control-Allow-Headers", "Content-Type, x-api-key");
  resp->addHeader("Access-Control-Max-Age", "3600");

  // Record metrics and call callback
  MetricsInterceptor::callWithMetrics(req, resp, std::move(callback));
}

void RecognitionHandler::searchAppearanceSubject(
    const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback) {
  // Set handler start time for accurate metrics
  MetricsInterceptor::setHandlerStartTime(req);

  auto start_time = std::chrono::steady_clock::now();

  if (isApiLoggingEnabled()) {
    PLOG_INFO
        << "[API] POST /v1/recognition/search - Search appearance subject";
    PLOG_DEBUG << "[API] Request from: " << req->getPeerAddr().toIpPort();
  }

  try {
    // Parse query parameters
    std::string thresholdStr = req->getParameter("threshold");
    double threshold = 0.5; // Default threshold
    if (!thresholdStr.empty()) {
      try {
        threshold = std::stod(thresholdStr);
        if (threshold < 0.0 || threshold > 1.0) {
          threshold = 0.5;
        }
      } catch (...) {
        threshold = 0.5;
      }
    }

    std::string limitStr = req->getParameter("limit");
    int limit = 0; // 0 = no limit
    if (!limitStr.empty()) {
      try {
        limit = std::stoi(limitStr);
        if (limit < 0)
          limit = 0;
      } catch (...) {
        limit = 0;
      }
    }

    std::string detProbThresholdStr = req->getParameter("det_prob_threshold");
    double detProbThreshold = 0.5;
    if (!detProbThresholdStr.empty()) {
      try {
        detProbThreshold = std::stod(detProbThresholdStr);
      } catch (...) {
        detProbThreshold = 0.5;
      }
    }

    if (isApiLoggingEnabled()) {
      PLOG_DEBUG << "[API] Search parameters - threshold: " << threshold
                 << ", limit: " << limit
                 << ", det_prob_threshold: " << detProbThreshold;
    }

    // Extract image from request
    std::vector<unsigned char> imageData;
    std::string imageError;
    if (!extractImageFromRequest(req, imageData, imageError)) {
      if (isApiLoggingEnabled()) {
        PLOG_WARNING << "[API] POST /v1/recognition/search - " << imageError;
      }
      callback(createErrorResponse(400, "Invalid request", imageError));
      return;
    }

    // Get database and extract embedding from input image
    FaceDatabase &db = get_database();

    // Decode image
    cv::Mat image = cv::imdecode(imageData, cv::IMREAD_COLOR);
    if (image.empty()) {
      callback(createErrorResponse(400, "Invalid request",
                                   "Failed to decode image"));
      return;
    }

    // Detect faces using YuNet
    std::string detector_path = db.get_detector_model_path();
    if (detector_path.empty()) {
      callback(createErrorResponse(500, "Internal server error",
                                   "Face detector model not found"));
      return;
    }

    cv::Ptr<cv::FaceDetectorYN> detector = cv::FaceDetectorYN::create(
        detector_path, "", cv::Size(image.cols, image.rows),
        static_cast<float>(detProbThreshold), 0.3f, 5000);

    cv::Mat faces;
    detector->detect(image, faces);

    if (faces.rows == 0) {
      Json::Value response;
      response["result"] = Json::arrayValue;
      response["message"] = "No faces detected in the input image";

      auto resp = HttpResponse::newHttpJsonResponse(response);
      resp->setStatusCode(k200OK);
      resp->addHeader("Access-Control-Allow-Origin", "*");
      resp->addHeader("Access-Control-Allow-Methods", "POST, OPTIONS");
      resp->addHeader("Access-Control-Allow-Headers",
                      "Content-Type, x-api-key");
      MetricsInterceptor::callWithMetrics(req, resp, std::move(callback));
      return;
    }

    // Get aligned face and extract embedding (use lightweight augmentation:
    // original + horizontal flip) to make query embeddings more robust.
    cv::Mat aligned_face = align_face_using_landmarks(image, faces, 0);
    std::string onnx_path = db.get_onnx_model_path();
    if (onnx_path.empty()) {
      callback(createErrorResponse(500, "Internal server error",
                                   "Recognition model not found"));
      return;
    }

    std::vector<std::vector<float>> input_embeddings;
    std::vector<float> emb1 =
        extract_embedding_from_image(aligned_face, onnx_path);
    if (!emb1.empty())
      input_embeddings.push_back(emb1);

    cv::Mat flipped;
    cv::flip(aligned_face, flipped, 1);
    std::vector<float> emb2 = extract_embedding_from_image(flipped, onnx_path);
    if (!emb2.empty())
      input_embeddings.push_back(emb2);

    std::vector<float> input_embedding = average_embeddings(input_embeddings);
    if (input_embedding.empty()) {
      callback(createErrorResponse(500, "Internal server error",
                                   "Failed to extract face embedding"));
      return;
    }

    // Load embeddings from database or file
    std::map<std::string, std::vector<float>> database;
    std::map<std::string, std::string> base64Images;
    std::map<std::string, std::vector<std::string>> subjectImageIds;

    FaceDatabaseHelper &dbHelper = get_db_helper();
    if (dbHelper.isEnabled()) {
      // Load from database with details
      std::string dbError;
      if (!dbHelper.loadAllFacesWithDetails(database, base64Images,
                                            subjectImageIds, dbError)) {
        if (isApiLoggingEnabled()) {
          PLOG_WARNING
              << "[RecognitionHandler] Failed to load faces from database: "
              << dbError << ", falling back to file";
        }
        // Fallback to file
        database = db.get_database();
      }
    } else {
      // Use file-based storage
      database = db.get_database();
    }

    // Compare with all faces in database
    std::vector<std::tuple<std::string, std::string, double, std::string>>
        matches; // image_id, subject_name, similarity, face_image_base64

    for (const auto &[subject_name, embedding] : database) {
      float similarity = cosine_similarity(input_embedding, embedding);

      if (similarity >= threshold) {
        // Get image_id and face_image for this subject
        std::string imageId;
        std::string faceImageBase64;

        if (dbHelper.isEnabled()) {
          // Get from database-loaded data
          auto imgIdsIt = subjectImageIds.find(subject_name);
          if (imgIdsIt != subjectImageIds.end() && !imgIdsIt->second.empty()) {
            imageId = imgIdsIt->second[0]; // Get first image_id
          }
          auto imgIt = base64Images.find(subject_name);
          if (imgIt != base64Images.end()) {
            faceImageBase64 = imgIt->second;
          }
        } else {
          // Get from in-memory storage
          std::lock_guard<std::mutex> lock(storage_mutex_);
          auto it = face_subjects_storage_.find(subject_name);
          if (it != face_subjects_storage_.end() && !it->second.empty()) {
            imageId = it->second[0]; // Get first image_id
          }
          auto imgIt = face_images_storage_.find(subject_name);
          if (imgIt != face_images_storage_.end()) {
            faceImageBase64 = imgIt->second;
          }
        }

        if (imageId.empty()) {
          imageId = generateImageIdForSubject(subject_name);
        }

        matches.push_back({imageId, subject_name,
                           static_cast<double>(similarity), faceImageBase64});
      }
    }

    // Sort by similarity (highest first)
    std::sort(matches.begin(), matches.end(), [](const auto &a, const auto &b) {
      return std::get<2>(a) > std::get<2>(b);
    });

    // Apply limit if specified
    if (limit > 0 && matches.size() > static_cast<size_t>(limit)) {
      matches.resize(limit);
    }

    // Build response
    Json::Value results(Json::arrayValue);
    for (const auto &[image_id, subject_name, similarity, face_image] :
         matches) {
      Json::Value match;
      match["image_id"] = image_id;
      match["subject"] = subject_name;
      match["similarity"] = similarity;
      match["face_image"] = face_image;
      results.append(match);
    }

    Json::Value response;
    response["result"] = results;
    response["faces_found"] = static_cast<int>(matches.size());
    response["threshold"] = threshold;

    auto resp = HttpResponse::newHttpJsonResponse(response);
    resp->setStatusCode(k200OK);
    resp->addHeader("Access-Control-Allow-Origin", "*");
    resp->addHeader("Access-Control-Allow-Methods", "POST, OPTIONS");
    resp->addHeader("Access-Control-Allow-Headers", "Content-Type, x-api-key");

    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time);

    if (isApiLoggingEnabled()) {
      PLOG_INFO << "[API] POST /v1/recognition/search - Success: Found "
                << matches.size() << " matching face(s) - " << duration.count()
                << "ms";
    }

    // Record metrics and call callback
    MetricsInterceptor::callWithMetrics(req, resp, std::move(callback));

  } catch (const std::exception &e) {
    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time);
    if (isApiLoggingEnabled()) {
      PLOG_ERROR << "[API] POST /v1/recognition/search - Exception: "
                 << e.what() << " - " << duration.count() << "ms";
    }
    callback(createErrorResponse(500, "Internal server error", e.what()));
  } catch (...) {
    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time);
    if (isApiLoggingEnabled()) {
      PLOG_ERROR << "[API] POST /v1/recognition/search - Unknown exception - "
                 << duration.count() << "ms";
    }
    callback(createErrorResponse(500, "Internal server error",
                                 "Unknown error occurred"));
  }
}

void RecognitionHandler::deleteMultipleFaceSubjects(
    const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback) {
  // Set handler start time for accurate metrics
  MetricsInterceptor::setHandlerStartTime(req);

  auto start_time = std::chrono::steady_clock::now();

  if (isApiLoggingEnabled()) {
    PLOG_INFO << "[API] POST /v1/recognition/faces/delete - Delete multiple "
                 "face subjects";
    PLOG_DEBUG << "[API] Request from: " << req->getPeerAddr().toIpPort();
  }

  try {
    // Parse JSON body
    auto json = req->getJsonObject();
    if (!json) {
      if (isApiLoggingEnabled()) {
        PLOG_WARNING
            << "[API] POST /v1/recognition/faces/delete - Invalid JSON body";
      }
      callback(createErrorResponse(400, "Invalid request",
                                   "Request body must be valid JSON"));
      return;
    }

    // Validate that body is an array
    if (!json->isArray()) {
      if (isApiLoggingEnabled()) {
        PLOG_WARNING << "[API] POST /v1/recognition/faces/delete - Request "
                        "body must be an array of image IDs";
      }
      callback(
          createErrorResponse(400, "Invalid request",
                              "Request body must be an array of image IDs"));
      return;
    }

    // Process each image ID
    Json::Value deletedFaces(Json::arrayValue);
    std::set<std::string> subjectsToDelete;

    for (const auto &item : *json) {
      if (!item.isString()) {
        continue; // Skip invalid entries
      }

      std::string imageId = item.asString();
      if (imageId.empty()) {
        continue; // Skip empty IDs
      }

      std::string subjectName;
      if (deleteImageFromStorage(imageId, subjectName)) {
        Json::Value deletedFace;
        deletedFace["image_id"] = imageId;
        deletedFace["subject"] = subjectName;
        deletedFaces.append(deletedFace);

        // Check if this subject has no more images - mark for deletion from
        // database
        std::lock_guard<std::mutex> lock(storage_mutex_);
        if (face_subjects_storage_.find(subjectName) ==
            face_subjects_storage_.end()) {
          subjectsToDelete.insert(subjectName);
        }
      }
      // If not found, ignore it (as per spec: "If some IDs do not exist, they
      // will be ignored")
    }

    // Remove from database or file
    // Wrap in try-catch to handle database initialization errors gracefully
    try {
      FaceDatabaseHelper &dbHelper = get_db_helper();
      if (dbHelper.isEnabled()) {
        // Use database - delete each image_id from database
        std::string dbError;
        for (const auto &deletedFace : deletedFaces) {
          std::string imageId = deletedFace["image_id"].asString();
          if (!dbHelper.deleteFace(imageId, dbError)) {
            if (isApiLoggingEnabled()) {
              PLOG_WARNING
                  << "[API] POST /v1/recognition/faces/delete - Warning: "
                     "Failed to delete face from database: image_id="
                  << imageId << ", error: " << dbError;
            }
          }
        }
      } else {
        // Use file-based storage
        FaceDatabase &db = get_database();
        for (const auto &subject : subjectsToDelete) {
          db.remove_subject(subject);
        }
      }
    } catch (const std::exception &e) {
      // If database access fails, log but continue - we still return success
      // with the deleted faces we managed to remove from memory storage
      if (isApiLoggingEnabled()) {
        PLOG_WARNING << "[API] POST /v1/recognition/faces/delete - Warning: "
                        "Failed to update database/file: "
                     << e.what();
      }
    }

    // Build response
    Json::Value response;
    response["deleted"] = deletedFaces;

    auto resp = HttpResponse::newHttpJsonResponse(response);
    resp->setStatusCode(k200OK);

    // Add CORS headers
    resp->addHeader("Access-Control-Allow-Origin", "*");
    resp->addHeader("Access-Control-Allow-Methods", "POST, OPTIONS");
    resp->addHeader("Access-Control-Allow-Headers", "Content-Type, x-api-key");

    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time);

    int deletedCount = deletedFaces.size();
    if (isApiLoggingEnabled()) {
      PLOG_INFO << "[API] POST /v1/recognition/faces/delete - Success: Deleted "
                << deletedCount << " face subject(s) - " << duration.count()
                << "ms";
    }

    // Record metrics and call callback
    MetricsInterceptor::callWithMetrics(req, resp, std::move(callback));

  } catch (const std::exception &e) {
    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time);
    if (isApiLoggingEnabled()) {
      PLOG_ERROR << "[API] POST /v1/recognition/faces/delete - Exception: "
                 << e.what() << " - " << duration.count() << "ms";
    }
    callback(createErrorResponse(500, "Internal server error", e.what()));
  } catch (...) {
    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time);
    if (isApiLoggingEnabled()) {
      PLOG_ERROR
          << "[API] POST /v1/recognition/faces/delete - Unknown exception - "
          << duration.count() << "ms";
    }
    callback(createErrorResponse(500, "Internal server error",
                                 "Unknown error occurred"));
  }
}

void RecognitionHandler::deleteAllFaceSubjects(
    const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback) {
  // Set handler start time for accurate metrics
  MetricsInterceptor::setHandlerStartTime(req);

  auto start_time = std::chrono::steady_clock::now();

  if (isApiLoggingEnabled()) {
    PLOG_INFO
        << "[API] DELETE /v1/recognition/faces/all - Delete all face subjects";
    PLOG_DEBUG << "[API] Request from: " << req->getPeerAddr().toIpPort();
  }

  try {
    std::lock_guard<std::mutex> lock(storage_mutex_);

    int deletedCount = static_cast<int>(image_id_to_subject_.size());

    // Clear all storage
    image_id_to_subject_.clear();
    face_subjects_storage_.clear();

    // Clear from database or file
    FaceDatabaseHelper &dbHelper = get_db_helper();
    if (dbHelper.isEnabled()) {
      // Use database
      std::string dbError;
      if (!dbHelper.deleteAllFaces(dbError)) {
        if (isApiLoggingEnabled()) {
          PLOG_WARNING << "[API] Failed to delete all faces from database: "
                       << dbError;
        }
      }
    } else {
      // Use file-based storage
      FaceDatabase &db = get_database();
      db.clear_all();
    }

    // Build response
    Json::Value response;
    response["deleted_count"] = deletedCount;
    response["message"] = "All face subjects deleted successfully";

    auto resp = HttpResponse::newHttpJsonResponse(response);
    resp->setStatusCode(k200OK);

    // Add CORS headers
    resp->addHeader("Access-Control-Allow-Origin", "*");
    resp->addHeader("Access-Control-Allow-Methods", "DELETE, OPTIONS");
    resp->addHeader("Access-Control-Allow-Headers", "Content-Type, x-api-key");

    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time);

    if (isApiLoggingEnabled()) {
      PLOG_INFO << "[API] DELETE /v1/recognition/faces/all - Success: Deleted "
                << deletedCount << " face subject(s) - " << duration.count()
                << "ms";
    }

    // Record metrics and call callback
    MetricsInterceptor::callWithMetrics(req, resp, std::move(callback));

  } catch (const std::exception &e) {
    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time);
    if (isApiLoggingEnabled()) {
      PLOG_ERROR << "[API] DELETE /v1/recognition/faces/all - Exception: "
                 << e.what() << " - " << duration.count() << "ms";
    }
    callback(createErrorResponse(500, "Internal server error", e.what()));
  } catch (...) {
    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time);
    if (isApiLoggingEnabled()) {
      PLOG_ERROR
          << "[API] DELETE /v1/recognition/faces/all - Unknown exception - "
          << duration.count() << "ms";
    }
    callback(createErrorResponse(500, "Internal server error",
                                 "Unknown error occurred"));
  }
}

void RecognitionHandler::handleOptionsDeleteFaces(
    const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback) {
  // Set handler start time for accurate metrics
  MetricsInterceptor::setHandlerStartTime(req);

  auto resp = HttpResponse::newHttpResponse();
  resp->setStatusCode(k200OK);
  resp->addHeader("Access-Control-Allow-Origin", "*");
  resp->addHeader("Access-Control-Allow-Methods", "POST, OPTIONS");
  resp->addHeader("Access-Control-Allow-Headers", "Content-Type, x-api-key");
  resp->addHeader("Access-Control-Max-Age", "3600");

  // Record metrics and call callback
  MetricsInterceptor::callWithMetrics(req, resp, std::move(callback));
}

void RecognitionHandler::handleOptionsDeleteAll(
    const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback) {
  // Set handler start time for accurate metrics
  MetricsInterceptor::setHandlerStartTime(req);

  auto resp = HttpResponse::newHttpResponse();
  resp->setStatusCode(k200OK);
  resp->addHeader("Access-Control-Allow-Origin", "*");
  resp->addHeader("Access-Control-Allow-Methods", "DELETE, OPTIONS");
  resp->addHeader("Access-Control-Allow-Headers", "Content-Type, x-api-key");
  resp->addHeader("Access-Control-Max-Age", "3600");

  // Record metrics and call callback
  MetricsInterceptor::callWithMetrics(req, resp, std::move(callback));
}

void RecognitionHandler::configureFaceDatabaseConnection(
    const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback) {
  // Set handler start time for accurate metrics
  MetricsInterceptor::setHandlerStartTime(req);

  auto start_time = std::chrono::steady_clock::now();

  if (isApiLoggingEnabled()) {
    PLOG_INFO << "[API] POST /v1/recognition/face-database/connection - "
                 "Configure face database connection";
    PLOG_DEBUG << "[API] Request from: " << req->getPeerAddr().toIpPort();
  }

  try {
    // Parse JSON request body
    Json::Value requestJson;
    auto bodyView = req->getBody();
    if (bodyView.empty()) {
      callback(
          createErrorResponse(400, "Bad request", "Request body is required"));
      return;
    }
    std::string requestBody(bodyView.data(), bodyView.size());

    Json::CharReaderBuilder builder;
    std::unique_ptr<Json::CharReader> reader(builder.newCharReader());
    std::string errors;
    if (!reader->parse(requestBody.c_str(),
                       requestBody.c_str() + requestBody.length(), &requestJson,
                       &errors)) {
      if (isApiLoggingEnabled()) {
        PLOG_WARNING << "[API] POST /v1/recognition/face-database/connection - "
                        "Invalid JSON: "
                     << errors;
      }
      callback(
          createErrorResponse(400, "Bad request", "Invalid JSON: " + errors));
      return;
    }

    // Check if user wants to disable database connection
    if (requestJson.isMember("enabled") && requestJson["enabled"].isBool() &&
        !requestJson["enabled"].asBool()) {
      // Disable database connection, use default file
      auto &systemConfig = SystemConfig::getInstance();
      Json::Value faceDbConfig(Json::objectValue);
      faceDbConfig["enabled"] = false;

      if (!systemConfig.updateConfigSection("face_database", faceDbConfig)) {
        callback(createErrorResponse(500, "Internal server error",
                                     "Failed to save configuration"));
        return;
      }

      if (!systemConfig.saveConfig()) {
        callback(createErrorResponse(500, "Internal server error",
                                     "Failed to persist configuration"));
        return;
      }

      Json::Value response(Json::objectValue);
      response["message"] = "Database connection disabled. Using default "
                            "face_database.txt file";
      response["enabled"] = false;
      response["default_file"] = resolveDatabasePath();

      auto resp = HttpResponse::newHttpJsonResponse(response);
      resp->setStatusCode(k200OK);
      resp->addHeader("Access-Control-Allow-Origin", "*");
      resp->addHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
      resp->addHeader("Access-Control-Allow-Headers",
                      "Content-Type, x-api-key");

      if (isApiLoggingEnabled()) {
        PLOG_INFO << "[API] POST /v1/recognition/face-database/connection - "
                     "Database connection disabled";
      }

      MetricsInterceptor::callWithMetrics(req, resp, std::move(callback));
      return;
    }

    // Validate required fields for enabling database connection
    if (!requestJson.isMember("type") || !requestJson["type"].isString()) {
      callback(createErrorResponse(
          400, "Bad request", "Field 'type' (mysql/postgresql) is required"));
      return;
    }

    std::string dbType = requestJson["type"].asString();
    std::transform(dbType.begin(), dbType.end(), dbType.begin(), ::tolower);

    if (dbType != "mysql" && dbType != "postgresql") {
      callback(createErrorResponse(
          400, "Bad request",
          "Field 'type' must be either 'mysql' or 'postgresql'"));
      return;
    }

    // Validate required fields for database connection
    std::vector<std::string> requiredFields = {"host", "database", "username",
                                               "password"};
    for (const auto &field : requiredFields) {
      if (!requestJson.isMember(field) || !requestJson[field].isString()) {
        callback(createErrorResponse(400, "Bad request",
                                     "Field '" + field + "' is required"));
        return;
      }
    }

    // Build database configuration JSON
    Json::Value dbConfig(Json::objectValue);
    dbConfig["type"] = dbType;
    dbConfig["host"] = requestJson["host"].asString();
    dbConfig["database"] = requestJson["database"].asString();
    dbConfig["username"] = requestJson["username"].asString();
    dbConfig["password"] = requestJson["password"].asString();

    // Optional fields
    if (requestJson.isMember("port") && requestJson["port"].isInt()) {
      dbConfig["port"] = requestJson["port"].asInt();
    } else if (requestJson.isMember("port") && requestJson["port"].isString()) {
      // Try to parse port as string
      try {
        int port = std::stoi(requestJson["port"].asString());
        dbConfig["port"] = port;
      } catch (...) {
        // Use default port if parsing fails
        dbConfig["port"] = (dbType == "mysql") ? 3306 : 5432;
      }
    } else {
      // Default ports
      dbConfig["port"] = (dbType == "mysql") ? 3306 : 5432;
    }

    if (requestJson.isMember("charset") && requestJson["charset"].isString()) {
      dbConfig["charset"] = requestJson["charset"].asString();
    } else {
      dbConfig["charset"] = "utf8mb4";
    }

    // Store configuration in SystemConfig
    auto &systemConfig = SystemConfig::getInstance();
    Json::Value faceDbConfig(Json::objectValue);
    faceDbConfig["connection"] = dbConfig;
    faceDbConfig["enabled"] = true;

    // Update config section
    if (!systemConfig.updateConfigSection("face_database", faceDbConfig)) {
      if (isApiLoggingEnabled()) {
        PLOG_ERROR << "[API] POST /v1/recognition/face-database/connection - "
                      "Failed to save configuration";
      }
      callback(createErrorResponse(500, "Internal server error",
                                   "Failed to save database configuration"));
      return;
    }

    // Save config to file
    if (!systemConfig.saveConfig()) {
      if (isApiLoggingEnabled()) {
        PLOG_ERROR << "[API] POST /v1/recognition/face-database/connection - "
                      "Failed to persist configuration to file";
      }
      callback(createErrorResponse(500, "Internal server error",
                                   "Failed to persist configuration"));
      return;
    }

    // Build success response
    Json::Value response(Json::objectValue);
    response["message"] = "Face database connection configured successfully";
    response["config"] = dbConfig;
    response["note"] =
        "Database connection will be used instead of face_database.txt file";

    auto resp = HttpResponse::newHttpJsonResponse(response);
    resp->setStatusCode(k200OK);

    // Add CORS headers
    resp->addHeader("Access-Control-Allow-Origin", "*");
    resp->addHeader("Access-Control-Allow-Methods", "POST, OPTIONS");
    resp->addHeader("Access-Control-Allow-Headers", "Content-Type, x-api-key");

    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time);

    if (isApiLoggingEnabled()) {
      PLOG_INFO << "[API] POST /v1/recognition/face-database/connection - "
                   "Success - "
                << duration.count() << "ms";
    }

    // Record metrics and call callback
    MetricsInterceptor::callWithMetrics(req, resp, std::move(callback));

  } catch (const std::exception &e) {
    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time);
    if (isApiLoggingEnabled()) {
      PLOG_ERROR << "[API] POST /v1/recognition/face-database/connection - "
                    "Exception: "
                 << e.what() << " - " << duration.count() << "ms";
    }
    callback(createErrorResponse(500, "Internal server error", e.what()));
  } catch (...) {
    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time);
    if (isApiLoggingEnabled()) {
      PLOG_ERROR << "[API] POST /v1/recognition/face-database/connection - "
                    "Unknown exception - "
                 << duration.count() << "ms";
    }
    callback(createErrorResponse(500, "Internal server error",
                                 "Unknown error occurred"));
  }
}

void RecognitionHandler::getFaceDatabaseConnection(
    const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback) {
  // Set handler start time for accurate metrics
  MetricsInterceptor::setHandlerStartTime(req);

  auto start_time = std::chrono::steady_clock::now();

  if (isApiLoggingEnabled()) {
    PLOG_INFO << "[API] GET /v1/recognition/face-database/connection - Get "
                 "face database connection configuration";
    PLOG_DEBUG << "[API] Request from: " << req->getPeerAddr().toIpPort();
  }

  try {
    auto &systemConfig = SystemConfig::getInstance();
    Json::Value faceDbConfig = systemConfig.getConfigSection("face_database");

    Json::Value response(Json::objectValue);

    if (faceDbConfig.isNull() || !faceDbConfig.isMember("enabled") ||
        !faceDbConfig["enabled"].asBool()) {
      // No database configured, using default file
      response["enabled"] = false;
      response["message"] = "No database connection configured. Using default "
                            "face_database.txt file";
      response["default_file"] = resolveDatabasePath();
    } else {
      // Database configured
      response["enabled"] = true;
      if (faceDbConfig.isMember("connection")) {
        response["config"] = faceDbConfig["connection"];
      }
      response["message"] = "Database connection is configured and enabled";
    }

    auto resp = HttpResponse::newHttpJsonResponse(response);
    resp->setStatusCode(k200OK);

    // Add CORS headers
    resp->addHeader("Access-Control-Allow-Origin", "*");
    resp->addHeader("Access-Control-Allow-Methods",
                    "GET, POST, DELETE, OPTIONS");
    resp->addHeader("Access-Control-Allow-Headers", "Content-Type, x-api-key");

    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time);

    if (isApiLoggingEnabled()) {
      PLOG_INFO << "[API] GET /v1/recognition/face-database/connection - "
                   "Success - "
                << duration.count() << "ms";
    }

    // Record metrics and call callback
    MetricsInterceptor::callWithMetrics(req, resp, std::move(callback));

  } catch (const std::exception &e) {
    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time);
    if (isApiLoggingEnabled()) {
      PLOG_ERROR << "[API] GET /v1/recognition/face-database/connection - "
                    "Exception: "
                 << e.what() << " - " << duration.count() << "ms";
    }
    callback(createErrorResponse(500, "Internal server error", e.what()));
  } catch (...) {
    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time);
    if (isApiLoggingEnabled()) {
      PLOG_ERROR << "[API] GET /v1/recognition/face-database/connection - "
                    "Unknown exception - "
                 << duration.count() << "ms";
    }
    callback(createErrorResponse(500, "Internal server error",
                                 "Unknown error occurred"));
  }
}

void RecognitionHandler::deleteFaceDatabaseConnection(
    const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback) {
  // Set handler start time for accurate metrics
  MetricsInterceptor::setHandlerStartTime(req);

  auto start_time = std::chrono::steady_clock::now();

  if (isApiLoggingEnabled()) {
    PLOG_INFO
        << "[API] DELETE /v1/recognition/face-database/connection - Delete "
           "face database connection configuration";
    PLOG_DEBUG << "[API] Request from: " << req->getPeerAddr().toIpPort();
  }

  try {
    auto &systemConfig = SystemConfig::getInstance();
    Json::Value faceDbConfig = systemConfig.getConfigSection("face_database");

    // Check if database connection is configured
    if (faceDbConfig.isNull() || !faceDbConfig.isMember("enabled") ||
        !faceDbConfig["enabled"].asBool()) {
      // No database configured
      Json::Value response(Json::objectValue);
      response["message"] = "No database connection configured to delete";
      response["enabled"] = false;
      response["default_file"] = resolveDatabasePath();

      auto resp = HttpResponse::newHttpJsonResponse(response);
      resp->setStatusCode(k200OK);
      resp->addHeader("Access-Control-Allow-Origin", "*");
      resp->addHeader("Access-Control-Allow-Methods", "DELETE, OPTIONS");
      resp->addHeader("Access-Control-Allow-Headers",
                      "Content-Type, x-api-key");

      if (isApiLoggingEnabled()) {
        PLOG_INFO << "[API] DELETE /v1/recognition/face-database/connection - "
                     "No configuration to delete";
      }

      MetricsInterceptor::callWithMetrics(req, resp, std::move(callback));
      return;
    }

    // Delete database configuration by setting enabled to false
    Json::Value newConfig(Json::objectValue);
    newConfig["enabled"] = false;

    if (!systemConfig.updateConfigSection("face_database", newConfig)) {
      callback(createErrorResponse(500, "Internal server error",
                                   "Failed to update configuration"));
      return;
    }

    if (!systemConfig.saveConfig()) {
      callback(createErrorResponse(500, "Internal server error",
                                   "Failed to persist configuration"));
      return;
    }

    // Reload database helper config
    get_db_helper().reloadConfig();

    Json::Value response(Json::objectValue);
    response["message"] =
        "Database connection configuration deleted successfully. "
        "System will now use default face_database.txt file";
    response["enabled"] = false;
    response["default_file"] = resolveDatabasePath();

    auto resp = HttpResponse::newHttpJsonResponse(response);
    resp->setStatusCode(k200OK);

    // Add CORS headers
    resp->addHeader("Access-Control-Allow-Origin", "*");
    resp->addHeader("Access-Control-Allow-Methods", "DELETE, OPTIONS");
    resp->addHeader("Access-Control-Allow-Headers", "Content-Type, x-api-key");

    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time);

    if (isApiLoggingEnabled()) {
      PLOG_INFO << "[API] DELETE /v1/recognition/face-database/connection - "
                   "Success - "
                << duration.count() << "ms";
    }

    // Record metrics and call callback
    MetricsInterceptor::callWithMetrics(req, resp, std::move(callback));

  } catch (const std::exception &e) {
    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time);
    if (isApiLoggingEnabled()) {
      PLOG_ERROR << "[API] DELETE /v1/recognition/face-database/connection - "
                    "Exception: "
                 << e.what() << " - " << duration.count() << "ms";
    }
    callback(createErrorResponse(500, "Internal server error", e.what()));
  } catch (...) {
    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time);
    if (isApiLoggingEnabled()) {
      PLOG_ERROR << "[API] DELETE /v1/recognition/face-database/connection - "
                    "Unknown exception - "
                 << duration.count() << "ms";
    }
    callback(createErrorResponse(500, "Internal server error",
                                 "Unknown error occurred"));
  }
}

void RecognitionHandler::handleOptionsFaceDatabaseConnection(
    const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback) {
  // Set handler start time for accurate metrics
  MetricsInterceptor::setHandlerStartTime(req);

  auto resp = HttpResponse::newHttpResponse();
  resp->setStatusCode(k200OK);
  resp->addHeader("Access-Control-Allow-Origin", "*");
  resp->addHeader("Access-Control-Allow-Methods", "GET, POST, DELETE, OPTIONS");
  resp->addHeader("Access-Control-Allow-Headers", "Content-Type, x-api-key");
  resp->addHeader("Access-Control-Max-Age", "3600");

  // Record metrics and call callback
  MetricsInterceptor::callWithMetrics(req, resp, std::move(callback));
}
