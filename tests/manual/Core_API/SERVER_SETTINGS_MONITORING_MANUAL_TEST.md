# Hướng dẫn test thủ công – Cấu hình server & API giám sát (toàn bộ tính năng)

> Tài liệu test **toàn bộ API** liên quan cấu hình server (thread, instance limit, web server, logging, monitoring) và **giám sát** (system info, system status, watchdog, endpoints, metrics). Copy lệnh curl từng bước và kiểm tra response.

---

## Mục lục

1. [Tổng quan](#tổng-quan)
2. [Chuẩn bị](#chuẩn-bị)
3. [Biến dùng chung](#biến-dùng-chung)
4. [PHẦN A: Config API](#phần-a-config-api)
5. [PHẦN B: System Info & System Status](#phần-b-system-info--system-status)
6. [PHẦN C: Watchdog & Health Monitor](#phần-c-watchdog--health-monitor)
7. [PHẦN D: Endpoints & Metrics](#phần-d-endpoints--metrics)
8. [PHẦN E: Health & Version](#phần-e-health--version)
9. [PHẦN F: System Config (entity API)](#phần-f-system-config-entity-api)
10. [Bảng tóm tắt API](#bảng-tóm-tắt-api)
11. [Troubleshooting](#troubleshooting)
12. [Tài liệu liên quan](#tài-liệu-liên-quan)

---

## Tổng quan

Các API trong test này đều **đã tồn tại** trong project. Test thủ công giúp xác nhận:

- **Config API** (`/v1/core/config`): đọc/ghi từng nhóm cấu hình (max_running_instances, performance threads, web_server, logging, monitoring gồm **max_cpu_percent**, **max_ram_percent**).
- **System info/status/resource-status**: số core, RAM, % CPU, % RAM, load average, uptime; **resource-status** trả về limits + current + over_limits để giám sát và tùy chỉnh ngưỡng.
- **Watchdog**: trạng thái watchdog, health monitor (CPU %, RAM MB), device report config.
- **Endpoints / Metrics**: thống kê request, latency, Prometheus/JSON.

---

## Chuẩn bị

1. API server đang chạy (ví dụ `http://localhost:8080`).
2. **curl** (bắt buộc). **jq** (tùy chọn, để format JSON).
3. Windows: có thể dùng PowerShell hoặc Postman/Swagger (`http://<host>:8080/v1/swagger`).

---

## Biến dùng chung

```bash
SERVER="http://localhost:8080"
CONFIG="${SERVER}/v1/core/config"
SYSTEM="${SERVER}/v1/core/system"
WATCHDOG="${SERVER}/v1/core/watchdog"
CORE="${SERVER}/v1/core"
```

---

## PHẦN A: Config API

Đọc/ghi cấu hình qua GET/POST/PATCH `/v1/core/config` với query `path` khi cần.

### A.1. Lấy toàn bộ config

```bash
echo "=== A.1 GET full config ==="
curl -s "${CONFIG}" | jq .
```

**Expected:** Status 200, JSON có ít nhất `system`.

### A.2. Đọc max_running_instances

```bash
echo "=== A.2 GET system/max_running_instances ==="
curl -s "${CONFIG}?path=system/max_running_instances" | jq .
```

**Expected:** Số (0 = không giới hạn).

### A.3. Đọc performance – max thread / min thread (số thread HTTP server)

```bash
echo "=== A.3 GET system/performance (max thread, min thread) ==="
curl -s "${CONFIG}?path=system/performance" | jq .
```

**Expected:** Object có **thread_num** (0 = tự động theo CPU), **min_threads**, **max_threads**. Đây là giới hạn số thread của HTTP server; chỉnh qua config hoặc xem gộp trong B.4 (resource-status).

### A.4. Đọc web_server

```bash
echo "=== A.4 GET system/web_server ==="
curl -s "${CONFIG}?path=system/web_server" | jq .
```

**Expected:** enabled, port, ip_address/bind_mode, max_body_size, keepalive_*.

### A.5. Đọc logging (retention_days, cleanup_interval_hours, max_disk_usage_percent)

```bash
echo "=== A.5 GET system/logging ==="
curl -s "${CONFIG}?path=system/logging" | jq .
```

**Expected:** retention_days, max_disk_usage_percent, cleanup_interval_hours, log_level, ...

### A.6. Đọc monitoring

```bash
echo "=== A.6 GET system/monitoring ==="
curl -s "${CONFIG}?path=system/monitoring" | jq .
```

**Expected:** watchdog_check_interval_ms, health_monitor_interval_ms, **max_cpu_percent**, **max_ram_percent** (0 = tắt; khi > 0 thì tạo instance mới bị 503 nếu vượt ngưỡng), device_report.

### A.7. Cập nhật max_running_instances (POST merge)

```bash
echo "=== A.7 POST config: max_running_instances=4 ==="
curl -s -X POST "${CONFIG}" -H "Content-Type: application/json" \
  -d '{"system":{"max_running_instances":4}}' | jq .
```

Gọi lại A.2 để xác nhận. Có thể đặt lại 0 sau test.

### A.8. Cập nhật logging retention và cleanup (POST merge)

```bash
echo "=== A.8 POST config: retention_days=14, cleanup_interval_hours=12 ==="
curl -s -X POST "${CONFIG}" -H "Content-Type: application/json" \
  -d '{"system":{"logging":{"retention_days":14,"cleanup_interval_hours":12}}}' | jq .
```

Gọi lại A.5 để xác nhận.

### A.9. Cập nhật max thread / min thread (PATCH system/performance)

```bash
echo "=== A.9 PATCH system/performance (min_threads=8, max_threads=32) ==="
curl -s -X PATCH "${CONFIG}?path=system/performance" -H "Content-Type: application/json" \
  -d '{"min_threads":8,"max_threads":32}' | jq .
```

**Expected:** Status 200/204. Gọi lại A.3 hoặc B.4 (resource-status) để xác nhận limits.min_threads, limits.max_threads. Thay đổi có hiệu lực sau khi restart server.

### A.10. Cập nhật monitoring (max_cpu_percent, max_ram_percent)

```bash
echo "=== A.10 POST config: max_cpu_percent=90, max_ram_percent=85 ==="
curl -s -X POST "${CONFIG}" -H "Content-Type: application/json" \
  -d '{"system":{"monitoring":{"max_cpu_percent":90,"max_ram_percent":85}}}' | jq .
```

**Expected:** Status 200/204. Gọi A.6 hoặc B.4 (resource-status) để xác nhận. Khi CPU hoặc RAM hệ thống >= ngưỡng, POST /v1/core/instance hoặc /v1/core/instance/quick sẽ trả 503. Đặt lại 0 để tắt giới hạn.

---

## PHẦN B: System Info & System Status

### B.1. GET /v1/core/system/info

```bash
echo "=== B.1 GET system/info ==="
curl -s "${SYSTEM}/info" | jq .
```

**Expected:** cpu (physical_cores, logical_cores, ...), ram (size_mib, ...), gpu, disk, mainboard, os.

### B.2. Chỉ cpu & ram

```bash
curl -s "${SYSTEM}/info" | jq '{cpu, ram}'
```

### B.3. GET /v1/core/system/status (CPU %, RAM %, load, uptime)

```bash
echo "=== B.3 GET system/status ==="
curl -s "${SYSTEM}/status" | jq .
```

**Expected:** cpu.usage_percent, ram.usage_percent, load_average, uptime_seconds.

### B.4. GET /v1/core/system/resource-status (limits + current + over_limits)

```bash
echo "=== B.4 GET system/resource-status ==="
curl -s "${SYSTEM}/resource-status" | jq .
```

**Expected:** **limits** gồm max_running_instances, max_cpu_percent, max_ram_percent, **thread_num** (0 = auto), **min_threads**, **max_threads**; **current** (instance_count, cpu_usage_percent, ram_usage_percent); **over_limits** (at_instance_limit, over_cpu_limit, over_ram_limit). Dùng API này để giám sát và tùy chỉnh mọi ngưỡng (instance, CPU, RAM, **max thread**) qua config.

### B.5. Chỉnh ngưỡng CPU/RAM (POST config) rồi kiểm tra resource-status

```bash
# Đặt max_cpu_percent=90, max_ram_percent=90 (0 = tắt)
curl -s -X POST "${CONFIG}" -H "Content-Type: application/json" \
  -d '{"system":{"monitoring":{"max_cpu_percent":90,"max_ram_percent":90}}}' | jq .

# Xem lại limits và current
curl -s "${SYSTEM}/resource-status" | jq .
```

**Expected:** limits.max_cpu_percent và max_ram_percent = 90. Khi CPU hoặc RAM hệ thống >= 90%, tạo instance mới sẽ nhận 503.

---

## PHẦN C: Watchdog & Health Monitor

### C.1. GET /v1/core/watchdog

```bash
echo "=== C.1 GET watchdog ==="
curl -s "${WATCHDOG}" | jq .
```

**Expected:** watchdog (running, is_healthy, ...), health_monitor (cpu_usage_percent, memory_usage_mb, ...), device_report.

### C.2. GET /v1/core/watchdog/config

```bash
echo "=== C.2 GET watchdog/config ==="
curl -s "${WATCHDOG}/config" | jq .
```

### C.3. PUT /v1/core/watchdog/config (ví dụ interval_sec)

```bash
echo "=== C.3 PUT watchdog/config interval_sec=600 ==="
curl -s -X PUT "${WATCHDOG}/config" -H "Content-Type: application/json" \
  -d '{"interval_sec":600}' | jq .
```

Gọi lại C.2 để xác nhận.

---

## PHẦN D: Endpoints & Metrics

### D.1. GET /v1/core/endpoints

```bash
echo "=== D.1 GET endpoints ==="
curl -s "${CORE}/endpoints" | jq .
```

**Expected:** endpoints (object), total_endpoints.

### D.2. GET /v1/core/metrics (Prometheus)

```bash
echo "=== D.2 GET metrics (Prometheus) ==="
curl -s "${CORE}/metrics"
```

### D.3. GET /v1/core/metrics (JSON)

```bash
echo "=== D.3 GET metrics (JSON) ==="
curl -s "${CORE}/metrics?format=json" | jq .
```

**Expected:** endpoints, overall (total_requests, throughput_rps, ...).

---

## PHẦN E: Health & Version

### E.1. GET /v1/core/health

```bash
echo "=== E.1 GET health ==="
curl -s "${CORE}/health" | jq .
```

### E.2. GET /v1/core/version

```bash
echo "=== E.2 GET version ==="
curl -s "${CORE}/version" | jq .
```

---

## PHẦN F: System Config (entity API)

### F.1. GET /v1/core/system/config

```bash
echo "=== F.1 GET system/config (entities) ==="
curl -s "${SYSTEM}/config" | jq .
```

**Expected:** systemConfig (array) với fieldId, displayName, type, value, group, availableValues.

### F.2. PUT /v1/core/system/config (nếu có entity)

```bash
echo "=== F.2 PUT system/config ==="
curl -s -X PUT "${SYSTEM}/config" -H "Content-Type: application/json" \
  -d '{"systemConfig":[{"fieldId":"voice6","value":"solutions"}]}' | jq .
```

Chi tiết xem SYSTEM_CONFIG_MANUAL_TEST.md.

---

## Bảng tóm tắt API

| Mục đích | Method | URL |
|----------|--------|-----|
| Đọc toàn bộ config | GET | /v1/core/config |
| Đọc section (max thread, min thread) | GET | /v1/core/config?path=system/performance |
| Cập nhật max/min thread | PATCH | /v1/core/config?path=system/performance |
| Cập nhật config (merge) | POST | /v1/core/config |
| Cập nhật section | PATCH | /v1/core/config?path=... |
| Thông tin phần cứng | GET | /v1/core/system/info |
| Trạng thái CPU/RAM % | GET | /v1/core/system/status |
| Giới hạn (instance, CPU%, RAM%, max thread) + current + over_limits | GET | /v1/core/system/resource-status |
| Watchdog + health | GET | /v1/core/watchdog |
| Device report config | GET/PUT | /v1/core/watchdog/config |
| Thống kê endpoint | GET | /v1/core/endpoints |
| Metrics | GET | /v1/core/metrics, ?format=json |
| Health / Version | GET | /v1/core/health, /v1/core/version |
| System config entity | GET/PUT | /v1/core/system/config |

---

## Troubleshooting

- **Connection refused:** Kiểm tra server, host, port.
- **404 config path:** Dùng path đúng (system/performance, system.logging, ...).
- **500 POST/PATCH config:** JSON hợp lệ, quyền ghi file config.
- **503 khi tạo instance:** Đã bật max_cpu_percent hoặc max_ram_percent và hệ thống đang vượt ngưỡng. Xem GET /v1/core/system/resource-status; giảm tải hoặc tăng ngưỡng / đặt 0 để tắt.
- **metrics rỗng:** Bình thường nếu chưa có request; gọi vài API rồi thử lại.
- **jq not found:** Bỏ `| jq .` trong lệnh curl.

---

## Tài liệu liên quan

- **Reference đầy đủ:** docs/SERVER_SETTINGS_AND_MONITORING_API.md
- **Log config:** LOG_CONFIG_MANUAL_TEST.md, LOG_CONFIG_API_GUIDE.md
- **Watchdog/Device report:** WATCHDOG_DEVICE_REPORT_MANUAL_TEST.md, WATCHDOG_DEVICE_REPORT_CONFIG_API.md
- **Bind & restart:** CONFIG_BIND_AND_RESTART_MANUAL_TEST.md
- **System config entity:** SYSTEM_CONFIG_MANUAL_TEST.md, SYSTEM_CONFIG_QUICK_TEST.md
