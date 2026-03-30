 # Release Notes - Edge AI API

## 📦 Version Information

**Version:** 2026.0.1.1
**Release Date:** 2026-01-01  
**Build Type:** Release

---

## 🎯 Tổng Quan

**Edge AI API** là REST API server cho CVEDIX Edge AI SDK, cho phép điều khiển và giám sát các AI processing instances trên thiết bị biên thông qua giao diện RESTful API.

### Tính Năng Chính

- ✅ **RESTful API** - Quản lý instances qua HTTP API
- ✅ **Instance Management** - Tạo, cấu hình, khởi động, dừng AI processing instances
- ✅ **Solution Templates** - Quản lý và sử dụng các solution templates có sẵn
- ✅ **Face Recognition** - Hỗ trợ nhận diện khuôn mặt với database management
- ✅ **Real-time Monitoring** - WebSocket support cho monitoring real-time
- ✅ **Swagger UI** - Giao diện web để khám phá và test API
- ✅ **Systemd Integration** - Chạy như system service
- ✅ **Comprehensive Logging** - Logging và monitoring đầy đủ
- ✅ **Multi-Platform Support** - Hỗ trợ nhiều AI hardware platforms

---

## 🎯 Các Bài Toán & Tính Năng Được Hỗ Trợ

API hỗ trợ các tính năng từ CVEDIX SDK với **43+ processing nodes**, bao gồm:

### 👤 Nhận Diện & Phân Tích Khuôn Mặt

- ✅ **Face Detection** - Phát hiện khuôn mặt (YuNet, YOLOv11, RKNN)
- ✅ **Face Recognition** - Nhận diện khuôn mặt (InsightFace, TensorRT)
- ✅ **Face Tracking** - Theo dõi khuôn mặt (SORT, ByteTrack, OCSort)
- ✅ **Face Feature Encoding** - Trích xuất đặc trưng khuôn mặt (SFace)
- ✅ **Face Swap** - Hoán đổi khuôn mặt
- ✅ **Face Database Management** - Quản lý database khuôn mặt (MySQL/PostgreSQL)

### 🚗 Phát Hiện & Phân Tích Phương Tiện

- ✅ **Vehicle Detection** - Phát hiện phương tiện (TensorRT, YOLO)
- ✅ **Vehicle Plate Detection** - Phát hiện biển số xe (YOLOv11, TensorRT)
- ✅ **Vehicle Plate Recognition** - Nhận diện biển số xe
- ✅ **Vehicle Tracking** - Theo dõi phương tiện
- ✅ **Vehicle Feature Encoding** - Trích xuất đặc trưng xe (TensorRT)
- ✅ **Vehicle Color Classification** - Phân loại màu xe (TensorRT)
- ✅ **Vehicle Type Classification** - Phân loại loại xe (TensorRT)
- ✅ **Vehicle Body Scan** - Quét thân xe
- ✅ **Vehicle Clustering** - Phân nhóm xe dựa trên đặc trưng

### 🎯 Phát Hiện Vật Thể & Phân Tích

- ✅ **Object Detection** - Phát hiện vật thể (YOLO, YOLOv8, YOLOv11)
- ✅ **Instance Segmentation** - Phân đoạn instance (Mask R-CNN, YOLOv8 Seg)
- ✅ **Semantic Segmentation** - Phân đoạn ngữ nghĩa (ENet)
- ✅ **Pose Estimation** - Ước lượng tư thế (OpenPose, YOLOv8 Pose)
- ✅ **Image Classification** - Phân loại ảnh
- ✅ **Text Detection** - Phát hiện văn bản (PaddleOCR)

### 🚦 Phân Tích Hành Vi (Behavior Analysis)

- ✅ **Crossline Detection** - Phát hiện vượt đường line (đếm đối tượng)
- ✅ **Multi-line Crossline** - Nhiều đường crossline
- ✅ **Traffic Jam Detection** - Phát hiện kẹt xe (BA Jam)
- ✅ **Stop Detection** - Phát hiện dừng tại stop-line (BA Stop)
- ✅ **Wrong Way Detection** - Phát hiện đi ngược chiều
- ✅ **Obstacle Detection** - Phát hiện chướng ngại vật

