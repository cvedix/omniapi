# Manual Tests - OmniAPI

Thư mục này chứa các tài liệu hướng dẫn test thủ công cho từng tính năng lớn của OmniAPI.

## Cấu Trúc

```
manual/
├── ONVIF/              # Manual tests cho tính năng ONVIF
│   ├── ONVIF_MANUAL_TEST_GUIDE.md
│   ├── ONVIF_QUICK_TEST.md
│   └── ONVIF_TESTING_GUIDE.md
│
├── Recognition/        # Manual tests cho tính năng Recognition
│   └── RECOGNITION_API_GUIDE.md
│
├── Instance_Management/ # Manual tests cho Instance Management
│
├── Core_API/           # Manual tests cho Core API
│   ├── SERVER_SETTINGS_MONITORING_MANUAL_TEST.md
│   ├── HEALTH_VERSION_LICENSE_MANUAL_TEST.md
│   ├── FONT_MANUAL_TEST.md
│   ├── INSTANCE_API_MANUAL_TEST.md
│   ├── INSTANCE_FPS_MANUAL_TEST.md
│   ├── CORE_AI_MANUAL_TEST.md
│   ├── MODEL_MANUAL_TEST.md
│   ├── VIDEO_MANUAL_TEST.md
│   ├── LOG_CONFIG_MANUAL_TEST.md
│   ├── LOG_CONFIG_API_GUIDE.md
│   ├── LOG_LIST_MANUAL_TEST.md
│   ├── SYSTEM_CONFIG_MANUAL_TEST.md
│   ├── SYSTEM_CONFIG_QUICK_TEST.md
│   ├── EVENTS_OUTPUT_MANUAL_TEST.md
│   ├── WATCHDOG_DEVICE_REPORT_MANUAL_TEST.md
│   ├── WATCHDOG_DEVICE_REPORT_CONFIG_API.md
│   ├── CONFIG_BIND_AND_RESTART_MANUAL_TEST.md
│   └── INSTANCE_UPDATE_HOT_RELOAD_MANUAL_TEST.md
│
├── Solutions/          # Manual tests cho Solutions
│   └── SOLUTION_MANUAL_TEST.md
│
├── Groups/             # Manual tests cho Groups
│   └── GROUPS_MANUAL_TEST.md
│
├── Nodes/              # Manual tests cho Nodes
│   └── NODE_MANUAL_TEST.md
│
├── Analytics/          # Manual tests cho Analytics (Lines, Jams, Stops, Area, SecuRT, Loitering)
│   ├── SECURT_INSTANCE_WORKFLOW_TEST.md
│   ├── SECURT_INSTANCE_API_MANUAL_TEST.md
│   ├── SECURT_AREA_MANUAL_TEST.md
│   ├── SECURT_LINES_MANUAL_TEST.md
│   ├── LOITERING_CORE_API_TEST.md
│   ├── LOITERING_SECURT_API_TEST.md
│   └── BA_AREA_ENTER_EXIT_API_TEST.md
│
└── Config/             # Manual tests cho Config
```

## Mục Đích

Manual tests được thiết kế để:
- Hướng dẫn tester thực hiện test thủ công các tính năng
- Cung cấp các test cases chi tiết với expected results
- Hướng dẫn troubleshooting khi gặp vấn đề
- Document các edge cases và scenarios phức tạp

## Cách Sử Dụng

1. Chọn tính năng cần test từ danh sách trên
2. Đọc các file markdown trong thư mục tính năng đó
3. Làm theo hướng dẫn từng bước
4. Ghi lại kết quả và báo cáo nếu có vấn đề

## Tính Năng Hiện Có

### ONVIF
- **ONVIF_MANUAL_TEST_GUIDE.md**: Hướng dẫn test chi tiết với camera Tapo
- **ONVIF_QUICK_TEST.md**: Các lệnh nhanh để test ONVIF
- **ONVIF_TESTING_GUIDE.md**: Hướng dẫn test với camera thật

