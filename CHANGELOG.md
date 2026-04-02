# Changelog - OmniAPI

Tất cả thay đổi đáng chú ý của dự án OmniAPI sẽ được ghi lại trong file này.

Format này tuân theo [Keep a Changelog](https://keepachangelog.com/en/1.1.0/)
và sử dụng [Semantic Versioning](https://semver.org/lang/vi/).

## [Unreleased]

### Added
- (Chưa có thay đổi trong phiên bản chưa phát hành)

### Changed
- (Chưa có thay đổi trong phiên bản chưa phát hành)

### Deprecated
- (Chưa có thay đổi trong phiên bản chưa phát hành)

### Removed
- (Chưa có thay đổi trong phiên bản chưa phát hành)

### Fixed
- (Chưa có thay đổi trong phiên bản chưa phát hành)

### Security
- (Chưa có thay đổi trong phiên bản chưa phát hành)

---

## [2026.0.1] - 2026-01-01

### Added

#### API & Core
- **REST API Server** — Drogon Framework HTTP server trên port 8080
- **Instance Management** — CRUD operations cho AI processing instances
  - `POST /v1/core/instance` — Tạo instance mới
  - `GET /v1/core/instance` — List tất cả instances
  - `GET /v1/core/instance/{id}` — Chi tiết instance
  - `PUT /v1/core/instance/{id}` — Update instance (hot swap / restart)
  - `DELETE /v1/core/instance/{id}` — Xóa instance
  - `POST /v1/core/instance/{id}/start` — Khởi động instance
  - `POST /v1/core/instance/{id}/stop` — Dừng instance
  - `POST /v1/core/instance/{id}/restart` — Khởi động lại instance
  - `GET /v1/core/instance/{id}/frame` — Lấy frame mới nhất
  - `GET /v1/core/instance/{id}/statistics` — Thống kê instance
  - `GET /v1/core/instance/{id}/config` — Lấy cấu hình instance
  - `POST /v1/core/instance/{id}/batch/*` — Batch operations

- **Solution Management** — Solution templates
  - `GET /v1/core/solution` — List solutions
  - `GET /v1/core/solution/{id}` — Chi tiết solution
  - `POST /v1/core/solution` — Tạo solution
  - `PUT /v1/core/solution/{id}` — Update solution
  - `DELETE /v1/core/solution/{id}` — Xóa solution

- **Group Management** — Camera groups
  - `GET /v1/core/groups` — List groups
  - `GET /v1/core/groups/{id}` — Chi tiết group
  - `POST /v1/core/groups` — Tạo group
  - `PUT /v1/core/groups/{id}` — Update group
  - `DELETE /v1/core/groups/{id}` — Xóa group

- **Core APIs** — System endpoints
  - `GET /v1/core/health` — Health check
  - `GET /v1/core/version` — Version information
  - `GET /v1/core/watchdog` — Watchdog status
  - `GET /v1/core/endpoints` — List all endpoints với statistics
  - `GET /v1/core/metrics` — Prometheus metrics
  - `GET /v1/core/log` — View logs
  - `GET /v1/core/config` — Get configuration
  - `POST /v1/core/config` — Update configuration
  - `GET /v1/core/system/info` — System hardware info
  - `GET /v1/core/system/status` — System status (CPU, RAM)

#### Face Recognition
- `POST /v1/recognition/face/database` — Tạo face database
- `GET /v1/recognition/face/database` — List face databases
- `POST /v1/recognition/face/database/{id}/person` — Thêm person
- `GET /v1/recognition/face/database/{id}/person` — List persons
- `POST /v1/recognition/face/database/{id}/person/{personId}/image` — Thêm ảnh
- **Face Detection** — YuNet, YOLOv11, RKNN
- **Face Recognition** — InsightFace, TensorRT
- **Face Tracking** — SORT, ByteTrack, OCSort
- **Face Swap** — Hoán đổi khuôn mặt
- **Face Database** — MySQL/PostgreSQL

#### Behavior Analysis (BA)
- **Crossline Detection** — Phát hiện vượt đường line (đếm đối tượng)
- **Multi-line Crossline** — Nhiều đường crossline
- **Traffic Jam Detection** — Phát hiện kẹt xe
- **Stop Detection** — Phát hiện dừng tại stop-line
- **Wrong Way Detection** — Phát hiện đi ngược chiều
- **Obstacle Detection** — Phát hiện chướng ngại vật

#### Security Rules (SecuRT)
- SecuRT line-based rules
- SecuRT area-based rules (loitering, intrusion)
- `POST /v1/core/instance/{id}/securt/*` — SecuRT management APIs

#### Analytics Rules
- `POST /v1/core/instance/{id}/lines/*` — Crossing lines management
- `POST /v1/core/instance/{id}/jams/*` — Jam line management
- `POST /v1/core/instance/{id}/stops/*` — Stop line management
- `POST /v1/core/instance/{id}/area/*` — Area management

#### ONVIF Camera
- ONVIF camera discovery
- Generic ONVIF handler
- Tapo-specific handler
- Camera configuration APIs

#### Pipeline & Model
- Model upload management
- Video file upload
- Font upload cho OSD overlay
- Node pool management
- HLS streaming endpoint

#### Documentation & Tools
- **Swagger UI** — `GET /swagger`, `GET /v1/swagger`
- **OpenAPI Spec** — `GET /openapi.yaml`, `GET /v1/openapi.yaml`
- **Scalar API Reference** — `GET /v1/document`
- **Postman Collection** — `api-specs/postman/`
- Manual test guides trong `tests/manual/`

#### Architecture
- **Execution Modes** — subprocess (production) và in-process (legacy/dev)
- **Subprocess Architecture** — Worker process isolation qua Unix Socket IPC
- **Hot Swap (Zero-Downtime)** — Cập nhật cấu hình không ngắt RTMP stream
- **Pipeline Snapshot** — Immutable pipeline snapshots
- **Frame Router** — Frame routing cho hot swap
- **Watchdog & Health Monitor** — Background threads giám sát 24/7
- **Circuit Breaker** — Fault tolerance
- **Backpressure Controller** — Queue management
- **Metrics Interceptor** — Request metrics tracking
- **Instance Persistence** — Tự động lưu và khôi phục instances

#### Multi-Platform Support
- NVIDIA Jetson AGX Orin (Jetson 030)
- NVIDIA Jetson Orin Nano (R7300)
- Intel Core Ultra (R360)
- Qualcomm QCS6490 (DK2721)
- AMD Ryzen 8000 (2210)
- Hailo-8 (1200/3300)
- Rockchip RK3588 (OPI5-Plus)

#### AI Models & Inference
- **TensorRT** — NVIDIA GPU acceleration
- **RKNN** — Rockchip NPU acceleration
- **ONNX Runtime** — Cross-platform inference
- **OpenCV DNN** — YOLO, Caffe, TensorFlow
- **PaddlePaddle** — OCR text detection
- **YOLO** — YOLOv8, YOLOv11
- **YuNet** — Face detection
- **InsightFace** — Face recognition
- **Mask R-CNN** — Instance segmentation
- **OpenPose** — Pose estimation

#### Source & Destination Nodes
- **Sources**: RTSP, RTMP, File, Image, App, UDP, FFmpeg
- **Destinations**: RTSP, RTMP, File, Image, Screen, App, FFmpeg
- **Brokers**: MQTT, Kafka, Socket, Console, File, SSE

#### System Integration
- **Systemd Service** — `sudo systemctl start omniapi`
- **Environment Variables** — `.env` configuration
- **Logging** — Daily rotating logs
- **Per-Instance Logging** — Instance file logger
- **Debug Logging** — `--log-api --log-instance --log-sdk-output`

### Changed
- Subprocess mode là **default** cho production (thay vì in-process)
- Hot swap ưu tiên cho mọi PATCH/PUT instance thay vì restart
- Line-only update dùng hot swap khi có RTMP output
- Worker logs được redirect vào `logs/worker_<instance_id>.log`

### Fixed
- (Xem chi tiết trong [ReleaseNotes.md](ReleaseNotes.md))

### Security
- Proprietary license — CVEDIX
- Xem [SECURITY.md](SECURITY.md) cho chính sách bảo mật

---

## Hướng Dẫn Thêm Thay Đổi

Khi thực hiện thay đổi, hãy thêm entry vào section `[Unreleased]` với format sau:

```markdown
### Added
- Mô tả feature mới thêm

### Changed
- Mô tả thay đổi của feature hiện có

### Deprecated
- Mô tả feature sẽ bị loại bỏ trong tương lai

### Removed
- Mô tả feature đã bị loại bỏ

### Fixed
- Mô tả bug đã được sửa

### Security
- Mô tả thay đổi bảo mật
```

Khi phát hành phiên bản mới, di chuyển toàn bộ nội dung section `[Unreleased]`
sang section mới `[YYYY.MM.N]` với ngày phát hành.

---

## Các Phiên Bản Cũ

- [ReleaseNotes.md](ReleaseNotes.md) — Chi tiết đầy đủ các phiên bản trước
