# Examples Instances

Thư mục này chứa các ví dụ cấu hình instances cho Edge AI API.

## Cấu trúc Tổ chức

Cấu trúc được tổ chức theo **solution** ở cấp ngoài, và theo **model type** ở cấp trong:

```
examples/instances/
├── {solution}/
│   └── {model_type}/
│       ├── *.json (example files)
│       └── README.md
```

### Solutions (Cấp ngoài)

- **face_detection/** - Face detection solutions
- **ba_crossline/** - Behavior analysis crossline solutions
- **ba_jam/** - Behavior analysis jam (vehicle stopped) solutions
- **ba_stop/** - Behavior analysis stop line solutions
- **mask_rcnn/** - Mask R-CNN segmentation solutions
- **fire_smoke_detection/** - Fire/smoke detection (YOLO)
- **obstacle_detection/** - Obstacle detection on road (YOLO)
- **wrong_way_detection/** - Wrong-way vehicle detection (YOLO + tracking)
- **infer_nodes/** - Các inference nodes riêng lẻ
- **other_solutions/** - Các solutions khác

### Model Types (Cấp trong)

- **onnx/** - ONNX models (`.onnx`)
- **tensorrt/** - TensorRT models (`.engine`, `.trt`)
- **rknn/** - RKNN models (`.rknn`)
- **yolo/** - YOLO models (`.weights`, `.cfg`)
- **tensorflow/** - TensorFlow models (`.pb`, `.pbtxt`)
- **caffe/** - Caffe models (`.caffemodel`, `.prototxt`)
- **other/** - Các model types khác hoặc source/broker nodes

## Ví dụ Cấu trúc

### Face Detection với ONNX
```
face_detection/
└── onnx/
    ├── test_file_source.json
    ├── test_rtsp_source.json
    ├── test_mqtt_events.json
    └── README.md
```

### Behavior Analysis với YOLO
```
ba_crossline/
└── yolo/
    ├── example_ba_crossline_file_mqtt_only.json
    ├── example_ba_crossline_rtmp_mqtt.json
    └── README.md
```

### Mask R-CNN với TensorFlow
```
mask_rcnn/
└── tensorflow/
    ├── test_file_source.json
    ├── example_mask_rcnn_rtmp.json
    └── README.md
```

### Fire/Smoke, Obstacle, Wrong-Way Detection (YOLO)
```
fire_smoke_detection/   obstacle_detection/   wrong_way_detection/
└── yolo/               └── yolo/              └── yolo/
    ├── example_*.json      ├── example_*.json     ├── example_*.json
    └── README.md           └── README.md          └── README.md
```

### Inference Nodes theo Model Type
```
infer_nodes/
├── tensorrt/
│   ├── example_trt_yolov8_detector.json
│   ├── example_trt_vehicle_detector.json
│   └── README.md
├── rknn/
│   ├── example_rknn_face_detector.json
│   └── README.md
├── yolo/
│   ├── example_yolo_detector.json
│   └── README.md
└── ...
```

## Cách Sử dụng

1. Chọn solution phù hợp với use case của bạn
2. Chọn model type dựa trên model format bạn có
3. Xem các ví dụ trong thư mục tương ứng
4. Điều chỉnh các tham số trong JSON file cho phù hợp với môi trường của bạn

## Model Type Reference

### ONNX
- Format: `.onnx`
- Use cases: Face detection, YOLOv11, InsightFace, Face swap
- Requirements: ONNX Runtime hoặc OpenCV DNN

### TensorRT
- Format: `.engine`, `.trt`
- Use cases: YOLOv8, Vehicle detection, InsightFace (optimized)
- Requirements: NVIDIA GPU, TensorRT library, CVEDIX_WITH_TRT

### RKNN
- Format: `.rknn`
- Use cases: YOLOv8, YOLOv11, Face detection (Rockchip NPU)
- Requirements: Rockchip NPU, RKNN toolkit, CVEDIX_WITH_RKNN

### YOLO
- Format: `.weights`, `.cfg`
- Use cases: Object detection, Behavior analysis
- Requirements: OpenCV DNN

### TensorFlow
- Format: `.pb`, `.pbtxt`
- Use cases: Mask R-CNN, Instance segmentation
- Requirements: TensorFlow library

### Caffe
- Format: `.caffemodel`, `.prototxt`
- Use cases: OpenPose, Pose estimation
- Requirements: OpenCV DNN với Caffe support

## Behavior Analysis Solutions

### Behavior Analysis Jam (`ba_jam/`)

**Solutions:**
- `ba_jam`: Phát hiện "jam" (vehicle stopped) trong các zone định nghĩa

**Tính năng:**
- Phát hiện phương tiện dừng lâu trong zone
- Tracking với SORT
- MQTT events khi phát hiện jam
- RTMP streaming (tùy chọn)

**Xem:** [ba_jam/README.md](./ba_jam/README.md)

### Behavior Analysis Stop Line (`ba_stop/`)

**Solutions:**
- `ba_stop`: Phát hiện dừng (stop) tại các stop-line định nghĩa

**Tính năng:**
- Phát hiện phương tiện dừng tại stop-line
- Tracking với SORT
- MQTT events khi phát hiện stop
- RTMP streaming (tùy chọn)

**Xem:** [ba_stop/README.md](./ba_stop/README.md)

### Object Detection (fire_smoke_detection, obstacle_detection, wrong_way_detection)

**Solutions:**
- `fire_smoke_detection`: Phát hiện lửa/khói (YOLO)
- `obstacle_detection`: Phát hiện chướng ngại vật trên đường (YOLO)
- `wrong_way_detection`: Phát hiện xe đi ngược chiều (YOLO + SORT tracking)

**Ví dụ:** [fire_smoke_detection/yolo/](./fire_smoke_detection/yolo/), [obstacle_detection/yolo/](./obstacle_detection/yolo/), [wrong_way_detection/yolo/](./wrong_way_detection/yolo/)

## RTMP/MQTT Integration

### RTMP Streaming

**Cấu hình RTMP Output:**
```json
{
  "output": {
    "RTMP_URL": "rtmp://server:1935/live/stream_key",
    "ENABLE_SCREEN_DES": "false"
  }
}
```

### MQTT Event Publishing

**Cấu hình MQTT:**
```json
{
  "output": {
    "MQTT_BROKER_URL": "localhost",
    "MQTT_PORT": "1883",
    "MQTT_TOPIC": "events",
    "MQTT_USERNAME": "",
    "MQTT_PASSWORD": "",
    "MQTT_RATE_LIMIT_MS": "1000",
    "BROKE_FOR": "FACE"  // hoặc "NORMAL"
  }
}
```

**Ví dụ tích hợp:**
- Face Detection + RTMP + MQTT: `face_detection/onnx/test_mqtt_events.json`, `face_detection/onnx/test_rtmp_output.json`
- BA Crossline + RTMP + MQTT: `ba_crossline/yolo/example_ba_crossline_rtsp_rtmp_mqtt.json`

**Tài liệu tham khảo:**
- RTMP setup: `sample/SELECTED_SAMPLES_RTMP_MQTT.md`
- MQTT transformer: `sample/README_MQTT_JSON_TRANSFORMER.md`
- Sample code: `sample/simple_rtmp_mqtt_sample.cpp`

## Thư mục Khác

- **update/** - Ví dụ cập nhật instances
- **tests/** - Test cases
