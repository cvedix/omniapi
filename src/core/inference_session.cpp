#include "core/inference_session.h"
#include <opencv2/dnn.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/objdetect.hpp>
#include <chrono>
#include <cmath>
#include <iostream>
#include <mutex>

namespace core {

namespace {

constexpr float kLandmarkDst[5][2] = {
    {38.2946f, 51.6963f}, {73.5318f, 51.5014f}, {56.0252f, 71.7366f},
    {41.5493f, 92.3655f},  {70.7299f, 92.2041f}};

cv::Mat alignFaceUsingLandmarks(const cv::Mat &image, const cv::Mat &faces,
                                int face_idx) {
  if (faces.cols < 15)
    return cv::Mat();
  float re_x = faces.at<float>(face_idx, 4), re_y = faces.at<float>(face_idx, 5);
  float le_x = faces.at<float>(face_idx, 6), le_y = faces.at<float>(face_idx, 7);
  float nt_x = faces.at<float>(face_idx, 8), nt_y = faces.at<float>(face_idx, 9);
  float rcm_x = faces.at<float>(face_idx, 10),
        rcm_y = faces.at<float>(face_idx, 11);
  float lcm_x = faces.at<float>(face_idx, 12),
        lcm_y = faces.at<float>(face_idx, 13);

  float src[5][2] = {{re_x, re_y}, {le_x, le_y}, {nt_x, nt_y},
                     {rcm_x, rcm_y}, {lcm_x, lcm_y}};
  float src_mean[2] = {
      (src[0][0] + src[1][0] + src[2][0] + src[3][0] + src[4][0]) / 5.0f,
      (src[0][1] + src[1][1] + src[2][1] + src[3][1] + src[4][1]) / 5.0f};
  float dst_mean[2] = {56.0262f, 71.9008f};

  float src_demean[5][2], dst_demean[5][2];
  for (int i = 0; i < 5; i++) {
    src_demean[i][0] = src[i][0] - src_mean[0];
    src_demean[i][1] = src[i][1] - src_mean[1];
    dst_demean[i][0] = kLandmarkDst[i][0] - dst_mean[0];
    dst_demean[i][1] = kLandmarkDst[i][1] - dst_mean[1];
  }

  double A00 = 0, A01 = 0, A10 = 0, A11 = 0;
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
  double tol = smax * 2 * static_cast<double>(FLT_MIN);
  int rank = (s.at<double>(0) > tol) + (s.at<double>(1) > tol);

  if (rank == 0) {
    cv::Mat aligned;
    cv::resize(image, aligned, cv::Size(112, 112));
    return aligned;
  }

  cv::Mat T = u * cv::Mat::diag(cv::Mat(cv::Vec2d(d[0], d[1]))) * vt;

  double var1 = 0, var2 = 0;
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

std::vector<float> extractEmbedding(const cv::Mat &aligned_face,
                                    cv::dnn::Net &net) {
  std::vector<float> out;
  if (net.empty() || aligned_face.empty())
    return out;
  try {
    cv::Mat rgb;
    cv::cvtColor(aligned_face, rgb, cv::COLOR_BGR2RGB);
    if (rgb.rows != 112 || rgb.cols != 112)
      cv::resize(rgb, rgb, cv::Size(112, 112), 0, 0, cv::INTER_LINEAR);

    cv::Mat blob;
    cv::dnn::blobFromImage(rgb, blob, 1.0f / 128.0f, cv::Size(),
                           cv::Scalar(127.5f, 127.5f, 127.5f), false, false,
                           CV_32F);

    net.setInput(blob);
    std::vector<cv::Mat> outputs;
    net.forward(outputs, net.getUnconnectedOutLayersNames());

    if (outputs.empty())
      return out;

    const cv::Mat &output = outputs[0];
    int emb_dim = (output.dims == 2) ? output.size[1] : output.size[0];
    out.resize(emb_dim);
    const float *p = output.ptr<float>();
    std::copy(p, p + emb_dim, out.begin());

    float norm = 0.f;
    for (float v : out)
      norm += v * v;
    norm = std::sqrt(norm);
    if (norm > 1e-6f)
      for (float &v : out)
        v /= norm;

    return out;
  } catch (...) {
    return std::vector<float>();
  }
}

}  // namespace

struct InferenceSession::Impl {
  std::string detector_path;
  std::string recognizer_path;
  cv::Ptr<cv::FaceDetectorYN> detector;
  cv::dnn::Net recognizer_net;
  bool loaded{false};
  std::mutex mutex;
};

InferenceSession::InferenceSession() : impl_(std::make_unique<Impl>()) {}

InferenceSession::~InferenceSession() { unload(); }

bool InferenceSession::load(const std::string &detector_path,
                            const std::string &recognizer_path) {
  std::lock_guard<std::mutex> lock(impl_->mutex);
  unload();

  if (detector_path.empty()) {
    return false;
  }

  try {
    impl_->detector = cv::FaceDetectorYN::create(
        detector_path, "", cv::Size(320, 320), 0.5f, 0.3f, 5000,
        cv::dnn::DNN_BACKEND_OPENCV, cv::dnn::DNN_TARGET_CPU);
  } catch (const cv::Exception &e) {
    std::cerr << "[InferenceSession] Failed to load detector: " << e.msg
              << std::endl;
    return false;
  }

  if (impl_->detector.empty()) {
    return false;
  }

  impl_->detector_path = detector_path;
  impl_->recognizer_path = recognizer_path;

  if (!recognizer_path.empty()) {
    try {
      impl_->recognizer_net = cv::dnn::readNetFromONNX(recognizer_path);
#ifdef CVEDIX_WITH_CUDA
      impl_->recognizer_net.setPreferableBackend(cv::dnn::DNN_BACKEND_CUDA);
      impl_->recognizer_net.setPreferableTarget(cv::dnn::DNN_TARGET_CUDA);
#else
      impl_->recognizer_net.setPreferableBackend(cv::dnn::DNN_BACKEND_OPENCV);
      impl_->recognizer_net.setPreferableTarget(cv::dnn::DNN_TARGET_CPU);
#endif
    } catch (const cv::Exception &e) {
      std::cerr << "[InferenceSession] Failed to load recognizer ONNX: "
                << e.msg << std::endl;
    }
  }

  impl_->loaded = true;
  return true;
}

void InferenceSession::unload() {
  std::lock_guard<std::mutex> lock(impl_->mutex);
  impl_->detector.release();
  impl_->recognizer_net = cv::dnn::Net();
  impl_->detector_path.clear();
  impl_->recognizer_path.clear();
  impl_->loaded = false;
}

bool InferenceSession::isLoaded() const {
  std::lock_guard<std::mutex> lock(impl_->mutex);
  return impl_->loaded && !impl_->detector.empty();
}

const std::string &InferenceSession::getDetectorPath() const {
  std::lock_guard<std::mutex> lock(impl_->mutex);
  return impl_->detector_path;
}

const std::string &InferenceSession::getRecognizerPath() const {
  std::lock_guard<std::mutex> lock(impl_->mutex);
  return impl_->recognizer_path;
}

InferenceResult InferenceSession::infer(const InferenceInput &input) {
  InferenceResult result;
  auto t0 = std::chrono::steady_clock::now();

  std::lock_guard<std::mutex> lock(impl_->mutex);
  if (!impl_->loaded || impl_->detector.empty()) {
    result.success = false;
    result.error = "InferenceSession not loaded";
    result.latency_ms = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - t0)
            .count());
    return result;
  }

