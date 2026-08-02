#!/bin/bash
# Build Astro Mount Controller under WSL2
# Fixes:
#   - Permission denied on /mnt/c/ → build on Linux filesystem
#   - math.h not found → unset CPLUS_INCLUDE_PATH
#   - gRPC CMake config → locate gRPCConfig.cmake automatically
set -euo pipefail

PROJECT_NAME="astro_mount_control"
WINDOWS_PROJECT_DIR="/mnt/c/Users/jacek/OneDrive/Documents/$PROJECT_NAME"
LINUX_PROJECT_DIR="$HOME/$PROJECT_NAME"
BUILD_DIR="$LINUX_PROJECT_DIR/build"

echo "=== Building Astro Mount Controller (WSL2) ==="

# Clear environment variables that may cause "math.h not found" on WSL
unset CPLUS_INCLUDE_PATH
unset C_INCLUDE_PATH
unset CPATH

# Check if Linux copy exists, if not copy from Windows
if [ ! -d "$LINUX_PROJECT_DIR" ]; then
    if [ -d "$WINDOWS_PROJECT_DIR" ]; then
        echo "Copying project from Windows to Linux filesystem..."
        cp -r "$WINDOWS_PROJECT_DIR" "$HOME/"
        rm -rf "$LINUX_PROJECT_DIR/build" "$LINUX_PROJECT_DIR/build-gui" 2>/dev/null || true
        echo "✓ Copied to $LINUX_PROJECT_DIR"
    else
        echo "Error: Project not found at $WINDOWS_PROJECT_DIR"
        echo "Please check the path or copy manually."
        exit 1
    fi
fi

# Ensure we're in WSL
if ! grep -q Microsoft /proc/version 2>/dev/null; then
    echo "⚠ This script is designed for WSL2."
fi

# Find gRPC CMake config
GPRC_CMAKE_DIR=$(find /usr -name "gRPCConfig.cmake" -type f 2>/dev/null | head -1 | xargs -I{} dirname {} 2>/dev/null || echo "")
if [ -z "$GPRC_CMAKE_DIR" ]; then
    for dir in /usr/lib/cmake/grpc /usr/lib/x86_64-linux-gnu/cmake/grpc /usr/local/lib/cmake/grpc; do
        if [ -f "$dir/gRPCConfig.cmake" ]; then
            GPRC_CMAKE_DIR="$dir"
            break
        fi
    done
fi

# Create build directory
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

# Configure
CMAKE_ARGS=(
    -DCMAKE_BUILD_TYPE=Release
    -DCMAKE_INSTALL_PREFIX="$BUILD_DIR/install"
)
if [ -n "$GPRC_CMAKE_DIR" ]; then
    CMAKE_ARGS+=("-DgRPC_DIR=$GPRC_CMAKE_DIR")
fi

echo "Configuring with: cmake ${CMAKE_ARGS[*]}"
cmake "${CMAKE_ARGS[@]}" "$LINUX_PROJECT_DIR"

# Build
NUM_CORES=$(nproc)
echo ""
echo "Building with $NUM_CORES cores..."
make -j"$NUM_CORES"

echo ""
echo "=== Build Complete ==="
echo "Project: $LINUX_PROJECT_DIR"
echo "Build:   $BUILD_DIR"
echo "Binaries: $BUILD_DIR/bin/"
echo ""
echo "To run:"
echo "  $BUILD_DIR/bin/astro_mount_controller $LINUX_PROJECT_DIR/config/default.json"
echo ""
echo "To run tests:"
echo "  $BUILD_DIR/bin/run_tests"
echo ""
echo "NOTE: Source files are on Linux filesystem at $LINUX_PROJECT_DIR"
echo "To sync changes from Windows:"
echo "  rsync -a --exclude=build $WINDOWS_PROJECT_DIR/ $LINUX_PROJECT_DIR/"
