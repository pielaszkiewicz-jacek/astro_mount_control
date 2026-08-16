# Qt Native GUI — User Guide

> ⚠️ **STATUS: NOT AVAILABLE (2026-08-11)**
>
> The Qt Native GUI source code has been **removed from this repository**
> (the `gui/` directory now contains only stale build artifacts; the sources
> were deleted in a later commit). The documentation below is kept for
> reference only — **it cannot be built from the current tree**.
>
> Use the [Web Interface (SPA)](../web/README.md) instead, which provides
> equivalent control over the mount from a browser. If a native desktop GUI
> is required, the sources are recoverable from git history
> (commit `603efbc` “Natywne API QT”).

## Overview

The Qt Native GUI provides a desktop application for controlling the astronomical mount. It connects to the mount controller via gRPC and provides real-time access to all system functions.

> **Note:** Screenshots will be added once the application is compiled and running on a target system. Below are descriptions of each panel.

## Building

### Prerequisites

- Qt 5.15+ or Qt 6.x (Widgets, Network, Svg, Charts modules)
- gRPC and Protobuf development libraries
- CMake 3.16+

### Build Steps

```bash
# From the project root
mkdir build-gui && cd build-gui
cmake ../gui -DQt5_DIR=/path/to/Qt5/5.15.2/gcc_64/lib/cmake/Qt5
make -j$(nproc)
./astro_mount_gui
```

For Qt6:
```bash
cmake ../gui -DQt6_DIR=/path/to/Qt6/lib/cmake/Qt6
```

## Main Window

The main window consists of a tabbed interface with 12 panels, a status bar showing connection state, and a 1-second refresh timer updating real-time data.

```
┌──────────────────────────────────────────────────────┐
│  Astro Mount Control                                 │
├──────┬──────┬──────┬──────┬──────┬──────┬──────┬─────┤
│Mount │Status│Calib │Seq.  │Dome  │Focus │Camera│...  │
├──────┴──────┴──────┴──────┴──────┴──────┴──────┴─────┤
│                                                      │
│              Panel Content Area                      │
│                                                      │
├──────────────────────────────────────────────────────┤
│  Connected to mount controller at 127.0.0.1:50051    │
└──────────────────────────────────────────────────────┘
```

## Panel Reference

### 🔭 Mount Panel

Controls the telescope mount — slew to coordinates, start/stop tracking, park/unpark.

**Controls:**
- **RA/Dec Spin Boxes** — Enter target coordinates
- **Slew** — Slew to entered RA/Dec
- **Track** — Slew and begin sidereal tracking
- **Stop** — Halt all motion immediately
- **Park** — Move to park position and park
- **Unpark** — Unpark and return to idle

**Displays:**
- Current RA/Dec coordinates (updated every second)
- Mount state (IDLE, SLEWING, TRACKING, PARKED, ERROR)
- Tracking status (ON/OFF)

### 📊 Status Panel

Comprehensive real-time status display of the mount and all subsystems.

**Axis Status:**
- Axis 1/2 position (degrees)
- Axis 1/2 commanded rate
- Axis 1/2 actual rate
- Telescope axis position (after gearing)

**Tracking:**
- Tracking RA/Dec
- Tracking error RA (arcsec)
- Tracking error Dec (arcsec)

**System Status:**
- Encoders active/inactive
- Guider active/inactive
- TPOINT calibrated
- Pier side (East/West)
- Time to meridian (hours)
- Meridian flip pending

### 🎯 Calibration Panel

Bootstrap and TPOINT calibration management.

**Bootstrap Calibration:**
- Add measurement — record a pointing measurement
- Run — execute bootstrap calibration (Wahba/SVD)
- Clear — remove all measurements
- Status display (calibrated, measurement count, RMS)

**TPOINT Calibration:**
- Add measurement — record precise pointing data (with env. parameters)
- Run — execute TPOINT fit
- Clear — remove all measurements
- Status display (measurements, RMS, max residual, chi-squared)

### 📋 Sequencer Panel

Automated observation sequencing.

**Controls:**
- **Load** — Load an observation plan
- **Start** — Begin executing the plan
- **Stop** — Stop execution immediately
- **Pause/Resume** — Pause and continue

**Display:**
- Target list with checkmarks
- Current target / total targets
- Current exposure / total exposures
- Progress bar
- Session log

**Target List:**
- Add/remove targets with RA/Dec coordinates
- Each target has configurable exposure plan
- Supports LRGB, narrowband, and calibration sequences

### 🏠 Dome Panel

Controls rotating domes and roll-off roofs.

