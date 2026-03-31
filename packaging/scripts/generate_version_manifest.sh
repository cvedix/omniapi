#!/bin/bash
# ============================================
# Generate Version Manifest
# ============================================
# Script này tạo file manifest chứa version của tất cả libraries
# Để đảm bảo reproducibility giữa test và production
#
# Usage: ./generate_version_manifest.sh <executable> <output_file>

set -e

EXECUTABLE="$1"
OUTPUT_FILE="$2"

if [ -z "$EXECUTABLE" ] || [ -z "$OUTPUT_FILE" ]; then
    echo "Usage: $0 <executable> <output_file>"
    exit 1
fi

if [ ! -f "$EXECUTABLE" ]; then
    echo "Error: Executable not found: $EXECUTABLE"
    exit 1
fi

echo "Generating version manifest for $EXECUTABLE..."
echo "Output: $OUTPUT_FILE"

# Get executable info
EXEC_INFO=$(file "$EXECUTABLE" 2>/dev/null || echo "unknown")
EXEC_SIZE=$(stat -c%s "$EXECUTABLE" 2>/dev/null || echo "unknown")
BUILD_DATE=$(date -u +"%Y-%m-%d %H:%M:%S UTC")

# Get library versions
get_lib_version() {
    local lib_path="$1"
    if [ ! -f "$lib_path" ]; then
        return
    fi
    
    # Try multiple methods to get version
    # Method 1: strings + grep for version patterns
    version=$(strings "$lib_path" 2>/dev/null | grep -E "([0-9]+\.[0-9]+\.[0-9]+|[0-9]+\.[0-9]+)" | head -1 || echo "")
    
    # Method 2: Check soname
    soname=$(objdump -p "$lib_path" 2>/dev/null | grep "SONAME" | awk '{print $2}' || echo "")
    
    # Method 3: Check file modification time
    mtime=$(stat -c%y "$lib_path" 2>/dev/null | cut -d' ' -f1 || echo "")
    
    echo "$version|$soname|$mtime"
}

# Create manifest
{
    echo "# OmniAPI Version Manifest"
    echo "# Generated: $BUILD_DATE"
    echo "#"
    echo "# This file contains version information for all bundled libraries"
    echo "# to ensure reproducibility between test and production environments"
    echo ""
    echo "EXECUTABLE_INFO=$EXEC_INFO"
    echo "EXECUTABLE_SIZE=$EXEC_SIZE"
    echo "BUILD_DATE=$BUILD_DATE"
    echo ""
    echo "# Library Versions"
    echo ""
    
    # Get libraries from ldd
    if command -v ldd >/dev/null 2>&1; then
        ldd "$EXECUTABLE" 2>/dev/null | grep -v "not found" | awk '{print $3}' | grep -v "^$" | sort -u | while read lib_path; do
            if [ -f "$lib_path" ]; then
                lib_name=$(basename "$lib_path")
                version_info=$(get_lib_version "$lib_path")
                echo "LIB_${lib_name}=${version_info}"
            fi
        done
    fi
    
    # Also check bundled libraries
    LIB_DIR=$(dirname "$EXECUTABLE")/../lib
    if [ -d "$LIB_DIR" ]; then
        find "$LIB_DIR" -name "*.so*" -type f | while read lib_path; do
            lib_name=$(basename "$lib_path")
            version_info=$(get_lib_version "$lib_path")
            echo "BUNDLED_${lib_name}=${version_info}"
        done
    fi
} > "$OUTPUT_FILE"

echo "Version manifest generated: $OUTPUT_FILE"
echo "Total entries: $(grep -c "^LIB_\|^BUNDLED_" "$OUTPUT_FILE" || echo "0")"

