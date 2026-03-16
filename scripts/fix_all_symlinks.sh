#!/bin/bash
# ============================================
# Fix All Symlinks Script
# ============================================
#
# Script để tạo các symlink cần thiết cho CVEDIX SDK và các dependencies
# Cần chạy với quyền sudo để tạo symlink trong /usr/lib và /usr/include
#
# Usage:
#   sudo ./scripts/fix_all_symlinks.sh
#
# ============================================

set -e

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

# Get script directory
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

# Check if running as root
if [ "$EUID" -ne 0 ]; then
    echo -e "${RED}Error:${NC} This script must be run as root (use sudo)"
    echo "Usage: sudo ./scripts/fix_all_symlinks.sh"
    exit 1
fi

echo -e "${BLUE}=== Fixing Symlinks ===${NC}\n"

# ============================================
# 1. Fix libtinyexpr.so symlink
# ============================================
echo -e "${BLUE}[1/4]${NC} Fixing libtinyexpr.so symlink..."

# Try multiple possible locations
TINYEXPR_SOURCES=(
    "/opt/edgeos-sdk/lib/cvedix/libtinyexpr.so"
    "/opt/cvedix-ai-runtime/lib/cvedix/libtinyexpr.so"
    "/opt/cvedix/lib/libtinyexpr.so"
)

TINYEXPR_TARGET="/usr/lib/libtinyexpr.so"
TINYEXPR_FOUND=false

for source in "${TINYEXPR_SOURCES[@]}"; do
    if [ -f "$source" ]; then
        # Remove existing symlink or file if exists
        if [ -L "$TINYEXPR_TARGET" ] || [ -f "$TINYEXPR_TARGET" ]; then
            rm -f "$TINYEXPR_TARGET"
        fi
        ln -sf "$source" "$TINYEXPR_TARGET"
        echo -e "  ${GREEN}✓${NC} Created symlink: $TINYEXPR_TARGET -> $source"
        TINYEXPR_FOUND=true
        break
    fi
done

if [ "$TINYEXPR_FOUND" = false ]; then
    echo -e "  ${YELLOW}⚠${NC}  libtinyexpr.so not found in any expected location"
    echo "     Searched: ${TINYEXPR_SOURCES[*]}"
fi

# ============================================
# 2. Fix libcvedix_instance_sdk.so symlink
# ============================================
echo -e "\n${BLUE}[2/4]${NC} Fixing libcvedix_instance_sdk.so symlink..."

CVEDIX_SDK_SOURCES=(
    "/opt/edgeos-sdk/lib/cvedix/libcvedix_instance_sdk.so"
    "/opt/cvedix-ai-runtime/lib/cvedix/libcvedix_instance_sdk.so"
)

CVEDIX_SDK_TARGET="/usr/lib/libcvedix_instance_sdk.so"
CVEDIX_SDK_FOUND=false

for source in "${CVEDIX_SDK_SOURCES[@]}"; do
    if [ -f "$source" ]; then
        # Remove existing symlink or file if exists
        if [ -L "$CVEDIX_SDK_TARGET" ] || [ -f "$CVEDIX_SDK_TARGET" ]; then
            rm -f "$CVEDIX_SDK_TARGET"
        fi
        ln -sf "$source" "$CVEDIX_SDK_TARGET"
        echo -e "  ${GREEN}✓${NC} Created symlink: $CVEDIX_SDK_TARGET -> $source"
        CVEDIX_SDK_FOUND=true
        break
    fi
done

if [ "$CVEDIX_SDK_FOUND" = false ]; then
    echo -e "  ${YELLOW}⚠${NC}  libcvedix_instance_sdk.so not found in any expected location"
fi

# ============================================
# 3. Fix Cereal symlink
# ============================================
echo -e "\n${BLUE}[3/4]${NC} Fixing Cereal symlink..."

CEREAL_SOURCES=(
    "$PROJECT_ROOT/build/_deps/cereal-src/include/cereal"
    "/usr/include/cereal"
    "/usr/local/include/cereal"
)

CEREAL_TARGET_DIR="/usr/include/cvedix/third_party"
CEREAL_TARGET="$CEREAL_TARGET_DIR/cereal"
CEREAL_FOUND=false

for source in "${CEREAL_SOURCES[@]}"; do
    if [ -d "$source" ]; then
        mkdir -p "$CEREAL_TARGET_DIR"
        # Remove existing symlink or directory if exists
        if [ -L "$CEREAL_TARGET" ] || [ -d "$CEREAL_TARGET" ]; then
            rm -rf "$CEREAL_TARGET"
        fi
        ln -sf "$source" "$CEREAL_TARGET"
        echo -e "  ${GREEN}✓${NC} Created symlink: $CEREAL_TARGET -> $source"
        CEREAL_FOUND=true
        break
    fi
done

if [ "$CEREAL_FOUND" = false ]; then
    echo -e "  ${YELLOW}⚠${NC}  Cereal not found in any expected location"
fi

# ============================================
# 4. Fix cpp-base64 symlink
# ============================================
echo -e "\n${BLUE}[4/4]${NC} Fixing cpp-base64 symlink..."

CPP_BASE64_SOURCES=(
    "$PROJECT_ROOT/build/_deps/cpp-base64-src/base64.h"
    "/opt/edgeos-sdk/include/cvedix/third_party/cpp_base64/base64.h"
)

CPP_BASE64_TARGET_DIR="/usr/include/cvedix/third_party/cpp_base64"
CPP_BASE64_TARGET="$CPP_BASE64_TARGET_DIR/base64.h"
CPP_BASE64_FOUND=false

for source in "${CPP_BASE64_SOURCES[@]}"; do
    if [ -f "$source" ]; then
        mkdir -p "$CPP_BASE64_TARGET_DIR"
        # Remove existing symlink or file if exists
        if [ -L "$CPP_BASE64_TARGET" ] || [ -f "$CPP_BASE64_TARGET" ]; then
            rm -f "$CPP_BASE64_TARGET"
        fi
        ln -sf "$source" "$CPP_BASE64_TARGET"
        echo -e "  ${GREEN}✓${NC} Created symlink: $CPP_BASE64_TARGET -> $source"
        CPP_BASE64_FOUND=true
        break
    fi
done

if [ "$CPP_BASE64_FOUND" = false ]; then
    echo -e "  ${YELLOW}⚠${NC}  cpp-base64/base64.h not found in any expected location"
fi

# ============================================
# Summary
# ============================================
echo -e "\n${GREEN}=== Symlink Fix Complete ===${NC}"
echo ""
echo "Summary:"
[ "$TINYEXPR_FOUND" = true ] && echo -e "  ${GREEN}✓${NC} libtinyexpr.so" || echo -e "  ${YELLOW}⚠${NC}  libtinyexpr.so (not found)"
[ "$CVEDIX_SDK_FOUND" = true ] && echo -e "  ${GREEN}✓${NC} libcvedix_instance_sdk.so" || echo -e "  ${YELLOW}⚠${NC}  libcvedix_instance_sdk.so (not found)"
[ "$CEREAL_FOUND" = true ] && echo -e "  ${GREEN}✓${NC} Cereal" || echo -e "  ${YELLOW}⚠${NC}  Cereal (not found)"
[ "$CPP_BASE64_FOUND" = true ] && echo -e "  ${GREEN}✓${NC} cpp-base64" || echo -e "  ${YELLOW}⚠${NC}  cpp-base64 (not found)"
echo ""
