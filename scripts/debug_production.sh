#!/bin/bash
# ============================================
# Debug Production Issues - Compare Build vs Service
# ============================================
#
# Script này so sánh environment và paths giữa:
# - Chạy trực tiếp từ build directory
# - Chạy từ systemd service (sau khi cài .deb)
#
# Usage:
#   ./scripts/debug_production.sh
#
# ============================================

set -e

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

SERVICE_NAME="edge-ai-api"
BUILD_DIR="build"
PROD_BIN="/usr/local/bin/edgeos-api"
PROD_WORKER="/usr/local/bin/edgeos-worker"
INSTALL_DIR="/opt/edgeos-api"

echo -e "${BLUE}========================================${NC}"
echo -e "${BLUE}Debug Production Issues${NC}"
echo -e "${BLUE}========================================${NC}"
echo ""

# ============================================
# 1. Check Service Status
# ============================================
echo -e "${BLUE}[1/8]${NC} Checking service status..."
if systemctl is-active --quiet "$SERVICE_NAME" 2>/dev/null; then
    echo -e "${GREEN}✓${NC} Service is running"
else
    echo -e "${RED}✗${NC} Service is not running"
    echo "  Start with: sudo systemctl start $SERVICE_NAME"
fi
echo ""

# ============================================
# 2. Compare Working Directories
# ============================================
echo -e "${BLUE}[2/8]${NC} Comparing working directories..."
BUILD_WD="$(pwd)/$BUILD_DIR"
SERVICE_WD=$(systemctl show "$SERVICE_NAME" --property=WorkingDirectory --value 2>/dev/null || echo "")

echo "  Build directory: $BUILD_WD"
echo "  Service WorkingDirectory: ${SERVICE_WD:-not set}"
if [ -n "$SERVICE_WD" ] && [ "$SERVICE_WD" != "$BUILD_WD" ]; then
    echo -e "${YELLOW}⚠${NC}  Working directories differ!"
fi
echo ""

# ============================================
# 3. Check Instances Directory
# ============================================
echo -e "${BLUE}[3/8]${NC} Checking instances directory..."

BUILD_INSTANCES="$BUILD_WD/instances"
PROD_INSTANCES="$INSTALL_DIR/instances"

echo "  Build: $BUILD_INSTANCES"
if [ -d "$BUILD_INSTANCES" ]; then
    echo -e "    ${GREEN}✓${NC} Exists"
    if [ -f "$BUILD_INSTANCES/instances.json" ]; then
        BUILD_COUNT=$(cat "$BUILD_INSTANCES/instances.json" | jq 'length' 2>/dev/null || echo "N/A")
        echo "    Instances count: $BUILD_COUNT"
    fi
else
    echo -e "    ${RED}✗${NC} Not found"
fi

echo "  Production: $PROD_INSTANCES"
if sudo test -d "$PROD_INSTANCES"; then
    echo -e "    ${GREEN}✓${NC} Exists"
    if sudo test -f "$PROD_INSTANCES/instances.json"; then
        PROD_COUNT=$(sudo cat "$PROD_INSTANCES/instances.json" | jq 'length' 2>/dev/null || echo "N/A")
        echo "    Instances count: $PROD_COUNT"
    else
        echo -e "    ${YELLOW}⚠${NC}  instances.json not found"
    fi
    
    # Check permissions
    PROD_OWNER=$(sudo stat -c '%U:%G' "$PROD_INSTANCES" 2>/dev/null || echo "")
    PROD_PERMS=$(sudo stat -c '%a' "$PROD_INSTANCES" 2>/dev/null || echo "")
    echo "    Owner: $PROD_OWNER, Perms: $PROD_PERMS"
else
    echo -e "    ${RED}✗${NC} Not found"
    echo "    Fix: sudo mkdir -p $PROD_INSTANCES && sudo chown edgeai:edgeai $PROD_INSTANCES"
fi
echo ""

# ============================================
# 4. Check Binaries
# ============================================
echo -e "${BLUE}[4/8]${NC} Checking binaries..."

echo "  Main executable:"
if [ -f "$BUILD_DIR/bin/edgeos-api" ]; then
    BUILD_SIZE=$(stat -c%s "$BUILD_DIR/bin/edgeos-api" 2>/dev/null || echo "N/A")
    echo -e "    Build: ${GREEN}✓${NC} ($BUILD_SIZE bytes)"
else
    echo -e "    Build: ${RED}✗${NC} Not found"
fi

if [ -f "$PROD_BIN" ]; then
    PROD_SIZE=$(stat -c%s "$PROD_BIN" 2>/dev/null || echo "N/A")
    echo -e "    Production: ${GREEN}✓${NC} ($PROD_SIZE bytes)"
    
    # Check RPATH
    if command -v patchelf >/dev/null 2>&1; then
        RPATH=$(patchelf --print-rpath "$PROD_BIN" 2>/dev/null || echo "not set")
        echo "    RPATH: $RPATH"
    fi
else
    echo -e "    Production: ${RED}✗${NC} Not found"
fi

