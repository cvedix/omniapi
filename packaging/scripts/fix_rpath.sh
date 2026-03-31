#!/bin/bash
# ============================================
# Fix RPATH for Edge AI API Executables
# ============================================
# Script này sửa RPATH cho các executables để trỏ đến bundled libraries
# Usage: sudo ./fix_rpath.sh

set -e

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

# Configuration
BIN_DIR="/usr/local/bin"
EXPECTED_RPATH="/opt/omniapi/lib:/opt/edgeos-sdk/lib/cvedix"
EXECUTABLES=(
    "$BIN_DIR/omniapi"
    "$BIN_DIR/edgeos-worker"
)

log_info() {
    echo -e "${BLUE}[INFO]${NC} $1"
}

log_success() {
    echo -e "${GREEN}[✓]${NC} $1"
}

log_warning() {
    echo -e "${YELLOW}[⚠]${NC} $1"
}

log_error() {
    echo -e "${RED}[✗]${NC} $1"
}

# Check if running as root
if [ "$EUID" -ne 0 ]; then
    log_error "This script must be run as root (use sudo)"
    exit 1
fi

# Check if patchelf is available
if ! command -v patchelf >/dev/null 2>&1; then
    log_error "patchelf is not installed"
    log_info "Please install patchelf: sudo apt-get install -y patchelf"
    exit 1
fi

log_info "Fixing RPATH for Edge AI API executables..."
log_info "Expected RPATH: $EXPECTED_RPATH"
echo ""

FIXED_COUNT=0
SKIPPED_COUNT=0
ERROR_COUNT=0

for executable in "${EXECUTABLES[@]}"; do
    if [ ! -f "$executable" ]; then
        log_warning "Executable not found: $executable (skipping)"
        ((SKIPPED_COUNT++))
        continue
    fi
    
    log_info "Processing: $executable"
    
    # Get current RPATH
    CURRENT_RPATH=$(patchelf --print-rpath "$executable" 2>/dev/null || echo "")
    
    if [ -z "$CURRENT_RPATH" ]; then
        log_warning "  Current RPATH: not set"
    else
        log_info "  Current RPATH: $CURRENT_RPATH"
    fi
    
    # Check if RPATH needs fixing
    if [ "$CURRENT_RPATH" = "$EXPECTED_RPATH" ]; then
        log_success "  RPATH already correct, skipping"
        ((SKIPPED_COUNT++))
        continue
    fi
    
    # Check if RPATH contains build paths (definitely needs fixing)
    NEEDS_FIX=false
    if echo "$CURRENT_RPATH" | grep -qE "(build/lib|/home/|/tmp/|/var/tmp/)"; then
        log_warning "  RPATH contains build paths - needs fixing"
        NEEDS_FIX=true
    elif [ "$CURRENT_RPATH" != "$EXPECTED_RPATH" ]; then
        log_info "  RPATH differs from expected - updating"
        NEEDS_FIX=true
    fi
    
    if [ "$NEEDS_FIX" = true ]; then
        # Fix RPATH
        if patchelf --set-rpath "$EXPECTED_RPATH" "$executable" 2>/dev/null; then
            # Verify
            VERIFIED_RPATH=$(patchelf --print-rpath "$executable" 2>/dev/null || echo "")
            if [ "$VERIFIED_RPATH" = "$EXPECTED_RPATH" ]; then
                log_success "  RPATH fixed successfully: $VERIFIED_RPATH"
                ((FIXED_COUNT++))
            else
                log_error "  Failed to verify RPATH. Got: $VERIFIED_RPATH"
                ((ERROR_COUNT++))
            fi
        else
            log_error "  Failed to set RPATH"
            ((ERROR_COUNT++))
        fi
    fi
    echo ""
done

# Summary
echo "=========================================="
echo "Summary"
echo "=========================================="
echo "Fixed:   $FIXED_COUNT"
echo "Skipped: $SKIPPED_COUNT"
echo "Errors:  $ERROR_COUNT"

if [ $ERROR_COUNT -eq 0 ] && [ $FIXED_COUNT -gt 0 ]; then
    log_success "RPATH fix completed successfully!"
    exit 0
elif [ $ERROR_COUNT -eq 0 ]; then
    log_info "No changes needed - RPATH already correct"
    exit 0
else
    log_error "Some errors occurred during RPATH fix"
    exit 1
fi