### 🔥 Phát Hiện An Toàn & Bất Thường

- ✅ **Fire/Smoke Detection** - Phát hiện lửa/khói
- ✅ **Video Restoration** - Khôi phục video chất lượng cao
- ✅ **Lane Detection** - Phát hiện làn đường

### 📹 Nguồn Video Đầu Vào (Source Nodes)

- ✅ **RTSP Source** - Stream RTSP từ camera IP
- ✅ **RTMP Source** - Stream RTMP
- ✅ **File Source** - Video file (MP4, AVI, etc.)
- ✅ **Image Source** - Ảnh đơn hoặc thư mục ảnh
- ✅ **App Source** - Input từ ứng dụng
- ✅ **UDP Source** - Stream UDP
- ✅ **FFmpeg Source** - Nguồn đa dạng qua FFmpeg

### 📤 Đầu Ra Video (Destination Nodes)

- ✅ **RTSP Destination** - Stream RTSP output
- ✅ **RTMP Destination** - Stream RTMP output
- ✅ **File Destination** - Lưu video file
- ✅ **Image Destination** - Lưu ảnh snapshot
- ✅ **Screen Destination** - Hiển thị trên màn hình
- ✅ **App Destination** - Output đến ứng dụng
- ✅ **FFmpeg Destination** - Output đa dạng qua FFmpeg

### 📡 Xuất Dữ Liệu (Broker Nodes)

- ✅ **MQTT Broker** - Gửi events qua MQTT (JSON, XML)
- ✅ **Kafka Broker** - Gửi events qua Apache Kafka
- ✅ **Socket Broker** - Gửi qua TCP/UDP Socket (JSON, XML, BA, Plate, Embeddings)
- ✅ **Console Broker** - In ra console (JSON)
- ✅ **File Broker** - Lưu events vào file (XML)
- ✅ **SSE Broker** - Server-Sent Events (real-time streaming)

### 🔄 Xử Lý & Tối Ưu

- ✅ **Object Tracking** - Theo dõi đối tượng (SORT, ByteTrack, OCSort)
- ✅ **Frame Fusion** - Kết hợp frame từ nhiều nguồn
- ✅ **Frame Synchronization** - Đồng bộ frame
- ✅ **Frame Splitting** - Chia tách frame
- ✅ **Frame Skipping** - Bỏ qua frame (tối ưu hiệu năng)
- ✅ **Recording** - Ghi lại video/ảnh
- ✅ **Clustering** - Phân nhóm đối tượng
- ✅ **OSD (On-Screen Display)** - Vẽ overlay kết quả (Face, Plate, Pose, Segmentation, Lane, MLLM)

### 🤖 AI Models & Hardware Support

- ✅ **TensorRT** - NVIDIA GPU acceleration (YOLOv8, Vehicle, InsightFace)
- ✅ **RKNN** - Rockchip NPU acceleration (YOLOv8, YOLOv11, Face)
- ✅ **ONNX Runtime** - Cross-platform inference
- ✅ **OpenCV DNN** - YOLO, Caffe, TensorFlow models
- ✅ **PaddlePaddle** - OCR text detection

### 🔧 Tính Năng Nâng Cao

- ✅ **Multi-Channel Pipelines** - Xử lý nhiều nguồn đồng thời (1-1-1, 1-1-N, 1-N-1, N-N)
- ✅ **Dynamic Pipeline** - Thay đổi pipeline trong runtime
- ✅ **Multi-Detector** - Nhiều detector trong một pipeline
- ✅ **Pipeline Interaction** - Tương tác với pipeline
- ✅ **MLLM Analysis** - Phân tích đa phương thức với Large Language Model

### 📊 Monitoring & Management

- ✅ **Real-time Statistics** - Thống kê FPS, latency, throughput
- ✅ **Health Monitoring** - Giám sát sức khỏe hệ thống
- ✅ **Logging Management** - Quản lý log theo category
- ✅ **Metrics Export** - Prometheus metrics
- ✅ **Instance Persistence** - Tự động lưu và khôi phục instances

