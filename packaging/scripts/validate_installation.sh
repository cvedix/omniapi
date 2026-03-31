#!/bin/bash
# ============================================
# Post-Installation Validation Script
# ============================================
# Script này verify tất cả dependencies và configuration sau khi cài package
# Đảm bảo môi trường production giống hệt môi trường test
#
# Usage: ./validate_installation.sh [--verbose]

set -e

VERBOSE=false
if [[ "$1" == "--verbose" ]] || [[ "$1" == "-v" ]]; then
    VERBOSE=true
fi

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

# Configuration
BIN_DIR="/usr/local/bin"
INSTALL_DIR="/opt/omniapi"
LIB_DIR="$INSTALL_DIR/lib"
SERVICE_NAME="omniapi"
EXECUTABLE="$BIN_DIR/omniapi"
WORKER="$BIN_DIR/edgeos-worker"

ERRORS=0
WARNINGS=0

# Helper functions
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

log_verbose() {
    if [ "$VERBOSE" = true ]; then
        echo -e "${BLUE}[VERBOSE]${NC} $1"
    fi
}

# ============================================
# 1. Verify Executables
# ============================================
echo ""
echo "=========================================="
echo "1. Verifying Executables"
echo "=========================================="

if [ -f "$EXECUTABLE" ] && [ -x "$EXECUTABLE" ]; then
    log_success "Main executable found: $EXECUTABLE"
    if [ "$VERBOSE" = true ]; then
        file_info=$(file "$EXECUTABLE")
        log_verbose "  File info: $file_info"
    fi
else
    log_error "Main executable not found or not executable: $EXECUTABLE"
fi

if [ -f "$WORKER" ] && [ -x "$WORKER" ]; then
    log_success "Worker executable found: $WORKER"
else
    log_warning "Worker executable not found: $WORKER (optional for in-process mode)"
fi

# ============================================
# 2. Verify Library Directory
# ============================================
echo ""
echo "=========================================="
echo "2. Verifying Bundled Libraries"
echo "=========================================="

if [ ! -d "$LIB_DIR" ]; then
    log_error "Library directory not found: $LIB_DIR"
else
    log_success "Library directory exists: $LIB_DIR"
    
    lib_count=$(find "$LIB_DIR" -name "*.so*" -type f 2>/dev/null | wc -l)
    if [ "$lib_count" -gt 0 ]; then
        log_success "Found $lib_count library files"
        if [ "$VERBOSE" = true ]; then
            echo "  Libraries:"
            find "$LIB_DIR" -name "*.so*" -type f -exec basename {} \; | sort | head -20 | while read lib; do
                echo "    - $lib"
            done
            if [ "$lib_count" -gt 20 ]; then
                echo "    ... and $((lib_count - 20)) more"
            fi
        fi
    else
        log_error "No libraries found in $LIB_DIR"
    fi
fi

# ============================================
# 3. Verify RPATH Configuration
# ============================================
echo ""
echo "=========================================="
echo "3. Verifying RPATH Configuration"
echo "=========================================="

EXPECTED_RPATH="/opt/omniapi/lib:/opt/edgeos-sdk/lib/cvedix"
RPATH_FIXED=false

