# Hướng Dẫn Test Thủ Công - BA Area Enter/Exit với SecuRT Area API

## Mục Lục

1. [Tổng Quan](#tổng-quan)
2. [Prerequisites](#prerequisites)
3. [Setup Variables](#setup-variables)
4. [Test Case 1: Tạo Instance và Thêm Enter/Exit Areas qua SecuRT API](#test-case-1-tạo-instance-và-thêm-enterexit-areas-qua-securt-api)
5. [Test Case 2: Workflow Hoàn Chỉnh](#test-case-2-workflow-hoàn-chỉnh)
6. [Expected Results](#expected-results)
7. [Troubleshooting](#troubleshooting)

---

## Tổng Quan

Tài liệu này hướng dẫn test thủ công tính năng **BA Area Enter/Exit Detection** bằng cách tạo instance qua **SecuRT API** và thêm enter/exit areas bằng **SecuRT Area API** (`POST /v1/securt/instance/{instanceId}/area/objectEnterExit`).

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
   - Video file hoặc RTSP camera URL để test input

3. **Tài nguyên test**:
   - Video file hoặc RTSP camera URL để test input
   - MQTT broker (nếu test MQTT output)

---

## Setup Variables

```bash
# Thay đổi các giá trị này theo môi trường của bạn
SERVER="http://localhost:8080"
SECURT_BASE="${SERVER}/v1/securt/instance"
CORE_BASE="${SERVER}/v1/core/instance"

# Instance ID sẽ được tạo tự động
INSTANCE_ID=""  # Sẽ được set sau khi tạo instance

# Test resources
RTSP_URL="rtsp://192.168.1.100:554/stream1"  # Thay bằng RTSP URL thực tế
VIDEO_FILE="./test_video.mp4"  # Thay bằng đường dẫn file video thực tế
```

---

## Test Case 1: Tạo Instance và Thêm Enter/Exit Areas qua SecuRT API

### 1.1: Tạo SecuRT Instance

```bash
echo "=== Test 1.1: Tạo SecuRT Instance ==="

RESPONSE=$(curl -s -X POST "${SECURT_BASE}" \
  -H "Content-Type: application/json" \
  -d '{
    "name": "BA Area Enter/Exit Test Instance",
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

### 1.2: Cấu hình Input

```bash
echo "=== Test 1.2: Cấu hình Input từ File ==="

curl -X POST "${SECURT_BASE}/${INSTANCE_ID}/input" \
  -H "Content-Type: application/json" \
  -d "{
    \"type\": \"File\",
    \"uri\": \"${VIDEO_FILE}\"
  }" -v
```

**Expected**: Status 204 No Content

**Hoặc với RTSP**:
```bash
echo "=== Test 1.2b: Cấu hình Input từ RTSP ==="

curl -X POST "${SECURT_BASE}/${INSTANCE_ID}/input" \
  -H "Content-Type: application/json" \
  -d "{
    \"type\": \"RTSP\",
    \"uri\": \"${RTSP_URL}\",
    \"additionalParams\": {
      \"RTSP_TRANSPORT\": \"tcp\"
    }
  }" -v
```

### 1.3: Thêm Enter/Exit Area (Basic)

```bash
echo "=== Test 1.3: Thêm Enter/Exit Area (Basic) ==="

AREA_RESPONSE=$(curl -s -X POST "${SECURT_BASE}/${INSTANCE_ID}/area/objectEnterExit" \
  -H "Content-Type: application/json" \
  -d '{
    "name": "Entrance Zone 1",
    "coordinates": [
      {"x": 100, "y": 100},
      {"x": 500, "y": 100},
      {"x": 500, "y": 400},
      {"x": 100, "y": 400}
    ],
    "classes": ["person"],
    "alertOnEnter": true,
    "alertOnExit": true,
    "color": [0, 255, 0, 128]
  }')

echo "$AREA_RESPONSE" | jq .

AREA_ID=$(echo "$AREA_RESPONSE" | jq -r '.areaId')
echo "Area ID: $AREA_ID"
```

**Expected**: Status 201 Created với `areaId`:
```json
{
  "areaId": "area-123-456"
}
```

**Giải thích**:
- `coordinates`: Array các điểm tạo thành polygon (sẽ được convert thành bounding rectangle)
- `alertOnEnter`: Có cảnh báo khi object vào area (default: true)
- `alertOnExit`: Có cảnh báo khi object ra khỏi area (default: true)
- `name`: Tên của area (optional)
- `classes`: Array các object classes để detect (optional, default: tất cả)
- `color`: RGBA color cho visualization (optional)

### 1.4: Thêm Enter/Exit Area với Custom Configuration

```bash
echo "=== Test 1.4: Thêm Enter/Exit Area với Custom Configuration ==="

AREA_RESPONSE=$(curl -s -X POST "${SECURT_BASE}/${INSTANCE_ID}/area/objectEnterExit" \
  -H "Content-Type: application/json" \
  -d '{
    "name": "Restricted Zone",
    "coordinates": [
      {"x": 600, "y": 200},
      {"x": 800, "y": 200},
      {"x": 800, "y": 500},
      {"x": 600, "y": 500}
    ],
    "classes": ["person", "vehicle"],
    "alertOnEnter": true,
    "alertOnExit": false,
    "color": [255, 0, 0, 128]
  }')

echo "$AREA_RESPONSE" | jq .
AREA_ID=$(echo "$AREA_RESPONSE" | jq -r '.areaId')
echo "Area ID: $AREA_ID"
```

**Expected**: Status 201 Created với chỉ alertOnEnter = true, alertOnExit = false.

### 1.5: Thêm Multiple Enter/Exit Areas

```bash
echo "=== Test 1.5: Thêm Multiple Enter/Exit Areas ==="

# Area 1
AREA1_RESPONSE=$(curl -s -X POST "${SECURT_BASE}/${INSTANCE_ID}/area/objectEnterExit" \
  -H "Content-Type: application/json" \
  -d '{
    "name": "Zone A - Entrance",
    "coordinates": [
      {"x": 50, "y": 50},
      {"x": 300, "y": 50},
      {"x": 300, "y": 250},
      {"x": 50, "y": 250}
    ],
    "alertOnEnter": true,
    "alertOnExit": false
  }')
AREA1_ID=$(echo "$AREA1_RESPONSE" | jq -r '.areaId')
echo "Area 1 ID: $AREA1_ID"

# Area 2
AREA2_RESPONSE=$(curl -s -X POST "${SECURT_BASE}/${INSTANCE_ID}/area/objectEnterExit" \
  -H "Content-Type: application/json" \
  -d '{
    "name": "Zone B - Restricted",
    "coordinates": [
      {"x": 350, "y": 50},
      {"x": 600, "y": 50},
      {"x": 600, "y": 250},
      {"x": 350, "y": 250}
    ],
    "alertOnEnter": true,
    "alertOnExit": true
  }')
AREA2_ID=$(echo "$AREA2_RESPONSE" | jq -r '.areaId')
echo "Area 2 ID: $AREA2_ID"

# Area 3
AREA3_RESPONSE=$(curl -s -X POST "${SECURT_BASE}/${INSTANCE_ID}/area/objectEnterExit" \
  -H "Content-Type: application/json" \
  -d '{
    "name": "Zone C - Exit",
    "coordinates": [
      {"x": 50, "y": 300},
      {"x": 300, "y": 300},
      {"x": 300, "y": 500},
      {"x": 50, "y": 500}
    ],
    "alertOnEnter": false,
    "alertOnExit": true
  }')
AREA3_ID=$(echo "$AREA3_RESPONSE" | jq -r '.areaId')
echo "Area 3 ID: $AREA3_ID"
```

**Expected**: Tất cả 3 areas được tạo thành công với các IDs khác nhau.

### 1.6: Lấy tất cả Enter/Exit Areas

```bash
echo "=== Test 1.6: Lấy tất cả Enter/Exit Areas ==="

curl -X GET "${SECURT_BASE}/${INSTANCE_ID}/areas" \
  -H "Content-Type: application/json" | jq '.objectEnterExit'
```

**Expected**: Status 200 OK với array các enter/exit areas:
```json
{
  "objectEnterExit": [
    {
      "id": "area-123",
      "name": "Entrance Zone 1",
      "coordinates": [...],
      "classes": ["person"],
      "alertOnEnter": true,
      "alertOnExit": true,
      "color": [0, 255, 0, 128]
    },
    ...
  ]
}
```

### 1.7: Lấy Analytics Entities (bao gồm Enter/Exit Areas)

```bash
echo "=== Test 1.7: Lấy Analytics Entities ==="

curl -X GET "${SECURT_BASE}/${INSTANCE_ID}/analytics_entities" \
  -H "Content-Type: application/json" | jq '.objectEnterExit'
```

**Expected**: Status 200 OK với enter/exit areas trong analytics entities.

---

## Test Case 2: Workflow Hoàn Chỉnh

### 2.1: Workflow Test Script

```bash
#!/bin/bash

echo "=========================================="
echo "BA Area Enter/Exit Detection - SecuRT API Test"
echo "=========================================="

# Setup
SERVER="http://localhost:8080"
SECURT_BASE="${SERVER}/v1/securt/instance"
CORE_BASE="${SERVER}/v1/core/instance"
VIDEO_FILE="./test_video.mp4"  # Thay bằng đường dẫn thực tế

# Step 1: Tạo SecuRT instance
echo ""
echo "Step 1: Tạo SecuRT Instance"
RESPONSE=$(curl -s -X POST "${SECURT_BASE}" \
  -H "Content-Type: application/json" \
  -d '{
    "name": "BA Area Enter/Exit E2E Test",
    "detectorMode": "SmartDetection",
    "detectionSensitivity": "Medium"
  }')
INSTANCE_ID=$(echo "$RESPONSE" | jq -r '.instanceId')
echo "Created instance: $INSTANCE_ID"

if [ -z "$INSTANCE_ID" ] || [ "$INSTANCE_ID" = "null" ]; then
  echo "ERROR: Failed to create instance"
  echo "$RESPONSE" | jq .
  exit 1
fi

# Step 2: Cấu hình input
echo ""
echo "Step 2: Cấu hình Input"
curl -s -X POST "${SECURT_BASE}/${INSTANCE_ID}/input" \
  -H "Content-Type: application/json" \
  -d "{
    \"type\": \"File\",
    \"uri\": \"${VIDEO_FILE}\"
  }" > /dev/null
echo "Input configured"

# Step 3: Thêm enter/exit area 1
echo ""
echo "Step 3: Thêm Enter/Exit Area 1"
AREA1_RESPONSE=$(curl -s -X POST "${SECURT_BASE}/${INSTANCE_ID}/area/objectEnterExit" \
  -H "Content-Type: application/json" \
  -d '{
    "name": "Zone 1",
    "coordinates": [
      {"x": 100, "y": 100},
      {"x": 500, "y": 100},
      {"x": 500, "y": 400},
      {"x": 100, "y": 400}
    ],
    "alertOnEnter": true,
    "alertOnExit": true
  }')
AREA1_ID=$(echo "$AREA1_RESPONSE" | jq -r '.areaId')
echo "Created area 1: $AREA1_ID"

# Step 4: Thêm enter/exit area 2
echo ""
echo "Step 4: Thêm Enter/Exit Area 2"
AREA2_RESPONSE=$(curl -s -X POST "${SECURT_BASE}/${INSTANCE_ID}/area/objectEnterExit" \
  -H "Content-Type: application/json" \
  -d '{
    "name": "Zone 2",
    "coordinates": [
      {"x": 600, "y": 200},
      {"x": 800, "y": 200},
      {"x": 800, "y": 500},
      {"x": 600, "y": 500}
    ],
    "alertOnEnter": true,
    "alertOnExit": false
  }')
AREA2_ID=$(echo "$AREA2_RESPONSE" | jq -r '.areaId')
echo "Created area 2: $AREA2_ID"

# Step 5: Kiểm tra enter/exit areas
echo ""
echo "Step 5: Kiểm tra Enter/Exit Areas"
curl -s -X GET "${SECURT_BASE}/${INSTANCE_ID}/areas" | jq '.objectEnterExit'

# Step 6: Start instance
echo ""
echo "Step 6: Start Instance"
curl -s -X POST "${CORE_BASE}/${INSTANCE_ID}/start" | jq .
echo "Instance started"

# Step 7: Đợi instance khởi động
echo ""
echo "Step 7: Đợi instance khởi động (5 giây)..."
sleep 5

# Step 8: Kiểm tra instance status
echo ""
echo "Step 8: Kiểm tra Instance Status"
curl -s -X GET "${CORE_BASE}/${INSTANCE_ID}" | jq '.running'

# Step 9: Kiểm tra statistics
echo ""
echo "Step 9: Kiểm tra Statistics"
curl -s -X GET "${SECURT_BASE}/${INSTANCE_ID}/stats" | jq .

# Step 10: Kiểm tra analytics entities
echo ""
echo "Step 10: Kiểm tra Analytics Entities"
curl -s -X GET "${SECURT_BASE}/${INSTANCE_ID}/analytics_entities" | jq '.objectEnterExit'

# Step 11: Kiểm tra pipeline có ba_area_enter_exit node
echo ""
echo "Step 11: Kiểm tra Pipeline Nodes"
curl -s -X GET "${CORE_BASE}/${INSTANCE_ID}/config" | jq '.Pipeline | map(select(.NodeType == "ba_area_enter_exit"))'

# Step 12: Stop instance
echo ""
echo "Step 12: Stop Instance"
curl -s -X POST "${CORE_BASE}/${INSTANCE_ID}/stop" | jq .

# Step 13: Xóa instance
echo ""
echo "Step 13: Xóa Instance"
curl -s -X DELETE "${SECURT_BASE}/${INSTANCE_ID}" -v

echo ""
echo "=========================================="
echo "Workflow Test Completed"
echo "=========================================="
```

**Lưu script trên vào file `test_ba_area_enter_exit_securt_api.sh`, chmod +x và chạy**:
```bash
chmod +x test_ba_area_enter_exit_securt_api.sh
./test_ba_area_enter_exit_securt_api.sh
```

### 2.2: Test Auto-Restart khi Thêm Areas

```bash
echo "=== Test 2.2: Auto-Restart khi Thêm Areas ==="

# Start instance trước
echo "Starting instance..."
curl -s -X POST "${CORE_BASE}/${INSTANCE_ID}/start" | jq .
sleep 3

# Kiểm tra instance đang chạy
RUNNING=$(curl -s -X GET "${CORE_BASE}/${INSTANCE_ID}" | jq -r '.running')
echo "Instance running: $RUNNING"

# Thêm enter/exit area (instance sẽ tự động restart)
echo "Adding enter/exit area (instance will auto-restart)..."
AREA_RESPONSE=$(curl -s -X POST "${SECURT_BASE}/${INSTANCE_ID}/area/objectEnterExit" \
  -H "Content-Type: application/json" \
  -d '{
    "name": "Dynamic Zone",
    "coordinates": [
      {"x": 200, "y": 200},
      {"x": 400, "y": 200},
      {"x": 400, "y": 400},
      {"x": 200, "y": 400}
    ],
    "alertOnEnter": true,
    "alertOnExit": true
  }')
echo "$AREA_RESPONSE" | jq .

# Đợi instance restart
echo "Waiting for instance to restart (5 seconds)..."
sleep 5

# Kiểm tra instance vẫn chạy
RUNNING_AFTER=$(curl -s -X GET "${CORE_BASE}/${INSTANCE_ID}" | jq -r '.running')
echo "Instance running after restart: $RUNNING_AFTER"
```

**Expected**: Instance tự động restart sau khi thêm area và vẫn tiếp tục chạy.

### 2.3: Test Xóa Enter/Exit Area

```bash
echo "=== Test 2.3: Xóa Enter/Exit Area ==="

# Lấy danh sách areas trước
echo "Areas before deletion:"
curl -s -X GET "${SECURT_BASE}/${INSTANCE_ID}/areas" | jq '.objectEnterExit | length'

# Xóa area (thay AREA_ID bằng ID thực tế)
AREA_ID="area-123-456"  # Thay bằng ID thực tế
curl -X DELETE "${SECURT_BASE}/${INSTANCE_ID}/area/${AREA_ID}" -v

# Kiểm tra areas sau khi xóa
echo "Areas after deletion:"
curl -s -X GET "${SECURT_BASE}/${INSTANCE_ID}/areas" | jq '.objectEnterExit | length'
```

**Expected**: Status 204 No Content, số lượng areas giảm đi 1.

---

## Expected Results

### Tổng Quan

Sau khi hoàn thành workflow, bạn nên thấy:

1. **Instance được tạo thành công** qua SecuRT API
2. **Enter/Exit areas được thêm** qua Area API
3. **ba_area_enter_exit node được tự động tạo** trong pipeline khi start instance
4. **ba_area_enter_exit_osd node được tự động tạo** để hiển thị regions
5. **Instance chạy thành công** và xử lý video
6. **Enter/Exit areas xuất hiện** trong analytics entities

### Kiểm tra Pipeline Nodes

```bash
# Kiểm tra instance có ba_area_enter_exit node không
curl -s -X GET "${CORE_BASE}/${INSTANCE_ID}/config" | jq '.Pipeline | map(select(.NodeType == "ba_area_enter_exit"))'

# Kiểm tra instance có ba_area_enter_exit_osd node không
curl -s -X GET "${CORE_BASE}/${INSTANCE_ID}/config" | jq '.Pipeline | map(select(.NodeType == "ba_area_enter_exit_osd"))'
```

**Expected**: Cả hai node types đều có trong pipeline sau khi start instance.

### Kiểm tra Enter/Exit Areas trong Analytics Entities

```bash
curl -s -X GET "${SECURT_BASE}/${INSTANCE_ID}/analytics_entities" | jq '.objectEnterExit'
```

**Expected**: Response chứa array các enter/exit areas với đầy đủ thông tin (id, name, coordinates, alertOnEnter, alertOnExit).

---

## Troubleshooting

### Vấn đề 1: Enter/Exit area không được thêm

**Triệu chứng**: 
- API trả về 201 nhưng area không xuất hiện trong danh sách
- Analytics entities không chứa enter/exit areas

**Giải pháp**:
1. Kiểm tra coordinates có hợp lệ không (ít nhất 3 điểm):
   ```bash
   # Validate coordinates
   echo '{"coordinates":[{"x":100,"y":100},{"x":500,"y":100},{"x":500,"y":400}]}' | jq .
   ```
2. Kiểm tra instance ID có đúng không
3. Kiểm tra server logs để xem có lỗi không

### Vấn đề 2: ba_area_enter_exit node không được tạo

**Triệu chứng**:
- Areas được thêm nhưng ba_area_enter_exit node không có trong pipeline
- Instance start nhưng không có enter/exit detection

**Giải pháp**:
1. Đảm bảo instance đã được start (nodes chỉ được tạo khi start):
   ```bash
   curl -s -X GET "${CORE_BASE}/${INSTANCE_ID}" | jq '.running'
   ```
2. Kiểm tra có enter/exit areas trong storage:
   ```bash
   curl -s -X GET "${SECURT_BASE}/${INSTANCE_ID}/areas" | jq '.objectEnterExit'
   ```
3. Restart instance để pipeline rebuild:
   ```bash
   curl -X POST "${CORE_BASE}/${INSTANCE_ID}/stop"
   curl -X POST "${CORE_BASE}/${INSTANCE_ID}/start"
   ```

### Vấn đề 3: Enter/Exit detection không hoạt động

**Triệu chứng**:
- Instance chạy nhưng không có enter/exit events
- Statistics không hiển thị enter/exit detections

**Giải pháp**:
1. Kiểm tra coordinates của areas có nằm trong frame không
2. Đảm bảo `alertOnEnter` hoặc `alertOnExit` được set đúng (ít nhất một trong hai phải là true)
3. Kiểm tra có objects được detect không:
   ```bash
   curl -s -X GET "${SECURT_BASE}/${INSTANCE_ID}/stats" | jq '.trackCount'
   ```
4. Kiểm tra ba_area_enter_exit node có trong pipeline:
   ```bash
   curl -s -X GET "${CORE_BASE}/${INSTANCE_ID}/config" | jq '.Pipeline | map(select(.NodeType == "ba_area_enter_exit"))'
   ```

### Vấn đề 4: Instance không auto-restart khi thêm areas

**Triệu chứng**:
- Thêm areas nhưng instance không restart
- Areas không được áp dụng

**Giải pháp**:
1. Đây là behavior bình thường - instance chỉ restart khi đang chạy
2. Nếu instance không chạy, start nó sau khi thêm areas:
   ```bash
   curl -X POST "${CORE_BASE}/${INSTANCE_ID}/start"
   ```
3. Kiểm tra instance status:
   ```bash
   curl -s -X GET "${CORE_BASE}/${INSTANCE_ID}" | jq '.running'
   ```

### Vấn đề 5: Coordinates không đúng format

**Triệu chứng**:
- API trả về 400 Bad Request
- Coordinates không được parse đúng

**Giải pháp**:
1. Đảm bảo coordinates là array các objects với `x` và `y`:
   ```json
   [
     {"x": 100, "y": 100},
     {"x": 500, "y": 100},
     {"x": 500, "y": 400}
   ]
   ```
2. Đảm bảo có ít nhất 3 điểm để tạo thành polygon
3. Kiểm tra JSON format:
   ```bash
   echo '{"coordinates":[{"x":100,"y":100},{"x":500,"y":100},{"x":500,"y":400}]}' | jq .
   ```

---

## Test Checklist

Sử dụng checklist này để đảm bảo đã test đầy đủ:

- [ ] Tạo SecuRT instance thành công
- [ ] Cấu hình input (File/RTSP)
- [ ] Thêm enter/exit area (basic)
- [ ] Thêm enter/exit area với custom configuration
- [ ] Thêm multiple enter/exit areas
- [ ] Lấy tất cả enter/exit areas
- [ ] Lấy analytics entities
- [ ] Start instance thành công
- [ ] Kiểm tra ba_area_enter_exit node trong pipeline
- [ ] Kiểm tra ba_area_enter_exit_osd node trong pipeline
- [ ] Kiểm tra instance statistics
- [ ] Kiểm tra enter/exit areas trong analytics entities
- [ ] Test auto-restart khi thêm areas
- [ ] Test xóa enter/exit area
- [ ] Verify enter/exit detection hoạt động (nếu có video input)
- [ ] Stop instance
- [ ] Xóa instance

---

## Notes

1. **Auto Node Creation**: ba_area_enter_exit và ba_area_enter_exit_osd nodes sẽ được tự động tạo khi start instance nếu có enter/exit areas trong storage.

2. **Auto-Restart**: Khi thêm areas trong khi instance đang chạy, instance sẽ tự động restart để áp dụng thay đổi.

3. **Coordinate Format**: 
   - Polygon coordinates sẽ được convert thành bounding rectangles
   - Hỗ trợ cả pixel coordinates và normalized coordinates (0.0-1.0)

4. **Alert Configuration**: 
   - `alertOnEnter`: Cảnh báo khi object vào area (default: true)
   - `alertOnExit`: Cảnh báo khi object ra khỏi area (default: true)
   - Ít nhất một trong hai phải được set là true

5. **Multiple Areas**: Bạn có thể thêm nhiều enter/exit areas cho cùng một instance, mỗi area có cấu hình alert riêng.

6. **Object Classes**: 
   - `classes`: Array các class cần phát hiện: `["Person", "Vehicle", "Animal", "Face", "Unknown"]`
   - Nếu không chỉ định, tất cả classes sẽ được detect

---

## References

- [SecuRT API Documentation](../../../api-specs/openapi.yaml)
- [Core API Documentation](../../../api-specs/openapi.yaml)
- [BA Area Enter/Exit Sample Code](../../../sample/ba_area_enter_exit_sample.cpp)
- [SecuRT Instance Workflow Test](./SECURT_INSTANCE_WORKFLOW_TEST.md)
