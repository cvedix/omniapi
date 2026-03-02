# Hướng Dẫn Cài Đặt CVEDIX SDK

Tài liệu này hướng dẫn cách cài đặt CVEDIX SDK (Core AI Runtime / EdgeOS SDK) để sử dụng với edgeos-api.

## 📋 Tổng Quan

CVEDIX SDK là framework C++ để phân tích và xử lý video, được đóng gói dưới dạng SDK để tích hợp vào các dự án.

**Repository**: `cvedix/core_ai_runtime` (private repository)

## 🚀 Các Cách Cài Đặt

### Cách 1: Sử dụng build_sdk.sh (Khuyến nghị)

Nếu bạn có quyền truy cập vào repository `core_ai_runtime`:

```bash
# Clone repository
git clone https://github.com/cvedix/core_ai_runtime.git
cd core_ai_runtime

# Build và cài đặt SDK với cấu hình mặc định
./build_sdk.sh

# Hoặc chỉ định thư mục cài đặt
./build_sdk.sh --prefix=/opt/cvedix

# Build với các tính năng tùy chọn
./build_sdk.sh --with-cuda --with-trt --build-type=Release

# Xem tất cả các tùy chọn
./build_sdk.sh --help
```

Sau khi build, SDK sẽ được cài đặt tại:
- Mặc định: `./output`
- Hoặc thư mục bạn chỉ định với `--prefix`

### Cách 2: Sử dụng CMake trực tiếp

```bash
cd core_ai_runtime

mkdir -p build_sdk
cd build_sdk

cmake .. \
    -DCMAKE_INSTALL_PREFIX=/opt/cvedix \
    -DCMAKE_BUILD_TYPE=Release \
    -DCVEDIX_WITH_CUDA=OFF \
    -DCVEDIX_WITH_TRT=OFF

make -j$(nproc)
sudo make install
```

### Cách 3: Cài đặt từ Binary Package (nếu có)

Nếu bạn có file `.deb` hoặc binary package:

```bash
# Debian/Ubuntu
sudo dpkg -i cvedix-sdk_*.deb
sudo apt-get install -f  # Fix dependencies if needed

# Hoặc extract và copy thủ công
tar -xzf cvedix-sdk-*.tar.gz
sudo cp -r output/* /opt/cvedix/
```

## 📁 Cấu Trúc SDK Sau Khi Cài Đặt

Sau khi cài đặt, SDK có cấu trúc như sau:

```
/opt/cvedix/  (hoặc thư mục bạn chỉ định)
├── include/
│   └── cvedix/              # Header files
│       ├── nodes/          # Node headers
│       ├── objects/        # Object definitions
│       ├── utils/          # Utility headers
│       └── cvedix_version.h
├── lib/
│   ├── libcvedix_instance_sdk.so    # Shared library
│   ├── cmake/
│   │   └── cvedix/         # CMake config files
│   │       ├── cvedix-config.cmake
│   │       ├── cvedix-config-version.cmake
│   │       └── cvedix-targets.cmake
│   └── pkgconfig/
│       └── cvedix.pc      # pkg-config file
└── share/
    └── cvedix/
        └── sdk_info.txt   # SDK information
```

## ⚙️ Cấu Hình Sau Khi Cài Đặt

### 1. Cập nhật Library Path

```bash
# Thêm vào /etc/ld.so.conf.d/cvedix.conf
echo "/opt/cvedix/lib" | sudo tee /etc/ld.so.conf.d/cvedix.conf

# Hoặc export LD_LIBRARY_PATH
export LD_LIBRARY_PATH=/opt/cvedix/lib:$LD_LIBRARY_PATH

# Cập nhật library cache
sudo ldconfig
```

### 2. Tạo Symlinks (nếu cần)

edgeos-api có thể cần symlinks cho một số dependencies:

```bash
# Fix symlinks cho CVEDIX SDK
sudo ./scripts/dev_setup.sh --skip-deps --skip-build

# Hoặc thủ công:
sudo ln -sf /opt/cvedix/lib/libtinyexpr.so /usr/lib/libtinyexpr.so
sudo ln -sf /opt/cvedix/lib/libcvedix_instance_sdk.so /usr/lib/libcvedix_instance_sdk.so
```

### 3. Cấu hình CMake cho edgeos-api

