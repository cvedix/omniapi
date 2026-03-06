# Default Solutions

Thư mục này chứa các **default solutions** đã được cấu hình sẵn để người dùng có thể chọn và sử dụng ngay. Các solutions này được tổ chức theo category và có đầy đủ documentation.

## 📋 Danh sách Solutions

### Face Detection

| Solution ID | Tên | Mô tả | Độ khó |
|------------|-----|-------|--------|
| `default_minimal` | Minimal Solution | Solution tối giản với cấu hình cơ bản nhất | ⭐ Beginner |
| `default_face_detection_file` | Face Detection - File Source | Face detection với file video input và file output | ⭐ Beginner |
| `default_face_detection_rtsp` | Face Detection - RTSP Stream | Face detection với RTSP stream input | ⭐⭐ Intermediate |
| `default_face_detection_rtmp` | Face Detection - RTMP Streaming | Face detection với RTMP streaming output | ⭐⭐ Intermediate |

### Object Detection

| Solution ID | Tên | Mô tả | Độ khó |
|------------|-----|-------|--------|
| `default_object_detection_yolo` | Object Detection - YOLO | Object detection với YOLO model | ⭐⭐ Intermediate |

### Segmentation

| Solution ID | Tên | Mô tả | Độ khó |
|------------|-----|-------|--------|
| `default_mask_rcnn_detection` | MaskRCNN Instance Segmentation | MaskRCNN segmentation với file output | ⭐⭐⭐ Advanced |
| `default_mask_rcnn_rtmp` | MaskRCNN with RTMP Streaming | MaskRCNN segmentation với RTMP streaming | ⭐⭐⭐ Advanced |

### Behavior Analysis

| Solution ID | Tên | Mô tả | Độ khó |
|------------|-----|-------|--------|
| `default_ba_crossline` | Behavior Analysis - Crossline | Phát hiện đối tượng vượt qua đường line | ⭐⭐⭐ Advanced |
| `default_ba_crossline_mqtt` | BA Crossline with MQTT | Crossline detection với MQTT events | ⭐⭐⭐ Advanced |

## 🚀 Cách sử dụng

### 1. Xem danh sách tất cả solutions (bao gồm cả default)

```bash
# API này sẽ trả về tất cả solutions đã load và cả default solutions chưa load
curl http://localhost:8080/v1/core/solution

# Response sẽ bao gồm:
# - solutions: Danh sách tất cả solutions
#   - loaded: true nếu đã load vào registry, false nếu chỉ có trong default_solutions
#   - available: true nếu file tồn tại
# - total: Tổng số solutions
# - default: Số system default solutions
# - custom: Số custom solutions đã tạo
# - availableDefault: Số default solutions có sẵn từ thư mục
```

### 2. Load Default Solution tự động khi tạo instance body

```bash
# Khi gọi API này, nếu solution chưa load, hệ thống sẽ tự động load từ default_solutions
curl http://localhost:8080/v1/core/solution/default_face_detection_file/instance-body

# Solution sẽ được tự động load và trả về example request body để tạo instance
```

### 3. Load Default Solution thủ công (Optional)

```bash
# Nếu muốn load trước khi sử dụng
curl -X POST http://localhost:8080/v1/core/solution/defaults/default_face_detection_file

# Hoặc tạo trực tiếp từ file JSON
curl -X POST http://localhost:8080/v1/core/solution \
  -H "Content-Type: application/json" \
  -d @examples/default_solutions/face_detection_file.json
```

### 3. Tạo Instance từ Solution

Sau khi đã tạo solution, bạn có thể tạo instance với các tham số cần thiết:

```bash
# Ví dụ: Tạo instance với default_face_detection_file
curl -X POST http://localhost:8080/v1/core/instance \
  -H "Content-Type: application/json" \
  -d '{
    "name": "my_face_detection",
    "solution": "default_face_detection_file",
    "additionalParams": {
      "FILE_PATH": "/path/to/video.mp4",
      "MODEL_PATH": "/path/to/face_detection_yunet_2022mar.onnx",
      "RESIZE_RATIO": "1.0"
    }
  }'
```

## 📝 Chi tiết từng Solution

### default_minimal

