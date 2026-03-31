# Hướng dẫn Sử dụng Inference Nodes, Source Nodes và Broker Nodes với API

## Tổng quan

API hiện tại hỗ trợ:
- **Inference Nodes**: Tất cả các inference nodes từ CVEDIX SDK (TensorRT, RKNN, YOLO, etc.)
- **Source Nodes**: Các node nguồn input (RTSP, File, App, Image, RTMP, UDP)
- **Broker Nodes**: Các node xuất output (MQTT, Kafka, Socket, Console, XML)

Để sử dụng các nodes, bạn cần:

1. Tạo solution config với pipeline chứa các nodes mong muốn
2. Sử dụng solution ID khi tạo instance
3. Cung cấp các tham số cần thiết trong `additionalParams`

## Các Node Types Được Hỗ trợ

### TensorRT Inference Nodes

#### 1. TRT YOLOv8 Detector
- **Node Type**: `trt_yolov8_detector`
- **Parameters**:
  - `model_path`: Đường dẫn đến model file (.engine)
  - `labels_path`: Đường dẫn đến file labels (optional)
- **Example**: `example_trt_yolov8_detector.json`

#### 2. TRT YOLOv8 Segmentation Detector
- **Node Type**: `trt_yolov8_seg_detector`
- **Parameters**:
  - `model_path`: Đường dẫn đến model file (.engine)
  - `labels_path`: Đường dẫn đến file labels (optional)
- **Example**: `example_trt_yolov8_segmentation.json`

#### 3. TRT YOLOv8 Pose Detector
- **Node Type**: `trt_yolov8_pose_detector`
- **Parameters**:
  - `model_path`: Đường dẫn đến model file (.engine)
- **Example**: `example_trt_yolov8_pose.json`

#### 4. TRT YOLOv8 Classifier
- **Node Type**: `trt_yolov8_classifier`
- **Parameters**:
  - `model_path`: Đường dẫn đến model file (.engine)
  - `labels_path`: Đường dẫn đến file labels (optional)
  - `class_ids_applied_to`: Danh sách class IDs (comma-separated, optional)
  - `min_width_applied_to`: Minimum width (optional)
  - `min_height_applied_to`: Minimum height (optional)

#### 5. TRT Vehicle Detector
- **Node Type**: `trt_vehicle_detector`
- **Parameters**:
  - `model_path`: Đường dẫn đến model file (.trt)
- **Example**: `example_trt_vehicle_detector.json`

#### 6. TRT Vehicle Plate Detector
- **Node Type**: `trt_vehicle_plate_detector` hoặc `trt_vehicle_plate_detector_v2`
- **Parameters**:
  - `det_model_path`: Đường dẫn đến detection model
  - `rec_model_path`: Đường dẫn đến recognition model
- **Example**: `example_trt_vehicle_plate_detector.json`

#### 7. TRT Vehicle Color Classifier
- **Node Type**: `trt_vehicle_color_classifier`
- **Parameters**:
  - `model_path`: Đường dẫn đến model file
  - `class_ids_applied_to`: Danh sách class IDs (comma-separated, optional)
  - `min_width_applied_to`: Minimum width (optional)
  - `min_height_applied_to`: Minimum height (optional)

#### 8. TRT Vehicle Type Classifier
- **Node Type**: `trt_vehicle_type_classifier`
- **Parameters**: Tương tự như Vehicle Color Classifier

#### 9. TRT Vehicle Feature Encoder
- **Node Type**: `trt_vehicle_feature_encoder`
- **Parameters**: Tương tự như Vehicle Color Classifier

#### 10. TRT Vehicle Scanner
- **Node Type**: `trt_vehicle_scanner`
- **Parameters**:
  - `model_path`: Đường dẫn đến model file

### RKNN Inference Nodes

#### 1. RKNN YOLOv8 Detector
- **Node Type**: `rknn_yolov8_detector`
- **Parameters**:
  - `model_path`: Đường dẫn đến model file (.rknn)
  - `score_threshold`: Score threshold (default: 0.5)
  - `nms_threshold`: NMS threshold (default: 0.5)
  - `input_width`: Input width (default: 640)
  - `input_height`: Input height (default: 640)
  - `num_classes`: Number of classes (default: 80)

