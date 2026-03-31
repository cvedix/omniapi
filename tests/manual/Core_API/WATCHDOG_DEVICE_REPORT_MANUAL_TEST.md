# Hướng Dẫn Test Thủ Công - Watchdog & Device Report API

## Mục Lục

1. [Tổng Quan](#tổng-quan)
2. [Prerequisites](#prerequisites)
3. [Setup Variables](#setup-variables)
4. [Test Cases](#test-cases)
5. [Kịch Bản Full](#kịch-bản-full)
6. [Expected Results](#expected-results)
7. [Troubleshooting](#troubleshooting)

---

## Tổng Quan

Tài liệu này hướng dẫn test thủ công các endpoints liên quan **Watchdog** và **Device Report (OsmAnd/Traccar)**:

- **GET /v1/core/watchdog**: Lấy trạng thái watchdog ứng dụng, health monitor và device report (khi bật).
- **GET /v1/core/watchdog/config**: Lấy cấu hình device report (bật/tắt, server, chu kỳ, …); có `_description` giải thích từng trường.
- **PUT /v1/core/watchdog/config**: Cập nhật cấu hình device report (chỉ gửi trường cần đổi); áp dụng ngay, không cần restart.
- **GET /v1/core/watchdog/report-now**: Gửi một report thủ công lên Traccar (OsmAnd). Chỉ hoạt động khi device report được bật và server reachable.

### Luồng Device Report

- Chỉ gửi report khi **server Traccar reachable** (kiểm tra bằng HTTP GET).
- Nếu không reachable → không gửi, retry kết nối với backoff.
- Report gửi: startup (khi reachable lần đầu), định kỳ theo `interval_sec` (mặc định 5 phút = 300s), và manual qua API.

### Base URL

```
http://localhost:8080/v1/core/watchdog
```

---

## Prerequisites

1. **API Server đang chạy**:
   ```bash
   curl -s http://localhost:8080/v1/core/health | jq .
   # Expected: "status": "healthy"
   ```

2. **Công cụ**:
   - `curl`
   - `jq` (optional, để format JSON)

3. **(Optional) Traccar server** cho test Device Report:
   - Traccar chạy với OsmAnd port (mặc định 5055).
   - Hoặc dùng URL giả lập (vd. httpbin.org) để test reachability/response.

---

## Setup Variables

```bash
# Thay đổi theo môi trường
SERVER="http://localhost:8080"
BASE="${SERVER}/v1/core/watchdog"
```

---

## Test Cases

### 1. GET /v1/core/watchdog — Device report tắt (mặc định)

#### Test Case 1.1: Lấy trạng thái watchdog (device report chưa bật)

```bash
echo "=== 1.1 GET /v1/core/watchdog (device_report off) ==="
curl -s -X GET "${BASE}" -H "Accept: application/json" | jq .
```

**Expected**:
- Status: `200`
- Body có: `watchdog`, `health_monitor`, `device_report`.
- `watchdog`: `running`, `total_heartbeats`, `missed_heartbeats`, `recovery_actions`, `is_healthy`, `seconds_since_last_heartbeat`.
- `health_monitor`: `running`, `cpu_usage_percent`, `memory_usage_mb`, `request_count`, `error_count`.
- `device_report`: `enabled` = `false` (khi không cấu hình bật).

**Ví dụ response (rút gọn)**:
```json
{
  "watchdog": {
    "running": true,
    "total_heartbeats": 42,
    "missed_heartbeats": 0,
    "recovery_actions": 0,
    "is_healthy": true,
    "seconds_since_last_heartbeat": 1
  },
  "health_monitor": {
    "running": true,
    "cpu_usage_percent": 0,
    "memory_usage_mb": 128,
    "request_count": 10,
    "error_count": 0
  },
  "device_report": {
    "enabled": false
  }
}
```

---

### 2. GET /v1/core/watchdog/report-now — Device report tắt

#### Test Case 2.1: Gọi report-now khi chưa bật device report

```bash
echo "=== 2.1 GET /v1/core/watchdog/report-now (disabled) ==="
curl -s -X GET "${BASE}/report-now" -H "Accept: application/json" | jq .
```

**Expected**:
- Status: `200`
- Body: `sent` = `false`, `error` = `"Device report not enabled"`.

```json
{
  "sent": false,
  "error": "Device report not enabled"
}
```

---

### 3. Bật Device Report — Cấu hình qua Environment

Khởi động lại API với env (thay URL Traccar thật nếu có):

```bash
export DEVICE_REPORT_ENABLED=1
export DEVICE_REPORT_SERVER_URL="http://localhost:5055"
export DEVICE_REPORT_DEVICE_ID="manual-test-01"
export DEVICE_REPORT_DEVICE_TYPE="aibox"
export DEVICE_REPORT_INTERVAL_SEC=300
export DEVICE_REPORT_LATITUDE=21.0285
export DEVICE_REPORT_LONGITUDE=105.8542

# Khởi động lại server (vd. từ build)
./build/omniapi
```

Hoặc một dòng để test nhanh (server URL có thể unreachable):

```bash
DEVICE_REPORT_ENABLED=1 DEVICE_REPORT_SERVER_URL="http://127.0.0.1:5055" ./build/omniapi
```

---

### 4. GET /v1/core/watchdog — Device report bật

#### Test Case 4.1: Trạng thái khi device report bật (server chưa chạy)

Sau khi bật bằng env như trên, gọi:

```bash
echo "=== 4.1 GET /v1/core/watchdog (device_report on, server down) ==="
curl -s -X GET "${BASE}" -H "Accept: application/json" | jq .
```

**Expected**:
- `device_report.enabled` = `true`
- `device_report.server` = URL đã cấu hình
- `device_report.device_id` = `manual-test-01` (hoặc giá trị đã set)
- `device_report.report_count` = 0 (nếu chưa bao giờ gửi thành công)
- `device_report.last_status` có thể `"network_error"` hoặc `""`
- `device_report.server_reachable` = `false` (nếu Traccar không chạy)

#### Test Case 4.2: Trạng thái khi device report bật và server reachable

Khi Traccar (hoặc mock) chạy và đã gửi ít nhất một report:

```bash
echo "=== 4.2 GET /v1/core/watchdog (device_report on, server up) ==="
curl -s -X GET "${BASE}" | jq '.device_report'
```

**Expected** (ví dụ):
- `server_reachable`: `true`
- `last_status`: `"success"`
- `report_count`: >= 1
- `last_report`: chuỗi thời gian ISO (vd. `2026-03-08T14:30:00Z`)

---

### 5. GET /v1/core/watchdog/report-now — Device report bật

#### Test Case 5.1: Gửi report thủ công — Server unreachable

Device report bật nhưng Traccar không chạy:

```bash
echo "=== 5.1 GET report-now (server down) ==="
curl -s -X GET "${BASE}/report-now" | jq .
```

**Expected**:
- Status: `200`
- `sent` = `false`
- `last_status` = `"network_error"` (hoặc tương tự)

#### Test Case 5.2: Gửi report thủ công — Server reachable

Traccar chạy (port 5055) và device đã được tạo với `identifier` = `device_id` trong config:

```bash
echo "=== 5.2 GET report-now (server up) ==="
curl -s -X GET "${BASE}/report-now" | jq .
```

**Expected**:
- Status: `200`
- `sent` = `true`
- `event` = `"manual"`
- `report_count` tăng (số nguyên)
- `last_report` có giá trị thời gian

**Ví dụ**:
```json
{
  "sent": true,
  "event": "manual",
  "report_count": 3,
  "last_report": "2026-03-08T14:35:00Z"
}
```

#### Test Case 5.3: Gọi report-now nhiều lần

```bash
for i in 1 2 3; do
  echo "=== Report-now lần $i ==="
  curl -s -X GET "${BASE}/report-now" | jq '.sent, .report_count'
  sleep 2
done
```

**Expected**: `sent` = `true`, `report_count` tăng dần mỗi lần gửi thành công.

---

### 6. Cấu hình qua config.json

Thêm hoặc sửa trong `config.json` (path tùy môi trường, vd. `./config.json` hoặc `/opt/omniapi/config/config.json`):

```json
{
  "system": {
    "monitoring": {
      "device_report": {
        "enabled": true,
        "server_url": "http://traccar.example.com:5055",
        "device_id": "aibox-01",
        "device_type": "aibox",
        "interval_sec": 300,
        "latitude": 21.0285,
        "longitude": 105.8542
      }
    }
  }
}
```

Sau đó **restart API** và lặp lại Test Case 4.1, 4.2, 5.1, 5.2 với URL/server tương ứng.

**Kiểm tra**:
```bash
curl -s "${BASE}" | jq '.device_report.server, .device_report.device_id'
# Phải trùng với config (server_url, device_id).
```

---

### 7. GET /v1/core/watchdog/config — Xem cấu hình

```bash
echo "=== 7 GET /v1/core/watchdog/config ==="
curl -s -X GET "${BASE}/config" | jq .
```

**Expected**: Status 200, có `config` với các trường: `enabled`, `server_url`, `device_id`, `device_type`, `interval_sec`, `latitude`, `longitude`, `reachability_timeout_sec`, `report_timeout_sec`, và `_description` (giải thích từng trường).

### 8. PUT /v1/core/watchdog/config — Cập nhật cấu hình (áp dụng ngay)

#### Chỉ bật report và đặt server (các trường khác giữ nguyên)

```bash
curl -s -X PUT "${BASE}/config" \
  -H "Content-Type: application/json" \
  -d '{"enabled": true, "server_url": "http://127.0.0.1:5055", "device_id": "test-device"}' | jq .
```

**Expected**: Status 200, `message` = "Device report config updated and applied.", `config` trả về cấu hình mới. Device report chạy ngay với cấu hình mới (không cần restart API).

#### Chỉ đổi chu kỳ (ví dụ 10 phút)

```bash
curl -s -X PUT "${BASE}/config" -H "Content-Type: application/json" -d '{"interval_sec": 600}' | jq .
```

#### Tắt report qua API

```bash
curl -s -X PUT "${BASE}/config" -H "Content-Type: application/json" -d '{"enabled": false}' | jq .
```

Sau đó gọi GET `/v1/core/watchdog` → `device_report.enabled` = false.

### 9. Kiểm tra cấu trúc response đầy đủ

```bash
echo "=== Cấu trúc đầy đủ GET /v1/core/watchdog ==="
curl -s -X GET "${BASE}" | jq 'keys'
# Expected (ít nhất): ["device_report", "health_monitor", "watchdog"]

curl -s -X GET "${BASE}" | jq '.watchdog | keys'
# Expected: ["is_healthy", "missed_heartbeats", "recovery_actions", "running", "seconds_since_last_heartbeat", "total_heartbeats"] (hoặc tương đương)

curl -s -X GET "${BASE}" | jq '.device_report | keys'

# Cấu hình (GET/PUT /v1/core/watchdog/config)
curl -s -X GET "${BASE}/config" | jq '.config | keys'
# Khi enabled: enabled, server, device_id, report_count, last_report, last_status, server_reachable
# Khi disabled: enabled
```

---

## Kịch Bản Full

Thực hiện tuần tự để cover toàn bộ luồng.

### Bước 0: Chuẩn bị

```bash
SERVER="http://localhost:8080"
BASE="${SERVER}/v1/core/watchdog"

# Đảm bảo server đang chạy
curl -s "${SERVER}/v1/core/health" | jq -r '.status'
# Expected: healthy
```

### Bước 1: Watchdog khi chưa bật Device Report

```bash
curl -s "${BASE}" | jq '{ watchdog: .watchdog.running, health: .health_monitor.running, device_report_enabled: .device_report.enabled }'
# Expected: watchdog true, health true, device_report_enabled false
```

### Bước 2: Report-now khi disabled

```bash
curl -s "${BASE}/report-now" | jq .
# Expected: sent false, error "Device report not enabled"
```

### Bước 3: Bật Device Report (env) và restart server

```bash
# Dừng server hiện tại (Ctrl+C), rồi:
export DEVICE_REPORT_ENABLED=1
export DEVICE_REPORT_SERVER_URL="http://127.0.0.1:5055"
export DEVICE_REPORT_DEVICE_ID="manual-test-device"
./build/omniapi
# Hoặc cách chạy service của bạn
```

### Bước 4: Kiểm tra trạng thái sau khi bật (server Traccar tắt)

```bash
curl -s "${BASE}" | jq '.device_report'
# Expected: enabled true, server "http://127.0.0.1:5055", report_count 0 hoặc 0, server_reachable false nếu không có Traccar
```

### Bước 5: Gọi report-now khi server down

```bash
curl -s "${BASE}/report-now" | jq '{ sent, last_status }'
# Expected: sent false, last_status "network_error" (hoặc tương tự)
```

### Bước 6: (Optional) Bật Traccar và kiểm tra report thành công

Nếu có Traccar tại `http://127.0.0.1:5055`:

```bash
# Gửi report thủ công
curl -s "${BASE}/report-now" | jq .
# Expected: sent true, report_count >= 1, last_report có giá trị

# Kiểm tra lại trạng thái
curl -s "${BASE}" | jq '.device_report'
# Expected: server_reachable true, last_status "success", report_count >= 1
```

### Bước 7: Tắt Device Report và kiểm tra lại

Dừng server, bỏ env (hoặc set `DEVICE_REPORT_ENABLED=0`), restart:

```bash
unset DEVICE_REPORT_ENABLED DEVICE_REPORT_SERVER_URL
# Restart server
```

```bash
curl -s "${BASE}" | jq '.device_report.enabled'
# Expected: false

curl -s "${BASE}/report-now" | jq '.error'
# Expected: "Device report not enabled"
```

---

## Expected Results — Tóm tắt

| Test | Điều kiện | Expected |
|------|-----------|----------|
| GET /watchdog | Bất kỳ | 200, có `watchdog`, `health_monitor`, `device_report` |
| GET /watchdog | device_report off | `device_report.enabled` = false |
| GET /watchdog | device_report on, server down | `device_report.server_reachable` = false, `report_count` có thể 0 |
| GET /watchdog | device_report on, server up | `server_reachable` true, `last_status` "success", `report_count` >= 1 |
| GET /watchdog/report-now | device_report off | 200, `sent` false, `error` "Device report not enabled" |
| GET /watchdog/report-now | device_report on, server down | 200, `sent` false, `last_status` network_error |
| GET /watchdog/report-now | device_report on, server up | 200, `sent` true, `event` "manual", `report_count` tăng |

---

## Troubleshooting

| Vấn đề | Nguyên nhân có thể | Cách xử lý |
|--------|--------------------|------------|
| `device_report` luôn `enabled: false` | Chưa set env hoặc config | Set `DEVICE_REPORT_ENABLED=1` và `DEVICE_REPORT_SERVER_URL`, hoặc thêm `system.monitoring.device_report` trong config.json, rồi restart. |
| `report-now` trả về `sent: false`, `last_status: "network_error"` | Traccar không chạy hoặc sai URL/port | Kiểm tra Traccar (port 5055), firewall, và `server_url`. |
| `last_status: "http_400"` | Device chưa tạo trên Traccar hoặc sai identifier | Trong Traccar: Devices → Add → Identifier phải trùng `device_id` (hoặc hostname nếu để trống). |
| `publicIp` trống trong report | Không ra internet hoặc api.ipify.org chặn | Bình thường trên mạng nội bộ; report vẫn gửi với localIp. |
| Report không gửi định kỳ | Server unreachable tại thời điểm interval | Device Watchdog sẽ retry; khi server lên lại sẽ gửi. Kiểm tra log "[DeviceWatchdog]". |

---

## Tham Chiếu

- Cấu hình: `task/watchdog.md` (Yêu cầu triển khai & tham số OsmAnd).
- Env: `docs/ENVIRONMENT_VARIABLES.md` — mục "Device Report (OsmAnd/Traccar)".
