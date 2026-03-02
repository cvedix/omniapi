# Hướng Dẫn Cài Đặt edgeos-api

Hướng dẫn chi tiết về cách build và cài đặt edgeos-api từ Debian package (.deb).

## 📦 Build và Cài Đặt Debian Package

### Build File .deb

Có 2 loại package có thể build:

**1. ALL-IN-ONE Package (Khuyến nghị - Ưu tiên):**
```bash
# Build ALL-IN-ONE package (tự chứa TẤT CẢ dependencies)
./packaging/scripts/build_deb_all_in_one.sh --sdk-deb <path-to-sdk.deb>

# Ví dụ:
./packaging/scripts/build_deb_all_in_one.sh \
    --sdk-deb ../edgeos-sdk-2025.0.1.3-x86_64.deb

# Với các tùy chọn
./packaging/scripts/build_deb_all_in_one.sh --sdk-deb <path> --clean
./packaging/scripts/build_deb_all_in_one.sh --sdk-deb <path> --no-build
```

**2. Package thông thường:**
```bash
# Build package
./packaging/scripts/build_deb.sh

# Với các tùy chọn
./packaging/scripts/build_deb.sh --clean          # Clean build trước khi build
./packaging/scripts/build_deb.sh --no-build       # Chỉ tạo package từ build có sẵn
./packaging/scripts/build_deb.sh --version 1.0.0  # Set version tùy chỉnh
./packaging/scripts/build_deb.sh --help           # Xem tất cả options
```

**Lưu ý:** Không cần `sudo` để build! Chỉ cần sudo khi **cài đặt** package.

### Yêu Cầu Build Dependencies

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

**Giải thích:**
- `build-essential`, `cmake`, `git`, `pkg-config`: Công cụ build cơ bản
- `debhelper`, `dpkg-dev`, `fakeroot`: Công cụ để tạo Debian package
- Các thư viện `lib*-dev`: Header files và libraries cần thiết để compile project
  - `libfreetype6-dev`, `libharfbuzz-dev`: Font rendering libraries
  - `libjpeg-dev`, `libpng-dev`, `libtiff-dev`: Image format libraries
  - `libavcodec-dev`, `libavformat-dev`, `libswscale-dev`: FFmpeg libraries cho video processing
  - `libgtk-3-dev`: GTK+ development libraries
- GStreamer plugins: Cần thiết cho ALL-IN-ONE package để bundle plugins
- `ffmpeg`: Cần thiết cho việc xử lý video và audio

**Sau khi build:**
- Package thông thường: `edgeos-api-{VERSION}-amd64.deb`
- ALL-IN-ONE package: `edgeos-api-all-in-one-{VERSION}-amd64.deb`

## 📥 Cài Đặt Package

**⚠️ Khuyến nghị: Sử dụng ALL-IN-ONE package** - Tự chứa tất cả dependencies, không cần cài thêm packages.

**⚠️ QUAN TRỌNG - Đọc Trước Khi Cài Đặt:**

Trước khi cài đặt package `.deb`, bạn **BẮT BUỘC** phải:
1. ✅ Cập nhật package list (`sudo apt-get update`)
2. ✅ Cài đặt system libraries cơ bản (libc6, libstdc++6, libgcc-s1, adduser, systemd)
3. ✅ Cài đặt dependencies cho OpenCV (nếu package chưa bundle OpenCV)
4. ✅ Cài đặt FFmpeg (`sudo apt install -y ffmpeg`)
5. ✅ (Tùy chọn) Cài đặt GStreamer plugins

**Lý do:** Trong quá trình cài đặt package (`dpkg -i`), hệ thống không cho phép cài đặt thêm packages khác vì dpkg đang giữ lock. Nếu không chuẩn bị dependencies trước, quá trình cài đặt sẽ thất bại hoặc OpenCV không thể cài đặt tự động.

### Cài Đặt ALL-IN-ONE Package (Khuyến nghị - Ưu tiên)

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
    systemd \
    patchelf
```

**Bước 3: Cài Đặt Dependencies Cho OpenCV và FFmpeg (Nếu Package Chưa Bundle OpenCV)**

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
    python3-numpy \
    ffmpeg
```

**Bước 4: Cài Đặt FFmpeg (BẮT BUỘC)**

FFmpeg cần thiết cho việc xử lý video và audio:

```bash
sudo apt install -y ffmpeg
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
# OpenCV 4.10 with freetype support is required for edgeos-api.
# The installation process will take approximately 30-60 minutes.
#
# Checking disk space...
#   ✓ Sufficient disk space available (27 GB)
# Checking network connectivity...
#   ✓ Network connectivity OK
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

# Bước 6: Xem log
sudo journalctl -u edgeos-api -f

# Bước 7: Test API
curl http://localhost:8080/v1/core/health
```