---

## 🏗️ Kiến Trúc

![Architecture](asset/architecture.png)
```
[Client] → [REST API Server] → [Instance Manager] → [CVEDIX SDK]
                                      ↓
                              [Data Broker] → [Output]
                              
```

**Thành phần:**
- **REST API Server**: Drogon Framework HTTP server
- **Instance Manager**: Quản lý vòng đời instances (In-Process hoặc Subprocess mode)
- **CVEDIX SDK**: 43+ processing nodes (source, inference, tracker, broker, destination)
- **Data Broker**: Message routing và output publishing

---

## 📡 API Endpoints

### Core APIs

- `GET /v1/core/health` - Health check
- `GET /v1/core/version` - Version information
- `GET /v1/core/watchdog` - Watchdog status
- `GET /v1/core/endpoints` - List all endpoints with statistics

### Instance Management

- `POST /v1/core/instance` - Tạo instance mới
- `GET /v1/core/instance` - List tất cả instances
- `GET /v1/core/instance/{id}` - Chi tiết instance
- `PUT /v1/core/instance/{id}` - Update instance
- `DELETE /v1/core/instance/{id}` - Xóa instance
- `POST /v1/core/instance/{id}/start` - Khởi động instance
- `POST /v1/core/instance/{id}/stop` - Dừng instance
- `POST /v1/core/instance/{id}/restart` - Khởi động lại instance
- `GET /v1/core/instance/{id}/frame` - Lấy frame mới nhất
- `GET /v1/core/instance/{id}/statistics` - Thống kê instance

### Solution Management

- `GET /v1/core/solution` - List tất cả solutions
- `GET /v1/core/solution/{id}` - Chi tiết solution
- `POST /v1/core/solution` - Tạo solution mới
- `PUT /v1/core/solution/{id}` - Update solution
- `DELETE /v1/core/solution/{id}` - Xóa solution

### Face Recognition

- `POST /v1/recognition/face/database` - Tạo face database
- `GET /v1/recognition/face/database` - List face databases
- `POST /v1/recognition/face/database/{id}/person` - Thêm person
- `GET /v1/recognition/face/database/{id}/person` - List persons
- `POST /v1/recognition/face/database/{id}/person/{personId}/image` - Thêm ảnh

### System & Config

- `GET /v1/core/config` - Get configuration
- `POST /v1/core/config` - Update configuration
- `GET /v1/core/system/info` - System hardware information
- `GET /v1/core/system/status` - System status (CPU, RAM, etc.)
- `GET /v1/core/log` - View logs

### Swagger UI

- `GET /swagger` - Swagger UI interface
- `GET /openapi.yaml` - OpenAPI specification

Xem đầy đủ: [docs/API_document](docs/API_document)

---

## 🔧 AI System Support

Hỗ trợ nhiều AI hardware platforms:

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

## 🏗️ Hướng Dẫn Build Từ Source Code

### Yêu Cầu Hệ Thống

- **OS**: Ubuntu 20.04+ / Debian 10+
- **CMake**: 3.14+
- **Compiler**: GCC 9+ / Clang 10+

### Cài Đặt Dependencies

```bash
sudo apt-get update
sudo apt-get install -y \
    build-essential cmake git pkg-config \
    libssl-dev zlib1g-dev \
    libjsoncpp-dev uuid-dev \
    libeigen3-dev \
    libglib2.0-dev \
    libgstreamer1.0-dev \
    libgstreamer-plugins-base1.0-dev \
    libgstrtspserver-1.0-dev \
    libmosquitto-dev
```

### Build Project

#### Cách 1: Sử dụng Script Tự Động (Khuyến Nghị)

```bash
# Development setup (tự động cài dependencies và build)
./scripts/dev_setup.sh

# Chạy server
./scripts/load_env.sh
```

#### Cách 2: Build Thủ Công

```bash
# 1. Tạo thư mục build
mkdir build && cd build

# 2. Cấu hình với CMake
cmake .. -DCMAKE_BUILD_TYPE=Release

# 3. Build project
make -j$(nproc)

# 4. Chạy server
./bin/edgeos-api
```

