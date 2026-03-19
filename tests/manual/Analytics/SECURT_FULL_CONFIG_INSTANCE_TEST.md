# Hướng Dẫn Manual Test - Tạo SecuRT Instance bằng một file JSON Full Config

## Mục tiêu

- Tạo một instance SecuRT **đầy đủ** (core instance + input + output + bài toán analytics) chỉ bằng **một file JSON**.
- Xác nhận instance chạy ổn định, có statistics và sinh analytics qua MQTT.
- Test với các bài toán khác nhau (lines và areas).

## Tổng quan các bài toán SecuRT

SecuRT hỗ trợ **17 bài toán analytics**:

### Lines (3 bài toán):
1. **Crossing Lines** - Phát hiện đối tượng vượt qua đường
2. **Counting Lines** - Đếm số lượng đối tượng qua đường
3. **Tailgating Lines** - Phát hiện bám đuôi

### Areas (14 bài toán):
1. **Crossing Areas** - Phát hiện đối tượng vượt qua vùng
2. **Intrusion Areas** - Phát hiện xâm nhập vào vùng cấm
3. **Loitering Areas** - Phát hiện lượn vòng trong vùng
4. **Crowding Areas** - Phát hiện tụ tập đông người
5. **Occupancy Areas** - Phát hiện chiếm dụng vùng
6. **Crowd Estimation Areas** - Ước lượng số lượng đám đông
7. **Dwelling Areas** - Phát hiện cư trú lâu trong vùng
8. **Armed Person Areas** - Phát hiện người mang vũ khí
9. **Object Left Areas** - Phát hiện vật bỏ quên
10. **Object Removed Areas** - Phát hiện vật bị dời
11. **Fallen Person Areas** - Phát hiện người ngã
12. **Vehicle Guard Areas** - Bảo vệ xe (thử nghiệm)
13. **Face Covered Areas** - Phát hiện che mặt (thử nghiệm)
14. **Object Enter Exit Areas** - Phát hiện vật vào/ra vùng

## 1. Chuẩn bị

- API server đã chạy:

```bash
curl http://localhost:8080/v1/core/health
```

- Có camera RTSP hoặc RTSP test stream.
- Có MQTT broker (ví dụ: `localhost:1883`).
- Công cụ:
  - `curl`, `jq`
  - `mosquitto_sub` (để xem MQTT events, nếu có)

## 2. File JSON full-config mẫu

Có nhiều file mẫu cho các bài toán khác nhau:

**Lines:**
- `flexible_securt_rtsp_mqtt_crossing.json` - Crossing line
- `flexible_securt_rtsp_mqtt_counting.json` - Counting line
- `flexible_securt_rtsp_mqtt_tailgating.json` - Tailgating line

**Areas:**
- `flexible_securt_rtsp_mqtt_intrusion.json` - Intrusion area
- `flexible_securt_rtsp_mqtt_loitering.json` - Loitering area
- `flexible_securt_rtsp_mqtt_crowding.json` - Crowding area
- `flexible_securt_rtsp_mqtt_occupancy.json` - Occupancy area
- `flexible_securt_rtsp_mqtt_fallenperson.json` - Fallen person area
- `flexible_securt_rtsp_mqtt_mixed.json` - Mixed analytics (nhiều bài toán)

Cập nhật lại các trường:

- `RTSP_SRC_URL`: URL camera/stream thực tế (hoặc `RTMP_SRC_URL`, `FILE_PATH`, `HLS_URL`).
- `MQTT_BROKER_URL`, `MQTT_PORT`, `MQTT_TOPIC`: theo môi trường test.
- Coordinates của lines/areas: điều chỉnh theo vị trí thực tế trong frame.

Ví dụ (trích):

```json
{
  "name": "securt_rtsp_mqtt_crossing",
  "solution": "securt",
  "autoStart": false,
  "additionalParams": {
    "input": {
      "RTSP_SRC_URL": "rtsp://192.168.1.100:554/stream1"
    },
    "output": {
      "MQTT_BROKER_URL": "localhost",
      "MQTT_PORT": "1883",
      "MQTT_TOPIC": "securt/events"
    }
  }
}
```

## 3. Tạo instance từ JSON

```bash
SERVER="http://localhost:8080"
CORE_BASE="${SERVER}/v1/core/instance"

JSON_PATH="examples/instances/other_solutions/securt/flexible_securt_rtsp_mqtt_crossing.json"

RESPONSE=$(curl -s -X POST "${CORE_BASE}" \
  -H "Content-Type: application/json" \
  -d @"${JSON_PATH}")

echo "${RESPONSE}" | jq .

INSTANCE_ID=$(echo "${RESPONSE}" | jq -r '.instanceId')
echo "INSTANCE_ID=${INSTANCE_ID}"
```

**Expected**:

- HTTP 201, response chứa `instanceId`.

## 4. Start instance

```bash
curl -s -X POST "${CORE_BASE}/${INSTANCE_ID}/start" | jq .
```

**Expected**:

- Trạng thái `running: true` trong response hoặc trong `GET /v1/core/instance/{id}`.

## 5. Kiểm tra SecuRT wrapper & analytics

### 5.1. Kiểm tra statistics

```bash
SECURT_BASE="${SERVER}/v1/securt/instance"

sleep 5
curl -s -X GET "${SECURT_BASE}/${INSTANCE_ID}/stats" | jq .
```

**Expected**:

- `isRunning: true`
- `frameRate > 0`
- `framesProcessed` tăng sau vài giây.

### 5.2. Kiểm tra analytics entities (lines)

```bash
curl -s -X GET "${SECURT_BASE}/${INSTANCE_ID}/analytics_entities" | jq .
```

**Expected**:

- Có ít nhất một crossing line tương ứng cấu hình trong JSON (ví dụ `Entrance Line`).

### 5.3. Kiểm tra MQTT events (nếu có)

Trong terminal khác:

```bash
mosquitto_sub -h localhost -p 1883 -t "securt/events"
```

**Expected**:

- Nhận được events khi có đối tượng đi qua line.

## 6. Cleanup

```bash
curl -s -X POST "${CORE_BASE}/${INSTANCE_ID}/stop" | jq .
curl -s -X DELETE "${SERVER}/v1/securt/instance/${INSTANCE_ID}" -v
```

**Expected**:

- Stop: HTTP 200.
- Delete: HTTP 204.

## 7. Test với các bài toán khác

Bạn có thể test với các file JSON khác:

```bash
# Test counting line
JSON_PATH="examples/instances/other_solutions/securt/flexible_securt_rtsp_mqtt_counting.json"

# Test intrusion area
JSON_PATH="examples/instances/other_solutions/securt/flexible_securt_rtsp_mqtt_intrusion.json"

# Test loitering area
JSON_PATH="examples/instances/other_solutions/securt/flexible_securt_rtsp_mqtt_loitering.json"

# Test mixed analytics
JSON_PATH="examples/instances/other_solutions/securt/flexible_securt_rtsp_mqtt_mixed.json"
```

Sau đó lặp lại các bước 3-6.

## 8. Checklist

- [ ] Tạo instance từ một file JSON thành công.
- [ ] Start instance thành công.
- [ ] Có statistics hợp lệ (`frameRate > 0`, `isRunning: true`).
- [ ] Analytics entities xuất hiện trong `GET /v1/securt/instance/{id}/analytics_entities`.
- [ ] MQTT nhận được events khi có sự kiện (nếu cấu hình).
- [ ] Test với ít nhất 3 bài toán khác nhau (ví dụ: crossing line, intrusion area, loitering area).

