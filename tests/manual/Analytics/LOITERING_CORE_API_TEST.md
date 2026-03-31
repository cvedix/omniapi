# Hướng Dẫn Test Thủ Công - Loitering với Core API

## Mục Lục

1. [Tổng Quan](#tổng-quan)
2. [Prerequisites](#prerequisites)
3. [Setup Variables](#setup-variables)
4. [Test Case 1: Tạo Instance với Loitering Areas qua Core API](#test-case-1-tạo-instance-với-loitering-areas-qua-core-api)
5. [Test Case 2: Workflow Hoàn Chỉnh](#test-case-2-workflow-hoàn-chỉnh)
6. [Expected Results](#expected-results)
7. [Troubleshooting](#troubleshooting)

---

## Tổng Quan

Tài liệu này hướng dẫn test thủ công tính năng **Loitering Detection** bằng cách tạo instance qua **Core API** (`POST /v1/core/instance`) với loitering areas được định nghĩa trong request body.

**Lưu ý quan trọng**: Pipeline structure trong test này phải khớp với `ba_loitering_sample.cpp`:
- Pipeline: `file_src` → `yolo_detector` → `sort_track` → `ba_loitering` → `ba_loitering_osd` → `screen_des`/`file_des`
- Regions format: Sử dụng `cvedix_rect(x, y, width, height)` - polygon coordinates sẽ được tự động convert thành bounding rectangle
- OSD node: `ba_loitering_osd` sử dụng `cvedix_ba_stop_osd_node` internally (giống như trong sample code)
- Frame rate: Mặc định 30 fps (giống như sample code)

### Base URLs
```
Core API:   http://localhost:8080/v1/core/instance
```

---

## Prerequisites

1. **API Server đang chạy**:
   ```bash
   # Kiểm tra server đang chạy
   curl http://localhost:8080/v1/core/health
   ```

2. **Công cụ cần thiết**:
   - `curl` - để gửi HTTP requests
   - `jq` (optional) - để format JSON output đẹp hơn
   - Video file hoặc RTSP camera URL để test input

3. **Tài nguyên test**:
   - Video file hoặc RTSP camera URL để test input
   - MQTT broker (nếu test MQTT output)

---

## Setup Variables

```bash
# Thay đổi các giá trị này theo môi trường của bạn
SERVER="http://localhost:8080"
CORE_BASE="${SERVER}/v1/core/instance"

# Instance ID sẽ được tạo tự động
INSTANCE_ID=""  # Sẽ được set sau khi tạo instance

# Test resources
RTSP_URL="rtsp://192.168.1.100:554/stream1"  # Thay bằng RTSP URL thực tế
VIDEO_FILE="./test_video.mp4"  # Thay bằng đường dẫn file video thực tế
```

---

## So Sánh với Sample Code (`ba_loitering_sample.cpp`)

### Pipeline Structure

**Sample Code (ba_loitering_sample.cpp)**:
```cpp
// Lines 51-56: Pipeline structure
vehicle_detector->attach_to({file_src_0});
tracker->attach_to({vehicle_detector});
ba_stop->attach_to({tracker});
osd->attach_to({ba_stop});
screen_des_0->attach_to({osd});
```

**API Pipeline (tương đương)**:
```
file_src → yolo_detector → sort_track → ba_loitering → ba_loitering_osd → screen_des/file_des
```

### Regions Format

**Sample Code (ba_loitering_sample.cpp)**:
```cpp
// Lines 36-38: Regions format
std::map<int, cvedix_objects::cvedix_rect> regions = {
    {0, cvedix_objects::cvedix_rect(20,30,580,270)}
};

// Lines 40-42: Alarm seconds
std::map<int, double> alarm_seconds = {
    {0,5}
};

// Line 43: Node creation
auto ba_stop = std::make_shared<cvedix_nodes::cvedix_ba_loitering_node>(
    "ba_stop", regions, alarm_seconds, 30);
```

**API Equivalent**:
- Polygon coordinates `[{x:20,y:30}, {x:600,y:30}, {x:600,y:300}, {x:20,y:300}]` → Rectangle `rect(20, 30, 580, 270)`
- `seconds: 5` → `alarm_seconds[0] = 5.0`
- Frame rate: 30 fps (mặc định)

### OSD Node

**Sample Code (ba_loitering_sample.cpp)**:
```cpp
// Line 45: OSD node
auto osd = std::make_shared<cvedix_nodes::cvedix_ba_stop_osd_node>("osd");
```

**API Equivalent**:
- `ba_loitering_osd` node type sử dụng `cvedix_ba_stop_osd_node` internally

---

## Test Case 1: Tạo Instance với Loitering Areas qua Core API

### 1.1: Tạo Instance với Loitering Areas (Rectangle Format - Khớp với Sample Code)

**Lưu ý**: Ví dụ này khớp với `ba_loitering_sample.cpp`:
- Sample code sử dụng: `cvedix_rect(20,30,580,270)` với `alarm_seconds={0,5}` và frame rate 30
- API sẽ convert polygon coordinates thành rectangle format tương tự

```bash
echo "=== Test 1.1: Tạo Instance với Loitering Areas (Rectangle - Khớp với Sample Code) ==="

RESPONSE=$(curl -s -X POST "${CORE_BASE}" \
  -H "Content-Type: application/json" \
  -d '{
    "solution": "securt",
    "name": "Loitering Test Instance",
    "group": "demo",
    "autoStart": false,
    "detectorMode": "SmartDetection",
    "detectionSensitivity": "Medium",
    "movementSensitivity": "Medium",
    "additionalParams": {
      "input": {
        "FILE_PATH": "./cvedix_data/test_video/enet_seg.mp4",
        "WEIGHTS_PATH": "./cvedix_data/models/det_cls/yolov3-tiny-2022-0721_best.weights",
        "CONFIG_PATH": "./cvedix_data/models/det_cls/yolov3-tiny-2022-0721.cfg",
        "LABELS_PATH": "./cvedix_data/models/det_cls/yolov3_tiny_5classes.txt",
        "RESIZE_RATIO": "0.6"
      },
      "output": {
        "ENABLE_SCREEN_DES": "true"
      },
      "LoiteringAreas": "[{\"coordinates\":[{\"x\":20,\"y\":30},{\"x\":600,\"y\":30},{\"x\":600,\"y\":300},{\"x\":20,\"y\":300}],\"seconds\":5,\"name\":\"Loitering Zone 1\",\"channel\":0}]"
    }
  }')

echo "$RESPONSE" | jq .

# Lấy instance ID từ response
INSTANCE_ID=$(echo "$RESPONSE" | jq -r '.instanceId')
echo "Instance ID: $INSTANCE_ID"

# Kiểm tra pipeline structure khớp với sample code
echo ""
echo "=== Kiểm tra Pipeline Structure (khớp với ba_loitering_sample.cpp) ==="
curl -s -X GET "${CORE_BASE}/${INSTANCE_ID}/config" | jq '.Pipeline | map({NodeType, NodeName})'

# Lấy instance ID từ response
INSTANCE_ID=$(echo "$RESPONSE" | jq -r '.instanceId')
echo "Instance ID: $INSTANCE_ID"

# Lưu vào biến để dùng cho các test sau
export INSTANCE_ID
```

**Expected**: Status 201 Created, response chứa `instanceId`:
```json
{
  "instanceId": "abc-123-def-456"
}
```

**Giải thích**:
- `additionalParams` có cấu trúc với `input`, `output`, và các tham số top-level như `LoiteringAreas`
- `input` section chứa các tham số liên quan đến input source (FILE_PATH, RTSP_URL, model paths, etc.)
- `output` section chứa các tham số liên quan đến output (RTMP_DES_URL, ENABLE_SCREEN_DES, etc.)
- `LoiteringAreas` là một JSON string (escaped) ở top-level của `additionalParams`, chứa array các loitering areas
- Mỗi area có:
  - `coordinates`: Array các điểm tạo thành polygon (sẽ được convert thành bounding rectangle `cvedix_rect(x, y, width, height)`)
    - **Format conversion**: Polygon coordinates `[{x:100,y:100}, {x:500,y:100}, {x:500,y:400}, {x:100,y:400}]` → Rectangle `rect(100, 100, 400, 300)` (x, y, width, height)
    - **Khớp với sample code**: Sample code sử dụng `cvedix_rect(20,30,580,270)` tương đương với polygon `[{x:20,y:30}, {x:600,y:30}, {x:600,y:300}, {x:20,y:300}]`
  - `seconds`: Thời gian (giây) object phải ở trong area để trigger alarm (default: 5) - khớp với `alarm_seconds` map trong sample code
  - `name`: Tên của area (optional)
  - `channel`: Channel number (default: 0) - khớp với channel trong `regions` map của sample code

### 1.2: Tạo Instance với Loitering Areas (Normalized Coordinates)

```bash
echo "=== Test 1.2: Tạo Instance với Loitering Areas (Normalized) ==="

RESPONSE=$(curl -s -X POST "${CORE_BASE}" \
  -H "Content-Type: application/json" \
  -d '{
    "solution": "securt",
    "name": "Loitering Test Instance (Normalized)",
    "group": "demo",
    "autoStart": false,
    "detectorMode": "SmartDetection",
    "additionalParams": {
      "input": {
        "FILE_PATH": "./test_video.mp4",
        "WEIGHTS_PATH": "/opt/omniapi/models/det_cls/yolov3-tiny-2022-0721_best.weights",
        "CONFIG_PATH": "/opt/omniapi/models/det_cls/yolov3-tiny-2022-0721.cfg",
        "LABELS_PATH": "/opt/omniapi/models/det_cls/yolov3_tiny_5classes.txt",
        "RESIZE_RATIO": "0.6"
      },
      "output": {
        "ENABLE_SCREEN_DES": "false"
      },
      "LoiteringAreas": "[{\"coordinates\":[{\"x\":0.1,\"y\":0.1},{\"x\":0.5,\"y\":0.1},{\"x\":0.5,\"y\":0.4},{\"x\":0.1,\"y\":0.4}],\"seconds\":8,\"name\":\"Normalized Zone\"}]"
    }
  }')

echo "$RESPONSE" | jq .
INSTANCE_ID=$(echo "$RESPONSE" | jq -r '.instanceId')
echo "Instance ID: $INSTANCE_ID"
```

**Expected**: Status 201 Created. Normalized coordinates (0.0-1.0) sẽ được tự động convert sang pixel coordinates.

### 1.3: Tạo Instance với RTMP Input/Output và Loitering Areas

```bash
echo "=== Test 1.3: Tạo Instance với RTMP Input/Output và Loitering Areas ==="

RESPONSE=$(curl -s -X POST "${CORE_BASE}" \
  -H "Content-Type: application/json" \
  -d '{
    "name": "ba_loitering_rtmp",
    "group": "demo",
    "solution": "securt",
    "autoStart": false,
    "additionalParams": {
      "input": {
        "RTMP_SRC_URL": "rtmp://192.168.1.128:1935/live/camera_demo",
        "WEIGHTS_PATH": "/opt/omniapi/models/det_cls/yolov3-tiny-2022-0721_best.weights",
        "CONFIG_PATH": "/opt/omniapi/models/det_cls/yolov3-tiny-2022-0721.cfg",
        "LABELS_PATH": "/opt/omniapi/models/det_cls/yolov3_tiny_5classes.txt",
        "RESIZE_RATIO": "1.0",
        "GST_DECODER_NAME": "avdec_h264"
      },
      "output": {
        "RTMP_DES_URL": "rtmp://192.168.1.128:1935/live/ba_loitering_stream_1",
        "ENABLE_SCREEN_DES": "false"
      },
      "LoiteringAreas": "[{\"id\":\"area1\",\"name\":\"Restricted Zone 1\",\"coordinates\":[{\"x\":100,\"y\":100},{\"x\":500,\"y\":100},{\"x\":500,\"y\":400},{\"x\":100,\"y\":400}],\"seconds\":5,\"classes\":[\"Person\"],\"color\":[255,0,0,255]}]"
    }
  }')

echo "$RESPONSE" | jq .
INSTANCE_ID=$(echo "$RESPONSE" | jq -r '.instanceId')
echo "Instance ID: $INSTANCE_ID"
```

**Expected**: Status 201 Created với RTMP input và output được cấu hình.

### 1.4: Tạo Instance với RTSP Input và Loitering Areas

```bash
echo "=== Test 1.4: Tạo Instance với RTSP Input và Loitering Areas ==="

RESPONSE=$(curl -s -X POST "${CORE_BASE}" \
  -H "Content-Type: application/json" \
  -d "{
    \"name\": \"ba_loitering_rtsp\",
    \"group\": \"demo\",
    \"solution\": \"securt\",
    \"autoStart\": false,
    \"additionalParams\": {
      \"input\": {
        \"RTSP_URL\": \"${RTSP_URL}\",
        \"WEIGHTS_PATH\": \"/opt/omniapi/models/det_cls/yolov3-tiny-2022-0721_best.weights\",
        \"CONFIG_PATH\": \"/opt/omniapi/models/det_cls/yolov3-tiny-2022-0721.cfg\",
        \"LABELS_PATH\": \"/opt/omniapi/models/det_cls/yolov3_tiny_5classes.txt\",
        \"RESIZE_RATIO\": \"0.6\",
        \"GST_DECODER_NAME\": \"avdec_h264\"
      },
      \"output\": {
        \"ENABLE_SCREEN_DES\": \"false\"
      },
      \"LoiteringAreas\": \"[{\\\"id\\\":\\\"area1\\\",\\\"name\\\":\\\"Full Frame Zone\\\",\\\"coordinates\\\":[{\\\"x\":0,\\\"y\":0},{\\\"x\":640,\\\"y\":0},{\\\"x\":640,\\\"y\":480},{\\\"x\":0,\\\"y\":480}],\\\"seconds\":5,\\\"classes\\\":[\\\"Person\\\"],\\\"color\\\":[255,0,0,255]}]\"\"
    }
  }")

echo "$RESPONSE" | jq .
INSTANCE_ID=$(echo "$RESPONSE" | jq -r '.instanceId')
echo "Instance ID: $INSTANCE_ID"
```

**Expected**: Status 201 Created với RTSP URL làm input source.

### 1.5: Kiểm tra Instance đã được tạo với Loitering Areas

```bash
echo "=== Test 1.4: Kiểm tra Instance Config ==="

curl -X GET "${CORE_BASE}/${INSTANCE_ID}/config" \
  -H "Content-Type: application/json" | jq '.AdditionalParams.LoiteringAreas'
```

**Expected**: Response chứa `LoiteringAreas` trong `AdditionalParams`.

---

## Test Case 2: Workflow Hoàn Chỉnh

### 2.1: Workflow Test Script

```bash
#!/bin/bash

echo "=========================================="
echo "Loitering Detection - Core API Test"
echo "=========================================="

# Setup
SERVER="http://localhost:8080"
CORE_BASE="${SERVER}/v1/core/instance"
VIDEO_FILE="./test_video.mp4"  # Thay bằng đường dẫn thực tế

# Step 1: Tạo instance với loitering areas
echo ""
echo "Step 1: Tạo Instance với Loitering Areas"
RESPONSE=$(curl -s -X POST "${CORE_BASE}" \
  -H "Content-Type: application/json" \
  -d "{
    \"solution\": \"securt\",
    \"name\": \"Loitering E2E Test\",
    \"group\": \"demo\",
    \"autoStart\": false,
    \"detectorMode\": \"SmartDetection\",
    \"detectionSensitivity\": \"Medium\",
    \"additionalParams\": {
      \"input\": {
        \"FILE_PATH\": \"${VIDEO_FILE}\",
        \"WEIGHTS_PATH\": \"/opt/omniapi/models/det_cls/yolov3-tiny-2022-0721_best.weights\",
        \"CONFIG_PATH\": \"/opt/omniapi/models/det_cls/yolov3-tiny-2022-0721.cfg\",
        \"LABELS_PATH\": \"/opt/omniapi/models/det_cls/yolov3_tiny_5classes.txt\",
        \"RESIZE_RATIO\": \"0.6\"
      },
      \"output\": {
        \"ENABLE_SCREEN_DES\": \"false\"
      },
      \"LoiteringAreas\": \"[{\\\"coordinates\\\":[{\\\"x\":100,\\\"y\":100},{\\\"x\":500,\\\"y\":100},{\\\"x\":500,\\\"y\":400},{\\\"x\":100,\\\"y\":400}],\\\"seconds\":5,\\\"name\\\":\\\"Zone 1\\\"},{\\\"coordinates\\\":[{\\\"x\":600,\\\"y\":200},{\\\"x\":800,\\\"y\":200},{\\\"x\":800,\\\"y\":500},{\\\"x\":600,\\\"y\":500}],\\\"seconds\":10,\\\"name\\\":\\\"Zone 2\\\"}]\"\"
    }
  }")

INSTANCE_ID=$(echo "$RESPONSE" | jq -r '.instanceId')
echo "Created instance: $INSTANCE_ID"

if [ -z "$INSTANCE_ID" ] || [ "$INSTANCE_ID" = "null" ]; then
  echo "ERROR: Failed to create instance"
  echo "$RESPONSE" | jq .
  exit 1
fi

# Step 2: Kiểm tra instance config
echo ""
echo "Step 2: Kiểm tra Instance Config"
curl -s -X GET "${CORE_BASE}/${INSTANCE_ID}/config" | jq '.AdditionalParams.LoiteringAreas'

# Step 3: Start instance
echo ""
echo "Step 3: Start Instance"
START_RESPONSE=$(curl -s -X POST "${CORE_BASE}/${INSTANCE_ID}/start")
echo "$START_RESPONSE" | jq .

# Step 4: Đợi instance khởi động
echo ""
echo "Step 4: Đợi instance khởi động (5 giây)..."
sleep 5

# Step 5: Kiểm tra instance status
echo ""
echo "Step 5: Kiểm tra Instance Status"
curl -s -X GET "${CORE_BASE}/${INSTANCE_ID}" | jq '.running'

# Step 6: Kiểm tra statistics (nếu có SecuRT stats endpoint)
echo ""
echo "Step 6: Kiểm tra Statistics"
SECURT_BASE="${SERVER}/v1/securt/instance"
curl -s -X GET "${SECURT_BASE}/${INSTANCE_ID}/stats" | jq . 2>/dev/null || echo "Stats endpoint not available"

# Step 7: Kiểm tra analytics entities
echo ""
echo "Step 7: Kiểm tra Analytics Entities"
curl -s -X GET "${SECURT_BASE}/${INSTANCE_ID}/analytics_entities" | jq '.loiteringAreas' 2>/dev/null || echo "Analytics entities endpoint not available"

# Step 8: Stop instance
echo ""
echo "Step 8: Stop Instance"
curl -s -X POST "${CORE_BASE}/${INSTANCE_ID}/stop" | jq .

# Step 9: Xóa instance
echo ""
echo "Step 9: Xóa Instance"
curl -s -X DELETE "${CORE_BASE}/${INSTANCE_ID}" -v

echo ""
echo "=========================================="
echo "Workflow Test Completed"
echo "=========================================="
```

**Lưu script trên vào file `test_loitering_core_api.sh`, chmod +x và chạy**:
```bash
chmod +x test_loitering_core_api.sh
./test_loitering_core_api.sh
```

### 2.2: Test với Multiple Loitering Areas

```bash
echo "=== Test 2.2: Tạo Instance với Multiple Loitering Areas ==="

# Tạo JSON array với nhiều areas
LOITERING_AREAS='[
  {
    "coordinates": [{"x": 50, "y": 50}, {"x": 300, "y": 50}, {"x": 300, "y": 250}, {"x": 50, "y": 250}],
    "seconds": 5,
    "name": "Zone A",
    "channel": 0
  },
  {
    "coordinates": [{"x": 350, "y": 50}, {"x": 600, "y": 50}, {"x": 600, "y": 250}, {"x": 350, "y": 250}],
    "seconds": 8,
    "name": "Zone B",
    "channel": 0
  },
  {
    "coordinates": [{"x": 50, "y": 300}, {"x": 300, "y": 300}, {"x": 300, "y": 500}, {"x": 50, "y": 500}],
    "seconds": 10,
    "name": "Zone C",
    "channel": 0
  }
]'

# Convert to JSON string (escape quotes)
LOITERING_AREAS_STR=$(echo "$LOITERING_AREAS" | jq -c . | sed 's/"/\\"/g')

RESPONSE=$(curl -s -X POST "${CORE_BASE}" \
  -H "Content-Type: application/json" \
  -d "{
    \"solution\": \"securt\",
    \"name\": \"Multi-Zone Loitering Test\",
    \"group\": \"demo\",
    \"autoStart\": false,
    \"detectorMode\": \"SmartDetection\",
    \"additionalParams\": {
      \"input\": {
        \"FILE_PATH\": \"./test_video.mp4\",
        \"WEIGHTS_PATH\": \"/opt/omniapi/models/det_cls/yolov3-tiny-2022-0721_best.weights\",
        \"CONFIG_PATH\": \"/opt/omniapi/models/det_cls/yolov3-tiny-2022-0721.cfg\",
        \"LABELS_PATH\": \"/opt/omniapi/models/det_cls/yolov3_tiny_5classes.txt\",
        \"RESIZE_RATIO\": \"0.6\"
      },
      \"output\": {
        \"ENABLE_SCREEN_DES\": \"false\"
      },
      \"LoiteringAreas\": \"${LOITERING_AREAS_STR}\"
    }
  }")

echo "$RESPONSE" | jq .
INSTANCE_ID=$(echo "$RESPONSE" | jq -r '.instanceId')
echo "Instance ID: $INSTANCE_ID"
```

**Expected**: Instance được tạo với 3 loitering areas, mỗi area có threshold time khác nhau.

---

## Expected Results

### Tổng Quan

Sau khi hoàn thành workflow, bạn nên thấy pipeline structure **khớp với `ba_loitering_sample.cpp`**:

1. **Instance được tạo thành công** với `LoiteringAreas` trong `additionalParams`
2. **Loitering areas được convert** từ polygon coordinates sang bounding rectangles (`cvedix_rect` format)
3. **Pipeline structure đúng**: `file_src` → `yolo_detector` → `sort_track` → `ba_loitering` → `ba_loitering_osd` → `screen_des`/`file_des`
4. **ba_loitering node được tự động tạo** trong pipeline với:
   - Regions map: `std::map<int, cvedix_rect>` (khớp với sample code)
   - Alarm seconds map: `std::map<int, double>` (khớp với sample code)
   - Frame rate: 30 fps (khớp với sample code)
5. **ba_loitering_osd node được tự động tạo** sử dụng `cvedix_ba_stop_osd_node` (khớp với sample code)
6. **Instance chạy thành công** và xử lý video
7. **Loitering events được phát hiện** khi objects ở trong areas quá thời gian threshold

### Kiểm tra Pipeline Nodes

```bash
# Kiểm tra instance có ba_loitering node không
curl -s -X GET "${CORE_BASE}/${INSTANCE_ID}/config" | jq '.Pipeline | map(select(.NodeType == "ba_loitering"))'

# Kiểm tra instance có ba_loitering_osd node không
curl -s -X GET "${CORE_BASE}/${INSTANCE_ID}/config" | jq '.Pipeline | map(select(.NodeType == "ba_loitering_osd"))'
```

**Expected**: Cả hai node types đều có trong pipeline.

### Kiểm tra Loitering Areas trong Analytics Entities

```bash
SECURT_BASE="${SERVER}/v1/securt/instance"
curl -s -X GET "${SECURT_BASE}/${INSTANCE_ID}/analytics_entities" | jq '.loiteringAreas'
```

**Expected**: Response chứa array các loitering areas với đầy đủ thông tin (id, name, coordinates, seconds).

---

## Troubleshooting

### Vấn đề 1: Loitering areas không được tạo

**Triệu chứng**: 
- Instance được tạo nhưng không có `LoiteringAreas` trong config
- ba_loitering node không xuất hiện trong pipeline

**Giải pháp**:
1. Kiểm tra JSON format của `LoiteringAreas`:
   ```bash
   # Validate JSON trước khi gửi
   echo '[{"coordinates":[{"x":100,"y":100},{"x":500,"y":100},{"x":500,"y":400},{"x":100,"y":400}],"seconds":5}]' | jq .
   ```
2. Đảm bảo `LoiteringAreas` là một JSON string (escaped) trong `additionalParams`
3. Kiểm tra server logs để xem có lỗi parsing không

### Vấn đề 2: Loitering detection không hoạt động

**Triệu chứng**:
- Instance chạy nhưng không có loitering events
- Statistics không hiển thị loitering detections

**Giải pháp**:
1. Kiểm tra coordinates của areas có nằm trong frame không:
   ```bash
   # Lấy frame size từ instance config
   curl -s -X GET "${CORE_BASE}/${INSTANCE_ID}/config" | jq '.Input'
   ```
2. Đảm bảo `seconds` threshold không quá cao (thử giảm xuống 3-5 giây)
3. Kiểm tra có objects được detect không:
   ```bash
   curl -s -X GET "${SECURT_BASE}/${INSTANCE_ID}/stats" | jq '.trackCount'
   ```
4. Kiểm tra ba_loitering node có trong pipeline:
   ```bash
   curl -s -X GET "${CORE_BASE}/${INSTANCE_ID}/config" | jq '.Pipeline | map(select(.NodeType == "ba_loitering"))'
   ```

### Vấn đề 3: Coordinates không đúng

**Triệu chứng**:
- Areas được tạo nhưng vị trí không đúng
- Normalized coordinates không được convert đúng

**Giải pháp**:
1. Sử dụng pixel coordinates thay vì normalized:
   ```json
   {"x": 100, "y": 100}  // Pixel coordinates
   ```
   thay vì:
   ```json
   {"x": 0.1, "y": 0.1}  // Normalized (có thể không chính xác)
   ```
2. Kiểm tra frame size và đảm bảo coordinates nằm trong phạm vi:
   - Width: 0 đến frame_width
   - Height: 0 đến frame_height

### Vấn đề 4: Instance không start được

**Triệu chứng**:
- Status code 500 khi start
- Instance không chuyển sang trạng thái `running`

**Giải pháp**:
1. Kiểm tra input đã được cấu hình chưa:
   ```bash
   curl "${CORE_BASE}/${INSTANCE_ID}/config" | jq '.Input'
   ```
2. Kiểm tra logs của server để xem lỗi chi tiết
3. Đảm bảo video file hoặc RTSP URL hợp lệ
4. Kiểm tra YOLO model paths có đúng không

---

## Test Checklist

Sử dụng checklist này để đảm bảo đã test đầy đủ:

- [ ] Tạo instance với loitering areas (rectangle format)
- [ ] Tạo instance với loitering areas (normalized coordinates)
- [ ] Tạo instance với RTSP input và loitering areas
- [ ] Tạo instance với multiple loitering areas
- [ ] Kiểm tra LoiteringAreas trong instance config
- [ ] Kiểm tra ba_loitering node trong pipeline
- [ ] Kiểm tra ba_loitering_osd node trong pipeline
- [ ] Start instance thành công
- [ ] Kiểm tra instance statistics
- [ ] Kiểm tra loitering areas trong analytics entities
- [ ] Verify loitering detection hoạt động (nếu có video input)
- [ ] Stop instance
- [ ] Xóa instance

---

## Notes

1. **Pipeline Structure (Khớp với Sample Code)**: 
   - Pipeline phải là: `file_src` → `yolo_detector` → `sort_track` → `ba_loitering` → `ba_loitering_osd` → `screen_des`/`file_des`
   - Khớp với `ba_loitering_sample.cpp` lines 51-56

2. **Format Structure**: `additionalParams` nên sử dụng cấu trúc với `input` và `output` sections:
   ```json
   {
     "additionalParams": {
       "input": {
         "FILE_PATH": "...",
         "WEIGHTS_PATH": "...",
         ...
       },
       "output": {
         "RTMP_DES_URL": "...",
         ...
       },
       "LoiteringAreas": "[...]"  // Top-level trong additionalParams
     }
   }
   ```

3. **JSON String Escaping**: `LoiteringAreas` phải là một JSON string (escaped) trong `additionalParams`, không phải JSON object trực tiếp.

4. **Coordinate Format (Khớp với Sample Code)**: 
   - Polygon coordinates sẽ được convert thành bounding rectangles `cvedix_rect(x, y, width, height)`
   - **Ví dụ từ sample code**: `cvedix_rect(20,30,580,270)` tương đương với polygon `[{x:20,y:30}, {x:600,y:30}, {x:600,y:300}, {x:20,y:300}]`
   - Hỗ trợ cả pixel coordinates và normalized coordinates (0.0-1.0)

5. **Node Parameters (Khớp với Sample Code)**:
   - `ba_loitering_node` được tạo với: `(nodeName, regions, alarm_seconds, 30)` - khớp với sample code line 43
   - `regions`: `std::map<int, cvedix_rect>` - khớp với sample code lines 36-38
   - `alarm_seconds`: `std::map<int, double>` - khớp với sample code lines 40-42
   - Frame rate: 30 fps - khớp với sample code line 43

6. **OSD Node (Khớp với Sample Code)**:
   - `ba_loitering_osd` sử dụng `cvedix_ba_stop_osd_node` internally - khớp với sample code line 45

7. **Auto Node Creation**: ba_loitering và ba_loitering_osd nodes sẽ được tự động tạo khi có `LoiteringAreas` trong request.

8. **Threshold Time**: `seconds` field xác định thời gian (giây) object phải ở trong area để trigger loitering alarm. Default là 5 giây - khớp với sample code line 41.

---

## References

- [Core API Documentation](../../../api-specs/openapi.yaml)
- [SecuRT API Documentation](../../../api-specs/openapi.yaml)
- **[Loitering Sample Code](../../../sample/ba_loitering_sample.cpp)** - **Pipeline chuẩn để so sánh**
  - Pipeline structure: lines 51-56
  - Regions format: lines 36-38 (`cvedix_rect(20,30,580,270)`)
  - Alarm seconds: lines 40-42 (`{0,5}`)
  - Node creation: line 43 (`ba_loitering_node` với frame rate 30)
  - OSD node: line 45 (`cvedix_ba_stop_osd_node`)