### Build với Tests

```bash
cd build
cmake .. -DBUILD_TESTS=ON
make -j$(nproc)
./bin/edgeos-api_tests
```

### Kiểm Tra Build

```bash
# Test API
curl http://localhost:8080/v1/core/health
curl http://localhost:8080/v1/core/version

# Xem Swagger UI
# Mở browser: http://localhost:8080/swagger
```

---

## 📦 Hướng Dẫn Build và Cài Đặt Debian Package (.deb)

### Yêu Cầu Build Package

Các package này cần được cài đặt **trước khi build** Debian package. Script sẽ tự động kiểm tra và báo lỗi nếu thiếu dependencies:

```bash
sudo apt update
sudo apt install -y \
    build-essential cmake git pkg-config \
    debhelper dpkg-dev fakeroot \
    libssl-dev zlib1g-dev \
    libjsoncpp-dev uuid-dev \
    libopencv-dev \
    libgstreamer1.0-dev libgstreamer-plugins-base1.0-dev \
    libmosquitto-dev \
    gstreamer1.0-libav \
    gstreamer1.0-plugins-base \
    gstreamer1.0-plugins-good \
    gstreamer1.0-plugins-bad \
    gstreamer1.0-plugins-ugly \
    libfreetype6-dev libharfbuzz-dev \
    libjpeg-dev libpng-dev libtiff-dev \
    libavcodec-dev libavformat-dev libswscale-dev \
    libgtk-3-dev \
    ffmpeg
```

**⚠️ QUAN TRỌNG cho ALL-IN-ONE Package:** Máy build **BẮT BUỘC** phải có OpenCV 4.10. Xem chi tiết: [packaging/docs/BUILD_ALL_IN_ONE.md](packaging/docs/BUILD_ALL_IN_ONE.md)

### Build Debian Package

Có 2 loại package có thể build:

#### 1. Package Thông Thường

```bash
# Từ project root
./packaging/scripts/build_deb.sh

# Hoặc từ thư mục scripts
cd packaging/scripts
./build_deb.sh
```

**Tùy chọn build:**
```bash
# Clean build (xóa build cũ trước)
./packaging/scripts/build_deb.sh --clean

# Chỉ tạo package từ build có sẵn
./packaging/scripts/build_deb.sh --no-build

# Set version tùy chỉnh
./packaging/scripts/build_deb.sh --version 1.0.0

# Xem tất cả options
./packaging/scripts/build_deb.sh --help
```

#### 2. ALL-IN-ONE Package (Khuyến nghị)

**ALL-IN-ONE package tự chứa TẤT CẢ dependencies**, không cần cài thêm packages khi cài đặt:

```bash
# Build ALL-IN-ONE package
./packaging/scripts/build_deb_all_in_one.sh --sdk-deb <path-to-sdk.deb>

# Ví dụ:
./packaging/scripts/build_deb_all_in_one.sh \
    --sdk-deb ../edgeos-sdk-2025.0.1.3-x86_64.deb

# Tùy chọn
./packaging/scripts/build_deb_all_in_one.sh --sdk-deb <path> --clean
./packaging/scripts/build_deb_all_in_one.sh --sdk-deb <path> --no-build
```

**Script tự động thực hiện:**
- ✅ Kiểm tra dependencies
- ✅ Build project với CMake
- ✅ Bundle tất cả shared libraries (OpenCV, GStreamer, FFmpeg, CVEDIX SDK)
- ✅ Bundle GStreamer plugins
- ✅ Bundle default fonts và models
- ✅ Tạo file .deb package

> ⚠️ **Lưu ý**: Không cần `sudo` để build! Chỉ cần sudo khi **cài đặt** package sau này.

> ⚠️ **QUAN TRỌNG**: Để build ALL-IN-ONE package, máy build **BẮT BUỘC** phải có OpenCV 4.10. Xem chi tiết: [packaging/docs/BUILD_ALL_IN_ONE.md](packaging/docs/BUILD_ALL_IN_ONE.md)

### Cài Đặt Package

