#!/bin/bash
# ============================================
# Edge AI API - Create Directories Script
# ============================================
#
# Script để tự động tạo các thư mục cần thiết từ directories.conf
#
# Usage:
#   ./scripts/create_directories.sh [INSTALL_DIR] [--full-permissions]
#
# Options:
#   INSTALL_DIR          - Thư mục cài đặt (default: /opt/omniapi)
#   --full-permissions   - Set quyền 755 cho tất cả thư mục (thay vì permissions trong config)
#
# Examples:
#   ./scripts/create_directories.sh
#   ./scripts/create_directories.sh /opt/omniapi
#   ./scripts/create_directories.sh /opt/omniapi --full-permissions
#
# ============================================

set -euo pipefail

# Default values
INSTALL_DIR="/opt/omniapi"
FULL_PERMISSIONS=false

# Parse arguments
while [ $# -gt 0 ]; do
    case "$1" in
        --full-permissions)
            FULL_PERMISSIONS=true
            shift
            ;;
        --help|-h)
            echo "Usage: $0 [INSTALL_DIR] [--full-permissions]"
            echo
            echo "Options:"
            echo "  INSTALL_DIR          - Thư mục cài đặt (default: /opt/omniapi)"
            echo "  --full-permissions   - Set quyền 755 cho tất cả thư mục"
            echo
            echo "Examples:"
            echo "  $0"
            echo "  $0 /opt/omniapi"
            echo "  $0 /opt/omniapi --full-permissions"
            exit 0
            ;;
        -*)
            echo "Unknown option: $1" >&2
            echo "Use --help for usage information" >&2
            exit 1
            ;;
        *)
            # First non-option argument is INSTALL_DIR
            if [ "$INSTALL_DIR" = "/opt/omniapi" ]; then
                INSTALL_DIR="$1"
            else
                echo "Warning: Multiple INSTALL_DIR arguments, using first: $INSTALL_DIR" >&2
            fi
            shift
            ;;
    esac
done

# Get script directory
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

# Find directories.conf
DIRS_CONF=""
if [ -f "$PROJECT_ROOT/deploy/directories.conf" ]; then
    DIRS_CONF="$PROJECT_ROOT/deploy/directories.conf"
elif [ -f "$SCRIPT_DIR/directories.conf" ]; then
    DIRS_CONF="$SCRIPT_DIR/directories.conf"
else
    echo "Error: directories.conf not found" >&2
    echo "Expected locations:" >&2
    echo "  - $PROJECT_ROOT/deploy/directories.conf" >&2
    echo "  - $SCRIPT_DIR/directories.conf" >&2
    exit 1
fi

echo "============================================"
echo "Edge AI API - Create Directories"
echo "============================================"
echo "Install directory: $INSTALL_DIR"
echo "Config file: $DIRS_CONF"
if [ "$FULL_PERMISSIONS" = true ]; then
    echo "Mode: Full permissions (755 for all directories)"
else
    echo "Mode: Custom permissions (from config)"
fi
echo "============================================"
echo

# Load configuration
if [ ! -f "$DIRS_CONF" ]; then
    echo "Error: Configuration file not found: $DIRS_CONF" >&2
    exit 1
fi

# Source the config file to get APP_DIRECTORIES array
# Use a subshell approach to properly load the associative array
declare -A APP_DIRECTORIES=()

# Create a temporary script to source the config and export the array
TEMP_SCRIPT=$(mktemp)
cat > "$TEMP_SCRIPT" << 'TEMP_EOF'
source "$1"
declare -p APP_DIRECTORIES
TEMP_EOF

# Execute the temp script and capture output
CONFIG_OUTPUT=$(bash "$TEMP_SCRIPT" "$DIRS_CONF" 2>/dev/null)
rm -f "$TEMP_SCRIPT"

# Evaluate the output to populate APP_DIRECTORIES
if [ -n "$CONFIG_OUTPUT" ]; then
    eval "$CONFIG_OUTPUT"
else
    echo "Error: Failed to load APP_DIRECTORIES from $DIRS_CONF" >&2
    exit 1
fi

# Verify array is populated
if [ ${#APP_DIRECTORIES[@]} -eq 0 ]; then
    echo "Error: APP_DIRECTORIES is empty in $DIRS_CONF" >&2
    exit 1
fi

# Create each directory
CREATED_COUNT=0
EXISTING_COUNT=0
FAILED_COUNT=0

for dir_name in "${!APP_DIRECTORIES[@]}"; do
    dir_path="$INSTALL_DIR/$dir_name"
    dir_perms="${APP_DIRECTORIES[$dir_name]}"

    # Override permissions if --full-permissions flag is set
    if [ "$FULL_PERMISSIONS" = true ]; then
        dir_perms="755"
    fi

    # Create directory
    if [ -d "$dir_path" ]; then
        echo "✓ Directory already exists: $dir_path"
        EXISTING_COUNT=$((EXISTING_COUNT + 1))
    else
        if mkdir -p "$dir_path" 2>/dev/null; then
            echo "✓ Created directory: $dir_path"
            CREATED_COUNT=$((CREATED_COUNT + 1))
        else
            echo "✗ Failed to create directory: $dir_path" >&2
            FAILED_COUNT=$((FAILED_COUNT + 1))
            continue
        fi
    fi

    # Set permissions
    if [ -n "$dir_perms" ] && [ "$dir_perms" != "0" ]; then
        if chmod "$dir_perms" "$dir_path" 2>/dev/null; then
            echo "  → Set permissions: $dir_perms"
        else
            echo "  ⚠ Warning: Failed to set permissions $dir_perms for $dir_path" >&2
        fi
    fi
done

echo
echo "============================================"
echo "Summary:"
echo "  Created:    $CREATED_COUNT"
echo "  Existing:   $EXISTING_COUNT"
echo "  Failed:     $FAILED_COUNT"
echo "============================================"

if [ $FAILED_COUNT -gt 0 ]; then
    echo
    echo "⚠ Some directories failed to create. You may need to run with sudo:" >&2
    echo "  sudo $0 $INSTALL_DIR $([ "$FULL_PERMISSIONS" = true ] && echo "--full-permissions")" >&2
    exit 1
fi

echo
echo "✓ All directories created successfully!"
exit 0