if [ -f "$EXECUTABLE" ]; then
    rpath=""
    
    # Try patchelf first (most reliable)
    if command -v patchelf >/dev/null 2>&1; then
        rpath=$(patchelf --print-rpath "$EXECUTABLE" 2>/dev/null || echo "")
    # Fallback to readelf (usually available on most Linux systems)
    elif command -v readelf >/dev/null 2>&1; then
        rpath=$(readelf -d "$EXECUTABLE" 2>/dev/null | grep -i "rpath" | sed -n 's/.*\[\(.*\)\].*/\1/p' | head -1 || echo "")
    # Fallback to objdump (also usually available)
    elif command -v objdump >/dev/null 2>&1; then
        rpath=$(objdump -p "$EXECUTABLE" 2>/dev/null | grep -i "rpath" | sed 's/.*RPATH[[:space:]]*\(.*\)/\1/' | head -1 || echo "")
    fi
    
    if [ -n "$rpath" ]; then
        # Check if RPATH contains build paths (should not)
        if echo "$rpath" | grep -qE "(build/lib|/home/|/tmp/|/var/tmp/)"; then
            log_warning "RPATH contains build paths (should be production paths only): $rpath"
            log_info "Expected RPATH: $EXPECTED_RPATH"
            
            # Try to fix if patchelf is available
            if command -v patchelf >/dev/null 2>&1; then
                log_info "Attempting to fix RPATH..."
                if patchelf --set-rpath "$EXPECTED_RPATH" "$EXECUTABLE" 2>/dev/null; then
                    VERIFIED_RPATH=$(patchelf --print-rpath "$EXECUTABLE" 2>/dev/null || echo "")
                    if [ "$VERIFIED_RPATH" = "$EXPECTED_RPATH" ]; then
                        log_success "RPATH fixed successfully: $VERIFIED_RPATH"
                        RPATH_FIXED=true
                    else
                        log_error "Failed to fix RPATH. Current: $VERIFIED_RPATH, Expected: $EXPECTED_RPATH"
                    fi
                else
                    log_error "Failed to set RPATH using patchelf"
                    log_info "Please run manually: sudo patchelf --set-rpath '$EXPECTED_RPATH' $EXECUTABLE"
                fi
            else
                log_error "patchelf not found. Cannot fix RPATH automatically."
                log_info "Please install patchelf and run: sudo patchelf --set-rpath '$EXPECTED_RPATH' $EXECUTABLE"
            fi
        # Check if RPATH contains production paths
        elif echo "$rpath" | grep -q "/opt/omniapi/lib"; then
            # Verify it matches expected exactly
            if [ "$rpath" = "$EXPECTED_RPATH" ]; then
                log_success "RPATH correctly set: $rpath"
            else
                log_warning "RPATH contains production paths but may not be optimal: $rpath"
                log_info "Expected RPATH: $EXPECTED_RPATH"
                
                # Try to fix if patchelf is available
                if command -v patchelf >/dev/null 2>&1; then
                    log_info "Attempting to optimize RPATH..."
                    if patchelf --set-rpath "$EXPECTED_RPATH" "$EXECUTABLE" 2>/dev/null; then
                        VERIFIED_RPATH=$(patchelf --print-rpath "$EXECUTABLE" 2>/dev/null || echo "")
                        if [ "$VERIFIED_RPATH" = "$EXPECTED_RPATH" ]; then
                            log_success "RPATH optimized successfully: $VERIFIED_RPATH"
                            RPATH_FIXED=true
                        fi
                    fi
                fi
            fi
        else
            log_warning "RPATH may not point to bundled libraries: $rpath"
            log_info "Expected RPATH: $EXPECTED_RPATH"
            
            # Try to fix if patchelf is available
            if command -v patchelf >/dev/null 2>&1; then
                log_info "Attempting to set RPATH..."
                if patchelf --set-rpath "$EXPECTED_RPATH" "$EXECUTABLE" 2>/dev/null; then
                    VERIFIED_RPATH=$(patchelf --print-rpath "$EXECUTABLE" 2>/dev/null || echo "")
                    if [ "$VERIFIED_RPATH" = "$EXPECTED_RPATH" ]; then
                        log_success "RPATH set successfully: $VERIFIED_RPATH"
                        RPATH_FIXED=true
                    fi
                fi
            fi
        fi
        
        # Also check worker executable
        if [ -f "$WORKER" ]; then
            worker_rpath=""
            if command -v patchelf >/dev/null 2>&1; then
                worker_rpath=$(patchelf --print-rpath "$WORKER" 2>/dev/null || echo "")
            elif command -v readelf >/dev/null 2>&1; then
                worker_rpath=$(readelf -d "$WORKER" 2>/dev/null | grep -i "rpath" | sed -n 's/.*\[\(.*\)\].*/\1/p' | head -1 || echo "")
            fi
            
            if [ -n "$worker_rpath" ] && [ "$worker_rpath" != "$EXPECTED_RPATH" ]; then
                if command -v patchelf >/dev/null 2>&1; then
                    log_info "Fixing RPATH for worker executable..."
                    if patchelf --set-rpath "$EXPECTED_RPATH" "$WORKER" 2>/dev/null; then
                        log_success "Worker RPATH fixed"
                        RPATH_FIXED=true
                    fi
                fi
            fi
        fi
    else
        # Check if we have a tool to verify RPATH
        if command -v patchelf >/dev/null 2>&1 || command -v readelf >/dev/null 2>&1 || command -v objdump >/dev/null 2>&1; then
            log_warning "RPATH not set (may use LD_LIBRARY_PATH or system libraries)"
            log_info "Expected RPATH: $EXPECTED_RPATH"
            
            # Try to set if patchelf is available
            if command -v patchelf >/dev/null 2>&1; then
                log_info "Attempting to set RPATH..."
                if patchelf --set-rpath "$EXPECTED_RPATH" "$EXECUTABLE" 2>/dev/null; then
                    VERIFIED_RPATH=$(patchelf --print-rpath "$EXECUTABLE" 2>/dev/null || echo "")
                    if [ "$VERIFIED_RPATH" = "$EXPECTED_RPATH" ]; then
                        log_success "RPATH set successfully: $VERIFIED_RPATH"
                        RPATH_FIXED=true
                    fi
                fi
            fi
        else
            log_warning "No tools available to verify RPATH (patchelf/readelf/objdump not found)"
        fi
    fi
