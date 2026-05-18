#!/bin/bash
# build_external_deps.sh — Build all external dependencies for AstroMountController
#
# Usage:
#   sudo ./scripts/build_external_deps.sh [prefix]
#
# Default prefix: /usr/local
#
# This script builds all required external libraries from source.
# Run it when system packages are unavailable (e.g., older openSUSE,
# minimal distributions, or air-gapped environments).
#
# After running, configure the project with:
#   cmake .. -DCMAKE_PREFIX_PATH=<prefix>
#
set -euo pipefail

PREFIX="${1:-/usr/local}"
JOBS=$(nproc)

echo "=== Building all external dependencies for AstroMountController ==="
echo "Install prefix: ${PREFIX}"
echo "Parallel jobs:  ${JOBS}"
echo ""

# Verify prerequisites
for cmd in git cmake make gcc g++; do
    if ! command -v "$cmd" &>/dev/null; then
        echo "ERROR: Required tool '$cmd' not found."
        echo "Install build tools first:"
        echo "  openSUSE: sudo zypper install -y gcc-c++ gcc make cmake git pkg-config"
        echo "  Ubuntu:   sudo apt install -y build-essential cmake git pkg-config"
        exit 1
    fi
done

# Detect number of available CPUs for make
MAKE_OPTS="-j${JOBS}"

# Track time
START_TIME=$(date +%s)

# ──────────────────────────────────────────────
# 1. Protobuf
# ──────────────────────────────────────────────
echo ""
echo "━━━ [1/7] Building protobuf v21.12 ━━━"
if [ -d /tmp/protobuf ]; then
    rm -rf /tmp/protobuf
fi
git clone --recurse-submodules -b v21.12 \
    https://github.com/protocolbuffers/protobuf.git /tmp/protobuf
cd /tmp/protobuf
mkdir -p build && cd build
cmake .. \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX=${PREFIX} \
    -Dprotobuf_BUILD_TESTS=OFF \
    -Dprotobuf_BUILD_SHARED_LIBS=ON \
    -Dprotobuf_MSVC_STATIC_RUNTIME=OFF
make ${MAKE_OPTS}
sudo make install
sudo ldconfig
echo "✓ protobuf installed to ${PREFIX}"

# ──────────────────────────────────────────────
# 2. gRPC
# ──────────────────────────────────────────────
echo ""
echo "━━━ [2/7] Building gRPC v1.59.0 ━━━"
if [ -d /tmp/grpc ]; then
    rm -rf /tmp/grpc
fi
git clone --recurse-submodules -b v1.59.0 \
    https://github.com/grpc/grpc.git /tmp/grpc
cd /tmp/grpc
mkdir -p cmake/build && cd cmake/build
cmake ../.. \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX=${PREFIX} \
    -DgRPC_INSTALL=ON \
    -DgRPC_BUILD_TESTS=OFF \
    -DgRPC_BUILD_CSHARP_EXT=OFF \
    -DgRPC_BUILD_GRPC_PYTHON_EXT=OFF \
    -DgRPC_USE_PROTO_LIBRARY=ON \
    -DgRPC_ABSL_PROVIDER=module \
    -DgRPC_RE2_PROVIDER=module \
    -DgRPC_CARES_PROVIDER=module \
    -DgRPC_SSL_PROVIDER=package \
    -DgRPC_ZLIB_PROVIDER=package \
    -DgRPC_PROTOBUF_PROVIDER=package
make ${MAKE_OPTS}
sudo make install
sudo ldconfig
echo "✓ gRPC installed to ${PREFIX}"

# ──────────────────────────────────────────────
# 3. nlohmann/json
# ──────────────────────────────────────────────
echo ""
echo "━━━ [3/7] Building nlohmann/json v3.11.3 ━━━"
if [ -d /tmp/json ]; then
    rm -rf /tmp/json
fi
git clone -b v3.11.3 \
    https://github.com/nlohmann/json.git /tmp/json