Sau khi build, file `.deb` sẽ được tạo tại project root:
- Package thông thường: `edgeos-api-{VERSION}-amd64.deb`
- ALL-IN-ONE package: `edgeos-api-all-in-one-{VERSION}-amd64.deb`

#### Cài Đặt ALL-IN-ONE Package (Khuyến nghị)

**⚠️ BẮT BUỘC - Prerequisites Trước Khi Cài Đặt Package:**

**QUAN TRỌNG:** Để cài đặt package thành công, bạn **BẮT BUỘC** phải chuẩn bị và cài đặt các dependencies sau **TRƯỚC KHI** chạy `dpkg -i`. Nếu không chuẩn bị đầy đủ, quá trình cài đặt sẽ thất bại hoặc gặp lỗi.

**Bước 1: Cập Nhật Package List**
```bash
sudo apt-get update
```

**Bước 2: Cài Đặt System Libraries Cơ Bản (BẮT BUỘC)**
```bash
sudo apt-get install -y \
    libc6 \
    libstdc++6 \
    libgcc-s1 \
    adduser \
    systemd
```

**Bước 3: Cài Đặt FFmpeg (BẮT BUỘC)**

FFmpeg cần thiết cho việc xử lý video và audio:

```bash
sudo apt install -y ffmpeg
```

**Bước 4: Cài Đặt Dependencies Cho OpenCV (Nếu Package Chưa Bundle OpenCV)**

Nếu package chưa bundle OpenCV 4.10, bạn cần cài đặt các dependencies để OpenCV có thể được cài đặt tự động trong quá trình cài package:

```bash
sudo apt-get install -y \
    unzip \
    cmake \
    make \
    g++ \
    wget \
    build-essential \
    pkg-config \
    libfreetype6-dev \
    libharfbuzz-dev \
    libjpeg-dev \
    libpng-dev \
    libtiff-dev \
    libavcodec-dev \
    libavformat-dev \
    libswscale-dev \
    libgtk-3-dev \
    gfortran \
    openexr \
    libatlas-base-dev \
    python3-dev \
    python3-numpy
```

**Bước 5: (Tùy chọn) Cài Đặt GStreamer Plugins Trước**

Để đảm bảo GStreamer plugins hoạt động tốt, bạn có thể cài đặt trước:

```bash
sudo apt-get install -y \
    gstreamer1.0-libav \
    gstreamer1.0-plugins-base \
    gstreamer1.0-plugins-good \
    gstreamer1.0-plugins-bad \
    gstreamer1.0-plugins-ugly \
    gstreamer1.0-tools
```

**Lưu ý quan trọng:**
- ⚠️ **KHÔNG** bỏ qua các bước trên! Cài đặt các dependencies **TRƯỚC KHI** chạy `dpkg -i`.
- Nếu thiếu dependencies, quá trình cài đặt sẽ thất bại hoặc OpenCV không thể cài đặt tự động.
- Trong quá trình cài đặt package (`dpkg -i`), hệ thống không cho phép cài đặt thêm packages khác vì dpkg đang giữ lock.

**Các bước cài đặt package:**

```bash
# Bước 1: Cài đặt package
sudo dpkg -i edgeos-api-all-in-one-*.deb

# Trong quá trình cài đặt, nếu thiếu OpenCV 4.10, hệ thống sẽ hiển thị:
# ==========================================
# OpenCV 4.10 Installation Required
# ==========================================
# Choose an option:
#   1) Install OpenCV 4.10 automatically (recommended)
#   2) Skip installation and install manually later
#
# Chọn option 1 để cài đặt tự động (mất khoảng 30-60 phút)

# Bước 2: Nếu có lỗi dependencies (hiếm khi xảy ra với ALL-IN-ONE)
sudo apt-get install -f

# Bước 3: Nếu OpenCV cài đặt bị lỗi hoặc bị gián đoạn, chạy lại script cài đặt:
sudo /opt/edgeos-api/scripts/build_opencv_safe.sh

# Bước 4: Khởi động service
sudo systemctl start edgeos-api
sudo systemctl enable edgeos-api  # Tự động chạy khi khởi động

# Bước 5: Kiểm tra service
sudo systemctl status edgeos-api

# Bước 6: Test API
curl http://localhost:8080/v1/core/health
```

