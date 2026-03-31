# omniapi

**Nền tảng Edge AI**: REST API + xử lý AI trực tiếp trên thiết bị biên. CVEDIX SDK (EdgeOS SDK) là tầng hỗ trợ — API điều khiển instances, nhận diện khuôn mặt, push frame và metrics qua một lớp thống nhất (AI Runtime).

![Edge AI Workflow](docs/image.png)

---

## 📌 Định vị sản phẩm

| Thành phần | Vai trò |
|------------|--------|
| **Edge AI API** | Nền tảng: REST API + xử lý AI (decode, inference, cache) |
| **AI Runtime / SDK Helper** | Lớp thống nhất: InferenceSession, AIRuntimeFacade, PipelineHelper |
| **CVEDIX SDK** | Tay hỗ trợ: pipeline (source → detector → broker → destination) |

Luồng: **API → AI Runtime → CVEDIX SDK**. Chi tiết: [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md#api--ai-runtime--sdk).

---

## 🎯 Tính năng chính & Execution mode

- **Instance** — Tạo, start/stop, cấu hình AI instances (pipeline).
- **Solution** — Quản lý solution templates.
- **Recognition** — Nhận diện khuôn mặt (REST), face database, register/list/delete.
- **Lines / Jams / Stops** — Crossline, traffic jam, stop-line (SecuRT, BA).
- **Push frame** — Đẩy frame (compressed/encoded) vào instance.
- **Metrics** — Health, version, watchdog, system info, logs.

**Execution mode:** Mặc định **subprocess** (cách ly process). In-process (legacy): `EDGE_AI_EXECUTION_MODE=in-process` (hoặc `inprocess`, `legacy`, `main`). Xem [docs/ENVIRONMENT_VARIABLES.md](docs/ENVIRONMENT_VARIABLES.md) và [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md#khi-nào-dùng-mode-nào).

---

## 🏗️ Kiến Trúc

![Architecture](asset/architecture.png)
```
[Client] → [REST API Server] → [Instance Manager] → [CVEDIX SDK]
                    ↓
              [AI Runtime] → decode / infer / cache
                              
```

## 🎯 Các Bài Toán & Tính Năng Được Hỗ Trợ

API hỗ trợ các tính năng từ CVEDIX SDK với **43+ processing nodes**, bao gồm:

### 👤 Nhận Diện & Phân Tích Khuôn Mặt
- ✅ **Face Detection** - Phát hiện khuôn mặt (YuNet, YOLOv11, RKNN)
- ✅ **Face Recognition** - Nhận diện khuôn mặt (InsightFace, TensorRT)
- ✅ **Face Tracking** - Theo dõi khuôn mặt (SORT, ByteTrack, OCSort)
- ✅ **Face Feature Encoding** - Trích xuất đặc trưng khuôn mặt
- ✅ **Face Swap** - Hoán đổi khuôn mặt
- ✅ **Face Database Management** - Quản lý database khuôn mặt

### 🚗 Phát Hiện & Phân Tích Phương Tiện
- ✅ **Vehicle Detection** - Phát hiện phương tiện (TensorRT, YOLO)
- ✅ **Vehicle Plate Detection & Recognition** - Phát hiện và nhận diện biển số xe
- ✅ **Vehicle Tracking** - Theo dõi phương tiện
- ✅ **Vehicle Classification** - Phân loại màu, loại xe (TensorRT)
- ✅ **Vehicle Feature Encoding** - Trích xuất đặc trưng xe
- ✅ **Vehicle Body Scan** - Quét thân xe
- ✅ **Vehicle Clustering** - Phân nhóm xe

### 🎯 Phát Hiện Vật Thể & Phân Tích
- ✅ **Object Detection** - Phát hiện vật thể (YOLO, YOLOv8, YOLOv11)
- ✅ **Instance Segmentation** - Phân đoạn instance (Mask R-CNN, YOLOv8)
- ✅ **Semantic Segmentation** - Phân đoạn ngữ nghĩa (ENet)
- ✅ **Pose Estimation** - Ước lượng tư thế (OpenPose, YOLOv8)
- ✅ **Image Classification** - Phân loại ảnh
- ✅ **Text Detection** - Phát hiện văn bản (PaddleOCR)

### 🚦 Phân Tích Hành Vi (Behavior Analysis)
- ✅ **Crossline Detection** - Phát hiện vượt đường line (đếm đối tượng)
- ✅ **Multi-line Crossline** - Nhiều đường crossline
- ✅ **Traffic Jam Detection** - Phát hiện kẹt xe
- ✅ **Stop Detection** - Phát hiện dừng tại stop-line
- ✅ **Wrong Way Detection** - Phát hiện đi ngược chiều
- ✅ **Obstacle Detection** - Phát hiện chướng ngại vật

### 🔥 Phát Hiện An Toàn & Bất Thường
- ✅ **Fire/Smoke Detection** - Phát hiện lửa/khói
- ✅ **Video Restoration** - Khôi phục video chất lượng cao
- ✅ **Lane Detection** - Phát hiện làn đường

### 📹 Nguồn Video & Đầu Ra
- ✅ **Source**: RTSP, RTMP, File, Image, App, UDP, FFmpeg
- ✅ **Destination**: RTSP, RTMP, File, Image, Screen, App, FFmpeg
- ✅ **Broker**: MQTT, Kafka, Socket, Console, File, SSE

### 🔄 Xử Lý & Tối Ưu
- ✅ **Object Tracking** - SORT, ByteTrack, OCSort
- ✅ **Frame Processing** - Fusion, Sync, Split, Skip
- ✅ **Recording** - Ghi lại video/ảnh
- ✅ **Clustering** - Phân nhóm đối tượng
- ✅ **OSD** - Vẽ overlay kết quả

### 🤖 AI Models & Hardware
- ✅ **TensorRT** - NVIDIA GPU (YOLOv8, Vehicle, InsightFace)
- ✅ **RKNN** - Rockchip NPU (YOLOv8, YOLOv11, Face)
- ✅ **ONNX Runtime** - Cross-platform
- ✅ **OpenCV DNN** - YOLO, Caffe, TensorFlow
- ✅ **PaddlePaddle** - OCR

### 🔧 Tính Năng Nâng Cao
- ✅ **Multi-Channel Pipelines** - Xử lý nhiều nguồn đồng thời
- ✅ **Dynamic Pipeline** - Thay đổi pipeline trong runtime
- ✅ **Multi-Detector** - Nhiều detector trong một pipeline
- ✅ **MLLM Analysis** - Phân tích đa phương thức

Xem chi tiết: [ReleaseNotes.md](ReleaseNotes.md#-các-bài-toán--tính-năng-được-hỗ-trợ)

---

## 🚀 Quick Start

### Development Setup

```bash
# Full setup (dependencies + build)
./scripts/dev_setup.sh

# Chạy server
./scripts/load_env.sh
```

### Production Setup

```bash
# Full deployment (cần sudo)
sudo ./scripts/prod_setup.sh

# Hoặc sử dụng deploy script trực tiếp
sudo ./deploy/deploy.sh
```

### Build Thủ Công

```bash
# 1. Cài dependencies
./scripts/install_dependencies.sh

# 2. Build
mkdir build && cd build
cmake ..
make -j$(nproc)

# 3. Chạy server
./bin/omniapi
```

### Build và Cài Đặt Debian Package

**⚠️ Khuyến nghị: Sử dụng ALL-IN-ONE package** - Tự chứa tất cả dependencies, không cần cài thêm packages.

**📥 Tải file .deb ALL-IN-ONE:** [Download từ Google Drive](https://drive.google.com/file/d/1KaGvhSVFqFOc8_XIU6gd7xgWTT52fVub/view?usp=sharing)

**⚠️ QUAN TRỌNG:** Trước khi cài đặt package `.deb`, bạn **BẮT BUỘC** phải chuẩn bị và cài đặt các dependencies trước. Xem chi tiết: [docs/INSTALLATION.md](docs/INSTALLATION.md)

**Quick Start:**

```bash
# Build ALL-IN-ONE package (khuyến nghị)
./packaging/scripts/build_deb_all_in_one.sh --sdk-deb <path-to-sdk.deb>

# Hoặc build package thông thường
./packaging/scripts/build_deb.sh

# Cài đặt package
sudo dpkg -i omniapi-all-in-one-*.deb
sudo apt-get install -f  # Nếu có lỗi dependencies
sudo systemctl start omniapi
```

**Lưu ý:** Không cần `sudo` để build! Chỉ cần sudo khi **cài đặt** package.

Xem hướng dẫn chi tiết đầy đủ: **[docs/INSTALLATION.md](docs/INSTALLATION.md)**

### Test

```bash
curl http://localhost:8080/v1/core/health
curl http://localhost:8080/v1/core/version
```

---

## 🌐 Khởi Động Server

### Với File .env (Khuyến nghị)

```bash
# Tạo .env từ template
cp .env.example .env
nano .env  # Chỉnh sửa nếu cần

# Load và chạy server
./scripts/load_env.sh
```

### Với Logging

```bash
./build/bin/omniapi --log-api --log-instance --log-sdk-output
```

### Environment Variables

| Variable | Default | Description |
|----------|---------|-------------|
| `API_HOST` | 0.0.0.0 | Server host |
| `API_PORT` | 8080 | Server port |
| `THREAD_NUM` | 0 (auto) | Worker threads |
| `LOG_LEVEL` | INFO | Log level |

Xem đầy đủ: [docs/ENVIRONMENT_VARIABLES.md](docs/ENVIRONMENT_VARIABLES.md)

---

## 📡 API Endpoints

### Core APIs

```bash
curl http://localhost:8080/v1/core/health      # Health check
curl http://localhost:8080/v1/core/version     # Version info
curl http://localhost:8080/v1/core/watchdog    # Watchdog status
curl http://localhost:8080/v1/core/endpoints   # List endpoints
```

### Instance APIs

```bash
# Create instance
curl -X POST http://localhost:8080/v1/core/instance \
  -H "Content-Type: application/json" \
  -d '{"name": "camera_1", "solution": "face_detection", "autoStart": true}'

# List instances
curl http://localhost:8080/v1/core/instance

# Start/Stop
curl -X POST http://localhost:8080/v1/core/instance/{id}/start
curl -X POST http://localhost:8080/v1/core/instance/{id}/stop
```

### Swagger UI

- **Swagger UI**: http://localhost:8080/swagger
- **OpenAPI Spec**: http://localhost:8080/openapi.yaml

Xem đầy đủ: [docs/API.md](docs/API.md)

---

## 🏗️ Kiến Trúc (chi tiết)

```
[Client] → [REST API Server] → [Instance Manager] → [CVEDIX SDK]
                    ↓
              [AI Runtime] → InferenceSession, AIRuntimeFacade, PipelineHelper
```

**Thành phần:**
- **REST API Server**: Drogon Framework HTTP server
- **Instance Manager**: Quản lý vòng đời instances (in-process hoặc subprocess)
- **AI Runtime**: Decode, inference, cache (Recognition, Push frame); SDK là tầng hỗ trợ
- **CVEDIX SDK**: 43+ processing nodes (source, inference, tracker, broker, destination)
- **Data Broker**: Message routing và output publishing

---

## 📊 Logging & Monitoring

```bash
# Development - full logging
./build/bin/omniapi --log-api --log-instance --log-sdk-output

# Production - minimal logging
./build/bin/omniapi --log-api
```

**Logs API:**
```bash
curl http://localhost:8080/v1/core/log
curl "http://localhost:8080/v1/core/log/api?level=ERROR&tail=100"
```

---

## 🚀 Production Deployment

```bash
# Setup với systemd service
sudo ./scripts/prod_setup.sh

# Hoặc sử dụng deploy script
sudo ./deploy/deploy.sh

# Kiểm tra service
sudo systemctl status omniapi
sudo journalctl -u omniapi -f

# Quản lý
sudo systemctl restart omniapi
sudo systemctl stop omniapi
```

Xem chi tiết: [deploy/README.md](deploy/README.md)

---

## ⚠️ Troubleshooting

### Lỗi "Could NOT find Jsoncpp"

```bash
sudo apt-get install libjsoncpp-dev
```

### Lỗi CVEDIX SDK symlinks

```bash
# Chạy lại dev setup để fix symlinks
./scripts/dev_setup.sh --skip-deps --skip-build
```

### Build Drogon lâu

Lần đầu build mất ~5-10 phút để download Drogon. Các lần sau nhanh hơn.

---

## 📚 Tài Liệu

| File | Nội dung |
|------|----------|
| [docs/INSTALLATION.md](docs/INSTALLATION.md) | **Hướng dẫn cài đặt Debian package** |
| [docs/API.md](docs/API.md) | Full API reference |
| [docs/DEVELOPMENT.md](docs/DEVELOPMENT.md) | Development guide & Pre-commit |
| [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) | System architecture |
| [docs/SCRIPTS.md](docs/SCRIPTS.md) | Scripts documentation (dev, prod, build) |
| [docs/ENVIRONMENT_VARIABLES.md](docs/ENVIRONMENT_VARIABLES.md) | Env vars |
| [docs/LOGGING.md](docs/LOGGING.md) | Logging guide |
| [docs/DEFAULT_SOLUTIONS_REFERENCE.md](docs/DEFAULT_SOLUTIONS_REFERENCE.md) | Default solutions |
| [docs/VISION_AI_PROCESSING_PLATFORM.md](docs/VISION_AI_PROCESSING_PLATFORM.md) | Vision: nền tảng Edge AI |
| [docs/AI_RUNTIME_DESIGN.md](docs/AI_RUNTIME_DESIGN.md) | Thiết kế AI Runtime (InferenceSession, Facade) |
| [task/omniapi/00_MASTER_PLAN.md](task/omniapi/00_MASTER_PLAN.md) | Master plan & trạng thái phases |
| [deploy/README.md](deploy/README.md) | Production deployment guide |
| [packaging/docs/BUILD_DEB.md](packaging/docs/BUILD_DEB.md) | Build Debian package guide |
| [packaging/docs/BUILD_ALL_IN_ONE.md](packaging/docs/BUILD_ALL_IN_ONE.md) | Build ALL-IN-ONE package guide |

---

## 🔧 AI System Support

| Vendor | Device | SOC |
|--------|--------|-----|
| Qualcomm | DK2721 | QCS6490 |
| Intel | R360 | Core Ultra |
| NVIDIA | 030 | Jetson AGX Orin |
| NVIDIA | R7300 | Jetson Orin Nano |
| AMD | 2210 | Ryzen 8000 |
| Hailo | 1200/3300 | Hailo-8 |
| Rockchip | OPI5-Plus | RK3588 |

---

## 📝 License

Proprietary - CVEDIX
