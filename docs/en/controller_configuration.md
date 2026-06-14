# Mount Controller Configuration File

## 1. Introduction

The configuration file in JSON format defines all operating parameters of the astronomical mount controller.
Default location: [`config/default.json`](config/default.json).
An alternative path can be passed as the first program argument (`argv[1]`).

Configuration loading is handled in [`src/config/configuration.cpp`](src/config/configuration.cpp) via the
`Configuration::Impl::loadFromFile()` class. The system supports hot-reload through [`ConfigMonitor`](src/config/config_monitor.cpp),
which monitors the file for modification time changes and automatically reloads the configuration.

---

## 2. Top-Level File Structure

```json
{
  "logging": { ... },
  "network": { ... },
  "canopen": { ... },
  "mount": { ... },
  "telescope": { ... },
  "guider": { ... },
  "kalman": { ... },
  "tpoint": { ... },
  "derotator": { ... },
  "field_rotation": { ... },
  "hal": { ... }
}
```

All sections are optional. If a section is missing, default values defined in
[`Configuration::Impl::initializeDefaults()`](src/config/configuration.cpp:891) are used.

---

## 3. `logging` Section — Logging Configuration

Defines how system logs are written.

| Parameter | Type | Range / Allowed Values | Default Value | Description |
|---|---|---|---|---|
| `level` | string | `"DEBUG"`, `"INFO"`, `"WARNING"`, `"ERROR"`, `"CRITICAL"` | `"INFO"` | Log verbosity level |
| `directory` | string | any absolute path | `"/var/log/astro-mount"` | Target directory for log files |
| `rotation_days` | integer | ≥ 1 | `7` | Log rotation after N days (unused — see notes below) |
| `max_file_size_mb` | integer | ≥ 1 | `100` | Maximum log file size in MB (unused — see notes below) |
| `console_output` | boolean | `true`, `false` | `true` | Whether to also output logs to console |

Log level validation: [`Configuration::Impl::isValidLogLevel()`](src/config/configuration.cpp:1055).

### Implementation Details (as of June 2025)

The logging system is based on the **spdlog** library. Initialization happens in [`Logger::initProgrammatic()`](src/logging/logger.cpp:68), called from [`main.cpp`](src/main.cpp:48).

#### Architecture

- **File sink**: [`basic_file_sink_mt`](src/logging/logger.cpp:90) — append mode (`"ab"`, no rotation). Log file rotation is handled **externally** (e.g., via `logrotate`). The `rotation_days` and `max_file_size_mb` config parameters are retained for backward compatibility but are not functionally used.
- **Console sink**: [`stdout_color_sink_mt`](src/logging/logger.cpp:97) — colorized stdout output.
- **Syslog sink** (optional): [`syslog_sink_mt`](src/logging/logger.cpp:104).
- **spdlog global registry registration**: All loggers are registered via [`spdlog::register_logger()`](src/logging/logger.cpp:384-386), enabling `spdlog::flush_every()` to find and flush them.
- **Periodic flush**: [`spdlog::flush_every(5s)`](src/logging/logger.cpp:119) — the stdio buffer is flushed to disk every 5 seconds.

#### Log Format

```
[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] [%n] %v
```

Example log entry:
```
[2025-06-01 22:30:15.123] [INFO] [mount] System initialized
```

#### Component Loggers

On startup, 13 predefined loggers are created ([`createDefaultLoggers()`](src/logging/logger.cpp:368)):

| Name | Purpose |
|---|---|
| `mount` | Main mount controller |
| `api` | API and gRPC |
| `canopen` | CANopen bus |
| `tpoint` | TPoint model |
| `kalman` | Kalman filter |
| `config` | Configuration management |
| `safety` | Safety system |
| `structured` | Structured data |
| `audit` | Operation audit |
| `performance` | Performance and timing |
| `error` | Errors and exceptions |
| `calibration` | Calibration |
| `health` | System health |

Additional loggers can be created on demand via [`Logger::get(name)`](src/logging/logger.cpp:166-185) — each new logger is automatically registered in the spdlog global registry and inherits the same sinks.

#### Log File

Default: `{directory}/astro-mount.log` (e.g. `/var/log/astro-mount/astro-mount.log`).

---

## 4. `network` Section — Network Configuration (gRPC)

Defines the gRPC server for external communication.

