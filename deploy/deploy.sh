#!/bin/bash
# ============================================
# Edge AI API - Complete Build & Deploy Script
# ============================================
#
# Script này tổng hợp tất cả các bước:
# 1. Cài đặt dependencies hệ thống
# 2. Build project
# 3. Deploy production (user, directories, executable, libraries)
# 4. Fix uploads directory permissions
# 5. Fix watchdog configuration
# 6. Cài đặt và khởi động systemd service
#
# Usage:
#   sudo ./deploy/deploy.sh [options]
#   hoặc từ thư mục deploy:
#   sudo ./deploy.sh [options]
#
# Options:
#   --all, --full-setup  Setup everything (deps, build, fixes, service, face database) - mặc định
#   --skip-deps      Skip cài đặt dependencies
#   --skip-build     Skip build (dùng build có sẵn)
#   --skip-fixes     Skip các bước fix (libraries, uploads, watchdog)
#   --no-start       Không tự động start service sau khi deploy
#   --full-permissions  Cấp quyền 777 (drwxrwxrwx) - như cvedix-rt
#   --standard-permissions  Cấp quyền 755 (drwxr-xr-x) - như Tabby, google, nvidia (mặc định)
#
# ============================================

set -e  # Exit on error

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Get script directory (script is in deploy/)
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

# Configuration
SERVICE_USER="edgeai"
SERVICE_GROUP="edgeai"
INSTALL_DIR="/opt/omniapi"
BIN_DIR="/usr/local/bin"
LIB_DIR="/usr/local/lib"
SERVICE_NAME="omniapi"
SERVICE_FILE="${SERVICE_NAME}.service"

# Load directory configuration
# Source the configuration file if it exists, otherwise use defaults
DIRS_CONF="$SCRIPT_DIR/directories.conf"
if [ -f "$DIRS_CONF" ]; then
    source "$DIRS_CONF"
else
    # Fallback: Default directory configuration
    declare -A APP_DIRECTORIES=(
        ["instances"]="750"      # Instance configurations (instances.json)
        ["solutions"]="750"     # Custom solutions
        ["groups"]="750"        # Group configurations
        ["models"]="750"        # Uploaded model files
        ["logs"]="750"          # Application logs
        ["data"]="750"          # Application data
        ["config"]="750"        # Configuration files
        ["uploads"]="755"       # Uploaded files (may need public access)
    )
fi

# Flags
SKIP_DEPS=false
SKIP_BUILD=false
SKIP_FIXES=false
NO_START=false
PERMISSION_MODE="standard"  # "standard" (755) or "full" (777)
FULL_SETUP=false

# Parse arguments
while [[ $# -gt 0 ]]; do
    case $1 in
        --skip-deps)
            SKIP_DEPS=true
            shift
            ;;
        --skip-build)
            SKIP_BUILD=true
            shift
            ;;
        --skip-fixes)
            SKIP_FIXES=true
            shift
            ;;
        --no-start)
            NO_START=true
            shift
            ;;
        --full-permissions|--full)
            PERMISSION_MODE="full"
            shift
            ;;
        --standard-permissions|--standard)
            PERMISSION_MODE="standard"
            shift
            ;;
        --all|--full-setup)
            FULL_SETUP=true
            # Full setup means: do everything, don't skip anything
            SKIP_DEPS=false
            SKIP_BUILD=false
            SKIP_FIXES=false
            NO_START=false
            shift
            ;;
        *)
            echo -e "${RED}Unknown option: $1${NC}"
            echo "Usage: sudo ./deploy.sh [--all|--full-setup] [--skip-deps] [--skip-build] [--skip-fixes] [--no-start] [--full-permissions|--standard-permissions]"
            exit 1
            ;;
    esac
done

echo "=========================================="
echo "Edge AI API - Complete Build & Deploy"
echo "=========================================="
echo ""

