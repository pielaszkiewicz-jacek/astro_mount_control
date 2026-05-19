# Training Data and Input Preparation

## Table of Contents
- [Training Data and Input Preparation](#training-data-and-input-preparation)
  - [Table of Contents](#table-of-contents)
  - [1. Introduction](#1-introduction)
    - [Where is this data used?](#where-is-this-data-used)
  - [2. Measurement Structure](#2-measurement-structure)
    - [2.1 Measurement Structure](#21-measurement-structure)
    - [2.2 Required Fields](#22-required-fields)
      - [How to obtain `observed_ra` / `observed_dec`?](#how-to-obtain-observed_ra--observed_dec)
      - [How to obtain `expected_ra` / `expected_dec`?](#how-to-obtain-expected_ra--expected_dec)
      - [How to obtain `mount_ha` / `mount_dec`?](#how-to-obtain-mount_ha--mount_dec)
    - [2.3 Optional Fields](#23-optional-fields)
      - [Environmental Conditions (`temperature`, `pressure`, `humidity`)](#environmental-conditions-temperature-pressure-humidity)
      - [Measurement Quality (`snr`, `seeing`)](#measurement-quality-snr-seeing)
      - [Timestamp (`timestamp`, `julian_date`)](#timestamp-timestamp-julian_date)
    - [2.4 Calibration Data Collection Methods](#24-calibration-data-collection-methods)
      - [Manual Method (via gRPC API)](#manual-method-via-grpc-api)
      - [Automatic Method (Star List Script)](#automatic-method-star-list-script)
      - [Method with Plate Solving Integration](#method-with-plate-solving-integration)
    - [2.5 Sky Coverage](#25-sky-coverage)
      - [Recommended Measurement Distribution](#recommended-measurement-distribution)
      - [Sample Calibration Star List](#sample-calibration-star-list)
    - [2.6 Minimum Number of Measurements](#26-minimum-number-of-measurements)
      - [Checking Calibration Readiness](#checking-calibration-readiness)
    - [2.7 Measurement Quality and Outlier Rejection](#27-measurement-quality-and-outlier-rejection)
  - [3. Calibration Data Collection Methods](#3-calibration-data-collection-methods)
    - [3.1 Bootstrap vs TPOINT Difference](#31-bootstrap-vs-tpoint-difference)
    - [3.2 Bootstrap Measurement Preparation](#32-bootstrap-measurement-preparation)
      - [For EQUATORIAL Mount](#for-equatorial-mount)
      - [For CASUAL (Arbitrarily Oriented) Mount](#for-casual-arbitrarily-oriented-mount)
  - [4. System Input Data](#4-system-input-data)
    - [4.1 Controller Configuration](#41-controller-configuration)
      - [Configuration Fields](#configuration-fields)
      - [Configuration Preparation](#configuration-preparation)
      - [Accuracy of Location Parameters](#accuracy-of-location-parameters)
    - [4.2 Ephemeris Data](#42-ephemeris-data)
      - [Ephemeris Data Structure](#ephemeris-data-structure)
      - [Ephemeris Data Preparation](#ephemeris-data-preparation)
      - [Ephemeris Data Validation](#ephemeris-data-validation)
    - [4.3 Autoguider Data](#43-autoguider-data)
    - [4.4 Encoder Data](#44-encoder-data)
    - [4.5 Axis Physical Parameters](#45-axis-physical-parameters)
      - [Calculating Effective Resolution](#calculating-effective-resolution)
  - [5. Error Sources](#5-error-sources)
    - [Practical Tips](#practical-tips)
  - [6. Helper Scripts](#6-helper-scripts)
    - [Measurement Collection Script (Python)](#measurement-collection-script-python)
    - [Calibration Star List Generator](#calibration-star-list-generator)
  - [7. Usage Scenarios](#7-usage-scenarios)
    - [Scenario 1: First Calibration of a New Mount](#scenario-1-first-calibration-of-a-new-mount)
    - [Scenario 2: Refining an Existing Model](#scenario-2-refining-an-existing-model)
    - [Scenario 3: Calibration After Configuration Change](#scenario-3-calibration-after-configuration-change)

---

## 1. Introduction

This document describes the structure of data required by the [`MountController`](../../src/controllers/mount_controller.cpp) calibration system (bootstrap and TPOINT), as well as other input data needed for the proper operation of the astronomical mount controller. The document covers measurement formats, collection methods, data quality requirements, and practical tips for preparing calibration sessions.

### Where is this data used?

- **Bootstrap calibration** — [`RunBootstrapCalibration()`](../../src/controllers/mount_controller.cpp) — initial mount alignment based on 3+ reference stars
- **TPOINT calibration** — [`RunTPointCalibration()`](../../src/controllers/mount_controller.cpp) — precise modeling of mount geometry errors (up to 40+ terms)
- **Ephemeris tracking** — [`EphemerisTracker`](../../src/models/ephemeris_tracker.cpp) — tracking moving objects (asteroids, comets, satellites)
- **Guider correction** — [`sendGuiderCorrection()`](../../src/controllers/mount_controller.cpp) — real-time tracking corrections
- **Encoder configuration** — [`EnableEncoders()`](../../src/controllers/mount_controller.cpp) — absolute/incremental encoder support
- **State estimation** — [`KalmanFilter`](../../src/models/kalman_filter.cpp) — sensor fusion and noise reduction

---

## 2. Measurement Structure

### 2.1 Measurement Structure

Both bootstrap and TPOINT calibration use a common [`Measurement`](../../include/controllers/mount_controller.h) structure, with additional fields specific to TPOINT:

```cpp
// From include/controllers/mount_controller.h
struct Measurement {
    // === Required fields ===
    double observed_ra;      // Right ascension measured (hours)
    double observed_dec;     // Declination measured (degrees)
    double expected_ra;      // Catalog right ascension (hours)
    double expected_dec;     // Catalog declination (degrees)
    
    // === Required for TPOINT only ===
    double mount_ha;         // Hour angle read from mount (hours)
    double mount_dec;        // Declination read from mount (degrees)
    
    // === Optional (improve model accuracy) ===
    double temperature{20.0};  // Ambient temperature (°C)
    double pressure{1013.25};  // Atmospheric pressure (hPa)
    double humidity{50.0};     // Relative humidity (%)
    double snr{100.0};         // Signal-to-noise ratio
    double seeing{2.0};        // Seeing in arcseconds
    std::chrono::system_clock::time_point timestamp;
    double julian_date{0.0};
};
```

### 2.2 Required Fields

| Field | Unit | Range | Description |
|-------|------|-------|-------------|
| `observed_ra` | hours | 0–24 | Right ascension measured by the telescope |
| `observed_dec` | degrees | –90 to +90 | Declination measured by the telescope |
| `expected_ra` | hours | 0–24 | Catalog right ascension of reference star |
| `expected_dec` | degrees | –90 to +90 | Catalog declination of reference star |
| `mount_ha` | hours | –12 to +12 | Hour angle read from mount encoders |
| `mount_dec` | degrees | –90 to +90 | Declination read from mount encoders |

#### How to obtain `observed_ra` / `observed_dec`?

**Method 1: Plate solving** (recommended)
- Take a frame with the telescope camera
- Use a plate solving tool (Astrometry.net, ASTAP, etc.)
- The solver returns the central coordinates of the frame

**Method 2: Manual centering**
- Center the star visually in the eyepiece/camera
- Read coordinates from the mount system
- Less accurate — recommended only for bootstrap calibration

#### How to obtain `expected_ra` / `expected_dec`?

Coordinates of reference stars should be taken from catalog databases:

```python
# Using the SIMBAD service (Python example)
from astroquery.simbad import Simbad
simbad = Simbad()
simbad.add_votable_fields('ra(d)', 'dec(d)')
result = simbad.query_object('Polaris')
ra_hours = result['RA_d'][0] / 15.0  # Convert degrees to hours
dec_degrees = result['DEC_d'][0]
```

- Use the **SIMBAD** database or **VizieR** catalog access service
- For best accuracy (sub-arcsecond), use the **Gaia** catalog
- For bright stars, the **Hipparcos** catalog is sufficient
- Ensure J2000.0 epoch coordinates

#### How to obtain `mount_ha` / `mount_dec`?

Read from the mount system via gRPC API:

```python
# Python via gRPC
import grpc
import mount_controller_pb2 as pb2

stub = ...  # gRPC stub initialization
state = stub.GetState(pb2.google_dot_protobuf_dot_empty__pb2.Empty())
# mount_ha and mount_dec are derived from mount position
```

- `mount_ha` = Local Sidereal Time (LST) - mount RA position
- `mount_dec` = direct read from mount Dec axis encoder
- For CASUAL mounts, the connection between mount coordinates and celestial coordinates is determined by the bootstrap model

### 2.3 Optional Fields

#### Environmental Conditions (`temperature`, `pressure`, `humidity`)

- **Effect**: Temperature affects mechanical structure (thermal expansion of gears, belts, truss). Pressure and humidity affect atmospheric refraction.
- **Recommendation**: Always record at least temperature. Use external temperature sensors (e.g., SHT31, DS18B20).
- **Compensation**: The TPOINT model can correct temperature-dependent errors if temperature data is provided with measurements.

#### Measurement Quality (`snr`, `seeing`)

- **SNR** — signal-to-noise ratio of the measurement. Higher values (>100) indicate reliable measurements.
- **Seeing** — atmospheric seeing in arcseconds. Below 2" is considered good, above 5" is poor.
- **Usage**: Used by the quality filter to reject outliers before calibration. See `MountController::filterOutliers()`.

#### Timestamp (`timestamp`, `julian_date`)

- `timestamp` — system time of the measurement (UTC)
- `julian_date` — Julian Date of the measurement for precise coordinate transformations
- If `julian_date` is 0, it will be automatically calculated from `timestamp`

---

### 2.4 Calibration Data Collection Methods

#### Manual Method (via gRPC API)

The simplest method — manually centering stars and sending measurements:

```bash
# Example: Slew to coordinates (via grpcurl)
grpcurl -d '{"ra_hours": 10.5, "dec_degrees": 41.3}' \
  localhost:50051 mount_controller.MountController/SlewToEquatorial

# Add measurement
grpcurl -d '{
  "observed_ra": 10.4923,
  "observed_dec": 41.2987,
  "expected_ra": 10.5000,
  "expected_dec": 41.3000,
  "mount_ha": -2.3456,
  "mount_dec": 41.3123
}' localhost:50051 mount_controller.MountController/AddTPointMeasurement
```

#### Automatic Method (Star List Script)

Using the measurement collection script (see [Section 6](#6-helper-scripts)), you can automatically slew to a predefined list of calibration stars.

The script:
1. Reads a list of calibration stars from a CSV file
2. Slews to each star
3. Waits for the user to confirm centering (or uses plate solving)
4. Automatically sends the measurement to the controller
5. Saves progress to continue later

#### Method with Plate Solving Integration

For fully automatic calibration:
1. Slew to star coordinates
2. Take a frame
3. Run plate solving to determine actual coordinates (`observed_ra`, `observed_dec`)
4. Automatically send the measurement
5. Move to the next star

This method achieves the highest accuracy (sub-arcsecond) and requires minimal operator intervention.

### 2.5 Sky Coverage

Proper sky coverage is critical for TPOINT model accuracy. Measurements should be evenly distributed across the observable hemisphere.

#### Recommended Measurement Distribution

```
Zenith (90°)
    |
    |   *      *      *
    |
    |      *      *      *         - 60° altitude
    |
    |   *      *      *      *
    |
    |      *      *      *         - 30° altitude
    |
    |   *      *      *
    |
    +---------------------------------- Horizon (0°)
    West   SW   S    SE   East
```

**Recommended distribution:**
- **Minimum 15 points** evenly distributed across the sky
- **3–5 points** near the zenith (>70° altitude)
- **5–8 points** at mid-altitude (30°–70°)
- **3–5 points** near the horizon (15°–30°)
- **At least 3 points** on each side of the meridian
- **Cover both** Eastern and Western hemispheres

#### Sample Calibration Star List

```csv
# RA (hours), Dec (degrees), Name, Magnitude
10.5000, 41.3000, "Alpha-Ursae-Majoris", 1.8
12.0000, 30.0000, "Beta-Virginis", 3.6
14.0000, 20.0000, "Gamma-Bootis", 3.0
16.0000, 10.0000, "Delta-Serpentis", 3.8
18.0000, 0.0000, "Epsilon-Sagittae", 4.0
20.0000, -10.0000, "Zeta-Capricorni", 3.8
22.0000, -20.0000, "Eta-Aquarii", 4.0
0.0000, -30.0000, "Theta-Sculptoris", 4.5
2.0000, -20.0000, "Iota-Ceti", 3.6
4.0000, -10.0000, "Kappa-Eridani", 4.2
6.0000, 0.0000, "Lambda-Orionis", 3.4
8.0000, 20.0000, "Mu-Geminorum", 2.9
```

### 2.6 Minimum Number of Measurements

| Calibration Type | Minimum | Recommended | Optimal |
|-----------------|---------|-------------|---------|
| Bootstrap | 3 | 5–8 | 10–12 |
| TPOINT (6 terms) | 6 | 12–15 | 20–25 |
| TPOINT (12 terms) | 12 | 20–25 | 30–40 |
| TPOINT (full, 36+ terms) | 36 | 50–60 | 80–100 |

#### Checking Calibration Readiness

The controller provides a method to check if enough measurements have been collected:

```cpp
// From MountController::Impl
bool isCalibrationReady() const {
    if (tpoint_measurements_.size() < MIN_TPOINT_MEASUREMENTS) {
        logger_->warn("Not enough measurements: {} < {}", 
                      tpoint_measurements_.size(), MIN_TPOINT_MEASUREMENTS);
        return false;
    }
    
    // Check sky coverage
    double ha_range = getHourAngleRange();
    double dec_range = getDeclinationRange();
    
    if (ha_range < MIN_HA_RANGE) {
        logger_->warn("Hour angle range too small: {}", ha_range);
        return false;
    }
    
    return true;
}
```

### 2.7 Measurement Quality and Outlier Rejection

The controller uses statistical filtering to reject poor measurements:

1. **Residual analysis** — after initial TPOINT fit, measurements with residuals > 3σ are flagged
2. **SNR filter** — measurements with SNR < minimum threshold are rejected
3. **Duplicate detection** — measurements too close to each other (< 1° separation) are merged
4. **Manual rejection** — individual measurements can be removed via gRPC `RemoveTPointMeasurement()`

```cpp
// From MountController::Impl
void filterOutliers() {
    auto& measurements = tpoint_measurements_;
    
    // First pass: remove measurements with low SNR
    measurements.erase(
        std::remove_if(measurements.begin(), measurements.end(),
            [](const Measurement& m) { return m.snr < MIN_SNR; }),
        measurements.end());
    
    // Second pass: 3-sigma clipping after initial fit
    // ... TPOINT model fitting, residual calculation
    // ... remove measurements with |residual| > 3 * RMS
}
```

---

## 3. Calibration Data Collection Methods

### 3.1 Bootstrap vs TPOINT Difference

| Feature | Bootstrap | TPOINT |
|---------|-----------|--------|
| **Purpose** | Initial mount alignment | Precise geometric model |
| **Measurements** | 3–12 | 12–100+ |
| **Model complexity** | 3-parameter (rotation only) | 6–40+ parameters |
| **Accuracy** | ~1 arcminute | ~0.1–1 arcsecond |
| **Hardware required** | None (uses mount encoders) | Plate solving recommended |
| **Time required** | 5–15 minutes | 30–120 minutes |
| **Required fields** | `observed` + `expected` | All fields including `mount_ha/mount_dec` |

### 3.2 Bootstrap Measurement Preparation

#### For EQUATORIAL Mount

Bootstrap calibration for equatorial mounts determines the offset between mount coordinates and celestial coordinates:

```cpp
// Implementation logic (simplified)
// MountController::RunBootstrapCalibration()

// 1. Calculate delta_ha and delta_dec offsets
for (const auto& m : bootstrap_measurements_) {
    double delta_ha = (m.expected_ra - m.observed_ra) * 15.0;  // deg
    double delta_dec = m.expected_dec - m.observed_dec;       // deg
    // Accumulate...
}

// 2. Apply average correction to all subsequent coordinate transformations
```

**Measurement procedure:**
1. Slew to a known bright star
2. Center the star in the eyepiece/camera
3. Record: `AddBootstrapMeasurement(observed={ra, dec}, expected={catalog_ra, catalog_dec})`
4. Repeat for 3+ stars spread across the sky
5. Call `RunBootstrapCalibration()`

#### For CASUAL (Arbitrarily Oriented) Mount

For mounts with arbitrary orientation (e.g., alt-az on a tripod that can be moved):

```cpp
// CASUAL mount bootstrap requires additional parameters
struct CasualCalibration {
    // 3 reference points for rotation matrix determination
    Eigen::Vector3d ref1_mount;  // Mount coordinates (x, y, z)
    Eigen::Vector3d ref1_sky;    // Celestial coordinates (RA, Dec)
    Eigen::Vector3d ref2_mount;
    Eigen::Vector3d ref2_sky;
    Eigen::Vector3d ref3_mount;
    Eigen::Vector3d ref3_sky;
    
    // Result: 3x3 rotation matrix
    Eigen::Matrix3d rotation_matrix;
};
```

---

## 4. System Input Data

### 4.1 Controller Configuration

The [`Configuration`](../../include/config/configuration.h) struct contains all configurable parameters of the controller, many of which are needed as input for the calibration and tracking models.

#### Configuration Fields

```cpp
// From include/config/configuration.h
struct MountConfig {
    // Location
    double latitude{52.0};           // Observatory latitude (degrees, N=positive)
    double longitude{21.0};          // Observatory longitude (degrees, E=positive)
    double altitude{100.0};          // Observatory altitude (m)
    
    // Mount geometry
    double mount_height{1.5};        // Mount height (m)
    double pier_west{0.0};           // Pier offset West (arcsec)
    double pier_east{0.0};           // Pier offset East (arcsec)
    
    // Telescope
    double focal_length{1000.0};     // Focal length (mm)
    double aperture{200.0};          // Aperture (mm)
    
    // Environmental defaults
    double default_temperature{20.0}; // Default temperature (°C)
    double default_pressure{1013.25}; // Default pressure (hPa)
    double default_humidity{50.0};    // Default humidity (%)
    
    // Kalman filter parameters
    double process_noise{1e-6};      // Process noise covariance
    double measurement_noise{1e-4};  // Measurement noise covariance
    
    // Limits
    double max_slew_rate{5.0};       // Maximum slew rate (deg/s)
    double max_tracking_rate{0.1};   // Maximum tracking rate (deg/s)
};
```

#### Configuration Preparation

Configuration is loaded from JSON files (see [`config/`](../../config/) directory):

```json
{
  "mount": {
    "latitude": 49.0,
    "longitude": 20.0,
    "altitude": 650.0,
    "mount_height": 1.2,
    "pier_west": 5.0,
    "pier_east": 3.0,
    "focal_length": 800.0,
    "aperture": 150.0,
    "max_slew_rate": 3.0,
    "max_tracking_rate": 0.05
  },
  "calibration": {
    "tpoint_enabled_terms": 12,
    "min_measurements": 15
  }
}
```

```bash
# Applying the configuration
./astro_mount_controller --config config/my_observatory.json
```

#### Accuracy of Location Parameters

| Parameter | Required Accuracy | Impact |
|-----------|------------------|--------|
| Latitude | ±1 arcsecond (≈30m) | Directly affects tracking rate calculation |
| Longitude | ±1 arcsecond (≈30m) | Affects sidereal time calculation |
| Altitude | ±10m | Minor effect on refraction correction |

### 4.2 Ephemeris Data

Ephemeris data is used by the [`EphemerisTracker`](../../src/models/ephemeris_tracker.cpp) for tracking moving objects.

#### Ephemeris Data Structure

```cpp
// From include/models/ephemeris_tracker.h
struct EphemerisData {
    std::vector<EphemerisPoint> points;
    std::string object_name;
    std::string object_id;         // MPC designation, e.g., "C/2023_A3"
    EphemerisSource source;        // MPC, JPL HORIZONS, custom
    std::chrono::system_clock::time_point epoch;
};

struct EphemerisPoint {
    double julian_date;            // JD(TT)
    double ra_hours;               // Right ascension (hours)
    double dec_degrees;            // Declination (degrees)
    double delta_au;               // Distance from Earth (AU)
    double r_au;                   // Distance from Sun (AU)
    double magnitude;              // Apparent magnitude (optional)
    double rate_ra_arcsec_h;       // RA rate (arcsec/hour)
    double rate_dec_arcsec_h;      // Dec rate (arcsec/hour)
    double position_angle;         // Position angle of motion (degrees)
    double elongation;             // Elongation from Sun (degrees)
};
```

#### Ephemeris Data Preparation

**From JPL HORIZONS:**
1. Go to [SSD JPL HORIZONS](https://ssd.jpl.nasa.gov/horizons/)
2. Select the object
3. Set output format to: "Table format (CSV)"
4. Include: RA, Dec, Delta, R, Magnitude, RA rate, Dec rate
5. Save as CSV and convert to the internal format

**From MPC (Minor Planet Center):**
```python
# MPC ephemeris download
from astroquery.mpc import MPC
ephem = MPC.get_ephemeris('C/2023_A3',
    location='500',  # Geocentric
    start='2025-01-01',
    step='1d')
```

**Internal format (JSON/Protobuf):**
```json
{
  "object_name": "C/2023_A3 (Tsuchinshan-ATLAS)",
  "object_id": "C/2023_A3",
  "source": "JPL_HORIZONS",
  "epoch": "2025-01-01T00:00:00Z",
  "points": [
    {
      "julian_date": 2460681.5,
      "ra_hours": 15.2345,
      "dec_degrees": -20.1234,
      "delta_au": 1.234,
      "r_au": 2.345,
      "magnitude": 8.5,
      "rate_ra_arcsec_h": 12.3,
      "rate_dec_arcsec_h": -5.6,
      "position_angle": 45.0,
      "elongation": 120.0
    }
  ]
}
```

#### Ephemeris Data Validation

The controller validates ephemeris data before use:

```cpp
// From EphemerisTracker::Impl
bool validateEphemeris(const EphemerisData& data) {
    if (data.points.size() < 2) {
        logger_->error("Need at least 2 ephemeris points");
        return false;
    }
    
    // Check temporal order
    for (size_t i = 1; i < data.points.size(); i++) {
        if (data.points[i].julian_date <= data.points[i-1].julian_date) {
            logger_->error("Ephemeris points not in chronological order");
            return false;
        }
    }
    
    // Check rate consistency
    // ... validate that rates are consistent with position changes
    
    return true;
}
```

### 4.3 Autoguider Data

Guider data is used for real-time tracking corrections:

**Connection:** The guider connects via TCP, typically using the PHD2 / PHD2-like protocol:

```cpp
// Guider connection configuration
struct GuiderConfig {
    std::string connection_type = "tcp";  // "tcp", "file", "simulated"
    std::string address = "localhost";
    int port = 7624;                      // Default PHD2 port
    double max_correction_arcsec = 10.0;  // Maximum correction (arcsec)
    double aggression = 0.5;              // Correction aggression (0.0-1.0)
    int calibration_time_sec = 30;        // Calibration time
};
```

**Data format from guider:**
```json
{
  "ra_correction_arcsec": 1.5,
  "dec_correction_arcsec": -0.8,
  "ra_duration_ms": 500,
  "dec_duration_ms": 300,
  "timestamp": "2025-01-01T12:34:56Z"
}
```

**Usage in controller:**
```cpp
// From MountController::Impl
void processGuiderCorrection(double ra_correction_arcsec, 
                              double dec_correction_arcsec) {
    // Convert to mount coordinate system
    double ha_correction = ra_correction_arcsec / cos(current_dec_ * DEG2RAD);
    double dec_correction = dec_correction_arcsec;
    
    // Apply via Kalman filter or directly to motor control
    if (use_kalman_filter_) {
        kalman_filter_->update(ha_correction, dec_correction);
    } else {
        // Direct correction to axis positions
        axis1_target_ += ha_correction / 3600.0;  // Convert to degrees
        axis2_target_ += dec_correction / 3600.0;
    }
}
```

### 4.4 Encoder Data

Encoder data provides precise position feedback:

**Supported encoder types:**
- **ABSOLUTE** — absolute position encoder (e.g., Renishaw, Heidenhain)
- **INCREMENTAL** — incremental with index pulse

**Encoder configuration:**
```json
{
  "encoder": {
    "axis0": {
      "type": "absolute",
      "resolution": 36000,
      "counts_per_degree": 100.0,
      "use_feedback": true
    },
    "axis1": {
      "type": "absolute",
      "resolution": 36000,
      "counts_per_degree": 100.0,
      "use_feedback": true
    }
  }
}
```

**Data flow:**
```
Encoder → HAL::EncoderReader → MountController → KalmanFilter
                                                      ↓
                                              Position correction
```

### 4.5 Axis Physical Parameters

Detailed mechanical properties of each axis, used for error modeling and compensation:

```cpp
struct AxisPhysicalParameters {
    // Motor parameters
    double motor_steps_per_rev{200};       // Motor steps per revolution
    double motor_microstepping{16};        // Microstepping mode (1, 2, 4, 8, 16, 32)
    double motor_step_angle{0.9};          // Motor step angle (degrees)
    
    // Encoder parameters
    double encoder_resolution{36000};       // Encoder counts per revolution
    double encoder_counts_per_arcsec;       // Calculated: encoder counts per arcsec
    double encoder_quantization_error;      // Encoder quantization error (arcsec)
    
    // Gear parameters
    double gear_ratio{144.0};              // Total gear ratio
    double worm_ratio{1.0};                // Worm gear ratio
    int32_t worm_teeth{1};                 // Number of worm starts
    int32_t worm_wheel_teeth{144};         // Number of worm wheel teeth
    
    // Periodic errors
    double cyclic_error_amplitude{10.0};   // Cyclic error amplitude (arcsec)
    double cyclic_error_period{360.0};     // Cyclic error period (degrees)
    std::vector<double> cyclic_harmonics;  // Harmonic components
    
    // Mechanical errors
    double backlash{30.0};                 // Backlash (arcsec)
    double backlash_temp_coeff{0.5};       // Backlash temperature coefficient (arcsec/°C)
    double axis_stiffness{1.0};            // Axis stiffness (arcsec/Nm)
    double torsional_compliance{0.01};     // Torsional compliance (rad/Nm)
    
    // Thermal expansion
    double expansion_coeff{12e-6};         // Thermal expansion coefficient (1/°C)
    double temp_gear_error_coeff{0.1};     // Temperature gear error coefficient (arcsec/°C)
    
    // Calibration table (for PEC)
    std::vector<double> calibration_table;
    double calibration_temp{20.0};         // Temperature at which PEC was recorded (°C)
};
```

#### Calculating Effective Resolution

```cpp
// Effective resolution calculation
double motor_steps_per_rev = 200;       // Standard stepper motor
double microstepping = 16;              // 1/16 microstepping
double gear_ratio = 144.0;             // Overall gear ratio
double encoder_resolution = 36000;     // Absolute encoder counts/rev

// Resolution via motor + microstepping:
double motor_resolution_deg = 360.0 / (motor_steps_per_rev * microstepping * gear_ratio);
// = 360.0 / (200 * 16 * 144) = 360.0 / 460800 = 0.00078125°/step ≈ 2.81"/step

// Effective with encoder feedback:
double encoder_effective = 360.0 / encoder_resolution;
// = 360.0 / 36000 = 0.01°/count
```

```
Resolution: 1,296,000" / 4,608,000 = 0.28"/step
```

---

## 5. Error Sources

| Error Source | Typical Magnitude | Mitigation |
|-------------|-------------------|------------|
| Atmospheric refraction | 10–60" at 30° alt | Refraction model in TPOINT |
| Polar alignment error | 100–600" | Bootstrap + drift alignment |
| Cone error (optical axis vs mount) | 100–600" | TPOINT term CH |
| Shaft flexure | 10–60" | TPOINT terms ME, MA |
| Tube flexure | 5–30" | Mechanical improvement + TPOINT |
| Gear periodic error | 5–40" | PEC (Periodic Error Correction) + TPOINT |
| Backlash | 10–60" | Backlash compensation + TPOINT |
| Encoder quantization | 0.1–5" | Higher resolution encoder + interpolation |
| Thermal expansion | 1–10"/°C | Temperature compensation + calibration |
| Wind buffeting | 0.5–5" | Guider + dome/baffle |
| Seeing | 0.5–5" | Statistical averaging + Kalman filter |
| Plate solving error | 0.1–1" | Multiple frames + averaging |

### Practical Tips

1. **Collect measurements on both sides of the meridian** — this helps model pier-side asymmetries
2. **Avoid low altitudes** (< 20°) — atmospheric refraction becomes large and unpredictable
3. **Use bright stars** for bootstrap, **fainter stars** for TPOINT (to avoid saturation)
4. **Record temperature** for each measurement — enables thermal error modeling
5. **Collect measurements over multiple nights** — for the best model, use 2–3 sessions
6. **Refine the model iteratively** — after initial calibration, test and add more points in poorly modeled areas

---

## 6. Helper Scripts

### Measurement Collection Script (Python)

```python
#!/usr/bin/env python3
"""
Measurement collection script for astro-mount-controller.
Slews to a list of calibration stars and collects measurements.
"""

import grpc
import time
import csv
import argparse
import mount_controller_pb2 as pb2
import mount_controller_pb2_grpc as pb2_grpc
from datetime import datetime

class CalibrationCollector:
    def __init__(self, address="localhost:50051"):
        channel = grpc.insecure_channel(address)
        self.stub = pb2_grpc.MountControllerStub(channel)
        self.measurements = []
    
    def slew_to_star(self, ra_hours, dec_degrees):
        """Slew to star coordinates."""
        request = pb2.Coordinates(
            ra_hours=ra_hours,
            dec_degrees=dec_degrees
        )
        response = self.stub.SlewToEquatorial(request)
        return response
    
    def add_measurement(self, observed_ra, observed_dec, 
                       expected_ra, expected_dec,
                       mount_ha, mount_dec,
                       temperature=20.0, pressure=1013.25,
                       snr=100.0):
        """Add a TPOINT measurement."""
        request = pb2.TPointMeasurement(
            observed_ra=observed_ra,
            observed_dec=observed_dec,
            expected_ra=expected_ra,
            expected_dec=expected_dec,
            mount_ha=mount_ha,
            mount_dec=mount_dec,
            temperature=temperature,
            pressure=pressure,
            snr=snr
        )
        response = self.stub.AddTPointMeasurement(request)
        print(f"Measurement added: {response}")
        return response
    
    def run_calibration(self):
        """Run TPOINT calibration."""
        response = self.stub.RunTPointCalibration(pb2.google_dot_protobuf_dot_empty__pb2.Empty())
        print(f"Calibration result: {response}")
        return response
    
    def collect_from_csv(self, csv_file):
        """Collect measurements from a CSV star list."""
        with open(csv_file, 'r') as f:
            reader = csv.DictReader(f)
            for row in reader:
                ra = float(row['ra_hours'])
                dec = float(row['dec_degrees'])
                
                print(f"\nSlewing to star: {row.get('name', 'Unknown')}")
                print(f"  RA={ra}h, Dec={dec}°")
                
                # Slew to star
                self.slew_to_star(ra, dec)
                
                # Wait for slew to complete
                time.sleep(2)
                
                # Wait for user confirmation
                input("Press Enter after centering the star...")
                
                # Get current mount position
                state = self.stub.GetState(pb2.google_dot_protobuf_dot_empty__pb2.Empty())
                current_pos = state.current_position
                
                # Calculate mount hour angle
                lst = self.get_local_sidereal_time()
                mount_ha = lst - current_pos.ra_hours
                
                # Add measurement
                self.add_measurement(
                    observed_ra=current_pos.ra_hours,
                    observed_dec=current_pos.dec_degrees,
                    expected_ra=ra,
                    expected_dec=dec,
                    mount_ha=mount_ha,
                    mount_dec=current_pos.dec_degrees
                )
        
        # Run calibration
        run = input("\nRun TPOINT calibration? (y/n): ")
        if run.lower() == 'y':
            self.run_calibration()
    
    def get_local_sidereal_time(self):
        """Get LST from controller."""
        response = self.stub.GetSiderealTime(
            pb2.google_dot_protobuf_dot_empty__pb2.Empty())
        return response.hours

def main():
    parser = argparse.ArgumentParser(
        description="TPOINT calibration measurement collector")
    parser.add_argument("csv_file", help="CSV file with calibration star list")
    parser.add_argument("--address", default="localhost:50051",
                       help="gRPC server address")
    args = parser.parse_args()
    
    collector = CalibrationCollector(args.address)
    collector.collect_from_csv(args.csv_file)

if __name__ == "__main__":
    main()
```

### Calibration Star List Generator

```python
#!/usr/bin/env python3
"""
Generates a list of calibration stars evenly distributed across the sky.
Uses the Hipparcos catalog (bright stars for easy centering).
"""

import numpy as np
import argparse

def generate_star_grid(n_alt=3, n_az=4, min_alt=20, max_alt=85):
    """
    Generate evenly distributed star positions.
    
    Args:
        n_alt: Number of altitude bands
        n_az: Number of azimuth points per band
        min_alt: Minimum altitude (degrees)
        max_alt: Maximum altitude (degrees)
    
    Returns:
        List of (alt, az) tuples
    """
    stars = []
    
    altitudes = np.linspace(min_alt, max_alt, n_alt)
    
    for i, alt in enumerate(altitudes):
        # More points near the horizon, fewer near zenith
        n_points = max(3, int(n_az * np.sin(np.radians(alt))))
        
        azimuths = np.linspace(0, 360, n_points, endpoint=False)
        
        for az in azimuths:
            stars.append((alt, az))
    
    return stars

def main():
    parser = argparse.ArgumentParser(
        description="Generate calibration star positions")
    parser.add_argument("--n-alt", type=int, default=3,
                       help="Number of altitude bands")
    parser.add_argument("--n-az", type=int, default=6,
                       help="Number of azimuth points")
    parser.add_argument("--min-alt", type=float, default=20,
                       help="Minimum altitude (degrees)")
    parser.add_argument("--max-alt", type=float, default=85,
                       help="Maximum altitude (degrees)")
    parser.add_argument("--output", default="calibration_stars.csv",
                       help="Output CSV file")
    
    args = parser.parse_args()
    
    stars = generate_star_grid(
        n_alt=args.n_alt,
        n_az=args.n_az,
        min_alt=args.min_alt,
        max_alt=args.max_alt
    )
    
    print(f"Generated {len(stars)} calibration star positions")
    
    with open(args.output, 'w') as f:
        f.write("altitude,azimuth\n")
        for alt, az in stars:
            f.write(f"{alt:.1f},{az:.1f}\n")
    
    print(f"Saved to {args.output}")

if __name__ == "__main__":
    main()
```

---

## 7. Usage Scenarios

### Scenario 1: First Calibration of a New Mount

**Goal:** Perform a complete first calibration from scratch.

**Steps:**
1. Set up the mount and enter location coordinates in the configuration
2. Slew to 3 bright stars for bootstrap calibration
3. Collect 15–25 TPOINT measurements across the sky
4. Run TPOINT calibration
5. Verify model quality (chi-squared, residuals)
6. Perform a test tracking run and verify pointing accuracy

**Expected result:** Pointing accuracy of 10–30 arcseconds RMS (depending on mount quality).

### Scenario 2: Refining an Existing Model

**Goal:** Improve an existing TPOINT model for sub-arcsecond pointing.

**Steps:**
1. Load the existing TPOINT model
2. Identify sky areas with the largest residuals
3. Collect additional measurements in those areas (10–15 points)
4. Re-run TPOINT calibration with the augmented dataset
5. Verify improvement in residual RMS

**Expected result:** 20–50% reduction in residual RMS.

### Scenario 3: Calibration After Configuration Change

**Goal:** Recalibrate after changing the optical train (camera, corrector, etc.).

**Steps:**
1. Clear existing measurements (the cone error has changed)
2. Collect 6–10 bootstrap measurements
3. Run bootstrap calibration
4. Collect 15–20 TPOINT measurements (if sub-arcsecond accuracy is needed)
5. Run TPOINT calibration

**Expected result:** Pointing accuracy restored to the previous level after a few measurements.
