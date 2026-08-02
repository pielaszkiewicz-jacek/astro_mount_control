#!/bin/bash
set -euo pipefail

# Fix WSL environment issues
unset CPLUS_INCLUDE_PATH C_INCLUDE_PATH CPATH

PROJECT_NAME="astro_mount_control"
WIN_PATH="/mnt/c/Users/jacek/OneDrive/Documents/$PROJECT_NAME"
LIN_PATH="$HOME/$PROJECT_NAME"

echo "=== Astro Mount Controller - WSL Build ==="
echo ""

# Step 1: Copy to Linux filesystem
if [ -d "$WIN_PATH" ]; then
    echo "[1/4] Copying project to Linux filesystem..."
    rm -rf "$LIN_PATH"
    cp -r "$WIN_PATH" "$HOME/"
    rm -rf "$LIN_PATH/build" "$LIN_PATH/build-gui" 2>/dev/null || true
    echo "  -> $LIN_PATH"
else
    echo "ERROR: Windows path not found: $WIN_PATH"
    exit 1
fi

# Step 2: Find gRPC config
echo "[2/4] Locating gRPC..."
GPRC_DIR=""
for d in /usr/lib/cmake/grpc /usr/lib/x86_64-linux-gnu/cmake/grpc; do
    if [ -f "$d/gRPCConfig.cmake" ]; then
        GPRC_DIR="$d"
        break
    fi
done
if [ -n "$GPRC_DIR" ]; then
    echo "  gRPC config: $GPRC_DIR"
else
    echo "  WARNING: gRPCConfig.cmake not found!"
fi

# Step 3: CMake configure
echo "[3/4] Configuring with CMake..."
cd "$LIN_PATH"
mkdir -p build
cd build

CMAKE_OPTS="-DCMAKE_BUILD_TYPE=Release"
[ -n "$GPRC_DIR" ] && CMAKE_OPTS="$CMAKE_OPTS -DgRPC_DIR=$GPRC_DIR"

echo "  cmake .. $CMAKE_OPTS"
cmake .. $CMAKE_OPTS 2>&1

# Step 4: Build
echo "[4/4] Building..."
make -j$(nproc) 2>&1

echo ""
echo "=== DONE ==="
echo "Binaries: $LIN_PATH/build/bin/"
echo "Run:      $LIN_PATH/build/bin/astro_mount_controller $LIN_PATH/config/default.json"
