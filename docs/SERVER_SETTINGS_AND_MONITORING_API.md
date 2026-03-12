# Cấu hình server và API giám sát / kiểm tra thông số

Tài liệu liệt kê **toàn bộ cấu hình liên quan tới server** mà project triển khai, và **các API** để user đọc/ghi cấu hình cũng như **giám sát** (CPU, RAM, thread, watchdog, log retention, v.v.).

---

## 1. Tổng quan API

### 1.1 Đọc / cập nhật cấu hình

| API | Mô tả |
|-----|--------|
| `GET /v1/core/config` | Lấy toàn bộ config hoặc một section (dùng query `?path=...`) |
| `GET /v1/core/config?path=<path>` | Lấy một section theo path (ví dụ: `system/performance`, `system/logging`) |
| `POST /v1/core/config` | Merge cấu hình (chỉ cập nhật các key gửi lên) |
| `PUT /v1/core/config` | Thay thế toàn bộ cấu hình |
| `PATCH /v1/core/config?path=<path>` | Cập nhật một section theo path |
| `DELETE /v1/core/config?path=<path>` | Xóa một section |
| `POST /v1/core/config/reset` | Reset config về mặc định |
| `GET /v1/core/system/config` | Lấy system config dạng entity (fieldId, displayName, type, value, group, availableValues) – dùng cho UI |
| `PUT /v1/core/system/config` | Cập nhật system config entity (JSON body có `systemConfig` array) |

### 1.2 Giám sát (chỉ đọc)

| API | Mô tả |
|-----|--------|
| `GET /v1/core/health` | Health check dịch vụ |
| `GET /v1/core/version` | Phiên bản API |
| `GET /v1/core/system/info` | **Phần cứng**: CPU (số core, tần số), RAM, GPU, disk, mainboard, OS, battery |
| `GET /v1/core/system/status` | **Trạng thái thời gian thực**: % CPU, % RAM, load average, uptime, nhiệt độ CPU |
| `GET /v1/core/system/resource-status` | **Giới hạn và sử dụng**: limits (max_running_instances, max_cpu_percent, max_ram_percent, **thread_num, min_threads, max_threads**), current (instance_count, cpu/ram %), over_limits – dùng để giám sát và tùy chỉnh mọi ngưỡng (instance, CPU, RAM, **max thread**) |
| `GET /v1/core/watchdog` | Trạng thái watchdog + health monitor (CPU %, RAM MB, request/error count) |
| `GET /v1/core/watchdog/config` | Cấu hình device report (OsmAnd/Traccar) |
| `PUT /v1/core/watchdog/config` | Cập nhật device report config |
| `GET /v1/core/endpoints` | Thống kê từng endpoint (số request, latency, lỗi) |
| `GET /v1/core/metrics` | Metrics Prometheus (text/plain) hoặc JSON (`Accept: application/json` hoặc `?format=json`) |

---

## 2. Bảng cấu hình server (config paths)

Các key dưới đây nằm trong `config.json` (hoặc section `system`). Dùng **path** với dấu `/` hoặc `.` khi gọi `GET/PATCH /v1/core/config?path=...`.

### 2.1 Giới hạn instance và luồng (cores / threads)

| Path | Mô tả | Mặc định | API đọc/ghi |
|------|--------|----------|-------------|
| `system.max_running_instances` | Số instance AI chạy đồng thời tối đa (0 = không giới hạn) | `0` | GET/POST/PATCH `/v1/core/config` với path `system/max_running_instances` |
| `system.performance.thread_num` | Số thread HTTP server (0 = tự động theo CPU) | `0` | `system/performance` |
| `system.performance.min_threads` | Số thread tối thiểu | `16` | `system/performance` |
| `system.performance.max_threads` | Số thread tối đa | `64` | `system/performance` |

**Lưu ý:** Có thể cấu hình ngưỡng **phần trăm CPU** và **phần trăm RAM** trong `system.monitoring` (xem 2.4). Khi vượt ngưỡng, **tạo instance mới** bị từ chối (503). Giới hạn RAM process có thể đặt thêm ở systemd (ví dụ `MemoryMax=2G`).

### 2.2 Web server (host, port, body size, keepalive)

| Path | Mô tả | Mặc định |
|------|--------|----------|
| `system.web_server.enabled` | Bật/tắt HTTP server | `true` |
| `system.web_server.ip_address` | IP bind (0.0.0.0 = mọi interface) | `"0.0.0.0"` |
| `system.web_server.bind_mode` | `"local"` (127.0.0.1) hoặc `"public"` (0.0.0.0) | `"public"` |
| `system.web_server.port` | Port HTTP | `8080` |
| `system.web_server.name` | Tên server | `"default"` |
| `system.web_server.max_body_size` | Kích thước body tối đa (bytes), ví dụ 500MB | `524288000` |
| `system.web_server.max_memory_body_size` | Body giữ trong RAM tối đa (bytes), ví dụ 100MB | `104857600` |
| `system.web_server.keepalive_requests` | Số request trên một kết nối keepalive | `100` |
| `system.web_server.keepalive_timeout` | Timeout keepalive (giây) | `60` |
| `system.web_server.reuse_port` | SO_REUSEPORT | `true` |
| `system.web_server.cors.enabled` | Bật CORS | `false` |

