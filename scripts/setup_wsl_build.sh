#!/bin/bash
# ============================================================================
# Astro Mount Control - WSL2 Ubuntu Build Environment Setup
# ============================================================================
# This script installs all dependencies needed to build the astro_mount_control
# project on Ubuntu 22.04/24.04 under WSL2 (Windows Subsystem for Linux).
#
# Usage:
#   1. Install WSL2 on Windows (if not already done):
#      wsl --install -d Ubuntu-24.04
#   2. Launch WSL and run this script:
#      chmod +x scripts/setup_wsl_build.sh
#      ./scripts/setup_wsl_build.sh
#
# The project is expected at:
#   /mnt/c/Users/jacek/OneDrive/Documents/astro_mount_control
# But will be COPIED to ~/astro_mount_control for building (avoiding WSL
# filesystem permission issues).
# ============================================================================

set -euo pipefail

WINDOWS_PROJECT_DIR="/mnt/c/Users/jacek/OneDrive/Documents/astro_mount_control"
LINUX_PROJECT_DIR="$HOME/astro_mount_control"

echo "========================================"
echo " Astro Mount Control - Build Setup"
echo " Windows path: $WINDOWS_PROJECT_DIR"
echo " Linux  path:  $LINUX_PROJECT_DIR"
echo "========================================"

# ─── System Packages ────────────────────────────────────────────────────────

echo "[1/5] Installing system packages..."
sudo apt-get update -qq
sudo apt-get install -y -qq \
    build-essential \
    cmake \
    g++ \
    git \
    pkg-config \
    libgrpc++-dev \
    protobuf-compiler-grpc \
    libprotobuf-dev \
    libeigen3-dev \
    libsqlite3-dev \
    libspdlog-dev \
    libfmt-dev \
    libssl-dev \
    libcurl4-openssl-dev \
    nlohmann-json3-dev \
    libgpiod-dev \
    libftdi1-dev \
    libhidapi-dev \
    uuid-dev \
    googletest \
    libgtest-dev \
    wget \
    curl

# ─── Qt5 for GUI (optional) ─────────────────────────────────────────────────

echo "[2/5] Installing Qt5 (optional - for Qt GUI)..."
sudo apt-get install -y -qq \
    qtbase5-dev \
    libqt5svg5-dev \
    qtcharts5-dev || echo "Qt5 installation optional, skipping..."

# ─── Fix gRPC CMake config ──────────────────────────────────────────────────

echo "[3/5] Locating gRPC CMake configuration..."
# On Ubuntu 24.04, libgrpc++-dev installs gRPCConfig.cmake at /usr/lib/cmake/grpc/
GPRC_CMAKE_DIR=$(find /usr -name "gRPCConfig.cmake" -type f 2>/dev/null | head -1 | xargs -I{} dirname {} 2>/dev/null || echo "")
if [ -z "$GPRC_CMAKE_DIR" ]; then
    echo "⚠ gRPCConfig.cmake not found after install."
    echo "  Checking known locations..."
    for dir in /usr/lib/cmake/grpc /usr/lib/x86_64-linux-gnu/cmake/grpc /usr/local/lib/cmake/grpc; do
        if [ -f "$dir/gRPCConfig.cmake" ]; then
            GPRC_CMAKE_DIR="$dir"
            echo "  ✓ Found at: $dir"
            break
        fi
    done
fi
echo "  gRPC config dir: ${GPRC_CMAKE_DIR:-not found}"

# ─── Copy project to Linux filesystem ───────────────────────────────────────

echo "[4/5] Copying project to Linux filesystem..."
if [ -d "$WINDOWS_PROJECT_DIR" ]; then
    echo "  Copying from $WINDOWS_PROJECT_DIR → $LINUX_PROJECT_DIR"
    rm -rf "$LINUX_PROJECT_DIR"
    mkdir -p "$LINUX_PROJECT_DIR"
    
    # Use rsync if available, otherwise cp
    if command -v rsync &>/dev/null; then
        rsync -a --exclude='build' --exclude='build-gui' --exclude='node_modules' \
              --exclude='.git' "$WINDOWS_PROJECT_DIR/" "$LINUX_PROJECT_DIR/"
    else
        cp -r "$WINDOWS_PROJECT_DIR" "$HOME/"
        # Clean up build artifacts from copy
        rm -rf "$LINUX_PROJECT_DIR/build" "$LINUX_PROJECT_DIR/build-gui" 2>/dev/null || true
    fi
    echo "  ✓ Project copied to $LINUX_PROJECT_DIR"