**Lưu ý về OpenCV:**
- Nếu package đã bundle OpenCV 4.10, quá trình cài đặt sẽ không yêu cầu cài thêm.
- Nếu thiếu OpenCV 4.10, quá trình cài đặt sẽ tự động phát hiện và cho phép cài đặt tự động.
- Nếu cài đặt OpenCV bị lỗi, chạy lại: `sudo /opt/edgeos-api/scripts/build_opencv_safe.sh`

#### Cài Đặt Package Thông Thường

**⚠️ Quan trọng - Prerequisites:**

Trước khi cài đặt package thông thường, cần cài dependencies trước:

```bash
sudo apt-get update
sudo apt-get install -y \
    unzip \
    cmake \
    make \
    g++ \
    wget \
    ffmpeg
```

**Lý do:** Trong quá trình cài đặt package (`dpkg -i`), hệ thống không cho phép cài đặt thêm packages khác vì dpkg đang giữ lock.

**Cài đặt package:**

```bash
# 1. Cài dependencies cho OpenCV và FFmpeg (nếu muốn cài OpenCV tự động)
sudo apt-get update
sudo apt-get install -y \
    unzip \
    cmake \
    make \
    g++ \
    wget \
    ffmpeg

# 2. Cài đặt
sudo dpkg -i edgeos-api-*.deb

# 3. Nếu có lỗi dependencies, chạy:
sudo apt-get install -f

# 4. Khởi động service
sudo systemctl start edgeos-api
sudo systemctl enable edgeos-api

# 5. Kiểm tra service
sudo systemctl status edgeos-api
```

**Nếu chưa cài OpenCV 4.10, cài sau:**

```bash
sudo apt-get update
sudo apt-get install -y \
    unzip \
    cmake \
    make \
    g++ \
    wget \
    ffmpeg
sudo /opt/edgeos-api/scripts/build_opencv_safe.sh
sudo systemctl restart edgeos-api
```

### Verify Installation

```bash
# Kiểm tra package status
dpkg -l | grep edgeos-api

# Kiểm tra libraries
ls -la /opt/edgeos-api/lib/

# Kiểm tra GStreamer plugins (ALL-IN-ONE)
ls -la /opt/edgeos-api/lib/gstreamer-1.0/

# Kiểm tra default fonts và models (ALL-IN-ONE)
ls -la /opt/edgeos-api/fonts/
ls -la /opt/edgeos-api/models/

# Kiểm tra CVEDIX SDK
ls -la /opt/cvedix/lib/

# Test executable
/usr/local/bin/edgeos-api --help

# Kiểm tra service status
sudo systemctl status edgeos-api

# Xem log
sudo journalctl -u edgeos-api -f

# Test API
curl http://localhost:8080/v1/core/health
curl http://localhost:8080/v1/core/version
```

### Cấu Trúc Sau Khi Cài Đặt

Sau khi cài đặt package, các file sẽ được đặt tại:

- **Executable**: `/usr/local/bin/edgeos-api`
- **Libraries**: `/opt/edgeos-api/lib/` (bundled - tự chứa)
- **GStreamer plugins**: `/opt/edgeos-api/lib/gstreamer-1.0/` (ALL-IN-ONE)
- **Config**: `/opt/edgeos-api/config/`
- **Data**: `/opt/edgeos-api/` (instances, solutions, models, logs, etc.)
- **Fonts**: `/opt/edgeos-api/fonts/` (default fonts - ALL-IN-ONE)
- **Models**: `/opt/edgeos-api/models/` (default models - ALL-IN-ONE)
- **Service**: `/etc/systemd/system/edgeos-api.service`

### Quản Lý Service

```bash
# Khởi động
sudo systemctl start edgeos-api

# Dừng
sudo systemctl stop edgeos-api

# Khởi động lại
sudo systemctl restart edgeos-api

# Xem status
sudo systemctl status edgeos-api

# Xem log
sudo journalctl -u edgeos-api -n 100
```

