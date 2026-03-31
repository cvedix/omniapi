#!/bin/bash
# ============================================
# Build Debian Package - All-in-One Script
# ============================================
#
# Script này làm tất cả: build project, bundle libraries, và tạo file .deb
# Chỉ cần chạy một lần: ./build_deb.sh
#
# Usage:
#   ./build_deb.sh [options]
#
# Options:
#   --clean        Clean build directory trước khi build
#   --no-build     Skip build (chỉ tạo package từ build có sẵn)
#   --version      Set version (default: 2025.0.1.3-Beta)
#   --help         Hiển thị help
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
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"

# Configuration
ARCH="amd64"
CLEAN_BUILD=false
SKIP_BUILD=false
AUTO_INCREMENT=true

# Function to read version from VERSION file or CMakeLists.txt
read_version() {
    local version_file="$PROJECT_ROOT/VERSION"
    local cmake_file="$PROJECT_ROOT/CMakeLists.txt"
    
    if [ -f "$version_file" ]; then
        cat "$version_file" | head -1 | tr -d '[:space:]'
    elif [ -f "$cmake_file" ]; then
        grep -E "project\(.*VERSION" "$cmake_file" | sed -E 's/.*VERSION[[:space:]]+([0-9.]+).*/\1/' | head -1
    else
        echo "2026.0.1.1"  # Default fallback
    fi
}

# Function to increment version (increment last number)
increment_version() {
    local version="$1"
    # Split version by dots
    IFS='.' read -ra PARTS <<< "$version"
    local major="${PARTS[0]}"
    local minor="${PARTS[1]}"
    local patch="${PARTS[2]}"
    local build="${PARTS[3]:-0}"
    
    # Increment build number (last number)
    build=$((build + 1))
    
    echo "$major.$minor.$patch.$build"
}

# Function to update version in all files
update_version_files() {
    local new_version="$1"
    local version_file="$PROJECT_ROOT/VERSION"
    local cmake_file="$PROJECT_ROOT/CMakeLists.txt"
    local changelog_file="$PROJECT_ROOT/debian/changelog"
    
    echo "Updating version to $new_version in all files..."
    
    # Update VERSION file
    echo "$new_version" > "$version_file"
    echo -e "${GREEN}✓${NC} Updated $version_file"
    
    # Update CMakeLists.txt
    if [ -f "$cmake_file" ]; then
        sed -i "s/project(omniapi VERSION [0-9.]*)/project(omniapi VERSION $new_version)/" "$cmake_file"
        echo -e "${GREEN}✓${NC} Updated CMakeLists.txt"
    fi
    
    # Update debian/changelog
    if [ -f "$changelog_file" ]; then
        # Update first line of changelog
        sed -i "1s/omniapi ([0-9.]*)/omniapi ($new_version)/" "$changelog_file"
        echo -e "${GREEN}✓${NC} Updated debian/changelog"
    fi
}

# Read current version
VERSION=$(read_version)

# Parse arguments
while [[ $# -gt 0 ]]; do
    case $1 in
        --clean)
            CLEAN_BUILD=true
            shift
            ;;
        --no-build)
            SKIP_BUILD=true
            shift
            ;;
        --version)
            VERSION="$2"
            AUTO_INCREMENT=false  # Don't auto-increment if version is manually set
            shift 2
            ;;
        --no-increment)
            AUTO_INCREMENT=false
            shift
            ;;
        --help|-h)
            echo "Usage: $0 [OPTIONS]"
            echo ""
            echo "Options:"
            echo "  --clean         Clean build directory before building"
            echo "  --no-build      Skip build (use existing build)"
            echo "  --version VER  Set package version (disables auto-increment)"
            echo "  --no-increment  Don't auto-increment version"
            echo "  --help, -h      Show this help"
            echo ""
            echo "Example:"
            echo "  ./build_deb.sh                    # Build với default settings"
            echo "  ./build_deb.sh --clean            # Clean và build lại từ đầu"
            echo "  ./build_deb.sh --version 1.0.0     # Build với version tùy chỉnh"
            exit 0
            ;;
        *)
            echo -e "${RED}Unknown option: $1${NC}"
            echo "Run '$0 --help' for usage information"
            exit 1
            ;;
    esac
