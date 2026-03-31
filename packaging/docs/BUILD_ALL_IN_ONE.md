# Build All-In-One Debian Package

## 📋 Tóm Tắt Nhanh

### ⚠️ Prerequisites Bắt Buộc Trước Khi Build

**QUAN TRỌNG:** Trước khi build package, bạn **BẮT BUỘC** phải cài đặt tất cả dependencies và **OpenCV 4.10** trên máy build. Xem chi tiết: [Yêu Cầu Hệ Thống](#-yêu-cầu-hệ-thống-prerequisites)

### Build Package
```bash
./packaging/scripts/build_deb_all_in_one.sh --sdk-deb <path-to-sdk.deb>
```

### Cài Đặt Package
```bash
# Chỉ cần một lệnh (ALL-IN-ONE không cần dependencies!)
sudo dpkg -i omniapi-all-in-one-*.deb

# Nếu có lỗi (hiếm)
sudo apt-get install -f

# Khởi động service
sudo systemctl start omniapi
```

### Khắc Phục Lỗi
- **Lỗi dependencies**: Xem [Khắc Phục Lỗi Thiếu Packages](#khắc-phục-lỗi-thiếu-packages-trong-quá-trình-cài-đặt)
- **GStreamer plugins**: Xem [Cài Đặt GStreamer Plugins](#-cài-đặt-gstreamer-plugins)
- **GStreamer errors**: Xem [GStreamer plugins không hoạt động](#gstreamer-plugins-không-hoạt-động)
- **OpenCV errors**: Xem [OpenCV không được tìm thấy](#opencv-không-được-tìm-thấy)
- **Service không start**: Xem [Service failed to start](#service-failed-to-start)

## 📦 Tổng Quan

Script `build_deb_all_in_one.sh` tạo một package **ALL-IN-ONE** - tự chứa **TẤT CẢ** dependencies:

- ✅ CVEDIX SDK runtime (bundled)
- ✅ OpenCV libraries (bundled)
- ✅ GStreamer libraries và plugins (bundled)
- ✅ FFmpeg libraries (bundled)
- ✅ Default fonts và models từ `cvedix_data/` (bundled)
- ✅ Tất cả libraries khác (bundled)

Package này **chỉ cần system libraries cơ bản** (libc6, libstdc++6, libgcc-s1) và có thể cài đặt trên bất kỳ Ubuntu/Debian nào mà **không cần cài dependencies**.

## 📋 Yêu Cầu Hệ Thống (Prerequisites)

**⚠️ QUAN TRỌNG:** Trước khi build package, bạn **BẮT BUỘC** phải cài đặt tất cả các dependencies sau trên máy build. Package sẽ không hoạt động đúng nếu thiếu bất kỳ dependency nào.

### Bước 1: Cập Nhật Package List

```bash
sudo apt-get update
```

### Bước 2: Cài Đặt Build Tools Cơ Bản

```bash
sudo apt-get install -y \
    build-essential \
    make \
    cmake \
    debhelper \
    dpkg-dev
```

### Bước 3: Cài Đặt Dependencies Cơ Bản

```bash
sudo apt-get install -y \
    mosquitto \
    mosquitto-clients \
    unzip \
    libmosquitto-dev \
    libturbojpeg0 \
    libturbojpeg0-dev \
    libgstreamer1.0-dev \
    libgstreamer-plugins-base1.0-dev \
    gstreamer1.0-plugins-good \
    gstreamer1.0-plugins-bad \
    gstreamer1.0-plugins-ugly \
    gstreamer1.0-tools \
    build-essential \
    cmake \
    pkg-config \
    libssl-dev \
    libcurl4-openssl-dev \
    libjson-c-dev
```

### Bước 4: Cài Đặt GStreamer Runtime Libraries

```bash
sudo apt install -y \
    libgstreamer1.0-0 \
    gstreamer1.0-plugins-base \
    gstreamer1.0-plugins-good \
    gstreamer1.0-plugins-bad \
    gstreamer1.0-plugins-ugly \
    gstreamer1.0-libav \
    gstreamer1.0-tools \
    gstreamer1.0-x \
    gstreamer1.0-alsa \
    gstreamer1.0-gl \
    gstreamer1.0-gtk3 \
    gstreamer1.0-qt5 \
    gstreamer1.0-pulseaudio
```

### Bước 5: Cài Đặt GStreamer Development Libraries

```bash
sudo apt install -y \
    libgstreamer1.0-dev \
    libgstreamer-plugins-base1.0-dev \
    python3-gst-1.0
```

### Bước 6: Cài Đặt GStreamer RTSP Support

```bash
sudo apt-get install -y \
    libgstrtspserver-1.0-dev \
    gstreamer1.0-rtsp
```

### Bước 7: Cài Đặt Build Dependencies Cho OpenCV và Image Processing

```bash
sudo apt install -y \
    build-essential \
    cmake \
    git \
    pkg-config \
    libgtk-3-dev \
    libavcodec-dev \
    libavformat-dev \
    libswscale-dev \
    libv4l-dev \
    libxvidcore-dev \
    libx264-dev \
    libjpeg-dev \
    libpng-dev \
    libtiff-dev \
    gfortran \
    openexr \
    libatlas-base-dev \
    python3-dev \
    python3-numpy \
    ffmpeg
```

### Bước 8: Cài Đặt OpenCV 4.10 (BẮT BUỘC)

**⚠️ QUAN TRỌNG:** OpenCV 4.10 là **BẮT BUỘC** để package hoạt động đúng. Package sẽ không hoạt động với các phiên bản OpenCV khác.

#### Cách 1: Build từ Source (Khuyến nghị)

```bash
# Clone OpenCV repository
cd /tmp
git clone https://github.com/opencv/opencv.git
cd opencv
git checkout 4.10.0

# Build và cài đặt
mkdir build && cd build
cmake -D CMAKE_BUILD_TYPE=Release \
      -D CMAKE_INSTALL_PREFIX=/usr/local \
      -D OPENCV_GENERATE_PKGCONFIG=ON \
      ..
make -j$(nproc)
sudo make install
sudo ldconfig

# Verify OpenCV version
pkg-config --modversion opencv4
```

#### Cách 2: Cài Đặt Tự Động Khi Cài Package (Trên Máy Cài Đặt)

Khi cài đặt package từ file `.deb`, nếu hệ thống thiếu OpenCV 4.10, quá trình cài đặt sẽ tự động phát hiện và hiển thị thông báo:

```
Checking OpenCV 4.10 installation...

⚠  OpenCV 4.10 is not properly installed or freetype library is missing.

==========================================
OpenCV 4.10 Installation Required
==========================================

OpenCV 4.10 with freetype support is required for omniapi.
The installation process will take approximately 30-60 minutes.

Checking disk space...
  ✓ Sufficient disk space available (27 GB)
Checking network connectivity...
  ✓ Network connectivity OK
Choose an option:
  1) Install OpenCV 4.10 automatically (recommended)
  2) Skip installation and install manually later
```

**Chọn option 1** để cài đặt OpenCV 4.10 tự động. Quá trình này sẽ mất khoảng 30-60 phút.

**Nếu cài đặt bị lỗi hoặc bị gián đoạn**, bạn có thể chạy lại script cài đặt tự động:

```bash
sudo /opt/omniapi/scripts/build_opencv_safe.sh
```

Script này sẽ tự động:
- Kiểm tra disk space và network connectivity
- Download và build OpenCV 4.10 từ source
- Cài đặt với freetype support
- Verify installation sau khi hoàn thành

**Kiểm tra OpenCV version sau khi cài đặt:**

```bash
# Kiểm tra version OpenCV đã cài
pkg-config --modversion opencv4

# Phải hiển thị 4.10.x
```

**Lưu ý:** 
- Trên máy build: Script build sẽ tự động bundle OpenCV từ system vào package, vì vậy đảm bảo OpenCV 4.10 đã được cài đặt đúng cách trên máy build trước khi build package (sử dụng Cách 1).
- Trên máy cài đặt: Nếu package không bundle OpenCV hoặc OpenCV bị thiếu, sử dụng Cách 2 để cài đặt tự động.

## 🚀 Sử Dụng

### Quick Start

```bash
# Từ project root
./packaging/scripts/build_deb_all_in_one.sh --sdk-deb <path-to-sdk.deb>

# Hoặc từ thư mục scripts
cd packaging/scripts
./build_deb_all_in_one.sh --sdk-deb <path-to-sdk.deb>
```

### Options

```bash
--sdk-deb PATH    Path to SDK .deb file (required)
--clean           Clean build directory trước khi build
--no-build        Skip build (chỉ tạo package từ build có sẵn)
--version VER     Set version (default: auto-detect)
--help            Hiển thị help
```

### Ví Dụ

```bash
# Build với SDK
./packaging/scripts/build_deb_all_in_one.sh \
    --sdk-deb ../edgeos-sdk-2025.0.1.3-x86_64.deb

# Clean build
./packaging/scripts/build_deb_all_in_one.sh \
    --sdk-deb ../edgeos-sdk-2025.0.1.3-x86_64.deb \
    --clean

# Chỉ tạo package từ build có sẵn
./packaging/scripts/build_deb_all_in_one.sh \
    --sdk-deb ../edgeos-sdk-2025.0.1.3-x86_64.deb \
    --no-build
```

## 📦 Cài Đặt Package

Sau khi build, file `.deb` sẽ được tạo tại project root:

```
omniapi-all-in-one-2026.0.1.22-amd64.deb
```

### Cài Đặt

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
sudo dpkg -i omniapi-all-in-one-2026.0.1.22-amd64.deb

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
sudo /opt/omniapi/scripts/build_opencv_safe.sh

# Bước 4: Khởi động service
sudo systemctl start omniapi
sudo systemctl enable omniapi  # Tự động chạy khi khởi động

# Bước 5: (Tùy chọn) Cài đặt GStreamer plugins nếu cần
# Xem [Cài Đặt GStreamer Plugins](#-cài-đặt-gstreamer-plugins) để biết chi tiết
sudo apt-get update
sudo apt-get install -y \
    gstreamer1.0-libav \
    gstreamer1.0-plugins-base \
    gstreamer1.0-plugins-good \
    gstreamer1.0-plugins-bad \
    gstreamer1.0-plugins-ugly
```

**Lưu ý về OpenCV:**
- Nếu package đã bundle OpenCV 4.10, quá trình cài đặt sẽ không yêu cầu cài thêm.
- Nếu thiếu OpenCV 4.10, quá trình cài đặt sẽ tự động phát hiện và cho phép cài đặt tự động.
- Nếu cài đặt OpenCV bị lỗi, chạy lại: `sudo /opt/omniapi/scripts/build_opencv_safe.sh`

### Verify Installation

```bash
# Kiểm tra package status
dpkg -l | grep omniapi

# Kiểm tra libraries
ls -la /opt/omniapi/lib/

# Kiểm tra GStreamer plugins
ls -la /opt/omniapi/lib/gstreamer-1.0/

# Kiểm tra default fonts
ls -la /opt/omniapi/fonts/

# Kiểm tra default models
ls -la /opt/omniapi/models/

# Kiểm tra CVEDIX SDK
ls -la /opt/cvedix/lib/

# Test executable
/usr/local/bin/omniapi --help

# Kiểm tra service
sudo systemctl status omniapi

# Test API
curl http://localhost:8080/v1/core/health
```

## 🎬 Cài Đặt GStreamer Plugins

### Tại Sao Cần Cài Đặt GStreamer Plugins?

ALL-IN-ONE package đã bundle GStreamer libraries và plugins, nhưng trên một số hệ thống production, có thể thiếu một số plugins cần thiết cho việc xử lý video:

- **isomp4** (qtdemux): Để đọc file MP4
- **h264parse**: Để parse H.264 video stream
- **avdec_h264**: Để decode H.264 video
- **filesrc**: Để đọc file video
- **videoconvert**: Để convert video format
- **x264enc**: Để encode H.264 (cho RTMP output)
- **flvmux**: Để mux FLV (cho RTMP)
- **rtmpsink**: Để output RTMP stream

### Kiểm Tra Plugins Hiện Tại

Trước khi cài đặt, kiểm tra plugins đã có:

```bash
# Kiểm tra plugins trong bundled directory
export GST_PLUGIN_PATH=/opt/omniapi/lib/gstreamer-1.0
gst-inspect-1.0 isomp4
gst-inspect-1.0 h264parse
gst-inspect-1.0 avdec_h264
gst-inspect-1.0 filesrc
gst-inspect-1.0 videoconvert
gst-inspect-1.0 x264enc
gst-inspect-1.0 flvmux
gst-inspect-1.0 rtmpsink
```

Nếu các lệnh trên trả về "No such element", plugins chưa được cài đặt.

### Cài Đặt GStreamer Plugins

#### Ubuntu/Debian

```bash
# Cài đặt tất cả plugins cần thiết
sudo apt-get update
sudo apt-get install -y \
    gstreamer1.0-libav \
    gstreamer1.0-plugins-base \
    gstreamer1.0-plugins-good \
    gstreamer1.0-plugins-bad \
    gstreamer1.0-plugins-ugly

# Verify installation
gst-inspect-1.0 isomp4
gst-inspect-1.0 h264parse
gst-inspect-1.0 avdec_h264
```

#### Copy Plugins Vào Bundled Directory (Tùy Chọn)

Nếu muốn sử dụng plugins từ system thay vì bundled plugins:

```bash
# Tìm plugins trong system
find /usr/lib -name "libgstisomp4.so" 2>/dev/null
find /usr/lib -name "libgsth264parse.so" 2>/dev/null
find /usr/lib -name "libgstav.so" 2>/dev/null

# Copy plugins vào bundled directory (nếu cần)
sudo cp /usr/lib/x86_64-linux-gnu/gstreamer-1.0/libgstisomp4.so \
    /opt/omniapi/lib/gstreamer-1.0/
sudo cp /usr/lib/x86_64-linux-gnu/gstreamer-1.0/libgsth264parse.so \
    /opt/omniapi/lib/gstreamer-1.0/
sudo cp /usr/lib/x86_64-linux-gnu/gstreamer-1.0/libgstav.so \
    /opt/omniapi/lib/gstreamer-1.0/

# Update GStreamer registry
export GST_PLUGIN_PATH=/opt/omniapi/lib/gstreamer-1.0
gst-inspect-1.0 > /dev/null 2>&1
```

### Xác Nhận Cài Đặt

Sau khi cài đặt, kiểm tra lại:

```bash
# Kiểm tra plugins
export GST_PLUGIN_PATH=/opt/omniapi/lib/gstreamer-1.0
gst-inspect-1.0 isomp4 | head -5
gst-inspect-1.0 h264parse | head -5
gst-inspect-1.0 avdec_h264 | head -5

# Restart service để áp dụng thay đổi
sudo systemctl restart omniapi

# Kiểm tra logs để xác nhận không còn lỗi thiếu plugins
sudo journalctl -u omniapi -n 50 | grep -i "plugin\|gstreamer"
```

### Lỗi Thường Gặp

#### Lỗi: "Missing required plugins" khi start instance

Nếu gặp lỗi này khi start instance với file source:

```
[InstanceRegistry] ✗ Cannot start instance - required GStreamer plugins are missing
[InstanceRegistry] Missing plugins: isomp4, h264parse, avdec_h264
```

**Giải pháp:**

1. Cài đặt plugins (xem phần trên)
2. Kiểm tra `GST_PLUGIN_PATH` trong service file:
   ```bash
   cat /etc/systemd/system/omniapi.service | grep GST_PLUGIN_PATH
   ```
3. Đảm bảo `GST_PLUGIN_PATH` trỏ đến bundled directory:
   ```bash
   # Kiểm tra .env file
   cat /opt/omniapi/config/.env | grep GST_PLUGIN_PATH
   ```
4. Restart service:
   ```bash
   sudo systemctl restart omniapi
   ```

#### Lỗi: "GStreamer: pipeline have not been created"

Lỗi này thường xảy ra khi:
- Plugins không được tìm thấy
- GStreamer registry chưa được update

**Giải pháp:**

```bash
# Bước 1: Update GStreamer registry
export GST_PLUGIN_PATH=/opt/omniapi/lib/gstreamer-1.0
gst-inspect-1.0 > /dev/null 2>&1

# Bước 2: Kiểm tra plugins
gst-inspect-1.0 isomp4

# Bước 3: Restart service
sudo systemctl restart omniapi
```

#### Lỗi: "Internal data stream error" từ qtdemux

Lỗi này xảy ra khi:
- File video bị corrupted
- Plugins không đầy đủ
- GStreamer không thể parse file format

**Giải pháp:**

1. Kiểm tra file video:
   ```bash
   ffprobe /path/to/video.mp4
   ```

2. Kiểm tra plugins:
   ```bash
   export GST_PLUGIN_PATH=/opt/omniapi/lib/gstreamer-1.0
   gst-inspect-1.0 isomp4
   ```

3. Test với gst-launch:
   ```bash
   export GST_PLUGIN_PATH=/opt/omniapi/lib/gstreamer-1.0
   gst-launch-1.0 filesrc location=/path/to/video.mp4 ! \
       qtdemux ! h264parse ! avdec_h264 ! fakesink
   ```

### Tự Động Kiểm Tra Plugins

Service tự động kiểm tra plugins khi khởi động. Xem logs:

```bash
sudo journalctl -u omniapi | grep -i "gstreamer\|plugin"
```

Nếu thấy warning về missing plugins, cài đặt theo hướng dẫn trên.

### Khắc Phục Lỗi Thiếu Packages Trong Quá Trình Cài Đặt

ALL-IN-ONE package đã bundle tất cả dependencies, nhưng đôi khi vẫn có thể gặp lỗi với system libraries cơ bản:

#### Lỗi: "dpkg: dependency problems prevent configuration"

Nếu gặp lỗi này, có nghĩa là một số system libraries cơ bản chưa được cài đặt:

```bash
# Bước 1: Cài đặt các system libraries cơ bản còn thiếu
sudo apt-get update
sudo apt-get install -y \
    libc6 \
    libstdc++6 \
    libgcc-s1 \
    adduser \
    systemd

# Bước 2: Fix dependencies
sudo apt-get install -f

# Bước 3: Thử cài lại package
sudo dpkg -i omniapi-all-in-one-2026.0.1.22-amd64.deb
```

#### Lỗi: "dpkg: error processing package"

```bash
# Bước 1: Xem chi tiết lỗi
sudo dpkg --configure -a

# Bước 2: Nếu package bị broken, remove và cài lại
sudo dpkg --remove --force-remove-reinstreq omniapi
sudo dpkg -i omniapi-all-in-one-2026.0.1.22-amd64.deb

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
sudo dpkg -i omniapi-all-in-one-2026.0.1.22-amd64.deb
```

#### Lỗi: "Package is in a very bad inconsistent state"

```bash
# Bước 1: Remove package hoàn toàn
sudo dpkg --remove --force-remove-reinstreq omniapi
sudo apt-get purge omniapi

# Bước 2: Clean up
sudo apt-get autoremove
sudo apt-get autoclean

# Bước 3: Cài lại từ đầu
sudo dpkg -i omniapi-all-in-one-2026.0.1.22-amd64.deb
sudo apt-get install -f
```

## 🔧 Cách Hoạt Động

### 1. Bundle Libraries

Script tự động bundle tất cả libraries từ:

- **Build directory**: Libraries được build cùng project
- **CVEDIX SDK**: Từ extracted SDK .deb
- **System paths**: OpenCV, GStreamer, FFmpeg từ `/usr/lib`, `/usr/local/lib`
- **ldd output**: Tất cả dependencies từ executable

### 2. Bundle GStreamer Plugins

Script tự động tìm và bundle GStreamer plugins từ:

- `/usr/lib/x86_64-linux-gnu/gstreamer-1.0`
- `/usr/local/lib/gstreamer-1.0`
- `/usr/lib/gstreamer-1.0`

Plugins được copy vào `/opt/omniapi/lib/gstreamer-1.0/`

### 3. Bundle Default Data (Fonts and Models)

Nếu project có `cvedix_data/font` và `cvedix_data/models`, chúng sẽ được tự động bundle vào package:

- **Fonts**: `cvedix_data/font/*` → `/opt/omniapi/fonts/`
- **Models**: `cvedix_data/models/*` → `/opt/omniapi/models/`

Đây là default data cho users trên máy mới, không cần upload thủ công.

### 4. Minimal Dependencies

File `debian/control` chỉ yêu cầu:

```
Depends: libc6, libstdc++6, libgcc-s1, adduser, systemd
```

Tất cả libraries khác đều được bundle trong package.

### 5. RPATH Configuration

Executables được cấu hình với RPATH:

```
/opt/omniapi/lib:/opt/cvedix/lib
```

Đảm bảo tìm libraries từ bundled directory trước.

## 📊 Package Size

Package ALL-IN-ONE sẽ lớn hơn do bundle nhiều libraries:

- **build_deb_with_sdk.sh**: ~50-100 MB
- **build_deb_all_in_one.sh**: ~200-500 MB (tùy thuộc vào libraries được bundle)

## ⚠️ Lưu Ý

1. **Package size**: Package sẽ lớn hơn do bundle nhiều libraries
2. **Build time**: Build có thể lâu hơn do phải bundle nhiều libraries
3. **Disk space**: Cần đủ disk space để bundle libraries và data (ít nhất 5GB free)
4. **GStreamer plugins**: Plugins được bundle từ system, đảm bảo system có đầy đủ plugins
5. **⚠️ OpenCV version**: **BẮT BUỘC** phải có OpenCV 4.10 trên máy build. Package sẽ không hoạt động với các phiên bản OpenCV khác. Xem [Bước 8: Cài Đặt OpenCV 4.10](#bước-8-cài-đặt-opencv-410-bắt-buộc) để cài đặt đúng version.
6. **Default data**: Nếu `cvedix_data/font` và `cvedix_data/models` tồn tại trong project, chúng sẽ được tự động bundle vào package và cài đặt vào `/opt/omniapi/fonts/` và `/opt/omniapi/models/` làm default data
7. **Dependencies**: Tất cả dependencies trong phần [Yêu Cầu Hệ Thống](#-yêu-cầu-hệ-thống-prerequisites) phải được cài đặt đầy đủ trước khi build package

## 🔍 Troubleshooting

### Package quá lớn

Nếu package quá lớn, có thể:
- Kiểm tra xem có bundle duplicate libraries không
- Xem xét không bundle một số libraries không cần thiết
- Kiểm tra package size: `du -h omniapi-all-in-one-*.deb`

### Missing libraries

Nếu thiếu libraries sau khi cài đặt:

**Kiểm tra libraries:**
```bash
# Kiểm tra libraries trong package
ls -la /opt/omniapi/lib/

# Kiểm tra dependencies của executable
ldd /usr/local/bin/omniapi | grep "not found"

# Kiểm tra RPATH
readelf -d /usr/local/bin/omniapi | grep RPATH
```

**Nếu thiếu libraries:**
```bash
# Kiểm tra bundle_libs.sh có chạy đúng không (trong build log)
# Verify libraries trong /opt/omniapi/lib/
# Kiểm tra ldconfig
sudo ldconfig
sudo ldconfig -v | grep omniapi
```

### GStreamer plugins không hoạt động

> **Lưu ý:** Xem thêm [Cài Đặt GStreamer Plugins](#-cài-đặt-gstreamer-plugins) để biết cách cài đặt plugins đầy đủ.

**Kiểm tra GST_PLUGIN_PATH:**
```bash
cat /opt/omniapi/config/.env | grep GST_PLUGIN_PATH
```

**Kiểm tra plugins:**
```bash
ls -la /opt/omniapi/lib/gstreamer-1.0/
```

**Kiểm tra registry:**
```bash
export GST_PLUGIN_PATH=/opt/omniapi/lib/gstreamer-1.0
gst-inspect-1.0 filesrc
gst-inspect-1.0 appsink
gst-inspect-1.0 isomp4
gst-inspect-1.0 h264parse
```

**Nếu plugins không được tìm thấy:**

1. **Cài đặt plugins** (xem [Cài Đặt GStreamer Plugins](#-cài-đặt-gstreamer-plugins)):
   ```bash
   sudo apt-get update
   sudo apt-get install -y \
       gstreamer1.0-libav \
       gstreamer1.0-plugins-base \
       gstreamer1.0-plugins-good \
       gstreamer1.0-plugins-bad \
       gstreamer1.0-plugins-ugly
   ```

2. **Update GStreamer registry:**
   ```bash
   export GST_PLUGIN_PATH=/opt/omniapi/lib/gstreamer-1.0
   gst-inspect-1.0 > /dev/null 2>&1
   ```

3. **Restart service:**
   ```bash
   sudo systemctl restart omniapi
   ```

4. **Check logs:**
   ```bash
   sudo journalctl -u omniapi -n 50
   ```

**Lỗi: "cannot find appsink in manual pipeline" hoặc "gst_bin_iterate_elements: assertion failed"**

Đây là lỗi GStreamer registry chưa được update. Khắc phục:

```bash
# Bước 1: Đảm bảo GST_PLUGIN_PATH được set đúng
echo "GST_PLUGIN_PATH=/opt/omniapi/lib/gstreamer-1.0" | \
    sudo tee -a /opt/omniapi/config/.env

# Bước 2: Update registry
export GST_PLUGIN_PATH=/opt/omniapi/lib/gstreamer-1.0
gst-inspect-1.0 filesrc > /dev/null 2>&1

# Bước 3: Restart service
sudo systemctl restart omniapi

# Bước 4: Kiểm tra logs
sudo journalctl -u omniapi -f
```

**Lỗi: "Missing required plugins" khi start instance**

Nếu gặp lỗi này, xem chi tiết trong [Cài Đặt GStreamer Plugins - Lỗi Thường Gặp](#lỗi-thường-gặp).

### Service failed to start

**Kiểm tra log:**
```bash
sudo journalctl -u omniapi -n 100
sudo journalctl -u omniapi -f  # Follow logs
```

**Kiểm tra permissions:**
```bash
sudo chown -R edgeai:edgeai /opt/omniapi
sudo chmod -R 755 /opt/omniapi
```

**Kiểm tra executable:**
```bash
ls -la /usr/local/bin/omniapi
file /usr/local/bin/omniapi
```

**Kiểm tra libraries:**
```bash
ldd /usr/local/bin/omniapi | grep "not found"
```

### OpenCV không được tìm thấy

**⚠️ QUAN TRỌNG:** ALL-IN-ONE package đã bundle OpenCV 4.10 từ máy build. Nếu máy build không có OpenCV 4.10, package sẽ không hoạt động đúng.

**Kiểm tra OpenCV libraries:**
```bash
# Kiểm tra OpenCV libraries trong package
ls -la /opt/omniapi/lib/libopencv*.so*

# Kiểm tra OpenCV version (nếu có pkg-config)
ldconfig -p | grep opencv

# Kiểm tra OpenCV core library
find /opt/omniapi/lib -name "libopencv_core.so*"
```

**Nếu thiếu OpenCV hoặc không phải version 4.10:**

**Trên máy cài đặt (sau khi cài package):**

1. **Cài đặt OpenCV 4.10 tự động:**
   ```bash
   sudo /opt/omniapi/scripts/build_opencv_safe.sh
   ```
   Script này sẽ tự động:
   - Kiểm tra disk space và network connectivity
   - Download và build OpenCV 4.10 từ source
   - Cài đặt với freetype support
   - Verify installation sau khi hoàn thành

2. **Kiểm tra OpenCV version sau khi cài đặt:**
   ```bash
   pkg-config --modversion opencv4
   # Phải hiển thị 4.10.x
   ```

3. **Restart service sau khi cài OpenCV:**
   ```bash
   sudo systemctl restart omniapi
   ```

**Trên máy build (trước khi build package):**

1. **Kiểm tra máy build có OpenCV 4.10:**
   ```bash
   # Trên máy build, kiểm tra version
   pkg-config --modversion opencv4
   # Phải là 4.10.x
   ```

2. **Nếu thiếu, cài đặt OpenCV 4.10:**
   - Xem [Bước 8: Cài Đặt OpenCV 4.10 - Cách 1](#cách-1-build-từ-source-khuyến-nghị)
   - Rebuild package từ đầu sau khi cài OpenCV 4.10
   - Verify OpenCV libraries được bundle vào package

### CVEDIX SDK không được tìm thấy

**Kiểm tra CVEDIX SDK:**
```bash
ls -la /opt/cvedix/lib/
ldconfig -p | grep cvedix
```

**Nếu thiếu SDK libraries:**
```bash
# Kiểm tra xem SDK có được bundle từ .deb không
# Verify trong build log khi build package
```

### Libraries không được tìm thấy (ldd shows "not found")

**Kiểm tra RPATH:**
```bash
readelf -d /usr/local/bin/omniapi | grep RPATH
```

**Kiểm tra libraries:**
```bash
# Tìm libraries bị thiếu
ldd /usr/local/bin/omniapi | grep "not found"

# Kiểm tra xem libraries có trong bundled directory không
ls -la /opt/omniapi/lib/ | grep <missing-library-name>
```

**Nếu libraries bị thiếu:**
```bash
# Có thể cần rebuild package với đầy đủ dependencies
# Hoặc copy libraries từ system vào bundled directory
sudo cp /usr/lib/x86_64-linux-gnu/<missing-library> /opt/omniapi/lib/
sudo ldconfig
```

### Permission denied

**Kiểm tra user và group:**
```bash
id edgeai
groups edgeai
```

**Fix permissions:**
```bash
sudo chown -R edgeai:edgeai /opt/omniapi
sudo chmod -R 755 /opt/omniapi
sudo chmod 640 /opt/omniapi/config/.env
```

### Port already in use

**Kiểm tra port:**
```bash
sudo netstat -tlnp | grep 8080
sudo lsof -i :8080
```

**Nếu port đang được sử dụng:**
```bash
# Stop service
sudo systemctl stop omniapi

# Hoặc thay đổi port trong config
sudo nano /opt/omniapi/config/config.json
```

### Kiểm tra toàn bộ cài đặt

**Script validation:**
```bash
sudo /opt/omniapi/scripts/validate_installation.sh --verbose
```

Script này sẽ kiểm tra:
- ✅ Executable và libraries
- ✅ GStreamer plugins và GST_PLUGIN_PATH
- ✅ OpenCV installation
- ✅ CVEDIX SDK
- ✅ Service status
- ✅ Permissions

## 📚 Tài Liệu Liên Quan

- `BUILD_DEB.md` - Hướng dẫn build package thông thường
- `build_deb_with_sdk.sh` - Script build với SDK bundled (không all-in-one)
- `debian/control` - Package dependencies configuration
- `debian/bundle_libs.sh` - Script bundle libraries



