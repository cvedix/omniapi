# Custom Solutions

Thư mục này chứa các ví dụ sử dụng solution "custom" - các pipeline được tùy chỉnh không thuộc solution cụ thể.

## Các Ví dụ

### Destination Nodes
- `example_app_des.json` - App Destination - Output frames via application callback
- `example_ff_des.json` - FFmpeg Destination - Output video stream using FFmpeg
- `example_image_des.json` - Image Destination - Save images to file or send via UDP
- `example_rtsp_des.json` - RTSP Destination - Stream video via RTSP server

### Source Nodes
- `example_ff_src.json` - FFmpeg Source - Read video stream using FFmpeg

### Processor Nodes
- `example_cluster.json` - Cluster - Vehicle clustering based on classify/encoding
- `example_frame_fusion.json` - Frame Fusion - Fuse frames from multiple channels
- `example_record.json` - Record - Record images and videos
- `example_split.json` - Split - Split frames by channel or deep copy
- `example_sync.json` - Sync - Synchronize frames from multiple channels

### TensorRT Detectors
- `example_trt_yolov11_face_detector.json` - TensorRT YOLOv11 Face Detector
- `example_trt_yolov11_plate_detector.json` - TensorRT YOLOv11 Plate Detector
- `example_trt_vehicle_feature_encoder.json` - TensorRT Vehicle Feature Encoder
- `example_trt_vehicle_color_classifier.json` - TensorRT Vehicle Color Classifier
- `example_trt_vehicle_type_classifier.json` - TensorRT Vehicle Type Classifier
- `example_trt_vehicle_scanner.json` - TensorRT Vehicle Scanner

### ONNX Detectors
- `example_yolov11_plate_detector.json` - YOLOv11 Plate Detector (ONNX)

## Tham số

Các examples này sử dụng `solution: "custom"` và có thể được tùy chỉnh theo nhu cầu cụ thể.

