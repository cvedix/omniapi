# Default Solutions Reference

## 📋 Tổng Quan

Tài liệu này mô tả các **Default Solutions** được hardcode trong ứng dụng. Các solutions này được tự động load khi khởi động và **KHÔNG THỂ** bị thay đổi, xóa hoặc ghi đè bởi người dùng.

## ✅ Xác nhận: Default Solutions Tự động Có sẵn

Khi bạn **chạy project**, các **default solutions** sẽ **TỰ ĐỘNG có sẵn** ngay lập tức:

1. ✅ `face_detection` - Face Detection với RTSP source
2. ✅ `face_detection_file` - Face Detection với File source
3. ✅ `object_detection` - Object Detection (YOLO)
4. ✅ `face_detection_rtmp` - Face Detection với RTMP Streaming

**Không cần cấu hình gì thêm** - chỉ cần chạy project và sử dụng!

---

## 🚀 Quick Start

### 1. Khởi động project

```bash
cd build
./omniapi
```

### 2. Kiểm tra solutions có sẵn

```bash
# List tất cả solutions (sẽ thấy default solutions)
curl http://localhost:8080/v1/core/solution | jq
```

Kết quả sẽ có:
```json
{
  "solutions": [
    {
      "solutionId": "face_detection",
      "solutionName": "Face Detection",
      "isDefault": true,
      ...
    },
    {
      "solutionId": "face_detection_file",
      "solutionName": "Face Detection with File Source",
      "isDefault": true,
      ...
    },
    {
      "solutionId": "object_detection",
      "solutionName": "Object Detection (YOLO)",
      "isDefault": true,
      ...
    },
    {
      "solutionId": "face_detection_rtmp",
      "solutionName": "Face Detection with RTMP Streaming",
      "isDefault": true,
      ...
    }
  ],
  "total": 4,
  "default": 4,
  "custom": 0
}
```

### 3. Sử dụng default solution

```bash
# Tạo instance với default solution
curl -X POST http://localhost:8080/v1/core/instance \
  -H "Content-Type: application/json" \
  -d '{
    "name": "my_instance",
    "solution": "face_detection",
    "additionalParams": {
      "RTSP_SRC_URL": "rtsp://localhost/stream",
      "MODEL_PATH": "/path/to/yunet.onnx"
    }
  }'
```

---

## 📚 Chi Tiết Các Default Solutions

### 1. `face_detection`

**Mô tả**: Face detection với RTSP source, YuNet face detector, và file destination

**Pipeline**:
- **RTSP Source** (`rtsp_src_{instanceId}`)
  - `rtsp_url`: `${RTSP_SRC_URL}`
  - `channel`: `0`
  - `resize_ratio`: `1.0`
- **YuNet Face Detector** (`face_detector_{instanceId}`)
  - `model_path`: `${MODEL_PATH}`
  - `score_threshold`: `${detectionSensitivity}`
  - `nms_threshold`: `0.5`
  - `top_k`: `50`
- **File Destination** (`file_des_{instanceId}`)
  - `save_dir`: `./output/{instanceId}`
  - `name_prefix`: `face_detection`
  - `osd`: `true`

**Defaults**:
- `detectorMode`: `SmartDetection`
- `detectionSensitivity`: `0.7`
- `sensorModality`: `RGB`

**Required Parameters**:
- `RTSP_SRC_URL`: RTSP stream URL
- `MODEL_PATH`: Đường dẫn đến YuNet model file (.onnx)

---

### 2. `face_detection_file`

**Mô tả**: Face detection với file source, YuNet face detector, và file destination

**Pipeline**:
- **File Source** (`file_src_{instanceId}`)
  - `file_path`: `${FILE_PATH}`
  - `channel`: `0`
  - `resize_ratio`: `1.0`
- **YuNet Face Detector** (`face_detector_{instanceId}`)
  - `model_path`: `${MODEL_PATH}`
  - `score_threshold`: `${detectionSensitivity}`
  - `nms_threshold`: `0.5`
  - `top_k`: `50`
- **File Destination** (`file_des_{instanceId}`)
  - `save_dir`: `./output/{instanceId}`
  - `name_prefix`: `face_detection`
  - `osd`: `true`

**Defaults**:
- `detectorMode`: `SmartDetection`
- `detectionSensitivity`: `0.7`
- `sensorModality`: `RGB`

**Required Parameters**:
- `FILE_PATH`: Đường dẫn đến video file
- `MODEL_PATH`: Đường dẫn đến YuNet model file (.onnx)

---

### 3. `object_detection`