#### 2. RKNN Face Detector
- **Node Type**: `rknn_face_detector`
- **Parameters**:
  - `model_path`: Đường dẫn đến model file (.rknn)
  - `score_threshold`: Score threshold (default: 0.5)
  - `nms_threshold`: NMS threshold (default: 0.5)
  - `input_width`: Input width (default: 640)
  - `input_height`: Input height (default: 640)
  - `top_k`: Top K (default: 50)
- **Example**: `example_rknn_face_detector.json`

### Other Inference Nodes

#### 1. YOLO Detector (OpenCV DNN)
- **Node Type**: `yolo_detector`
- **Parameters**:
  - `model_path`: Đường dẫn đến weights file (.weights)
  - `model_config_path`: Đường dẫn đến config file (.cfg)
  - `labels_path`: Đường dẫn đến labels file
  - `input_width`: Input width (default: 416)
  - `input_height`: Input height (default: 416)
  - `score_threshold`: Score threshold (default: 0.5)
  - `confidence_threshold`: Confidence threshold (default: 0.5)
  - `nms_threshold`: NMS threshold (default: 0.5)
- **Example**: `example_yolo_detector.json`

#### 2. ENet Segmentation
- **Node Type**: `enet_seg`
- **Parameters**:
  - `model_path`: Đường dẫn đến model file
  - `model_config_path`: Config path (optional)
  - `labels_path`: Labels path (optional)
  - `input_width`: Input width (default: 1024)
  - `input_height`: Input height (default: 512)
- **Example**: `example_enet_segmentation.json`

#### 3. Mask RCNN Detector
- **Node Type**: `mask_rcnn_detector`
- **Parameters**:
  - `model_path`: Đường dẫn đến model file (.pb)
  - `model_config_path`: Config path (.pbtxt)
  - `labels_path`: Labels path
  - `input_width`: Input width (default: 416)
  - `input_height`: Input height (default: 416)
  - `score_threshold`: Score threshold (default: 0.5)
- **Example**: `example_mask_rcnn.json`

#### 4. OpenPose Detector
- **Node Type**: `openpose_detector`
- **Parameters**:
  - `model_path`: Đường dẫn đến model file (.caffemodel)
  - `model_config_path`: Config path (.prototxt)
  - `input_width`: Input width (default: 368)
  - `input_height`: Input height (default: 368)
  - `score_threshold`: Score threshold (default: 0.1)
- **Example**: `example_openpose.json`

#### 5. Classifier (Generic)
- **Node Type**: `classifier`
- **Parameters**:
  - `model_path`: Đường dẫn đến model file
  - `labels_path`: Labels path (optional)
  - `input_width`: Input width (default: 128)
  - `input_height`: Input height (default: 128)
  - `batch_size`: Batch size (default: 1)
  - `need_softmax`: Need softmax (default: true)
- **Example**: `example_classifier.json`

#### 6. Feature Encoder (Generic)
- **Node Type**: `feature_encoder`
- **Parameters**:
  - `model_path`: Đường dẫn đến model file

#### 7. Lane Detector
- **Node Type**: `lane_detector`
- **Parameters**:
  - `model_path`: Đường dẫn đến model file
  - `input_width`: Input width (default: 736)
  - `input_height`: Input height (default: 416)
- **Example**: `example_lane_detector.json`

#### 8. PaddleOCR Text Detector
- **Node Type**: `ppocr_text_detector`
- **Parameters**:
  - `det_model_dir`: Detection model directory
  - `cls_model_dir`: Classification model directory (optional)
  - `rec_model_dir`: Recognition model directory
  - `rec_char_dict_path`: Character dictionary path (optional)

#### 9. Restoration (Real-ESRGAN)
- **Node Type**: `restoration`
- **Parameters**:
  - `bg_restoration_model`: Background restoration model path
  - `face_restoration_model`: Face restoration model path (optional)
  - `restoration_to_osd`: Restoration to OSD (default: true)
- **Example**: `example_restoration.json`

## Cách Sử dụng

### Bước 1: Tạo Solution Config

Bạn cần tạo solution config với pipeline chứa các node infers. Ví dụ:

