# AI Runtime Design

Thiết kế lớp AI Runtime: một điểm tích hợp cho decode, inference và cache; Recognition và Push frame đều đi qua lớp này.

## Thành phần

### InferenceSession

- **Vai trò:** Load/unload model (face detector YuNet + recognizer ONNX), chạy infer.
- **API:** `load(detector_path, recognizer_path)`, `unload()`, `isLoaded()`, `infer(InferenceInput) → InferenceResult`.
- **Struct:** `InferenceInput` (frame, model_id, options), `InferenceResult` (success, error, data JSON, latency_ms).
- **File:** `include/core/inference_session.h`, `src/core/inference_session.cpp`.

### AIRuntimeFacade

- **Vai trò:** Request (encoded/compressed payload, codec, model_key) → decode → cache? → infer → response.
- **Response:** result (JSON), from_cache, decode_ms, inference_ms.
- **Dùng bởi:** RecognitionHandler (`processFaceRecognition` gọi `getFaceRuntimeFacade().request()`).
- **File:** `include/core/ai_runtime_facade.h`, `src/core/ai_runtime_facade.cpp`.

### PipelineHelper

- **Vai trò:** Pipeline ngắn frame → detector → callback; không dùng InstanceRegistry.
- **API:** `runFrameThroughDetector(frame, detector_path, recognizer_path, options, callback)`, `buildShortPipelineDescriptor()`.
- **File:** `include/core/pipeline_helper.h`, `src/core/pipeline_helper.cpp`.

### AIProcessor

- **Vai trò:** Quản lý xử lý AI trên thread riêng; `initializeSDK(config)` tạo InferenceSession, `processFrame()` gọi infer, `submitFrame(cv::Mat)` đẩy frame.
- **File:** `include/core/ai_processor.h`, `src/core/ai_processor.cpp`.

## Luồng Recognition

1. Request → RecognitionHandler.
2. Extract image (base64 / multipart) → payload.
3. `AIRuntimeFacade::request({ payload, codec, model_key: "face", options })` → decode → infer (InferenceSession) → response.
4. Map response.result["faces"] sang JSON trả về client (thêm subjects, execution_time).

## Config model path

- **Env:** `FACE_DETECTOR_PATH`, `FACE_RECOGNIZER_PATH` (ưu tiên nếu set).
- **Mặc định:** Tìm theo thứ tự `/opt/edgeos-api/models/face/`, `./models/face/`, `../models/face/`.

## Xem thêm

- [ARCHITECTURE.md](ARCHITECTURE.md) — Diagram tổng thể.
- [VISION_AI_PROCESSING_PLATFORM.md](VISION_AI_PROCESSING_PLATFORM.md) — Vision nền tảng.
- [task/edgeos-api/03_PHASE_AI_RUNTIME.md](../task/edgeos-api/03_PHASE_AI_RUNTIME.md) — Phase 3 task.
