#!/bin/bash
# ============================================
# Fix OpenCV Freetype Library for CVEDIX SDK
# ============================================
#
# Script này tự động fix lỗi thiếu libopencv_freetype.so.410
# File này REQUIRED cho CVEDIX SDK compatibility
#
# Usage:
#   sudo ./scripts/fix_freetype.sh
#   sudo /opt/edgeos-api/scripts/fix_freetype.sh
#
# ============================================

set -e

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

# Configuration
INSTALL_DIR="${INSTALL_DIR:-/opt/edgeos-api}"
FREETYPE_410_TARGET="$INSTALL_DIR/lib/libopencv_freetype.so.410"
SERVICE_NAME="edgeos-api"
SERVICE_USER="edgeai"
SERVICE_GROUP="edgeai"

echo "=========================================="
echo "Fix OpenCV Freetype Library for CVEDIX SDK"
echo "=========================================="
echo ""

# Check if running as root
if [ "$EUID" -ne 0 ]; then
    echo -e "${RED}Error: This script must be run as root (use sudo)${NC}"
    exit 1
fi

# Ensure lib directory exists
mkdir -p "$INSTALL_DIR/lib"
echo "Target directory: $INSTALL_DIR/lib"
echo ""

# Check if file already exists
if [ -f "$FREETYPE_410_TARGET" ] && [ -r "$FREETYPE_410_TARGET" ]; then
    echo -e "${GREEN}✓${NC} libopencv_freetype.so.410 already exists at $FREETYPE_410_TARGET"
    echo "  File size: $(stat -c%s "$FREETYPE_410_TARGET" 2>/dev/null || echo "unknown") bytes"
    echo ""
    echo "File is already present. If you're still experiencing issues, try:"
    echo "  1. sudo ldconfig"
    echo "  2. sudo systemctl restart $SERVICE_NAME"
    exit 0
fi

echo "Searching for OpenCV 4.10 freetype library..."
echo ""

# OpenCV 4.10 ONLY (REQUIRED - no fallback to OpenCV 4.6)
OPENCV_410_FREETYPE_SOURCES=(
    # Actual files (not symlinks) - highest priority
    "/usr/local/lib/libopencv_freetype.so.4.10.0"
    "/usr/lib/x86_64-linux-gnu/libopencv_freetype.so.4.10.0"
    "/usr/lib/libopencv_freetype.so.4.10.0"
    # Symlinks (will be resolved)
    "/usr/local/lib/libopencv_freetype.so.4.10"
    "/usr/local/lib/libopencv_freetype.so.410"
    "/usr/lib/x86_64-linux-gnu/libopencv_freetype.so.4.10"
    "/usr/lib/x86_64-linux-gnu/libopencv_freetype.so.410"
    "/usr/lib/libopencv_freetype.so.4.10"
    "/usr/lib/libopencv_freetype.so.410"
)

FREETYPE_COPIED=false
FREETYPE_SOURCE=""

for source in "${OPENCV_410_FREETYPE_SOURCES[@]}"; do
    # Check if source exists (file or symlink)
    if [ -f "$source" ] || [ -L "$source" ]; then
        echo "  Found: $source"
        
        # Resolve symlink to get actual file path
        if [ -L "$source" ]; then
            resolved_source=$(readlink -f "$source" 2>/dev/null || echo "")
            if [ -n "$resolved_source" ] && [ -f "$resolved_source" ]; then
                echo "    → Resolved to: $resolved_source"
                actual_source="$resolved_source"
            else
                echo "    ⚠  Warning: Symlink cannot be resolved, skipping..."
                continue
            fi
        else
            actual_source="$source"
        fi
        
        # Try to copy
        echo "    Copying to $FREETYPE_410_TARGET..."
        if cp -L "$actual_source" "$FREETYPE_410_TARGET" 2>&1; then
            # Verify the copy was successful
            if [ -f "$FREETYPE_410_TARGET" ] && [ -r "$FREETYPE_410_TARGET" ]; then
                FREETYPE_COPIED=true
                FREETYPE_SOURCE="$source"
                echo -e "    ${GREEN}✓${NC} Successfully copied!"
                break
            else
                echo "    ⚠  Copy appeared to succeed but file not found at destination"
            fi
        else
            echo "    ⚠  Copy failed, trying next source..."
        fi
    fi
done

echo ""