else
    echo "⚠ Windows project not found at $WINDOWS_PROJECT_DIR"
    echo "  Create symlink or copy manually:"
    echo "    cp -r /mnt/c/.../astro_mount_control ~/"
fi

# ─── Generate build script ──────────────────────────────────────────────────

echo "[5/5] Generating build script..."

cat > "$HOME/build_astro.sh" << 'BUILDSCRIPT'
#!/bin/bash
set -euo pipefail

PROJECT_DIR="$HOME/astro_mount_control"
BUILD_DIR="$PROJECT_DIR/build"

# Clear environment variables that may cause "math.h not found" on WSL
unset CPLUS_INCLUDE_PATH
unset C_INCLUDE_PATH
unset CPATH

# Find gRPC cmake config
GPRC_CMAKE_DIR=$(find /usr -name "gRPCConfig.cmake" -type f 2>/dev/null | head -1 | xargs -I{} dirname {} 2>/dev/null || echo "")
if [ -z "$GPRC_CMAKE_DIR" ]; then
    # Fallback: check known paths
    for dir in /usr/lib/cmake/grpc /usr/lib/x86_64-linux-gnu/cmake/grpc; do
        [ -f "$dir/gRPCConfig.cmake" ] && GPRC_CMAKE_DIR="$dir" && break
    done
fi

echo "=== Building Astro Mount Controller ==="
echo "  Project: $PROJECT_DIR"
echo "  Build:   $BUILD_DIR"
echo "  gRPC:    ${GPRC_CMAKE_DIR:-NOT FOUND}"
echo ""

cd "$PROJECT_DIR"
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

# Configure
CMAKE_ARGS="-DCMAKE_BUILD_TYPE=Release"
if [ -n "$GPRC_CMAKE_DIR" ]; then
    CMAKE_ARGS="$CMAKE_ARGS -DgRPC_DIR=$GPRC_CMAKE_DIR"
fi

echo "  cmake .. $CMAKE_ARGS"
cmake .. $CMAKE_ARGS

# Build
NUM_CORES=$(nproc)
echo ""
echo "Building with $NUM_CORES cores..."
make -j"$NUM_CORES"

echo ""
echo "=== Build Complete ==="
echo "Binaries: $BUILD_DIR/bin/"
echo ""
echo "To run:"
echo "  $BUILD_DIR/bin/astro_mount_controller $PROJECT_DIR/config/default.json"
echo ""
echo "To run tests:"
echo "  $BUILD_DIR/bin/run_tests"
BUILDSCRIPT

chmod +x "$HOME/build_astro.sh"

echo ""
echo "========================================"
echo " Setup Complete!"
echo "========================================"
echo ""
echo "  ✓ Packages installed"
echo "  ✓ Project copied to: $LINUX_PROJECT_DIR"
echo "  ✓ Build script at:   $HOME/build_astro.sh"
echo ""
echo "To build the project, run:"
echo ""
echo "  $HOME/build_astro.sh"
echo ""
echo "Or manually:"
echo ""
echo "  cd $LINUX_PROJECT_DIR"
echo "  unset CPLUS_INCLUDE_PATH C_INCLUDE_PATH CPATH"
echo "  mkdir -p build && cd build"
echo '  cmake .. -DgRPC_DIR=/usr/lib/cmake/grpc -DCMAKE_BUILD_TYPE=Release'
echo "  make -j\$(nproc)"
echo ""
echo "NOTE: Building on the Linux filesystem (~/) avoids permission errors"
echo "on /mnt/c/ (Windows mount). Source files are synced from Windows."
echo ""