  if (input.frame.empty()) {
    result.success = false;
    result.error = "Empty frame";
    result.latency_ms = 0;
    return result;
  }

  if (input.model_id != "face") {
    result.success = false;
    result.error = "Unsupported model_id: " + input.model_id;
    result.latency_ms = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - t0)
            .count());
    return result;
  }

  double det_thresh = 0.5;
  if (input.options.isMember("det_prob_threshold") &&
      input.options["det_prob_threshold"].isDouble())
    det_thresh = input.options["det_prob_threshold"].asDouble();
  bool extract_embedding = true;
  if (input.options.isMember("extract_embedding") &&
      input.options["extract_embedding"].isBool())
    extract_embedding = input.options["extract_embedding"].asBool();

  try {
    impl_->detector->setInputSize(input.frame.size());
    impl_->detector->setScoreThreshold(static_cast<float>(det_thresh));

    cv::Mat faces;
    impl_->detector->detect(input.frame, faces);

    Json::Value faces_json(Json::arrayValue);
    int limit = input.options.isMember("limit") && input.options["limit"].isInt()
                    ? input.options["limit"].asInt()
                    : 0;
    int num_faces =
        (limit > 0) ? std::min(limit, faces.rows) : faces.rows;

    for (int i = 0; i < num_faces; i++) {
      Json::Value face_obj;
      float x = faces.at<float>(i, 0), y = faces.at<float>(i, 1);
      float w = faces.at<float>(i, 2), h = faces.at<float>(i, 3);
      float score = (faces.cols > 14) ? faces.at<float>(i, 14) : 1.0f;

      Json::Value box;
      box["probability"] = static_cast<double>(score);
      box["x_min"] = static_cast<int>(x);
      box["y_min"] = static_cast<int>(y);
      box["x_max"] = static_cast<int>(x + w);
      box["y_max"] = static_cast<int>(y + h);
      face_obj["box"] = box;

      Json::Value landmarks(Json::arrayValue);
      if (faces.cols >= 15) {
        for (int k = 0; k < 5; k++) {
          Json::Value pt(Json::arrayValue);
          pt.append(static_cast<int>(faces.at<float>(i, 4 + k * 2)));
          pt.append(static_cast<int>(faces.at<float>(i, 5 + k * 2)));
          landmarks.append(pt);
        }
      }
      face_obj["landmarks"] = landmarks;

      if (extract_embedding && !impl_->recognizer_net.empty()) {
        cv::Mat aligned;
        if (faces.cols >= 15)
          aligned = alignFaceUsingLandmarks(input.frame, faces, i);
        else {
          float xc = std::max(0.f, std::min(x, static_cast<float>(input.frame.cols - 1)));
          float yc = std::max(0.f, std::min(y, static_cast<float>(input.frame.rows - 1)));
          float wc = std::max(1.f, std::min(w, static_cast<float>(input.frame.cols - xc)));
          float hc = std::max(1.f, std::min(h, static_cast<float>(input.frame.rows - yc)));
          cv::Mat roi = input.frame(cv::Rect(static_cast<int>(xc), static_cast<int>(yc),
                                             static_cast<int>(wc), static_cast<int>(hc)))
                           .clone();
          cv::resize(roi, aligned, cv::Size(112, 112));
        }
        if (!aligned.empty()) {
          std::vector<float> emb =
              extractEmbedding(aligned, impl_->recognizer_net);
          if (!emb.empty()) {
            Json::Value emb_json(Json::arrayValue);
            for (float v : emb)
              emb_json.append(static_cast<double>(v));
            face_obj["embedding"] = emb_json;
          }
        }
      }

      faces_json.append(face_obj);
    }

    result.data["faces"] = faces_json;
    result.success = true;
  } catch (const cv::Exception &e) {
    result.success = false;
    result.error = std::string("OpenCV: ") + e.msg;
  } catch (const std::exception &e) {
    result.success = false;
    result.error = std::string("Exception: ") + e.what();
  } catch (...) {
    result.success = false;
    result.error = "Unknown exception";
  }

  result.latency_ms = static_cast<uint64_t>(
      std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::steady_clock::now() - t0)
          .count());
  return result;
}

}  // namespace core