else
    log_warning "Executable not found, cannot verify RPATH"
fi

# ============================================
# 4. Verify Library Dependencies
# ============================================
echo ""
echo "=========================================="
echo "4. Verifying Library Dependencies"
echo "=========================================="

if [ -f "$EXECUTABLE" ] && command -v ldd >/dev/null 2>&1; then
    log_info "Checking library dependencies for $EXECUTABLE"
    
    missing_libs=$(ldd "$EXECUTABLE" 2>&1 | grep "not found" || true)
    if [ -n "$missing_libs" ]; then
        log_error "Missing libraries detected:"
        echo "$missing_libs" | while read line; do
            echo "    $line"
        done
    else
        log_success "All library dependencies resolved"
    fi
    
    if [ "$VERBOSE" = true ]; then
        log_verbose "Library dependencies:"
        ldd "$EXECUTABLE" 2>/dev/null | grep -v "not found" | while read line; do
            lib=$(echo "$line" | awk '{print $1}')
            path=$(echo "$line" | awk '{print $3}')
            if echo "$path" | grep -q "$LIB_DIR"; then
                echo "    ✓ $lib -> $path (bundled)"
            else
                echo "    - $lib -> $path (system)"
            fi
        done
    fi
else
    log_warning "Cannot verify library dependencies (ldd not available)"
fi

# ============================================
# 5. Verify Critical Libraries
# ============================================
echo ""
echo "=========================================="
echo "5. Verifying Critical Libraries"
echo "=========================================="

CRITICAL_LIBS=(
    "libdrogon"
    "libjsoncpp"
    "libopencv"
    "libgstreamer"
    "libcvedix"
    "libedgeos_core"
)

for lib_pattern in "${CRITICAL_LIBS[@]}"; do
    found=false
    for lib_file in "$LIB_DIR"/${lib_pattern}*.so*; do
        if [ -f "$lib_file" ]; then
            found=true
            log_success "$lib_pattern found: $(basename "$lib_file")"
            break
        fi
    done
    
    if [ "$found" = false ]; then
        # Check if it's a system library (acceptable for some libs)
        if ldd "$EXECUTABLE" 2>/dev/null | grep -q "$lib_pattern"; then
            log_warning "$lib_pattern not bundled (using system library)"
        else
            log_error "$lib_pattern not found (neither bundled nor system)"
        fi
    fi
done

# ============================================
# 6. Verify Configuration Files
# ============================================
echo ""
echo "=========================================="
echo "6. Verifying Configuration Files"
echo "=========================================="

CONFIG_FILES=(
    "$INSTALL_DIR/config/config.json"
    "$INSTALL_DIR/config/.env"
)

for config_file in "${CONFIG_FILES[@]}"; do
    if [ -f "$config_file" ]; then
        log_success "Config file exists: $config_file"
        if [ "$VERBOSE" = true ]; then
            if [ -r "$config_file" ]; then
                log_verbose "  Readable: Yes"
                log_verbose "  Size: $(stat -c%s "$config_file") bytes"
            else
                log_warning "  Readable: No"
            fi
        fi
    else
        log_warning "Config file not found: $config_file"
    fi