**API:** Đọc/ghi qua `GET/POST/PATCH /v1/core/config` với path `system/web_server` (hoặc từng key con). Đổi host/port thường cần restart server (có thể dùng `auto_restart=true` khi POST config).

### 2.3 Logging và thời gian xóa log (retention, cleanup)

| Path | Mô tả | Mặc định |
|------|--------|----------|
| `system.logging.enabled` | Bật/tắt toàn bộ logging | `true` |
| `system.logging.log_level` | Mức log: `none`, `fatal`, `error`, `warning`, `info`, `debug`, `verbose` | `"info"` |
| `system.logging.api_enabled` | Log request/response API | `false` |
| `system.logging.instance_enabled` | Log lifecycle instance | `false` |
| `system.logging.sdk_output_enabled` | Log output SDK | `false` |
| `system.logging.log_file` | File log (đường dẫn) | `"logs/api.log"` |
| `system.logging.log_dir` | Thư mục log | `"./logs"` |
| `system.logging.max_log_file_size` | Kích thước tối đa mỗi file log (bytes), ví dụ 50MB | `52428800` |
| `system.logging.max_log_files` | Số file log xoay vòng | `3` |
| **`system.logging.retention_days`** | **Số ngày giữ log trước khi xóa** | **`30`** |
| **`system.logging.max_disk_usage_percent`** | **Phần trăm dung lượng disk tối đa cho log** | **`85`** |
| **`system.logging.cleanup_interval_hours`** | **Chu kỳ chạy cleanup (giờ)** | **`24`** |

**API:** Đọc/ghi qua `GET/POST/PATCH /v1/core/config` với path `system/logging`.

### 2.4 Monitoring (watchdog, health monitor, giới hạn CPU/RAM)

| Path | Mô tả | Mặc định |
|------|--------|----------|
| `system.monitoring.watchdog_check_interval_ms` | Chu kỳ kiểm tra watchdog (ms) | `5000` |
| `system.monitoring.watchdog_timeout_ms` | Timeout watchdog (ms) | `30000` |
| `system.monitoring.health_monitor_interval_ms` | Chu kỳ lấy mẫu CPU/RAM (ms) | `1000` |
| **`system.monitoring.max_cpu_percent`** | **Ngưỡng % CPU hệ thống (0 = tắt). Khi >= ngưỡng thì từ chối tạo instance mới (503).** | **`0`** |
| **`system.monitoring.max_ram_percent`** | **Ngưỡng % RAM hệ thống (0 = tắt). Khi >= ngưỡng thì từ chối tạo instance mới (503).** | **`0`** |
| `system.monitoring.device_report.*` | Cấu hình gửi báo cáo thiết bị (OsmAnd/Traccar) | Xem bảng dưới |

**Device report (OsmAnd/Traccar):**

| Path | Mô tả | Mặc định |
|------|--------|----------|
| `system.monitoring.device_report.enabled` | Bật gửi report | `false` |
| `system.monitoring.device_report.server_url` | URL server | `""` |
| `system.monitoring.device_report.device_id` | Mã thiết bị (trống = hostname) | `""` |
| `system.monitoring.device_report.device_type` | Loại thiết bị | `"aibox"` |
| `system.monitoring.device_report.interval_sec` | Chu kỳ gửi (giây) | `300` |
| `system.monitoring.device_report.latitude` / `longitude` | Tọa độ (thiết bị cố định) | `0.0` |
| `system.monitoring.device_report.reachability_timeout_sec` | Timeout kiểm tra server | `10` |
| `system.monitoring.device_report.report_timeout_sec` | Timeout gửi report | `30` |

**API:**  
- Config chung: `GET/PATCH /v1/core/config` với path `system/monitoring`.  
- Device report: `GET /v1/core/watchdog/config`, `PUT /v1/core/watchdog/config`.

### 2.5 Khác (thư mục, auto reload, modelforge)

| Path | Mô tả | Mặc định |
|------|--------|----------|
| `system.auto_reload` | Tự restart khi config thay đổi (cần hỗ trợ) | `false` |
| `system.modelforge_permissive` | Chế độ permissive cho modelforge | `false` |
| `system.directories.instances_dir` | Thư mục instances (trống = tự động) | `""` |
| `system.directories.models_dir` | Thư mục models | `""` |
| `system.directories.solutions_dir` | Thư mục solutions | `""` |
| `system.directories.videos_dir` | Thư mục videos | `""` |
| `system.directories.fonts_dir` | Thư mục fonts | `""` |
| `system.directories.nodes_dir` | Thư mục nodes | `""` |

---

## 3. API giám sát – thông số trả về

### 3.1 `GET /v1/core/system/info` – Thông tin phần cứng (tĩnh)