**Mô tả**: Solution tối giản nhất với cấu hình cơ bản cho face detection.

**Tham số bắt buộc**:
- `FILE_PATH`: Đường dẫn file video input
- `MODEL_PATH`: Đường dẫn model face detection (yunet)

**Ví dụ**:
```json
{
  "name": "minimal_test",
  "solution": "default_minimal",
  "additionalParams": {
    "FILE_PATH": "./test_video/face.mp4",
    "MODEL_PATH": "/opt/edgeos-api/models/face/yunet.onnx"
  }
}
```

### default_face_detection_file

**Mô tả**: Face detection với file video làm input và lưu kết quả ra file.

**Tham số bắt buộc**:
- `FILE_PATH`: Đường dẫn file video input
- `MODEL_PATH`: Đường dẫn model face detection (yunet)

**Tham số tùy chọn**:
- `RESIZE_RATIO`: Tỷ lệ resize video (0.0-1.0), mặc định 1.0

**Ví dụ**:
```json
{
  "name": "face_detection_file",
  "solution": "default_face_detection_file",
  "additionalParams": {
    "FILE_PATH": "./test_video/face.mp4",
    "MODEL_PATH": "/opt/edgeos-api/models/face/yunet.onnx",
    "RESIZE_RATIO": "0.5"
  }
}
```

### default_face_detection_rtsp

**Mô tả**: Face detection với RTSP stream làm input và lưu kết quả ra file.

**Tham số bắt buộc**:
- `RTSP_URL`: URL RTSP stream
- `MODEL_PATH`: Đường dẫn model face detection (yunet)

**Tham số tùy chọn**:
- `RESIZE_RATIO`: Tỷ lệ resize video (0.0-1.0), mặc định 1.0

**Ví dụ**:
```json
{
  "name": "face_detection_rtsp",
  "solution": "default_face_detection_rtsp",
  "additionalParams": {
    "RTSP_URL": "rtsp://localhost:8554/live/stream",
    "MODEL_PATH": "/opt/edgeos-api/models/face/yunet.onnx"
  }
}
```

### default_face_detection_rtmp

**Mô tả**: Face detection với file/RTSP input và stream kết quả qua RTMP.

**Tham số bắt buộc**:
- `FILE_PATH` hoặc `RTSP_URL`: Đường dẫn file video hoặc URL RTSP
- `RTMP_URL`: URL RTMP server để stream output
- `MODEL_PATH`: Đường dẫn model face detection (yunet)

**Ví dụ**:
```json
{
  "name": "face_detection_rtmp",
  "solution": "default_face_detection_rtmp",
  "additionalParams": {
    "FILE_PATH": "./test_video/face.mp4",
    "RTMP_URL": "rtmp://localhost:1935/live/face_stream",
    "MODEL_PATH": "/opt/edgeos-api/models/face/yunet.onnx"
  }
}
```

### default_object_detection_yolo

**Mô tả**: Object detection với YOLO model.

**Tham số bắt buộc**:
- `FILE_PATH` hoặc `RTSP_URL`: Đường dẫn file video hoặc URL RTSP
- `MODEL_PATH`: Đường dẫn YOLO weights file (.weights)
- `CONFIG_PATH`: Đường dẫn YOLO config file (.cfg)
- `LABELS_PATH`: Đường dẫn labels file (.txt)

**Ví dụ**:
```json
{
  "name": "object_detection_yolo",
  "solution": "default_object_detection_yolo",
  "additionalParams": {
    "FILE_PATH": "./test_video/objects.mp4",
    "MODEL_PATH": "/opt/edgeos-api/models/yolo/yolov8.weights",
    "CONFIG_PATH": "/opt/edgeos-api/models/yolo/yolov8.cfg",
    "LABELS_PATH": "/opt/edgeos-api/models/yolo/coco.names"
  }
}
```

### default_mask_rcnn_detection

**Mô tả**: MaskRCNN instance segmentation với file input và file output.

**Tham số bắt buộc**:
- `FILE_PATH`: Đường dẫn file video input
- `MODEL_PATH`: Đường dẫn MaskRCNN model file (.pb)
- `MODEL_CONFIG_PATH`: Đường dẫn config file (.pbtxt)
- `LABELS_PATH`: Đường dẫn labels file (.txt)