### Gỡ Cài Đặt

```bash
# Gỡ package
sudo dpkg -r edgeos-api

# Hoặc gỡ hoàn toàn (bao gồm config files)
sudo dpkg -P edgeos-api
```

---

## 🚀 Production Deployment

### Sử dụng Production Setup Script

```bash
# Full deployment (cần sudo)
sudo ./scripts/prod_setup.sh

# Hoặc sử dụng deploy script trực tiếp
sudo ./deploy/deploy.sh
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

## 🔧 Tính Năng Package

✅ **Bundled Libraries**: Tất cả shared libraries được bundle vào package  
✅ **RPATH Configuration**: Executable tự động tìm libraries trong package  
✅ **Systemd Integration**: Tự động tạo và enable systemd service  
✅ **User Management**: Tự động tạo user `edgeai`  
✅ **Directory Structure**: Tự động tạo cấu trúc thư mục cần thiết  
✅ **ldconfig**: Tự động cấu hình ldconfig để tìm libraries  

---

## 📝 Tóm Tắt

| Bước | Lệnh | Cần Sudo? |
|------|------|-----------|
| **Cài dependencies** | `sudo apt-get install -y build-essential cmake ...` | ✅ **CÓ** |
| **Build từ source** | `./scripts/dev_setup.sh` | ❌ **KHÔNG** |
| **Build .deb** | `./build_deb.sh` | ❌ **KHÔNG** |
| **Cài đặt package** | `sudo dpkg -i *.deb` | ✅ **CÓ** |
| **Khởi động service** | `sudo systemctl start edgeos-api` | ✅ **CÓ** |

---

## 🐛 Troubleshooting

### Lỗi Build: "Could NOT find Jsoncpp"

```bash
sudo apt-get install libjsoncpp-dev
```

### Lỗi Build: "dpkg-buildpackage: command not found"

```bash
sudo apt-get install -y dpkg-dev debhelper
```

### Lỗi: "Could not find required libraries"

Đảm bảo CVEDIX SDK đã được cài đặt tại `/opt/cvedix/lib` hoặc libraries đã được bundle vào package.

### Lỗi: "Service failed to start"

Kiểm tra log:
```bash
sudo journalctl -u edgeos-api -n 50
```

Kiểm tra permissions:
```bash
sudo chown -R edgeai:edgeai /opt/edgeos-api
```

### Libraries không được tìm thấy

Kiểm tra ldconfig:
```bash
sudo ldconfig -v | grep edgeos-api
```

Nếu không có, chạy lại:
```bash
sudo ldconfig
```

### CVEDIX SDK symlinks

```bash
# Chạy lại dev setup để fix symlinks
./scripts/dev_setup.sh --skip-deps --skip-build
```

---

## ✨ Core Features

### 🧠 Core System
- Health check & version info
- System hardware information (CPU, RAM, Disk, OS, GPU)
- Runtime system status (CPU/RAM usage, load average, uptime)
- Watchdog & health monitor
- Prometheus metrics endpoint
- Endpoint statistics

### 🧾 Logging & Observability
- Quản lý log theo category: `api`, `instance`, `sdk_output`, `general`
- Filter theo level, time range, tail lines
- Truy xuất log theo ngày hoặc realtime

---

## 🤖 AI Processing (in develop)
- Xử lý ảnh/frame đơn (base64)
- Priority-based queue & rate limiting
- Theo dõi AI runtime status & metrics
- Batch processing endpoint (chưa implement – trả về 501)

---

## ⚙️ Configuration Management
- Get / Update / Replace toàn bộ system configuration
- Update & delete config theo path (query & path parameter)
- Reset config về mặc định
- Persist configuration xuống file

---

## 📦 Instance Management
- Tạo, cập nhật, xóa instance AI
- Start / Stop / Restart instance
- Batch start / stop / restart song song
- Persistent instance (auto-load khi restart service)
- AutoStart / AutoRestart

### Runtime & Output
- Lấy runtime status, FPS, latency, statistics
- Truy xuất output (FILE / RTMP / RTSP)
- Lấy last processed frame (base64 JPEG)
- Cấu hình input source (RTSP / FILE / Manual)
- Stream & record output configuration

---

## 📐 Lines API (Behavior Analysis)
- CRUD crossing lines cho `ba_crossline`
- Realtime update, không cần restart instance
- Hỗ trợ direction, class filter, color RGBA

---

## 🧩 Solution Management (in develop)
- Danh sách solution mặc định & custom
- CRUD custom solution
- Pipeline-based solution definition
- Sinh tự động schema & example body cho tạo instance

---

## 🗂️ Group Management
- Quản lý group instance
- Gán instance theo group
- Group mặc định & read-only protection

---

## 🧱 Node & Pipeline (in develop)
- Node template discovery
- Pre-configured node pool
- CRUD node (source, detector, processor, destination, broker)
- Node availability & statistics
- Dynamic parameter schema cho UI

---

## 🎥 Media & Asset Management
### Video
- Upload / list / rename / delete video files

### Model
- Upload / list / rename / delete AI models

### Font
- Upload / list / rename fonts (OSD / rendering)

---

## 👤 Face Recognition
- Face recognition từ ảnh upload
- Face registration & subject management
- Search appearance (cosine similarity)
- Rename & merge subject
- Batch delete / delete all faces
- Hỗ trợ MySQL / PostgreSQL face database
- Fallback sang file-based database

---

## ⚠️ Known Limitations

### Chức Năng Chưa Hoàn Thiện
- AI batch processing endpoint trả về 501 (Not Implemented)

### Build Flags
Một số detector yêu cầu build flags tùy chọn:
- `CVEDIX_WITH_TRT` - TensorRT support
- `CVEDIX_WITH_RKNN` - Rockchip RKNN support  
- `CVEDIX_WITH_PADDLE` - PaddlePaddle support

### Dependencies
- Yêu cầu CVEDIX SDK được cài đặt tại `/opt/cvedix/lib` hoặc bundle vào package
- Một số tính năng yêu cầu GStreamer plugins đầy đủ

---

## 🔧 Breaking Changes
- Không có breaking changes trong version này (first stable release)

---

## 📌 Roadmap (Preview)
- AI batch processing
- Authentication & RBAC
- WebSocket / Event streaming
- Instance template & cloning
- Multi-tenant support

---

## 🧪 API Documentation & Testing

Toàn bộ danh sách API, request/response schema và ví dụ `curl` để **test trực tiếp API** được mô tả chi tiết trong:

- **Swagger UI**: http://localhost:8080/swagger (khi server đang chạy)
- **OpenAPI Spec**: http://localhost:8080/openapi.yaml
- **API Reference**: [docs/API.md](docs/API.md)

**Công cụ test:**
- Swagger UI - Giao diện web tương tác
- Postman Collection - [api-specs/postman/api.collection.json](api-specs/postman/api.collection.json)
- `curl` commands - Xem ví dụ trong [docs/API_document.md](docs/API_document.md)



---

## 📚 Tài Liệu Tham Khảo

- [README.md](README.md) - Tổng quan project
- [packaging/docs/BUILD_DEB.md](packaging/docs/BUILD_DEB.md) - Chi tiết build Debian package thông thường
- [packaging/docs/BUILD_ALL_IN_ONE.md](packaging/docs/BUILD_ALL_IN_ONE.md) - Chi tiết build ALL-IN-ONE package (khuyến nghị)
- [docs/API.md](docs/API.md) - Full API reference
- [docs/DEVELOPMENT.md](docs/DEVELOPMENT.md) - Development guide
- [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) - System architecture
- [docs/DEFAULT_SOLUTIONS_REFERENCE.md](docs/DEFAULT_SOLUTIONS_REFERENCE.md) - Default solutions
- [deploy/README.md](deploy/README.md) - Production deployment guide

---

## 📞 Hỗ Trợ

Nếu gặp vấn đề, vui lòng:
1. Kiểm tra [Troubleshooting](#-troubleshooting) section
2. Xem log: `sudo journalctl -u edgeos-api -n 100`
3. Liên hệ support team