| Parameter | Type | Range / Allowed Values | Default Value | Description |
|---|---|---|---|---|
| `grpc_address` | string | valid IPv4 address | `"0.0.0.0"` | gRPC server listen address |
| `grpc_port` | integer | 1–65535 | `50051` | gRPC server port |
| `max_connections` | integer | 1–1000 | `10` | Maximum number of concurrent connections |
| `enable_ssl` | boolean | `true`, `false` | `false` | Whether to enable TLS encryption |
| `ssl_cert_path` | string | path to .pem/.crt file | `""` (empty) | SSL certificate path |
| `ssl_key_path` | string | path to .key file | `""` (empty) | SSL private key path |

Implementation: [`NetworkConfig`](include/config/configuration.h:30).

---

## 5. `canopen` Section — CANopen Bus Configuration

Applies to the CANopen layer — the CAN bus interface.

| Parameter | Type | Range / Allowed Values | Default Value | Description |
|---|---|---|---|---|
| `interface` | string | SocketCAN interface name | `"can0"` | CAN interface name (e.g. can0, can1, vcan0) |
| `node_id` | integer | 1–127 (CiA 301) | `1` | CANopen Node ID of the controller (own address) |
| `baud_rate` | integer | 10000, 20000, 50000, 100000, 125000, 250000, 500000, 800000, 1000000 | `1000000` | CAN bus baud rate (1 Mbit/s) |
| `enable_sync` | boolean | `true`, `false` | `true` | Whether to enable cyclic SYNC (CiA 301 §7.2.4) |
| `sync_interval_ms` | integer | 10–10000 | `100` | SYNC interval in milliseconds |

