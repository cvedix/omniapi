#!/bin/bash
# ============================================
# Reinstall GStreamer Script
# ============================================
# Script này gỡ cài đặt và cài lại toàn bộ GStreamer
# để fix lỗi plugin registry
#
# Usage:
#   sudo ./deploy/reinstall_gstreamer.sh
# ============================================

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

echo -e "${BLUE}========================================${NC}"
echo -e "${BLUE}Reinstall GStreamer${NC}"
echo -e "${BLUE}========================================${NC}"
echo ""

# Check if running as root
if [ "$EUID" -ne 0 ]; then
    echo -e "${RED}Error: Script phải chạy bằng sudo${NC}"
    echo "Usage: sudo ./deploy/reinstall_gstreamer.sh"
    exit 1
fi

# Step 1: Remove GStreamer packages
echo -e "${BLUE}[1/6]${NC} Gỡ cài đặt GStreamer packages..."
apt-get remove --purge -y \
    gstreamer1.0-* \
    libgstreamer1.0-* \
    2>/dev/null || true
echo -e "${GREEN}✓${NC} Đã gỡ cài đặt GStreamer packages"
echo ""

# Step 2: Clean up cache and registry
echo -e "${BLUE}[2/6]${NC} Xóa GStreamer cache và registry..."
rm -rf /root/.cache/gstreamer-1.0/ 2>/dev/null || true
rm -rf /home/*/.cache/gstreamer-1.0/ 2>/dev/null || true
rm -rf /var/cache/gstreamer-1.0/ 2>/dev/null || true
echo -e "${GREEN}✓${NC} Đã xóa cache và registry"
echo ""

# Step 3: Update package list
echo -e "${BLUE}[3/6]${NC} Cập nhật package list..."
apt-get update
echo -e "${GREEN}✓${NC} Đã cập nhật package list"
echo ""

# Step 4: Install GStreamer core
echo -e "${BLUE}[4/6]${NC} Cài đặt GStreamer core..."
apt-get install -y \
    gstreamer1.0-tools \
    gstreamer1.0-x \
    gstreamer1.0-plugins-base \
    gstreamer1.0-plugins-good \
    gstreamer1.0-plugins-bad \
    gstreamer1.0-plugins-ugly \
    gstreamer1.0-libav \
    libgstreamer1.0-dev \
    libgstreamer-plugins-base1.0-dev
echo -e "${GREEN}✓${NC} Đã cài đặt GStreamer core và plugins"
echo ""

# Step 5: Rebuild plugin registry
echo -e "${BLUE}[5/6]${NC} Rebuild GStreamer plugin registry..."
# Force rebuild by inspecting a known plugin
gst-inspect-1.0 videoconvert >/dev/null 2>&1 || true
gst-inspect-1.0 filesrc >/dev/null 2>&1 || true
gst-inspect-1.0 urisourcebin >/dev/null 2>&1 || true
echo -e "${GREEN}✓${NC} Đã rebuild plugin registry"
echo ""

# Step 6: Verify installation
echo -e "${BLUE}[6/6]${NC} Kiểm tra cài đặt..."
echo ""

# Check plugin count
PLUGIN_COUNT=$(gst-inspect-1.0 2>&1 | grep -c "^[a-z]" || echo "0")
echo -e "Số lượng plugins tìm thấy: ${PLUGIN_COUNT}"

# Check specific plugins
echo ""
echo "Kiểm tra các plugins quan trọng:"

check_plugin() {
    local plugin=$1
    if gst-inspect-1.0 "$plugin" >/dev/null 2>&1; then
        echo -e "  ${GREEN}✓${NC} $plugin"
        return 0
    else
        echo -e "  ${RED}✗${NC} $plugin (MISSING)"
        return 1
    fi
}

MISSING=0
check_plugin "filesrc" || MISSING=1
check_plugin "videoconvert" || MISSING=1
check_plugin "urisourcebin" || MISSING=1
check_plugin "appsink" || MISSING=1
check_plugin "qtdemux" || MISSING=1
check_plugin "h264parse" || MISSING=1
check_plugin "avdec_h264" || MISSING=1

echo ""

if [ $MISSING -eq 0 ]; then
    echo -e "${GREEN}========================================${NC}"
    echo -e "${GREEN}✓ Cài đặt thành công!${NC}"
    echo -e "${GREEN}========================================${NC}"
    echo ""
    echo "Tất cả plugins quan trọng đã được cài đặt."
    echo ""
    echo "Để test:"
    echo "  gst-inspect-1.0 filesrc"
    echo "  gst-inspect-1.0 videoconvert"
    exit 0
else
    echo -e "${YELLOW}========================================${NC}"
    echo -e "${YELLOW}⚠ Một số plugins vẫn còn thiếu${NC}"
    echo -e "${YELLOW}========================================${NC}"
    echo ""
    echo "Có thể cần:"
    echo "  1. Restart service: sudo systemctl restart edgeos-api"
    echo "  2. Kiểm tra lại: gst-inspect-1.0 filesrc"
    echo "  3. Xem log: sudo journalctl -u edgeos-api -f"
    exit 1
fi
