#!/bin/bash

# Astronomical Mount Controller Build Script
# This script builds the project and sets up the development environment

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

echo -e "${GREEN}Astronomical Mount Controller Build Script${NC}"
echo "=========================================="

# Check if we're in the right directory
if [ ! -f "CMakeLists.txt" ]; then
    echo -e "${RED}Error: CMakeLists.txt not found. Please run from project root.${NC}"
    exit 1
fi

# Create build directory
echo -e "${YELLOW}Creating build directory...${NC}"
mkdir -p build
cd build

# Configure with CMake
echo -e "${YELLOW}Configuring with CMake...${NC}"
cmake .. -DCMAKE_BUILD_TYPE=Release

# Build the project
echo -e "${YELLOW}Building project...${NC}"
make -j$(nproc)

# Generate gRPC Python code
echo -e "${YELLOW}Generating gRPC Python code...${NC}"
cd ..
if [ -d "build/proto" ]; then
    python3 -m grpc_tools.protoc \
        -I=proto \
        --python_out=build/proto \
        --grpc_python_out=build/proto \
        proto/mount_controller.proto
    
    # Fix import paths in generated Python code
    sed -i 's/import mount_controller_pb2 as mount__controller__pb2/from . import mount_controller_pb2 as mount__controller__pb2/' build/proto/mount_controller_pb2_grpc.py
fi

# Run tests
echo -e "${YELLOW}Running tests...${NC}"
cd build
if [ -f "test_astronomical_calculations" ]; then
    ./test_astronomical_calculations
fi

# Create installation directories
echo -e "${YELLOW}Creating installation directories...${NC}"
sudo mkdir -p /opt/astro-mount
sudo mkdir -p /etc/astro-mount-controller
sudo mkdir -p /var/log/astro-mount

# Copy configuration
echo -e "${YELLOW}Copying configuration files...${NC}"
sudo cp ../config/default.json /etc/astro-mount-controller/

# Copy systemd service
echo -e "${YELLOW}Installing systemd service...${NC}"
sudo cp ../scripts/astro-mount-controller.service /lib/systemd/system/

# Create astro user if it doesn't exist
if ! id "astro" &>/dev/null; then
    echo -e "${YELLOW}Creating astro user...${NC}"
    sudo useradd -r -s /bin/false astro
fi

# Set permissions
echo -e "${YELLOW}Setting permissions...${NC}"
sudo chown -R astro:astro /opt/astro-mount
sudo chown -R astro:astro /var/log/astro-mount
sudo chmod 755 /opt/astro-mount
sudo chmod 755 /var/log/astro-mount

# Install the binary
echo -e "${YELLOW}Installing binary...${NC}"
sudo cp bin/astro-mount-controller /usr/local/bin/

# Reload systemd
echo -e "${YELLOW}Reloading systemd...${NC}"
sudo systemctl daemon-reload

echo -e "${GREEN}Build and installation complete!${NC}"
echo ""
echo "To start the service:"
echo "  sudo systemctl start astro-mount-controller"
echo ""
echo "To enable at boot:"
echo "  sudo systemctl enable astro-mount-controller"
echo ""
echo "To check status:"
echo "  sudo systemctl status astro-mount-controller"
echo ""
echo "To view logs:"
echo "  sudo journalctl -u astro-mount-controller -f"
echo ""
echo "Python examples are available in examples/python/"
echo "C++ examples are available in examples/cpp/"