### Recognition
- **RECOGNITION_API_GUIDE.md**: Hướng dẫn đầy đủ về Recognition API, bao gồm:
  - Đăng ký khuôn mặt
  - Nhận diện khuôn mặt
  - Tìm kiếm khuôn mặt
  - Quản lý subjects
  - Cấu hình database

### Core API - Server settings & Monitoring (toàn bộ tính năng)
- **SERVER_SETTINGS_MONITORING_MANUAL_TEST.md**: Test thủ công **toàn bộ** API cấu hình server và giám sát:
  - **Config API**: GET/POST/PATCH /v1/core/config (max_running_instances, performance threads, web_server, logging, monitoring).
  - **System info/status**: GET /v1/core/system/info (CPU cores, RAM, GPU, disk), GET /v1/core/system/status (CPU %, RAM %, load, uptime).
  - **Watchdog**: GET /v1/core/watchdog (health_monitor: cpu_usage_percent, memory_usage_mb), GET/PUT /v1/core/watchdog/config.
  - **Endpoints & Metrics**: GET /v1/core/endpoints, GET /v1/core/metrics (Prometheus + JSON).
  - **Health, Version**: GET /v1/core/health, GET /v1/core/version.
  - **System config entity**: GET/PUT /v1/core/system/config.
  - Bảng tóm tắt API, troubleshooting, link tới docs và các manual test chi tiết (log, watchdog, bind, system config).

### Core API - System Config
- **SYSTEM_CONFIG_MANUAL_TEST.md**: Hướng dẫn test chi tiết System Config & Preferences API, bao gồm:
  - Get/Update System Configuration
  - Get System Preferences từ rtconfig.json
  - Get System Decoders (NVIDIA/Intel)
  - Get Registry Key Value
  - System Shutdown
- **SYSTEM_CONFIG_QUICK_TEST.md**: Các lệnh nhanh để test System Config API

### Core API - Events & Output
- **EVENTS_OUTPUT_MANUAL_TEST.md**: Hướng dẫn test chi tiết Events & Output Configuration API, bao gồm:
  - Consume Events từ instance event queue
  - Configure HLS (HTTP Live Streaming) output
  - Configure RTSP (Real-Time Streaming Protocol) output
  - Integration tests với multiple output formats

### Core API - Watchdog & Device Report
- **WATCHDOG_DEVICE_REPORT_MANUAL_TEST.md**: Hướng dẫn test thủ công Watchdog và Device Report (OsmAnd/Traccar), bao gồm:
  - GET /v1/core/watchdog — trạng thái watchdog, health monitor, device_report
  - GET /v1/core/watchdog/report-now — gửi report thủ công
  - Cấu hình qua config.json và environment
  - Kịch bản full: bật/tắt device report, server reachable/unreachable
  - Expected results và troubleshooting
- **WATCHDOG_DEVICE_REPORT_CONFIG_API.md**: Hướng dẫn cho người vận hành (non-dev) — cấu hình Device Report qua API:
  - GET/PUT /v1/core/watchdog/config — xem và sửa toàn bộ cấu hình (bật/tắt, server, chu kỳ, device_id, tọa độ, timeout)
  - Giải thích từng trường, ví dụ curl, lỗi thường gặp và cách xử lý
- **CONFIG_BIND_AND_RESTART_MANUAL_TEST.md**: Hướng dẫn cho người vận hành (non-dev) — cấu hình bind (chỉ local hay public) và restart server:
  - GET /v1/core/config?path=system/web_server — xem cấu hình
  - POST /v1/core/config với auto_restart=true — đổi bind_mode/ip_address/port và tự động restart server
  - Các kịch bản: chỉ local, public, đổi port, lưu không restart; dùng curl hoặc Swagger/Scalar

### Core API - Log Config
- **LOG_CONFIG_MANUAL_TEST.md**: Hướng dẫn test thủ công cấu hình ghi log (cho người mới / non-dev):
  - Phần A: Log hệ thống — GET/PUT /v1/core/log/config (bật/tắt API, instance, SDK; đổi log_level; kiểm tra file log)
  - Phần B: Log theo instance — GET/PUT /v1/core/instance/{instanceId}/log/config; kiểm tra thư mục logs/instance/
  - Bảng tóm tắt, lỗi thường gặp, dùng Swagger/Postman