**Note:** `node_id` in this section is the controller's own address on the CAN bus, **not** the servo drive addresses.
Drive addresses are configured in the [`hal.axes[].can_node_id`](#831-can_node_id) section.

Implementation: [`CanOpenConfig`](include/config/configuration.h:39),
read: [`Configuration::getCanOpenConfig()`](src/config/configuration.cpp:302).

---

## 6. `mount` Section — Mount Configuration

### 6.1 Global Mount Parameters

| Parameter | Type | Range / Allowed Values | Default Value | Description |
|---|---|---|---|---|
| `type` | string | `"equatorial"`, `"altazimuth"` | `"equatorial"` | Mount type: equatorial (EQ) or altazimuth (Alt-Az) |
| `latitude` | float | -90.0 to 90.0 | `52.0` | Observatory latitude (degrees) |
| `longitude` | float | -180.0 to 180.0 | `21.0` | Longitude (degrees, +E) |
| `altitude` | float | 0.0 to 10000.0 | `100.0` | Elevation above sea level (meters) |
| `mount_height` | float | 0.0 to 10.0 | `1.5` | Mount axis height above ground level (m) |
| `pier_west` | float | -180.0 to 180.0 | `0.0` | Pier offset for the west side of the mount (deg) |
| `pier_east` | float | -180.0 to 180.0 | `0.0` | Pier offset for the east side of the mount (deg) |
| `default_temperature` | float | -50.0 to 60.0 | `15.0` | Default temperature (°C) for refraction |
| `default_pressure` | float | 500.0 to 1100.0 | `1013.25` | Default pressure (hPa) for refraction |
| `default_humidity` | float | 0.0 to 1.0 | `0.5` | Default humidity (0.0–1.0) for refraction |
| `use_encoders` | boolean | `true`, `false` | `true` | Whether to use encoders for feedback |
| `encoders_absolute` | boolean | `true`, `false` | `true` | Whether encoders are absolute (true) or incremental (false) |
| `encoder_resolution` | integer | 1–2³² | `16384` | Encoder resolution (counts/revolution) |

### 6.2 Motion Parameters

| Parameter | Type | Range / Allowed Values | Default Value | Description |
|---|---|---|---|---|
| `max_slew_rate` | float | 0.1 to 20.0 | `5.0` | Maximum slew speed (deg/s) |
| `max_tracking_rate` | float | 0.0 to 0.1 | `0.004178` | Maximum tracking speed (~1× sidereal, deg/s) |
| `slew_acceleration` | float | 0.01 to 10.0 | `1.0` | Slew acceleration (deg/s²) |
| `tracking_acceleration` | float | 0.0001 to 0.1 | `0.001` | Tracking acceleration (deg/s²) |
| `position_tolerance` | float | 0.001 to 10.0 | `0.1` | Position tolerance for considering a target reached (arcsec) |
| `rate_tolerance` | float | 0.0001 to 1.0 | `0.01` | Rate tolerance (deg/s) |

### 6.3 Meridian Flip

| Parameter | Type | Range / Allowed Values | Default Value | Description |
|---|---|---|---|---|
| `meridian_flip_enabled` | boolean | `true`, `false` | `true` | Whether to enable automatic meridian flip |
| `meridian_flip_delay_minutes` | float | 0.0 to 60.0 | `5.0` | Flip delay after reaching the meridian (min) |
| `meridian_flip_hysteresis_degrees` | float | 0.0 to 10.0 | `0.5` | Flip hysteresis (deg) — prevents oscillations |
| `meridian_flip_timeout_seconds` | float | 10.0 to 600.0 | `120.0` | Maximum flip duration (s) |

### 6.4 Soft Limits

| Parameter | Type | Range / Allowed Values | Default Value | Description |
|---|---|---|---|---|
| `soft_limits_enabled` | boolean | `true`, `false` | `true` | Whether to enable soft position limits |
| `soft_limit_axis1_min` | float | -360.0 to 360.0 | `-270.0` | Minimum position for axis 1 (HA/Azm, deg) |
| `soft_limit_axis1_max` | float | -360.0 to 360.0 | `270.0` | Maximum position for axis 1 (HA/Azm, deg) |
| `soft_limit_axis2_min` | float | -360.0 to 360.0 | `-5.0` | Minimum position for axis 2 (Dec/Alt, deg) |
| `soft_limit_axis2_max` | float | -360.0 to 360.0 | `185.0` | Maximum position for axis 2 (Dec/Alt, deg) |
| `soft_limit_warning_degrees` | float | 0.0 to 90.0 | `10.0` | Warning zone before the limit (deg) |
| `soft_limit_deceleration_degrees` | float | 0.0 to 90.0 | `5.0` | Deceleration zone before the limit (deg) |
| `soft_limit_tracking_rate_factor` | float | 0.0 to 1.0 | `0.1` | Speed reduction factor in the warning zone |

### 6.5 Parking Position

| Parameter | Type | Range | Default Value | Description |
|---|---|---|---|---|
| `park_position_axis1` | float | -360.0 to 360.0 | `0.0` | Parking position for axis 1 (deg) |
| `park_position_axis2` | float | -360.0 to 360.0 | `90.0` | Parking position for axis 2 (deg) |

### 6.6 Atmospheric Refraction

| Parameter | Type | Range | Default Value | Description |
|---|---|---|---|---|
| `enable_refraction_correction` | boolean | `true`, `false` | `true` | Whether to enable atmospheric refraction correction |
| `orientation_quaternion` | float[4] | unit vector | `[1,0,0,0]` | Mount orientation quaternion (x, y, z, w) |

### 6.7 Axis Physical Parameters (`axis_physical_parameters`)

Defines the mechanical characteristics of each mount axis.

#### 6.7.1 `ha_axis` / `dec_axis`

| Parameter | Type | Range | Default Value | Description |
|---|---|---|---|---|
| `encoder_resolution` | integer | 1–2³² | `16384` | Encoder resolution (counts/revolution) |
| `encoder_counts_per_arcsec` | float | > 0 | `0.0126` | Number of encoder counts per arcsecond |
| `encoder_quantization_error` | float | > 0 | `39.6` | Encoder quantization error (mas) |
| `gear_ratio` | float | 0.1 to 10000.0 | `360.0` | Total gear ratio |
| `worm_ratio` | float | 0.1 to 1000.0 | `180.0` | Worm gear ratio |
| `worm_teeth` | integer | 1–100 | `1` | Number of worm teeth (typically 1) |
| `worm_wheel_teeth` | integer | 1–1000 | `180` | Number of worm wheel teeth |
| `cyclic_error_amplitude` | float | 0.0 to 100.0 | `15.2` (HA) / `12.8` (Dec) | Cyclic error amplitude (arcsec) |
| `cyclic_error_period` | float | 0.0 to 360.0 | `360.0` | Cyclic error period (deg) |
| `cyclic_harmonics` | float[8] | arbitrary | axis-dependent | Cyclic error harmonic coefficients |
| `backlash` | float | 0.0 to 100.0 | `8.5` (HA) / `6.3` (Dec) | Mechanical backlash (arcsec) |
| `backlash_temp_coeff` | float | 0.0 to 1.0 | `0.02` (HA) / `0.015` (Dec) | Backlash temperature coefficient (arcsec/°C) |
| `axis_stiffness` | float | 0.0 to 10.0 | `0.5` (HA) / `0.6` (Dec) | Axis stiffness |
| `torsional_compliance` | float | > 0 | `1e-6` (HA) / `1.2e-6` (Dec) | Torsional compliance (rad/Nm) |
| `expansion_coeff` | float | > 0 | `11.0e-6` | Coefficient of thermal expansion (1/°C) |
| `temp_gear_error_coeff` | float | > 0 | `0.05` (HA) / `0.04` (Dec) | Gear error temperature coefficient (arcsec/°C) |
| `calibration_temp` | float | -50.0 to 60.0 | `20.0` | Calibration temperature (°C) |
| `calibration_table` | array | list of [position, correction] | `[]` | Calibration table (deg → arcsec pairs) |

Implementation: [`MountConfig`](include/config/configuration.h:86),
[`AxisPhysicalParameters`](include/config/configuration.h:47),
read: [`Configuration::getMountConfig()`](src/config/configuration.cpp:315).

---

## 7. `telescope`, `guider`, `kalman`, `tpoint` Sections

### 7.1 `telescope` — Telescope Parameters

| Parameter | Type | Range | Default Value | Description |
|---|---|---|---|---|
| `focal_length` | float | 10.0 to 50000.0 | `2000.0` | Telescope focal length (mm) |
| `aperture` | float | 10.0 to 5000.0 | `200.0` | Telescope aperture/diameter (mm) |
| `tube_length` | float | 100.0 to 10000.0 | `1800.0` | Tube length (mm) |
| `camera_model` | string | any | `"ASI1600"` | Camera model |
| `pixel_size` | float | 0.1 to 100.0 | `3.8` | Pixel size (µm) |
| `sensor_width` | integer | 1–20000 | `4656` | Sensor width (pixels) |
| `sensor_height` | integer | 1–20000 | `3520` | Sensor height (pixels) |

### 7.2 `guider` — Autoguider Configuration

| Parameter | Type | Range | Default Value | Description |
|---|---|---|---|---|
| `enabled` | boolean | `true`, `false` | `false` | Whether to enable autoguider |
| `connection_string` | string | any | `""` | Connection string (protocol-dependent) |
| `max_correction` | float | 0.0 to 100.0 | `10.0` | Maximum correction (arcsec) |
| `aggression` | float | 0.0 to 1.0 | `0.5` | Correction aggressiveness (0=none, 1=full) |
| `exposure_time_ms` | integer | 100–60000 | `2000` | Guider exposure time (ms) |
| `binning` | integer | 1–4 | `2` | Binning (1=no binning) |

### 7.3 `kalman` — Kalman Filter

| Parameter | Type | Range | Default Value | Description |
|---|---|---|---|---|
| `process_noise` | float | 1e-10 to 1.0 | `0.01` | Process noise (Q) — higher = faster adaptation |
| `measurement_noise` | float | 1e-10 to 1.0 | `1.0` | Measurement noise (R) — higher = smoother output |
| `adaptive_q` | boolean | `true`, `false` | `true` | Adaptive Q (adjusts to conditions) |
| `adaptive_r` | boolean | `true`, `false` | `false` | Adaptive R |
| `innovation_threshold` | float | 0.1 to 10.0 | `3.0` | Innovation threshold (outlier rejection in sigma) |
| `max_iterations` | integer | 1–100 | `10` | Maximum number of correction iterations |

### 7.4 `tpoint` — TPoint Model

| Parameter | Type | Range | Default Value | Description |
|---|---|---|---|---|
| `enabled_terms` | integer | 0–65535 | `65535` | Bitmask of enabled TPoint terms |
| `min_measurements` | integer | 3–1000 | `10` | Minimum measurements for calibration |
| `max_residual` | float | 0.1 to 100.0 | `30.0` | Maximum allowable residual (arcsec) |
| `auto_calibrate` | boolean | `true`, `false` | `true` | Auto-calibrate when minimum measurements are reached |

Implementation: [`TelescopeConfig`](include/config/configuration.h:142),
[`GuiderConfig`](include/config/configuration.h:152),
[`KalmanConfig`](include/config/configuration.h:161),
[`TPointConfig`](include/config/configuration.h:170).

---

## 8. `hal` Section — Hardware Abstraction Layer (HAL)

The `hal` section defines the hardware layer configuration. Parsed by
[`HALConfig::fromJson()`](include/hal/hal_config.h:222).

```json
"hal": {
  "type": "...",
  "name": "...",
  "canopen": { ... },
  "axes": [ ... ],
  "gamepad": { ... },
  "pid_params": { ... },
  "safety": { ... }
}
```

### 8.1 General HAL Parameters

| Parameter | Type | Allowed Values | Default Value | Description |
|---|---|---|---|---|
| `type` | string | `"canopen"`, `"simulated"`, `"ethernet"`, `"serial"`, `"gamepad"` | `"canopen"` | HAL backend type |
| `name` | string | any | `""` | HAL instance name (for logs/debug) |

### 8.2 `hal.canopen` — CANopen Configuration in the HAL Layer

Independent from the main [`canopen`](#5-canopen-section--canopen-bus-configuration) section.
Concerns integration details with the CANopen library.

| Parameter | Type | Range | Default Value | Description |
|---|---|---|---|---|
| `library` | string | `"canopensocket"`, `"libcanopen"` | `"canopensocket"` | CANopen library |
| `interface_name` | string | interface name | `"can0"` | SocketCAN interface |
| `bitrate` | integer | 10000–1000000 | `1000000` | Baud rate (bps) |
| `node_id` | integer | 1–127 | `1` | Local controller Node ID |
| `use_sync` | boolean | `true`, `false` | `true` | Whether to use SYNC |
| `sync_period_ms` | integer | 10–10000 | `100` | SYNC period (ms) |
| `sdo_timeout_ms` | integer | 100–10000 | `1000` | SDO timeout (ms) |
| `pdo_update_rate` | integer | 1–1000 | `100` | PDO update frequency (Hz) |

#### 8.2.1 `hal.canopen.nmt` — NMT (Network Management)

According to CiA 301 (DS-301).

| Parameter | Type | Range | Default Value | Description |
|---|---|---|---|---|
| `enable_nmt` | boolean | `true`, `false` | `true` | Whether to enable NMT monitoring |
| `heartbeat_period_ms` | integer | 10–10000 | `100` | Heartbeat period (ms) |
| `heartbeat_timeout_ms` | integer | 50–30000 | `500` | Heartbeat timeout (ms) |
| `max_missed_heartbeats` | integer | 1–100 | `3` | Max missed heartbeats before action |
| `enable_bootup_check` | boolean | `true`, `false` | `true` | Whether to check node bootup |
| `bootup_timeout_ms` | integer | 100–30000 | `5000` | Bootup timeout (ms) |
| `enable_auto_recovery` | boolean | `true`, `false` | `true` | Automatic recovery after communication loss |
| `recovery_interval_s` | integer | 1–300 | `5` | Minimum interval between recovery attempts (s) — anti-flapping protection |
| `enable_node_guarding` | boolean | `true`, `false` | `false` | Node Guarding (CiA 301 §7.2.6.2) instead of Heartbeat |
| `node_guarding_period_ms` | integer | 50–10000 | `1000` | Node Guarding period (ms) |

NMT implementation: [`CanOpenHAL::nmtMonitoringThread()`](src/hal/canopen_hal/canopen_hal.cpp:1397) —
100 Hz loop with 6 phases:
1. **Bootup** — waiting for node initialization confirmation
2. **Heartbeat** — cyclic node status checking
3. **Communication loss** — reaction after `MAX_MISSED_HB` missed heartbeats (→ PRE-OPERATIONAL)
4. **Auto-recovery** — automatic restoration to OPERATIONAL after communication is regained
5. **Initial transition** — first PRE-OPERATIONAL → OPERATIONAL transition
6. **Reporting** — periodic network status logging (~1s)

### 8.3 `hal.axes[]` — Axis Configuration

Array of objects, each defining one drive axis.

#### 8.3.1 General Axis Parameters

| Parameter | Type | Range | Default Value | Description |
|---|---|---|---|---|
| `id` | integer | 0–N | `0` | Axis index (0 = HA/Azm, 1 = Dec/Alt, 2 = Derotator) |
| `name` | string | any | `"Axis_0"` | Axis name (for logs/debug) |
| [`can_node_id`](include/hal/hal_config.h:135) | integer | 0–127 | `0` (auto) | Drive CANopen Node ID. **`0` means automatic mapping: `axis_id + 1`** |

**Important:** `can_node_id` is the servo drive's address on the CAN bus. A value of `0` (default) provides
backward compatibility: axis 0 → node 1, axis 1 → node 2, axis 2 → node 3.
For non-standard addresses (e.g. 5, 6) this value must be explicitly set.

Implementation in [`canopen_hal.cpp`](src/hal/canopen_hal/canopen_hal.cpp:1450):
```cpp
auto getNodeId = [this](int axis_index) -> uint8_t {
    if (axis_index < config_.axes.size() && config_.axes[axis_index].can_node_id > 0)
        return config_.axes[axis_index].can_node_id;
    return static_cast<uint8_t>(axis_index + 1);  // fallback
};
```

#### 8.3.2 `motor_config` — Motor Configuration

| Parameter | Type | Allowed Values | Default Value | Description |
|---|---|---|---|---|
| `type` | string | `"STEPPER"`, `"SERVO"`, `"BRUSHED_DC"`, `"BRUSHLESS_DC"`, `"CANOPEN_SERVO"`, `"VIRTUAL"` | `"STEPPER"` | Motor type |
| `default_mode` | string | `"POSITION"`, `"VELOCITY"`, `"TORQUE"`, `"TRAJECTORY"`, `"OPEN_LOOP"` | `"POSITION"` | Default control mode |
| `max_velocity` | float | 0.01 to 100.0 | `2.0` | Maximum velocity (deg/s) |
| `max_acceleration` | float | 0.01 to 50.0 | `0.5` | Maximum acceleration (deg/s²) |
| `max_torque` | float | 0.1 to 500.0 | `100.0` | Maximum torque (Nm or %) |
| `encoder_counts_per_degree` | float | 1.0 to 1e9 | `10000.0` | Encoder counts per degree |
| `gear_ratio` | float | 0.1 to 10000.0 | `1.0` | Gear ratio |
| `enable_current_limit` | boolean | `true`, `false` | `true` | Whether to enable current limiter |
| `current_limit` | float | 0.1 to 100.0 | `5.0` | Current limit (A) |
| `enable_temperature_protection` | boolean | `true`, `false` | `true` | Whether to enable temperature protection |
| `max_temperature` | float | 20.0 to 120.0 | `80.0` | Maximum motor temperature (°C) |

#### 8.3.3 `encoder_config` — Encoder Configuration

| Parameter | Type | Allowed Values | Default Value | Description |
|---|---|---|---|---|
| `type` | string | `"ABSOLUTE"`, `"INCREMENTAL"`, `"RESOLVER"`, `"HALL_SENSOR"` | `"ABSOLUTE"` | Encoder type |
| `interface` | string | `"SSI"`, `"QUADRATURE"`, `"BISS"`, `"ENDAT"`, `"CANOPEN"` | `"SSI"` | Encoder interface |
| `resolution` | integer | 1–2³² | `16384` | Resolution (counts/revolution) |
| `counts_per_degree` | float | 1.0 to 1e9 | `10000.0` | Counts per angular degree |
| `use_index_pulse` | boolean | `true`, `false` | `true` | Whether to use index pulse (homing) |
| `use_direction_signal` | boolean | `true`, `false` | `true` | Whether to use direction signal |
| `max_velocity` | float | 1.0 to 10000.0 | `1000.0` | Maximum encoder read velocity (deg/s) |
| `enable_error_detection` | boolean | `true`, `false` | `true` | Whether to enable encoder error detection |
| `error_threshold` | integer | 1–1000 | `10` | Error threshold before alarm |
| `calibration_offset` | float | -360.0 to 360.0 | `0.0` | Calibration offset (deg) |

#### 8.3.4 `safety_limits` — Axis Safety Limits

| Parameter | Type | Range | Default Value | Description |
|---|---|---|---|---|
| `min_position` | float | -360.0 to 360.0 | `-270.0` | Minimum position (deg) |
| `max_position` | float | -360.0 to 360.0 | `270.0` | Maximum position (deg) |
| `max_velocity` | float | 0.1 to 100.0 | `5.0` | Maximum velocity (deg/s) |
| `max_acceleration` | float | 0.01 to 50.0 | `2.0` | Maximum acceleration (deg/s²) |
| `max_current` | float | 0.1 to 100.0 | `10.0` | Maximum current (A) |
| `max_temperature` | float | 20.0 to 120.0 | `80.0` | Maximum temperature (°C) |

### 8.4 `hal.gamepad` — Gamepad Configuration

| Parameter | Type | Range | Default Value | Description |
|---|---|---|---|---|
| `device_path` | string | /dev/input/* path | `""` | Input device path |
| `deadzone` | float | 0.0 to 1.0 | `0.15` | Analog axis deadzone |
| `sensitivity` | float | 0.1 to 10.0 | `1.0` | Axis sensitivity |
| `max_velocity_deg_s` | float | 0.1 to 20.0 | `5.0` | Maximum manual control velocity |
| `invert_axis1` | boolean | `true`, `false` | `false` | Axis 1 inversion |
| `invert_axis2` | boolean | `true`, `false` | `false` | Axis 2 inversion |
| `speed_presets` | float[] | list of speeds | `[]` | Speed presets (e.g. [0.5, 1.0, 2.0, 5.0]) |
| `update_rate_hz` | float | 10.0 to 200.0 | `50.0` | Gamepad read frequency (Hz) |

#### 8.4.1 `button_mapping` — Button Mapping

Map of `"button_number": "action"`.

Allowed actions:
- `"home"` — return to home position
- `"stop"` — stop motion
- `"park"` — park the mount
- `"emergency_stop"` — emergency stop
- `"speed_down"` — decrease speed
- `"speed_up"` — increase speed
- `"manual_toggle"` — toggle manual/automatic mode
- `"none"` — no action

#### 8.4.2 `axis_mapping` — Analog Axis Mapping

Map of `"axis_number": "function"`.

Allowed functions:
- `"lx"`, `"ly"` — left stick (X/Y)
- `"rx"`, `"ry"` — right stick (X/Y)
- `"trigger_l"`, `"trigger_r"` — triggers
- `"pov_x"`, `"pov_y"` — POV hat switch

### 8.5 `hal.pid_params` — PID Controller Parameters

| Parameter | Type | Range | Default Value | Description |
|---|---|---|---|---|
| `kp` | float | 0.0 to 1000.0 | `1.5` | Proportional gain |
| `ki` | float | 0.0 to 100.0 | `0.2` | Integral gain |
| `kd` | float | 0.0 to 100.0 | `0.05` | Derivative gain |
| `integral_limit` | float | 0.0 to 1e6 | `1000.0` | Integral term limit (anti-windup) |
| `output_limit` | float | 0.0 to 1e6 | `100.0` | PID output limit |
| `anti_windup_gain` | float | 0.0 to 1.0 | `0.1` | Anti-windup gain (back-calculation) |
| `enable_anti_windup` | boolean | `true`, `false` | `true` | Whether to enable anti-windup |

Implementation in [`include/hal/hal_config.h:152`](include/hal/hal_config.h:152).

### 8.6 `hal.safety` — Global Safety

| Parameter | Type | Range | Default Value | Description |
|---|---|---|---|---|
| `enable_limits` | boolean | `true`, `false` | `true` | Whether to enable safety limits |
| `enable_emergency_stop` | boolean | `true`, `false` | `true` | Whether to enable emergency stop |
| `emergency_stop_timeout_ms` | integer | 10–10000 | `100` | E-stop response time (ms) |
| `enable_temperature_monitoring` | boolean | `true`, `false` | `true` | Temperature monitoring |
| `enable_current_monitoring` | boolean | `true`, `false` | `true` | Current monitoring |
| `enable_voltage_monitoring` | boolean | `true`, `false` | `true` | Voltage monitoring |
| `min_voltage` | float | 0.0 to 50.0 | `20.0` | Minimum supply voltage (V) |
| `max_voltage` | float | 0.0 to 50.0 | `30.0` | Maximum supply voltage (V) |
| `monitoring_rate` | integer | 1–1000 | `10` | Monitoring frequency (Hz) |

---

## 9. `derotator` and `field_rotation` Sections

### 9.1 `derotator` — Field Derotator

| Parameter | Type | Range | Default Value | Description |
|---|---|---|---|---|
| `type` | string | `"CANOPEN"`, `"STEPPER"`, `"SERVO"`, `"NONE"` | `"CANOPEN"` | Derotator type |
| `enabled` | boolean | `true`, `false` | `false` | Whether enabled |
| `connection_string` | string | any | `""` | Connection string |
| `gear_ratio` | float | 0.1 to 1000.0 | `10.0` | Derotator gear ratio |
| `max_speed` | float | 0.1 to 50.0 | `5.0` | Maximum speed (deg/s) |
| `max_acceleration` | float | 0.01 to 20.0 | `2.0` | Maximum acceleration (deg/s²) |
| `backlash` | float | 0.0 to 50.0 | `2.0` | Derotator backlash (arcsec) |
| `absolute_encoder` | boolean | `true`, `false` | `true` | Absolute encoder |
| `encoder_resolution` | integer | 1–2³² | `16384` | Encoder resolution |
| `homing_offset` | float | -360.0 to 360.0 | `0.0` | Home position offset (deg) |
| `calibration_table` | array | list of corrections | `[]` | Calibration table |

### 9.2 `field_rotation` — Computed Field Rotation

| Parameter | Type | Range | Default Value | Description |
|---|---|---|---|---|
| `enabled` | boolean | `true`, `false` | `false` | Whether rotation correction is enabled |
| `latitude` | float | -90.0 to 90.0 | `52.0` | Geographic latitude |
| `altitude` | float | 0.0 to 90.0 | `0.0` | Target altitude |
| `azimuth` | float | 0.0 to 360.0 | `0.0` | Target azimuth |
| `computed_rate` | float | any | `0.0` | Computed rotation rate (deg/s) |
| `applied_correction` | float | any | `0.0` | Applied correction (deg) |
| `temperature` | float | -50.0 to 60.0 | `15.0` | Temperature (°C) |
| `flexure_correction` | float | any | `0.0` | Flexure correction (deg) |

---

## 10. Example Configurations

### 10.1 Two CANopen Servo Drives at Addresses 5 and 6 (Equatorial Mount)

File: [`config/dual_servo_config.json`](config/dual_servo_config.json)

```json
{
  "hal": {
    "type": "canopen",
    "axes": [
      {
        "id": 0,
        "name": "HA_Axis",
        "can_node_id": 5,
        "motor_config": { "type": "CANOPEN_SERVO", ... },
        "encoder_config": { "interface": "CANOPEN", ... },
        "safety_limits": { ... }
      },
      {
        "id": 1,
        "name": "Dec_Axis",
        "can_node_id": 6,
        "motor_config": { "type": "CANOPEN_SERVO", ... },
        "encoder_config": { "interface": "CANOPEN", ... },
        "safety_limits": { ... }
      }
    ]
  }
}
```

### 10.2 Default Configuration (3 Axes, Addresses 1, 2, 3)

File: [`config/default.json`](config/default.json)

In the default configuration the `hal.axes[]` section is absent — the system uses default values,
where `can_node_id = 0` (auto) resulting in mapping: axis 0 → node 1, axis 1 → node 2, axis 2 → node 3.

---

## 11. Configuration Loading Flow Diagram

```
argv[1] (file path)
    │
    ├── provided → Configuration::loadFromFile(argv[1])
    │               │
    │               ├── success → done
    │               └── error  → fallback to default.json
    │
    └── missing → Configuration::loadFromFile("config/default.json")
                    │
                    ├── success → done
                    └── error  → Configuration::initializeDefaults()
                                  (hardcoded default values)
```

After loading:
1. [`ConfigMonitor`](src/config/config_monitor.cpp) monitors the file for changes
2. [`ConfigNotifier`](src/config/config_monitor.cpp:205) notifies subscribers of changes
3. [`ConfigManager`](src/config/config_monitor.cpp:266) manages the configuration lifecycle

---

## 12. Configuration Validation

Validation is performed in [`Configuration::Impl::validate()`](src/config/configuration.cpp:61).
The following aspects are checked among others:

- Correctness of log level
- gRPC port range (1–65535)
- CANopen Node ID range (1–127)
- Geographic coordinate validity
- HAL configuration consistency

In case of critical errors, the program logs a warning and applies default values.