echo "  Worker executable:"
if [ -f "$BUILD_DIR/bin/edgeos-worker" ]; then
    echo -e "    Build: ${GREEN}✓${NC}"
else
    echo -e "    Build: ${RED}✗${NC} Not found"
fi

if [ -f "$PROD_WORKER" ]; then
    echo -e "    Production: ${GREEN}✓${NC}"
else
    echo -e "    Production: ${RED}✗${NC} Not found"
    echo "    Fix: Check if worker was built and installed correctly"
fi
echo ""

# ============================================
# 5. Check Libraries
# ============================================
echo -e "${BLUE}[5/8]${NC} Checking libraries..."

PROD_LIB="$INSTALL_DIR/lib"
if sudo test -d "$PROD_LIB"; then
    LIB_COUNT=$(sudo find "$PROD_LIB" -name "*.so*" | wc -l)
    echo -e "  Production libs: ${GREEN}✓${NC} ($LIB_COUNT files)"
    
    # Check for missing libraries
    if command -v ldd >/dev/null 2>&1 && [ -f "$PROD_BIN" ]; then
        MISSING=$(ldd "$PROD_BIN" 2>/dev/null | grep "not found" || true)
        if [ -n "$MISSING" ]; then
            echo -e "  ${RED}✗${NC} Missing libraries:"
            echo "$MISSING" | sed 's/^/    /'
        else
            echo -e "  ${GREEN}✓${NC} All libraries found"
        fi
    fi
else
    echo -e "  ${RED}✗${NC} Library directory not found: $PROD_LIB"
fi
echo ""

# ============================================
# 6. Check Environment Variables
# ============================================
echo -e "${BLUE}[6/8]${NC} Checking environment variables..."

SERVICE_ENV=$(systemctl show "$SERVICE_NAME" --property=Environment --value 2>/dev/null || echo "")

echo "  Critical environment variables:"
for VAR in "EDGE_AI_WORKER_PATH" "EDGE_AI_SOCKET_DIR" "EDGE_AI_EXECUTION_MODE" "INSTANCES_DIR"; do
    VALUE=$(echo "$SERVICE_ENV" | grep -oP "(?<=${VAR}=)[^ ]*" || echo "")
    if [ -n "$VALUE" ]; then
        echo -e "    ${GREEN}✓${NC} $VAR=$VALUE"
    else
        echo -e "    ${YELLOW}⚠${NC}  $VAR not set"
    fi
done
echo ""

# ============================================
# 7. Check Socket Directory
# ============================================
echo -e "${BLUE}[7/8]${NC} Checking socket directory..."

SOCKET_DIR="$INSTALL_DIR/run"
if sudo test -d "$SOCKET_DIR"; then
    echo -e "  ${GREEN}✓${NC} Exists: $SOCKET_DIR"
    
    SOCKET_OWNER=$(sudo stat -c '%U:%G' "$SOCKET_DIR" 2>/dev/null || echo "")
    SOCKET_PERMS=$(sudo stat -c '%a' "$SOCKET_DIR" 2>/dev/null || echo "")
    echo "    Owner: $SOCKET_OWNER, Perms: $SOCKET_PERMS"
    
    SOCKET_COUNT=$(sudo find "$SOCKET_DIR" -name "*.sock" 2>/dev/null | wc -l)
    echo "    Active sockets: $SOCKET_COUNT"
else
    echo -e "  ${RED}✗${NC} Not found: $SOCKET_DIR"
    echo "    Fix: sudo mkdir -p $SOCKET_DIR && sudo chown edgeai:edgeai $SOCKET_DIR"
fi
echo ""

# ============================================
# 8. Check Recent Logs
# ============================================
echo -e "${BLUE}[8/8]${NC} Checking recent logs..."

if systemctl is-active --quiet "$SERVICE_NAME" 2>/dev/null; then
    echo "  Last 10 log lines:"
    sudo journalctl -u "$SERVICE_NAME" -n 10 --no-pager | tail -5 | sed 's/^/    /'
    
    # Check for errors
    ERROR_COUNT=$(sudo journalctl -u "$SERVICE_NAME" --since "10 minutes ago" | grep -i "error\|failed\|✗" | wc -l)
    if [ "$ERROR_COUNT" -gt 0 ]; then
        echo -e "  ${RED}✗${NC} Found $ERROR_COUNT errors in last 10 minutes"
        echo "    View with: sudo journalctl -u $SERVICE_NAME -n 50 | grep -i error"
    else
        echo -e "  ${GREEN}✓${NC} No recent errors"
    fi
else
    echo -e "  ${YELLOW}⚠${NC}  Service not running, cannot check logs"
fi
echo ""

# ============================================
# Summary
# ============================================
echo "=========================================="
echo -e "${BLUE}Summary${NC}"
echo "=========================================="
echo ""
echo "Next steps:"
echo "  1. Check service logs: sudo journalctl -u $SERVICE_NAME -f"
echo "  2. Fix any issues found above"
echo "  3. Restart service: sudo systemctl restart $SERVICE_NAME"
echo "  4. Run this script again to verify"
echo ""

