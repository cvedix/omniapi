#!/bin/bash
# ============================================
# Copy cvedix_data to /opt/omniapi/
# ============================================
#
# This script copies the contents of cvedix_data directory
# to /opt/omniapi/ directory.
#
# Usage:
#   ./scripts/copy_cvedix_data.sh
#   sudo ./scripts/copy_cvedix_data.sh  # If permissions required
#
# ============================================

set -e  # Exit on error

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Get script directory
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

# Source and destination paths
SOURCE_DIR="${PROJECT_ROOT}/cvedix_data"
DEST_DIR="/opt/omniapi"

# Function to print colored messages
print_info() {
    echo -e "${GREEN}[INFO]${NC} $1"
}

print_warn() {
    echo -e "${YELLOW}[WARN]${NC} $1"
}

print_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

# Check if source directory exists
if [ ! -d "$SOURCE_DIR" ]; then
    print_error "Source directory does not exist: $SOURCE_DIR"
    exit 1
fi

print_info "Source directory: $SOURCE_DIR"
print_info "Destination directory: $DEST_DIR"

# Check if destination directory exists, create if not
if [ ! -d "$DEST_DIR" ]; then
    print_warn "Destination directory does not exist. Creating: $DEST_DIR"
    if ! mkdir -p "$DEST_DIR" 2>/dev/null; then
        print_error "Failed to create destination directory. Try running with sudo."
        exit 1
    fi
    print_info "Destination directory created successfully."
else
    print_info "Destination directory already exists."
fi

# Check write permissions
if [ ! -w "$DEST_DIR" ]; then
    print_error "No write permission to $DEST_DIR"
    print_warn "You may need to run this script with sudo: sudo $0"
    exit 1
fi

# Copy contents
print_info "Copying contents from $SOURCE_DIR to $DEST_DIR..."
if cp -r "${SOURCE_DIR}"/* "${DEST_DIR}/" 2>/dev/null; then
    print_info "Contents copied successfully!"
    
    # List copied items
    echo ""
    print_info "Copied items:"
    ls -lh "$DEST_DIR" | tail -n +2 | awk '{print "  - " $9 " (" $5 ")"}'
    
    print_info "Copy operation completed successfully!"
else
    print_error "Failed to copy contents. Check permissions and try again."
    exit 1
fi