edgeos-api sẽ tự động tìm EdgeOS SDK tại:
- `/opt/edgeos-sdk/` (ưu tiên, cấu trúc: lib/cvedix, lib/opencv, lib/cuda, lib/cudnn, lib/tensorrt)
- `/opt/cvedix/` hoặc `/usr/include/cvedix/` và `/usr/lib/`

Nếu SDK ở vị trí khác, thêm vào `CMAKE_PREFIX_PATH`:

```bash
cd edgeos-api/build
cmake .. -DCMAKE_PREFIX_PATH=/path/to/edgeos-sdk
```

## ✅ Kiểm Tra Cài Đặt

### Kiểm tra CMake config

```bash
cmake --find-package -DNAME=cvedix -DCOMPILER_ID=GNU -DLANGUAGE=CXX -DMODE=EXIST
```

### Kiểm tra pkg-config

```bash
# Nếu SDK ở /opt/cvedix
export PKG_CONFIG_PATH=/opt/cvedix/lib/pkgconfig:$PKG_CONFIG_PATH

pkg-config --modversion cvedix
pkg-config --cflags cvedix
pkg-config --libs cvedix
```

### Kiểm tra thư viện

```bash
# Kiểm tra library có trong system
ldconfig -p | grep cvedix

# Kiểm tra file tồn tại
ls -la /opt/cvedix/lib/libcvedix_instance_sdk.so
ls -la /opt/cvedix/include/cvedix/
```

### Kiểm tra từ edgeos-api

```bash
cd edgeos-api/build
cmake ..  # Sẽ hiển thị thông tin về EdgeOS SDK nếu tìm thấy
```

## 📦 Yêu Cầu Hệ Thống

### Phụ thuộc bắt buộc

- **C++17** compiler (GCC >= 7.5)
- **OpenCV >= 4.6**
- **GStreamer >= 1.14.5**

### Phụ thuộc tùy chọn

- **CUDA** (nếu build với `--with-cuda`)
- **TensorRT** (nếu build với `--with-trt`)
- **PaddlePaddle** (nếu build với `--with-paddle`)
- **Kafka** (nếu build với `--with-kafka`)
- **OpenSSL** (nếu build với `--with-llm`)
- **FFmpeg** (nếu build với `--with-ffmpeg`)
- **RKNN** (nếu build với `--with-rknn`)
- **RGA** (nếu build với `--with-rga`)

### Cài đặt dependencies cơ bản

```bash
# Ubuntu/Debian
sudo apt-get update
sudo apt-get install -y \
    build-essential \
    cmake \
    git \
    libopencv-dev \
    libgstreamer1.0-dev \
    libgstreamer-plugins-base1.0-dev \
    pkg-config
```

## 🔧 Sử Dụng SDK trong edgeos-api

edgeos-api đã được cấu hình để dùng lib có sẵn trong EdgeOS SDK (`/opt/edgeos-sdk/lib/cvedix`, `lib/opencv`, `lib/cuda`, `lib/cudnn`, `lib/tensorrt`). Sau khi cài đặt SDK:

1. **Chạy dev_setup.sh** để fix symlinks (nếu cần):
   ```bash
   sudo ./scripts/dev_setup.sh --skip-deps --skip-build
   ```

2. **Build edgeos-api**:
   ```bash
   ./scripts/dev_setup.sh --skip-deps
   # Hoặc
   mkdir build && cd build
   cmake ..
   make -j$(nproc)
   ```

3. **Kiểm tra build thành công**:
   ```bash
   ./build/bin/edgeos-api --version
   ```

## ⚠️ Troubleshooting

### Lỗi: "Could not find cvedix"

**Nguyên nhân**: CMake không tìm thấy SDK

**Giải pháp**:
```bash
# Kiểm tra SDK đã cài đặt
ls -la /opt/cvedix/lib/cmake/cvedix/

# Thêm vào CMAKE_PREFIX_PATH
cmake .. -DCMAKE_PREFIX_PATH=/opt/cvedix

# Hoặc set environment variable
export CMAKE_PREFIX_PATH=/opt/cvedix:$CMAKE_PREFIX_PATH
```

### Lỗi: "undefined reference to cvedix::..."

**Nguyên nhân**: Chưa link với SDK library

