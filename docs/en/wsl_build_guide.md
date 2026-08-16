# WSL2 Build Guide — Astro Mount Controller

**Building the C++ project on Windows using WSL2 (Ubuntu 24.04)**

---

## Table of Contents

1. [What is WSL2?](#1-what-is-wsl2)
2. [Installing WSL2](#2-installing-wsl2)
3. [Setting Up the Build Environment](#3-setting-up-the-build-environment)
4. [Building the Project](#4-building-the-project)
5. [Running the Controller](#5-running-the-controller)
6. [Running Tests](#6-running-tests)
7. [Building the Qt GUI](#7-building-the-qt-gui)
8. [VS Code Integration](#8-vs-code-integration)
9. [Troubleshooting](#9-troubleshooting)

---

## 1. What is WSL2?

WSL2 (Windows Subsystem for Linux version 2) lets you run a full Linux kernel inside Windows. This allows compiling the astro_mount_control project natively for Linux while using Windows development tools (VS Code, Git for Windows).

**Advantages over native Windows build:**
- Full POSIX API support (required by gRPC, CANopen, GPIO libraries)
- Same build environment as the target deployment system (Raspberry Pi, Odroid)
- All Linux packages available via `apt`
- Network bridge mode for gRPC testing

---

## 2. Installing WSL2

### Step 1: Enable WSL2 (one-time)

Open **PowerShell as Administrator** and run:

```powershell
# Enable WSL
dism.exe /online /enable-feature /featurename:Microsoft-Windows-Subsystem-Linux /all /norestart

# Enable Virtual Machine Platform
dism.exe /online /enable-feature /featurename:VirtualMachinePlatform /all /norestart

# Restart Windows when prompted
```

### Step 2: Set WSL2 as default

```powershell
wsl --set-default-version 2
```

### Step 3: Install Ubuntu 24.04

```powershell
# Install Ubuntu
wsl --install -d Ubuntu-24.04

# Or from Microsoft Store: search "Ubuntu 24.04" and install
```

### Step 4: First launch

```powershell
# Launch Ubuntu (this will start the setup)
wsl
```

On first run, you'll be prompted to create a **Linux username and password**. This is separate from your Windows login.

### Step 5: Verify WSL2 is running

```powershell
# From PowerShell (outside WSL)
wsl -l -v
```

Expected output:
```
  NAME            STATE           VERSION
* Ubuntu-24.04    Running         2
```

---

## 3. Setting Up the Build Environment

### Option A: Automated (recommended)

Inside WSL, run the setup script:

```bash
# Navigate to project directory
cd /mnt/c/Users/jacek/OneDrive/Documents/astro_mount_control

# Make script executable
chmod +x scripts/setup_wsl_build.sh

# Run setup
./scripts/setup_wsl_build.sh
```

### Option B: Manual

If you prefer to install each package individually:

```bash
# Update package list
sudo apt update

# Install essential build tools
sudo apt install -y build-essential cmake g++ git pkg-config

# Install gRPC and Protobuf
sudo apt install -y libgrpc++-dev protobuf-compiler-grpc libprotobuf-dev

# Install linear algebra
sudo apt install -y libeigen3-dev

# Install database
sudo apt install -y libsqlite3-dev

# Install logging
sudo apt install -y libspdlog-dev libfmt-dev

# Install networking
sudo apt install -y libcurl4-openssl-dev libssl-dev

# Install JSON
sudo apt install -y nlohmann-json3-dev

# Install hardware interfaces
sudo apt install -y libgpiod-dev libftdi1-dev libhidapi-dev

# Install UUID
sudo apt install -y uuid-dev

# Install testing
sudo apt install -y googletest libgtest-dev

# Install Qt5 (optional, for GUI)
sudo apt install -y qtbase5-dev libqt5svg5-dev qtcharts5-dev
```

---

## 4. Building the Project

### Automated build

```bash
cd /mnt/c/Users/jacek/OneDrive/Documents/astro_mount_control
chmod +x scripts/build_wsl.sh
./scripts/build_wsl.sh
```

### Manual build

```bash
cd /mnt/c/Users/jacek/OneDrive/Documents/astro_mount_control

# Create build directory
mkdir -p build && cd build

# Configure with CMake
cmake .. -DCMAKE_BUILD_TYPE=Release

# Build (using all CPU cores)
make -j$(nproc)
```

### Build options

```bash
# Debug build (full symbols, assertions)
cmake .. -DCMAKE_BUILD_TYPE=Debug

# Release build (optimized)
cmake .. -DCMAKE_BUILD_TYPE=Release

# Minimal build (no GUI, no CANopen)
cmake .. -DCMAKE_BUILD_TYPE=MinSizeRel

# With CANopen support
cmake .. -DHAVE_CANOPEN=ON

# With test coverage
cmake .. -DCMAKE_BUILD_TYPE=Debug -DCOVERAGE=ON
```

### Build output

After a successful build, binaries are in `build/bin/`:

| Binary | Description |
|--------|-------------|
| `astro_mount_controller` | Main controller daemon |
| `run_tests` | Unit test runner |
| `object_database` | Object database server |

---

## 5. Running the Controller

```bash
# From the build directory
cd /mnt/c/Users/jacek/OneDrive/Documents/astro_mount_control/build

# Run with default config (simulated hardware)
./bin/astro_mount_controller ../config/default.json

# Run with specific config
./bin/astro_mount_controller ../config/dual_servo_config.json

# Run with test config (no hardware required)
./bin/astro_mount_controller ../config/test_no_hardware.json
```

The controller will start:
- gRPC API server on port 50051
- Object database on port 50052
- Web proxy server on port 8080

### Testing with simulated hardware

The default configuration uses `HALType::SIMULATED` which doesn't require any physical hardware. To verify the controller is running:

```bash
# In another WSL terminal, check if the gRPC port is listening
curl -s http://localhost:50051/  # Should show gRPC response

# Or use the Python example
cd /mnt/c/Users/jacek/OneDrive/Documents/astro_mount_control
python3 examples/python/example_usage.py
```

### Web UI

Once the controller is running, start the web proxy:

```bash
cd /mnt/c/Users/jacek/OneDrive/Documents/astro_mount_control/web/proxy
npm install
node server.js
```

Then open `http://localhost:8080` in your Windows browser.

---

## 6. Running Tests

```bash
cd /mnt/c/Users/jacek/OneDrive/Documents/astro_mount_control/build

# Run all tests
./bin/run_tests

# Run specific test suite
./bin/run_tests --gtest_filter=*MountController*

# Run with verbose output
./bin/run_tests --gtest_print_time=1
```

Key test files:

| Test | File | Description |
|------|------|-------------|
| Mount Controller | `tests/test_mount_controller.cpp` | 25 test groups, 919 lines |
| Kalman Filter | `tests/test_kalman_filter.cpp` | State estimation |
| TPOINT Model | `tests/test_tpoint_model.cpp` | Pointing model |
| Configuration | `tests/test_configuration.cpp` | JSON loading/saving |
| gRPC | `tests/test_grpc_integration.cpp` | API integration |

---

## 7. Building the Qt GUI

> ⚠️ **Not available (2026-08-11):** the Qt GUI sources have been removed from
> this repository (see [`qt_gui.md`](qt_gui.md)). Use the web SPA
> ([`../web/README.md`](../web/README.md)) instead.

> **Note:** Qt GUI requires Qt5 installed (see Step 3). The steps below are
> historical and require the sources to be restored from git history.

```bash
cd /mnt/c/Users/jacek/OneDrive/Documents/astro_mount_control

# Create separate build directory for GUI
mkdir -p build-gui && cd build-gui

# Configure with Qt5
cmake ../gui

# Build
make -j$(nproc)

# Run
./astro_mount_gui
```

If Qt5 is not found, specify the path:

```bash
cmake ../gui -DQt5_DIR=/usr/lib/x86_64-linux-gnu/cmake/Qt5
```

---

## 8. VS Code Integration

### Opening the project from WSL

```bash
# Inside WSL, navigate to the project
cd /mnt/c/Users/jacek/OneDrive/Documents/astro_mount_control

# Open VS Code (WSL-aware)
code .
```

VS Code will automatically:
1. Install the "Remote - WSL" extension if not present
2. Open the project with WSL as the backend
3. Use the Linux C++ compiler and tools

### Recommended VS Code extensions

```bash
code --install-extension ms-vscode.cpptools
code --install-extension ms-vscode.cmake-tools
code --install-extension vadimcn.vscode-lldb
code --install-extension twxs.cmake
```

### C++ IntelliSense configuration

Create `.vscode/c_cpp_properties.json`:

```json
{
    "configurations": [{
        "name": "WSL",
        "intelliSenseMode": "linux-gcc-x64",
        "compilerPath": "/usr/bin/g++",
        "cStandard": "c17",
        "cppStandard": "c++17",
        "includePath": [
            "${workspaceFolder}/include",
            "${workspaceFolder}/build/proto",
            "/usr/include",
            "/usr/include/eigen3"
        ]
    }]
}
```

---

## 9. Troubleshooting

### WSL not starting

```powershell
# Reset WSL
wsl --shutdown
wsl
```

### File permission issues

```bash
# Windows files in /mnt/c/ inherit Windows permissions
# To fix:
sudo chown -R $(whoami) /mnt/c/Users/jacek/OneDrive/Documents/astro_mount_control
```

### gRPC protobuf generation errors

```bash
# Ensure protobuf compiler is installed
sudo apt install -y protobuf-compiler-grpc

# Verify version
protoc --version
grpc_cpp_plugin --version 2>/dev/null || echo "grpc_cpp_plugin not in PATH"
```

### CMake cannot find packages

```bash
# Check if packages are installed
dpkg -l | grep grpc
dpkg -l | grep protobuf
dpkg -l | grep eigen3

# If missing, install them
sudo apt install -y libgrpc++-dev libprotobuf-dev libeigen3-dev
```

### Linker errors (undefined references)

```bash
# Clean rebuild
cd build && make clean && cmake .. && make -j$(nproc)
```

### "Permission denied" when running scripts

```bash
chmod +x scripts/*.sh
```

### Network issues (gRPC connections from Windows to WSL)

The controller listens on `0.0.0.0:50051` inside WSL. To access it from Windows:

```powershell
# Get WSL's IP address (from PowerShell)
wsl -- hostname -I
# Typically 172.x.x.x

# From Windows, connect to that IP
# Or use WSL's port forwarding:
netsh interface portproxy add v4tov4 listenport=50051 listenaddress=0.0.0.0 connectport=50051 connectaddress=$(wsl -- hostname -I)
```