- **cpu:** vendor, model, physical_cores, logical_cores, max_frequency, regular_frequency, current_frequency, min_frequency, cache_size  
- **ram:** vendor, model, name, serial_number, size_mib, free_mib, available_mib  
- **gpu:** mảng { vendor, model, driver_version, memory_mib, current_frequency, ... }  
- **disk:** mảng { vendor, model, size_bytes, free_size_bytes, volumes }  
- **mainboard:** vendor, name, version, serial_number  
- **os:** name, version, kernel, architecture, endianess, short_name  
- **battery:** mảng (nếu có)

Dùng để biết **số core, số luồng (logical_cores), dung lượng RAM, GPU, ổ đĩa**.

### 3.2 `GET /v1/core/system/status` – Trạng thái thời gian thực

- **cpu:** usage_percent, current_frequency_mhz, temperature_celsius (nếu có)  
- **ram:** total_mib, used_mib, free_mib, available_mib, usage_percent, cached_mib, buffers_mib  
- **load_average:** 1min, 5min, 15min  
- **uptime_seconds**

Dùng để giám sát **phần trăm CPU, phần trăm RAM, load, uptime**.

### 3.3 `GET /v1/core/watchdog` – Watchdog và health monitor

- **watchdog:** running, total_heartbeats, missed_heartbeats, recovery_actions, is_healthy, seconds_since_last_heartbeat  
- **health_monitor:** running, **cpu_usage_percent**, **memory_usage_mb**, request_count, error_count  
- **device_report:** enabled, server, device_id, report_count, last_report, last_status, server_reachable

Dùng để giám sát **CPU %, RAM (MB)** của process và trạng thái watchdog.

### 3.4 `GET /v1/core/endpoints` – Thống kê endpoint

- **endpoints:** object theo từng endpoint với total_requests, successful_requests, failed_requests, avg_latency_ms, max_latency_ms, min_latency_ms  
- **total_endpoints**

### 3.5 `GET /v1/core/metrics` – Prometheus / JSON

- **Prometheus:** `Accept: text/plain` hoặc không truyền `format=json` → text dạng Prometheus (http_requests_total, http_request_duration_seconds, ...).  
- **JSON:** `Accept: application/json` hoặc `?format=json` → JSON với **endpoints** (per-endpoint metrics) và **overall** (total_requests, successful_requests, failed_requests, avg_latency_ms, throughput_rps).

---

## 4. Ví dụ nhanh

### Đọc giới hạn instance và performance (thread)

```bash
# Toàn bộ system
curl -s "http://localhost:8080/v1/core/config?path=system" | jq

# Chỉ max_running_instances và performance
curl -s "http://localhost:8080/v1/core/config?path=system/max_running_instances"
curl -s "http://localhost:8080/v1/core/config?path=system/performance"
```

### Đọc cấu hình logging (retention, cleanup, % disk)

```bash
curl -s "http://localhost:8080/v1/core/config?path=system/logging" | jq
```

### Đọc trạng thái CPU/RAM thời gian thực

```bash
curl -s "http://localhost:8080/v1/core/system/status" | jq
curl -s "http://localhost:8080/v1/core/watchdog" | jq '.health_monitor'
```

### Đọc thông tin CPU cores / RAM (phần cứng)

```bash
curl -s "http://localhost:8080/v1/core/system/info" | jq '.cpu, .ram'
```

### Cập nhật max_running_instances và retention_days

```bash
# Merge update
curl -s -X POST "http://localhost:8080/v1/core/config" \
  -H "Content-Type: application/json" \
  -d '{"system":{"max_running_instances":4,"logging":{"retention_days":14}}}'

# Hoặc PATCH từng path
curl -s -X PATCH "http://localhost:8080/v1/core/config?path=system/max_running_instances" \
  -H "Content-Type: application/json" \
  -d '4'
```

---

## 5. Tóm tắt: có gì / chưa có gì

- **Có trong config và API:**  
  Giới hạn số instance (`max_running_instances`), số thread server (`thread_num`, `min_threads`, `max_threads`), cấu hình web server (port, body size, keepalive), logging (log level, **retention_days**, **max_disk_usage_percent**, **cleanup_interval_hours**), monitoring (watchdog/health interval), device report.

- **Giới hạn CPU/RAM (tùy chỉnh qua config):**  
  `system.monitoring.max_cpu_percent` và `system.monitoring.max_ram_percent` (0 = tắt). Khi vượt ngưỡng, **tạo instance mới** bị trả 503. Giám sát qua `GET /v1/core/system/resource-status` (limits + current + over_limits).

- **File cấu hình:**  
  Mặc định `config.json` (đường dẫn theo env `CONFIG_FILE` hoặc auto-detect). File `system_config.json` dùng cho API entity `GET/PUT /v1/core/system/config` (UI).

Tài liệu này bám theo code trong `include/config/system_config.h`, `src/config/system_config.cpp`, `src/api/system_info_handler.cpp`, `src/api/watchdog_handler.cpp`, `src/core/performance_monitor.cpp` và OpenAPI trong `api-specs/openapi/`.
