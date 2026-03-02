# Hướng Dẫn Build và Cài Đặt Debian Package

File này hướng dẫn cách build file `.deb` tự chứa tất cả dependencies và cách cài đặt.

## 📋 Tóm Tắt Nhanh

### Build Package
```bash
./packaging/scripts/build_deb.sh
```

### Cài Đặt Package
```bash
# 1. Cài dependencies (nếu muốn cài OpenCV tự động)
sudo apt-get update
sudo apt-get install -y \
    unzip \
    cmake \
    make \
    g++ \
    wget \
    ffmpeg

# 2. Cài package
sudo dpkg -i edge-ai-api-*.deb

# 3. Fix dependencies nếu có lỗi
sudo apt-get install -f

# 4. Khởi động service
sudo systemctl start edge-ai-api
```

### Khắc Phục Lỗi
- **Lỗi dependencies**: Xem [Khắc Phục Lỗi Thiếu Packages](#khắc-phục-lỗi-thiếu-packages-trong-quá-trình-cài-đặt)
- **Service không start**: Xem [Troubleshooting](#-troubleshooting)
- **Libraries không tìm thấy**: Xem phần [Libraries không được tìm thấy](#libraries-không-được-tìm-thấy)

## 📦 Packaging Directory

Thư mục `packaging/` chứa các scripts và tài liệu liên quan đến việc build Debian package (.deb).

**Cấu trúc:**
```
packaging/
├── scripts/           # Build scripts
│   └── build_deb.sh   # Script chính để build .deb package
└── docs/              # Tài liệu hướng dẫn
    └── BUILD_DEB.md   # File này
```

## 🚀 Quick Start - Chỉ Cần Một Lệnh!

Có 2 cách để build:

**Option 1: Dùng Đường Dẫn Đầy Đủ**
```bash
# Từ project root
./packaging/scripts/build_deb.sh
```

**Option 2: Từ Thư Mục Packaging**
```bash
cd packaging/scripts
./build_deb.sh
```

**Sau khi build:**
```bash
# File sẽ được tạo: edge-ai-api-2025.0.1.3-Beta-amd64.deb

# Cài đặt
sudo dpkg -i edge-ai-api-2025.0.1.3-Beta-amd64.deb

# Khởi động service
sudo systemctl start edge-ai-api
```

**Script `packaging/scripts/build_deb.sh` tự động làm tất cả:**
- ✅ Kiểm tra dependencies
- ✅ Build project
- ✅ Bundle libraries
- ✅ Tạo file .deb

> ⚠️ **Lưu ý**: Không cần `sudo` để build! Chỉ cần sudo khi **cài đặt** package sau này.

## 📋 Yêu Cầu Build

Script sẽ tự động kiểm tra và báo lỗi nếu thiếu. Cài đặt với:

```bash
sudo apt-get update
sudo apt-get install -y \
    build-essential cmake git \
    debhelper dpkg-dev \
    libssl-dev zlib1g-dev \
    libjsoncpp-dev uuid-dev pkg-config \
    libopencv-dev \
    libgstreamer1.0-dev libgstreamer-plugins-base1.0-dev \
    libmosquitto-dev \
    ffmpeg
```

## 🔧 Build Package

```bash
# Build với script tự động (khuyến nghị - tất cả trong một)
./packaging/scripts/build_deb.sh

# Hoặc với các tùy chọn
./packaging/scripts/build_deb.sh --clean          # Clean build trước
./packaging/scripts/build_deb.sh --no-build       # Chỉ tạo package từ build có sẵn
./packaging/scripts/build_deb.sh --version 1.0.0  # Set version tùy chỉnh
./packaging/scripts/build_deb.sh --help           # Xem tất cả options
```

## 💾 Cài Đặt Package

### ⚠️ Quan Trọng: Prerequisites Trước Khi Cài Đặt

**Trước khi cài đặt package**, bạn cần cài đặt các dependencies sau nếu muốn cài OpenCV 4.10 tự động:

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

**Lý do:** Trong quá trình cài đặt package (`dpkg -i`), hệ thống không cho phép cài đặt thêm packages khác vì dpkg đang giữ lock. Do đó, nếu bạn muốn cài OpenCV 4.10 tự động trong quá trình cài đặt package, các dependencies trên phải được cài đặt **trước**.

**Nếu không cài dependencies trước:**
- Package vẫn sẽ được cài đặt thành công
- OpenCV 4.10 sẽ được bỏ qua trong quá trình cài đặt
- Bạn có thể cài OpenCV 4.10 sau bằng cách:
  ```bash
  sudo apt-get update
  sudo apt-get install -y \
      unzip \
      cmake \
      make \
      g++ \
      wget \
      ffmpeg
  sudo /opt/edge_ai_api/scripts/build_opencv_safe.sh
  ```

### Các Bước Cài Đặt

**Sau khi có file .deb**, mới cần sudo để cài đặt:

```bash
# 1. Cài đặt dependencies cho OpenCV (nếu muốn cài OpenCV tự động)
sudo apt-get update
sudo apt-get install -y \
    unzip \
    cmake \
    make \
    g++ \
    wget \
    ffmpeg

# 2. Cài đặt package
sudo dpkg -i edge-ai-api-2025.0.1.3-Beta-amd64.deb

# 3. Nếu có lỗi dependencies
sudo apt-get install -f

# 4. Khởi động service
sudo systemctl start edge-ai-api
sudo systemctl enable edge-ai-api  # Tự động chạy khi khởi động
```

### Khắc Phục Lỗi Thiếu Packages Trong Quá Trình Cài Đặt

#### Lỗi: "dpkg: dependency problems prevent configuration"

Nếu gặp lỗi này, có nghĩa là một số dependencies chưa được cài đặt:

```bash
# Bước 1: Cài đặt các dependencies còn thiếu
sudo apt-get install -f

# Bước 2: Nếu vẫn lỗi, cài đặt thủ công các packages còn thiếu
sudo apt-get update
sudo apt-get install -y \
    libc6 \
    libstdc++6 \
    libgcc-s1 \
    adduser \
    systemd

# Bước 3: Thử cài lại package
sudo dpkg -i edge-ai-api-2025.0.1.3-Beta-amd64.deb
```

#### Lỗi: "dpkg: error processing package"

Nếu gặp lỗi này trong quá trình cài đặt:

```bash
# Bước 1: Xem chi tiết lỗi
sudo dpkg --configure -a

# Bước 2: Nếu package bị broken, remove và cài lại
sudo dpkg --remove --force-remove-reinstreq edge-ai-api
sudo dpkg -i edge-ai-api-2025.0.1.3-Beta-amd64.deb

# Bước 3: Fix dependencies
sudo apt-get install -f
```

#### Lỗi: "E: Sub-process /usr/bin/dpkg returned an error code"

```bash
# Bước 1: Unlock dpkg nếu bị lock
sudo rm /var/lib/dpkg/lock-frontend
sudo rm /var/lib/dpkg/lock
sudo rm /var/cache/apt/archives/lock

# Bước 2: Reconfigure dpkg
sudo dpkg --configure -a

# Bước 3: Cài lại package
sudo dpkg -i edge-ai-api-2025.0.1.3-Beta-amd64.deb
```

#### Lỗi: "Package is in a very bad inconsistent state"

```bash
# Bước 1: Remove package hoàn toàn
sudo dpkg --remove --force-remove-reinstreq edge-ai-api
sudo apt-get purge edge-ai-api

# Bước 2: Clean up
sudo apt-get autoremove
sudo apt-get autoclean

# Bước 3: Cài lại từ đầu
sudo dpkg -i edge-ai-api-2025.0.1.3-Beta-amd64.deb
sudo apt-get install -f
```

### Cài Đặt OpenCV 4.10 Sau Khi Cài Package

Nếu bạn đã cài package nhưng chưa cài OpenCV 4.10, bạn có thể cài sau:

```bash
# 1. Cài dependencies
sudo apt-get update
sudo apt-get install -y \
    unzip \
    cmake \
    make \
    g++ \
    wget \
    ffmpeg

# 2. Chạy script cài OpenCV 4.10
sudo /opt/edge_ai_api/scripts/build_opencv_safe.sh

# 3. Khởi động lại service
sudo systemctl restart edge-ai-api
```

## ✅ Kiểm Tra

```bash
# Kiểm tra service
sudo systemctl status edge-ai-api

# Xem log
sudo journalctl -u edge-ai-api -f

# Test API
curl http://localhost:8080/v1/core/health
```

## 📦 Cấu Trúc Package

Sau khi cài đặt:

- **Executable**: `/usr/local/bin/edge_ai_api`
- **Libraries**: `/opt/edge_ai_api/lib/` (bundled - tất cả trong một nơi)
- **Config**: `/opt/edge_ai_api/config/`
- **Data**: `/opt/edge_ai_api/` (instances, solutions, models, logs, etc.)
- **Service**: `/etc/systemd/system/edgeos-api.service`

## ✨ Tính Năng

✅ **Bundled Libraries**: Tất cả shared libraries được bundle vào package
✅ **RPATH Configuration**: Executable tự động tìm libraries trong package
✅ **Systemd Integration**: Tự động tạo và enable systemd service
✅ **User Management**: Tự động tạo user `edgeai`
✅ **Directory Structure**: Tự động tạo cấu trúc thư mục cần thiết
✅ **ldconfig**: Tự động cấu hình ldconfig để tìm libraries

## 📝 Tóm Tắt

| Bước | Lệnh | Cần Sudo? |
|------|------|-----------|
| **Build .deb** | `./packaging/scripts/build_deb.sh` | ❌ **KHÔNG** |
| **Cài đặt package** | `sudo dpkg -i *.deb` | ✅ **CÓ** |
| **Khởi động service** | `sudo systemctl start edge-ai-api` | ✅ **CÓ** |

## 🛠️ Script Làm Gì?

1. ✅ Kiểm tra dependencies
2. ✅ Build project với CMake
3. ✅ Bundle tất cả libraries
4. ✅ Tạo file .deb package
5. ✅ Đặt tên file đúng format

Tất cả trong một lần chạy!

## 🐛 Troubleshooting

### Lỗi Build: "dpkg-buildpackage: command not found"

```bash
sudo apt-get install -y dpkg-dev debhelper
```

### Lỗi Build: "Could not find required libraries"

Đảm bảo CVEDIX SDK đã được cài đặt tại `/opt/cvedix/lib` hoặc libraries đã được bundle vào package.

### Lỗi Cài Đặt: "dpkg: error processing package"

Xem phần [Khắc Phục Lỗi Thiếu Packages](#khắc-phục-lỗi-thiếu-packages-trong-quá-trình-cài-đặt) ở trên.

### Lỗi: "Service failed to start"

**Kiểm tra log:**
```bash
sudo journalctl -u edge-ai-api -n 50
sudo journalctl -u edge-ai-api -f  # Follow logs
```

**Kiểm tra permissions:**
```bash
sudo chown -R edgeai:edgeai /opt/edge_ai_api
sudo chmod -R 755 /opt/edge_ai_api
```

**Kiểm tra executable:**
```bash
ls -la /usr/local/bin/edge_ai_api
file /usr/local/bin/edge_ai_api
```

**Kiểm tra libraries:**
```bash
ldd /usr/local/bin/edge_ai_api | grep "not found"
```

### Libraries không được tìm thấy

**Kiểm tra ldconfig:**
```bash
sudo ldconfig -v | grep edge-ai-api
sudo ldconfig -v | grep cvedix
```

**Nếu không có, chạy lại:**
```bash
sudo ldconfig
```

**Kiểm tra RPATH:**
```bash
readelf -d /usr/local/bin/edge_ai_api | grep RPATH
```

**Kiểm tra libraries trong package:**
```bash
ls -la /opt/edge_ai_api/lib/
```

### Lỗi: "GStreamer plugins not found"

**Kiểm tra GST_PLUGIN_PATH:**
```bash
cat /opt/edge_ai_api/config/.env | grep GST_PLUGIN_PATH
```

**Kiểm tra plugins:**
```bash
ls -la /opt/edge_ai_api/lib/gstreamer-1.0/
```

**Nếu thiếu plugins:**
```bash
# Cài đặt GStreamer plugins trên system
sudo apt-get install -y \
    gstreamer1.0-plugins-base \
    gstreamer1.0-plugins-good \
    gstreamer1.0-plugins-bad \
    gstreamer1.0-plugins-ugly

# Hoặc copy từ system vào bundled directory
sudo cp -r /usr/lib/x86_64-linux-gnu/gstreamer-1.0/* \
    /opt/edge_ai_api/lib/gstreamer-1.0/
```

### Lỗi: "OpenCV not found"

**Kiểm tra OpenCV:**
```bash
opencv_version
ldconfig -p | grep opencv
```

**Nếu thiếu OpenCV 4.10:**
```bash
# Cài dependencies
sudo apt-get update
sudo apt-get install -y \
    unzip \
    cmake \
    make \
    g++ \
    wget \
    ffmpeg

# Build và cài OpenCV 4.10
sudo /opt/edge_ai_api/scripts/build_opencv_safe.sh

# Restart service
sudo systemctl restart edge-ai-api
```

### Lỗi: "Permission denied"

**Kiểm tra user và group:**
```bash
id edgeai
groups edgeai
```

**Fix permissions:**
```bash
sudo chown -R edgeai:edgeai /opt/edge_ai_api
sudo chmod -R 755 /opt/edge_ai_api
sudo chmod 640 /opt/edge_ai_api/config/.env
```

### Lỗi: "Port already in use"

**Kiểm tra port:**
```bash
sudo netstat -tlnp | grep 8080
sudo lsof -i :8080
```

**Nếu port đang được sử dụng:**
```bash
# Stop service
sudo systemctl stop edge-ai-api

# Hoặc thay đổi port trong config
sudo nano /opt/edge_ai_api/config/config.json
```

### Lỗi: "Cannot connect to service"

**Kiểm tra service status:**
```bash
sudo systemctl status edge-ai-api
```

**Kiểm tra network:**
```bash
curl http://localhost:8080/v1/core/health
```

**Kiểm tra firewall:**
```bash
sudo ufw status
sudo iptables -L -n
```

## 📝 Lưu Ý

1. **Bundled Libraries**: Package bundle tất cả shared libraries cần thiết vào `/opt/edge_ai_api/lib`. Điều này đảm bảo ứng dụng hoạt động ngay cả khi hệ thống thiếu một số dependencies.

2. **RPATH**: Executable được cấu hình với RPATH để tìm libraries trong `/opt/edge_ai_api/lib` trước khi tìm trong system paths.

3. **CVEDIX SDK**: Nếu CVEDIX SDK được cài đặt tại `/opt/cvedix/lib`, các libraries sẽ được tự động bundle vào package.

4. **System Dependencies**: Một số system dependencies vẫn cần được cài đặt (như libssl3, libc6, etc.) nhưng chúng thường đã có sẵn trên hệ thống Debian/Ubuntu.

5. **File .deb được tạo sẽ nằm ở project root**

6. **Thư mục `debian/` phải ở project root** (theo convention của Debian)

7. **Không cần sudo để build** - chỉ cần sudo khi cài đặt package

## 🔗 Liên Quan

- `debian/` - Debian package source files (phải ở root)
- `deploy/` - Production deployment scripts
- `scripts/` - Development scripts
- `docs/` - General documentation