```json
{
  "solutionId": "trt_yolov8_detection",
  "solutionName": "TensorRT YOLOv8 Detection",
  "pipeline": [
    {
      "nodeType": "rtsp_src",
      "nodeName": "rtsp_src_{instanceId}",
      "parameters": {
        "rtsp_url": "${RTSP_URL}",
        "channel": "0"
      }
    },
    {
      "nodeType": "trt_yolov8_detector",
      "nodeName": "yolov8_detector_{instanceId}",
      "parameters": {
        "model_path": "${MODEL_PATH}",
        "labels_path": "${LABELS_PATH}"
      }
    },
    {
      "nodeType": "rtmp_des",
      "nodeName": "rtmp_des_{instanceId}",
      "parameters": {
        "rtmp_url": "${RTMP_URL}",
        "channel": "0"
      }
    }
  ]
}
```

### Bước 2: Tạo Instance

Sử dụng solution ID và cung cấp các tham số trong `additionalParams`:

```json
{
  "name": "yolov8_detector_demo",
  "solution": "trt_yolov8_detection",
  "autoStart": true,
  "additionalParams": {
    "RTSP_URL": "rtsp://localhost:8554/stream",
    "MODEL_PATH": "/opt/omniapi/models/trt/others/yolov8s_v8.5.engine",
    "LABELS_PATH": "/opt/omniapi/models/coco_80classes.txt",
    "RTMP_URL": "rtmp://localhost/live/stream"
  }
}
```

## Lưu ý

1. **Model Paths**: Có thể sử dụng đường dẫn tuyệt đối hoặc tương đối. Hệ thống sẽ tự động resolve model paths dựa trên:
   - `CVEDIX_DATA_ROOT` environment variable
   - `CVEDIX_SDK_ROOT` environment variable
   - System-wide locations (`/usr/share/cvedix/cvedix_data/`)
   - Relative paths từ working directory

2. **Conditional Compilation**: Một số nodes chỉ có sẵn khi compile với flags tương ứng:
   - TensorRT nodes: `CVEDIX_WITH_TRT`
   - RKNN nodes: `CVEDIX_WITH_RKNN`
   - PaddleOCR nodes: `CVEDIX_WITH_PADDLE`

3. **Parameter Mapping**: Các tham số trong `additionalParams` sẽ được map vào các placeholder trong solution config:
   - `${MODEL_PATH}` → `MODEL_PATH` trong additionalParams
   - `${RTSP_URL}` → `RTSP_URL` trong additionalParams
   - `${RTMP_URL}` → `RTMP_URL` trong additionalParams
   - `${FILE_PATH}` → `FILE_PATH` trong additionalParams

## Source Nodes

### 1. App Source
- **Node Type**: `app_src`
- **Parameters**:
  - `channel`: Channel index (default: 0)
- **Example**: `example_app_src.json`
- **Note**: Sử dụng `push_frames()` method để push frames vào pipeline

### 2. Image Source
- **Node Type**: `image_src`
- **Parameters**:
  - `port_or_location`: Port (UDP) hoặc file path pattern (e.g., "./images/%d.jpg")
  - `interval`: Interval giữa các images (seconds, default: 1)
  - `resize_ratio`: Resize ratio (default: 1.0)
  - `cycle`: Cycle through images (default: true)
- **Example**: `example_image_src.json`

### 3. RTMP Source
- **Node Type**: `rtmp_src`
- **Parameters**:
  - `rtmp_url`: RTMP URL
  - `resize_ratio`: Resize ratio (default: 1.0)
  - `skip_interval`: Skip interval (default: 0)
- **Example**: `example_rtmp_src.json`

### 4. UDP Source
- **Node Type**: `udp_src`
- **Parameters**:
  - `port`: UDP port
  - `resize_ratio`: Resize ratio (default: 1.0)
  - `skip_interval`: Skip interval (default: 0)
- **Example**: `example_udp_src.json`

## Broker Nodes

### 1. JSON Console Broker
- **Node Type**: `json_console_broker`
- **Parameters**:
  - `broke_for`: "NORMAL", "FACE", "TEXT", "POSE" (default: "NORMAL")
- **Example**: `example_json_console_broker.json`

### 2. JSON Enhanced Console Broker
- **Node Type**: `json_enhanced_console_broker`
- **Parameters**:
  - `broke_for`: "NORMAL", "FACE", "TEXT", "POSE"
  - `encode_full_frame`: Encode full frame (default: false)
- **Example**: `example_json_enhanced_console_broker.json`

### 3. JSON MQTT Broker
- **Node Type**: `json_mqtt_broker`
- **Parameters**:
  - `broke_for`: "NORMAL", "FACE", "TEXT", "POSE"
  - Requires custom callbacks (provided via SDK)