**Mô tả**: Object detection với RTSP source, YOLO detector (chưa implement), và file destination

**Pipeline**:
- **RTSP Source** (`rtsp_src_{instanceId}`)
  - `rtsp_url`: `${RTSP_SRC_URL}`
  - `channel`: `0`
  - `resize_ratio`: `1.0`
- **YOLO Detector** (`yolo_detector_{instanceId}`) - **CHƯA IMPLEMENT**
  - `weights_path`: `${WEIGHTS_PATH}`
  - `config_path`: `${CONFIG_PATH}`
  - `labels_path`: `${LABELS_PATH}`
  - **Lưu ý**: Node này đang bị comment trong code. Để sử dụng cần:
    1. Thêm case `yolo_detector` trong `PipelineBuilder::createNode()`
    2. Implement `createYOLODetectorNode()` trong `PipelineBuilder`
    3. Uncomment code trong `registerObjectDetectionSolution()`
- **File Destination** (`file_des_{instanceId}`)
  - `save_dir`: `./output/{instanceId}`
  - `name_prefix`: `object_detection`
  - `osd`: `true`

**Defaults**:
- `detectorMode`: `SmartDetection`
- `detectionSensitivity`: `0.7`
- `sensorModality`: `RGB`

**Required Parameters**:
- `RTSP_SRC_URL`: RTSP stream URL
- `WEIGHTS_PATH`: Đường dẫn đến YOLO weights file (.weights)
- `CONFIG_PATH`: Đường dẫn đến YOLO config file (.cfg)
- `LABELS_PATH`: Đường dẫn đến labels file (.txt)

---

### 4. `face_detection_rtmp`

**Mô tả**: Face detection với file source, YuNet detector, Face OSD v2, và RTMP streaming destination

**Pipeline**:
- **File Source** (`file_src_{instanceId}`)
  - `file_path`: `${FILE_PATH}`
  - `channel`: `0`
  - `resize_ratio`: `1.0`
  - **Lưu ý**: Sử dụng `resize_ratio = 1.0` nếu video đã có resolution cố định để tránh double-resizing
- **YuNet Face Detector** (`yunet_face_detector_{instanceId}`)
  - `model_path`: `${MODEL_PATH}`
  - `score_threshold`: `${detectionSensitivity}`
  - `nms_threshold`: `0.5`
  - `top_k`: `50`
  - **Lưu ý**: YuNet 2022mar có thể có vấn đề với dynamic input sizes. Nên dùng YuNet 2023mar
- **Face OSD v2** (`osd_{instanceId}`)
  - Không có parameters
- **RTMP Destination** (`rtmp_des_{instanceId}`)
  - `rtmp_url`: `${RTMP_URL}`
  - `channel`: `0`

**Defaults**:
- `detectorMode`: `SmartDetection`
- `detectionSensitivity`: `Low`
- `sensorModality`: `RGB`

**Required Parameters**:
- `FILE_PATH`: Đường dẫn đến video file
- `MODEL_PATH`: Đường dẫn đến YuNet model file (.onnx)
- `RTMP_URL`: RTMP streaming URL

---

## 📊 Tóm Tắt

| Solution ID | Solution Name | Type | Source | Detector | Destination |
|-------------|---------------|------|--------|-----------|-------------|
| `face_detection` | Face Detection | face_detection | RTSP | YuNet | File |
| `face_detection_file` | Face Detection with File Source | face_detection | File | YuNet | File |
| `object_detection` | Object Detection (YOLO) | object_detection | RTSP | YOLO* | File |
| `face_detection_rtmp` | Face Detection with RTMP Streaming | face_detection | File | YuNet | RTMP |

*YOLO detector chưa được implement

---

## 🔧 Thêm/Cập nhật Default Solutions

### Cách 1: Sử dụng Script Helper (Khuyến nghị)

```bash
# Generate template code tự động
./scripts/generate_default_solution_template.sh

# Script sẽ hỏi:
# - Solution ID
# - Solution Name
# - Solution Type
# → Tạo template code sẵn để copy vào project
```

### Cách 2: Làm thủ công

**Tóm tắt nhanh:**
1. Tạo hàm `register[Name]Solution()` trong `src/solutions/solution_registry.cpp`
2. Khai báo hàm trong `include/solutions/solution_registry.h`
3. Gọi hàm trong `initializeDefaultSolutions()`
4. Set `config.isDefault = true`
5. Rebuild project

**Ví dụ**: Cập nhật `detectionSensitivity` mặc định của `face_detection`:

