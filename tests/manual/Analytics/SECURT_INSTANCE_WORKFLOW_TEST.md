# Hướng Dẫn Test Thủ Công - SecuRT Instance Workflow

## Mục Lục

1. [Tổng Quan](#tổng-quan)
2. [Prerequisites](#prerequisites)
3. [Setup Variables](#setup-variables)
4. [Workflow Test - End-to-End](#workflow-test---end-to-end)
5. [Test Cases Chi Tiết](#test-cases-chi-tiết)
6. [Expected Results](#expected-results)
7. [Troubleshooting](#troubleshooting)

---

## Tổng Quan

Tài liệu này hướng dẫn test thủ công workflow hoàn chỉnh của SecuRT instance, bao gồm:
- **Tạo SecuRT Instance**: Tạo instance mới với SecuRT API
- **Cấu hình Input**: Thiết lập nguồn video (File, RTSP, RTMP, HLS)
- **Cấu hình Output**: Thiết lập đích xuất (MQTT, RTMP, RTSP, HLS)
- **Thêm Bài Toán**: Cấu hình lines và areas cho analytics
- **Start Instance**: Khởi động instance và xử lý video

### Base URLs
```
SecuRT API: http://localhost:8080/v1/securt/instance
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
   - `VLC` hoặc `ffplay` (optional) - để test RTSP/HLS streams
   - `mosquitto_sub` (optional) - để test MQTT output

3. **Tài nguyên test**:
   - Video file hoặc RTSP camera URL để test input
   - MQTT broker (nếu test MQTT output)
   - RTMP/RTSP server (nếu test streaming output)

---

## Setup Variables

```bash
# Thay đổi các giá trị này theo môi trường của bạn
SERVER="http://localhost:8080"
SECURT_BASE="${SERVER}/v1/securt/instance"
CORE_BASE="${SERVER}/v1/core/instance"

# Instance ID sẽ được tạo tự động, hoặc bạn có thể set trước
INSTANCE_ID=""  # Sẽ được set sau khi tạo instance

# Test resources
RTSP_URL="rtsp://192.168.1.100:554/stream1"  # Thay bằng RTSP URL thực tế
VIDEO_FILE="./test_video.mp4"  # Thay bằng đường dẫn file video thực tế
MQTT_BROKER="localhost"  # MQTT broker address
MQTT_PORT="1883"
MQTT_TOPIC="securt/test"
```

---

## Workflow Test - End-to-End

### Workflow Hoàn Chỉnh

Workflow này test toàn bộ quy trình từ tạo instance đến xử lý video:

```
1. Tạo SecuRT Instance
   ↓
2. Cấu hình Input (RTSP/File/RTMP/HLS)
   ↓
3. Cấu hình Output (MQTT/RTMP/RTSP/HLS)
   ↓
4. Thêm Lines/Areas (Bài toán analytics)
   ↓
5. Start Instance
   ↓
6. Kiểm tra kết quả (Stats, Analytics Entities, Output)
```

---

## Test Cases Chi Tiết

### Test Case 1: Tạo SecuRT Instance

#### 1.1: Tạo Instance với ID tự động

```bash
echo "=== Test 1.1: Tạo SecuRT Instance (Auto ID) ==="

RESPONSE=$(curl -s -X POST "${SECURT_BASE}" \
  -H "Content-Type: application/json" \
  -d '{
    "name": "Test SecuRT Instance",
    "detectorMode": "SmartDetection",
    "detectionSensitivity": "Medium",
    "movementSensitivity": "Medium"
  }')

echo "$RESPONSE" | jq .

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

#### 1.2: Tạo Instance với ID cụ thể

```bash
echo "=== Test 1.2: Tạo SecuRT Instance (Với ID) ==="

INSTANCE_ID="test-securt-$(date +%s)"
curl -X PUT "${SECURT_BASE}/${INSTANCE_ID}" \
  -H "Content-Type: application/json" \
  -d '{
    "name": "Test SecuRT Instance with ID",
    "detectorMode": "SmartDetection"
  }' | jq .
```

**Expected**: Status 201 Created với `instanceId` trùng với ID đã chỉ định.

---

### Test Case 2: Cấu hình Input

**QUAN TRỌNG**: SecuRT solution cần YOLO model paths để hoạt động. Các giá trị mặc định sau sẽ được tự động thiết lập khi tạo instance:
- `WEIGHTS_PATH`: `/opt/edgeos-api/models/det_cls/yolov3-tiny-2022-0721_best.weights`
- `CONFIG_PATH`: `/opt/edgeos-api/models/det_cls/yolov3-tiny-2022-0721.cfg`
- `LABELS_PATH`: `/opt/edgeos-api/models/det_cls/yolov3_tiny_5classes.txt`

Bạn có thể override các giá trị này bằng cách cung cấp trong `additionalParams` khi cấu hình input.

#### 2.1: Input từ RTSP Camera

```bash
echo "=== Test 2.1: Cấu hình Input RTSP ==="

curl -X POST "${SECURT_BASE}/${INSTANCE_ID}/input" \
  -H "Content-Type: application/json" \
  -d "{
    \"type\": \"RTSP\",
    \"uri\": \"${RTSP_URL}\",
    \"additionalParams\": {
      \"RTSP_TRANSPORT\": \"tcp\",
      \"GST_DECODER_NAME\": \"avdec_h264\"
    }
  }" -v
```

**Expected**: Status 204 No Content

**Lưu ý**: Nếu instance đang chạy, nó sẽ tự động restart để áp dụng thay đổi.

#### 2.2: Input từ Video File

```bash
echo "=== Test 2.2: Cấu hình Input từ File ==="

curl -X POST "${SECURT_BASE}/${INSTANCE_ID}/input" \
  -H "Content-Type: application/json" \
  -d "{
    \"type\": \"File\",
    \"uri\": \"${VIDEO_FILE}\"
  }" -v
```

**Expected**: Status 204 No Content

#### 2.3: Input từ RTMP Stream

```bash
echo "=== Test 2.3: Cấu hình Input RTMP ==="

curl -X POST "${SECURT_BASE}/${INSTANCE_ID}/input" \
  -H "Content-Type: application/json" \
  -d '{
    "type": "RTMP",
    "uri": "rtmp://live.example.com/live/stream"
  }" -v
```

**Expected**: Status 204 No Content

#### 2.4: Input từ HLS Stream

```bash
echo "=== Test 2.4: Cấu hình Input HLS ==="

curl -X POST "${SECURT_BASE}/${INSTANCE_ID}/input" \
  -H "Content-Type: application/json" \
  -d '{
    "type": "HLS",
    "uri": "http://example.com/stream.m3u8"
  }" -v
```

**Expected**: Status 204 No Content

---

### Test Case 3: Cấu hình Output

#### 3.1: Output qua MQTT

```bash
echo "=== Test 3.1: Cấu hình Output MQTT ==="

curl -X POST "${SECURT_BASE}/${INSTANCE_ID}/output" \
  -H "Content-Type: application/json" \
  -d "{
    \"type\": \"MQTT\",
    \"broker\": \"${MQTT_BROKER}\",
    \"port\": \"${MQTT_PORT}\",
    \"topic\": \"${MQTT_TOPIC}\",
    \"username\": \"\",
    \"password\": \"\"
  }" -v
```

**Expected**: Status 204 No Content

**Test MQTT Output** (trong terminal khác):
```bash
# Subscribe để nhận messages
mosquitto_sub -h ${MQTT_BROKER} -p ${MQTT_PORT} -t "${MQTT_TOPIC}"
```

#### 3.2: Output qua RTMP

```bash
echo "=== Test 3.2: Cấu hình Output RTMP ==="

curl -X POST "${SECURT_BASE}/${INSTANCE_ID}/output" \
  -H "Content-Type: application/json" \
  -d '{
    "type": "RTMP",
    "uri": "rtmp://live.example.com/live/stream_key"
  }" -v
```

**Expected**: Status 204 No Content

#### 3.3: Output qua RTSP

```bash
echo "=== Test 3.3: Cấu hình Output RTSP ==="

curl -X POST "${SECURT_BASE}/${INSTANCE_ID}/output" \
  -H "Content-Type: application/json" \
  -d '{
    "type": "RTSP",
    "uri": "rtsp://localhost:8554/stream"
  }" -v
```

**Expected**: Status 204 No Content

**Test RTSP Output**:
```bash
# Sử dụng VLC hoặc ffplay
ffplay rtsp://localhost:8554/stream
```

#### 3.4: Output qua HLS

```bash
echo "=== Test 3.4: Cấu hình Output HLS ==="

curl -X POST "${SECURT_BASE}/${INSTANCE_ID}/output" \
  -H "Content-Type: application/json" \
  -d '{
    "type": "HLS",
    "uri": "hls://localhost:8080/hls/stream"
  }" -v
```

**Expected**: Status 204 No Content

---

### Test Case 4: Thêm Bài Toán (Lines và Areas)

#### 4.1: Thêm Crossing Line

```bash
echo "=== Test 4.1: Thêm Crossing Line ==="

curl -X POST "${SECURT_BASE}/${INSTANCE_ID}/line/crossing" \
  -H "Content-Type: application/json" \
  -d '{
    "coordinates": [
      {"x": 0, "y": 300},
      {"x": 640, "y": 300}
    ],
    "direction": "AtoB",
    "name": "Entrance Line"
  }" | jq .
```

**Expected**: Status 201 Created với `lineId`:
```json
{
  "lineId": "line-123"
}
```

**Lưu ý**: Nếu instance đang chạy, nó sẽ tự động restart để áp dụng line mới.

#### 4.2: Thêm Counting Line

```bash
echo "=== Test 4.2: Thêm Counting Line ==="

curl -X POST "${SECURT_BASE}/${INSTANCE_ID}/line/counting" \
  -H "Content-Type: application/json" \
  -d '{
    "coordinates": [
      {"x": 100, "y": 0},
      {"x": 100, "y": 480}
    ],
    "name": "People Counter"
  }" | jq .
```

**Expected**: Status 201 Created với `lineId`.

#### 4.3: Thêm Tailgating Line

```bash
echo "=== Test 4.3: Thêm Tailgating Line ==="

curl -X POST "${SECURT_BASE}/${INSTANCE_ID}/line/tailgating" \
  -H "Content-Type: application/json" \
  -d '{
    "coordinates": [
      {"x": 200, "y": 0},
      {"x": 200, "y": 480}
    ],
    "minGap": 50,
    "name": "Tailgating Detection"
  }" | jq .
```

**Expected**: Status 201 Created với `lineId`.

#### 4.4: Thêm Crossing Area

```bash
echo "=== Test 4.4: Thêm Crossing Area ==="

curl -X POST "${SECURT_BASE}/${INSTANCE_ID}/area/crossing" \
  -H "Content-Type: application/json" \
  -d '{
    "coordinates": [
      {"x": 100, "y": 100},
      {"x": 500, "y": 100},
      {"x": 500, "y": 400},
      {"x": 100, "y": 400}
    ],
    "name": "Restricted Area"
  }" | jq .
```

**Expected**: Status 201 Created với `areaId`.

#### 4.5: Thêm Intrusion Area

```bash
echo "=== Test 4.5: Thêm Intrusion Area ==="

curl -X POST "${SECURT_BASE}/${INSTANCE_ID}/area/intrusion" \
  -H "Content-Type: application/json" \
  -d '{
    "coordinates": [
      {"x": 200, "y": 200},
      {"x": 400, "y": 200},
      {"x": 400, "y": 300},
      {"x": 200, "y": 300}
    ],
    "name": "No Entry Zone"
  }" | jq .
```

**Expected**: Status 201 Created với `areaId`.

#### 4.6: Lấy tất cả Analytics Entities

```bash
echo "=== Test 4.6: Lấy tất cả Analytics Entities ==="

curl -X GET "${SECURT_BASE}/${INSTANCE_ID}/analytics_entities" \
  -H "Content-Type: application/json" | jq .
```

**Expected**: Status 200 OK với danh sách tất cả lines và areas:
```json
{
  "crossingLines": [...],
  "countingLines": [...],
  "tailgatingLines": [...],
  "crossingAreas": [...],
  "intrusionAreas": [...],
  ...
}
```

---

### Test Case 5: Start Instance

#### 5.1: Start Instance

```bash
echo "=== Test 5.1: Start SecuRT Instance ==="

curl -X POST "${CORE_BASE}/${INSTANCE_ID}/start" \
  -H "Content-Type: application/json" | jq .
```

**Expected**: Status 200 OK với thông tin instance:
```json
{
  "instanceId": "...",
  "status": "running",
  "message": "Instance started successfully and is running"
}
```

**Lưu ý**: SecuRT instance sử dụng Core API để start/stop vì nó được xây dựng trên core instance system.

#### 5.2: Kiểm tra Instance Statistics

```bash
echo "=== Test 5.2: Kiểm tra Instance Statistics ==="

# Đợi vài giây để instance xử lý frames
sleep 5

curl -X GET "${SECURT_BASE}/${INSTANCE_ID}/stats" \
  -H "Content-Type: application/json" | jq .
```

**Expected**: Status 200 OK với statistics:
```json
{
  "startTime": 1234567890,
  "frameRate": 30.0,
  "latency": 50.0,
  "framesProcessed": 150,
  "trackCount": 5,
  "isRunning": true
}
```

#### 5.3: Kiểm tra Instance đang chạy

```bash
echo "=== Test 5.3: Kiểm tra Instance Status ==="

curl -X GET "${CORE_BASE}/${INSTANCE_ID}" \
  -H "Content-Type: application/json" | jq .
```

**Expected**: Status 200 OK với `running: true`.

---

### Test Case 6: Workflow Hoàn Chỉnh - End-to-End

#### 6.1: Workflow Test Script

```bash
#!/bin/bash

echo "=========================================="
echo "SecuRT Instance Workflow Test"
echo "=========================================="

# Setup
SERVER="http://localhost:8080"
SECURT_BASE="${SERVER}/v1/securt/instance"
CORE_BASE="${SERVER}/v1/core/instance"
RTSP_URL="rtsp://192.168.1.100:554/stream1"
MQTT_BROKER="localhost"
MQTT_TOPIC="securt/test"

# Step 1: Tạo instance
echo ""
echo "Step 1: Tạo SecuRT Instance"
RESPONSE=$(curl -s -X POST "${SECURT_BASE}" \
  -H "Content-Type: application/json" \
  -d '{
    "name": "E2E Test Instance",
    "detectorMode": "SmartDetection"
  }')
INSTANCE_ID=$(echo "$RESPONSE" | jq -r '.instanceId')
echo "Created instance: $INSTANCE_ID"

# Step 2: Cấu hình input
echo ""
echo "Step 2: Cấu hình Input RTSP"
curl -s -X POST "${SECURT_BASE}/${INSTANCE_ID}/input" \
  -H "Content-Type: application/json" \
  -d "{
    \"type\": \"RTSP\",
    \"uri\": \"${RTSP_URL}\"
  }" > /dev/null
echo "Input configured"

# Step 3: Cấu hình output
echo ""
echo "Step 3: Cấu hình Output MQTT"
curl -s -X POST "${SECURT_BASE}/${INSTANCE_ID}/output" \
  -H "Content-Type: application/json" \
  -d "{
    \"type\": \"MQTT\",
    \"broker\": \"${MQTT_BROKER}\",
    \"port\": \"1883\",
    \"topic\": \"${MQTT_TOPIC}\"
  }" > /dev/null
echo "Output configured"

# Step 4: Thêm crossing line
echo ""
echo "Step 4: Thêm Crossing Line"
LINE_RESPONSE=$(curl -s -X POST "${SECURT_BASE}/${INSTANCE_ID}/line/crossing" \
  -H "Content-Type: application/json" \
  -d '{
    "coordinates": [
      {"x": 0, "y": 300},
      {"x": 640, "y": 300}
    ],
    "direction": "AtoB"
  }')
LINE_ID=$(echo "$LINE_RESPONSE" | jq -r '.lineId')
echo "Created line: $LINE_ID"

# Step 5: Start instance
echo ""
echo "Step 5: Start Instance"
curl -s -X POST "${CORE_BASE}/${INSTANCE_ID}/start" | jq .
echo "Instance started"

# Step 6: Kiểm tra stats sau 5 giây
echo ""
echo "Step 6: Kiểm tra Statistics (sau 5 giây)"
sleep 5
curl -s -X GET "${SECURT_BASE}/${INSTANCE_ID}/stats" | jq .

# Step 7: Kiểm tra analytics entities
echo ""
echo "Step 7: Kiểm tra Analytics Entities"
curl -s -X GET "${SECURT_BASE}/${INSTANCE_ID}/analytics_entities" | jq .

echo ""
echo "=========================================="
echo "Workflow Test Completed"
echo "=========================================="
```

**Expected**: Tất cả các bước thành công, instance chạy và xử lý video.

---

### Test Case 7: Cleanup

#### 7.1: Stop Instance

```bash
echo "=== Test 7.1: Stop Instance ==="

curl -X POST "${CORE_BASE}/${INSTANCE_ID}/stop" \
  -H "Content-Type: application/json" | jq .
```

**Expected**: Status 200 OK.

#### 7.2: Xóa Instance

```bash
echo "=== Test 7.2: Xóa Instance ==="

curl -X DELETE "${SECURT_BASE}/${INSTANCE_ID}" \
  -H "Content-Type: application/json" -v
```

**Expected**: Status 204 No Content.

---

## Expected Results

### Tổng Quan

Sau khi hoàn thành workflow, bạn nên thấy:

1. **Instance được tạo thành công** với ID hợp lệ
2. **Input được cấu hình** và instance có thể đọc video
3. **Output được cấu hình** và kết quả được gửi đi
4. **Lines/Areas được thêm** và xuất hiện trong analytics entities
5. **Instance chạy thành công** với statistics hợp lệ

### Metrics Cần Kiểm Tra

- **frameRate**: > 0 (thường 15-30 FPS)
- **framesProcessed**: Tăng dần theo thời gian
- **isRunning**: `true`
- **trackCount**: >= 0 (tùy thuộc vào số objects được detect)

---

## Troubleshooting

### Vấn đề 1: Instance không start được

**Triệu chứng**: 
- Status code 500 khi start
- Instance không chuyển sang trạng thái `running`

**Giải pháp**:
1. Kiểm tra input đã được cấu hình chưa:
   ```bash
   curl "${CORE_BASE}/${INSTANCE_ID}/config" | jq '.Input'
   ```
2. Kiểm tra logs của server để xem lỗi chi tiết
3. Đảm bảo RTSP URL hoặc file path hợp lệ

### Vấn đề 2: Không nhận được output qua MQTT

**Triệu chứng**:
- Instance chạy nhưng không có messages trong MQTT topic

**Giải pháp**:
1. Kiểm tra MQTT broker đang chạy:
   ```bash
   mosquitto_sub -h localhost -p 1883 -t "#" -v
   ```
2. Kiểm tra cấu hình output:
   ```bash
   curl "${CORE_BASE}/${INSTANCE_ID}/config" | jq '.AdditionalParams | {MQTT_BROKER_URL, MQTT_PORT, MQTT_TOPIC}'
   ```
3. Kiểm tra instance có detect được objects không (stats.trackCount > 0)

### Vấn đề 3: Lines/Areas không hoạt động

**Triệu chứng**:
- Đã thêm lines nhưng không có events khi objects qua line

**Giải pháp**:
1. Kiểm tra lines đã được thêm:
   ```bash
   curl "${SECURT_BASE}/${INSTANCE_ID}/analytics_entities" | jq '.crossingLines'
   ```
2. Đảm bảo instance đã restart sau khi thêm lines (nếu đang chạy)
3. Kiểm tra coordinates của line có nằm trong frame không
4. Kiểm tra direction của line có đúng không

### Vấn đề 4: Instance tự động restart nhiều lần

**Triệu chứng**:
- Instance restart liên tục khi thay đổi config

**Giải pháp**:
1. Đây là behavior bình thường khi thay đổi config trong khi instance đang chạy
2. Nếu muốn tránh restart, stop instance trước khi thay đổi config:
   ```bash
   curl -X POST "${CORE_BASE}/${INSTANCE_ID}/stop"
   # Thay đổi config
   curl -X POST "${CORE_BASE}/${INSTANCE_ID}/start"
   ```

### Vấn đề 5: RTSP Input không kết nối được

**Triệu chứng**:
- Instance start nhưng không nhận được frames
- frameRate = 0

**Giải pháp**:
1. Test RTSP URL bằng VLC hoặc ffplay:
   ```bash
   ffplay rtsp://your-camera-url
   ```
2. Kiểm tra RTSP transport protocol (TCP/UDP):
   ```bash
   # Thử với TCP
   curl -X POST "${SECURT_BASE}/${INSTANCE_ID}/input" \
     -H "Content-Type: application/json" \
     -d '{
       "type": "RTSP",
       "uri": "rtsp://...",
       "additionalParams": {
         "RTSP_TRANSPORT": "tcp"
       }
     }'
   ```
3. Kiểm tra decoder compatibility:
   ```bash
   # Thử với decodebin
   curl -X POST "${SECURT_BASE}/${INSTANCE_ID}/input" \
     -H "Content-Type: application/json" \
     -d '{
       "type": "RTSP",
       "uri": "rtsp://...",
       "additionalParams": {
         "GST_DECODER_NAME": "decodebin",
         "USE_URISOURCEBIN": "true"
       }
     }'
   ```

---

## Test Checklist

Sử dụng checklist này để đảm bảo đã test đầy đủ:

- [ ] Tạo SecuRT instance thành công
- [ ] Cấu hình input từ RTSP
- [ ] Cấu hình input từ File
- [ ] Cấu hình input từ RTMP
- [ ] Cấu hình input từ HLS
- [ ] Cấu hình output qua MQTT
- [ ] Cấu hình output qua RTMP
- [ ] Cấu hình output qua RTSP
- [ ] Cấu hình output qua HLS
- [ ] Thêm crossing line
- [ ] Thêm counting line
- [ ] Thêm tailgating line
- [ ] Thêm crossing area
- [ ] Thêm intrusion area
- [ ] Lấy analytics entities
- [ ] Start instance thành công
- [ ] Kiểm tra statistics
- [ ] Verify output (MQTT messages, RTSP stream, etc.)
- [ ] Stop instance
- [ ] Xóa instance

---

## Notes

1. **Thứ tự thực hiện**: Khuyến nghị cấu hình input, output, và analytics entities TRƯỚC KHI start instance để tránh restart không cần thiết.

2. **Auto-restart**: Khi thay đổi config trong khi instance đang chạy, instance sẽ tự động restart. Điều này là bình thường và cần thiết để áp dụng thay đổi.

3. **Performance**: Instance cần thời gian để khởi động pipeline và bắt đầu xử lý. Đợi ít nhất 5-10 giây sau khi start trước khi kiểm tra statistics.

4. **Multiple Outputs**: Bạn có thể cấu hình nhiều output cùng lúc (ví dụ: vừa MQTT vừa RTSP) bằng cách gọi API output nhiều lần.

---

## References

- [SecuRT API Documentation](../../../api-specs/openapi.yaml)
- [Core API Documentation](../../../api-specs/openapi.yaml)
- [Architecture Documentation](../../../docs/ARCHITECTURE.md)