# If --all flag is set, show message
if [ "$FULL_SETUP" = true ]; then
    echo -e "${BLUE}Full setup mode: Setup everything (deps, build, fixes, service, face database)${NC}"
    echo ""
fi

echo "Options:"
echo "  Skip dependencies: $SKIP_DEPS"
echo "  Skip build:        $SKIP_BUILD"
echo "  Skip fixes:        $SKIP_FIXES"
echo "  No auto-start:     $NO_START"
if [ "$FULL_SETUP" = true ]; then
    echo "  Full setup:        ✅ Enabled"
fi
echo ""

# Check if running as root
if [ "$EUID" -ne 0 ]; then
    echo -e "${RED}Error: Script này cần chạy với quyền sudo${NC}"
    echo "Usage: sudo ./deploy.sh"
    exit 1
fi

# ============================================
# Step 1: Install System Dependencies
# ============================================
if [ "$SKIP_DEPS" = false ]; then
    echo -e "${BLUE}[1/6]${NC} Cài đặt system dependencies..."

    # Detect OS
    if [ -f /etc/os-release ]; then
        . /etc/os-release
        OS=$ID
    else
        echo "Cannot detect OS. Assuming Ubuntu/Debian"
        OS="ubuntu"
    fi

    echo "Detected OS: $OS"
    echo ""

    # Install dependencies based on OS
    if [ "$OS" = "ubuntu" ] || [ "$OS" = "debian" ]; then
        echo "Installing dependencies for Ubuntu/Debian..."

        # Try to update package list, but continue even if some repositories fail
        echo "Updating package lists..."
        set +e  # Temporarily disable exit on error
        apt-get update > /dev/null 2>&1
        UPDATE_EXIT_CODE=$?
        set -e  # Re-enable exit on error

        if [ $UPDATE_EXIT_CODE -eq 0 ]; then
            echo -e "${GREEN}✓${NC} Package lists updated successfully"
        else
            echo -e "${YELLOW}⚠${NC}  Some repositories had errors (this is often OK)"
            echo "  Continuing with installation anyway..."
        fi

        # Check if packages are already installed
        echo "Installing required packages..."
        set +e  # Temporarily disable exit on error
        apt-get install -y \
            build-essential \
            cmake \
            git \
            libssl-dev \
            zlib1g-dev \
            libjsoncpp-dev \
            uuid-dev \
            pkg-config
        INSTALL_EXIT_CODE=$?
        set -e  # Re-enable exit on error

        if [ $INSTALL_EXIT_CODE -eq 0 ]; then
            echo -e "${GREEN}✓${NC} All packages installed successfully"
        else
            echo -e "${YELLOW}⚠${NC}  Some packages failed to install"
            echo "  This might be OK if packages are already installed"
            echo "  If build fails, try: sudo ./build.sh --skip-deps"
        fi

        echo -e "${GREEN}✓${NC} Dependencies installation completed!"

    elif [ "$OS" = "centos" ] || [ "$OS" = "rhel" ] || [ "$OS" = "fedora" ]; then
        echo "Installing dependencies for CentOS/RHEL/Fedora..."

        if command -v dnf &> /dev/null; then
            dnf install -y \
                gcc-c++ \
                cmake \
                git \
                openssl-devel \
                zlib-devel \
                jsoncpp-devel \
                libuuid-devel \
                pkgconfig
        else
            yum install -y \
                gcc-c++ \
                cmake \
                git \
                openssl-devel \
                zlib-devel \
                jsoncpp-devel \
                libuuid-devel \
                pkgconfig
        fi

        echo -e "${GREEN}✓${NC} Dependencies installed successfully!"
    else
        echo -e "${YELLOW}⚠${NC}  Unknown OS. Please install dependencies manually"
    fi
    echo ""
else
    echo -e "${YELLOW}[1/6]${NC} Skip cài đặt dependencies"
    echo ""
fi