cd /tmp/json
mkdir -p build && cd build
cmake .. \
    -DCMAKE_INSTALL_PREFIX=${PREFIX} \
    -DJSON_BuildTests=OFF
sudo make install
echo "✓ nlohmann/json installed to ${PREFIX}"

# ──────────────────────────────────────────────
# 4. Eigen3
# ──────────────────────────────────────────────
echo ""
echo "━━━ [4/7] Building Eigen3 3.4.0 ━━━"
if [ -d /tmp/eigen ]; then
    rm -rf /tmp/eigen
fi
git clone -b 3.4.0 \
    https://gitlab.com/libeigen/eigen.git /tmp/eigen
cd /tmp/eigen
mkdir -p build && cd build
cmake .. \
    -DCMAKE_INSTALL_PREFIX=${PREFIX}
sudo make install
echo "✓ Eigen3 installed to ${PREFIX}"

# ──────────────────────────────────────────────
# 5. fmt
# ──────────────────────────────────────────────
echo ""
echo "━━━ [5/7] Building fmt 10.2.1 ━━━"
if [ -d /tmp/fmt ]; then
    rm -rf /tmp/fmt
fi
git clone -b 10.2.1 \
    https://github.com/fmtlib/fmt.git /tmp/fmt
cd /tmp/fmt
mkdir -p build && cd build
cmake .. \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX=${PREFIX} \
    -DFMT_TEST=OFF \
    -DFMT_DOC=OFF
make ${MAKE_OPTS}
sudo make install
echo "✓ fmt installed to ${PREFIX}"

# ──────────────────────────────────────────────
# 6. spdlog
# ──────────────────────────────────────────────
echo ""
echo "━━━ [6/7] Building spdlog v1.13.0 ━━━"
if [ -d /tmp/spdlog ]; then
    rm -rf /tmp/spdlog
fi
git clone -b v1.13.0 \
    https://github.com/gabime/spdlog.git /tmp/spdlog
cd /tmp/spdlog
mkdir -p build && cd build
cmake .. \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX=${PREFIX} \
    -DSPDLOG_BUILD_TESTS=OFF \
    -DSPDLOG_BUILD_EXAMPLE=OFF \
    -DSPDLOG_FMT_EXTERNAL=ON
make ${MAKE_OPTS}
sudo make install
echo "✓ spdlog installed to ${PREFIX}"

# ──────────────────────────────────────────────
# 7. Google Test
# ──────────────────────────────────────────────
echo ""
echo "━━━ [7/7] Building Google Test v1.14.0 ━━━"
if [ -d /tmp/googletest ]; then
    rm -rf /tmp/googletest
fi
git clone -b v1.14.0 \
    https://github.com/google/googletest.git /tmp/googletest
cd /tmp/googletest
mkdir -p build && cd build
cmake .. \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX=${PREFIX} \
    -DBUILD_GMOCK=ON \
    -DBUILD_GTEST=ON \
    -DINSTALL_GTEST=ON
make ${MAKE_OPTS}
sudo make install
echo "✓ Google Test installed to ${PREFIX}"

# ──────────────────────────────────────────────
# Cleanup
# ──────────────────────────────────────────────
echo ""
echo "━━━ Cleaning up temporary files ━━━"
rm -rf /tmp/protobuf /tmp/grpc /tmp/json /tmp/eigen \
       /tmp/fmt /tmp/spdlog /tmp/googletest

# Summary
END_TIME=$(date +%s)
DURATION=$((END_TIME - START_TIME))
echo ""
echo "═══════════════════════════════════════════"
echo "  ✅ All dependencies built and installed!"
echo "  Prefix:  ${PREFIX}"
echo "  Duration: ${DURATION} seconds"
echo "═══════════════════════════════════════════"
echo ""
echo "Now configure the AstroMountController project:"
echo ""
echo "  cd <project-dir>"
echo "  mkdir -p build && cd build"
echo "  cmake .. -DCMAKE_BUILD_TYPE=Release \\"
echo "          -DCMAKE_PREFIX_PATH=${PREFIX}"
echo "  make -j\$(nproc)"
echo ""