- **Example**: `example_json_mqtt_broker.json`

### 4. JSON Kafka Broker
- **Node Type**: `json_kafka_broker`
- **Parameters**:
  - `kafka_servers`: Kafka server address (e.g., "127.0.0.1:9092")
  - `topic_name`: Kafka topic name
  - `broke_for`: "NORMAL", "FACE", "TEXT", "POSE"
- **Example**: `example_json_kafka_broker.json`

### 5. XML File Broker
- **Node Type**: `xml_file_broker`
- **Parameters**:
  - `file_path`: Output XML file path
  - `broke_for`: "NORMAL", "FACE", "TEXT", "POSE"
- **Example**: `example_xml_file_broker.json`

### 6. XML Socket Broker
- **Node Type**: `xml_socket_broker`
- **Parameters**:
  - `des_ip`: Destination IP address
  - `des_port`: Destination port
  - `broke_for`: "NORMAL", "FACE", "TEXT", "POSE"
- **Example**: `example_xml_socket_broker.json`

### 7. Message Broker
- **Node Type**: `msg_broker`
- **Parameters**:
  - `broke_for`: "NORMAL", "FACE", "TEXT", "POSE"
- **Example**: `example_msg_broker.json`

### 8. BA Socket Broker
- **Node Type**: `ba_socket_broker`
- **Parameters**:
  - `des_ip`: Destination IP address
  - `des_port`: Destination port
  - `broke_for`: "NORMAL", "FACE", "TEXT", "POSE"
- **Example**: `example_ba_socket_broker.json`

### 9. Embeddings Socket Broker
- **Node Type**: `embeddings_socket_broker`
- **Parameters**:
  - `des_ip`: Destination IP address
  - `des_port`: Destination port
  - `cropped_dir`: Directory for cropped images
  - `min_crop_width`: Minimum crop width
  - `min_crop_height`: Minimum crop height
  - `only_for_tracked`: Only for tracked objects (default: false)
  - `broke_for`: "NORMAL", "FACE", "TEXT", "POSE"
- **Example**: `example_embeddings_socket_broker.json`

### 10. Embeddings Properties Socket Broker
- **Node Type**: `embeddings_properties_socket_broker`
- **Parameters**: Tương tự Embeddings Socket Broker
- **Example**: `example_embeddings_properties_socket_broker.json`

### 11. Plate Socket Broker
- **Node Type**: `plate_socket_broker`
- **Parameters**:
  - `des_ip`: Destination IP address
  - `des_port`: Destination port
  - `plates_dir`: Directory for plate images
  - `min_crop_width`: Minimum crop width (default: 100)
  - `only_for_tracked`: Only for tracked objects (default: true)
  - `broke_for`: "NORMAL", "FACE", "TEXT", "POSE"
- **Example**: `example_plate_socket_broker.json`

### 12. Expression Socket Broker
- **Node Type**: `expr_socket_broker`
- **Parameters**:
  - `des_ip`: Destination IP address
  - `des_port`: Destination port
  - `screenshot_dir`: Directory for screenshots
  - `broke_for`: "NORMAL", "FACE", "TEXT", "POSE" (default: "TEXT")
- **Example**: `example_expr_socket_broker.json`

## Examples

Xem các file example trong thư mục `examples/instances/infer_nodes/`:

### Inference Nodes
- `example_trt_yolov8_detector.json`
- `example_trt_yolov8_segmentation.json`
- `example_trt_yolov8_pose.json`
- `example_trt_vehicle_detector.json`
- `example_trt_vehicle_plate_detector.json`
- `example_rknn_face_detector.json`
- `example_yolo_detector.json`
- `example_enet_segmentation.json`
- `example_mask_rcnn.json`
- `example_openpose.json`
- `example_classifier.json`
- `example_lane_detector.json`
- `example_restoration.json`

### Source Nodes
- `example_app_src.json`
- `example_image_src.json`
- `example_rtmp_src.json`
- `example_udp_src.json`

### Broker Nodes
- `example_json_console_broker.json`
- `example_json_mqtt_broker.json`
- `example_json_kafka_broker.json`
- `example_xml_file_broker.json`
- `example_xml_socket_broker.json`
- `example_msg_broker.json`
- `example_ba_socket_broker.json`
- `example_embeddings_socket_broker.json`
- `example_plate_socket_broker.json`