# ============================================
# Step 2: Build Project
# ============================================
if [ "$SKIP_BUILD" = false ]; then
    echo -e "${BLUE}[2/6]${NC} Build project..."
    cd "$PROJECT_ROOT" || exit 1

    # Check if CMakeLists.txt exists
    if [ ! -f "CMakeLists.txt" ]; then
        echo -e "${RED}Error: Không tìm thấy CMakeLists.txt trong $PROJECT_ROOT${NC}"
        exit 1
    fi

    if [ ! -d "build" ]; then
        echo "Tạo thư mục build..."
        mkdir -p build
    fi

    cd build

    # Check if CMake is available
    if ! command -v cmake &> /dev/null; then
        echo -e "${RED}Error: CMake không được cài đặt${NC}"
        echo "Vui lòng cài đặt CMake hoặc chạy script với --skip-deps để cài dependencies"
        exit 1
    fi

    # Check and fix OpenCV dual version before CMake
    if [ -f "/usr/local/lib/cmake/opencv4/OpenCVConfig.cmake" ]; then
        echo "Checking OpenCV configuration..."
        
        # Create opencv4.pc symlink if needed
        if [ ! -f "/usr/local/lib/pkgconfig/opencv4.pc" ] && [ -f "/usr/local/lib/pkgconfig/opencv.pc" ]; then
            echo "Creating opencv4.pc symlink..."
            mkdir -p /usr/local/lib/pkgconfig
            ln -sf /usr/local/lib/pkgconfig/opencv.pc /usr/local/lib/pkgconfig/opencv4.pc 2>/dev/null || true
        fi
        
        # Set PKG_CONFIG_PATH for current session
        export PKG_CONFIG_PATH=/usr/local/lib/pkgconfig:$PKG_CONFIG_PATH
    fi
    
    if [ ! -f "CMakeCache.txt" ]; then
        echo "Chạy CMake với Release mode..."
        
        # Set OpenCV_DIR if OpenCV 4.10.0 exists in /usr/local
        CMAKE_OPTS=(
            -DCMAKE_BUILD_TYPE=Release
            -DAUTO_DOWNLOAD_DEPENDENCIES=ON
            -DDROGON_USE_FETCHCONTENT=ON
        )
        
        if [ -f "/usr/local/lib/cmake/opencv4/OpenCVConfig.cmake" ]; then
            CMAKE_OPTS+=(-DOpenCV_DIR=/usr/local/lib/cmake/opencv4)
            echo "  Using OpenCV from /usr/local (version 4.10.0+)"
        fi
        
        if ! cmake .. "${CMAKE_OPTS[@]}"; then
            echo -e "${RED}Error: CMake configuration failed${NC}"
            exit 1
        fi
    else
        echo "CMake đã được cấu hình, chỉ build..."
    fi

    # Check if make is available
    if ! command -v make &> /dev/null; then
        echo -e "${RED}Error: Make không được cài đặt${NC}"
        exit 1
    fi

    echo "Build project (sử dụng tất cả CPU cores)..."
    CPU_CORES=$(nproc)
    echo "Sử dụng $CPU_CORES CPU cores..."
    if ! make -j$CPU_CORES; then
        echo -e "${RED}Error: Build failed${NC}"
        exit 1
    fi

    cd ..
    echo -e "${GREEN}✓${NC} Build hoàn tất!"
    echo ""
else
    echo -e "${YELLOW}[2/6]${NC} Skip build (dùng build có sẵn)"
    echo ""
fi

# ============================================
# Step 3: Create User and Directories
# ============================================
echo -e "${BLUE}[3/6]${NC} Tạo user và thư mục..."
cd "$PROJECT_ROOT" || exit 1

# Create user and group
if ! id "$SERVICE_USER" &>/dev/null; then
    if useradd -r -s /bin/false -d "$INSTALL_DIR" "$SERVICE_USER" 2>/dev/null; then
        echo -e "${GREEN}✓${NC} Đã tạo user: $SERVICE_USER"
    else
        echo -e "${RED}Error: Không thể tạo user $SERVICE_USER${NC}"
        exit 1
    fi