done

# ============================================
# 7. Verify Directory Structure
# ============================================
echo ""
echo "=========================================="
echo "7. Verifying Directory Structure"
echo "=========================================="

REQUIRED_DIRS=(
    "$INSTALL_DIR"
    "$INSTALL_DIR/config"
    "$INSTALL_DIR/instances"
    "$INSTALL_DIR/solutions"
    "$INSTALL_DIR/models"
    "$INSTALL_DIR/videos"
    "$INSTALL_DIR/logs"
    "$INSTALL_DIR/data"
    "$INSTALL_DIR/lib"
)

for dir in "${REQUIRED_DIRS[@]}"; do
    if [ -d "$dir" ]; then
        log_success "Directory exists: $dir"
    else
        log_error "Required directory missing: $dir"
    fi
done

# ============================================
# 8. Verify Systemd Service
# ============================================
echo ""
echo "=========================================="
echo "8. Verifying Systemd Service"
echo "=========================================="

SERVICE_FILE="/etc/systemd/system/${SERVICE_NAME}.service"
if [ -f "$SERVICE_FILE" ]; then
    log_success "Service file exists: $SERVICE_FILE"
    
    if systemctl is-enabled "$SERVICE_NAME" >/dev/null 2>&1; then
        log_success "Service is enabled"
    else
        log_warning "Service is not enabled"
    fi
    
    if systemctl is-active "$SERVICE_NAME" >/dev/null 2>&1; then
        log_success "Service is running"
    else
        log_warning "Service is not running"
    fi
else
    log_error "Service file not found: $SERVICE_FILE"
fi

# ============================================
# 9. Verify Environment Variables
# ============================================
echo ""
echo "=========================================="
echo "9. Verifying Environment Configuration"
echo "=========================================="

if [ -f "$INSTALL_DIR/config/.env" ]; then
    log_success ".env file exists"
    
    # Check for critical environment variables
    if grep -q "API_HOST" "$INSTALL_DIR/config/.env"; then
        log_success "API_HOST configured"
    else
        log_warning "API_HOST not found in .env"
    fi
    
    if grep -q "API_PORT" "$INSTALL_DIR/config/.env"; then
        log_success "API_PORT configured"
    else
        log_warning "API_PORT not found in .env"
    fi
    
    if grep -q "GST_PLUGIN_PATH" "$INSTALL_DIR/config/.env"; then
        gst_path=$(grep "GST_PLUGIN_PATH" "$INSTALL_DIR/config/.env" | cut -d'=' -f2)
        if [ -d "$gst_path" ]; then
            log_success "GST_PLUGIN_PATH configured and valid: $gst_path"
        else
            log_warning "GST_PLUGIN_PATH configured but directory not found: $gst_path"
        fi
    else
        log_warning "GST_PLUGIN_PATH not configured (may use system default)"
    fi
else
    log_warning ".env file not found"
fi

# ============================================
# 10. Test Executable Execution
# ============================================
echo ""
echo "=========================================="
echo "10. Testing Executable Execution"
echo "=========================================="

if [ -f "$EXECUTABLE" ]; then
    # Test if executable can at least show version/help
    if "$EXECUTABLE" --version >/dev/null 2>&1 || "$EXECUTABLE" --help >/dev/null 2>&1; then
        log_success "Executable can run (version/help test passed)"
    else
        # Try to check if it's a valid ELF binary
        if file "$EXECUTABLE" | grep -q "ELF"; then
            log_warning "Executable is valid ELF but --version/--help failed (may be normal)"
        else
            log_error "Executable appears to be invalid"
        fi
    fi
fi

# ============================================
# Summary
# ============================================
echo ""
echo "=========================================="
echo "Validation Summary"
echo "=========================================="
echo "Errors:   $ERRORS"
echo "Warnings: $WARNINGS"

if [ $ERRORS -eq 0 ] && [ $WARNINGS -eq 0 ]; then
    echo ""
    log_success "All checks passed! Installation is valid."
    exit 0
elif [ $ERRORS -eq 0 ]; then
    echo ""
    log_warning "Installation completed with warnings. Please review above."
    exit 0
else
    echo ""
    log_error "Installation validation failed. Please fix the errors above."
    exit 1
fi