**Lưu ý về OpenCV:**
- ⚠️ **OpenCV 4.10 là BẮT BUỘC** để package hoạt động đúng. Package sẽ không hoạt động với các phiên bản OpenCV khác.
- Nếu package đã bundle OpenCV 4.10, quá trình cài đặt sẽ không yêu cầu cài thêm.
- Nếu thiếu OpenCV 4.10, quá trình cài đặt sẽ tự động phát hiện và cho phép cài đặt tự động (yêu cầu dependencies đã được cài ở Bước 3).
- Nếu cài đặt OpenCV bị lỗi hoặc bị gián đoạn, chạy lại: `sudo /opt/edgeos-api/scripts/build_opencv_safe.sh`

**Verify Installation:**

```bash
# Kiểm tra package status
dpkg -l | grep edgeos-api

# Kiểm tra libraries
ls -la /opt/edgeos-api/lib/

# Kiểm tra GStreamer plugins
ls -la /opt/edgeos-api/lib/gstreamer-1.0/

# Kiểm tra default fonts
ls -la /opt/edgeos-api/fonts/

# Kiểm tra default models
ls -la /opt/edgeos-api/models/

# Kiểm tra CVEDIX SDK
ls -la /opt/cvedix/lib/

# Test executable
/usr/local/bin/edgeos-api --help

# Kiểm tra service
sudo systemctl status edgeos-api

# Test API
curl http://localhost:8080/v1/core/health
```

### Cài Đặt Package Thông Thường

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

**Các bước cài đặt:**

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

# 2. Cài đặt package
sudo dpkg -i edgeos-api-*.deb

# 3. Nếu có lỗi dependencies, fix với:
sudo apt-get install -f

# 4. Khởi động service
sudo systemctl start edgeos-api
sudo systemctl enable edgeos-api  # Tự động chạy khi khởi động

# 5. Kiểm tra service
sudo systemctl status edgeos-api

# 6. Xem log
sudo journalctl -u edgeos-api -f

# 7. Test API
curl http://localhost:8080/v1/core/health
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

### Cài Đặt GStreamer Plugins (Nếu Cần)

ALL-IN-ONE package đã bundle GStreamer libraries và plugins, nhưng trên một số hệ thống production có thể thiếu một số plugins cần thiết cho việc xử lý video:

- **isomp4** (qtdemux): Để đọc file MP4
- **h264parse**: Để parse H.264 video stream
- **avdec_h264**: Để decode H.264 video
- **filesrc**: Để đọc file video
- **videoconvert**: Để convert video format
- **x264enc**: Để encode H.264 (cho RTMP output)
- **flvmux**: Để mux FLV (cho RTMP)
- **rtmpsink**: Để output RTMP stream

**Kiểm tra plugins hiện tại:**

```bash
# Kiểm tra plugins trong bundled directory
export GST_PLUGIN_PATH=/opt/edgeos-api/lib/gstreamer-1.0
gst-inspect-1.0 isomp4
gst-inspect-1.0 h264parse
gst-inspect-1.0 avdec_h264
gst-inspect-1.0 filesrc
gst-inspect-1.0 videoconvert
```

Nếu các lệnh trên trả về "No such element", plugins chưa được cài đặt.

**Cài đặt GStreamer plugins:**

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
export GST_PLUGIN_PATH=/opt/edgeos-api/lib/gstreamer-1.0
gst-inspect-1.0 isomp4 | head -5
gst-inspect-1.0 h264parse | head -5
gst-inspect-1.0 avdec_h264 | head -5

# Restart service để áp dụng thay đổi
sudo systemctl restart edgeos-api

# Kiểm tra logs để xác nhận không còn lỗi thiếu plugins
sudo journalctl -u edgeos-api -n 50 | grep -i "plugin\|gstreamer"
```

**Quản lý service:**
```bash
sudo systemctl start edgeos-api      # Khởi động
sudo systemctl stop edgeos-api       # Dừng
sudo systemctl restart edgeos-api    # Khởi động lại
sudo systemctl status edgeos-api     # Kiểm tra trạng thái
```

**Cấu trúc sau khi cài đặt:**
- **Executable**: `/usr/local/bin/edgeos-api`
- **Libraries**: `/opt/edgeos-api/lib/` (bundled - tự chứa)
- **GStreamer plugins**: `/opt/edgeos-api/lib/gstreamer-1.0/`
- **Config**: `/opt/edgeos-api/config/`
- **Data**: `/opt/edgeos-api/` (instances, solutions, models, logs, etc.)
- **Fonts**: `/opt/edgeos-api/fonts/` (default fonts)
- **Models**: `/opt/edgeos-api/models/` (default models)
- **Service**: `/etc/systemd/system/edgeos-api.service`

## 🔧 Khắc Phục Lỗi Thường Gặp Khi Cài Đặt

**⚠️ Lưu ý:** Hầu hết các lỗi này có thể tránh được nếu bạn đã chuẩn bị đầy đủ dependencies ở phần Prerequisites trên.

### Lỗi: "dpkg: dependency problems prevent configuration"

Lỗi này xảy ra khi thiếu system libraries cơ bản. **Giải pháp:**

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
sudo dpkg -i edgeos-api-all-in-one-*.deb
```