else
    echo -e "${GREEN}✓${NC} User $SERVICE_USER đã tồn tại"
fi

# Ensure group exists (create if user exists but group doesn't)
if ! getent group "$SERVICE_GROUP" > /dev/null 2>&1; then
    if groupadd -r "$SERVICE_GROUP" 2>/dev/null; then
        echo -e "${GREEN}✓${NC} Đã tạo group: $SERVICE_GROUP"
    fi
    # Add user to group if not already a member
    usermod -a -G "$SERVICE_GROUP" "$SERVICE_USER" 2>/dev/null || true
fi

# Create installation directory and subdirectories
echo -e "${BLUE}Tạo thư mục...${NC}"
mkdir -p "$INSTALL_DIR"

# Create all directories from configuration
for dir_name in "${!APP_DIRECTORIES[@]}"; do
    dir_path="$INSTALL_DIR/$dir_name"
    dir_perms="${APP_DIRECTORIES[$dir_name]}"
    mkdir -p "$dir_path"
    chmod "$dir_perms" "$dir_path"
    echo -e "${GREEN}✓${NC} Đã tạo: $dir_path (permissions: $dir_perms)"
done

# Set ownership for all directories
chown -R "$SERVICE_USER:$SERVICE_GROUP" "$INSTALL_DIR"

# Set base directory permissions based on mode
if [ "$PERMISSION_MODE" = "full" ]; then
    chmod 777 "$INSTALL_DIR"  # Full permissions: drwxrwxrwx (như cvedix-rt)
    chmod -R 777 "$INSTALL_DIR"  # Apply to all subdirectories
    echo -e "${YELLOW}⚠${NC} Đang sử dụng FULL PERMISSIONS (777) - mọi người có thể đọc/ghi"
    echo -e "${YELLOW}⚠${NC} Cảnh báo: Quyền 777 không an toàn cho production!"
else
    chmod 755 "$INSTALL_DIR"  # Standard permissions: drwxr-xr-x (như Tabby, google, nvidia)
    # Keep individual directory permissions from configuration
    for dir_name in "${!APP_DIRECTORIES[@]}"; do
        dir_path="$INSTALL_DIR/$dir_name"
        dir_perms="${APP_DIRECTORIES[$dir_name]}"
        chmod "$dir_perms" "$dir_path"
    done
    echo -e "${GREEN}✓${NC} Đang sử dụng STANDARD PERMISSIONS (755)"
fi

echo -e "${GREEN}✓${NC} Đã tạo tất cả thư mục trong: $INSTALL_DIR"
echo ""

# Setup face database permissions
if [ -f "$PROJECT_ROOT/scripts/utils.sh" ]; then
    echo "Setup face database permissions..."
    if [ "$PERMISSION_MODE" = "full" ]; then
        "$PROJECT_ROOT/scripts/utils.sh" setup-face-db --full-permissions
    else
        "$PROJECT_ROOT/scripts/utils.sh" setup-face-db --standard-permissions
    fi
    echo -e "${GREEN}✓${NC} Face database setup hoàn tất!"
    echo ""
fi

# ============================================
# Step 4: Install Executable and Libraries
# ============================================
echo -e "${BLUE}[4/6]${NC} Cài đặt executable và libraries..."

# Find executable - check multiple possible locations
EXECUTABLE=""
EXECUTABLE_PATHS=(
    "$PROJECT_ROOT/build/bin/omniapi"
    "$PROJECT_ROOT/build/omniapi"
    "$PROJECT_ROOT/build/omniapi/omniapi"
)

for path in "${EXECUTABLE_PATHS[@]}"; do
    if [ -f "$path" ] && [ -x "$path" ]; then
        EXECUTABLE="$path"
        break
    fi
