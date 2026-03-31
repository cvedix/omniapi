# Deploy Directory

Thư mục chứa các script và file cấu hình cho production deployment.

## Files Quan Trọng

### Scripts

- **`deploy.sh`** - Script chính để build và deploy production
  - Cài đặt dependencies
  - Build project
  - Tạo user và directories
  - Cài đặt executable và libraries
  - Cài đặt systemd service
  - Usage: `sudo ./deploy/deploy.sh [options]`

- **`../scripts/create_directories.sh`** - Script tạo thư mục từ `directories.conf`
  - Được dùng bởi `deploy.sh`, `debian/postinst`
  - Usage: `./scripts/create_directories.sh [INSTALL_DIR] [--full-permissions]`

### Configuration Files

- **`directories.conf`** - Định nghĩa tất cả thư mục cần tạo
  - Single source of truth cho directory structure
  - Format: `["directory_name"]="permissions"`

- **`omniapi.service`** - Systemd service file

## Quick Start

### Production Deployment

Có 2 cách để deploy production:

**Cách 1: Sử dụng script trong scripts/ (khuyến nghị)**
```bash
# Full deployment
sudo ./scripts/prod_setup.sh

# Skip dependencies (if already installed)
sudo ./scripts/prod_setup.sh --skip-deps

# Skip build (use existing build)
sudo ./scripts/prod_setup.sh --skip-build
```

**Cách 2: Sử dụng deploy script trực tiếp**
```bash
# Full deployment
sudo ./deploy/deploy.sh

# Skip dependencies (if already installed)
sudo ./deploy/deploy.sh --skip-deps

# Skip build (use existing build)
sudo ./deploy/deploy.sh --skip-build

# Full permissions (777) - development only
sudo ./deploy/deploy.sh --full-permissions
```

> **Lưu ý:** `scripts/prod_setup.sh` là symlink đến `deploy/deploy.sh`, cả hai đều giống nhau.

## Deploy Options

- `--skip-deps` - Skip installing system dependencies
- `--skip-build` - Skip building project
- `--skip-fixes` - Skip fixing libraries/uploads/watchdog
- `--no-start` - Don't auto-start service
- `--full-permissions` - Use 777 permissions (development)
- `--standard-permissions` - Use 755 permissions (production, default)

---

## Directory Management System

Hệ thống quản lý thư mục tập trung, đảm bảo đồng bộ giữa development và production.

### Tổng quan

Tất cả các thư mục được định nghĩa trong một file duy nhất: `deploy/directories.conf`

File này được sử dụng bởi:
- ✅ `deploy/deploy.sh` - Production deployment
- ✅ `debian/rules` - Debian package build
- ✅ `debian/postinst` - Debian package installation
- ✅ `deploy/create_directories.sh` - Helper script

### Cấu trúc `directories.conf`

File định nghĩa tất cả thư mục cần tạo:

```bash
declare -A APP_DIRECTORIES=(
    ["instances"]="750"      # Instance configurations
    ["solutions"]="750"      # Custom solutions
    ["groups"]="750"         # Group configurations
    ["nodes"]="750"          # Pre-configured nodes
    ["models"]="750"         # Uploaded model files
    ["videos"]="750"        # Uploaded video files
    ["logs"]="750"          # Application logs
    ["data"]="750"          # Application data
    ["config"]="750"        # Configuration files
    ["fonts"]="750"         # Font files
    ["uploads"]="755"       # Uploaded files (public read)
    ["lib"]="755"           # Bundled libraries
)
```

**Format:** `["directory_name"]="permissions"`

**Permissions:**
- `750` = Restricted (chỉ user/group có quyền truy cập)
- `755` = Public read (mọi người có thể đọc, chỉ user/group có thể ghi)

### Script `scripts/create_directories.sh`

Standalone script để tạo thư mục từ `directories.conf`:

**Usage:**
```bash
./scripts/create_directories.sh [INSTALL_DIR] [--full-permissions]
```

**Options:**
- `INSTALL_DIR` - Thư mục cài đặt (default: `/opt/omniapi`)
- `--full-permissions` - Set quyền 755 cho tất cả thư mục (thay vì permissions trong config)

**Examples:**
```bash
# Tạo thư mục với permissions từ config
./scripts/create_directories.sh

# Tạo thư mục tại custom location
./scripts/create_directories.sh /opt/omniapi

# Tạo thư mục với full permissions (755)
./scripts/create_directories.sh /opt/omniapi --full-permissions
```

### Cách sử dụng

#### Thêm thư mục mới

1. Mở `deploy/directories.conf`
2. Thêm dòng mới:
   ```bash
   ["new_directory"]="750"
   ```
3. Chạy script để tạo thư mục mới:
   ```bash
   ./scripts/create_directories.sh
   ```

### Lợi ích

✅ **Single Source of Truth** - Chỉ cần sửa một file
✅ **Đồng bộ tự động** - Dev và production luôn giống nhau
✅ **Dễ bảo trì** - Không cần sửa nhiều file
✅ **Tránh lỗi** - Không còn thư mục lạ do typo
✅ **Consistent permissions** - Quyền được quản lý tập trung

### Migration

Nếu bạn đang có script cũ với danh sách thư mục hardcoded:

**Trước:**
```bash
mkdir -p "$INSTALL_DIR"/instances
mkdir -p "$INSTALL_DIR"/solutions
# ... nhiều dòng khác
```

**Sau:**
```bash
./scripts/create_directories.sh "$INSTALL_DIR"
# hoặc với full permissions
./scripts/create_directories.sh "$INSTALL_DIR" --full-permissions
```

### Troubleshooting

#### Script không tìm thấy directories.conf

Script sẽ tự động tìm file theo thứ tự:
1. `$PROJECT_ROOT/deploy/directories.conf`
2. `deploy/directories.conf` (relative to current dir)
3. Fallback to default directories

#### Permissions không đúng

Kiểm tra:
1. File `directories.conf` có format đúng không?
2. Script có quyền chạy không? (có thể cần `sudo`)
3. User/group có tồn tại không?