**Controls:**
- **Open Shutter** — Open dome slit or roll-off roof
- **Close Shutter** — Close and seal
- **Rotate To** — Rotate dome to specific azimuth
- **Park** — Rotate to park position and close

**Display:**
- Dome state (OPEN, CLOSED, ROTATING, ERROR)
- Current azimuth
- Shutter position
- Mount sync status

**Mount Sync:**
- Enable/disable automatic dome tracking
- Configure offset between mount and dome azimuth

### 🔍 Focuser Panel

Focuser control for ZWO EAF, MoonLite, Pegasus FocusCube.

**Manual Control:**
- Position spin box + speed slider
- Move / Halt buttons
- Current position, temperature, HFD display

**Auto-Focus:**
- Start/End position range
- Step size
- Run auto-focus button
- Progress during scanning
- V-curve display showing HFD vs position
- Best focus position marker

### 📷 Camera Panel

Camera control for ZWO ASI cameras.

**Exposure:**
- Exposure time (0.001s to hours)
- Gain slider (0-100)
- Binning selector (1x1, 2x2, 3x3)
- Filter selector (L, R, G, B, Ha, OIII, SII)
- Start/Abort buttons
- Progress bar during exposure

**Cooler:**
- Target temperature setpoint
- Current temperature display
- Cooler power percentage

**Camera Info:**
- Model name, sensor type
- Resolution, pixel size
- Cooler status

### 🌤️ Weather Panel

Environmental monitoring display.

**Current Readings:**
- Temperature (°C) with trend
- Humidity (%)
- Atmospheric pressure (hPa) with trend
- Wind speed and direction
- Rain detection and rate
- Cloud cover percentage
- Sky brightness (MPSAS)

**Safety:**
- Overall safety status (SAFE/CAUTION/WARNING/DANGER)
- Auto-park status
- Safety rules configuration

### 🔄 Derotator Panel

Field derotation control for alt-az and CASUAL mounts.

**Mode Selection:**
- **Disabled** — No derotation
- **Auto** — Automatic continuous compensation
- **Fixed Angle** — Hold specific rotation angle
- **Manual Rate** — Manual rotation speed

**Controls:**
- Set absolute angle
- Set rotation rate
- Home derotator (find reference position)

**Display:**
- Current position
- Current rate
- Homed status

**Field Rotation Info:**
- Current field rotation angle
- Rotation rate (arcsec/s)
- Predicted angle in 10 minutes

### ⚙ PEC Panel

Periodic Error Correction training and management.

**Training:**
- Worm cycle period (default 638s for 1 RPM)
- Number of harmonics (1-20)
- Duration in worm cycles
- Start/Stop training buttons
- Progress bar

**Status:**
- PEC enabled/disabled
- Trained/not trained
- Peak error (arcsec)
- RMS error (arcsec)
- Harmonics display

**Data Management:**
- Save trained PEC data to disk
- Load previously saved PEC data

### 🔋 Power Panel

Power management dashboard.

**Sensor Display:**
- Voltage (V)
- Current (A)
- Power (W)
- Battery capacity (%)
- Estimated runtime (minutes)
- Temperature (°C)
- Power source (Battery/External)
- Charging status

**Outputs:**
- Individual output toggles (Main, Mount, Camera, Focuser)
- Per-output voltage and current

### 🔔 Notifications Panel

Notification channel configuration.

**Email Channel:**
- SMTP host, port
- TLS enable
- Authentication
- From/To addresses
- Test button

**Webhook Channel:**
- Target URL
- Auth token
- Test button

**Event List:**
- Real-time event log
- Severity filtering
- Event details

### ⚙ Settings Dialog

Application settings.

**Connection:**
- gRPC host/port
- SSL enable
- Certificate and key paths

**Logging:**
- Log level
- Log directory
- Log rotation period

## Custom Widgets

### 🌌 Sky Map

Interactive sky map using QGraphicsView:
- Zoom with mouse wheel
- Pan with click-and-drag
- Star rendering based on magnitude
- Constellation lines
- Coordinate grid

### 📊 Star Chart

Statistical star chart:
- Magnitude vs color scatter plot
- Custom paint rendering
- Configurable filters

### 📈 Focus Graph

V-curve display for auto-focus:
- Real-time plotting during scanning
- Best focus position marker
- HFD axis and position axis
- Fit quality display (R²)

### 📉 Weather Plot

Time-series weather data:
- Multi-line plot (temperature, humidity, pressure)
- Configurable time range (1h, 6h, 24h, 7d)
- Auto-scaling axes
- Interactive legend