done

if [ -z "$EXECUTABLE" ]; then
    echo -e "${RED}Error: Không tìm thấy executable${NC}"
    echo "Đã kiểm tra các vị trí sau:"
    for path in "${EXECUTABLE_PATHS[@]}"; do
        echo "  - $path"
    done
    echo ""
    echo "Vui lòng build project trước hoặc bỏ --skip-build"
    exit 1
fi

echo "Executable: $EXECUTABLE"

# Verify executable is actually executable
if [ ! -x "$EXECUTABLE" ]; then
    echo -e "${YELLOW}⚠${NC}  File không có quyền thực thi, đang thêm quyền..."
    chmod +x "$EXECUTABLE"
fi

# Stop service if running (to avoid "Text file busy" error)
if systemctl is-active --quiet "${SERVICE_NAME}.service" 2>/dev/null; then
    echo "Dừng service để copy file mới..."
    systemctl stop "${SERVICE_NAME}.service" || true
    sleep 2  # Wait a bit longer to ensure service is fully stopped
fi

# Copy executable with backup if exists
if [ -f "$BIN_DIR/omniapi" ]; then
    echo "Backup executable cũ..."
    cp "$BIN_DIR/omniapi" "$BIN_DIR/omniapi.backup.$(date +%Y%m%d_%H%M%S)" 2>/dev/null || true
fi

# Copy executable
cp "$EXECUTABLE" "$BIN_DIR/omniapi"
chmod +x "$BIN_DIR/omniapi"
chown root:root "$BIN_DIR/omniapi"
echo -e "${GREEN}✓${NC} Đã cài đặt: $BIN_DIR/omniapi"

# Verify installation
if [ ! -f "$BIN_DIR/omniapi" ] || [ ! -x "$BIN_DIR/omniapi" ]; then
    echo -e "${RED}Error: Không thể cài đặt executable${NC}"
    exit 1
fi

# Copy shared libraries
mkdir -p "$LIB_DIR"
LIB_SOURCE="$PROJECT_ROOT/build/lib"

if [ -d "$LIB_SOURCE" ]; then
    echo "Copy shared libraries..."
    cd "$LIB_SOURCE"

    LIB_COUNT=0
    # Copy all drogon, trantor, and jsoncpp libraries (including symlinks)
    for lib in libdrogon.so* libtrantor.so* libjsoncpp.so*; do
        if [ -e "$lib" ]; then
            if cp -P "$lib" "$LIB_DIR/" 2>/dev/null; then
                LIB_COUNT=$((LIB_COUNT + 1))
            fi
        fi
    done

    if [ $LIB_COUNT -gt 0 ]; then
        # Update library cache
        ldconfig
        echo -e "${GREEN}✓${NC} Đã cài đặt $LIB_COUNT shared libraries vào $LIB_DIR"
    else
        echo -e "${YELLOW}⚠${NC}  Không tìm thấy libraries để copy trong $LIB_SOURCE"
    fi
else
    echo -e "${YELLOW}⚠${NC}  Thư mục build/lib không tồn tại. Bỏ qua copy libraries."
    echo "  Libraries có thể đã được cài đặt hệ thống hoặc được link tĩnh."
fi

