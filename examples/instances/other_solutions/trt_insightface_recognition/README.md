# TensorRT InsightFace Recognition Solution

Solution nhận diện khuôn mặt sử dụng InsightFace với TensorRT optimization.

## Ví dụ

- `example_trt_insightface_recognition.json` - InsightFace recognition với TensorRT model

## Tham số

- `FACE_DETECTION_MODEL_PATH`: Đường dẫn đến face detection model (ONNX)
- `FACE_RECOGNITION_MODEL_PATH`: Đường dẫn đến InsightFace TensorRT model (`.trt`)

## Yêu cầu

- NVIDIA GPU với CUDA support
- TensorRT library đã được cài đặt
- CVEDIX_WITH_TRT flag được enable khi build