**Giải pháp**:
- Đảm bảo CMakeLists.txt có `find_package(cvedix REQUIRED)`
- Kiểm tra `target_link_libraries` có `cvedix::cvedix_instance_sdk`

### Lỗi runtime: "cannot open shared object file"

**Nguyên nhân**: System không tìm thấy shared library

**Giải pháp**:
```bash
# Thêm vào ld.so.conf
echo "/opt/cvedix/lib" | sudo tee /etc/ld.so.conf.d/cvedix.conf
sudo ldconfig

# Hoặc export LD_LIBRARY_PATH
export LD_LIBRARY_PATH=/opt/cvedix/lib:$LD_LIBRARY_PATH
```

### Lỗi: "libtinyexpr.so not found"

**Nguyên nhân**: Thiếu symlink cho tinyexpr

**Giải pháp**:
```bash
sudo ln -sf /opt/cvedix/lib/libtinyexpr.so /usr/lib/libtinyexpr.so
sudo ldconfig

# Hoặc chạy dev_setup script
sudo ./scripts/dev_setup.sh --skip-deps --skip-build
```

### Lỗi: "cpp-base64 not found"

**Nguyên nhân**: Thiếu symlink cho cpp-base64

**Giải pháp**:
```bash
sudo mkdir -p /opt/cvedix/include/cvedix/third_party/cpp_base64
sudo ln -sf /path/to/cpp-base64/base64.h \
    /opt/cvedix/include/cvedix/third_party/cpp_base64/base64.h

# Hoặc chạy dev_setup script
sudo ./scripts/dev_setup.sh --skip-deps --skip-build
```

## ⚠️ Known Issues (Báo cáo cho Team SDK)

### SSE Broker – ASIO dependency issue

**Mô tả**: Node **SSE broker** (`cvedix_sse_broker_node`) bị tắt tạm thời khi tích hợp edgeos-api với CVEDIX SDK do lỗi liên quan tới dependency **ASIO**.

**Thời điểm**: Từ khoảng **2026-02-03** (commit `9d01991` – *Fix build errors: comment ASIO/cereal dependencies temporarily*).

**Vị trí trong edgeos-api**:
- `src/core/pipeline_builder_broker_nodes.cpp`: `#include <cvedix/nodes/broker/cvedix_sse_broker_node.h>` bị comment; `createSSEBrokerNode()` throw thay vì tạo node.
- `src/core/pipeline_builder.cpp`: với `nodeType == "sse_broker"` trả về `nullptr` và in cảnh báo.

**Thông tin cần Team SDK hỗ trợ**:
1. **Nội dung lỗi build gốc** không còn lưu trong repo (chỉ có workaround). Để lấy lại: bật lại include và implementation SSE broker trong edgeos-api rồi build với cùng bộ SDK/compiler.
2. **Nguyên nhân khả dĩ**: conflict ASIO (standalone hoặc Boost.Asio) giữa SDK và Drogon/Trantor (HTTP server dùng Boost.Asio). Cần xác nhận phiên bản ASIO SDK dùng và cách export/include.
3. **Đường dẫn ASIO hiện tại**: edgeos-api đã thêm  
   `/opt/edgeos-sdk/include/cvedix/third_party/asio/include`  
   (xem `CMakeLists.txt`).

**Để reproduce**: Uncomment `#include <cvedix/nodes/broker/cvedix_sse_broker_node.h>` và phần code tạo `cvedix_sse_broker_node` trong `pipeline_builder_broker_nodes.cpp`, sau đó build. Gửi lại full compiler/linker error log cho Team SDK.

Chi tiết đầy đủ để report: xem `docs/SDK_ISSUE_REPORT_ASIO_SSE_BROKER.md`.

## 📚 Tài Liệu Thêm

- **SDK Documentation**: Xem `doc/README_SDK.md` trong repository `core_ai_runtime`
- **SDK Integration**: Xem `doc/pages/sdk_integration.md`
- **edgeos-api Development**: Xem `docs/DEVELOPMENT.md`
- **edgeos-api Setup**: Xem `docs/SCRIPTS.md`

## 🔗 Liên Kết

- Repository SDK: `https://github.com/cvedix/core_ai_runtime` (private)
- Repository edgeos-api: `https://github.com/cvedix/edgeos-api`

---

**Lưu ý**: CVEDIX SDK là private repository. Bạn cần quyền truy cập để clone và build.