### Lỗi: "dpkg: error processing package"

```bash
# Bước 1: Xem chi tiết lỗi
sudo dpkg --configure -a

# Bước 2: Nếu package bị broken, remove và cài lại
sudo dpkg --remove --force-remove-reinstreq edgeos-api
sudo dpkg -i edgeos-api-all-in-one-*.deb

# Bước 3: Fix dependencies
sudo apt-get install -f
```

### Lỗi: "E: Sub-process /usr/bin/dpkg returned an error code"

```bash
# Bước 1: Unlock dpkg nếu bị lock
sudo rm /var/lib/dpkg/lock-frontend
sudo rm /var/lib/dpkg/lock
sudo rm /var/cache/apt/archives/lock

# Bước 2: Reconfigure dpkg
sudo dpkg --configure -a

# Bước 3: Cài lại package
sudo dpkg -i edgeos-api-all-in-one-*.deb
```

### Lỗi: "Package is in a very bad inconsistent state"

```bash
# Bước 1: Remove package hoàn toàn
sudo dpkg --remove --force-remove-reinstreq edgeos-api
sudo apt-get purge edgeos-api

# Bước 2: Clean up
sudo apt-get autoremove
sudo apt-get autoclean

# Bước 3: Cài lại từ đầu (nhớ chuẩn bị dependencies trước!)
sudo dpkg -i edgeos-api-all-in-one-*.deb
sudo apt-get install -f
```

### Lỗi: "Missing required plugins" khi start instance

```bash
# Cài đặt GStreamer plugins (xem phần trên)
# Kiểm tra GST_PLUGIN_PATH trong service file
cat /etc/systemd/system/edgeos-api.service | grep GST_PLUGIN_PATH

# Đảm bảo GST_PLUGIN_PATH trỏ đến bundled directory
cat /opt/edgeos-api/config/.env | grep GST_PLUGIN_PATH

# Restart service
sudo systemctl restart edgeos-api
```

### Lỗi: "GStreamer: pipeline have not been created"

```bash
# Update GStreamer registry
export GST_PLUGIN_PATH=/opt/edgeos-api/lib/gstreamer-1.0
gst-inspect-1.0 > /dev/null 2>&1

# Kiểm tra plugins
gst-inspect-1.0 isomp4

# Restart service
sudo systemctl restart edgeos-api
```

### Lỗi: Service failed to start

```bash
# Kiểm tra log
sudo journalctl -u edgeos-api -n 100

# Kiểm tra permissions
sudo chown -R edgeai:edgeai /opt/edgeos-api
sudo chmod -R 755 /opt/edgeos-api

# Kiểm tra executable
ls -la /usr/local/bin/edgeos-api
file /usr/local/bin/edgeos-api

# Kiểm tra libraries
ldd /usr/local/bin/edgeos-api | grep "not found"
```

### Kiểm tra toàn bộ cài đặt

```bash
# Script validation (nếu có)
sudo /opt/edgeos-api/scripts/validate_installation.sh --verbose
```

### Lỗi: RPATH chứa đường dẫn build

Nếu script validation báo cảnh báo về RPATH chứa đường dẫn build (ví dụ: `/home/.../build/lib`), bạn cần sửa RPATH:

```bash
# Cài đặt patchelf nếu chưa có
sudo apt-get install -y patchelf

# Chạy script tự động fix RPATH
sudo /opt/edgeos-api/scripts/fix_rpath.sh

# Hoặc fix thủ công
sudo patchelf --set-rpath "/opt/edgeos-api/lib:/opt/cvedix/lib" /usr/local/bin/edgeos-api
sudo patchelf --set-rpath "/opt/edgeos-api/lib:/opt/cvedix/lib" /usr/local/bin/edgeos-worker

# Verify RPATH đã được fix
patchelf --print-rpath /usr/local/bin/edgeos-api
# Kết quả mong đợi: /opt/edgeos-api/lib:/opt/cvedix/lib

# Restart service sau khi fix
sudo systemctl restart edgeos-api
```

**Nguyên nhân:** Executable được build với RPATH trỏ đến thư mục build thay vì thư mục production. Script `fix_rpath.sh` sẽ tự động sửa lỗi này.

## 📚 Tài Liệu Liên Quan

- [packaging/docs/BUILD_DEB.md](../packaging/docs/BUILD_DEB.md) - Build package thông thường
- [packaging/docs/BUILD_ALL_IN_ONE.md](../packaging/docs/BUILD_ALL_IN_ONE.md) - Build ALL-IN-ONE package (chi tiết đầy đủ)