**Tham số tùy chọn**:
- `INPUT_WIDTH`: Chiều rộng input (mặc định 416)
- `INPUT_HEIGHT`: Chiều cao input (mặc định 416)
- `SCORE_THRESHOLD`: Ngưỡng confidence (mặc định 0.5)

**Ví dụ**:
```json
{
  "name": "mask_rcnn_test",
  "solution": "default_mask_rcnn_detection",
  "additionalParams": {
    "FILE_PATH": "./test_video/segmentation.mp4",
    "MODEL_PATH": "/opt/edgeos-api/models/mask_rcnn/frozen_inference_graph.pb",
    "MODEL_CONFIG_PATH": "/opt/edgeos-api/models/mask_rcnn/mask_rcnn.pbtxt",
    "LABELS_PATH": "/opt/edgeos-api/models/coco_80classes.txt",
    "INPUT_WIDTH": "416",
    "INPUT_HEIGHT": "416",
    "SCORE_THRESHOLD": "0.5"
  }
}
```

### default_ba_crossline

**Mô tả**: Phát hiện đối tượng vượt qua đường line.

**Tham số bắt buộc**:
- `FILE_PATH` hoặc `RTSP_URL`: Đường dẫn file video hoặc URL RTSP
- `WEIGHTS_PATH`: Đường dẫn YOLO weights file
- `CONFIG_PATH`: Đường dẫn YOLO config file
- `LABELS_PATH`: Đường dẫn labels file
- `CROSSLINE_START_X`, `CROSSLINE_START_Y`: Tọa độ điểm đầu đường line
- `CROSSLINE_END_X`, `CROSSLINE_END_Y`: Tọa độ điểm cuối đường line

**Ví dụ**:
```json
{
  "name": "ba_crossline_test",
  "solution": "default_ba_crossline",
  "additionalParams": {
    "FILE_PATH": "./test_video/crossline.mp4",
    "WEIGHTS_PATH": "/opt/edgeos-api/models/yolo/yolov8.weights",
    "CONFIG_PATH": "/opt/edgeos-api/models/yolo/yolov8.cfg",
    "LABELS_PATH": "/opt/edgeos-api/models/yolo/coco.names",
    "CROSSLINE_START_X": "100",
    "CROSSLINE_START_Y": "200",
    "CROSSLINE_END_X": "800",
    "CROSSLINE_END_Y": "200"
  }
}
```

## 🔧 Tùy chỉnh Solutions

Bạn có thể:
1. Copy một solution file và chỉnh sửa theo nhu cầu
2. Thay đổi các tham số trong `defaults` hoặc `pipeline`
3. Tạo solution mới dựa trên các solution có sẵn

## 📚 Tài liệu tham khảo

- [Solutions Reference](../../docs/DEFAULT_SOLUTIONS_REFERENCE.md) - Documentation về các solutions
- [Instance Guide](../../docs/INSTANCE_GUIDE.md) - Hướng dẫn tạo và cập nhật instances
- [API Documentation](../../docs/DEVELOPMENT.md) - API documentation

## 💡 Tips

1. **Bắt đầu với solution đơn giản**: Nếu mới bắt đầu, hãy dùng `default_minimal` hoặc `default_face_detection_file`
2. **Kiểm tra tham số**: Mỗi solution có danh sách `requiredParams` và `optionalParams` trong file JSON
3. **Test với file nhỏ**: Trước khi chạy với video lớn, test với file video nhỏ để đảm bảo cấu hình đúng
4. **Xem logs**: Kiểm tra logs để debug nếu có lỗi

## 🐛 Troubleshooting

### Solution không tạo được
- Kiểm tra file JSON có đúng format không
- Kiểm tra các tham số bắt buộc đã được cung cấp chưa
- Xem logs của API server

### Instance không chạy được
- Kiểm tra đường dẫn model/file có đúng không
- Kiểm tra quyền truy cập file
- Kiểm tra RTSP/RTMP URL có hoạt động không

### Performance issues
- Giảm `RESIZE_RATIO` để tăng tốc độ xử lý
- Giảm `frameRateLimit` trong instance config
- Kiểm tra tài nguyên hệ thống (CPU, RAM, GPU)
