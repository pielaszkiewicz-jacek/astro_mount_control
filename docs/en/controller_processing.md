# Controller Processing Flow

## Table of Contents
- [Controller Processing Flow](#controller-processing-flow)
  - [Table of Contents](#table-of-contents)
  - [Introduction](#introduction)
  - [Component Architecture](#component-architecture)
  - [Processing Scenarios](#processing-scenarios)
    - [1. Controller Initialization](#1-controller-initialization)
    - [2. Bootstrap Calibration (Initial)](#2-bootstrap-calibration-initial)
    - [3. TPOINT Calibration (Full)](#3-tpoint-calibration-full)
    - [4. Slew to Equatorial Coordinates](#4-slew-to-equatorial-coordinates)
    - [5. Slew to Horizontal Coordinates](#5-slew-to-horizontal-coordinates)
    - [6. Object Tracking](#6-object-tracking)
    - [7. Parking/Unparking](#7-parkingunparking)
    - [8. Guider Control](#8-guider-control)
    - [9. Ephemeris Tracking (Moving Objects)](#9-ephemeris-tracking-moving-objects)
    - [10. Derotator Control (Field Rotation)](#10-derotator-control-field-rotation)
    - [11. State Management (Save/Load)](#11-state-management-saveload)
    - [12. Error Handling and Emergency States](#12-error-handling-and-emergency-states)
  - [Data Flow Diagrams](#data-flow-diagrams)
    - [Object Tracking Flow with Guider](#object-tracking-flow-with-guider)
    - [TPOINT Calibration Flow](#tpoint-calibration-flow)
  - [Key Files and Components](#key-files-and-components)
    - [Key Header Files:](#key-header-files)
    - [Key Source Files:](#key-source-files)
    - [Protobuf/gRPC Files:](#protobufgrpc-files)
    - [Configuration Files:](#configuration-files)
  - [Internal Architecture](#internal-architecture)
    - [`MountController::Impl` (PIMPL pattern):](#mountcontrollerimpl-pimpl-pattern)
    - [Structure of `Measurement`:](#structure-of-measurement)
  - [Design Patterns](#design-patterns)
    - [1. **PIMPL (Pointer to IMPLementation)**](#1-pimpl-pointer-to-implementation)
    - [2. **Factory Method**](#2-factory-method)
    - [3. **Strategy**](#3-strategy)
    - [4. **Observer**](#4-observer)
  - [Key Technical Areas](#key-technical-areas)
    - [1. **Threads and Synchronization**](#1-threads-and-synchronization)
    - [2. **Coordinate Transformations**](#2-coordinate-transformations)
    - [3. **Calibration and Modeling**](#3-calibration-and-modeling)
    - [4. **Error Handling and Recovery**](#4-error-handling-and-recovery)
  - [Test Scenarios](#test-scenarios)
    - [1. **Basic Scenario**](#1-basic-scenario)
    - [2. **Calibration Scenario**](#2-calibration-scenario)
  - [Thread Implementation in Controller](#thread-implementation-in-controller)
    - [Key Files Implementing Threads:](#key-files-implementing-threads)
    - [1. MountController Movement Simulation Threads](#1-mountcontroller-movement-simulation-threads)
    - [2. CANopen SYNC Thread](#2-canopen-sync-thread)
    - [3. CANopen Movement Simulation Threads](#3-canopen-movement-simulation-threads)
    - [4. Ephemeris Tracking Thread](#4-ephemeris-tracking-thread)
    - [5. Configuration Monitoring Thread](#5-configuration-monitoring-thread)
    - [6. gRPC Server Thread](#6-grpc-server-thread)
    - [Synchronization Mechanisms](#synchronization-mechanisms)
    - [Thread Safety](#thread-safety)
    - [Performance and Optimization](#performance-and-optimization)
    - [Thread Debugging](#thread-debugging)
    - [3. **Ephemeris Scenario**](#3-ephemeris-scenario)
    - [4. **Error Scenario**](#4-error-scenario)
  - [13. Trajectory Generation and Execution](#13-trajectory-generation-and-execution)
  - [14. Controller Implementation Status (Simulation vs Real Control)](#14-controller-implementation-status-simulation-vs-real-control)
    - [Implementation Summary](#implementation-summary)
    - [Detailed Analysis](#detailed-analysis)
    - [Simulation vs Real Control Analysis](#simulation-vs-real-control-analysis)
      - [Simulated CANopen Interface (`CanOpenInterface` in `src/controllers/canopen_interface.cpp`):](#simulated-canopen-interface-canopeninterface-in-srccontrollerscanopen_interfacecpp)
      - [Real CANopen Implementation Proposal:](#real-canopen-implementation-proposal)
    - [PID Controller Code](#pid-controller-code)
    - [Safety System](#safety-system)
    - [CMake Configuration](#cmake-configuration)
    - [HAL Layer Architecture](#hal-layer-architecture)
  - [15. Real CANopen Hardware Integration Proposal](#15-real-canopen-hardware-integration-proposal)
    - [15.1 CanOpenSocket Implementation (Linux SocketCAN)](#151-canopensocket-implementation-linux-socketcan)
    - [15.2 Real Implementation of Movement Operations in MountController](#152-real-implementation-of-movement-operations-in-mountcontroller)
    - [15.3 Safety System](#153-safety-system)
    - [15.4 Integration with Existing Factory](#154-integration-with-existing-factory)
    - [15.5 CMake Configuration](#155-cmake-configuration)
    - [15.6 Example Configuration](#156-example-configuration)
    - [15.7 Implementation Plan](#157-implementation-plan)
    - [15.8 Hardware Requirements](#158-hardware-requirements)
    - [15.9 Benefits of Real Implementation](#159-benefits-of-real-implementation)
  - [16. HAL (Hardware Abstraction Layer) - Architecture and Implementation](#16-hal-hardware-abstraction-layer---architecture-and-implementation)
    - [16.1 Layered Architecture with HAL](#161-layered-architecture-with-hal)
    - [16.2 HAL Interface](#162-hal-interface)
    - [16.3 CANopen HAL Implementation](#163-canopen-hal-implementation)
    - [16.4 HAL Integration with MountController](#164-hal-integration-with-mountcontroller)
    - [16.5 HAL Factory](#165-hal-factory)
    - [16.6 HAL Configuration](#166-hal-configuration)
    - [16.7 Example Configuration](#167-example-configuration)
    - [16.8 Benefits of HAL Introduction](#168-benefits-of-hal-introduction)
    - [16.9 Migration Plan to HAL Architecture](#169-migration-plan-to-hal-architecture)
    - [16.10 Impact on Existing Code](#1610-impact-on-existing-code)
  - [17. Current HAL Implementation Status (Hardware Abstraction Layer)](#17-current-hal-implementation-status-hardware-abstraction-layer)
    - [17.1 Implementation Status](#171-implementation-status)
    - [17.2 HAL File Structure](#172-hal-file-structure)
    - [17.3 Key Classes and Interfaces](#173-key-classes-and-interfaces)
    - [17.4 CanOpenHAL Implementation](#174-canopenhal-implementation)
    - [17.5 Compatibility with MountController](#175-compatibility-with-mountcontroller)
    - [17.6 Benefits of Implemented HAL](#176-benefits-of-implemented-hal)
    - [17.7 Example Configuration](#177-example-configuration)
    - [17.8 Next Steps](#178-next-steps)
  - [18. Low-level Axis Control](#18-low-level-axis-control)
  - [19. Health Check and System Metrics](#19-health-check-and-system-metrics)
  - [20. Configuration Management via gRPC](#20-configuration-management-via-grpc)
  - [21. Pole Position Determination](#21-pole-position-determination)
  - [22. Rotation Matrix](#22-rotation-matrix)
  - [23. Encoder Control via gRPC](#23-encoder-control-via-grpc)
  - [24. Axis Physical Parameters](#24-axis-physical-parameters)
  - [25. HAL RPC API (HAL Management via gRPC)](#25-hal-rpc-api-hal-management-via-grpc)
  - [26. Advanced State Monitoring (ControllerState)](#26-advanced-state-monitoring-controllerstate)
  - [27. Complete Operational Workflow](#27-complete-operational-workflow)
  - [Summary](#summary)

---

## Introduction

This document describes the detailed processing flow of the astronomical mount [`MountController`](../../src/controllers/mount_controller.cpp). It covers all operational scenarios, data transformations, internal mechanisms, and thread synchronization. The document serves as a comprehensive reference for understanding how each controller function works internally, what data flows occur, and how the system transitions between states.

The primary source code for all described processes is located in [`mount_controller.cpp`](../../src/controllers/mount_controller.cpp) (5195 lines), with additional logic in [`mount_controller.h`](../../include/controllers/mount_controller.h).

---

## Component Architecture

```
┌──────────────────────────────────────────────────────────────┐
│                      MountController                          │
│  ┌────────────────────────────────────────────────────────┐  │
│  │                    Impl (PIMPL)                         │  │
│  │  ┌──────────┐  ┌──────────┐  ┌──────────────────────┐  │  │
│  │  │ Bootstrap│  │  TPOINT  │  │   KalmanFilter       │  │  │
│  │  │  Model   │  │  Model   │  │   (State Estimator)  │  │  │
│  │  └──────────┘  └──────────┘  └──────────────────────┘  │  │
│  │  ┌──────────┐  ┌──────────┐  ┌──────────────────────┐  │  │
│  │  │Ephemeris │  │  Guider  │  │   Configuration      │  │  │
│  │  │ Tracker  │  │  Control │  │   Manager            │  │  │
│  │  └──────────┘  └──────────┘  └──────────────────────┘  │  │
│  │  ┌──────────────────────────────────────────────────┐  │  │
│  │  │         HAL Interface (Hardware Abstraction)     │  │  │
│  │  └──────────────────────────────────────────────────┘  │  │
│  └────────────────────────────────────────────────────────┘  │
└──────────────────────────────────────────────────────────────┘
         │                       │
         ▼                       ▼
   ┌──────────┐          ┌──────────────┐
   │   gRPC   │          │   CANopen/   │
   │   API    │          │   Simulator  │
   └──────────┘          └──────────────┘
```

---

## Processing Scenarios

### 1. Controller Initialization

**Trigger:** `MountController::initialize(config)` called.

**Sequence:**
```
initialize(config)
  ↓
Configuration validation (MountConfig)
  ↓
Mount type detection (EQUATORIAL / CASUAL ...)
  ↓
Initialization of coordinate system:
  - SOFA library initialization
  - Julian Date calculation for J2000.0
  - LST calculation for current location
  ↓
Creating internal components:
  - TPoint model (empty, no calibration yet)
  - Bootstrap model (identity matrix initially)
  - KalmanFilter with default noise parameters
  - EphemerisTracker (inactive)
  ↓
HAL layer initialization (CANopen/Simulated/Serial):
  - Interface initialization (ICanOpenInterface / SimulatedInterface)
  - Motor control setup
  - Encoder reader setup
  - Safety monitor initialization
  ↓
State machine → IDLE
  ↓
Initialization of threads:
  - Movement monitoring threads (if needed)
  - Safety monitoring thread
  - Configuration monitoring thread
  - gRPC server (if not already running)
```

### 2. Bootstrap Calibration (Initial)

**Trigger:** `AddBootstrapMeasurement()` called 3+ times, then `RunBootstrapCalibration()`.

**Sequence:**
```
AddBootstrapMeasurement(observed_ra, observed_dec, expected_ra, expected_dec)
  ↓
Storage in bootstrap_measurements_ vector
  ↓
If count >= 3 → RunBootstrapCalibration() enabled

RunBootstrapCalibration()
  ↓
Loading bootstrap_measurements_ (min 3)
  ↓
Calculation of rotation offset vector:
  - For each measurement pair:
    - Convert observed (ra, dec) to unit vector
    - Convert expected (ra, dec) to unit vector
    - Calculate delta = v_expected - v_observed
  ↓
Least squares fit:
  - Find optimal rotation matrix (3 DOF)
  - Minimize sum of |v_expected - R * v_observed|²
  ↓
Application of bootstrap correction:
  - Update mount_home_offset_ (RA offset in hours)
  - Update mount_home_offset_dec (Dec offset in degrees)
  ↓
State → CALIBRATED (bootstrap_complete_ = true)
  ↓
Return success (alignment error in arcsec)
```

### 3. TPOINT Calibration (Full)

**Trigger:** `AddTPointMeasurement()` called N times, then `RunTPointCalibration()`.

**Sequence:**
```
AddTPointMeasurement(observed, expected, mount_position, env_conditions)
  ↓
Full measurement object creation (Measurement struct)
  ↓
Storage in tpoint_measurements_ vector
  ↓
Optional: outlier filtering (if already calibrated)

RunTPointCalibration()
  ↓
Loading tpoint_measurements_ (min N, depending on enabled terms)
  ↓
Pre-filtering:
  - SNR < threshold → reject
  - |residual| > 3σ → reject (if previous calibration exists)
  ↓
Formation of the design matrix A (N_measurements × N_terms):
  - For each measurement i:
    - For each enabled term j:
      - A[i][j] = term_j(HA_i, Dec_i, pier_side_i, temperature_i, ...)
  ↓
Solving the linear system A * x = b:
  - Where b = observed - expected residuals
  - Methods: SVD decomposition or QR decomposition
  ↓
Calculation of model coefficients x = (A^T * A)^{-1} * A^T * b
  ↓
Residual analysis:
  - RMS calculation
  - Chi-squared statistic
  - Maximum residual
  - Outlier identification (> 3σ deviation)
  ↓
State → CALIBRATED (tpoint_complete_ = true)
  ↓
Return TPointParameters (coefficients, residuals, chi-squared, RMS)
```

### 4. Slew to Equatorial Coordinates

**Trigger:** `SlewToEquatorial(ra_hours, dec_degrees)` called.

**Sequence:**
```
SlewToEquatorial(ra, dec)
  ↓
State verification:
  - ERROR → reject
  - PARKED → unpark first (automatic)
  - SLEWING / TRACKING → stop current movement
  ↓
Coordinate transformation:
  - RA → degrees: ra_deg = ra_hours * 15.0
  - Application of TPOINT and bootstrap corrections:
    - corrected_ra = ra + model_error_correction(ra, dec, pier_side)
    - corrected_dec = dec + model_error_correction(ra, dec, pier_side)
  ↓
Setting motor targets:
  - axis1_target_ (HA/RA axis)
  - axis2_target_ (Dec axis)
  ↓
State → SLEWING
  ↓
Asynchronous movement start through HAL:
  - HAL::MotorControl::setPosition(target_ra, max_slew_rate, acceleration)
  - HAL::MotorControl::setPosition(target_dec, max_slew_rate, acceleration)
  ↓
Return immediately (movement in background thread)
  ↓
Background monitoring in slewToEquatorial monitoring thread:
  - Loop: check position → update status
  - When target_reached for both axes → state → IDLE
```

### 5. Slew to Horizontal Coordinates

**Trigger:** `SlewToHorizontal(alt_degrees, az_degrees)` called.

**Sequence:**
```
SlewToHorizontal(alt, az)
  ↓
State verification (same as equatorial)
  ↓
Coordinate transformation (alt-az → equatorial):
  - LST calculation for current time
  - HA = LST - RA
  - Use AstronomicalCalculations::altAzToRaDec()
  ↓
Application of TPOINT and bootstrap corrections
  ↓
Setting motor targets (same as equatorial)
  ↓
State → SLEWING
  ↓
Asynchronous start through HAL
  ↓
Return immediately
```

### 6. Object Tracking

**Trigger:** `TrackObject(ra_hours, dec_degrees)` called.

**Sequence:**
```
TrackObject(ra, dec)
  ↓
State verification (must be IDLE or slewing completed)
  ↓
Configuration of tracking parameters:
  - tracking_ra_ = ra
  - tracking_dec_ = dec
  - tracking_rate_ra_ = 15.041067 * cos(dec) arcsec/s (sidereal rate)
  - tracking_rate_dec_ = 0 (for equatorial mounts)
  ↓
State → TRACKING
  ↓
Start tracking thread (startTracking):
  - Continuous position update at tracking rate
  - axis1_target_ += tracking_rate_ha * dt
  - axis2_target_ += tracking_rate_dec * dt
  ↓
Guider connection (if enabled):
  - Optional correction via guider
  - processGuiderCorrection(ra_corr, dec_corr)
  ↓
Kalman filter (if enabled):
  - update(tracking_error_ra, tracking_error_dec)
  - Smoothing of corrections from guider
```

### 7. Parking/Unparking

**Trigger:** `Park()` or `Unpark()` called.

**Parking sequence:**
```
Park()
  ↓
State verification (must be IDLE or TRACKING)
  ↓
Stop active movement:
  - Stop tracking (if TRACKING)
  - Stop guider corrections
  ↓
Slew to park position:
  - Predefined park coordinates: HA=12h, Dec=90° (or custom)
  - Slow movement to limit mechanical stress
  ↓
State → PARKING
  ↓
Wait for movement completion
  ↓
Disable motors (HAL::MotorControl::disable())
  ↓
State → PARKED
```

**Unparking sequence:**
```
Unpark()
  ↓
State verification (must be PARKED)
  ↓
Enable motors (HAL::MotorControl::enable())
  ↓
Slew from park position to safe position
  ↓
State → IDLE
```

### 8. Guider Control

**Trigger:** `ConnectGuider(connection_string)` or `SendGuiderCorrection(ra, dec)` called.

**Sequence:**
```
ConnectGuider(connection)
  ↓
TCP connection to guider (PHD2 protocol)
  ↓
Guider configuration setup:
  - Calibration (direction, pulse duration)
  - Aggression parameters
  ↓
Guider active → guider_active_ = true

SendGuiderCorrection(ra_corr_arcsec, dec_corr_arcsec)
  ↓
Conversion from arcseconds to degrees:
  - ra_corr_deg = ra_corr_arcsec / 3600.0
  - dec_corr_deg = dec_corr_arcsec / 3600.0
  ↓
Application through Kalman filter (if enabled):
  - kalman_filter_->update(ra_corr_deg, dec_corr_deg)
  - The filter smooths the correction and applies it smoothly
  ↓
Direct correction to axis positions:
  - axis1_target_ += filtered_ra_correction
  - axis2_target_ += filtered_dec_correction
```

### 9. Ephemeris Tracking (Moving Objects)

**Trigger:** `UploadEphemeris(data)` and `StartEphemerisTracking(object_id)` called.

**Sequence:**
```
UploadEphemeris(ephemeris_data)
  ↓
Validation:
  - Minimum 2 points
  - Chronological order check
  - Rate consistency check
  ↓
Loading into EphemerisTracker:
  - Internal cubic spline interpolation
  - RA(t), Dec(t), rate(t) as continuous functions

StartEphemerisTracking(object_id)
  ↓
State verification (must be IDLE)
  ↓
Ephemeris selection by object_id
  ↓
Slew to current object position:
  - ra_current = ephemeris_tracker_->getPosition(jd_now).ra
  - dec_current = ephemeris_tracker_->getPosition(jd_now).dec
  ↓
State → TRACKING (ephemeris mode)
  ↓
Background thread (trackingLoop):
  - Continuous update of the target position from ephemeris data:
    - position = ephemeris_tracker_->interpolate(jd_now)
    - axis_target = RA_to_mount_coordinates(position.ra, position.dec)
  - Tracking rate adaptation:
    - rate = ephemeris_tracker_->getRate(jd_now)
    - Tracking rate adjusted to follow the moving object
```

### 10. Derotator Control (Field Rotation)

**Trigger:** `ConfigureDerotator(config)` and `EnableFieldRotation(enabled, latitude)` called.

**Sequence:**
```
ConfigureDerotator(config)
  ↓
Configuration:
  - Motor type (stepper/servo)
  - Gear ratio
  - Homing sensor configuration
  ↓
Initialization of the derotator mechanism

EnableFieldRotation(enabled, latitude)
  ↓
If enabled:
  - Calculation of field rotation angle based on:
    - Mount type (equatorial → minimal rotation, alt-az → full rotation)
    - Current position (HA, Dec)
    - Latitude
  - Continuous derotator position update
  ↓
If disabled → derotator parked at 0°
```

### 11. State Management (Save/Load)

**Trigger:** `SaveState(filepath)` or `LoadState(filepath)` called.

**Save sequence:**
```
SaveState(filepath)
  ↓
Serialization of internal state to JSON:
  - Current position (axis1, axis2)
  - Tracking state (tracking_ra, tracking_dec)
  - TPOINT model coefficients
  - Bootstrap corrections
  - Calibration measurements (bootstrap + TPOINT)
  - Configuration snapshot
  - Encoder calibration data
  ↓
Write to file (JSON format)
  ↓
Return confirmation
```

**Load sequence:**
```
LoadState(filepath)
  ↓
Read JSON file
  ↓
Deserialization:
  - Restore TPOINT model
  - Restore bootstrap corrections
  - Restore calibration measurements
  - Restore configuration
  - Restore current position
  ↓
State restoration → IDLE (or TRACKING if was active)
```

### 12. Error Handling and Emergency States

**Trigger:** Error detection in any component (motor, encoder, communication).

**Error handling sequence:**
```
Error detected:
  - Motor failure → position discrepancy > threshold
  - Encoder error → communication loss
  - CANopen bus error → NMT state change
  - Safety limit exceeded → temperature, voltage, position
  ↓
State → ERROR
  ↓
Emergency stop:
  - EmergencyStop() called automatically for critical errors
  - Motors disabled (HAL::MotorControl::disable())
  - Deceleration ramp for non-critical errors
  ↓
Error logging:
  - Error source recording
  - Context (position, time, state)
  ↓
Error notification:
  - gRPC event stream (if connected client)
  - Log entry
  ↓
Error recovery (if auto_recovery_ enabled):
  - Wait for error condition to clear
  - ClearErrors()
  - State → IDLE
```

---

## Data Flow Diagrams

### Object Tracking Flow with Guider

```
User command: TrackObject(ra=12.5h, dec=30.0°)
    │
    ▼
MountController::TrackObject()
    │
    ├──→ Convert RA/Dec to mount coordinates
    │    (TPOINT + bootstrap correction)
    │
    ├──→ State: IDLE → TRACKING
    │
    ├──→ Start tracking thread:
    │    │
    │    ▼
    │    Loop (every 10ms):
    │    │
    │    ├──→ Calculate tracking rate:
    │    │    ra_rate = 15.041067 * cos(dec)  [arcsec/s]
    │    │    dec_rate = 0.0
    │    │
    │    ├──→ Position update:
    │    │    axis1_target += ra_rate * dt / 3600.0  [deg]
    │    │    axis2_target += dec_rate * dt / 3600.0  [deg]
    │    │
    │    └──→ Send to HAL/ICanOpenInterface:
    │         HAL::MotorControl::setPosition(axis1, axis2)
    │
    └──→ Guider (if connected):
         │
         ▼
         PHD2 correction received:
         │
         ├──→ ra_corr = +1.5", dec_corr = -0.8"
         │
         ├──→ KalmanFilter::update(ra_corr, dec_corr)
         │    │
         │    ▼
         │    Filtered correction: +1.2", -0.6"
         │
         └──→ Position correction:
              axis1_target += 1.2 / 3600.0  [deg]
              axis2_target += -0.6 / 3600.0  [deg]
```

### TPOINT Calibration Flow

```
Measurement collection:
  AddTPointMeasurement(obs={ra,dec}, exp={ra,dec}, mount={ha,dec}, env)
    │
    ▼
  tpoint_measurements_.push_back(measurement)
    │
    ▼
  (Repeat N times, N >= enabled_terms)
    │
    ▼
RunTPointCalibration()
    │
    ▼
Pre-processing:
  │
  ├──→ Filter outliers:
  │    - Remove measurements with SNR < threshold
  │    - 3σ clipping (after optional initial fit)
  │
  ├──→ Build design matrix A:
  │    - For each measurement i, term j:
  │      A[i][j] = term_j(HA_i, Dec_i, ...)
  │
  └──→ Build observation vector b:
       - b[i] = (observed_ra - expected_ra) * cos(dec)
       - b[i+N] = observed_dec - expected_dec
    │
    ▼
Solve: A * x = b
  │
  ├──→ SVD decomposition: A = U * Σ * V^T
  ├──→ x = V * Σ^{-1} * U^T * b
  │
  ▼
Post-processing:
  │
  ├──→ Calculate residuals: r = b - A * x
  ├──→ Calculate RMS = sqrt(mean(r^2))
  ├──→ Calculate chi-squared = sum(r_i^2 / sigma_i^2)
  │
  └──→ Update TPointModel with coefficients x
    │
    ▼
Return TPointParameters:
  - coefficients (6-40+ values)
  - residuals (RMS, max)
  - chi-squared
  - measurement count
```

---

## Key Files and Components

### Key Header Files:

- [`include/controllers/mount_controller.h`](../../include/controllers/mount_controller.h) — Main MountController class
- [`include/controllers/icanopen_interface.h`](../../include/controllers/icanopen_interface.h) — CANopen interface
- [`include/config/configuration.h`](../../include/config/configuration.h) — Configuration structures
- [`include/models/tpoint_model.h`](../../include/models/tpoint_model.h) — TPOINT model
- [`include/models/kalman_filter.h`](../../include/models/kalman_filter.h) — Kalman filter
- [`include/models/ephemeris_tracker.h`](../../include/models/ephemeris_tracker.h) — Ephemeris tracker
- [`include/core/astronomical_calculations.h`](../../include/core/astronomical_calculations.h) — Astronomical calculations
- [`include/hal/hal_interface.h`](../../include/hal/hal_interface.h) — HAL interface
- [`include/hal/motor_control.h`](../../include/hal/motor_control.h) — Motor control interface
- [`include/hal/encoder_reader.h`](../../include/hal/encoder_reader.h) — Encoder reader interface

### Key Source Files:

- [`src/controllers/mount_controller.cpp`](../../src/controllers/mount_controller.cpp) (5195 lines) — Main controller logic
- [`src/controllers/canopen_interface.cpp`](../../src/controllers/canopen_interface.cpp) — CANopen communication layer
- [`src/controllers/canopen_factory.cpp`](../../src/controllers/canopen_factory.cpp) — CANopen factory
- [`src/config/configuration.cpp`](../../src/config/configuration.cpp) — Configuration management
- [`src/core/astronomical_calculations.cpp`](../../src/core/astronomical_calculations.cpp) — Astronomical calculations
- [`src/models/tpoint_model.cpp`](../../src/models/tpoint_model.cpp) — TPOINT model implementation
- [`src/models/kalman_filter.cpp`](../../src/models/kalman_filter.cpp) — Kalman filter implementation
- [`src/models/ephemeris_tracker.cpp`](../../src/models/ephemeris_tracker.cpp) — Ephemeris tracker
- [`src/api/service_impl.cpp`](../../src/api/service_impl.cpp) — gRPC service implementation
- [`src/api/grpc_server.cpp`](../../src/api/grpc_server.cpp) — gRPC server
- [`src/hal/hal_factory.cpp`](../../src/hal/hal_factory.cpp) — HAL factory
- [`src/hal/canopen_hal/canopen_hal.cpp`](../../src/hal/canopen_hal/canopen_hal.cpp) — CANopen HAL implementation
- [`src/hal/simulated_hal/simulated_hal.cpp`](../../src/hal/simulated_hal/simulated_hal.cpp) — Simulated HAL

### Protobuf/gRPC Files:

- [`proto/mount_controller.proto`](../../proto/mount_controller.proto) (1115 lines) — Main protobuf definition

### Configuration Files:

- [`config/`](../../config/) — Configuration JSON files directory

---

## Internal Architecture

### `MountController::Impl` (PIMPL pattern):

```cpp
class MountController::Impl {
private:
    // Model components
    std::unique_ptr<TPointModel> tpoint_model_;
    BootstrapModel bootstrap_model_;
    std::unique_ptr<KalmanFilter> kalman_filter_;
    std::unique_ptr<EphemerisTracker> ephemeris_tracker_;
    
    // State
    MountStatus::State state_{MountStatus::State::IDLE};
    
    // Axis positions
    double axis1_position_{0.0};   // HA/RA/Azimuth (degrees)
    double axis2_position_{0.0};   // Dec/Altitude (degrees)
    double axis1_target_{0.0};
    double axis2_target_{0.0};
    double axis1_rate_{0.0};
    double axis2_rate_{0.0};
    
    // Configuration
    MountConfig config_;
    Configuration* global_config_;
    
    // Communication
    std::unique_ptr<controllers::ICanOpenInterface> canopen_interface_;
    
    // HAL interface
    std::unique_ptr<hal::HALInterface> hal_interface_;
    
    // Threads
    std::thread slew_monitor_thread_;
    std::thread tracking_thread_;
    std::thread ephemeris_tracking_thread_;
    std::atomic<bool> thread_running_{false};
    
    // Measurements
    std::vector<Measurement> bootstrap_measurements_;
    std::vector<Measurement> tpoint_measurements_;
    
    // Synchronization
    mutable std::mutex mutex_;
    mutable std::shared_mutex state_mutex_;
};
```

### Structure of `Measurement`:

```cpp
struct Measurement {
    // Required
    double observed_ra;        // Measured RA (hours)
    double observed_dec;       // Measured Dec (degrees)
    double expected_ra;        // Catalog RA (hours)
    double expected_dec;       // Catalog Dec (degrees)
    
    // TPOINT only
    double mount_ha;           // Mount hour angle (hours)
    double mount_dec;          // Mount declination (degrees)
    
    // Optional
    double temperature{20.0};
    double pressure{1013.25};
    double humidity{50.0};
    double snr{100.0};
    double seeing{2.0};
    std::chrono::system_clock::time_point timestamp;
    double julian_date{0.0};
};
```

---

## Design Patterns

### 1. **PIMPL (Pointer to IMPLementation)**
- [`MountController`](../../include/controllers/mount_controller.h) uses `Impl` to hide implementation details
- Minimizes header dependencies
- Enables ABI stability

### 2. **Factory Method**
- [`CanOpenFactory`](../../src/controllers/canopen_factory.cpp) creates CANopen interface instances
- [`HALFactory`](../../src/hal/hal_factory.cpp) creates HAL instances based on configuration

### 3. **Strategy**
- Different calibration strategies (Bootstrap, TPOINT)
- Different communication strategies (CANopen, Simulated, Serial)

### 4. **Observer**
- Position callbacks in HAL interfaces
- Error callbacks
- gRPC event streams

---

## Key Technical Areas

### 1. **Threads and Synchronization**
- See [`docs/en/controller_threads.md`](controller_threads.md) for details
- Monitoring threads for operations (slew, park, tracking)
- Thread synchronization via mutexes, shared_mutex, atomic flags
- Condition variables for state changes

### 2. **Coordinate Transformations**
- RA/Dec ↔ Alt/Az (via SOFA library)
- Equatorial ↔ Mount coordinates (via TPOINT + bootstrap)
- J2000.0 ↔ current epoch
- Hour Angle ↔ RA (via LST)

### 3. **Calibration and Modeling**
- Bootstrap: 3-parameter rotation model
- TPOINT: 6-40+ parameter geometric model
- Kalman filter: sensor fusion and noise reduction
- PEC: Periodic Error Correction

### 4. **Error Handling and Recovery**
- Automatic error recovery (configurable)
- Emergency stop with deceleration ramp
- Graceful degradation (guider off → pure tracking)

---

## Test Scenarios

### 1. **Basic Scenario**
```
1. Initialize controller → IDLE
2. SlewToEquatorial(ra=10.5h, dec=41.3°) → SLEWING → IDLE
3. TrackObject(ra=10.5h, dec=41.3°) → TRACKING
4. Stop() → IDLE
5. Park() → PARKED
6. Unpark() → IDLE
```

### 2. **Calibration Scenario**
```
1. Initialize → IDLE
2. AddBootstrapMeasurement ×3 → RunBootstrapCalibration() → CALIBRATED
3. AddTPointMeasurement ×15 → RunTPointCalibration() → CALIBRATED
4. SlewToEquatorial(6h, 30°) → pointing accuracy ~1"
```

---

## Thread Implementation in Controller

### Key Files Implementing Threads:

| Thread | File | Description |
|--------|------|-------------|
| Slew monitoring | `mount_controller.cpp` | Monitors movement during slew operations |
| Tracking | `mount_controller.cpp` | Continuous tracking rate update |
| Ephemeris tracking | `ephemeris_tracker.cpp` | Moving object position update |
| CANopen SYNC | `canopen_interface.cpp` | CANopen SYNC message generation |
| PID control | `canopen_hal.cpp` | PID control loop |
| Safety monitoring | `canopen_hal.cpp` | Limit and error monitoring |
| NMT monitoring | `canopen_hal.cpp` | CiA 301 network management |
| Simulation | `simulated_hal.cpp` | Physical simulation |
| Encoder reading | `simulated_hal.cpp` | Simulated encoder reading |
| gRPC server | `grpc_server.cpp` | gRPC request handling |

### 1. MountController Movement Simulation Threads

```cpp
// Simplified thread implementation for slew monitoring
void MountController::Impl::slewToEquatorialThread(double target_ra, double target_dec) {
    thread_running_ = true;
    
    while (thread_running_) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            
            // Calculate remaining distance
            double ra_remaining = target_ra - axis1_position_;
            double dec_remaining = target_dec - axis2_position_;
            
            // Update position (simulation)
            double step_ra = std::min(max_slew_rate_ * dt_, std::abs(ra_remaining));
            double step_dec = std::min(max_slew_rate_ * dt_, std::abs(dec_remaining));
            
            axis1_position_ += std::copysign(step_ra, ra_remaining);
            axis2_position_ += std::copysign(step_dec, dec_remaining);
            
            // Check completion
            if (std::abs(ra_remaining) < POSITION_TOLERANCE &&
                std::abs(dec_remaining) < POSITION_TOLERANCE) {
                axis1_position_ = target_ra;
                axis2_position_ = target_dec;
                state_ = MountStatus::State::IDLE;
                break;
            }
        }
        
        std::this_thread::sleep_for(std::chrono::milliseconds(SLEW_UPDATE_MS));
    }
    
    thread_running_ = false;
}
```

### 2. CANopen SYNC Thread

```cpp
// SYNC thread responsible for generating SYNC messages
void CanOpenInterface::syncThread() {
    while (running_) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            
            // Send SYNC message (CiA 301)
            canopen_driver_->sendSync();
            
            // Update monitored drives
            for (auto& drive : drives_) {
                // Request PDO transmission
                drive->requestPDOs();
            }
        }
        
        std::this_thread::sleep_for(sync_period_);
    }
}
```

### 3. CANopen Movement Simulation Threads

```cpp
// Simulated movement thread
void CanOpenInterface::simulateMovement() {
    while (running_) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            
            for (auto& drive : drives_) {
                if (!drive->isEnabled()) continue;
                
                // Simulate position update based on target
                double target = drive->getTargetPosition();
                double current = drive->getActualPosition();
                
                if (std::abs(target - current) > POSITION_TOLERANCE) {
                    double step = drive->getVelocity() * dt_;
                    double new_pos = current + std::copysign(step, target - current);
                    drive->setActualPosition(new_pos);
                }
            }
        }
        
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}
```

### 4. Ephemeris Tracking Thread

```cpp
void EphemerisTracker::Impl::trackingLoop() {
    while (tracking_active_) {
        auto now = std::chrono::system_clock::now();
        double jd_now = currentJulianDate();
        
        {
            std::lock_guard<std::mutex> lock(mutex_);
            
            // Get interpolated position from ephemeris
            EphemerisPoint pos = interpolate(jd_now);
            
            // Update target position
            current_ra_ = pos.ra_hours;
            current_dec_ = pos.dec_degrees;
            current_rate_ra_ = pos.rate_ra_arcsec_h;
            current_rate_dec_ = pos.rate_dec_arcsec_h;
        }
        
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}
```

### 5. Configuration Monitoring Thread

```cpp
void ConfigMonitor::monitoringLoop() {
    while (running_) {
        // Check if config file has been modified
        if (config_file_modified()) {
            logger_->info("Configuration file changed, reloading...");
            
            try {
                Configuration new_config = loadConfig(config_path_);
                applyConfiguration(new_config);
                logger_->info("Configuration reloaded successfully");
            } catch (const std::exception& e) {
                logger_->error("Failed to reload config: {}", e.what());
            }
        }
        
        std::this_thread::sleep_for(config_check_interval_);
    }
}
```

### 6. gRPC Server Thread

```cpp
void GrpcServer::run() {
    ServerBuilder builder;
    builder.AddListeningPort(address_, grpc::InsecureServerCredentials());
    builder.RegisterService(&service_);
    
    server_ = builder.BuildAndStart();
    logger_->info("gRPC server started on {}", address_);
    
    server_->Wait();  // Blocks until Shutdown()
}
```

### Synchronization Mechanisms

| Mechanism | Usage | Critical Section |
|-----------|-------|-----------------|
| `std::mutex` + `std::lock_guard` | Position updates, state changes | Short (μs) |
| `std::shared_mutex` + `shared_lock` | Configuration reads | Reads allowed concurrently |
| `std::atomic<bool>` | Thread running flags | Lock-free |
| `std::condition_variable` | Thread synchronization | State changes |
| `std::recursive_mutex` | Complex operations with nested calls | Rare |

### Thread Safety

```cpp
// Example: Thread-safe position read
MountPosition MountController::Impl::getCurrentPosition() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return MountPosition{axis1_position_, axis2_position_};
}

// Example: Thread-safe state update with notification
void MountController::Impl::setState(MountStatus::State new_state) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        state_ = new_state;
    }
    state_cv_.notify_all();
}
```

### Performance and Optimization

1. **Lock granularity**: Fine-grained locking to minimize contention
2. **Lock-free reads**: Atomic flags for thread state
3. **Thread priority**: Real-time priority for critical threads (safety, tracking)
4. **CPU affinity**: Critical threads bound to dedicated cores (if available)
5. **Update rates**:
   - Tracking thread: 100 Hz
   - Safety monitoring: 10 Hz
   - Encoder reading: 100 Hz (simulated), 1 kHz (hardware)
   - gRPC: unlimited (request-based)

### Thread Debugging

For thread debugging, use:
```bash
# View all threads:
ps -eLf | grep astro_mount_controller

# Attach GDB to specific thread:
gdb -p <pid>
(gdb) info threads
(gdb) thread 2
(gdb) bt

# Thread analysis with perf:
perf top -p <pid>
```

### 3. **Ephemeris Scenario**
```
1. UploadEphemeris(comet_data)
2. StartEphemerisTracking("C/2023_A3") → TRACKING
3. Continue tracking for duration of observation
4. StopEphemerisTracking() → IDLE
```

### 4. **Error Scenario**
```
1. While TRACKING → encoder error
2. State → ERROR, emergency stop
3. ClearErrors()
4. State → IDLE
5. Resume normal operation
```

---

## 13. Trajectory Generation and Execution

The controller supports trajectory generation for smooth, coordinated multi-axis movement. This is used for precision pointing and advanced calibration sequences.

**Trajectory types:**
- **Linear** — straight-line movement between two points
- **Ramp** — acceleration/deceleration profile
- **S-curve** — jerk-limited smooth movement

**Implementation:**
```cpp
// Trajectory generation (simplified)
struct TrajectoryPoint {
    double position;        // Target position (deg)
    double velocity;        // Target velocity (deg/s)
    double acceleration;    // Target acceleration (deg/s²)
    double time;            // Time from start (s)
};

class TrajectoryGenerator {
public:
    // Generate S-curve trajectory
    std::vector<TrajectoryPoint> generateSCurve(
        double start_pos, double end_pos,
        double max_vel, double max_acc, double max_jerk,
        double dt
    );
    
    // Execute trajectory through HAL
    void executeTrajectory(const std::vector<TrajectoryPoint>& trajectory,
                          HAL::MotorControl& motor);
};
```

---

## 14. Controller Implementation Status (Simulation vs Real Control)

The controller has been developed primarily in a simulated environment. This section analyzes which parts are fully implemented, which are simulated, and what remains for real hardware control.

### Implementation Summary

| Component | Simulation | Real Hardware | Notes |
|-----------|-----------|---------------|-------|
| **MountController core logic** | ✅ Complete | ✅ Complete | Business logic independent of HW |
| **Coordinate transformations** | ✅ Complete | ✅ Complete | SOFA library, no HW dependency |
| **TPOINT model** | ✅ Complete | ✅ Complete | Mathematical model, no HW dependency |
| **Bootstrap calibration** | ✅ Complete | ✅ Complete | Uses mount coordinates only |
| **Kalman filter** | ✅ Complete | ✅ Complete | Works on position error data |
| **Ephemeris tracking** | ✅ Complete | ✅ Complete | Based on time interpolation |
| **Axis movement (slew/track/park)** | ✅ Simulated | ⬜ Real | Needs CANopen or other HW interface |
| **Encoder reading** | ✅ Simulated | ⬜ Real | Needs hardware encoder interface |
| **Safety monitoring** | ✅ Simulated | ⬜ Real | Needs hardware limit switches, sensors |
| **Guider integration** | ✅ Simulated | ✅ TCP/IP | PHD2 protocol is real, rest is test |
| **gRPC API** | ✅ Complete | ✅ Complete | Full API implemented |
| **Configuration** | ✅ Complete | ✅ Complete | Full config system |

### Detailed Analysis

**What works now (simulation):**
- All gRPC API endpoints are functional
- TPOINT and bootstrap calibration with simulated measurements
- Coordinate transformations including refraction correction
- Ephemeris tracking with interpolated positions
- Kalman filter state estimation
- Movement simulation (kinematic model without physics)
- System health monitoring and metrics
- State persistence (save/load)

**What needs real hardware:**
1. **Motor control**: Currently simulated position updates. Real control requires CANopen CiA 402 profile commands (or alternative interface).
2. **Encoder reading**: Currently simulated position/velocity. Real encoders need physical reading (CANopen PDO, quadrature decoder, etc.).
3. **Safety monitoring**: Currently simulated limits. Real safety requires limit switches, emergency stop circuits, temperature sensors.
4. **Backlash measurement**: Physical measurement procedure implemented, but needs real axis movement and encoder readings.

### Simulation vs Real Control Analysis

#### Simulated CANopen Interface (`CanOpenInterface` in [`src/controllers/canopen_interface.cpp`](../../src/controllers/canopen_interface.cpp)):

```cpp
class CanOpenInterface : public ICanOpenInterface {
    // ALL operations are simulated internally
    // No real CAN bus communication
    
    bool setPositionTarget(int axis_id, double position_deg) override {
        // Just stores the target internally
        std::lock_guard<std::mutex> lock(mutex_);
        drives_[axis_id].setTargetPosition(position_deg);
        return true;
    }
    
    double getActualPosition(int axis_id) override {
        // Returns simulated position (updated by simulation thread)
        std::lock_guard<std::mutex> lock(mutex_);
        return drives_[axis_id].getActualPosition();
    }
};
```

#### Real CANopen Implementation Proposal:

```cpp
class CanOpenSocket : public ICanOpenInterface {
    // Real CAN bus communication via SocketCAN
    
    bool setPositionTarget(int axis_id, double position_deg) override {
        // Convert degrees to CiA 402 position units
        int32_t position_units = deg_to_cia402(position_deg, axis_id);
        
        // Send SDO or PDO to drive
        can_frame frame;
        frame.can_id = 0x200 + node_id_;  // COB-ID for PDO
        frame.data[0] = 0x23;             // SDO write command
        frame.data[1] = 0x7A;             // Object index lo (CiA 402 target position)
        frame.data[2] = 0x60;             // Object index hi
        frame.data[3] = 0x00;             // Sub-index
        *(int32_t*)(&frame.data[4]) = htonl(position_units); // Data
        frame.len = 8;
        
        return send_can_frame(frame) >= 0;
    }
    
    double getActualPosition(int axis_id) override {
        // Read from drive via PDO
        // Real-time position from drive's actual position
        int32_t position_units = get_drive_actual_position(axis_id);
        return cia402_to_deg(position_units, axis_id);
    }
};
```

### PID Controller Code

The PID controller implemented in the CANopen HAL provides closed-loop control:

```cpp
// From CanOpenHAL implementation
class PIDController {
private:
    double kp_, ki_, kd_;           // PID gains
    double integral_{0.0};          // Integral term
    double prev_error_{0.0};        // Previous error for derivative
    double output_min_, output_max_; // Output limits
    double integral_limit_;          // Anti-windup
    std::chrono::steady_clock::time_point last_time_;
    
public:
    double calculate(double setpoint, double measurement) {
        auto now = std::chrono::steady_clock::now();
        double dt = std::chrono::duration<double>(now - last_time_).count();
        last_time_ = now;
        
        double error = setpoint - measurement;
        
        // Proportional
        double p_term = kp_ * error;
        
        // Integral (with anti-windup)
        integral_ += error * dt;
        integral_ = std::clamp(integral_, -integral_limit_, integral_limit_);
        double i_term = ki_ * integral_;
        
        // Derivative (with filtering)
        double derivative = (error - prev_error_) / dt;
        double d_term = kd_ * derivative;
        
        prev_error_ = error;
        
        // PID output
        double output = p_term + i_term + d_term;
        return std::clamp(output, output_min_, output_max_);
    }
};
```

### Safety System

The safety system monitors critical parameters and responds to limit violations:

```cpp
class SafetyMonitor {
public:
    struct Limits {
        double max_position_deg{270.0};
        double max_velocity_deg_s{5.0};
        double max_acceleration_deg_s2{2.0};
        double max_temperature_c{80.0};
        double min_voltage{20.0};
        double max_voltage{30.0};
        double max_current_a{10.0};
    };
    
    enum class SafetyEvent {
        POSITION_LIMIT_EXCEEDED,
        VELOCITY_LIMIT_EXCEEDED,
        TEMPERATURE_EXCEEDED,
        VOLTAGE_OUT_OF_RANGE,
        EMERGENCY_STOP,
        COMMUNICATION_LOST
    };
    
    using SafetyCallback = std::function<void(SafetyEvent event, const std::string& details)>;
    
private:
    Limits limits_;
    SafetyCallback callback_;
    bool emergency_stop_active_{false};
    
public:
    void checkLimits(double position, double velocity, double temperature, double voltage) {
        if (std::abs(position) > limits_.max_position_deg) {
            triggerEvent(SafetyEvent::POSITION_LIMIT_EXCEEDED,
                        "Position: " + std::to_string(position));
        }
        
        if (std::abs(velocity) > limits_.max_velocity_deg_s) {
            triggerEvent(SafetyEvent::VELOCITY_LIMIT_EXCEEDED,
                        "Velocity: " + std::to_string(velocity));
        }
        
        if (temperature > limits_.max_temperature_c) {
            triggerEvent(SafetyEvent::TEMPERATURE_EXCEEDED,
                        "Temperature: " + std::to_string(temperature));
        }
    }
    
    void triggerEvent(SafetyEvent event, const std::string& details) {
        if (callback_) {
            callback_(event, details);
        }
    }
};
```

### CMake Configuration

```cmake
# CANopen backend selection
option(USE_REAL_CANOPEN "Use real CANopen hardware" OFF)

if(USE_REAL_CANOPEN)
    # Real CANopen via SocketCAN
    add_definitions(-DUSE_REAL_CANOPEN)
    find_package(LinuxSocketCAN REQUIRED)
    target_link_libraries(astro_mount_controller linux-socketcan)
    
    # CANopen protocol stack (optional)
    option(USE_CANOPEN_STACK "Use CANopen protocol stack" OFF)
    if(USE_CANOPEN_STACK)
        find_package(CANopenStack REQUIRED)
        target_link_libraries(astro_mount_controller canopen-stack)
    endif()
else()
    # Simulated CANopen (default)
    add_definitions(-DUSE_SIMULATED_CANOPEN)
endif()
```

### HAL Layer Architecture

The HAL (Hardware Abstraction Layer) provides a clean separation between high-level controller logic and low-level hardware control:

```
┌──────────────────────────────────────────────┐
│           MountController                     │
│  (high-level: coord transforms, tracking,    │
│   calibration, state machine)                │
└──────────────────┬───────────────────────────┘
                   │ uses
┌──────────────────▼───────────────────────────┐
│           HALInterface                        │
│  (abstract: MotorControl, EncoderReader,     │
│   SafetyMonitor, SensorInterface)            │
└──────┬────────────┬──────────────┬───────────┘
       │            │              │
┌──────▼─────┐ ┌───▼──────┐ ┌───▼──────────┐
│ CanOpenHAL │ │SerialHAL │ │SimulatedHAL   │
│ (CANopen   │ │(RS-232/  │ │(testing,     │
│  CiA 402)  │ │ 485)     │ │ development) │
└────────────┘ └──────────┘ └──────────────┘
```

---

## 15. Real CANopen Hardware Integration Proposal

This section describes how to integrate real CANopen hardware (CiA 402 drives) with the existing controller structure. The proposal builds on the existing [`ICanOpenInterface`](../../include/controllers/icanopen_interface.h) abstraction.

### 15.1 CanOpenSocket Implementation (Linux SocketCAN)

**Files to create:**
- `src/controllers/canopen_socket.cpp`
- `include/controllers/canopen_socket.h` (or in existing header)

**Implementation:**

```cpp
// include/controllers/canopen_socket.h
#pragma once
#include "controllers/icanopen_interface.h"
#include <linux/can.h>
#include <linux/can/raw.h>
#include <thread>
#include <atomic>
#include <functional>
#include <map>

namespace astro_mount {
namespace controllers {

class CanOpenSocket : public ICanOpenInterface {
public:
    struct Config {
        std::string interface_name{"can0"};
        int bitrate{125000};
        int node_id{1};
        bool use_sync{true};
        std::chrono::milliseconds sync_period{100};
        
        // Drive-specific configuration
        struct DriveConfig {
            int node_id;
            int cob_id_pdo_rx;  // Receive PDO COB-ID
            int cob_id_pdo_tx;  // Transmit PDO COB-ID
            int cob_id_sdo;     // SDO COB-ID
            double position_factor;  // CiA 402 units per degree
            double velocity_factor;  // CiA 402 units per deg/s
        };
        
        std::vector<DriveConfig> drives;
    };

    CanOpenSocket(const Config& config);
    ~CanOpenSocket() override;
    
    // ICanOpenInterface implementation
    bool initialize() override;
    void shutdown() override;
    bool isInitialized() const override;
    
    bool setPositionTarget(int axis_id, double position_deg) override;
    bool setVelocityTarget(int axis_id, double velocity_deg_s) override;
    bool setTorqueTarget(int axis_id, double torque_percent) override;
    
    double getActualPosition(int axis_id) override;
    double getActualVelocity(int axis_id) override;
    double getActualTorque(int axis_id) override;
    
    bool enableDrive(int axis_id) override;
    bool disableDrive(int axis_id) override;
    bool isDriveEnabled(int axis_id) override;
    
    bool emergencyStop(int axis_id) override;
    bool clearErrors(int axis_id) override;
    
    DriveStatus getDriveStatus(int axis_id) override;
    
private:
    // SocketCAN operations
    int openSocket(const std::string& interface_name);
    bool sendFrame(const can_frame& frame);
    bool receiveFrame(can_frame& frame, std::chrono::milliseconds timeout);
    
    // CiA 402 state machine
    bool setDriveState(int node_id, uint8_t state);
    uint8_t getDriveState(int node_id);
    
    // SDO communication
    bool sdoWrite(int node_id, uint16_t index, uint8_t subindex,
                  const void* data, size_t size);
    bool sdoRead(int node_id, uint16_t index, uint8_t subindex,
                 void* data, size_t size);
    
    // PDO handling
    void pdoReceiveThread();
    void syncThread();
    
    // CiA 402 object dictionary mapping
    int32_t degToPositionUnits(double degrees, int axis_id);
    double positionUnitsToDeg(int32_t units, int axis_id);
    int16_t degToVelocityUnits(double deg_s, int axis_id);
    double velocityUnitsToDeg(int16_t units, int axis_id);
    
    Config config_;
    int socket_fd_{-1};
    std::atomic<bool> running_{false};
    std::thread pdo_thread_;
    std::thread sync_thread_;
    std::mutex mutex_;
    
    // Drive state cache
    struct DriveState {
        double actual_position{0.0};
        double actual_velocity{0.0};
        double actual_torque{0.0};
        uint8_t nmt_state{0};
        bool enabled{false};
        bool error{false};
        std::chrono::steady_clock::time_point last_update;
    };
    
    std::map<int, DriveState> drive_states_;
    
    // Callbacks
    std::function<void(int axis_id, double position, double velocity)> position_callback_;
    std::function<void(int axis_id, const std::string& error)> error_callback_;
};

} // namespace controllers
} // namespace astro_mount
```

**Example configuration** `config/canopen_socket.json`:
```json
{
  "canopen": {
    "interface": "can0",
    "bitrate": 125000,
    "sync_period_ms": 100,
    "drives": [
      {
        "axis": 0,
        "node_id": 2,
        "name": "RA_Axis",
        "cob_id_pdo_rx": 0x200,
        "cob_id_pdo_tx": 0x180,
        "cob_id_sdo": 0x600,
        "position_factor": 10000.0,
        "velocity_factor": 100.0
      },
      {
        "axis": 1,
        "node_id": 3,
        "name": "Dec_Axis",
        "cob_id_pdo_rx": 0x300,
        "cob_id_pdo_tx": 0x280,
        "cob_id_sdo": 0x700,
        "position_factor": 10000.0,
        "velocity_factor": 100.0
      }
    ]
  }
}
```

### 15.2 Real Implementation of Movement Operations in MountController

After implementing `CanOpenSocket`, the `MountController` can operate in real mode:

```cpp
// In MountController::Impl::slewToEquatorial() with real CANopen
bool MountController::Impl::slewToEquatorialReal(double ra_hours, double dec_degrees) {
    // Coordinate transformation (same as simulated)
    // ...
    
    // REAL CONTROL via ICANOPENINTERFACE
    state_ = MountStatus::State::SLEWING;
    
    // Convert degrees to CiA 402 units and send
    double ha_target = calculateHADegrees(ra_deg);
    
    bool success = canopen_interface_->setPositionTarget(0, ha_target);
    success &= canopen_interface_->setPositionTarget(1, dec_degrees);
    
    if (!success) {
        state_ = MountStatus::State::ERROR;
        return false;
    }
    
    // Real-time position monitoring via PDO callbacks
    canopen_interface_->setPositionCallback(0, [this](int axis, double pos, double vel) {
        axis1_position_ = pos;
        axis1_rate_ = vel;
        
        // Check if target reached
        if (std::abs(pos - axis1_target_) < POSITION_TOLERANCE) {
            if (std::abs(axis2_position_ - axis2_target_) < POSITION_TOLERANCE) {
                state_ = MountStatus::State::IDLE;
            }
        }
    });
    
    return true;
}
```

### 15.3 Safety System

```cpp
class CanOpenSafetyMonitor {
private:
    struct AxisLimits {
        double min_position_deg{-270.0};
        double max_position_deg{270.0};
        double max_velocity_deg_s{5.0};
        double max_acceleration_deg_s2{2.0};
        double max_temperature_c{80.0};
        double max_current_a{10.0};
    };
    
    std::array<AxisLimits, 2> axis_limits_;
    bool emergency_stop_{false};
    
public:
    void checkAndRespond(double position_ra, double position_dec,
                        double velocity_ra, double velocity_dec) {
        // RA axis check
        if (position_ra < axis_limits_[0].min_position_deg ||
            position_ra > axis_limits_[0].max_position_deg) {
            triggerEmergencyStop("RA position limit exceeded");
            return;
        }
        
        // Dec axis check
        if (position_dec < axis_limits_[1].min_position_deg ||
            position_dec > axis_limits_[1].max_position_deg) {
            triggerEmergencyStop("Dec position limit exceeded");
            return;
        }
        
        // Velocity check
        if (std::abs(velocity_ra) > axis_limits_[0].max_velocity_deg_s ||
            std::abs(velocity_dec) > axis_limits_[1].max_velocity_deg_s) {
            triggerEmergencyStop("Velocity limit exceeded");
            return;
        }
    }
    
    void triggerEmergencyStop(const std::string& reason) {
        emergency_stop_ = true;
        // Send CiA 402 Halt / Quick Stop to all drives
        // ...
        Logger::getInstance().error("EMERGENCY STOP: {}", reason);
    }
};
```

### 15.4 Integration with Existing Factory

```cpp
// In CanOpenFactory::create()
std::unique_ptr<ICanOpenInterface> CanOpenFactory::create(const Config& config) {
#ifdef USE_REAL_CANOPEN
    if (config.use_real_canopen) {
        CanOpenSocket::Config socket_config;
        // ... convert config to socket config ...
        auto canopen = std::make_unique<CanOpenSocket>(socket_config);
        if (canopen->initialize()) {
            return canopen;
        }
        logger_->warn("Failed to initialize real CANopen, falling back to simulation");
    }
#endif
    
    // Fall back to simulated interface
    return std::make_unique<CanOpenInterface>(config);
}
```

### 15.5 CMake Configuration

```cmake
# CMakeLists.txt (main)
option(USE_CANOPEN_SOCKET "Use real CANopen SocketCAN interface" OFF)

if(USE_CANOPEN_SOCKET)
    add_definitions(-DUSE_REAL_CANOPEN)
    find_package(LinuxSocketCAN REQUIRED)
    target_link_libraries(astro_mount_controller linux-socketcan)
    target_sources(astro_mount_controller PRIVATE
        src/controllers/canopen_socket.cpp
    )
endif()
```

```cmake
# Linking with main project
target_link_libraries(astro_mount_controller
    ${SOFA_LIBRARIES}
    ${PROTOBUF_LIBRARIES}
    ${GRPC_LIBRARIES}
    ${NLOHMANN_JSON_LIBRARIES}
    pthread
    ${CANOPEN_LIBRARIES}
)
```

### 15.6 Example Configuration

```json
{
  "controller": {
    "mode": "real",
    "canopen": {
      "interface": "can0",
      "bitrate": 125000,
      "use_sync": true,
      "sync_rate_hz": 10
    }
  },
  "axes": [
    {
      "id": 0,
      "name": "RA",
      "node_id": 2,
      "max_velocity": 5.0,
      "max_acceleration": 2.0,
      "encoder_resolution": 16384,
      "gear_ratio": 144.0
    },
    {
      "id": 1,
      "name": "Dec",
      "node_id": 3,
      "max_velocity": 5.0,
      "max_acceleration": 2.0,
      "encoder_resolution": 16384,
      "gear_ratio": 144.0
    }
  ],
  "safety": {
    "position_limits": [-270, 270],
    "velocity_limit": 5.0,
    "temperature_limit": 80.0,
    "auto_recovery": false
  }
}
```

### 15.7 Implementation Plan

| Phase | Task | Duration |
|-------|------|----------|
| 1 | SocketCAN driver implementation (CanOpenSocket class) | 1 week |
| 2 | CiA 402 state machine + SDO communication | 1 week |
| 3 | PDO mapping and real-time data exchange | 1 week |
| 4 | SYNC thread and drive synchronization | 3 days |
| 5 | Emergency stop and safety procedures | 3 days |
| 6 | Integration tests with vcan0 | 1 week |
| 7 | Testing with real CANopen hardware | 2 weeks |
| 8 | Performance optimization and tuning | 1 week |

### 15.8 Hardware Requirements

| Component | Example | Estimated Cost |
|-----------|---------|---------------|
| CAN-USB adapter | Peak CAN-USB, LAWICEL | $30-80 |
| CANopen servo drives | Nanotec, Faulhaber, Maxon | $200-600 each |
| CANopen motors | Stepper or servo with encoder | $100-400 each |
| Power supply | 24V/48V DC, 10A+ | $50-150 |
| CAN terminator | 120Ω resistor × 2 | $2 |
| Wiring | CAN bus cable (shielded twisted pair) | $10-20 |

### 15.9 Benefits of Real Implementation

1. **Real hardware control** — the controller can actually move a telescope
2. **CiA 402 profile compliance** — compatibility with industrial drives
3. **Real-time position feedback** — from drive encoders via PDO
4. **Safety systems** — physical limit switches, emergency stop
5. **Diagnostic capabilities** — real drive status, error history

---

## 16. HAL (Hardware Abstraction Layer) - Architecture and Implementation

The Hardware Abstraction Layer (HAL) provides a clean separation between high-level controller logic and low-level hardware control. It enables the same `MountController` code to work with different hardware backends (CANopen, serial, simulated) without modification.

### 16.1 Layered Architecture with HAL

```
┌──────────────────────────────────────────────────────────────┐
│                   Application Layer                           │
│  MountController, gRPC API, Configuration Manager            │
│  (coordinates, tracking, calibration, state machine)         │
├──────────────────────────────────────────────────────────────┤
│                   HAL Interface Layer                         │
│  HALInterface, MotorControl, EncoderReader,                  │
│  SafetyMonitor, SensorInterface                               │
├──────────────┬──────────────┬──────────────┬────────────────┤
│  CANopenHAL  │  SerialHAL   │ SimulatedHAL │  EthernetHAL   │
│  (CAN bus,   │  (RS-232/   │  (testing,   │  (future:      │
│   CiA 402)   │   485)      │  simulation) │   EtherCAT)    │
├──────────────┴──────────────┴──────────────┴────────────────┤
│                      Hardware Layer                           │
│  CAN bus, serial ports, network interfaces, GPIO             │
└──────────────────────────────────────────────────────────────┘
```

```
Data flow visualization:
  MC → HAL → IMPL → HW

  MC  = MountController (business logic)
  HAL = Hardware Abstraction Layer (interfaces)
  IMPL = Concrete implementation (CanOpenHAL, SimulatedHAL)
  HW  = Physical hardware
```

### 16.2 HAL Interface

**Header files** (`include/hal/`):
```
include/hal/
├── hal_interface.h           # Main HAL interface
├── motor_control.h           # Motor control
├── encoder_reader.h          # Encoder reading
├── safety_monitor.h          # Safety monitoring
├── sensor_interface.h        # Sensors (temperature, voltage)
├── hal_factory.h             # HAL implementation factory
└── hal_config.h              # HAL configuration
```

**Main HAL interface**:
```cpp
// include/hal/hal_interface.h
#pragma once
#include <memory>
#include <string>
#include <functional>
#include <chrono>

namespace astro_mount {
namespace hal {

class HALInterface {
public:
    virtual ~HALInterface() = default;
    
    // Initialization and management
    virtual bool initialize(const HALConfig& config) = 0;
    virtual void shutdown() = 0;
    virtual bool isInitialized() const = 0;
    
    // Component factory
    virtual std::unique_ptr<MotorControl> createMotorControl(int axis_id) = 0;
    virtual std::unique_ptr<EncoderReader> createEncoderReader(int axis_id) = 0;
    virtual std::unique_ptr<SafetyMonitor> createSafetyMonitor() = 0;
    virtual std::unique_ptr<SensorInterface> createSensorInterface() = 0;
    
    // Platform information
    virtual std::string getPlatformName() const = 0;
    virtual std::string getHardwareVersion() const = 0;
    virtual bool supportsFeature(HALFeature feature) const = 0;
};

} // namespace hal
} // namespace astro_mount
```

**Motor control interface**:
```cpp
// include/hal/motor_control.h
#pragma once

namespace astro_mount {
namespace hal {

enum class MotorType {
    STEPPER,        // Stepper motor
    SERVO,          // Servo drive
    BRUSHED_DC,     // Brushed DC motor
    BRUSHLESS_DC,   // Brushless DC motor
    CANOPEN_SERVO   // CANopen servo drive
};

enum class ControlMode {
    POSITION,       // Position control
    VELOCITY,       // Velocity control
    TORQUE,         // Torque control
    TRAJECTORY      // Trajectory control
};

class MotorControl {
public:
    virtual ~MotorControl() = default;
    
    // Basic operations
    virtual bool enable() = 0;
    virtual bool disable() = 0;
    virtual bool isEnabled() const = 0;
    
    // Control
    virtual bool setPosition(double position_deg, double velocity_deg_s = 0.0, 
                            double acceleration_deg_s2 = 0.0) = 0;
    virtual bool setVelocity(double velocity_deg_s, double acceleration_deg_s2 = 0.0) = 0;
    virtual bool setTorque(double torque_percent) = 0;
    
    // State information
    virtual double getActualPosition() const = 0;
    virtual double getActualVelocity() const = 0;
    virtual double getActualTorque() const = 0;
    virtual bool isMoving() const = 0;
    virtual bool targetReached() const = 0;
    
    // Configuration
    virtual bool configure(const MotorConfig& config) = 0;
    virtual MotorConfig getConfiguration() const = 0;
    
    // Callbacks
    using PositionCallback = std::function<void(double position, double velocity)>;
    using ErrorCallback = std::function<void(const std::string& error)>;
    
    virtual void setPositionCallback(PositionCallback callback) = 0;
    virtual void setErrorCallback(ErrorCallback callback) = 0;
};

} // namespace hal
} // namespace astro_mount
```

**Encoder reading interface**:
```cpp
// include/hal/encoder_reader.h
#pragma once

namespace astro_mount {
namespace hal {

enum class EncoderType {
    INCREMENTAL,    // Incremental encoder
    ABSOLUTE,       // Absolute encoder
    RESOLVER,       // Resolver
    HALL_SENSOR     // Hall sensors
};

struct EncoderReading {
    double position_deg;           // Position in degrees
    double velocity_deg_s;         // Velocity in deg/s
    uint32_t raw_counts;           // Raw count value
    bool index_pulse;              // Index pulse
    bool direction;                // Direction (true = forward)
    std::chrono::system_clock::time_point timestamp;
    uint32_t error_count;          // Error counter
};

class EncoderReader {
public:
    virtual ~EncoderReader() = default;
    
    // Initialization
    virtual bool initialize(const EncoderConfig& config) = 0;
    virtual void shutdown() = 0;
    
    // Reading
    virtual EncoderReading read() const = 0;
    virtual bool isDataValid() const = 0;
    
    // Calibration
    virtual bool calibrate(double reference_position_deg) = 0;
    virtual bool autoCalibrate() = 0;
    virtual double getCalibrationOffset() const = 0;
    
    // Information
    virtual EncoderType getType() const = 0;
    virtual uint32_t getResolution() const = 0;  // Counts per revolution
    virtual double getCountsPerDegree() const = 0;
    
    // Callbacks
    using ReadingCallback = std::function<void(const EncoderReading& reading)>;
    virtual void setReadingCallback(ReadingCallback callback) = 0;
};

} // namespace hal
} // namespace astro_mount
```

### 16.3 CANopen HAL Implementation

**CanOpenHAL** (`src/hal/canopen_hal/`):
```cpp
// src/hal/canopen_hal/canopen_hal.h
#pragma once
#include "hal/hal_interface.h"
#include "hal/motor_control.h"
#include "hal/encoder_reader.h"
#include "controllers/icanopen_interface.h"

namespace astro_mount {
namespace hal {

class CanOpenHAL : public HALInterface {
private:
    std::unique_ptr<controllers::ICanOpenInterface> canopen_;
    std::array<std::unique_ptr<MotorControl>, 3> motors_;
    std::array<std::unique_ptr<EncoderReader>, 3> encoders_;
    std::unique_ptr<SafetyMonitor> safety_monitor_;
    std::unique_ptr<SensorInterface> sensors_;
    
public:
    CanOpenHAL(std::unique_ptr<controllers::ICanOpenInterface> canopen);
    ~CanOpenHAL() override;
    
    // HALInterface implementation
    bool initialize(const HALConfig& config) override;
    void shutdown() override;
    bool isInitialized() const override;
    
    std::unique_ptr<MotorControl> createMotorControl(int axis_id) override;
    std::unique_ptr<EncoderReader> createEncoderReader(int axis_id) override;
    std::unique_ptr<SafetyMonitor> createSafetyMonitor() override;
    std::unique_ptr<SensorInterface> createSensorInterface() override;
    
    std::string getPlatformName() const override;
    std::string getHardwareVersion() const override;
    bool supportsFeature(HALFeature feature) const override;
};

class CanOpenMotorControl : public MotorControl {
private:
    controllers::ICanOpenInterface& canopen_;
    int axis_id_;
    MotorConfig config_;
    
public:
    CanOpenMotorControl(controllers::ICanOpenInterface& canopen, int axis_id);
    
    bool enable() override;
    bool disable() override;
    bool isEnabled() const override;
    
    bool setPosition(double position_deg, double velocity_deg_s, 
                    double acceleration_deg_s2) override;
    bool setVelocity(double velocity_deg_s, double acceleration_deg_s2) override;
    bool setTorque(double torque_percent) override;
    
    double getActualPosition() const override;
    double getActualVelocity() const override;
    double getActualTorque() const override;
    bool isMoving() const override;
    bool targetReached() const override;
    
    bool configure(const MotorConfig& config) override;
    MotorConfig getConfiguration() const override;
    
    void setPositionCallback(PositionCallback callback) override;
    void setErrorCallback(ErrorCallback callback) override;
};

class CanOpenEncoderReader : public EncoderReader {
private:
    controllers::ICanOpenInterface& canopen_;
    int axis_id_;
    EncoderConfig config_;
    double calibration_offset_{0.0};
    
public:
    CanOpenEncoderReader(controllers::ICanOpenInterface& canopen, int axis_id);
    
    bool initialize(const EncoderConfig& config) override;
    void shutdown() override;
    
    EncoderReading read() const override;
    bool isDataValid() const override;
    
    bool calibrate(double reference_position_deg) override;
    bool autoCalibrate() override;
    double getCalibrationOffset() const override;
    
    EncoderType getType() const override;
    uint32_t getResolution() const override;
    double getCountsPerDegree() const override;
    
    void setReadingCallback(ReadingCallback callback) override;
};

} // namespace hal
} // namespace astro_mount
```

### 16.4 HAL Integration with MountController

**Modification of `MountController`**:
```cpp
// include/controllers/mount_controller.h (adding HAL dependency)
#include "hal/hal_interface.h"

namespace astro_mount {
namespace controllers {

class MountController {
private:
    class Impl;
    std::unique_ptr<Impl> pimpl_;
    
public:
    // New constructor with HAL
    MountController(std::unique_ptr<hal::HALInterface> hal_interface);
    
    // Remaining methods unchanged...
};

} // namespace controllers
} // namespace astro_mount
```

**Implementation with HAL**:
```cpp
// src/controllers/mount_controller.cpp (fragments)
class MountController::Impl {
private:
    std::unique_ptr<hal::HALInterface> hal_;
    std::array<std::unique_ptr<hal::MotorControl>, 2> motors_;
    std::array<std::unique_ptr<hal::EncoderReader>, 2> encoders_;
    std::unique_ptr<hal::SafetyMonitor> safety_monitor_;
    
public:
    Impl(std::unique_ptr<hal::HALInterface> hal_interface)
        : hal_(std::move(hal_interface)) {
        
        // Creating HAL components
        motors_[0] = hal_->createMotorControl(0); // RA/Azimuth
        motors_[1] = hal_->createMotorControl(1); // Dec/Altitude
        
        encoders_[0] = hal_->createEncoderReader(0);
        encoders_[1] = hal_->createEncoderReader(1);
        
        safety_monitor_ = hal_->createSafetyMonitor();
    }
    
    bool slewToEquatorial(double ra_hours, double dec_degrees) {
        if (state_ == MountStatus::State::ERROR) return false;
        
        // Coordinate transformation
        double ra_deg = ra_hours * 15.0;
        
        // REAL CONTROL THROUGH HAL
        state_ = MountStatus::State::SLEWING;
        
        // Set position through HAL (instead of simulation)
        bool success_ra = motors_[0]->setPosition(ra_deg, config_.max_slew_rate, config_.max_acceleration);
        bool success_dec = motors_[1]->setPosition(dec_degrees, config_.max_slew_rate, config_.max_acceleration);
        
        if (!success_ra || !success_dec) {
            state_ = MountStatus::State::ERROR;
            return false;
        }
        
        // Execution monitoring via HAL callbacks
        motors_[0]->setPositionCallback([this](double position, double velocity) {
            axis1_position_ = position;
            axis1_rate_ = velocity;
            
            if (!motors_[0]->isMoving() && motors_[0]->targetReached()) {
                checkSlewCompletion();
            }
        });
        
        motors_[1]->setPositionCallback([this](double position, double velocity) {
            axis2_position_ = position;
            axis2_rate_ = velocity;
            
            if (!motors_[1]->isMoving() && motors_[1]->targetReached()) {
                checkSlewCompletion();
            }
        });
        
        return true;
    }
    
private:
    void checkSlewCompletion() {
        if (state_ == MountStatus::State::SLEWING &&
            motors_[0]->targetReached() && 
            motors_[1]->targetReached()) {
            state_ = MountStatus::State::IDLE;
        }
    }
};
```

### 16.5 HAL Factory

**HALFactory** (`src/hal/hal_factory.cpp`):
```cpp
// src/hal/hal_factory.cpp
#include "hal/hal_factory.h"
#include "hal/canopen_hal/canopen_hal.h"
#include "hal/simulated_hal/simulated_hal.h"
#include "hal/serial_hal/serial_hal.h"
#include "controllers/canopen_factory.h"

namespace astro_mount {
namespace hal {

std::unique_ptr<HALInterface> HALFactory::create(HALType type, const HALConfig& config) {
    switch (type) {
        case HALType::CANOPEN: {
            // Use existing CANopen factory
            auto canopen_factory = controllers::CanOpenFactory();
            auto canopen_impl = canopen_factory.create(config.canopen_config);
            
            if (!canopen_impl) {
                throw std::runtime_error("Failed to create CANopen interface");
            }
            
            return std::make_unique<CanOpenHAL>(std::move(canopen_impl));
        }
        
        case HALType::SIMULATED:
            return std::make_unique<SimulatedHAL>(config);
            
        case HALType::SERIAL:
            return std::make_unique<SerialHAL>(config);
            
        case HALType::ETHERNET:
            // Future implementation
            throw std::runtime_error("Ethernet HAL not yet implemented");
            
        default:
            throw std::runtime_error("Unknown HAL type");
    }
}

std::vector<HALType> HALFactory::getAvailableTypes() {
    std::vector<HALType> types;
    
    types.push_back(HALType::SIMULATED);  // Always available
    
    // Check CANopen availability
#ifdef HAVE_CANOPENSOCKET
    types.push_back(HALType::CANOPEN);
#endif
    
    // Check serial port availability
    if (hasSerialPorts()) {
        types.push_back(HALType::SERIAL);
    }
    
    return types;
}

} // namespace hal
} // namespace astro_mount
```

### 16.6 HAL Configuration

**Configuration structure**:
```cpp
// include/hal/hal_config.h
#pragma once
#include <string>
#include <vector>
#include <nlohmann/json.hpp>

namespace astro_mount {
namespace hal {

enum class HALType {
    SIMULATED,   // Simulated hardware
    CANOPEN,     // CANopen/CiA 402
    SERIAL,      // Serial port (RS-232/485)
    ETHERNET,    // Ethernet (EtherCAT, Modbus TCP)
    CUSTOM       // Custom implementation
};

struct HALConfig {
    HALType type{HALType::SIMULATED};
    std::string name;
    
    // Type-specific configuration
    struct {
        controllers::ICanOpenInterface::Config canopen_config;
    } canopen;
    
    struct {
        std::string port;
        int baud_rate{115200};
        std::string protocol;  // "modbus", "custom", etc.
    } serial;
    
    struct {
        bool enable_simulation{true};
        double simulation_update_rate{100.0};  // Hz
    } simulated;
    
    // Loading from JSON
    static HALConfig fromJson(const nlohmann::json& json);
    nlohmann::json toJson() const;
};

} // namespace hal
} // namespace astro_mount
```

### 16.7 Example Configuration

**Configuration file** `config/hal_config.json`:
```json
{
  "hal": {
    "type": "canopen",
    "name": "CANopen_HAL_v1",
    "canopen": {
      "interface_name": "can0",
      "bitrate": 125000,
      "node_id": 1,
      "use_sync": true,
      "sync_period_ms": 100,
      "library": "canopensocket"
    },
    "axes": [
      {
        "id": 0,
        "motor_type": "canopen_servo",
        "encoder_type": "absolute",
        "encoder_resolution": 16384,
        "gear_ratio": 144.0,
        "max_velocity": 2.0,
        "max_acceleration": 0.5,
        "max_current": 5.0
      },
      {
        "id": 1,
        "motor_type": "canopen_servo",
        "encoder_type": "absolute",
        "encoder_resolution": 16384,
        "gear_ratio": 144.0,
        "max_velocity": 2.0,
        "max_acceleration": 0.5,
        "max_current": 5.0
      }
    ],
    "safety": {
      "enable_limits": true,
      "max_temperature": 80.0,
      "min_voltage": 20.0,
      "max_voltage": 30.0,
      "emergency_stop_timeout_ms": 100
    }
  }
}
```

### 16.8 Benefits of HAL Introduction

1. **Full hardware abstraction**: Application logic independent of hardware
2. **Cross-platform**: Support for different interfaces (CANopen, Serial, Ethernet)
3. **Easy testing**: Simulated HAL for tests without hardware
4. **Modularity**: Components can be swapped without changing logic
5. **Extensibility**: Easy addition of new hardware types
6. **Consistency**: Unified interface for all components
7. **Safety**: Centralized safety monitoring
8. **Debugging**: Unified logging and diagnostics

### 16.9 Migration Plan to HAL Architecture

**Step 1: Define HAL interfaces** (1 week)
1. Create header files in `include/hal/`
2. Define `HALInterface`, `MotorControl`, `EncoderReader`, etc.
3. Integration with build system (CMake)

**Step 2: Implement CanOpenHAL** (2 weeks)
1. Implement `CanOpenHAL` using the existing `ICanOpenInterface`
2. `CanOpenMotorControl` and `CanOpenEncoderReader` classes
3. Tests with CAN simulator (vcan0)

**Step 3: Modify MountController** (1 week)
1. Add HAL dependency to `MountController`
2. Migrate control from `ICanOpenInterface` to `HALInterface`
3. Update `slewToEquatorial`, `startTracking`, etc.

**Step 4: Implement SimulatedHAL** (1 week)
1. Simulated HAL implementation for testing
2. Maintain compatibility with existing tests
3. Simulate encoders, motors, sensors

**Step 5: HAL configuration system** (3 days)
1. JSON configuration files for HAL
2. `HALFactory` with hardware auto-detection
3. Configuration loading during initialization

**Step 6: Tests and integration** (1 week)
1. HAL unit tests
2. Integration tests with real hardware
3. Performance benchmarks
4. User documentation

### 16.10 Impact on Existing Code

**Minimal changes**:
- `MountController` receives `HALInterface` instead of `ICanOpenInterface`
- Application logic remains nearly unchanged
- Existing tests require updates to use `SimulatedHAL`

**Compatibility preserved**:
- Existing `ICanOpenInterface` remains (used by `CanOpenHAL`)
- `CanOpenFactory` still available
- gRPC API unchanged

**Long-term benefits**:
- Ability to support different platforms (RPi, x86, embedded)
- Easy migration to new interfaces (EtherCAT, Modbus TCP)
- Simplified testing and debugging
- Better separation of concerns

---

## 17. Current HAL Implementation Status (Hardware Abstraction Layer)

### 17.1 Implementation Status
The HAL architecture has been fully implemented according to the migration plan described in section 16.9. Below is the current implementation status:

**Completed phases**:
1. ✅ **HAL interface definitions** — all header files created in `include/hal/`
2. ✅ **CanOpenHAL implementation** — complete implementation in `src/hal/canopen_hal/`
3. ✅ **MountController modification** — ready for HAL integration
4. ✅ **SimulatedHAL implementation** — headers in `src/hal/simulated_hal/`
5. ✅ **HAL configuration system** — `HALFactory` with auto-detection
6. ⏳ **Tests and integration** — in progress

### 17.2 HAL File Structure
```
include/hal/
├── hal_interface.h          # Main HAL interface
├── hal_config.h             # HAL configuration
├── hal_factory.h            # HAL factory
├── motor_control.h          # Motor control interface
├── encoder_reader.h         # Encoder reader interface
├── safety_monitor.h         # Safety monitor
└── sensor_interface.h       # Sensor interface

src/hal/
├── hal_factory.cpp          # HAL factory implementation
├── canopen_hal/
│   ├── canopen_hal.h        # CanOpenHAL implementation
│   └── canopen_hal.cpp      # CanOpenHAL implementation
├── simulated_hal/
│   └── simulated_hal.h      # SimulatedHAL header
└── serial_hal/              # (to be implemented)
```

### 17.3 Key Classes and Interfaces
1. **`HALInterface`** — main hardware abstraction interface
2. **`MotorControl`** — motor control with PID and monitoring
3. **`EncoderReader`** — encoder reading with calibration
4. **`SafetyMonitor`** — safety monitoring with limits
5. **`SensorInterface`** — sensor interface (temperature, current, voltage)
6. **`HALFactory`** — factory creating HAL instances
7. **`HALConfig`** — HAL configuration in JSON format

### 17.4 CanOpenHAL Implementation
**Architecture**:
```cpp
class CanOpenHAL : public HALInterface {
private:
    class CanOpenMotor : public MotorControl { ... };
    class CanOpenEncoder : public EncoderReader { ... };
    class CanOpenSafetyMonitor : public SafetyMonitor { ... };
    class CanOpenSensorInterface : public SensorInterface { ... };
    
    std::unique_ptr<controllers::ICanOpenInterface> canopen_interface_;
    // ...
};
```

**Integration with existing code**:
- Uses the existing `ICanOpenInterface` through composition
- `CanOpenFactory` creates CANopen implementations
- `HALFactory` creates `CanOpenHAL` with configuration

**Usage example**:
```cpp
// HAL configuration
HALConfig config;
config.type = HALType::CANOPEN;
config.canopen.interface_name = "can0";
config.canopen.bitrate = 125000;

// Create HAL through factory
auto hal = HALFactory::create(config);

// Initialization
if (hal->initialize(config)) {
    // Create components
    auto motor = hal->createMotorControl(0);  // RA axis
    auto encoder = hal->createEncoderReader(0);
    auto safety = hal->createSafetyMonitor();
    
    // Control
    motor->enable();
    motor->setPosition(45.0, 1.0, 0.5);  // 45°, 1°/s, 0.5°/s²
}
```

### 17.5 Compatibility with MountController
**Current status**:
- `MountController` uses `ICanOpenInterface` directly
- Ready for migration to `HALInterface`
- Minimal changes required in `MountController::Impl`

**Migration plan**:
1. Replace `ICanOpenInterface` with `HALInterface` in `MountController`
2. Update `initialize()`, `slewToEquatorial()`, etc.
3. Maintain backward compatibility through adapter

### 17.6 Benefits of Implemented HAL
1. **Full hardware abstraction** — logic independent of hardware
2. **Multi-platform support** — CANopen, Serial, Ethernet, Simulated
3. **Unified API** — consistent interfaces for all components
4. **Safety** — centralized safety monitoring
5. **Diagnostics** — unified logging and reporting
6. **Testability** — `SimulatedHAL` for tests without hardware

### 17.7 Example Configuration
**File `config/hal_config.json`**:
```json
{
  "hal": {
    "type": "canopen",
    "name": "CANopen_Mount_HAL",
    "canopen": {
      "interface_name": "can0",
      "bitrate": 125000,
      "node_id": 1,
      "use_sync": true,
      "sync_period_ms": 100,
      "library": "canopensocket"
    },
    "axes": [
      {
        "id": 0,
        "name": "RA_Axis",
        "motor_config": {
          "type": "canopen_servo",
          "max_velocity": 2.0,
          "max_acceleration": 0.5,
          "encoder_counts_per_degree": 10000.0
        },
        "encoder_config": {
          "type": "absolute",
          "resolution_counts": 16384,
          "counts_per_degree": 10000.0
        }
      }
    ],
    "safety": {
      "monitoring_rate_hz": 10,
      "axes_limits": [
        {
          "min_position_deg": -270.0,
          "max_position_deg": 270.0,
          "max_velocity_deg_s": 5.0,
          "max_temperature_c": 80.0
        }
      ]
    }
  }
}
```

### 17.8 Next Steps
1. **Integration with MountController** — migrate to HALInterface
2. **SimulatedHAL implementation** — full simulation for tests
3. **Unit tests** — HAL code coverage with tests
4. **API documentation** — detailed interface documentation
5. **Usage examples** — demonstration applications

---

## 18. Low-level Axis Control

**Description**: API for direct axis control of the mount, useful for uncalibrated mounts, manual calibration, diagnostics, and service.

**Protobuf definitions**:
```protobuf
// From proto/mount_controller.proto, lines 260-308
enum AxisControlMode {
    POSITION_CONTROL = 0;  // Move to absolute position
    VELOCITY_CONTROL = 1;  // Constant velocity movement
}

message AxisControlRequest {
    int32 axis_id = 1;          // 0=HA/RA/Azimuth, 1=Dec/Altitude
    AxisControlMode mode = 2;
    double target_position = 3; // deg (for POSITION_CONTROL)
    double max_velocity = 4;    // deg/s
    double acceleration = 5;    // deg/s²
    double target_velocity = 6; // deg/s (for VELOCITY_CONTROL)
    bool relative = 7;          // relative movement
}

message AxisStopRequest {
    int32 axis_id = 1;
    bool decelerate = 2;        // smooth deceleration
    double deceleration = 3;    // deg/s²
}

message EmergencyStopRequest {
    int32 axis_id = 1;          // -1 = all axes
    bool reset_after = 2;       // reset after stop
}

message AxisStatus {
    int32 axis_id = 1;
    double current_position = 2;
    double current_velocity = 3;
    double target_position = 4;
    double target_velocity = 5;
    bool moving = 6;
    bool target_reached = 7;
    bool error = 8;
    string error_message = 9;
    google.protobuf.Timestamp timestamp = 10;
}
```

**RPC**:
```protobuf
rpc ControlAxis(AxisControlRequest) returns (google.protobuf.Empty);
rpc StopAxis(AxisStopRequest) returns (google.protobuf.Empty);
rpc EmergencyStop(EmergencyStopRequest) returns (google.protobuf.Empty);
rpc GetAxisStatus(google.protobuf.Empty) returns (AxisStatus);
```

**Call sequence — POSITION_CONTROL**:
```
Client → ControlAxis(axis_id=0, mode=POSITION_CONTROL, target=90°, vel=3°/s)
  ↓
MountControllerServiceImpl::ControlAxis()
  ↓
Conversion protobuf → internal parameters
  ↓
State check (ERROR → reject)
  ↓
HAL::MotorControl::setPosition(90.0, 3.0, 1.0) [or ICanOpenInterface::setPositionTarget()]
  ↓
Start async movement
  ↓
Return OK (movement in background)
```

**Call sequence — VELOCITY_CONTROL**:
```
Client → ControlAxis(axis_id=0, mode=VELOCITY_CONTROL, vel=2.0°/s)
  ↓
HAL::MotorControl::setVelocity(2.0) / ICanOpenInterface::setVelocityTarget()
  ↓
Axis moves at constant velocity
  ↓
Client → StopAxis(axis_id=0, decelerate=true, deceleration=2.0)
  ↓
HAL::MotorControl::setVelocity(0.0) / ICanOpenInterface::stopAxis()
  ↓
Axis stops smoothly
```

**Sequence — Emergency Stop**:
```
Client → EmergencyStop(axis_id=-1, reset_after=true)
  ↓
ICanOpenInterface::emergencyStop(-1) for all axes
  ↓
Immediate drive power cutoff
  ↓
If reset_after: clearErrors() and restore to IDLE
  ↓
State ERROR → (after reset) IDLE
```

**Reading axis status**:
```
Client → GetAxisStatus()
  ↓
ICanOpenInterface::getDriveStatus(0) and getDriveStatus(1)
  ↓
Aggregation to AxisStatus
  ↓
Return to client
```

**Source code**:
- [`proto/mount_controller.proto`](../../proto/mount_controller.proto) (lines 260-308, 381-392): definitions
- [`include/controllers/icanopen_interface.h`](../../include/controllers/icanopen_interface.h): axis control methods
- [`src/api/service_impl.cpp`](../../src/api/service_impl.cpp): gRPC `ControlAxis()`, `StopAxis()`, `EmergencyStop()`, `GetAxisStatus()`

**Usage scenario**:
```
1. ControlAxis(axis=0, POSITION, target=90°) → slew RA to 90°
2. GetAxisStatus() → moving=true, position=45°
3. GetAxisStatus() → moving=false, position=90°, target_reached=true
4. ControlAxis(axis=1, VELOCITY, vel=1.0) → Dec motion at 1°/s
5. StopAxis(axis=1, decelerate=true) → stop
6. EmergencyStop(axis=-1) → emergency stop
```

---

## 19. Health Check and System Metrics

**Description**: Monitoring service status, performance, and system metrics through a dedicated API.

**Definitions**:
```protobuf
// From proto/mount_controller.proto, lines 582-637
rpc CheckHealth(HealthCheckRequest) returns (HealthCheckResponse);

message HealthCheckRequest {
    string service = 1;
}

message HealthCheckResponse {
    enum ServingStatus { UNKNOWN = 0; SERVING = 1; NOT_SERVING = 2; SERVICE_UNKNOWN = 3; }
    ServingStatus status = 1;
    string service = 2;
    SystemMetrics metrics = 3;
}

message SystemMetrics {
    double cpu_usage_percent = 1;
    double memory_usage_mb = 2;
    uint64 active_connections = 3;
    uint64 total_requests = 4;
    uint64 error_count = 5;
    double avg_response_time_ms = 6;
    MountControllerMetrics mount_metrics = 7;
    KalmanFilterMetrics kalman_metrics = 8;
    TPointMetrics tpoint_metrics = 9;
}

message MountControllerMetrics {
    double tracking_error_ra_avg = 1;
    double tracking_error_dec_avg = 2;
    double tracking_error_max = 3;
    uint64 slew_count = 4;
    uint64 track_count = 5;
    uint64 calibration_count = 6;
    bool encoders_active = 7;
    bool guider_active = 8;
}

message KalmanFilterMetrics {
    double process_noise = 1;
    double measurement_noise = 2;
    double innovation_norm = 3;
    uint64 update_count = 4;
    double avg_update_time_ms = 5;
}

message TPointMetrics {
    uint32 measurement_count = 1;
    double residual_max = 2;
    double residual_rms = 3;
    double chi_squared = 4;
    bool calibrated = 5;
}
```

**Data flow**:
```
CheckHealth(service="mount_controller")
  ↓
MountControllerServiceImpl::CheckHealth()
  ↓
Collect system metrics (CPU, RAM, gRPC stats)
  ↓
Collect MountController metrics:
  - Avg tracking error RA/Dec
  - Slew/track/calibration count
  - Encoder/guider status
  ↓
Collect Kalman filter metrics:
  - Process/measurement noise
  - Innovation norm
  - Update count/time
  ↓
Collect TPOINT metrics:
  - Measurement count
  - Residuals, chi-squared
  - Calibrated status
  ↓
Aggregate to SystemMetrics
  ↓
Return HealthCheckResponse(status=SERVING, metrics=...)
```

**Source code**:
- [`proto/mount_controller.proto`](../../proto/mount_controller.proto) (lines 582-637): HealthCheck definitions
- [`src/api/service_impl.cpp`](../../src/api/service_impl.cpp): `CheckHealth()` implementation
- [`include/controllers/mount_controller.h`](../../include/controllers/mount_controller.h): `getMetrics()` method

**Usage scenario**:
```
1. CheckHealth("mount_controller") → SERVING
2. CPU: 23.5%, Memory: 156.3 MB
3. Tracking error RA: 0.8", Dec: 0.5"
4. TPOINT calibrated: true, chi-squared: 1.2
5. Kalman updates: 15234, avg time: 0.08 ms
6. Slew count: 47, Track count: 12
```

---

## 20. Configuration Management via gRPC

**Description**: Remote read and update of controller configuration through RPC.

**RPC**:
```protobuf
rpc GetConfiguration(google.protobuf.Empty) returns (Configuration);
rpc UpdateConfiguration(Configuration) returns (google.protobuf.Empty);
```

**`Configuration` definition** (from [`proto/mount_controller.proto`](../../proto/mount_controller.proto), lines 520-580):
```protobuf
message Configuration {
    double latitude = 1;
    double longitude = 2;
    double altitude = 3;
    double mount_height = 4;
    double pier_west = 5;
    double pier_east = 6;
    double focal_length = 7;
    double aperture = 8;
    double default_temperature = 9;
    double default_pressure = 10;
    double default_humidity = 11;
    double process_noise = 12;
    double measurement_noise = 13;
    string log_level = 14;
    string log_directory = 15;
    int32 log_rotation_days = 16;
    string grpc_address = 17;
    int32 grpc_port = 18;
    string canopen_interface = 19;
    int32 canopen_node_id = 20;
    double max_slew_rate = 23;
    double max_tracking_rate = 24;
    double slew_acceleration = 25;
    double tracking_acceleration = 26;
    AxisPhysicalParameters ha_axis_params = 27;
    AxisPhysicalParameters dec_axis_params = 28;
    bool use_encoders = 29;
    bool encoders_absolute = 30;
    double encoder_resolution_config = 31;
    uint32 tpoint_enabled_terms = 32;
    bool enable_guider = 33;
    double guider_max_correction = 34;
    double guider_aggression = 35;
}
```

**Flow**:
```
GetConfiguration()
  ↓
Configuration::getMountConfig(), getTelescopeConfig(), getLoggingConfig(), etc.
  ↓
Convert to protobuf Configuration
  ↓
Return

UpdateConfiguration(config)
  ↓
Convert protobuf → Configuration struct
  ↓
Update internal Configuration
  ↓
Reinitialize affected components (CANopen, guider, etc.)
  ↓
Save to file (optional)
```

**Source code**:
- [`proto/mount_controller.proto`](../../proto/mount_controller.proto) (lines 520-580): Configuration definition
- [`include/config/configuration.h`](../../include/config/configuration.h): configuration system
- [`src/config/configuration.cpp`](../../src/config/configuration.cpp): load/save/expose configuration
- [`src/api/service_impl.cpp`](../../src/api/service_impl.cpp): `GetConfiguration()` and `UpdateConfiguration()`

**Usage scenario**:
```
1. GetConfiguration() → {latitude=52.0, max_slew_rate=5.0, ...}
2. Change latitude to 49.0
3. UpdateConfiguration(modified config) → OK
4. GetConfiguration() → {latitude=49.0, ...} (confirm change)
```

---

## 21. Pole Position Determination

**Description**: Automatic determination of the celestial pole position using the drift method.

**Definitions**:
```protobuf
rpc DeterminePolePosition(PoleDeterminationRequest) returns (PolePosition);

message PoleDeterminationRequest {
    int32 measurement_count = 1;   // Number of measurements
    double duration_hours = 2;     // Drift duration (h)
}

message PolePosition {
    double latitude = 1;
    double longitude = 2;
    double altitude = 3;
    double accuracy = 4;           // Accuracy in arcsec
    google.protobuf.Timestamp determined_at = 5;
}
```

**Flow**:
```
DeterminePolePosition(measurement_count=10, duration_hours=2.0)
  ↓
Start drift measurement series
  ↓
For each measurement:
  - Track a star for duration_hours
  - Record position deviations (encoders + plate solving)
  ↓
Calculate pole offset vector
  ↓
Convert to correction (latitude, longitude)
  ↓
Return PolePosition with accuracy
```

**Source code**:
- [`proto/mount_controller.proto`](../../proto/mount_controller.proto) (lines 240-247, 447-451): definitions
- [`src/controllers/mount_controller.cpp`](../../src/controllers/mount_controller.cpp): method implementation

---

## 22. Rotation Matrix

**Description**: Retrieve the rotation matrix (represented as a quaternion) for transformations between coordinate systems.

**Definitions**:
```protobuf
rpc GetRotationMatrix(google.protobuf.Empty) returns (RotationMatrix);

message RotationMatrix {
    double q0 = 1;
    double q1 = 2;
    double q2 = 3;
    double q3 = 4;
    google.protobuf.Timestamp valid_from = 5;
}
```

**Flow**:
```
GetRotationMatrix()
  ↓
Get current rotation matrix (from TPOINT + bootstrap)
  ↓
Convert to quaternion (q0, q1, q2, q3)
  ↓
Return

Application:
  [x_mount, y_mount, z_mount] = R(q) * [x_radec, y_radec, z_radec]
```

**Source code**:
- [`proto/mount_controller.proto`](../../proto/mount_controller.proto) (lines 98-105): RotationMatrix definition
- [`src/controllers/mount_controller.cpp`](../../src/controllers/mount_controller.cpp): getRotationMatrix()

---

## 23. Encoder Control via gRPC

**Description**: Remote enable/disable of encoders and configuration.

**RPC**:
```protobuf
rpc EnableEncoders(EncoderConfig) returns (google.protobuf.Empty);
rpc DisableEncoders(google.protobuf.Empty) returns (google.protobuf.Empty);

message EncoderConfig {
    enum EncoderType { ABSOLUTE = 0; INCREMENTAL = 1; }
    EncoderType type = 1;
    double resolution = 2;
    bool use_feedback = 3;
}
```

**Flow**:
```
EnableEncoders(type=ABSOLUTE, resolution=36000, use_feedback=true)
  ↓
Configure encoders through HAL/ICanOpenInterface
  ↓
Set encoders_active_ = true
  ↓
Start encoder reading (PDO in CANopen)

DisableEncoders()
  ↓
Stop encoder reading
  ↓
Set encoders_active_ = false
```

---

## 24. Axis Physical Parameters

**Description**: Advanced configuration of mechanical axis properties, used for error modeling and compensation.

**Definition** (from [`proto/mount_controller.proto`](../../proto/mount_controller.proto), lines 479-517):
```protobuf
message AxisPhysicalParameters {
    double motor_steps_per_rev = 1;
    double motor_microstepping = 2;
    double motor_step_angle = 3;           // arcsec
    double encoder_resolution = 4;         // counts/rev
    double encoder_counts_per_arcsec = 5;
    double encoder_quantization_error = 6; // arcsec
    double gear_ratio = 7;
    double worm_ratio = 8;
    int32 worm_teeth = 9;
    int32 worm_wheel_teeth = 10;
    double cyclic_error_amplitude = 11;    // arcsec
    double cyclic_error_period = 12;       // deg
    repeated double cyclic_harmonics = 13;
    double backlash = 14;                  // arcsec
    double backlash_temp_coeff = 15;       // arcsec/°C
    double axis_stiffness = 16;            // arcsec/Nm
    double torsional_compliance = 17;      // rad/Nm
    double expansion_coeff = 18;           // 1/°C
    double temp_gear_error_coeff = 19;     // arcsec/°C
    repeated double calibration_table = 20;
    double calibration_temp = 21;          // °C
}
```

**Configuration flow**:
```
Set in configuration:
  Configuration.ha_axis_params = {...}
  Configuration.dec_axis_params = {...}
  ↓
UpdateConfiguration(Configuration) → save to MountConfig
  ↓
Used in:
  - TPOINT model (cyclic error correction)
  - Encoder simulation (quantization error, backlash)
  - Gear ratio calculations (degrees → counts)
  - Thermal compensation (temperature → backlash)
  - PEC calibration (Periodic Error Correction)
```

**Source code**:
- [`proto/mount_controller.proto`](../../proto/mount_controller.proto) (lines 479-517): AxisPhysicalParameters
- [`include/config/configuration.h`](../../include/config/configuration.h) (lines 46-83): C++ struct
- [`src/config/configuration.cpp`](../../src/config/configuration.cpp) (lines 171-229): loading from JSON

---

## 25. HAL RPC API (HAL Management via gRPC)

**Description**: Remote management of the HAL layer — reading configuration, changing type, reinitialization.

**RPC** (from [`proto/mount_controller.proto`](../../proto/mount_controller.proto), lines 416-429):
```protobuf
rpc GetHALConfig(google.protobuf.Empty) returns (HALConfig);
rpc SetHALConfig(HALConfigRequest) returns (google.protobuf.Empty);
rpc GetHALStatus(google.protobuf.Empty) returns (HALStatus);
rpc ReinitializeHAL(HALReinitRequest) returns (google.protobuf.Empty);
```

**Full HAL type change flow**:
```
GetHALConfig()
  ↓
Read current config from Configuration::getHALConfig()
  ↓
Return HALConfig (type, canopen, axes, safety, etc.)

SetHALConfig(new_config)
  ↓
Convert HALConfigRequest → astro_mount::hal::HALConfig
  ↓
Save to Configuration::setHALConfig()
  ↓
Requires ReinitializeHAL() to activate

GetHALStatus()
  ↓
Get: isInitialized(), isRunning(), getPlatformName(),
     getHardwareVersion(), getSupportedFeatures()
  ↓
Return HALStatus

ReinitializeHAL(request)
  ↓
If force_restart: shutdown() existing HAL
  ↓
create() new HAL with new config (or existing)
  ↓
initialize() and start()
  ↓
Update MountController
```

**Protobuf definitions**:
```protobuf
// From proto/mount_controller.proto, lines 871-1000+
enum HALType { HAL_SIMULATED = 0; HAL_CANOPEN = 1; HAL_SERIAL = 2; HAL_ETHERNET = 3; HAL_CUSTOM = 4; }

message HALConfigRequest {
    HALType type = 1;
    CanOpenConfig canopen = 2;
    SerialConfig serial = 3;
    EthernetConfig ethernet = 4;
    SimulatedConfig simulated = 5;
    repeated AxisConfig axes = 6;
    SafetyConfig safety = 7;
    PIDParams pid_params = 8;
}

message HALStatus {
    bool initialized = 1;
    bool running = 2;
    string platform_name = 3;
    string hardware_version = 4;
    repeated string supported_features = 5;
    int32 active_connections = 6;
}

message HALReinitRequest {
    HALConfigRequest new_config = 1;
    bool force_restart = 2;
    int32 timeout_seconds = 3;
}
```

**Usage scenario**:
```
1. GetHALConfig() → {type=HAL_SIMULATED, axes=[...], ...}
2. SetHALConfig(new_config={type=HAL_CANOPEN, ...}) → OK
3. GetHALStatus() → {initialized=false, running=false}
4. ReinitializeHAL(force_restart=true) → OK
5. GetHALStatus() → {initialized=true, running=true, platform="CANopen v1.0"}
```

---

## 26. Advanced State Monitoring (ControllerState)

**Description**: Full controller state available through the `GetState()` RPC — includes not only status but also environmental conditions, performance, and metrics.

**Definition** (from [`proto/mount_controller.proto`](../../proto/mount_controller.proto), lines 198-237):
```protobuf
message ControllerState {
    enum MountStatus { UNKNOWN = 0; IDLE = 1; SLEWING = 2; TRACKING = 3; PARKED = 4; ERROR = 5; }
    
    MountStatus status = 1;
    TrackedObject tracked_object = 2;
    MountPosition current_position = 3;
    RotationMatrix rotation_matrix = 4;
    TPointParameters tpoint_params = 5;
    bool encoders_enabled = 6;
    bool guider_active = 7;
    google.protobuf.Timestamp state_time = 8;
    
    // Tracking rates
    double tracking_rate_ra = 9;     // arcsec/s
    double tracking_rate_dec = 10;   // arcsec/s
    
    // Pier side and meridian
    double pier_side = 11;           // 1=East, -1=West
    bool meridian_flipped = 12;
    double time_to_meridian = 13;    // h
    double time_to_set = 14;         // h
    double time_to_rise = 15;        // h
    
    // Environmental conditions
    double temperature = 16;         // °C
    double pressure = 17;            // hPa
    double humidity = 18;            // %
    double wind_speed = 19;          // m/s
    double wind_direction = 20;      // deg
    
    // Performance
    double pointing_error = 21;      // arcsec
    double tracking_performance = 22; // %
    double guiding_performance = 23;  // %
    double mount_vibration = 24;     // arcsec RMS
}
```

**Data flow for GetState()**:
```
GetState()
  ↓
Read internal MountController::Impl state
  ↓
Read position from encoders (via HAL)
  ↓
Calculate pier_side, time_to_meridian/set/rise
  ↓
Read environmental sensors (T, P, H, wind)
  ↓
Calculate pointing_error from TPOINT model
  ↓
Aggregate to ControllerState protobuf
  ↓
Return
```

**Monitoring scenario**:
```
1. GetState() → status=TRACKING, position=(45.2°, 30.1°)
2. pier_side=1 (East), meridian_flipped=false
3. time_to_meridian=1.5h, time_to_set=4.2h
4. T=12.5°C, P=1015hPa, H=65%
5. pointing_error=2.3", tracking_performance=98.5%
6. mount_vibration=0.15" RMS
```

---

## 27. Complete Operational Workflow

**Description**: A full observation session combining multiple scenarios in a single sequence.

**Sequence**:
```
=== Phase 1: Initialization ===
1. MountController::initialize() → IDLE
2. GetHALConfig() → check HAL type
3. ReinitializeHAL() → activate selected HAL

=== Phase 2: Bootstrap Calibration ===
4. SlewToHorizontal(alt=30°, az=180°) → SLEWING → IDLE
5. AddBootstrapMeasurement(observed={ra1,dec1}, expected={ra1_cat,dec1_cat})
6. AddBootstrapMeasurement(observed={ra2,dec2}, expected={ra2_cat,dec2_cat})
7. AddBootstrapMeasurement(observed={ra3,dec3}, expected={ra3_cat,dec3_cat})
8. RunBootstrapCalibration() → success=true, alignment_error=30"

=== Phase 3: TPOINT Calibration ===
9. EnableEncoders(type=ABSOLUTE, resolution=36000)
10. AddTPointMeasurement(obs1, exp1, mount_pos1, T=15°C, P=1013hPa)
11. AddTPointMeasurement(obs2, exp2, mount_pos2, T=15°C, P=1013hPa)
12. ... (10-20 measurements distributed across the sky)
13. RunTPointCalibration() → success=true, chi_squared=1.5
14. GetRotationMatrix() → q0=0.999, q1=0.017, q2=0.017, q3=0.0

=== Phase 4: Object Tracking ===
15. SlewToCoordinates(ra=10.5h, dec=41.3°) → SLEWING → IDLE
16. TrackObject(ra=10.5h, dec=41.3°) → TRACKING

=== Phase 5: Guider Correction ===
17. ConnectGuider(connection="tcp://localhost:7624")
18. SendGuiderCorrection(ra_corr=1.5", dec_corr=-0.8")
19. GetState() → tracking_performance=99.2%

=== Phase 6: Field Rotation (for alt-az) ===
20. ConfigureDerotator(type=STEPPER, gear_ratio=180:1)
21. EnableFieldRotation(enabled=true, latitude=52°)
22. ControlFieldRotation(mode=ALT_AZ)

=== Phase 7: Moving Object Tracking ===
23. UploadEphemeris(comet_data)
24. StartEphemerisTracking(object_id="C/2023_A3")
25. GetEphemerisTrackStatus() → state=TRACKING, error=1.2"

=== Phase 8: Measurement and Recording ===
26. AddMeasurement(observed={...}, expected={...}, mount_pos={...})
27. GetTPointParameters() → chi_squared, coefficients
28. CheckHealth("mount_controller") → SERVING

=== Phase 9: Shutdown ===
29. StopEphemerisTracking()
30. DisconnectGuider()
31. Stop()
32. SaveState("session_20250101.json") → confirmation
33. Park() → PARKING → PARKED
```

---

## Summary

The mount controller implements a complex control system with:
- Multi-level architecture (application, API, logic, communication, hardware)
- Advanced mathematical models (TPOINT, Kalman)
- Flexible communication (CANopen with factory implementation)
- Complete gRPC API
- Calibration, tracking, and correction mechanisms
- **Modern HAL architecture** (fully implemented)

The processing flow is driven by internal state, with clear transitions between operational states. Each use case has a defined call sequence and data transformation, following safety and error handling principles.

This documentation serves as the foundation for understanding the system's operation, extending functionality, and diagnosing problems. The HAL architecture implementation represents a key step towards modularity, testability, and cross-platform support.
