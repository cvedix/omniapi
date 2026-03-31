# Hướng Dẫn Test Thủ Công - BA Loitering Solution

## Mục Lục

1. [Tổng Quan](#tổng-quan)
2. [Prerequisites](#prerequisites)
3. [Setup Variables](#setup-variables)
4. [Test Case 1: Kiểm Tra Solution Đã Được Đăng Ký](#test-case-1-kiểm-tra-solution-đã-được-đăng-ký)
5. [Test Case 2: Tạo Instance với ba_loitering Solution](#test-case-2-tạo-instance-với-ba_loitering-solution)
6. [Test Case 3: Tạo Instance với LoiteringAreas từ additionalParams](#test-case-3-tạo-instance-với-loiteringareas-từ-additionalparams)
7. [Test Case 4: Tạo Instance với LOITERING_AREAS_JSON Placeholder](#test-case-4-tạo-instance-với-loitering_areas_json-placeholder)
8. [Test Case 5: Tạo Instance với Multiple Loitering Areas](#test-case-5-tạo-instance-với-multiple-loitering-areas)
9. [Test Case 6: Tạo Instance với RTMP Input/Output và Nested Structure](#test-case-6-tạo-instance-với-rtmp-inputoutput-và-nested-structure)
10. [Test Case 7: Kiểm Tra Instance Status và Events](#test-case-7-kiểm-tra-instance-status-và-events)
11. [Expected Results](#expected-results)
12. [Troubleshooting](#troubleshooting)

---

## Tổng Quan

Tài liệu này hướng dẫn test thủ công **BA Loitering Solution** (`ba_loitering`) - một solution mới được thêm vào hệ thống để phát hiện loitering (đối tượng ở lại quá lâu trong một khu vực).

**Solution ID**: `ba_loitering`

**Pipeline Structure**:
```
file_src → yolo_detector → sort_track → ba_loitering → ba_loitering_osd → screen_des/file_des
```

**Tính năng chính**:
- Phát hiện đối tượng ở lại trong khu vực quá lâu (loitering detection)
- Hỗ trợ multiple loitering areas với alarm seconds khác nhau
- Tự động visualize regions trên video frames
- Hỗ trợ MQTT event publishing (optional)

### Base URLs
```
Solution API:  http://localhost:8080/v1/core/solution
Instance API:  http://localhost:8080/v1/core/instance
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
   - Video file: `./cvedix_data/test_video/vehicle_stop.mp4` (hoặc video tương tự)
   - YOLO model files:
     - Weights: `./cvedix_data/models/det_cls/yolov3-tiny-2022-0721_best.weights`
     - Config: `./cvedix_data/models/det_cls/yolov3-tiny-2022-0721.cfg`
     - Labels: `./cvedix_data/models/det_cls/yolov3_tiny_5classes.txt`

---

## Setup Variables

```bash
# Thay đổi các giá trị này theo môi trường của bạn
SERVER="http://localhost:8080"
SOLUTION_API="${SERVER}/v1/core/solution"
INSTANCE_API="${SERVER}/v1/core/instance"

# Solution ID
SOLUTION_ID="ba_loitering"

# Instance ID sẽ được tạo tự động
INSTANCE_ID=""  # Sẽ được set sau khi tạo instance

# Test resources
VIDEO_FILE="./cvedix_data/test_video/vehicle_stop.mp4"
WEIGHTS_PATH="./cvedix_data/models/det_cls/yolov3-tiny-2022-0721_best.weights"
CONFIG_PATH="./cvedix_data/models/det_cls/yolov3-tiny-2022-0721.cfg"
LABELS_PATH="./cvedix_data/models/det_cls/yolov3_tiny_5classes.txt"
```

---

## Test Case 1: Kiểm Tra Solution Đã Được Đăng Ký

### Mục đích
Xác nhận rằng `ba_loitering` solution đã được đăng ký và có sẵn trong hệ thống.

### Các bước

1. **List tất cả solutions**:
   ```bash
   curl -X GET "${SOLUTION_API}" | jq '.'
   ```

2. **Tìm ba_loitering solution**:
   ```bash
   curl -X GET "${SOLUTION_API}" | jq '.solutions[] | select(.solutionId == "ba_loitering")'
   ```

3. **Get solution details**:
   ```bash
   curl -X GET "${SOLUTION_API}/${SOLUTION_ID}" | jq '.'
   ```

4. **Get solution parameters**:
   ```bash
   curl -X GET "${SOLUTION_API}/${SOLUTION_ID}/parameters" | jq '.'
   ```

5. **Get instance body template**:
   ```bash
   curl -X GET "${SOLUTION_API}/${SOLUTION_ID}/instance-body" | jq '.'
   ```

### Expected Results

- Solution `ba_loitering` xuất hiện trong danh sách solutions
- `isDefault` = `true`
- `solutionType` = `"behavior_analysis"`
- Pipeline có các nodes: `file_src`, `yolo_detector`, `sort_track`, `ba_loitering`, `ba_loitering_osd`, `screen_des`, `file_des`
- Instance body template có các placeholders: `${FILE_PATH}`, `${WEIGHTS_PATH}`, `${CONFIG_PATH}`, `${LABELS_PATH}`, `${LOITERING_AREAS_JSON}`, `${ALARM_SECONDS}`

---

## Test Case 2: Tạo Instance với ba_loitering Solution

### Mục đích
Tạo instance sử dụng `ba_loitering` solution với cấu hình cơ bản.

### Request Body

```json
{
  "name": "test_ba_loitering_basic",
  "solution": "ba_loitering",
  "additionalParams": {
    "FILE_PATH": "./cvedix_data/test_video/vehicle_stop.mp4",
    "WEIGHTS_PATH": "./cvedix_data/models/det_cls/yolov3-tiny-2022-0721_best.weights",
    "CONFIG_PATH": "./cvedix_data/models/det_cls/yolov3-tiny-2022-0721.cfg",
    "LABELS_PATH": "./cvedix_data/models/det_cls/yolov3_tiny_5classes.txt",
    "RESIZE_RATIO": "0.6",
    "ENABLE_SCREEN_DES": "false"
  }
}
```

### Các bước

1. **Tạo instance**:
   ```bash
   INSTANCE_ID=$(curl -X POST "${INSTANCE_API}" \
     -H "Content-Type: application/json" \
     -d '{
       "name": "test_ba_loitering_basic",
       "solution": "ba_loitering",
       "additionalParams": {
         "FILE_PATH": "./cvedix_data/test_video/vehicle_stop.mp4",
         "WEIGHTS_PATH": "./cvedix_data/models/det_cls/yolov3-tiny-2022-0721_best.weights",
         "CONFIG_PATH": "./cvedix_data/models/det_cls/yolov3-tiny-2022-0721.cfg",
         "LABELS_PATH": "./cvedix_data/models/det_cls/yolov3_tiny_5classes.txt",
         "RESIZE_RATIO": "0.6",
         "ENABLE_SCREEN_DES": "false"
       }
     }' | jq -r '.instanceId')
   
   echo "Instance ID: ${INSTANCE_ID}"
   ```

2. **Kiểm tra instance status**:
   ```bash
   curl -X GET "${INSTANCE_API}/${INSTANCE_ID}" | jq '.'
   ```

3. **Start instance**:
   ```bash
   curl -X POST "${INSTANCE_API}/${INSTANCE_ID}/start" | jq '.'
   ```

4. **Kiểm tra instance đang chạy**:
   ```bash
   curl -X GET "${INSTANCE_API}/${INSTANCE_ID}" | jq '.status'
   ```

5. **Stop instance**:
   ```bash
   curl -X POST "${INSTANCE_API}/${INSTANCE_ID}/stop" | jq '.'
   ```

6. **Delete instance**:
   ```bash
   curl -X DELETE "${INSTANCE_API}/${INSTANCE_ID}" | jq '.'
   ```

### Expected Results

- Instance được tạo thành công với `status` = `"stopped"`
- Instance có `solutionId` = `"ba_loitering"`
- Instance start thành công và `status` = `"running"`
- Pipeline được build đúng với các nodes: `file_src`, `yolo_detector`, `sort_track`, `ba_loitering`, `ba_loitering_osd`, `file_des`
- Không có loitering areas được cấu hình (node sẽ được tạo nhưng không có regions)

---

## Test Case 3: Tạo Instance với LoiteringAreas từ additionalParams

### Mục đích
Tạo instance với loitering areas được định nghĩa trực tiếp trong `additionalParams["LoiteringAreas"]`.

### Request Body

```json
{
  "name": "test_ba_loitering_with_areas",
  "solution": "ba_loitering",
  "additionalParams": {
    "FILE_PATH": "./cvedix_data/test_video/vehicle_stop.mp4",
    "WEIGHTS_PATH": "./cvedix_data/models/det_cls/yolov3-tiny-2022-0721_best.weights",
    "CONFIG_PATH": "./cvedix_data/models/det_cls/yolov3-tiny-2022-0721.cfg",
    "LABELS_PATH": "./cvedix_data/models/det_cls/yolov3_tiny_5classes.txt",
    "RESIZE_RATIO": "0.6",
    "ENABLE_SCREEN_DES": "false",
    "LoiteringAreas": "[{\"coordinates\":[{\"x\":20,\"y\":30},{\"x\":600,\"y\":30},{\"x\":600,\"y\":300},{\"x\":20,\"y\":300}],\"seconds\":5.0,\"channel\":0}]"
  }
}
```

### Các bước

1. **Tạo instance với loitering areas**:
   ```bash
   INSTANCE_ID=$(curl -X POST "${INSTANCE_API}" \
     -H "Content-Type: application/json" \
     -d '{
       "name": "test_ba_loitering_with_areas",
       "solution": "ba_loitering",
       "additionalParams": {
         "FILE_PATH": "./cvedix_data/test_video/vehicle_stop.mp4",
         "WEIGHTS_PATH": "./cvedix_data/models/det_cls/yolov3-tiny-2022-0721_best.weights",
         "CONFIG_PATH": "./cvedix_data/models/det_cls/yolov3-tiny-2022-0721.cfg",
         "LABELS_PATH": "./cvedix_data/models/det_cls/yolov3_tiny_5classes.txt",
         "RESIZE_RATIO": "0.6",
         "ENABLE_SCREEN_DES": "false",
         "LoiteringAreas": "[{\"coordinates\":[{\"x\":20,\"y\":30},{\"x\":600,\"y\":30},{\"x\":600,\"y\":300},{\"x\":20,\"y\":300}],\"seconds\":5.0,\"channel\":0}]"
       }
     }' | jq -r '.instanceId')
   
   echo "Instance ID: ${INSTANCE_ID}"
   ```

2. **Kiểm tra instance được tạo với regions**:
   ```bash
   curl -X GET "${INSTANCE_API}/${INSTANCE_ID}" | jq '.'
   ```

3. **Start instance**:
   ```bash
   curl -X POST "${INSTANCE_API}/${INSTANCE_ID}/start" | jq '.'
   ```

4. **Kiểm tra output video** (nếu có file_des):
   ```bash
   ls -lh ./output/${INSTANCE_ID}/
   ```

5. **Stop và delete instance**:
   ```bash
   curl -X POST "${INSTANCE_API}/${INSTANCE_ID}/stop" | jq '.'
   curl -X DELETE "${INSTANCE_API}/${INSTANCE_ID}" | jq '.'
   ```

### Expected Results

- Instance được tạo thành công
- `ba_loitering` node được tạo với 1 region (rect từ polygon coordinates)
- Region có alarm_seconds = 5.0
- OSD node vẽ region trên video frames
- Output video có region được vẽ (nếu có file_des)

---

## Test Case 4: Tạo Instance với LOITERING_AREAS_JSON Placeholder

### Mục đích
Test việc sử dụng placeholder `${LOITERING_AREAS_JSON}` trong solution config và substitute từ `additionalParams`.

### Request Body

```json
{
  "name": "test_ba_loitering_placeholder",
  "solution": "ba_loitering",
  "additionalParams": {
    "FILE_PATH": "./cvedix_data/test_video/vehicle_stop.mp4",
    "WEIGHTS_PATH": "./cvedix_data/models/det_cls/yolov3-tiny-2022-0721_best.weights",
    "CONFIG_PATH": "./cvedix_data/models/det_cls/yolov3-tiny-2022-0721.cfg",
    "LABELS_PATH": "./cvedix_data/models/det_cls/yolov3_tiny_5classes.txt",
    "RESIZE_RATIO": "0.6",
    "ENABLE_SCREEN_DES": "false",
    "LOITERING_AREAS_JSON": "[{\"coordinates\":[{\"x\":20,\"y\":30},{\"x\":600,\"y\":30},{\"x\":600,\"y\":300},{\"x\":20,\"y\":300}],\"seconds\":5.0,\"channel\":0}]",
    "ALARM_SECONDS": "5"
  }
}
```

### Các bước

1. **Tạo instance với placeholder**:
   ```bash
   INSTANCE_ID=$(curl -X POST "${INSTANCE_API}" \
     -H "Content-Type: application/json" \
     -d '{
       "name": "test_ba_loitering_placeholder",
       "solution": "ba_loitering",
       "additionalParams": {
         "FILE_PATH": "./cvedix_data/test_video/vehicle_stop.mp4",
         "WEIGHTS_PATH": "./cvedix_data/models/det_cls/yolov3-tiny-2022-0721_best.weights",
         "CONFIG_PATH": "./cvedix_data/models/det_cls/yolov3-tiny-2022-0721.cfg",
         "LABELS_PATH": "./cvedix_data/models/det_cls/yolov3_tiny_5classes.txt",
         "RESIZE_RATIO": "0.6",
         "ENABLE_SCREEN_DES": "false",
         "LOITERING_AREAS_JSON": "[{\"coordinates\":[{\"x\":20,\"y\":30},{\"x\":600,\"y\":30},{\"x\":600,\"y\":300},{\"x\":20,\"y\":300}],\"seconds\":5.0,\"channel\":0}]",
         "ALARM_SECONDS": "5"
       }
     }' | jq -r '.instanceId')
   
   echo "Instance ID: ${INSTANCE_ID}"
   ```

2. **Kiểm tra instance**:
   ```bash
   curl -X GET "${INSTANCE_API}/${INSTANCE_ID}" | jq '.'
   ```

3. **Start instance**:
   ```bash
   curl -X POST "${INSTANCE_API}/${INSTANCE_ID}/start" | jq '.'
   ```

4. **Stop và delete instance**:
   ```bash
   curl -X POST "${INSTANCE_API}/${INSTANCE_ID}/stop" | jq '.'
   curl -X DELETE "${INSTANCE_API}/${INSTANCE_ID}" | jq '.'
   ```

### Expected Results

- Instance được tạo thành công
- Placeholder `${LOITERING_AREAS_JSON}` được substitute từ `additionalParams["LOITERING_AREAS_JSON"]`
- `ba_loitering` node được tạo với regions từ substituted value
- Placeholder `${ALARM_SECONDS}` được substitute từ `additionalParams["ALARM_SECONDS"]`

---

## Test Case 5: Tạo Instance với Multiple Loitering Areas

### Mục đích
Test việc tạo instance với nhiều loitering areas, mỗi area có alarm seconds khác nhau.

### Request Body

```json
{
  "name": "test_ba_loitering_multiple_areas",
  "solution": "ba_loitering",
  "additionalParams": {
    "FILE_PATH": "./cvedix_data/test_video/vehicle_stop.mp4",
    "WEIGHTS_PATH": "./cvedix_data/models/det_cls/yolov3-tiny-2022-0721_best.weights",
    "CONFIG_PATH": "./cvedix_data/models/det_cls/yolov3-tiny-2022-0721.cfg",
    "LABELS_PATH": "./cvedix_data/models/det_cls/yolov3_tiny_5classes.txt",
    "RESIZE_RATIO": "0.6",
    "ENABLE_SCREEN_DES": "false",
    "LoiteringAreas": "[{\"coordinates\":[{\"x\":20,\"y\":30},{\"x\":300,\"y\":30},{\"x\":300,\"y\":200},{\"x\":20,\"y\":200}],\"seconds\":3.0,\"channel\":0},{\"coordinates\":[{\"x\":350,\"y\":50},{\"x\":600,\"y\":50},{\"x\":600,\"y\":300},{\"x\":350,\"y\":300}],\"seconds\":5.0,\"channel\":0}]"
  }
}
```

### Các bước

1. **Tạo instance với multiple areas**:
   ```bash
   INSTANCE_ID=$(curl -X POST "${INSTANCE_API}" \
     -H "Content-Type: application/json" \
     -d '{
       "name": "test_ba_loitering_multiple_areas",
       "solution": "ba_loitering",
       "additionalParams": {
         "FILE_PATH": "./cvedix_data/test_video/vehicle_stop.mp4",
         "WEIGHTS_PATH": "./cvedix_data/models/det_cls/yolov3-tiny-2022-0721_best.weights",
         "CONFIG_PATH": "./cvedix_data/models/det_cls/yolov3-tiny-2022-0721.cfg",
         "LABELS_PATH": "./cvedix_data/models/det_cls/yolov3_tiny_5classes.txt",
         "RESIZE_RATIO": "0.6",
         "ENABLE_SCREEN_DES": "false",
         "LoiteringAreas": "[{\"coordinates\":[{\"x\":20,\"y\":30},{\"x\":300,\"y\":30},{\"x\":300,\"y\":200},{\"x\":20,\"y\":200}],\"seconds\":3.0,\"channel\":0},{\"coordinates\":[{\"x\":350,\"y\":50},{\"x\":600,\"y\":50},{\"x\":600,\"y\":300},{\"x\":350,\"y\":300}],\"seconds\":5.0,\"channel\":0}]"
       }
     }' | jq -r '.instanceId')
   
   echo "Instance ID: ${INSTANCE_ID}"
   ```

2. **Kiểm tra instance**:
   ```bash
   curl -X GET "${INSTANCE_API}/${INSTANCE_ID}" | jq '.'
   ```

3. **Start instance**:
   ```bash
   curl -X POST "${INSTANCE_API}/${INSTANCE_ID}/start" | jq '.'
   ```

4. **Kiểm tra output**:
   ```bash
   ls -lh ./output/${INSTANCE_ID}/
   ```

5. **Stop và delete instance**:
   ```bash
   curl -X POST "${INSTANCE_API}/${INSTANCE_ID}/stop" | jq '.'
   curl -X DELETE "${INSTANCE_API}/${INSTANCE_ID}" | jq '.'
   ```

### Expected Results

- Instance được tạo thành công
- `ba_loitering` node được tạo với 2 regions
- Region 1 có alarm_seconds = 3.0
- Region 2 có alarm_seconds = 5.0
- OSD node vẽ cả 2 regions trên video frames
- Output video có cả 2 regions được vẽ

---

## Test Case 6: Tạo Instance với RTMP Input/Output và Nested Structure

### Mục đích
Test việc tạo instance với RTMP source/destination và nested structure `input`/`output` trong `additionalParams`.

### Request Body

```json
{
  "name": "ba_loitering_rtmp",
  "group": "demo",
  "solution": "ba_loitering",
  "autoStart": false,
  "additionalParams": {
    "input": {
      "RTMP_SRC_URL": "rtmp://192.168.1.128:1935/live/camera_demo_sang_vehicle",
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
    "LOITERING_AREAS_JSON": "[{\"coordinates\":[{\"x\":20,\"y\":30},{\"x\":600,\"y\":30},{\"x\":600,\"y\":300},{\"x\":20,\"y\":300}],\"seconds\":5.0,\"channel\":0}]",
    "ALARM_SECONDS": "5"
  }
}
```

### Các bước

1. **Tạo instance với RTMP và nested structure**:
   ```bash
   INSTANCE_ID=$(curl -X POST "${INSTANCE_API}" \
     -H "Content-Type: application/json" \
     -d '{
       "name": "ba_loitering_rtmp",
       "group": "demo",
       "solution": "ba_loitering",
       "autoStart": false,
       "additionalParams": {
         "input": {
           "RTMP_SRC_URL": "rtmp://192.168.1.128:1935/live/camera_demo_sang_vehicle",
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
         "LOITERING_AREAS_JSON": "[{\"coordinates\":[{\"x\":20,\"y\":30},{\"x\":600,\"y\":30},{\"x\":600,\"y\":300},{\"x\":20,\"y\":300}],\"seconds\":5.0,\"channel\":0}]",
         "ALARM_SECONDS": "5"
       }
     }' | jq -r '.instanceId')
   
   echo "Instance ID: ${INSTANCE_ID}"
   ```

2. **Kiểm tra instance được tạo**:
   ```bash
   curl -X GET "${INSTANCE_API}/${INSTANCE_ID}" | jq '.'
   ```

3. **Kiểm tra pipeline structure** (phải có `rtmp_src` và `rtmp_des`):
   ```bash
   curl -X GET "${INSTANCE_API}/${INSTANCE_ID}/config" | jq '.Pipeline | map({NodeType, NodeName})'
   ```

4. **Start instance**:
   ```bash
   curl -X POST "${INSTANCE_API}/${INSTANCE_ID}/start" | jq '.'
   ```

5. **Kiểm tra instance đang chạy**:
   ```bash
   curl -X GET "${INSTANCE_API}/${INSTANCE_ID}" | jq '.status'
   ```

6. **Stop và delete instance**:
   ```bash
   curl -X POST "${INSTANCE_API}/${INSTANCE_ID}/stop" | jq '.'
   curl -X DELETE "${INSTANCE_API}/${INSTANCE_ID}" | jq '.'
   ```

### Expected Results

- Instance được tạo thành công
- Nested structure `input` và `output` được flatten thành flat structure trong `req.additionalParams`
- `RTMP_SRC_URL` từ `input` section được flatten thành `req.additionalParams["RTMP_SRC_URL"]`
- `RTMP_DES_URL` từ `output` section được flatten thành `req.additionalParams["RTMP_DES_URL"]`
- Top-level keys (`LOITERING_AREAS_JSON`, `ALARM_SECONDS`) được giữ lại
- Pipeline builder auto-detect `RTMP_SRC_URL` và convert `file_src` → `rtmp_src`
- Pipeline builder auto-add `rtmp_des` node khi có `RTMP_DES_URL`
- `ba_loitering` node được tạo với regions từ `LOITERING_AREAS_JSON`
- Pipeline structure: `rtmp_src` → `yolo_detector` → `sort_track` → `ba_loitering` → `ba_loitering_osd` → `rtmp_des`

### Lưu ý

- Nested structure `input`/`output` được hỗ trợ và sẽ được flatten tự động
- Top-level keys trong `additionalParams` (như `LOITERING_AREAS_JSON`) sẽ được giữ lại ngay cả khi có nested structure
- `RTMP_SRC_URL` có priority cao hơn `FILE_PATH` nếu cả hai đều được cung cấp
- `RTMP_DES_URL` sẽ tự động tạo `rtmp_des` node nếu chưa có trong pipeline

---

## Test Case 7: Kiểm Tra Instance Status và Events

### Mục đích
Kiểm tra instance status, events, và metrics khi instance đang chạy.

### Các bước

1. **Tạo và start instance** (sử dụng test case 3):
   ```bash
   # Tạo instance với loitering areas
   INSTANCE_ID=$(curl -X POST "${INSTANCE_API}" \
     -H "Content-Type: application/json" \
     -d '{
       "name": "test_ba_loitering_status",
       "solution": "ba_loitering",
       "additionalParams": {
         "FILE_PATH": "./cvedix_data/test_video/vehicle_stop.mp4",
         "WEIGHTS_PATH": "./cvedix_data/models/det_cls/yolov3-tiny-2022-0721_best.weights",
         "CONFIG_PATH": "./cvedix_data/models/det_cls/yolov3-tiny-2022-0721.cfg",
         "LABELS_PATH": "./cvedix_data/models/det_cls/yolov3_tiny_5classes.txt",
         "RESIZE_RATIO": "0.6",
         "ENABLE_SCREEN_DES": "false",
         "LoiteringAreas": "[{\"coordinates\":[{\"x\":20,\"y\":30},{\"x\":600,\"y\":30},{\"x\":600,\"y\":300},{\"x\":20,\"y\":300}],\"seconds\":5.0,\"channel\":0}]"
       }
     }' | jq -r '.instanceId')
   
   curl -X POST "${INSTANCE_API}/${INSTANCE_ID}/start" | jq '.'
   ```

2. **Kiểm tra instance status**:
   ```bash
   curl -X GET "${INSTANCE_API}/${INSTANCE_ID}" | jq '.status'
   ```

3. **Kiểm tra instance details**:
   ```bash
   curl -X GET "${INSTANCE_API}/${INSTANCE_ID}" | jq '.'
   ```

4. **Kiểm tra instance metrics** (nếu có):
   ```bash
   curl -X GET "${INSTANCE_API}/${INSTANCE_ID}/metrics" | jq '.' 2>/dev/null || echo "Metrics API not available"
   ```

5. **Kiểm tra events** (nếu có MQTT broker):
   ```bash
   # Events sẽ được publish qua MQTT nếu có json_mqtt_broker node
   # Kiểm tra MQTT broker logs hoặc subscribe to MQTT topic
   ```

6. **Stop và delete instance**:
   ```bash
   curl -X POST "${INSTANCE_API}/${INSTANCE_ID}/stop" | jq '.'
   curl -X DELETE "${INSTANCE_API}/${INSTANCE_ID}" | jq '.'
   ```

### Expected Results

- Instance status = `"running"` sau khi start
- Instance có đầy đủ thông tin: `instanceId`, `name`, `solutionId`, `status`, `createdAt`
- Metrics (nếu có) hiển thị thông tin về frames processed, detections, etc.
- Events (nếu có MQTT) được publish khi phát hiện loitering

---

## Expected Results

### Tổng Quan

Tất cả test cases trên phải:
- ✅ Tạo instance thành công
- ✅ Build pipeline đúng với các nodes: `file_src`, `yolo_detector`, `sort_track`, `ba_loitering`, `ba_loitering_osd`, `file_des`
- ✅ Parse loitering areas từ JSON đúng format
- ✅ Tạo `ba_loitering` node với regions và alarm_seconds
- ✅ Start instance thành công
- ✅ Process video và tạo output (nếu có file_des)
- ✅ Stop và delete instance thành công

### Pipeline Structure

Pipeline phải có cấu trúc:
```
file_src_{instanceId}
  └─→ yolo_detector_{instanceId}
      └─→ sort_tracker_{instanceId}
          └─→ ba_loitering_{instanceId}
              └─→ ba_loitering_osd_{instanceId} (uses ba_stop_osd_node)
                  └─→ file_des_{instanceId}
                  └─→ screen_des_{instanceId} (if enabled)
```

### Loitering Areas Format

Loitering areas phải được parse từ JSON format:
```json
[
  {
    "coordinates": [
      {"x": 20, "y": 30},
      {"x": 600, "y": 30},
      {"x": 600, "y": 300},
      {"x": 20, "y": 300}
    ],
    "seconds": 5.0,
    "channel": 0
  }
]
```

Polygon coordinates sẽ được convert thành bounding rectangle: `cvedix_rect(x, y, width, height)`

---

## Troubleshooting

### Vấn đề 1: Solution không tìm thấy

**Lỗi**: `Solution not found: ba_loitering`

**Nguyên nhân**: Solution chưa được đăng ký trong solution registry

**Giải pháp**:
1. Kiểm tra server logs để xem solution có được initialize không
2. Restart server để đảm bảo `initializeDefaultSolutions()` được gọi
3. Kiểm tra `src/solutions/solution_registry.cpp` có gọi `registerBALoiteringSolution()` không

### Vấn đề 2: Loitering areas không được parse

**Lỗi**: `No valid loitering configuration found`

**Nguyên nhân**: 
- JSON format không đúng
- Placeholder không được substitute đúng

**Giải pháp**:
1. Kiểm tra JSON format trong `LoiteringAreas` hoặc `LOITERING_AREAS_JSON`
2. Đảm bảo JSON là valid (có thể test với `jq`)
3. Kiểm tra server logs để xem parsing errors

### Vấn đề 3: Instance không start

**Lỗi**: `Failed to start instance`

**Nguyên nhân**:
- Pipeline build failed
- Missing model files
- Invalid video file path

**Giải pháp**:
1. Kiểm tra model files tồn tại và paths đúng
2. Kiểm tra video file path đúng
3. Xem server logs để tìm lỗi cụ thể

### Vấn đề 4: Regions không được vẽ trên video

**Lỗi**: Output video không có regions được vẽ

**Nguyên nhân**:
- OSD node không được attach đúng
- Regions không được pass đến OSD node

**Giải pháp**:
1. Kiểm tra pipeline có `ba_loitering_osd` node không
2. Kiểm tra `ba_loitering_osd` được attach sau `ba_loitering` node
3. Xem server logs để xem regions có được parse không

### Vấn đề 5: Placeholder không được substitute

**Lỗi**: `${LOITERING_AREAS_JSON}` không được substitute

**Nguyên nhân**:
- `buildParameterMap` không tìm thấy key trong `additionalParams`
- Key name không khớp (case-sensitive)

**Giải pháp**:
1. Đảm bảo key trong `additionalParams` là `LOITERING_AREAS_JSON` (uppercase)
2. Kiểm tra `buildParameterMap` có xử lý generic placeholders không
3. Sử dụng `LoiteringAreas` trực tiếp thay vì placeholder nếu cần

---

## Best Practices

1. **Luôn kiểm tra solution có sẵn** trước khi tạo instance
2. **Sử dụng `instance-body` endpoint** để lấy template request body
3. **Validate JSON format** trước khi gửi request
4. **Kiểm tra server logs** khi gặp lỗi
5. **Clean up instances** sau khi test xong
6. **Sử dụng video files nhỏ** để test nhanh hơn
7. **Test với multiple areas** để đảm bảo logic hoạt động đúng

---

## References

- Solution Registry: `src/solutions/solution_registry.cpp`
- Pipeline Builder: `src/core/pipeline_builder.cpp`
- BA Loitering Node Factory: `src/core/pipeline_builder_behavior_analysis_nodes.cpp`
- Sample Code: `sample/ba_loitering_sample.cpp`

