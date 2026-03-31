# Hướng Dẫn Test Thủ Công - Events & Output Configuration API

## Mục Lục

1. [Tổng Quan](#tổng-quan)
2. [Prerequisites](#prerequisites)
3. [Setup Variables](#setup-variables)
4. [Test Cases](#test-cases)
5. [Expected Results](#expected-results)
6. [Troubleshooting](#troubleshooting)

---

## Tổng Quan

Tài liệu này hướng dẫn test thủ công các endpoints Events & Output Configuration API, bao gồm:
- **Consume Events**: Lấy events từ instance event queue
- **HLS Output**: Cấu hình HLS (HTTP Live Streaming) output cho instance
- **RTSP Output**: Cấu hình RTSP (Real-Time Streaming Protocol) output cho instance

### Base URL
```
http://localhost:8080/v1/core/instance
```

---

## Prerequisites

1. **API Server đang chạy**:
   ```bash
   # Kiểm tra server đang chạy
   curl http://localhost:8080/v1/core/health
   ```

2. **Có instance đang chạy**:
   ```bash
   # Kiểm tra danh sách instances
   curl http://localhost:8080/v1/core/instance | jq .
   
   # Lấy instance ID từ kết quả
   INSTANCE_ID="<your-instance-id>"
   ```

3. **Công cụ cần thiết**:
   - `curl` - để gửi HTTP requests
   - `jq` (optional) - để format JSON output đẹp hơn
   - `VLC` hoặc `ffplay` (optional) - để test HLS/RTSP streams

---

## Setup Variables

```bash
# Thay đổi các giá trị này theo môi trường của bạn
SERVER="http://localhost:8080"
BASE_URL="${SERVER}/v1/core/instance"
INSTANCE_ID="<your-instance-id>"  # Thay bằng instance ID thực tế
```

---

## Test Cases

### 1. Consume Events - Lấy Events từ Instance

#### Test Case 1.1: Consume Events (Success - Có Events)

```bash
echo "=== Test 1.1: Consume Events (Có Events) ==="
curl -X GET "${BASE_URL}/${INSTANCE_ID}/consume_events" \
  -H "Content-Type: application/json" \
  -H "Accept: application/json" | jq .
```

**Expected**: Status 200, JSON response với array of events:
```json
[
  {
    "dataType": "detection",
    "jsonObject": "{\"timestamp\": \"2024-01-01T00:00:00Z\", \"objects\": []}"
  },
  {
    "dataType": "tracking",
    "jsonObject": "{\"timestamp\": \"2024-01-01T00:00:01Z\", \"tracks\": []}"
  }
]
```

#### Test Case 1.2: Consume Events (No Events Available)

```bash
echo "=== Test 1.2: Consume Events (Không có Events) ==="
curl -X GET "${BASE_URL}/${INSTANCE_ID}/consume_events" \
  -H "Content-Type: application/json" \
  -v
```

**Expected**: Status 204 No Content (không có body)

#### Test Case 1.3: Consume Events (Instance Not Found)

```bash
echo "=== Test 1.3: Consume Events (Instance Not Found) ==="
curl -X GET "${BASE_URL}/invalid-instance-id/consume_events" \
  -H "Content-Type: application/json" | jq .
```

**Expected**: Status 404, JSON response:
```json
{
  "error": "Not found",
  "message": "Instance not found: invalid-instance-id"
}
```

#### Test Case 1.4: Consume Events với CORS

```bash
echo "=== Test 1.4: Consume Events (CORS Preflight) ==="
curl -X OPTIONS "${BASE_URL}/${INSTANCE_ID}/consume_events" \
  -H "Origin: http://localhost:3000" \
  -H "Access-Control-Request-Method: GET" \
  -H "Access-Control-Request-Headers: Content-Type" \
  -v
```

**Expected**: Status 200, headers:
```
Access-Control-Allow-Origin: *
Access-Control-Allow-Methods: GET, POST, PUT, DELETE, OPTIONS
Access-Control-Allow-Headers: Content-Type, Authorization
```

---

### 2. Configure HLS Output - Cấu Hình HLS Output

#### Test Case 2.1: Enable HLS Output

```bash
echo "=== Test 2.1: Enable HLS Output ==="
curl -X POST "${BASE_URL}/${INSTANCE_ID}/output/hls" \
  -H "Content-Type: application/json" \
  -H "Accept: application/json" \
  -d '{
    "enabled": true
  }' | jq .
```

**Expected**: Status 200, JSON response với HLS URI:
```json
{
  "uri": "http://localhost:8080/hls/{instanceId}/stream.m3u8"
}
```

**Lưu ý**: Thay `{instanceId}` bằng instance ID thực tế trong response.

#### Test Case 2.2: Disable HLS Output

```bash
echo "=== Test 2.2: Disable HLS Output ==="
curl -X POST "${BASE_URL}/${INSTANCE_ID}/output/hls" \
  -H "Content-Type: application/json" \
  -d '{
    "enabled": false
  }' \
  -v
```

**Expected**: Status 204 No Content

#### Test Case 2.3: Enable HLS Output (Missing enabled field)

```bash
echo "=== Test 2.3: Enable HLS Output (Missing enabled) ==="
curl -X POST "${BASE_URL}/${INSTANCE_ID}/output/hls" \
  -H "Content-Type: application/json" \
  -d '{}' | jq .
```

**Expected**: Status 400, JSON response:
```json
{
  "error": "Bad request",
  "message": "Field 'enabled' is required"
}
```

#### Test Case 2.4: Enable HLS Output (Invalid JSON)

```bash
echo "=== Test 2.4: Enable HLS Output (Invalid JSON) ==="
curl -X POST "${BASE_URL}/${INSTANCE_ID}/output/hls" \
  -H "Content-Type: application/json" \
  -d '{invalid json}' | jq .
```

**Expected**: Status 400, JSON response:
```json
{
  "error": "Bad request",
  "message": "Invalid JSON body"
}
```

#### Test Case 2.5: Enable HLS Output (Instance Not Found)

```bash
echo "=== Test 2.5: Enable HLS Output (Instance Not Found) ==="
curl -X POST "${BASE_URL}/invalid-instance-id/output/hls" \
  -H "Content-Type: application/json" \
  -d '{
    "enabled": true
  }' | jq .
```

**Expected**: Status 404, JSON response:
```json
{
  "error": "Not found",
  "message": "Instance not found: invalid-instance-id"
}
```

#### Test Case 2.6: Test HLS Stream với VLC

Sau khi enable HLS output thành công:

```bash
# Lấy HLS URI từ response
HLS_URI="http://localhost:8080/hls/${INSTANCE_ID}/stream.m3u8"

# Test với VLC (nếu có)
vlc "${HLS_URI}"

# Hoặc với ffplay
ffplay "${HLS_URI}"
```

**Expected**: Video stream phát được trong VLC/ffplay

---

### 3. Configure RTSP Output - Cấu Hình RTSP Output

#### Test Case 3.1: Enable RTSP Output với Custom URI

```bash
echo "=== Test 3.1: Enable RTSP Output (Custom URI) ==="
curl -X POST "${BASE_URL}/${INSTANCE_ID}/output/rtsp" \
  -H "Content-Type: application/json" \
  -d '{
    "enabled": true,
    "uri": "rtsp://localhost:8554/stream"
  }' \
  -v
```

**Expected**: Status 204 No Content

#### Test Case 3.2: Enable RTSP Output với Default URI

```bash
echo "=== Test 3.2: Enable RTSP Output (Default URI) ==="
curl -X POST "${BASE_URL}/${INSTANCE_ID}/output/rtsp" \
  -H "Content-Type: application/json" \
  -d '{
    "enabled": true
  }' \
  -v
```

**Expected**: Status 204 No Content (sử dụng default URI: `rtsp://localhost:8554/stream`)

#### Test Case 3.3: Disable RTSP Output

```bash
echo "=== Test 3.3: Disable RTSP Output ==="
curl -X POST "${BASE_URL}/${INSTANCE_ID}/output/rtsp" \
  -H "Content-Type: application/json" \
  -d '{
    "enabled": false
  }' \
  -v
```

**Expected**: Status 204 No Content

#### Test Case 3.4: Enable RTSP Output (Invalid URI Format)

```bash
echo "=== Test 3.4: Enable RTSP Output (Invalid URI) ==="
curl -X POST "${BASE_URL}/${INSTANCE_ID}/output/rtsp" \
  -H "Content-Type: application/json" \
  -d '{
    "enabled": true,
    "uri": "http://localhost:8554/stream"
  }' | jq .
```

**Expected**: Status 400, JSON response:
```json
{
  "error": "Bad request",
  "message": "URI must start with rtsp://"
}
```

#### Test Case 3.5: Enable RTSP Output (Missing enabled field)

```bash
echo "=== Test 3.5: Enable RTSP Output (Missing enabled) ==="
curl -X POST "${BASE_URL}/${INSTANCE_ID}/output/rtsp" \
  -H "Content-Type: application/json" \
  -d '{
    "uri": "rtsp://localhost:8554/stream"
  }' | jq .
```

**Expected**: Status 400, JSON response:
```json
{
  "error": "Bad request",
  "message": "Field 'enabled' is required"
}
```

#### Test Case 3.6: Test RTSP Stream với VLC

Sau khi enable RTSP output thành công:

```bash
# Sử dụng URI đã cấu hình
RTSP_URI="rtsp://localhost:8554/stream"

# Test với VLC (nếu có)
vlc "${RTSP_URI}"

# Hoặc với ffplay
ffplay "${RTSP_URI}"

# Hoặc với gst-launch-1.0
gst-launch-1.0 rtspsrc location="${RTSP_URI}" ! rtph264depay ! avdec_h264 ! autovideosink
```

**Expected**: Video stream phát được trong VLC/ffplay/gst-launch

---

### 4. Integration Tests - Test Kết Hợp

#### Test Case 4.1: Enable Multiple Output Formats

```bash
echo "=== Test 4.1: Enable HLS và RTSP cùng lúc ==="

# Enable HLS
echo "Enabling HLS..."
curl -X POST "${BASE_URL}/${INSTANCE_ID}/output/hls" \
  -H "Content-Type: application/json" \
  -d '{"enabled": true}' | jq .

# Enable RTSP
echo "Enabling RTSP..."
curl -X POST "${BASE_URL}/${INSTANCE_ID}/output/rtsp" \
  -H "Content-Type: application/json" \
  -d '{"enabled": true}' \
  -v

# Kiểm tra cả hai streams đều hoạt động
echo "HLS URI: http://localhost:8080/hls/${INSTANCE_ID}/stream.m3u8"
echo "RTSP URI: rtsp://localhost:8554/stream"
```

**Expected**: Cả HLS và RTSP đều enable thành công và stream được

#### Test Case 4.2: Consume Events sau khi Enable Output

```bash
echo "=== Test 4.2: Consume Events sau khi Enable Output ==="

# Enable HLS output
curl -X POST "${BASE_URL}/${INSTANCE_ID}/output/hls" \
  -H "Content-Type: application/json" \
  -d '{"enabled": true}' > /dev/null

# Đợi một chút để instance xử lý
sleep 2

# Consume events
curl -X GET "${BASE_URL}/${INSTANCE_ID}/consume_events" \
  -H "Content-Type: application/json" | jq .
```

**Expected**: Có thể có events liên quan đến output configuration

---

## Expected Results

### Consume Events Endpoint

| Test Case | HTTP Status | Response Body |
|-----------|-------------|---------------|
| Có events | 200 OK | Array of events |
| Không có events | 204 No Content | Empty body |
| Instance not found | 404 Not Found | Error message |
| Invalid request | 400 Bad Request | Error message |

### HLS Output Endpoint

| Test Case | HTTP Status | Response Body |
|-----------|-------------|---------------|
| Enable success | 200 OK | `{"uri": "http://..."}` |
| Disable success | 204 No Content | Empty body |
| Missing enabled | 400 Bad Request | Error message |
| Instance not found | 404 Not Found | Error message |
| Could not set | 406 Not Acceptable | Error message |

### RTSP Output Endpoint

| Test Case | HTTP Status | Response Body |
|-----------|-------------|---------------|
| Enable success | 204 No Content | Empty body |
| Disable success | 204 No Content | Empty body |
| Invalid URI | 400 Bad Request | Error message |
| Missing enabled | 400 Bad Request | Error message |
| Instance not found | 404 Not Found | Error message |

---

## Troubleshooting

### 1. Consume Events trả về 204 (No Content)

**Nguyên nhân**: Instance chưa có events trong queue hoặc events đã được consume hết.

**Giải pháp**:
- Đảm bảo instance đang chạy và xử lý frames
- Kiểm tra xem instance có publish events không
- Đợi một chút rồi thử lại

### 2. HLS Output không trả về URI

**Nguyên nhân**: 
- Instance không tồn tại
- Không thể cấu hình HLS output

**Giải pháp**:
- Kiểm tra instance ID có đúng không
- Kiểm tra instance có đang chạy không
- Xem logs của server để biết lỗi chi tiết

### 3. RTSP Stream không phát được

**Nguyên nhân**:
- RTSP server chưa được khởi động
- Port 8554 bị chặn
- URI không đúng

**Giải pháp**:
- Kiểm tra RTSP server có đang chạy không
- Kiểm tra firewall có cho phép port 8554 không
- Thử với URI khác: `rtsp://localhost:8554/stream`

### 4. HLS Stream không phát được

**Nguyên nhân**:
- HLS segments chưa được tạo
- Playlist file (.m3u8) không tồn tại
- HTTP server không serve HLS files

**Giải pháp**:
- Đợi một chút để HLS segments được tạo
- Kiểm tra file `/hls/{instanceId}/stream.m3u8` có tồn tại không
- Kiểm tra HTTP server có serve static files từ `/hls/` không

### 5. Events không được publish

**Nguyên nhân**:
- Instance chưa được cấu hình để publish events
- Event queue chưa được tích hợp với instance pipeline

**Giải pháp**:
- Kiểm tra instance configuration có enable event publishing không
- Xem logs của instance để biết có events được tạo không
- Đảm bảo instance đang xử lý frames và có detections/tracking

### 6. CORS Errors

**Nguyên nhân**: Browser chặn cross-origin requests.

**Giải pháp**:
- Kiểm tra server có trả về CORS headers không
- Sử dụng `-H "Origin: http://localhost:3000"` trong curl để test CORS
- Kiểm tra OPTIONS request có trả về đúng headers không

---

## Quick Test Script

Tạo file `test_events_output.sh`:

```bash
#!/bin/bash

# Setup
SERVER="http://localhost:8080"
BASE_URL="${SERVER}/v1/core/instance"
INSTANCE_ID="${1:-}"  # Lấy từ command line argument

if [ -z "$INSTANCE_ID" ]; then
  echo "Usage: $0 <instance-id>"
  echo "Example: $0 a5204fc9-9a59-f80f-fb9f-bf3b42214943"
  exit 1
fi

echo "=== Testing Events & Output API for Instance: ${INSTANCE_ID} ==="
echo ""

# Test 1: Consume Events
echo "1. Testing Consume Events..."
curl -s -X GET "${BASE_URL}/${INSTANCE_ID}/consume_events" \
  -H "Content-Type: application/json" | jq .
echo ""

# Test 2: Enable HLS Output
echo "2. Testing Enable HLS Output..."
curl -s -X POST "${BASE_URL}/${INSTANCE_ID}/output/hls" \
  -H "Content-Type: application/json" \
  -d '{"enabled": true}' | jq .
echo ""

# Test 3: Enable RTSP Output
echo "3. Testing Enable RTSP Output..."
curl -s -X POST "${BASE_URL}/${INSTANCE_ID}/output/rtsp" \
  -H "Content-Type: application/json" \
  -d '{"enabled": true}' \
  -w "\nHTTP Status: %{http_code}\n"
echo ""

# Test 4: Consume Events again
echo "4. Testing Consume Events (after enabling outputs)..."
sleep 2
curl -s -X GET "${BASE_URL}/${INSTANCE_ID}/consume_events" \
  -H "Content-Type: application/json" | jq .
echo ""

echo "=== Tests Complete ==="
```

**Sử dụng**:
```bash
chmod +x test_events_output.sh
./test_events_output.sh <instance-id>
```

---

## Notes

1. **Event Queue**: Events được lưu trong memory queue, sẽ bị mất khi server restart
2. **HLS Segments**: HLS segments được tạo tự động, cần đợi một chút sau khi enable
3. **RTSP Server**: Cần có RTSP server (MediaMTX hoặc GStreamer RTSP server) để stream hoạt động
4. **Multiple Outputs**: Có thể enable nhiều output formats cùng lúc (HLS + RTSP)
5. **Event Schema**: Events tuân theo schema tại https://bin.cvedia.com/schema/

---

## Related Documentation

- [TASK-002: Events & Output Configuration](../../../task/omniapi_task/TASK-002-Events-Output.md)
- [API Documentation](../../../api-specs/openapi/en/openapi.yaml)
- [Scalar Documentation](../../../api-specs/README.md)

