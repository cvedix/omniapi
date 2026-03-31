#!/bin/bash
# ============================================
# Pre-Installation System Compatibility Check
# ============================================
# Script này kiểm tra system compatibility trước khi cài package
# Đảm bảo môi trường đáp ứng yêu cầu tối thiểu
#
# Usage: ./pre_install_check.sh

set -e

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

ERRORS=0
WARNINGS=0

log_info() {
    echo -e "${BLUE}[INFO]${NC} $1"
}

log_success() {
    echo -e "${GREEN}[✓]${NC} $1"
}

log_warning() {
    echo -e "${YELLOW}[⚠]${NC} $1"
    ((WARNINGS++))
}

log_error() {
    echo -e "${RED}[✗]${NC} $1"
    ((ERRORS++))
}

echo "=========================================="
echo "Pre-Installation System Check"
echo "=========================================="
echo ""

# ============================================
# 1. Check Operating System
# ============================================
echo "1. Checking Operating System..."

if [ -f /etc/os-release ]; then
    . /etc/os-release
    OS_NAME="$ID"
    OS_VERSION="$VERSION_ID"
    
    log_success "OS: $PRETTY_NAME"
    
    # Check if it's a supported OS
    case "$OS_NAME" in
        ubuntu|debian)
            log_success "Supported OS detected: $OS_NAME"
            ;;
        *)
            log_warning "OS $OS_NAME may not be fully supported (tested on Ubuntu/Debian)"
            ;;
    esac
else
    log_error "Cannot determine OS (/etc/os-release not found)"
fi

# ============================================
# 2. Check Architecture
# ============================================
echo ""
echo "2. Checking Architecture..."

ARCH=$(uname -m)
case "$ARCH" in
    x86_64)
        log_success "Architecture: $ARCH (supported)"
        ;;
    aarch64|arm64)
        log_success "Architecture: $ARCH (supported)"
        ;;
    *)
        log_warning "Architecture: $ARCH (may not be fully tested)"
        ;;
esac

# ============================================
# 3. Check System Libraries (Basic)
# ============================================
echo ""
echo "3. Checking System Libraries..."

REQUIRED_SYSTEM_LIBS=(
    "libc.so"
    "libstdc++.so"
    "libgcc_s.so"
)

for lib in "${REQUIRED_SYSTEM_LIBS[@]}"; do
    if ldconfig -p 2>/dev/null | grep -q "$lib"; then
        log_success "$lib found"
    else
        log_error "$lib not found (critical system library)"
    fi
done

# ============================================
# 4. Check Disk Space
# ============================================
echo ""
echo "4. Checking Disk Space..."

INSTALL_DIR="/opt/omniapi"
REQUIRED_SPACE_MB=500  # Minimum 500MB

if [ -d "$INSTALL_DIR" ]; then
    available_space=$(df -m "$INSTALL_DIR" | tail -1 | awk '{print $4}')
else
    available_space=$(df -m /opt 2>/dev/null | tail -1 | awk '{print $4}' || df -m / | tail -1 | awk '{print $4}')
fi

if [ -n "$available_space" ] && [ "$available_space" -gt "$REQUIRED_SPACE_MB" ]; then
    log_success "Sufficient disk space: ${available_space}MB available (required: ${REQUIRED_SPACE_MB}MB)"
else
    log_warning "Low disk space: ${available_space}MB available (recommended: ${REQUIRED_SPACE_MB}MB+)"
fi

# ============================================
# 5. Check Permissions
# ============================================
echo ""
echo "5. Checking Permissions..."

if [ "$EUID" -eq 0 ]; then
    log_success "Running as root (required for installation)"
else
    log_error "Not running as root (install with: sudo dpkg -i <package.deb>)"
fi

# ============================================
# 6. Check Systemd (if available)
# ============================================
echo ""
echo "6. Checking Systemd..."

if command -v systemctl >/dev/null 2>&1; then
    log_success "systemctl found"
    if systemctl is-system-running >/dev/null 2>&1; then
        log_success "Systemd is running"
    else
        log_warning "Systemd may not be running (service management may not work)"
    fi
else
    log_warning "systemctl not found (service management will not work)"
fi

# ============================================
# 7. Check Network (optional)
# ============================================
echo ""
echo "7. Checking Network Connectivity..."

if ping -c 1 -W 2 8.8.8.8 >/dev/null 2>&1; then
    log_success "Network connectivity OK"
else
    log_warning "Network connectivity check failed (may be normal in isolated environments)"
fi

# ============================================
# Summary
# ============================================
echo ""
echo "=========================================="
echo "Pre-Installation Check Summary"
echo "=========================================="
echo "Errors:   $ERRORS"
echo "Warnings: $WARNINGS"
echo ""

if [ $ERRORS -eq 0 ]; then
    log_success "System is ready for installation"
    exit 0
else
    log_error "System check failed. Please fix the errors above before installation."
    exit 1
fi