```cpp
void SolutionRegistry::registerFaceDetectionSolution() {
    // ... existing code ...

    // Thay đổi default
    config.defaults["detectionSensitivity"] = "0.8";  // Từ 0.7 → 0.8

    registerSolution(config);
}
```

Sau đó rebuild:
```bash
cd build && make
```

### 📋 Checklist Khi Thêm/Cập nhật

- [ ] Tạo hàm register mới (hoặc sửa hàm cũ)
- [ ] Khai báo trong header file
- [ ] Gọi hàm trong `initializeDefaultSolutions()`
- [ ] Set `config.isDefault = true`
- [ ] Đảm bảo `solutionId` unique
- [ ] Rebuild project
- [ ] Test solution bằng cách tạo instance
- [ ] Cập nhật `docs/default_solutions_backup.json` (nếu cần)
- [ ] Cập nhật tài liệu

---

## 📍 Vị Trí Trong Code

Các default solutions được định nghĩa trong:
- **File**: `src/solutions/solution_registry.cpp`
- **Functions**:
  - `registerFaceDetectionSolution()` - line 100
  - `registerFaceDetectionFileSolution()` - line 145
  - `registerObjectDetectionSolution()` - line 190
  - `registerFaceDetectionRTMPSolution()` - line 238
- **Initialization**: `initializeDefaultSolutions()` - line 93

### Kiểm tra trong code:

```bash
# Xem các hàm register
grep "register.*Solution()" src/solutions/solution_registry.cpp

# Xem initialization
grep -A 5 "initializeDefaultSolutions" src/main.cpp
```

### Kiểm tra khi chạy:

```bash
# List solutions
curl http://localhost:8080/v1/core/solution | jq '.solutions[] | select(.isDefault == true)'

# Get chi tiết từng solution
curl http://localhost:8080/v1/core/solution/face_detection | jq
```

---

## 💾 Backup và Restore

### File Backup

File backup JSON chứa tất cả default solutions:
- **Location**: `docs/default_solutions_backup.json`
- **Mục đích**: Tham khảo và restore khi cần
- **Lưu ý**: File này chỉ để tham khảo, không được load vào storage

### Script Restore

Script để reset storage file về trạng thái mặc định:
- **Location**: `scripts/restore_default_solutions.sh`
- **Usage**:
  ```bash
  ./scripts/restore_default_solutions.sh
  ```
- **Chức năng**:
  - Backup file `solutions.json` hiện tại
  - Reset file về trạng thái rỗng `{}`
  - Default solutions sẽ tự động load khi khởi động lại ứng dụng

---

## 🔒 Bảo Mật

Các default solutions được bảo vệ bởi nhiều lớp:
- ✅ Không thể tạo/update/delete qua API
- ✅ Không được lưu vào storage file
- ✅ Không thể load từ storage (nếu có ai đó manually edit)
- ✅ Luôn được load từ code khi khởi động

**Lưu ý bảo mật:** Default solutions được load từ code khi khởi động, không thể bị thay đổi từ bên ngoài.

---

## ⚠️ Lưu ý Quan Trọng

1. **Default solutions tự động load**: Không cần cấu hình, tự động có sẵn khi chạy project
2. **Không lưu vào storage**: Default solutions không được lưu vào `solutions.json`
3. **Không thể xóa qua API**: Default solutions được bảo vệ, chỉ có thể sửa trong code
4. **Phải rebuild**: Sau khi sửa code, phải rebuild để thay đổi có hiệu lực
5. **isDefault = true**: Luôn nhớ set flag này cho default solutions
6. **Default solutions không thể thay đổi**: Nếu cần customize, hãy tạo custom solution mới với ID khác
7. **Storage file**: File `solutions.json` chỉ chứa custom solutions, không chứa default solutions
8. **Restore**: Nếu muốn reset về trạng thái mặc định, chỉ cần xóa tất cả custom solutions trong storage file

---

## 🎯 Tóm Tắt

✅ **Default solutions tự động có sẵn khi chạy project**
✅ **Không cần cấu hình gì thêm**
✅ **Có thể thêm/cập nhật bằng cách sửa code**
✅ **Có script helper để tạo template nhanh**

**Bắt đầu sử dụng ngay bây giờ!** 🚀

---

## 📚 Tài Liệu Liên Quan

- **[INSTANCE_GUIDE.md](./INSTANCE_GUIDE.md)** - Hướng dẫn sử dụng instances
- **[DEVELOPMENT_GUIDE.md](./DEVELOPMENT_GUIDE.md)** - Hướng dẫn phát triển và thêm features mới
- **[examples/instances/README.md](../examples/instances/README.md)** - Examples và test files cho instances
