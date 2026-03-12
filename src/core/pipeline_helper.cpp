#include "core/pipeline_helper.h"

namespace core {

namespace {
InferenceSession &getSession() {
  static InferenceSession session;
  return session;
}
}  // namespace

void PipelineHelper::runFrameThroughDetector(
    const cv::Mat &frame,
    const std::string &detector_path,
    const std::string &recognizer_path,
    const Json::Value &options,
    ResultCallback callback) {
  if (callback == nullptr) {
    return;
  }
  if (frame.empty()) {
    callback(Json::Value());
    return;
  }
  if (detector_path.empty()) {
    callback(Json::Value());
    return;
  }

  InferenceSession &session = getSession();
  if (!session.isLoaded() || session.getDetectorPath() != detector_path ||
      session.getRecognizerPath() != recognizer_path) {
    if (!session.load(detector_path, recognizer_path)) {
      callback(Json::Value());
      return;
    }
  }

  InferenceInput input;
  input.frame = frame;
  input.model_id = "face";
  input.options = options;
  if (!input.options.isMember("extract_embedding")) {
    input.options["extract_embedding"] = !recognizer_path.empty();
  }

  InferenceResult ir = session.infer(input);
  if (!ir.success) {
    callback(Json::Value());
    return;
  }
  callback(ir.data);
}

PipelineHelper::ShortPipelineDescriptor
PipelineHelper::buildShortPipelineDescriptor(
    const std::string &source_type,
    const std::string &detector_path,
    const std::string &recognizer_path) {
  ShortPipelineDescriptor d;
  d.source_type = source_type;
  d.detector_key = "face";
  d.detector_path = detector_path;
  d.recognizer_path = recognizer_path;
  return d;
}

}  // namespace core
