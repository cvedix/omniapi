# Face Detection - ONNX Models

Thư mục này chứa các ví dụ sử dụng Face Detection với các model ONNX.

## Model Types

- **YuNet Face Detection** (`.onnx`) - Model phát hiện khuôn mặt

## Các Ví dụ

### Input Sources
- `test_file_source.json` - Sử dụng file video làm nguồn input
- `test_rtsp_source.json` - Sử dụng RTSP stream làm nguồn input

### Output Types
- `test_mqtt_events.json` - Output qua MQTT broker
- `test_rtmp_output.json` - Output qua RTMP stream
- `example_face_detection_rtmp.json` - Ví dụ đầy đủ với RTMP output

## Tham số Model

- `MODEL_PATH`: Đường dẫn đến model YuNet face detection (`.onnx`)
- `RESIZE_RATIO`: Tỷ lệ resize frame (0.0 - 1.0)
