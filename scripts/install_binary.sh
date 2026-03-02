#!/bin/bash
# Script to install newly built binary to production

set -e

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
# Try build/bin first, then build/
if [ -f "$PROJECT_ROOT/build/bin/edgeos-api" ]; then
    BUILD_BINARY="$PROJECT_ROOT/build/bin/edgeos-api"
    BUILD_WORKER="$PROJECT_ROOT/build/bin/edgeos-worker"
elif [ -f "$PROJECT_ROOT/build/edgeos-api" ]; then
    BUILD_BINARY="$PROJECT_ROOT/build/edgeos-api"
    BUILD_WORKER="$PROJECT_ROOT/build/edgeos-worker"
else
    BUILD_BINARY=""
    BUILD_WORKER=""
fi
TARGET_BINARY="/usr/local/bin/edgeos-api"
TARGET_WORKER="/usr/local/bin/edgeos-worker"
SERVICE_NAME="edge-ai-api"

echo -e "${BLUE}========================================${NC}"
echo -e "${BLUE}Install New Binary to Production${NC}"
echo -e "${BLUE}========================================${NC}"
echo ""

# Check if running as root
if [ "$EUID" -ne 0 ]; then 
    echo -e "${RED}✗${NC} This script needs sudo privileges"
    echo "Please run: sudo $0"
    exit 1
fi

# Check if build binary exists
if [ ! -f "$BUILD_BINARY" ]; then
    echo -e "${RED}✗${NC} Build binary not found: $BUILD_BINARY"
    echo "Please build the project first:"
    echo "  cd $PROJECT_ROOT && mkdir -p build && cd build && cmake .. && make"
    exit 1
fi

# Check build date
BUILD_DATE=$(stat -c %y "$BUILD_BINARY" | cut -d' ' -f1)
TARGET_DATE=$(stat -c %y "$TARGET_BINARY" 2>/dev/null | cut -d' ' -f1 || echo "not found")

echo -e "${BLUE}[1/4]${NC} Checking binary versions..."
echo "  Build binary: $BUILD_BINARY"
echo "    Modified: $BUILD_DATE"
echo "  Target binary: $TARGET_BINARY"
echo "    Modified: $TARGET_DATE"
echo ""

if [ "$BUILD_DATE" = "$TARGET_DATE" ] && [ -f "$TARGET_BINARY" ]; then
    echo -e "${YELLOW}⚠${NC}  Binary dates are the same. Are you sure you rebuilt?"
    read -p "Continue anyway? (y/N) " -n 1 -r
    echo
    if [[ ! $REPLY =~ ^[Yy]$ ]]; then
        exit 1
    fi
fi

# Stop service first
echo -e "${BLUE}[2/5]${NC} Stopping service..."
if systemctl is-active --quiet "$SERVICE_NAME.service" 2>/dev/null; then
    systemctl stop "$SERVICE_NAME.service"
    echo "  Waiting 2 seconds for service to stop..."
    sleep 2
    echo -e "${GREEN}✓${NC} Service stopped"
else
    echo -e "${YELLOW}⚠${NC}  Service is not running"
fi
echo ""

# Backup old binary
echo -e "${BLUE}[3/5]${NC} Backing up old binary..."
if [ -f "$TARGET_BINARY" ]; then
    BACKUP="$TARGET_BINARY.backup.$(date +%Y%m%d_%H%M%S)"
    cp "$TARGET_BINARY" "$BACKUP"
    echo -e "${GREEN}✓${NC} Old binary backed up to: $BACKUP"
else
    echo -e "${YELLOW}⚠${NC}  No existing binary to backup"
fi
echo ""

# Install new binary
echo -e "${BLUE}[4/5]${NC} Installing new binary..."
cp "$BUILD_BINARY" "$TARGET_BINARY"
chmod +x "$TARGET_BINARY"
chown root:root "$TARGET_BINARY"

# Also install worker if it exists
if [ -f "$BUILD_WORKER" ]; then
    echo "  Also installing edgeos-worker..."
    if [ -f "$TARGET_WORKER" ]; then
        BACKUP_WORKER="$TARGET_WORKER.backup.$(date +%Y%m%d_%H%M%S)"
        cp "$TARGET_WORKER" "$BACKUP_WORKER"
    fi
    cp "$BUILD_WORKER" "$TARGET_WORKER"
    chmod +x "$TARGET_WORKER"
    chown root:root "$TARGET_WORKER"
    echo -e "${GREEN}✓${NC} Worker binary installed"
fi

echo -e "${GREEN}✓${NC} Binary installed to $TARGET_BINARY"
echo ""

# Start service
echo -e "${BLUE}[5/5]${NC} Starting service..."
systemctl start "$SERVICE_NAME.service"
sleep 3
echo -e "${GREEN}✓${NC} Service started"
echo ""

echo -e "${BLUE}========================================${NC}"
echo -e "${BLUE}Verification:${NC}"
echo -e "${BLUE}========================================${NC}"
echo ""
echo "Service status:"
systemctl status "$SERVICE_NAME.service" --no-pager -l | head -10

echo ""
echo "Execution mode from logs:"
journalctl -u "$SERVICE_NAME.service" -n 50 --no-pager | grep -i "execution mode" | tail -1 || echo "  Not found in recent logs"

echo ""
echo -e "${GREEN}✓ Done!${NC}"
echo ""

