# Other Solutions Examples

Thư mục này chứa các ví dụ cho các solutions khác, được tổ chức theo tên solution.

## Cấu Trúc

Các examples được tổ chức theo solution names:

### Solutions Có Tên Cụ Thể

- **`mllm_analysis/`** - Multi-modal Large Language Model analysis
  - `example_mllm_analysis.json`

- **`face_swap/`** - Face swap solution
  - `example_face_swap.json`

- **`insightface_recognition/`** - InsightFace recognition (ONNX)
  - `example_insightface_recognition.json`

- **`trt_insightface_recognition/`** - InsightFace recognition (TensorRT)
  - `example_trt_insightface_recognition.json`

- **`yolov11_detection/`** - YOLOv11 object detection (ONNX)
  - `example_yolov11_detection.json`

- **`rknn_yolov11_detection/`** - YOLOv11 detection (RKNN)
  - `example_rknn_yolov11_detection.json`

- **`securt/`** - SecuRT solution
  - `example_full_config.json`

### Custom Solutions

- **`custom/`** - Các examples với `solution: "custom"`
  - Destination nodes: `app_des`, `ff_des`, `image_des`, `rtsp_des`
  - Source nodes: `ff_src`
  - Processor nodes: `cluster`, `frame_fusion`, `record`, `split`, `sync`
  - TensorRT detectors: `trt_yolov11_face_detector`, `trt_yolov11_plate_detector`, `trt_vehicle_*`
  - ONNX detectors: `yolov11_plate_detector`

## Tổng Quan

Tất cả các examples trong thư mục này sử dụng các solutions không thuộc các category chính (face_detection, ba_crossline, etc.) hoặc là các custom solutions được tùy chỉnh.

## Sử Dụng

Mỗi thư mục solution có README.md riêng mô tả chi tiết về solution và các tham số cần thiết.