if [ "$FREETYPE_COPIED" = false ]; then
    echo -e "${RED}✗ ERROR: Could not find OpenCV freetype library${NC}"
    echo ""
    echo "OpenCV 4.10 freetype library not found in system."
    echo "This file is REQUIRED for CVEDIX SDK compatibility."
    echo ""
    echo "To fix this issue:"
    echo ""
    echo "1. Install OpenCV 4.10 with freetype support:"
    echo "   sudo apt-get install libfreetype6-dev"
    echo "   sudo $INSTALL_DIR/scripts/build_opencv_safe.sh"
    echo ""
    echo "2. Or manually copy if you have OpenCV 4.10 installed elsewhere:"
    echo "   sudo cp /path/to/libopencv_freetype.so.4.10.0 $FREETYPE_410_TARGET"
    echo ""
    echo "3. Check if OpenCV is installed:"
    echo "   opencv_version"
    echo "   ls -la /usr/local/lib/libopencv_freetype.so*"
    echo "   ls -la /usr/lib/x86_64-linux-gnu/libopencv_freetype.so*"
    echo ""
    exit 1
fi

# Set correct permissions
echo "Setting permissions..."
chmod 644 "$FREETYPE_410_TARGET" 2>/dev/null || true
chown "$SERVICE_USER:$SERVICE_GROUP" "$FREETYPE_410_TARGET" 2>/dev/null || true

# Verify permissions
if [ -r "$FREETYPE_410_TARGET" ]; then
    echo -e "${GREEN}✓${NC} Permissions set correctly"
else
    echo -e "${YELLOW}⚠${NC}  Warning: File may not be readable"
fi

# Update library cache
echo ""
echo "Updating library cache..."
if command -v ldconfig >/dev/null 2>&1; then
    ldconfig 2>&1 | grep -v "is not a symbolic link" || true
    echo -e "${GREEN}✓${NC} Library cache updated"
else
    echo -e "${YELLOW}⚠${NC}  Warning: ldconfig not found"
fi

# Verify file is accessible
echo ""
echo "Verifying file..."
if [ -f "$FREETYPE_410_TARGET" ] && [ -r "$FREETYPE_410_TARGET" ]; then
    FILE_SIZE=$(stat -c%s "$FREETYPE_410_TARGET" 2>/dev/null || echo "unknown")
    echo -e "${GREEN}✓${NC} File verified: $FREETYPE_410_TARGET"
    echo "  Size: $FILE_SIZE bytes"
    echo "  Source: $FREETYPE_SOURCE"
    
    # Test if it's a valid shared library
    if file "$FREETYPE_410_TARGET" | grep -q "shared object"; then
        echo -e "${GREEN}✓${NC} Valid shared library"
    else
        echo -e "${YELLOW}⚠${NC}  Warning: File may not be a valid shared library"
    fi
else
    echo -e "${RED}✗ ERROR: File verification failed${NC}"
    exit 1
fi

# Restart service if it exists
echo ""
if systemctl list-unit-files | grep -q "^${SERVICE_NAME}.service"; then
    echo "Restarting $SERVICE_NAME service..."
    if systemctl restart "$SERVICE_NAME" 2>&1; then
        echo -e "${GREEN}✓${NC} Service restarted"
        sleep 2
        if systemctl is-active --quiet "$SERVICE_NAME"; then
            echo -e "${GREEN}✓${NC} Service is running"
        else
            echo -e "${YELLOW}⚠${NC}  Service may not be running. Check status:"
            echo "   sudo systemctl status $SERVICE_NAME"
        fi
    else
        echo -e "${YELLOW}⚠${NC}  Could not restart service. You may need to restart manually:"
        echo "   sudo systemctl restart $SERVICE_NAME"
    fi
else
    echo "Service $SERVICE_NAME not found. Skipping restart."
fi

echo ""
echo "=========================================="
echo -e "${GREEN}✓ Fix completed successfully!${NC}"
echo "=========================================="
echo ""
echo "File location: $FREETYPE_410_TARGET"
echo ""
echo "If you still experience issues:"
echo "  1. Check service status: sudo systemctl status $SERVICE_NAME"
echo "  2. Check service logs: sudo journalctl -u $SERVICE_NAME -n 50"
echo "  3. Verify library: ldd $INSTALL_DIR/bin/edgeos-api | grep freetype"
echo ""

