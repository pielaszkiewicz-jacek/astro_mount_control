# INDI and ASCOM integration — configuration & usage

**Project:** AstroMountController
**Date:** 2026-08-15
**Scope:** the INDI driver (C++, [`indi/`](../indi)) and the ASCOM driver (C#, [`ascom/`](../ascom)).

> The Polish version of this document lives at [`docs/pl/integracja_indi_ascom.md`](../pl/integracja_indi_ascom.md).

---

## Table of contents

1. [Integration architecture](#1-integration-architecture)
2. [Requirements](#2-requirements)
3. [Shared step: start the controller](#3-shared-step-start-the-controller)
4. [INDI — build, configure, use](#4-indi--build-configure-use)
5. [ASCOM — build, configure, use](#5-ascom--build-configure-use)
6. [Feature mapping (INDI ↔ ASCOM ↔ gRPC)](#6-feature-mapping)
7. [Troubleshooting](#7-troubleshooting)
8. [Key files](#8-key-files)

---

## 1. Integration architecture

```mermaid
flowchart LR
    subgraph Clients
        K[KStars / Ekos / INDI Control Panel]
        A[NINA / SGP / MaxIm DL / SkyX / PHD2]
    end
    subgraph Drivers
        I[astro_mount_indi_driver (C++)]
        AS[AstroMountTelescope.dll (C#)]
    end
    subgraph Transport
        IS[INDI server / indiserver]
        AC[ASCOM Platform / Alpaca]
    end
    subgraph Controller
        M[astro_mount_controller<br/>gRPC :50051]
    end
    K --> IS
    IS --> I
    I -->|gRPC| M
    A --> AC
    AC --> AS
    AS -->|gRPC| M
```

- Both drivers are **thin** — all logic (astronomy, TPOINT, Kalman, safety) runs inside `astro_mount_controller`; the drivers only translate calls to gRPC.
- **Default gRPC port:** `50051` (configurable, see below).
- **INDI:** the driver registers with `indiserver`, which fans out events to clients (KStars/Ekos).
- **ASCOM:** the driver is a COM library registered with the ASCOM Platform; a client (NINA, etc.) connects by selecting the device in the chooser.

---

## 2. Requirements

### INDI (typically Linux / RPi)
- **INDI SDK ≥ 2.0** (`libindi-dev`): `sudo apt install libindi-dev`
  (on Debian/Ubuntu INDI is discovered via **pkg-config** — `libindi.pc`; the driver uses pkg-config with a fallback to `find_package(INDI)` for 2.x builds that ship `INDIConfig.cmake`)
- **libnova-dev** (dependency of INDI headers):
  `sudo apt install libnova-dev`
- **protobuf + gRPC** (dev):
  `sudo apt install protobuf-compiler libprotobuf-dev libgrpc-dev libgrpc++-dev protobuf-compiler-grpc`
- **cmake ≥ 3.16**, a C++17 compiler
- **indiserver + client** (e.g. KStars/Ekos or INDI Control Panel)

Full dependency install on Debian/Ubuntu:
```bash
sudo apt install libindi-dev libnova-dev \
  protobuf-compiler libprotobuf-dev libgrpc-dev libgrpc++-dev protobuf-compiler-grpc \
  cmake build-essential
```

### ASCOM (Windows)
- **.NET SDK** (to build) and **ASCOM Platform ≥ 6** (on the target machine)
- Mono (optional): `mono-devel` + `msbuild`
- Client: NINA, Sequence Generator Pro, MaxIm DL, SkyX, ASCOM Device Hub, etc.

> Note: **COM registration (`regasm`) is Windows-only.** On Linux/Mono the ASCOM code can be compiled but not registered with ASCOM.

---

## 3. Shared step: start the controller

Build and run the controller (full build, see [`docs/en/installation.md`](installation.md)):

```bash
# build (from the project root)
cmake --build build -j6
# run (from the project root)
./build/bin/astro_mount_controller config/default.json
```

Verify gRPC is listening:

```bash
grpc_cli call localhost:50051 CheckHealth "service: 'mount_controller'"
```

> For tests with a simulated HAL use [`config/emulation.json`](../config/emulation.json).

---

## 4. INDI — build, configure, use

### 4.1 Build

The INDI driver has its **own** [`CMakeLists.txt`](../indi/CMakeLists.txt) — it is not part of the main build:

```bash
cd indi
mkdir -p build && cd build
cmake .. -DCMAKE_PREFIX_PATH=/usr
make -j$(nproc)
```

This produces the `astro_mount_indi_driver` executable. Optionally install it into the INDI drivers directory:

```bash
sudo cmake --install .
# copies to ${INDI_DATA_DIR}/drivers (usually /usr/share/indi/drivers)
```

### 4.2 Configure the controller address

#### a) Defaults (environment)

The driver initialises the gRPC host/port from **environment variables** at startup ([`astro_mount_driver.cpp`](../indi/astro_mount_driver.cpp:13)):

| Variable | Default | Description |
|----------|---------|-------------|
| `GRPC_HOST` | `localhost` | Controller gRPC host |
| `GRPC_PORT` | `50051` | Controller gRPC port |

```bash
export GRPC_HOST=192.168.1.100
export GRPC_PORT=50051
indiserver astro_mount_indi_driver
```

#### b) Configure from the INDI UI

The driver defines **editable INDI properties** (visible immediately in KStars/Ekos / INDI Control Panel, no connection required):

| Property | Type | Meaning |
|----------|------|---------|
| **GRPC_CONNECTION** (HOST, PORT) | Text (IP_RW) | Controller gRPC host and port |
| **GRPC_TLS** (ENABLE / DISABLE) | Switch | Enable TLS/SSL for the gRPC link |
| **GRPC_CONNECTION_STATUS** | Text (IP_RO) | Connection state (“Connected to host:port” / “Not connected”) |

Edit them in the **Connection tab** of the INDI client. Changes update `m_grpcHost`/`m_grpcPort`/`m_grpcUseSsl` and take effect on the next **Connect** ([`applyConnectionConfig()`](../indi/astro_mount_driver.cpp:836) recreates the gRPC client before `m_grpc->connect()`).

### 4.3 Run

The driver exports loader symbols (`ISGetProperties`, `ISNewSwitch`, …) — it is launched **via indiserver**:

```bash
indiserver -v astro_mount_indi_driver
```

### 4.4 Connect from a client (KStars / Ekos)

1. **KStars → Ekos → Ekos Manager (or Tools → Devices):**
   - select **AstroMount** as the telescope (Mount tab),
   - optionally start indiserver with **Start** (local) or provide a remote INDI server address,
   - click **Connect** — the driver establishes gRPC to the controller.
2. **INDI Control Panel:** connect to `localhost:7624` (or the server address), find `astro_mount_indi_driver`, enable **CONNECT**.

> **Important:** since fix N12, `Connect()` actually establishes gRPC (`m_grpc->connect()`). Without it, the Connect button did nothing.

### 4.5 Available INDI features

| Feature | INDI location | Controller RPC |
|---------|---------------|----------------|
| **Goto (RA/Dec)** | `EQUATORIAL_EOD_COORD` | `SlewToCoordinates` |
| **Sync** | `EQUATORIAL_EOD_COORD` | `AddBootstrapMeasurement` + `RunBootstrapCalibration` |
| **MoveNS / MoveWE** | `TELESCOPE_MOTION_NS/WE` | `ControlAxis` (velocity) / `StopAxis` |
| **Abort** | `TELESCOPE_ABORT_MOTION` | `Stop` |
| **Park / Unpark** | `TELESCOPE_PARK` | `Park` / `Unpark` |
| **Bootstrap** | `BOOTSTRAP_CALIBRATION` (RUN/CLEAR/STATUS) | `AddBootstrapMeasurement`, `RunBootstrapCalibration`, `GetBootstrapStatus` |
| **TPOINT status** | `TPOINT_STATUS` (read-only) | from `GetState().tpoint_params` |
| **Environment** | `ENVIRONMENT` (temp/pressure/humidity) | from `GetState()` |
| **Location** | `GEOGRAPHIC_COORD` | `GetConfiguration`/`UpdateConfiguration` |

State is polled ~1 s (`TimerHit` → `GetState`); RA/Dec computed from mount position (LST in `IndiPropertyMapper`).

---

## 5. ASCOM — build, configure, use

### 5.1 Build

Project file: [`AstroMountTelescope.csproj`](../ascom/AstroMountTelescope.csproj) (target `net48`).

```bash
cd ascom
dotnet build -c Release
# or Mono:
msbuild AstroMountTelescope.csproj -p:Configuration=Release
```

Result: `bin/Release/AstroMountTelescope.dll`.

### 5.2 Register (Windows, administrator)

```bash
regasm /codebase ascom/bin/Release/AstroMountTelescope.dll
```

The driver is exposed as **ProgId `AstroMount.Telescope`**, name **“AstroMount Telescope Controller”**.

### 5.3 Configure via the Setup UI

Since fix N13 the driver has its **own setup dialog** (Windows Forms, [`SetupDialog.cs`](../ascom/SetupDialog.cs)):

1. In an ASCOM app (NINA, SGP, MaxIm, SkyX, ASCOM Device Hub): select **AstroMount Telescope Controller**.
2. Click **Setup** (opens `ActionSetup()` → `SetupDialog`).
3. In the dialog set:
   - **gRPC Host** (e.g. `192.168.1.100` or `localhost`),
   - **Port** (default `50051`),
   - **Use TLS (SSL)** — only if the controller has SSL enabled.
4. Click **Test Connection** — verifies `CheckHealth` and reports the result.
5. **OK** — saves config and rebuilds the gRPC client.

### 5.4 Configure via connection string

Alternatively (e.g. in the ASCOM chooser) set a connection string of the form:

```
host=192.168.1.100;port=50051
```

Optional: `ssl=1` (or `tls=1`) enables TLS.

### 5.5 Connect and control

1. **Connect** — the driver creates the gRPC channel, verifies `CheckHealth`, and starts `StateCache` (polls `GetState()` every 1 s).
2. Control (interface `ITelescopeV3`):

| Feature | ASCOM property/method |
|---------|------------------------|
| **Goto** | `SlewToCoordinatesAsync(ra, dec)` / `SlewToTargetAsync` |
| **Sync** | `SyncToCoordinates(ra, dec)` |
| **Abort** | `AbortSlew()` |
| **Park / Unpark** | `Park()` / `Unpark()` |
| **Guiding** | `PulseGuide(direction, durationMs)` |
| **Position** | `RightAscension`, `Declination` (from cache) |
| **State** | `Slewing`, `Tracking`, `SideOfPier` |
| **Alt/Az** | `SlewToAltAz(alt, az)` |
| **Location** | `SiteLatitude`, `SiteLongitude`, `SiteElevation` |

---

## 6. Feature mapping

| Feature | gRPC RPC | INDI | ASCOM |
|---------|----------|------|-------|
| Slew RA/Dec | `SlewToCoordinates` | Goto | `SlewToCoordinatesAsync` |
| Slew Alt/Az | `SlewToHorizontal` | — | `SlewToAltAz` |
| Sync / bootstrap | `AddBootstrapMeasurement`+`RunBootstrapCalibration` | Sync / BOOTSTRAP | `SyncToCoordinates` |
| Stop / Abort | `Stop` | Abort | `AbortSlew` |
| Park | `Park` | Park | `Park` |
| Unpark | `Unpark` | Unpark | `Unpark` |
| Axis motion | `ControlAxis` | MoveNS/MoveWE | — (Phase 3) |
| Guiding | `PulseGuide` (guider) | — | `PulseGuide` |
| Position | `GetState` | `EQUATORIAL_EOD_COORD` | `RightAscension`/`Declination` |
| Configuration | `GetConfiguration`/`UpdateConfiguration` | `UpdateLocation` | `SiteLatitude/...` |

---

## 7. Troubleshooting

### INDI
- **`CMake Error: find_package(INDI)` / “Could not find INDIConfig.cmake”:** INDI is not installed. The driver discovers INDI via **pkg-config** (`libindi.pc`) with a fallback to `find_package(INDI)` — after installing the package, configuration succeeds:
  ```bash
  sudo apt install libindi-dev
  pkg-config --modversion libindi   # must print a version
  ```
  If INDI is installed elsewhere: `cmake .. -DCMAKE_PREFIX_PATH=/path/to/indi`.
- **Connect does nothing / no gRPC link:** make sure the driver is built with the `Connect()` fix (N12) and that the controller is running. Check `GRPC_HOST`/`GRPC_PORT` or the `GRPC_CONNECTION` UI property.
- **Driver missing in KStars list:** `sudo cmake --install build` inside `indi`, then restart indiserver; use `indiserver -v` for verbose output.
- **INDI SDK missing at build time:** install `libindi-dev`; `pkg-config --modversion libindi` must return a version.
- **Position does not refresh:** check driver logs (`LOG_ERROR` in `pollController`); possible port/connectivity issue.

### ASCOM
- **`regasm` not found:** use the Visual Studio Developer Command Prompt, or the full path `C:\Windows\Microsoft.NET\Framework64\v4.0.30319\regasm.exe`.
- **Driver not in the chooser:** check COM registration and `ProgId AstroMount.Telescope`; `dotnet build -c Release` must succeed (NuGet: ASCOM.Tools, Grpc.Core, Google.Protobuf).
- **Test Connection fails:** check host/port and that the controller gRPC is serving (`CheckHealth`).
- **TLS:** `ssl=1` requires a controller configured with certificates (`certs/`) and `ENABLE_SSL=true`; use `ssl=0` by default.
- **Linux/Mono:** COM registration is unavailable — use Windows, or an ASCOM Remote/Alpaca bridge.

---

## 8. Key files

- **INDI:** [`indi/astro_mount_driver.cpp`](../indi/astro_mount_driver.cpp), [`indi/astro_mount_driver.h`](../indi/astro_mount_driver.h), [`indi/MountGrpcClient.cpp`](../indi/MountGrpcClient.cpp), [`indi/IndiPropertyMapper.cpp`](../indi/IndiPropertyMapper.cpp), [`indi/CMakeLists.txt`](../indi/CMakeLists.txt)
- **ASCOM:** [`ascom/AstroMountTelescope.cs`](../ascom/AstroMountTelescope.cs), [`ascom/SetupDialog.cs`](../ascom/SetupDialog.cs), [`ascom/GrpcClient.cs`](../ascom/GrpcClient.cs), [`ascom/StateCache.cs`](../ascom/StateCache.cs), [`ascom/ConversionHelper.cs`](../ascom/ConversionHelper.cs), [`ascom/AstroMountTelescope.csproj`](../ascom/AstroMountTelescope.csproj)
- **Shared:** [`proto/mount_controller.proto`](../proto/mount_controller.proto), [`src/main.cpp`](../src/main.cpp) (gRPC hosting on 50051)
