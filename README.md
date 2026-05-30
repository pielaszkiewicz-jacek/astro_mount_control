# Astronomical Mount Controller

[![Build Status](https://img.shields.io/badge/build-passing-brightgreen.svg)](.)
[![Tests](https://img.shields.io/badge/tests-9%2F9%20passing-brightgreen.svg)](build_verify/)
[![C++](https://img.shields.io/badge/C%2B%2B-17-blue.svg)](https://isocpp.org/)
[![gRPC](https://img.shields.io/badge/gRPC-1.60%2B-blue.svg)](https://grpc.io/)
[![License](https://img.shields.io/badge/license-MIT-green.svg)](LICENSE)
[![Platform](https://img.shields.io/badge/platform-Linux%20%7C%20ARM-lightgrey.svg)](docs/en/installation.md)

A high-precision astronomical mount controller with sub-arcsecond tracking accuracy, TPOINT model calibration, extended Kalman filter, CANopen/CiA 402 hardware interface, and complete gRPC API for remote operation.

---

## Features

### 🎯 Precision Tracking
- **Sub-arcsecond tracking** accuracy with real-time corrections
- **TPOINT model** (21+ parameters) for mount geometry error compensation
- **Extended Kalman filter** for continuous state estimation and calibration
- **Automatic meridian flip** with configurable hysteresis and delay
- **3-zone soft limit system** with warning, deceleration, and hard-stop zones

### 🔄 Coordinate Systems & Corrections
- **Full astronomical correction chain**: precession, nutation, aberration, light-time, gravitational deflection
- **Atmospheric refraction** (Saastamoinen model + Saemundsson formula)
- **Field rotation compensation** for Alt-Az mounts with configurable derotator
- **Equatorial ↔ Hour Angle ↔ Horizontal** transformations

### ⚙️ Hardware Abstraction
- **CANopen/CiA 402** interface for industrial servo/stepper drives
- **Simulated HAL** for testing and development (no hardware required)
- **Extensible HAL architecture** supporting Serial, Ethernet, and custom implementations
- **Absolute and incremental encoder** support
- **Derotator (field rotator)** control with homing and calibration

### 🌐 Remote Control & Integration
- **Complete gRPC API** for remote operation from any language
- **Python and C++ client libraries** with full examples
- **Object database** (SQLite + gRPC) for astronomical catalog management
- **Ephemeris tracking** for comets, asteroids, and satellites
- **Autoguider integration** (PHD2, Ekos, ASCOM-compatible)

### 🛡️ Safety & Reliability
- **11 NaN/Inf propagation guards** in tracking loop
- **State machine** with safe transitions and error recovery
- **Configurable logging** with rotation and multiple levels
- **Systemd service** integration for headless operation
- **SSL/TLS** support for secure gRPC connections

---

## Quick Start

### Prerequisites
- Linux (Ubuntu 22.04+, Debian 12+, RHEL 9+, or any distribution with C++17 support)
- C++17 compiler (GCC 11+, Clang 14+)
- CMake 3.15+
- gRPC and Protocol Buffers
- SOFA library (included as submodule)

### Build & Run

```bash
# Clone
git clone https://github.com/your-org/astro-mount-controller.git
cd astro-mount-controller

# Configure and build
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)

# Run (with simulated hardware by default)
./bin/astro_mount_controller

# Run tests
ctest -V
```

### Basic Python Client

```python
import grpc
from proto import mount_controller_pb2
from proto import mount_controller_pb2_grpc

# Connect to running controller
channel = grpc.insecure_channel('localhost:50051')
stub = mount_controller_pb2_grpc.MountControllerServiceStub(channel)

# Slew to M31 (Andromeda Galaxy)
from google.protobuf import empty_pb2
coords = mount_controller_pb2.Coordinates(ra=0.7117, dec=41.2692)
stub.SlewToCoordinates(coords)

# Monitor state
state = stub.GetState(empty_pb2.Empty())
print(f"Status: {state.status}, Position: axis1={state.current_position.axis1:.4f}°")
```

---

## Documentation

| Document | Description |
|----------|-------------|
| [`docs/en/index.md`](docs/en/index.md) | Full system overview, architecture, and feature guide |
| [`docs/en/architecture.md`](docs/en/architecture.md) | Detailed architecture with component descriptions and data flow |
| [`docs/en/installation.md`](docs/en/installation.md) | Installation for Ubuntu, Debian, RHEL, OpenSUSE, ARM/Raspberry Pi |
| [`docs/en/api.md`](docs/en/api.md) | Complete gRPC API reference |
| [`docs/en/api_examples.md`](docs/en/api_examples.md) | API usage examples in C++ and Python |
| [`docs/en/examples.md`](docs/en/examples.md) | Practical usage scenarios and tutorial |
| [`docs/en/mathematical_model.md`](docs/en/mathematical_model.md) | Mathematical models: TPOINT, Kalman filter, coordinate transforms |
| [`docs/en/hal_layer.md`](docs/en/hal_layer.md) | Hardware Abstraction Layer documentation |
| [`docs/en/data_flow.md`](docs/en/data_flow.md) | Data flow diagrams for all subsystems |
| [`docs/en/developer_onboarding.md`](docs/en/developer_onboarding.md) | Developer onboarding guide |
| [`web/README.md`](web/README.md) | Web dashboard documentation (HTTP/JSON proxy + SPA) |
| [`docs/pl/`](docs/pl/) | Polish language documentation |

---

## Architecture Overview

```mermaid
flowchart TB
    %% Styles
    classDef client fill:#e1f5fe,stroke:#0288d1,stroke-width:2px
    classDef api fill:#e8f5e9,stroke:#388e3c,stroke-width:2px
    classDef core fill:#fff3e0,stroke:#f57c00,stroke-width:2px
    classDef model fill:#fce4ec,stroke:#d32f2f,stroke-width:2px
    classDef hal fill:#f3e5f5,stroke:#7b1fa2,stroke-width:2px
    classDef hw fill:#efebe9,stroke:#4e342e,stroke-width:2px

    subgraph CLIENTS["Client Layer"]
        PY["Python Client<br/>(gRPC stub)"]
        CPP["C++ Client<br/>(gRPC stub)"]
        WEB["Web Dashboard<br/>(HTTP/JSON proxy)"]
    end

    subgraph API["gRPC API Layer"]
        GRPC["MountControllerServiceImpl<br/>50+ RPC methods"]
    end

    subgraph CORE["Mount Controller (src/controllers/)"]
        MC["MountController<br/>State Machine<br/>9 states"]
        MODELS["Embedded Models:"]
        TPOINT["TPOINT Model<br/>21 params / QR solver"]
        KF["Kalman Filter<br/>EKF / Joseph form"]
        EPHEM["Ephemeris Tracker<br/>Moving objects"]
        ASTRO["Astro Calculations<br/>SOFA transforms"]
    end

    subgraph HAL["Hardware Abstraction Layer"]
        HAL_IF["HALInterface"]
        CAN_IMPL["CANopen (CiA 402)<br/>Servo drives"]
        SIM_IMPL["Simulated<br/>Testing/Dev"]
        SERIAL_IMPL["Serial<br/>(Planned)"]
        ETH_IMPL["Ethernet<br/>(Planned)"]
    end

    subgraph HW["Physical Hardware"]
        MOT["Motors<br/>Stepper / Servo"]
        ENC["Encoders<br/>Absolute / Incremental"]
        DEROT["Derotator<br/>Field Rotation"]
        SENS["Sensors<br/>Temp / Pressure"]
    end

    CLIENTS -->|gRPC| GRPC
    GRPC --> MC
    MC --> HAL_IF
    HAL_IF --> CAN_IMPL
    HAL_IF --> SIM_IMPL
    HAL_IF --> SERIAL_IMPL
    HAL_IF --> ETH_IMPL
    CAN_IMPL --> HW

    class PY,CPP,WEB client
    class GRPC api
    class MC,MODELS core
    class TPOINT,KF,EPHEM,ASTRO model
    class HAL_IF,CAN_IMPL,SIM_IMPL,SERIAL_IMPL,ETH_IMPL hal
    class MOT,ENC,DEROT,SENS hw
```

**Key components:**
- [`src/controllers/mount_controller.cpp`](src/controllers/mount_controller.cpp) — Core controller with state machine, tracking loop, meridian flip, soft limits, and 11 NaN/Inf guards
- [`src/config/configuration.cpp`](src/config/configuration.cpp) — JSON-based configuration with 25+ field validations
- [`src/models/tpoint_model.cpp`](src/models/tpoint_model.cpp) — TPOINT pointing error model with QR decomposition solver
- [`src/models/kalman_filter.cpp`](src/models/kalman_filter.cpp) — Extended Kalman filter with Joseph form covariance update
- [`src/core/astronomical_calculations.cpp`](src/core/astronomical_calculations.cpp) — SOFA-based coordinate transforms and corrections
- [`proto/mount_controller.proto`](proto/mount_controller.proto) — gRPC service definition (50+ RPCs)

---

## Project Structure

```
├── config/           # JSON configuration files
│   └── default.json
├── docs/             # Documentation (en + pl)
│   ├── en/
│   └── pl/
├── include/          # C++ headers
│   ├── config/
│   ├── controllers/
│   ├── core/
│   ├── hal/
│   ├── logging/
│   └── models/
├── proto/            # gRPC protobuf definitions
│   ├── mount_controller.proto
│   └── canopen_service.proto
├── src/              # C++ implementation
│   ├── api/
│   ├── config/
│   ├── controllers/
│   ├── core/
│   ├── hal/
│   ├── logging/
│   └── models/
├── tests/            # Test suites (9 test binaries)
├── examples/         # Python and C++ examples
├── sofa/             # SOFA library
├── scripts/          # Build and utility scripts
├── web/              # Web dashboard (Express proxy + SPA)
└── db/               # Object database (SQLite)
```

---

## Test Status

| Test Suite | Status |
|------------|--------|
| `test_mount_controller` | ✅ 25 test groups |
| `test_tpoint_model` | ✅ 17 test cases |
| `test_astronomical_calculations` | ✅ Full coverage |
| `test_configuration` | ✅ Validation tests |
| `test_kalman_filter` | ✅ State estimation |
| `test_ephemeris_tracker` | ✅ Moving object tracking |
| `test_hal_integration` | ✅ HAL interface tests |
| `test_grpc_integration` | ✅ API server tests |
| `test_subarcsecond_accuracy` | ✅ Accuracy verification |

All **9/9 tests pass** in 18.34s with full NaN/Inf guard coverage.

---

## Contributing

1. Read the [Developer Onboarding Guide](docs/en/developer_onboarding.md)
2. Check existing [Issues](https://github.com/your-org/astro-mount-controller/issues)
3. Fork the repository and create a feature branch
4. Write tests for new functionality
5. Submit a pull request

---

## License

[MIT License](LICENSE) — see LICENSE file for details.

---

## Support

- 📖 [Full Documentation](docs/en/index.md)
- 🐛 [Issue Tracker](https://github.com/your-org/astro-mount-controller/issues)
- 💬 [Discussion Forum](https://forum.astro-mount-controller.org)
- 📧 [Email](mailto:support@astro-mount-controller.org)