- **LOG_CONFIG_API_GUIDE.md**: Tài liệu API chi tiết (reference) — từng endpoint: Method, URL, body, ý nghĩa từng trường, ví dụ curl; bảng tóm tắt nhanh; dành cho người mới / non-dev

### Analytics - SecuRT Instance
- **SECURT_INSTANCE_WORKFLOW_TEST.md**: Hướng dẫn test chi tiết SecuRT Instance Workflow, bao gồm:
  - Tạo SecuRT instance
  - Cấu hình input (File, RTSP, RTMP, HLS)
  - Cấu hình output (MQTT, RTMP, RTSP, HLS)
  - Thêm bài toán analytics (Lines, Areas)
  - Start instance và xử lý video
  - End-to-end workflow test

### Analytics - Loitering Detection
- **LOITERING_CORE_API_TEST.md**: Hướng dẫn test chi tiết Loitering Detection qua Core API, bao gồm:
  - Tạo instance với loitering areas qua POST /v1/core/instance
  - Định nghĩa loitering areas trong request body
  - Multiple loitering areas với các threshold khác nhau
  - Workflow hoàn chỉnh từ tạo instance đến xử lý video
  - Troubleshooting và best practices

- **LOITERING_SECURT_API_TEST.md**: Hướng dẫn test chi tiết Loitering Detection qua SecuRT Area API, bao gồm:
  - Tạo SecuRT instance
  - Thêm loitering areas qua POST /v1/securt/instance/{instanceId}/area/loitering
  - Quản lý loitering areas (thêm, xóa, lấy danh sách)
  - Auto-restart khi thêm areas
  - Workflow hoàn chỉnh với SecuRT API
  - Troubleshooting và best practices

### Core API - Health, Version, License
- **HEALTH_VERSION_LICENSE_MANUAL_TEST.md**: GET /v1/core/health, /v1/core/version, /v1/core/license/check, /v1/core/license/info.

### Core API - Font
- **FONT_MANUAL_TEST.md**: GET/POST/PUT/DELETE /v1/core/font/list, upload, {fontName}.

### Core API - Instance (toàn bộ endpoint)
- **INSTANCE_API_MANUAL_TEST.md**: Toàn bộ Core Instance API (list, create, load/start/stop/restart/unload, batch, config, input, output, lines, jams, stops, frame, preview, consume_events, statistics, quick, status/summary).
- **INSTANCE_FPS_MANUAL_TEST.md**: GET/POST/DELETE /api/v1/instances/{instance_id}/fps.

### Core API - AI
- **CORE_AI_MANUAL_TEST.md**: POST /v1/core/ai/process, /v1/core/ai/batch; GET /v1/core/ai/metrics, /v1/core/ai/status.

### Core API - Model, Video
- **MODEL_MANUAL_TEST.md**: GET/POST/PUT/DELETE /v1/core/model/list, upload, {modelName}.
- **VIDEO_MANUAL_TEST.md**: GET/POST/PUT/DELETE /v1/core/video/list, upload, {videoName}.

### Core API - Log list
- **LOG_LIST_MANUAL_TEST.md**: GET /v1/core/log, /v1/core/log/{category}, /v1/core/log/{category}/{date}.

### Groups
- **GROUPS_MANUAL_TEST.md**: GET/POST/PUT/DELETE /v1/core/groups, /v1/core/groups/{groupId}, /v1/core/groups/{groupId}/instances.

### Nodes
- **NODE_MANUAL_TEST.md**: GET /v1/core/node, /v1/core/node/{nodeId}, preconfigured, template, build-solution, stats.

### Solutions
- **SOLUTION_MANUAL_TEST.md**: GET/POST/PUT/DELETE /v1/core/solution, defaults, {solutionId}, parameters, instance-body.

