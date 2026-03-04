#include "core/ai_runtime_facade.h"
#include "core/frame_decoder.h"
#include <chrono>
#include <json/json.h>
#include <sstream>

namespace core {

namespace {

std::string detectCodec(const std::vector<uint8_t> &data) {
  if (data.size() < 4)
    return "";
  if (data.size() >= 3 && data[0] == 0xFF && data[1] == 0xD8 && data[2] == 0xFF)
    return "jpeg";
  if (data.size() >= 4 && data[0] == 0x89 && data[1] == 0x50 &&
      data[2] == 0x4E && data[3] == 0x47)
    return "png";
  if (data.size() >= 4 && data[0] == 0x52 && data[1] == 0x49 &&
      data[2] == 0x46 && data[3] == 0x46)
    return "webp";
  return "";
}

std::string optionsToConfigString(const Json::Value &options) {
  Json::StreamWriterBuilder wb;
  wb["indentation"] = "";
  return Json::writeString(wb, options);
}

}  // namespace

AIRuntimeFacade::AIRuntimeFacade()
    : session_(std::make_unique<InferenceSession>()) {}

AIRuntimeFacade::~AIRuntimeFacade() = default;

bool AIRuntimeFacade::decodePayload(const AIRuntimeRequest &request,
                                    cv::Mat &frame, std::string &codec_used,
                                    uint64_t &decode_ms) {
  auto t0 = std::chrono::steady_clock::now();
  const std::vector<uint8_t> &data = request.payload;
  if (data.empty()) {
    return false;
  }
  std::string codec = request.codec;
  if (codec.empty()) {
    codec = detectCodec(data);
  }
  if (codec.empty()) {
    codec = "jpeg";
  }
  codec_used = codec;

  FrameDecoder decoder;
  bool ok = decoder.decodeCompressedFrame(data, frame);
  decode_ms = static_cast<uint64_t>(
      std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::steady_clock::now() - t0)
          .count());
  return ok && !frame.empty();
}

InferenceSession &AIRuntimeFacade::getSessionForModel(
    const std::string &model_key, const Json::Value &options) {
  if (model_key != "face") {
    return *session_;
  }
  std::string detector_path =
      options.isMember("detector_path") ? options["detector_path"].asString()
                                        : "";
  std::string recognizer_path =
      options.isMember("recognizer_path") ? options["recognizer_path"].asString()
                                          : "";
  if (detector_path != last_detector_path_ ||
      recognizer_path != last_recognizer_path_) {
    if (!detector_path.empty()) {
      session_->load(detector_path, recognizer_path);
      last_detector_path_ = detector_path;
      last_recognizer_path_ = recognizer_path;
    }
  }
  return *session_;
}

AIRuntimeResponse AIRuntimeFacade::request(const AIRuntimeRequest &request) {
  AIRuntimeResponse response;

  cv::Mat frame;
  std::string codec_used;
  uint64_t decode_ms = 0;
  if (!decodePayload(request, frame, codec_used, decode_ms)) {
    response.success = false;
    response.error = "Failed to decode payload";
    response.decode_ms = decode_ms;
    return response;
  }
  response.decode_ms = decode_ms;

  std::string cache_key;
  if (cache_enabled_) {
    if (!cache_)
      cache_ = std::make_unique<AICache>(500, std::chrono::seconds(60));
  }
  if (cache_enabled_ && cache_) {
    std::string config_str = optionsToConfigString(request.options);
    cache_key = AICache::generateKey(
        std::string(request.payload.begin(), request.payload.end()),
        request.model_key + "|" + config_str);
    auto cached = cache_->get(cache_key);
    if (cached && !cached->empty()) {
      Json::CharReaderBuilder rb;
      std::istringstream ss(*cached);
      std::string errs;
      if (Json::parseFromStream(rb, ss, &response.result, &errs)) {
        response.success = true;
        response.from_cache = true;
        response.inference_ms = 0;
        return response;
      }
    }
  }

  InferenceSession &session =
      getSessionForModel(request.model_key, request.options);
  if (!session.isLoaded()) {
    response.success = false;
    response.error = "InferenceSession not loaded (missing detector path?)";
    return response;
  }

  InferenceInput input;
  input.frame = frame;
  input.model_id = request.model_key;
  input.options = request.options;
  if (!input.options.isMember("extract_embedding") &&
      request.model_key == "face") {
    input.options["extract_embedding"] = true;
  }

  InferenceResult ir = session.infer(input);
  response.inference_ms = ir.latency_ms;
  if (!ir.success) {
    response.success = false;
    response.error = ir.error;
    return response;
  }
  response.success = true;
  response.result = ir.data;

  if (cache_enabled_ && cache_ && !response.result.isNull()) {
    Json::StreamWriterBuilder wb;
    wb["indentation"] = "";
    std::string value = Json::writeString(wb, response.result);
    cache_->put(cache_key, value);
  }
  return response;
}

}  // namespace core