cd "$PROJECT_ROOT"

    # Copy configuration files
    # Create .env from .env.example if .env doesn't exist
    if [ ! -f "$PROJECT_ROOT/.env" ] && [ -f "$PROJECT_ROOT/.env.example" ]; then
        echo "Tạo .env từ .env.example..."
        cp "$PROJECT_ROOT/.env.example" "$PROJECT_ROOT/.env"
        echo -e "${GREEN}✓${NC} Đã tạo .env từ .env.example"
    fi

    if [ -f "$PROJECT_ROOT/.env" ]; then
        cp "$PROJECT_ROOT/.env" "$INSTALL_DIR/config/.env"
        chown "$SERVICE_USER:$SERVICE_GROUP" "$INSTALL_DIR/config/.env"
        chmod 640 "$INSTALL_DIR/config/.env"
        echo -e "${GREEN}✓${NC} Đã copy .env"
    else
        echo -e "${YELLOW}⚠${NC}  File .env không tồn tại. Bạn có thể tạo sau tại $INSTALL_DIR/config/.env"
    fi

    # Setup GStreamer plugin path automatically
    echo "Setup GStreamer plugin path..."
    if [ -f "$PROJECT_ROOT/scripts/utils.sh" ]; then
        "$PROJECT_ROOT/scripts/utils.sh" setup-gst-path "$INSTALL_DIR/config/.env" 2>/dev/null || {
            echo -e "${YELLOW}⚠${NC}  Could not auto-detect GStreamer plugin path"
            echo "  You can set it manually: sudo $PROJECT_ROOT/scripts/utils.sh setup-gst-path"
        }
    fi

# Copy openapi.yaml file (required for Swagger UI)
if [ -f "$PROJECT_ROOT/api-specs/openapi.yaml" ]; then
    cp "$PROJECT_ROOT/api-specs/openapi.yaml" "$INSTALL_DIR/openapi.yaml"
    chown "$SERVICE_USER:$SERVICE_GROUP" "$INSTALL_DIR/openapi.yaml"
    chmod 644 "$INSTALL_DIR/openapi.yaml"
    echo -e "${GREEN}✓${NC} Đã copy openapi.yaml"
else
    echo -e "${YELLOW}⚠${NC}  File openapi.yaml không tồn tại. Swagger UI sẽ không hoạt động."
fi