### Analytics - SecuRT Area & Lines
- **SECURT_INSTANCE_API_MANUAL_TEST.md**: SecuRT instance CRUD, input, output, stats, performance_profile, pip, analytics_entities, face_detection, lpr, exclusion_areas, masking_areas, motion_area, surrender_detection.
- **SECURT_AREA_MANUAL_TEST.md**: Tất cả loại area (armedPerson, crossing, crowdEstimation, crowding, dwelling, faceCovered, fallenPerson, intrusion, loitering, objectEnterExit, objectLeft, objectRemoved, occupancy, vehicleGuard, areas).
- **SECURT_LINES_MANUAL_TEST.md**: Lines, line/counting, line/crossing, line/tailgating và CRUD theo lineId.

## Bảng ánh xạ API → Manual Test

| Nhóm API | Path (prefix hoặc tag) | Manual Test |
|----------|-------------------------|-------------|
| Config | /v1/core/config, /v1/core/config/{path}, reset | SERVER_SETTINGS_MONITORING, CONFIG_BIND_AND_RESTART |
| System | /v1/core/system/*, /v1/core/endpoints, /v1/core/metrics | SERVER_SETTINGS_MONITORING, SYSTEM_CONFIG_* |
| Health, Version, License | /v1/core/health, version, license/* | HEALTH_VERSION_LICENSE_MANUAL_TEST |
| Watchdog | /v1/core/watchdog, watchdog/config, report-now | SERVER_SETTINGS_MONITORING, WATCHDOG_* |
| Font | /v1/core/font/* | FONT_MANUAL_TEST |
| Groups | /v1/core/groups, groups/{groupId}, .../instances | GROUPS_MANUAL_TEST |
| Instance (core) | /v1/core/instance, instance/{id}/* | INSTANCE_API_MANUAL_TEST, EVENTS_OUTPUT, INSTANCE_UPDATE_HOT_RELOAD |
| Instance FPS | /api/v1/instances/{id}/fps | INSTANCE_FPS_MANUAL_TEST |
| AI | /v1/core/ai/process, batch, metrics, status | CORE_AI_MANUAL_TEST |
| Log | /v1/core/log, log/{category}, log/config | LOG_LIST_MANUAL_TEST, LOG_CONFIG_* |
| Model | /v1/core/model/* | MODEL_MANUAL_TEST |
| Node | /v1/core/node/* | NODE_MANUAL_TEST |
| Solution | /v1/core/solution/* | SOLUTION_MANUAL_TEST |
| Video | /v1/core/video/* | VIDEO_MANUAL_TEST |
| ONVIF | /v1/onvif/* | ONVIF_* |
| Recognition | /v1/recognition/* | RECOGNITION_API_GUIDE |
| SecuRT instance | /v1/securt/instance, instance/{id}/* (không area/lines) | SECURT_INSTANCE_API_MANUAL_TEST, SECURT_INSTANCE_WORKFLOW |
| SecuRT area | /v1/securt/instance/{id}/area/*, areas | SECURT_AREA_MANUAL_TEST, LOITERING_*, BA_AREA_ENTER_EXIT |
| SecuRT lines | /v1/securt/instance/{id}/line/*, lines | SECURT_LINES_MANUAL_TEST |

## Thêm Manual Test Mới

Khi thêm manual test mới:

1. Xác định tính năng lớn mà test thuộc về
2. Tạo file markdown trong thư mục tính năng tương ứng
3. Bao gồm:
   - Mục đích test
   - Prerequisites
   - Các bước test chi tiết
   - Expected results
   - Troubleshooting guide
   - Test cases examples

## Best Practices

1. **Rõ ràng và chi tiết**: Mỗi bước test nên có hướng dẫn rõ ràng
2. **Có ví dụ**: Cung cấp ví dụ cụ thể với curl commands hoặc code
3. **Expected results**: Luôn mô tả kết quả mong đợi
4. **Troubleshooting**: Bao gồm phần troubleshooting cho các vấn đề thường gặp
5. **Cập nhật**: Giữ tài liệu cập nhật khi API thay đổi