done

# Auto-increment version if enabled
if [ "$AUTO_INCREMENT" = true ]; then
    OLD_VERSION="$VERSION"
    VERSION=$(increment_version "$VERSION")
    echo "=========================================="
    echo "Auto-Incrementing Version"
    echo "=========================================="
    echo "Old version: $OLD_VERSION"
    echo "New version: $VERSION"
    echo ""
    update_version_files "$VERSION"
    echo ""
fi

echo "=========================================="
echo "Build Debian Package - All-in-One"
echo "=========================================="
echo "Version: $VERSION"
echo "Architecture: $ARCH"
echo ""

cd "$PROJECT_ROOT"

# ============================================
# Function: Create bundle_libs.sh script
# ============================================
create_bundle_libs_script() {
    local BUNDLE_SCRIPT="$PROJECT_ROOT/debian/bundle_libs.sh"

    if [ ! -f "$BUNDLE_SCRIPT" ] || [ "$CLEAN_BUILD" = true ]; then
        echo "Creating bundle_libs.sh script..."
        cat > "$BUNDLE_SCRIPT" <<'BUNDLE_SCRIPT_EOF'
#!/bin/bash
# Bundle libraries for Debian package
# This script is auto-generated by build_deb.sh

set -e

EXEC_PATH="$1"
LIB_TEMP_DIR="$2"

if [ -z "$EXEC_PATH" ] || [ -z "$LIB_TEMP_DIR" ]; then
    echo "Usage: $0 <executable> <lib_dir>"
    exit 1
fi

if [ ! -f "$EXEC_PATH" ]; then
    echo "Error: Executable not found: $EXEC_PATH"
    exit 1
fi

mkdir -p "$LIB_TEMP_DIR"

echo "Bundling libraries from $EXEC_PATH..."

# Copy libraries from build directory
BUILD_LIB_DIR=$(dirname "$EXEC_PATH")/../lib
if [ -d "$BUILD_LIB_DIR" ]; then
    cp -L "$BUILD_LIB_DIR"/*.so* "$LIB_TEMP_DIR/" 2>/dev/null || true
fi

# EdgeOS SDK at /opt/edgeos-sdk
if [ -d "/opt/edgeos-sdk/lib/cvedix" ]; then
    cp -L /opt/edgeos-sdk/lib/cvedix/libcvedix*.so* "$LIB_TEMP_DIR/" 2>/dev/null || true
    cp -L /opt/edgeos-sdk/lib/cvedix/libtinyexpr.so* "$LIB_TEMP_DIR/" 2>/dev/null || true
fi

# Copy CUDA libraries if available (for GPU acceleration support)
# Detect CUDA installation from common paths
CUDA_LIB_PATHS=(
    "/usr/local/cuda/lib64"
    "/usr/local/cuda/lib"
    "/usr/lib/x86_64-linux-gnu"
    "/opt/cuda/lib64"
    "/opt/cuda/lib"
)

CUDA_LIBS=(
    "libcublas.so*"
    "libcublasLt.so*"
    "libcurand.so*"
    "libcusolver.so*"
    "libcufft.so*"
    "libcusparse.so*"
    "libcudart.so*"
    "libnvrtc.so*"
    "libnvjitlink.so*"
)

CUDA_FOUND=false
for cuda_path in "\${CUDA_LIB_PATHS[@]}"; do
    if [ -d "\$cuda_path" ]; then
        for cuda_lib_pattern in "\${CUDA_LIBS[@]}"; do
            # Find and copy CUDA libraries
            find "\$cuda_path" -maxdepth 1 -name "\$cuda_lib_pattern" -type f 2>/dev/null | while read cuda_lib; do
                if [ -f "\$cuda_lib" ]; then
                    libname=\$(basename "\$cuda_lib")
                    if [ ! -f "\$LIB_TEMP_DIR/\$libname" ]; then
                        echo "  Copying CUDA library \$libname from \$cuda_path..."
                        cp -L "\$cuda_lib" "\$LIB_TEMP_DIR/" 2>/dev/null || true
                    fi
                fi
            done
        done
        # Check if any CUDA libraries were found in this path
        if find "\$cuda_path" -maxdepth 1 -name "libcublas*.so*" -type f 2>/dev/null | grep -q .; then
            CUDA_FOUND=true
        fi
    fi
done

if [ "\$CUDA_FOUND" = true ]; then
    echo "  CUDA libraries bundled successfully"
fi

# Copy hwinfo libraries from build directory
# hwinfo libraries are built as part of the project but may not be in system paths
EXEC_DIR=\$(dirname "\$EXEC_PATH")
# Find build directory: go up from exec dir until we find a directory named "build" or use exec dir's parent
if [[ "\$EXEC_DIR" == *"/bin" ]]; then
    BUILD_DIR=\$(dirname "\$EXEC_DIR")
else
    BUILD_DIR="\$EXEC_DIR"
fi
HWINFO_LIB_PATHS=(
    "\$BUILD_DIR/_deps/hwinfo-build"
    "\$BUILD_DIR/third_party/hwinfo"
    "\$BUILD_DIR"
)
HWINFO_FOUND=false
for hwinfo_path in "\${HWINFO_LIB_PATHS[@]}"; do
    if [ -d "\$hwinfo_path" ]; then
        # Find all libhwinfo*.so* files
        while IFS= read -r hwinfo_lib; do
            if [ -f "\$hwinfo_lib" ]; then
                libname=\$(basename "\$hwinfo_lib")
                if [ ! -f "\$LIB_TEMP_DIR/\$libname" ]; then
                    echo "  Copying hwinfo library \$libname from \$hwinfo_path..."
                    cp -L "\$hwinfo_lib" "\$LIB_TEMP_DIR/" 2>/dev/null || true
                    HWINFO_FOUND=true
                fi
            fi
        done < <(find "\$hwinfo_path" -name "libhwinfo*.so*" -type f 2>/dev/null)
    fi
done

if [ "\$HWINFO_FOUND" = true ]; then
    echo "  hwinfo libraries bundled successfully"
fi

# Copy other required libraries from ldd output (including OpenCV and GStreamer for full package)
# Bundle all required libraries for a self-contained package
ldd "$EXEC_PATH" 2>/dev/null | grep -v "not found" | awk '{print $3}' | grep -v "^$" | sort -u | while read lib; do
    if [ -f "$lib" ]; then
        libname=$(basename "$lib")

        # Skip system libraries
        case "$libname" in
            libc.so*|libm.so*|libpthread.so*|libdl.so*|libgcc_s.so*|libstdc++.so*|ld-linux*)
                continue
                ;;
            *)
                if [ ! -f "$LIB_TEMP_DIR/$libname" ]; then
                    # Copy all required libraries including OpenCV and GStreamer
                    # Bundle from all paths (including /usr/lib, /lib/x86_64-linux-gnu, etc.)
                    cp -L "$lib" "$LIB_TEMP_DIR/" 2>/dev/null || true
                fi
                ;;
        esac
    fi
done

# Copy symlinks
for lib in "$LIB_TEMP_DIR"/*.so*; do
    if [ -L "$lib" ] 2>/dev/null; then
        target=$(readlink -f "$lib")
        if [ -f "$target" ] && [ ! -f "$LIB_TEMP_DIR/$(basename "$target")" ]; then
            cp -L "$target" "$LIB_TEMP_DIR/" 2>/dev/null || true
        fi
    fi
done

echo "Libraries bundled successfully."
BUNDLE_SCRIPT_EOF
        chmod +x "$BUNDLE_SCRIPT"
        echo -e "${GREEN}✓${NC} Created bundle_libs.sh"
    fi
}

# ============================================
# Function: Bundle Libraries
# ============================================
bundle_libraries() {
    local EXECUTABLE="$1"
    local LIB_DIR="$2"

    if [ -z "$EXECUTABLE" ] || [ -z "$LIB_DIR" ]; then
        echo -e "${RED}Error: bundle_libraries requires executable and lib_dir${NC}"
        return 1
    fi

    if [ ! -f "$EXECUTABLE" ]; then
        echo -e "${RED}Error: Executable not found: $EXECUTABLE${NC}"
        return 1
    fi

    mkdir -p "$LIB_DIR"

    echo "Bundling libraries for $(basename "$EXECUTABLE")..."

    # Copy libraries from build directory
    if [ -d "$(dirname "$EXECUTABLE")/../lib" ]; then
        BUILD_LIB_DIR="$(dirname "$EXECUTABLE")/../lib"
        echo "  Copying from build/lib..."
        cp -L "$BUILD_LIB_DIR"/*.so* "$LIB_DIR/" 2>/dev/null || true
    fi

    # Copy CVEDIX SDK libraries if available
    # Check both old and new SDK locations for compatibility
    if [ -d "/opt/edgeos-sdk/lib/cvedix" ]; then
        echo "  Copying EdgeOS SDK libraries from /opt/edgeos-sdk/lib/cvedix..."
        cp -L /opt/edgeos-sdk/lib/cvedix/libcvedix*.so* "$LIB_DIR/" 2>/dev/null || true
        cp -L /opt/edgeos-sdk/lib/cvedix/libtinyexpr.so* "$LIB_DIR/" 2>/dev/null || true
    fi

    # Copy CUDA libraries if available (for GPU acceleration support)
    CUDA_LIB_PATHS=(
        "/usr/local/cuda/lib64"
        "/usr/local/cuda/lib"
        "/usr/lib/x86_64-linux-gnu"
        "/opt/cuda/lib64"
        "/opt/cuda/lib"
    )

    CUDA_LIBS=(
        "libcublas.so*"
        "libcublasLt.so*"
        "libcurand.so*"
        "libcusolver.so*"
        "libcufft.so*"
        "libcusparse.so*"
        "libcudart.so*"
        "libnvrtc.so*"
        "libnvjitlink.so*"
    )

    CUDA_FOUND=false
    for cuda_path in "${CUDA_LIB_PATHS[@]}"; do
        if [ -d "$cuda_path" ]; then
            # Check if any CUDA libraries exist in this path
            for cuda_lib_pattern in "${CUDA_LIBS[@]}"; do
                if find "$cuda_path" -maxdepth 1 -name "$cuda_lib_pattern" -type f 2>/dev/null | grep -q .; then
                    CUDA_FOUND=true
                    # Find and copy CUDA libraries
                    find "$cuda_path" -maxdepth 1 -name "$cuda_lib_pattern" -type f 2>/dev/null | while read cuda_lib; do
                        if [ -f "$cuda_lib" ]; then
                            libname=$(basename "$cuda_lib")
                            if [ ! -f "$LIB_DIR/$libname" ]; then
                                echo "  Copying CUDA library $libname from $cuda_path..."
                                cp -L "$cuda_lib" "$LIB_DIR/" 2>/dev/null || true
                            fi
                        fi
                    done
                fi
            done
        fi
    done

    if [ "$CUDA_FOUND" = true ]; then
        echo "  CUDA libraries bundled successfully"
    fi

    # Copy hwinfo libraries from build directory
    # hwinfo libraries are built as part of the project but may not be in system paths
    EXEC_DIR=$(dirname "$EXECUTABLE")
    # Find build directory: go up from exec dir until we find a directory named "build" or use exec dir's parent
    if [[ "$EXEC_DIR" == *"/bin" ]]; then
        BUILD_DIR=$(dirname "$EXEC_DIR")
    else
        BUILD_DIR="$EXEC_DIR"
    fi
    HWINFO_LIB_PATHS=(
        "$BUILD_DIR/_deps/hwinfo-build"
        "$BUILD_DIR/third_party/hwinfo"
        "$BUILD_DIR"
    )
    HWINFO_FOUND=false
    for hwinfo_path in "${HWINFO_LIB_PATHS[@]}"; do
        if [ -d "$hwinfo_path" ]; then
            # Find all libhwinfo*.so* files
            while IFS= read -r hwinfo_lib; do
                if [ -f "$hwinfo_lib" ]; then
                    libname=$(basename "$hwinfo_lib")
                    if [ ! -f "$LIB_DIR/$libname" ]; then
                        echo "  Copying hwinfo library $libname from $hwinfo_path..."
                        cp -L "$hwinfo_lib" "$LIB_DIR/" 2>/dev/null || true
                        HWINFO_FOUND=true
                    fi
                fi
            done < <(find "$hwinfo_path" -name "libhwinfo*.so*" -type f 2>/dev/null)
        fi
    done

    if [ "$HWINFO_FOUND" = true ]; then
        echo "  hwinfo libraries bundled successfully"
    fi

    # Find and copy required libraries (including OpenCV and GStreamer for full package)
    REQUIRED_LIBS=$(ldd "$EXECUTABLE" 2>/dev/null | grep -v "not found" | awk '{print $3}' | grep -v "^$" | sort -u)

    for lib in $REQUIRED_LIBS; do
        if [ -f "$lib" ]; then
            libname=$(basename "$lib")

            # Skip system libraries
            if [[ "$libname" =~ ^(libc\.|libm\.|libpthread\.|libdl\.|libgcc_s\.|libstdc\+\+\.|ld-linux) ]]; then
                continue
            fi

            # Copy all required libraries including OpenCV and GStreamer
            if [ ! -f "$LIB_DIR/$libname" ]; then
                # Bundle from all paths (including /usr/lib, /lib/x86_64-linux-gnu, etc.)
                echo "  Copying $libname..."
                cp -L "$lib" "$LIB_DIR/" 2>/dev/null || true
            fi
        fi
    done

    # Copy symlinks
    for lib in "$LIB_DIR"/*.so*; do
        if [ -L "$lib" ] 2>/dev/null; then
            target=$(readlink -f "$lib")
            if [ -f "$target" ] && [ ! -f "$LIB_DIR/$(basename "$target")" ]; then
                cp -L "$target" "$LIB_DIR/" 2>/dev/null || true
            fi
        fi
    done

    echo -e "${GREEN}✓${NC} Libraries bundled to $LIB_DIR"
    local lib_count=$(ls -1 "$LIB_DIR"/*.so* 2>/dev/null | wc -l)
    echo "  Total libraries: $lib_count"
}

# ============================================
# Step 0: Create bundle_libs.sh script
# ============================================
create_bundle_libs_script

# ============================================
# Step 1: Check Build Dependencies
# ============================================
echo -e "${BLUE}[1/5]${NC} Checking build dependencies..."
MISSING_DEPS=()

# Check for commands
for dep in cmake make; do
    if ! command -v $dep &>/dev/null; then
        MISSING_DEPS+=($dep)
    fi
done

# Check for dpkg-buildpackage (from dpkg-dev package)
if ! command -v dpkg-buildpackage &>/dev/null; then
    MISSING_DEPS+=(dpkg-dev)
fi

# Check for debhelper (check for dh command or package)
if ! command -v dh &>/dev/null && ! dpkg -l | grep -q "^ii.*debhelper"; then
    MISSING_DEPS+=(debhelper)
fi

# Check for fakeroot
if ! command -v fakeroot &>/dev/null; then
    MISSING_DEPS+=(fakeroot)
fi

# Check build dependencies from debian/control if it exists
if [ -f "debian/control" ]; then
    # Extract Build-Depends (may span multiple lines)
    # Get all lines from Build-Depends: to the next field
    BUILD_DEPS_TEXT=$(awk '/^Build-Depends:/ {flag=1; sub(/^Build-Depends: */, ""); print; next} flag && /^[A-Z]/ {flag=0} flag {print}' debian/control | tr '\n' ' ')

    # Parse: remove version constraints, handle alternatives (|), split by comma
    # Remove version constraints like (>= 13), (>= 3.14)
    # Handle alternatives - take first option before |
    # Split by comma and clean up
    while IFS=',' read -ra DEPS; do
        for dep_raw in "${DEPS[@]}"; do
            # Remove version constraints
            dep=$(echo "$dep_raw" | sed 's/([^)]*)//g' | sed 's/|.*$//' | sed 's/^[[:space:]]*//;s/[[:space:]]*$//')

            # Skip empty entries
            [ -z "$dep" ] && continue

            # Check if it's a command (can be checked with command -v)
            if command -v "$dep" &>/dev/null; then
                continue
            fi

            # Check if it's an installed package (check exact package name)
            if ! dpkg-query -W -f='${Status}' "$dep" 2>/dev/null | grep -q "install ok installed"; then
                # Only add if not already in MISSING_DEPS
                found=false
                for existing_dep in "${MISSING_DEPS[@]}"; do
                    if [ "$existing_dep" = "$dep" ]; then
                        found=true
                        break
                    fi
                done
                if [ "$found" = false ]; then
                    MISSING_DEPS+=($dep)
                fi
            fi
        done
    done <<< "$BUILD_DEPS_TEXT"