# Copy api-specs/scalar directory (required for Scalar documentation)
if [ -d "$PROJECT_ROOT/api-specs/scalar" ]; then
    mkdir -p "$INSTALL_DIR/api-specs/scalar"
    cp -r "$PROJECT_ROOT/api-specs/scalar"/* "$INSTALL_DIR/api-specs/scalar/"
    chown -R "$SERVICE_USER:$SERVICE_GROUP" "$INSTALL_DIR/api-specs"
    chmod -R 644 "$INSTALL_DIR/api-specs/scalar"/* 2>/dev/null || true
    find "$INSTALL_DIR/api-specs/scalar" -type f -exec chmod 644 {} \;
    echo -e "${GREEN}✓${NC} Đã copy api-specs/scalar directory"
else
    echo -e "${YELLOW}⚠${NC}  Thư mục api-specs/scalar không tồn tại. Scalar documentation sẽ không hoạt động."
fi

echo ""

# ============================================
# Step 5: Fix Issues (Libraries, Uploads, Watchdog)
# ============================================
if [ "$SKIP_FIXES" = false ]; then
    echo -e "${BLUE}[5/6]${NC} Fix các vấn đề cấu hình..."

    # Fix uploads directory (already done in step 3, but ensure permissions)
    echo "Đảm bảo uploads directory có quyền đúng..."
    mkdir -p "$INSTALL_DIR/uploads"
    chown "$SERVICE_USER:$SERVICE_USER" "$INSTALL_DIR/uploads"
    chmod 755 "$INSTALL_DIR/uploads"
    echo -e "${GREEN}✓${NC} Uploads directory OK"

    # Fix watchdog in service file (if service file exists)
    INSTALLED_SERVICE_FILE="/etc/systemd/system/${SERVICE_NAME}.service"
    if [ -f "$INSTALLED_SERVICE_FILE" ]; then
        echo "Fix watchdog configuration..."

        # Backup service file
        if [ ! -f "${INSTALLED_SERVICE_FILE}.backup" ]; then
            cp "$INSTALLED_SERVICE_FILE" "${INSTALLED_SERVICE_FILE}.backup"
        fi

        # Comment out WatchdogSec and NotifyAccess if not already commented
        sed -i 's/^WatchdogSec=/#WatchdogSec=/' "$INSTALLED_SERVICE_FILE" 2>/dev/null || true
        sed -i 's/^NotifyAccess=/#NotifyAccess=/' "$INSTALLED_SERVICE_FILE" 2>/dev/null || true

        # Generate ReadWritePaths from APP_DIRECTORIES configuration
        READWRITE_PATHS=""
        for dir_name in "${!APP_DIRECTORIES[@]}"; do
            READWRITE_PATHS="$READWRITE_PATHS $INSTALL_DIR/$dir_name"
        done
        READWRITE_PATHS="${READWRITE_PATHS# }"  # Trim leading space

        # Update ReadWritePaths in service file
        if grep -q "^ReadWritePaths=" "$INSTALLED_SERVICE_FILE"; then
            # Replace existing ReadWritePaths
            sed -i "s|^ReadWritePaths=.*|ReadWritePaths=$READWRITE_PATHS|" "$INSTALLED_SERVICE_FILE"
            echo -e "${GREEN}✓${NC} Đã cập nhật ReadWritePaths trong service file"
        else
            # Add ReadWritePaths if it doesn't exist (before [Install] section)
            sed -i "/^\[Install\]/i ReadWritePaths=$READWRITE_PATHS" "$INSTALLED_SERVICE_FILE"
            echo -e "${GREEN}✓${NC} Đã thêm ReadWritePaths vào service file"
        fi

        echo -e "${GREEN}✓${NC} Service file đã được cập nhật"
    fi

    echo ""
else
    echo -e "${YELLOW}[5/6]${NC} Skip các bước fix"
    echo ""
fi

# ============================================
# Step 6: Install and Start Systemd Service
# ============================================
echo -e "${BLUE}[6/6]${NC} Cài đặt systemd service..."

# Service file is in the same directory as build.sh (deploy/)
SERVICE_FILE_PATH="$SCRIPT_DIR/$SERVICE_FILE"
if [ ! -f "$SERVICE_FILE_PATH" ]; then
    echo -e "${RED}Error: Service file không tồn tại: $SERVICE_FILE_PATH${NC}"
    exit 1
fi

# Generate ReadWritePaths from APP_DIRECTORIES configuration
READWRITE_PATHS=""
for dir_name in "${!APP_DIRECTORIES[@]}"; do
    READWRITE_PATHS="$READWRITE_PATHS $INSTALL_DIR/$dir_name"
done
READWRITE_PATHS="${READWRITE_PATHS# }"  # Trim leading space

# Update service file paths and ReadWritePaths
SERVICE_TEMP=$(mktemp)
sed "s|/opt/omniapi|$INSTALL_DIR|g" "$SERVICE_FILE_PATH" > "$SERVICE_TEMP"

# Update ReadWritePaths in service file
if grep -q "^ReadWritePaths=" "$SERVICE_TEMP"; then
    # Replace existing ReadWritePaths
    sed -i "s|^ReadWritePaths=.*|ReadWritePaths=$READWRITE_PATHS|" "$SERVICE_TEMP"
else
    # Add ReadWritePaths if it doesn't exist (before [Install] section)
    sed -i "/^\[Install\]/i ReadWritePaths=$READWRITE_PATHS" "$SERVICE_TEMP"
fi

# Copy service file
cp "$SERVICE_TEMP" "/etc/systemd/system/${SERVICE_NAME}.service"
rm "$SERVICE_TEMP"
chmod 644 "/etc/systemd/system/${SERVICE_NAME}.service"

# Reload systemd
systemctl daemon-reload
echo -e "${GREEN}✓${NC} Đã cài đặt service: ${SERVICE_NAME}.service"

# Enable service (auto-start on boot)
systemctl enable "${SERVICE_NAME}.service"
echo -e "${GREEN}✓${NC} Đã enable service (sẽ tự động chạy khi khởi động)"

# Start or restart service
if [ "$NO_START" = false ]; then
    echo ""
    echo "Khởi động service..."
    if systemctl is-active --quiet "${SERVICE_NAME}.service" 2>/dev/null; then
        echo "Service đang chạy, đang restart..."
        if systemctl restart "${SERVICE_NAME}.service"; then
            echo -e "${GREEN}✓${NC} Đã restart service"
        else
            echo -e "${RED}✗${NC} Không thể restart service"
            echo "Kiểm tra log: sudo journalctl -u ${SERVICE_NAME}.service -n 50"
            exit 1
        fi
    else
        echo "Khởi động service mới..."
        if systemctl start "${SERVICE_NAME}.service"; then
            echo -e "${GREEN}✓${NC} Đã khởi động service"
        else
            echo -e "${RED}✗${NC} Không thể khởi động service"
            echo "Kiểm tra log: sudo journalctl -u ${SERVICE_NAME}.service -n 50"
            exit 1
        fi
    fi

    # Wait a moment for service to start
    echo "Đợi service khởi động..."
    sleep 3

    # Check service status
    echo ""
    echo "=========================================="
    echo "Kết quả triển khai:"
    echo "=========================================="
    if systemctl is-active --quiet "${SERVICE_NAME}.service"; then
        echo -e "${GREEN}✓ Service đang chạy thành công!${NC}"
        echo ""
        echo "Thông tin service:"
        systemctl status "${SERVICE_NAME}.service" --no-pager -l | head -n 15

        # Try to get API endpoint info if available
        echo ""
        echo "Kiểm tra API endpoint..."
        sleep 1
        if command -v curl &> /dev/null; then
            API_PORT=$(grep -E "^API_PORT=" "$INSTALL_DIR/config/.env" 2>/dev/null | cut -d'=' -f2 || echo "8080")
            API_HOST=$(grep -E "^API_HOST=" "$INSTALL_DIR/config/.env" 2>/dev/null | cut -d'=' -f2 || echo "0.0.0.0")
            if [ "$API_HOST" = "0.0.0.0" ] || [ "$API_HOST" = "127.0.0.1" ] || [ -z "$API_HOST" ]; then
                API_HOST="localhost"
            fi
            if curl -s -f "http://${API_HOST}:${API_PORT}/v1/core/health" > /dev/null 2>&1; then
                echo -e "${GREEN}✓${NC} API endpoint đang phản hồi: http://${API_HOST}:${API_PORT}/v1/core/health"
            else
                echo -e "${YELLOW}⚠${NC}  API endpoint chưa sẵn sàng (có thể cần thêm thời gian)"
            fi
        fi
    else
        echo -e "${RED}✗ Service không chạy. Kiểm tra log:${NC}"
        echo "  sudo journalctl -u ${SERVICE_NAME}.service -n 50"
        echo ""
        echo "Hoặc xem log real-time:"
        echo "  sudo journalctl -u ${SERVICE_NAME}.service -f"
        exit 1
    fi
else
    echo -e "${YELLOW}⚠${NC}  Service không được tự động khởi động (--no-start)"
    echo "  Để khởi động thủ công: sudo systemctl start ${SERVICE_NAME}.service"
fi

echo ""
echo "=========================================="
echo "Các lệnh hữu ích:"
echo "=========================================="
echo "  Xem trạng thái:    sudo systemctl status ${SERVICE_NAME}"
echo "  Xem log:           sudo journalctl -u ${SERVICE_NAME} -f"
echo "  Khởi động lại:     sudo systemctl restart ${SERVICE_NAME}"
echo "  Dừng service:      sudo systemctl stop ${SERVICE_NAME}"
echo "  Bắt đầu service:   sudo systemctl start ${SERVICE_NAME}"
echo ""
echo -e "${GREEN}=========================================="
echo "Build & Deploy hoàn tất!"
echo "==========================================${NC}"