fi

if [ ${#MISSING_DEPS[@]} -gt 0 ]; then
    echo -e "${RED}Error: Missing dependencies: ${MISSING_DEPS[*]}${NC}"
    echo ""
    echo "Install with:"
    echo "  sudo apt-get update"
    echo "  sudo apt-get install -y ${MISSING_DEPS[*]}"
    exit 1
fi
echo -e "${GREEN}✓${NC} All build dependencies available"
echo ""

# ============================================
# Step 2: Clean (if requested)
# ============================================
if [ "$CLEAN_BUILD" = true ]; then
    echo -e "${BLUE}[2/5]${NC} Cleaning build directory..."
    rm -rf build
    rm -rf debian/omniapi
    rm -f ../omniapi_*.deb ../omniapi_*.changes ../omniapi_*.buildinfo
    echo -e "${GREEN}✓${NC} Cleaned"
    echo ""
fi

# ============================================
# Step 3: Build Project
# ============================================
if [ "$SKIP_BUILD" = false ]; then
    echo -e "${BLUE}[3/5]${NC} Building project..."

    if [ ! -d "build" ]; then
        mkdir -p build
    fi

    cd build

    if [ ! -f "CMakeCache.txt" ]; then
        echo "Running CMake..."
        cmake .. -DCMAKE_BUILD_TYPE=Release \
                 -DAUTO_DOWNLOAD_DEPENDENCIES=ON \
                 -DDROGON_USE_FETCHCONTENT=ON
    fi

    echo "Building (using all CPU cores)..."
    CPU_CORES=$(nproc)
    echo "Using $CPU_CORES CPU cores..."
    make -j$CPU_CORES

    cd ..
    echo -e "${GREEN}✓${NC} Build completed"
    echo ""
else
    echo -e "${YELLOW}[3/5]${NC} Skipping build (using existing build)"
    echo ""
fi

# Check if executable exists - check multiple possible locations
EXECUTABLE=""
EXECUTABLE_PATHS=(
    "build/bin/omniapi"
    "build/omniapi"
    "build/omniapi/omniapi"
)

for path in "${EXECUTABLE_PATHS[@]}"; do
    if [ -f "$path" ] && [ -x "$path" ]; then
        EXECUTABLE="$path"
        break
    fi
done

if [ -z "$EXECUTABLE" ]; then
    echo -e "${RED}Error: Executable not found${NC}"
    echo "Checked the following locations:"
    for path in "${EXECUTABLE_PATHS[@]}"; do
        echo "  - $path"
    done
    echo ""
    echo "Please build the project first or remove --no-build flag"
    exit 1
fi

echo "Found executable: $EXECUTABLE"

# ============================================
# Step 4: Update Changelog
# ============================================
echo -e "${BLUE}[4/5]${NC} Updating changelog..."
if [ -f "debian/changelog" ]; then
    sed -i "s/omniapi (.*) unstable/omniapi ($VERSION) unstable/" debian/changelog
    echo -e "${GREEN}✓${NC} Changelog updated"
else
    echo -e "${YELLOW}⚠${NC}  debian/changelog not found, skipping..."
fi
echo ""

# ============================================
# Step 5: Build Debian Package
# ============================================
# Note: Library bundling is handled by debian/rules during install phase
echo -e "${BLUE}[5/5]${NC} Building Debian package..."

# Use TMPDIR on /home/cvedix/Data (larger partition) to avoid disk space issues
# This ensures all build tools (strip, dh_strip, etc.) use the larger partition
export TMPDIR="${TMPDIR:-/home/cvedix/Data/tmp}"
export TEMP="$TMPDIR"
export TMP="$TMPDIR"
mkdir -p "$TMPDIR"
echo "Using TMPDIR: $TMPDIR (available space: $(df -h "$TMPDIR" | tail -1 | awk '{print $4}'))"

export DEB_BUILD_OPTIONS="parallel=$(nproc)"

# Build package (dpkg-buildpackage expects to be run from project root)
echo "Running dpkg-buildpackage..."
dpkg-buildpackage -b -us -uc

# Find the generated .deb file
DEB_FILE=$(find .. -maxdepth 1 -name "omniapi_${VERSION}_${ARCH}.deb" -o -name "omniapi_*.deb" 2>/dev/null | head -1)

if [ -z "$DEB_FILE" ]; then
    # Try to find any .deb file in parent directory
    DEB_FILE=$(find .. -maxdepth 1 -name "*.deb" -type f 2>/dev/null | grep omniapi | head -1)
fi

if [ -z "$DEB_FILE" ]; then
    echo -e "${RED}Error: Could not find generated .deb file${NC}"
    echo "Check build output above for errors"
    exit 1
fi

# Get absolute path and move to project root with proper name
DEB_FILE=$(readlink -f "$DEB_FILE")
FINAL_NAME="omniapi-${VERSION}-${ARCH}.deb"

if [ "$(basename "$DEB_FILE")" != "$FINAL_NAME" ]; then
    mv "$DEB_FILE" "$PROJECT_ROOT/$FINAL_NAME"
    DEB_FILE="$PROJECT_ROOT/$FINAL_NAME"
else
    # Move to project root if not already there
    if [ "$(dirname "$DEB_FILE")" != "$PROJECT_ROOT" ]; then
        mv "$DEB_FILE" "$PROJECT_ROOT/$FINAL_NAME"
        DEB_FILE="$PROJECT_ROOT/$FINAL_NAME"
    fi
fi

# Clean up temporary files
rm -rf debian/omniapi
rm -f ../omniapi_*.changes ../omniapi_*.buildinfo 2>/dev/null || true

# Generate version manifest
if [ -f "$PROJECT_ROOT/packaging/scripts/generate_version_manifest.sh" ] && [ -f "$EXECUTABLE" ]; then
    echo ""
    echo "Generating version manifest..."
    MANIFEST_FILE="$PROJECT_ROOT/version_manifest_${VERSION}.txt"
    "$PROJECT_ROOT/packaging/scripts/generate_version_manifest.sh" "$EXECUTABLE" "$MANIFEST_FILE"
    if [ -f "$MANIFEST_FILE" ]; then
        echo -e "${GREEN}✓${NC} Version manifest: $MANIFEST_FILE"
    fi
fi

echo ""
echo "=========================================="
echo -e "${GREEN}✓ Package built successfully!${NC}"
echo "=========================================="
echo ""
echo "Package: $FINAL_NAME"
echo "Location: $DEB_FILE"
echo "Size: $(du -h "$DEB_FILE" | cut -f1)"
echo ""
echo "=========================================="
echo "Installation Instructions"
echo "=========================================="
echo ""
echo "1. Pre-installation check (optional):"
echo "   ./packaging/scripts/pre_install_check.sh"
echo ""
echo "2. Install package:"
echo "   sudo dpkg -i $FINAL_NAME"
echo ""
echo "3. If there are dependency issues:"
echo "   sudo apt-get install -f"
echo ""
echo "4. Verify installation:"
echo "   sudo /opt/omniapi/scripts/validate_installation.sh --verbose"
echo ""
echo "5. Start the service:"
echo "   sudo systemctl start omniapi"
echo ""
echo "6. Check status:"
echo "   sudo systemctl status omniapi"
echo ""
echo "=========================================="
echo "Reproducibility"
echo "=========================================="
echo ""
echo "This package is self-contained with all libraries bundled."
echo "For reproducibility guide, see:"
echo "  packaging/REPRODUCIBILITY.md"
echo ""
if [ -f "$MANIFEST_FILE" ]; then
    echo "Version manifest saved: $MANIFEST_FILE"
    echo "Use this to verify library versions match between test and production."
fi
echo ""
