# External Services Configuration Guide

## Overview

The mount controller supports auxiliary subsystems that are enabled via the `external_services` configuration section:

- **Dome, derotator and focuser** are hosted **in-process** inside `astro_mount_controller` — no separate process is needed. The `address` field is the gRPC listening address exposed to clients.
- **Weather and power** run as stand-alone processes (`astro_weather_server`, `astro_power_server`) and communicate via gRPC.

| Service | Hosting | Default Port | Purpose |
|---------|---------|-------------|---------|
| **Dome** | in-process (`astro_mount_controller`) | 50051 (unified) | Dome shutter control, rotation, auto-sync with mount |
| **Derotator** | in-process (`astro_mount_controller`) | 50051 (unified) | Field derotation compensation |
| **Focuser** | in-process (`astro_mount_controller`) | 50051 (unified) | Focuser control and auto-focus |
| **Weather** | stand-alone (`astro_weather_server`) | 50055 | Weather monitoring, alerts, auto-park |
| **Power** | stand-alone (`astro_power_server`) | 50056 | Battery/power monitoring, low-voltage auto-park |

All integrations are **disabled by default**. Each must be explicitly enabled in the mount controller's configuration file (`config/default.json`) under the `external_services` section.

---

## 1. Mount Controller Integration

Integration is configured in the mount controller's JSON config file under the `external_services` key. The **dome, derotator and focuser** subsystems are served in-process on the **unified gRPC port 50051** (no separate address is needed). For **weather and power** (stand-alone processes) `address` is the endpoint the mount controller connects to:

```json
{
  "external_services": {
    "dome": {
      "enabled": false,
      "update_interval_ms": 1000
    },
    "derotator": {
      "enabled": false,
      "update_interval_ms": 1000
    },
    "focuser": {
      "enabled": false,
      "poll_interval_ms": 5000
    },
    "weather": {
      "enabled": false,
      "address": "127.0.0.1:50055",
      "poll_interval_ms": 10000
    },
    "power": {
      "enabled": false,
      "address": "127.0.0.1:50056",
      "poll_interval_ms": 10000
    }
  }
}
```

### Parameter Reference

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `enabled` | bool | `false` | Enable this integration (in-process subsystem or external client) |
| `address` | string | `127.0.0.1:500xx` | Dome/derotator/focuser: gRPC listening address exposed to clients. Weather/power: gRPC address of the external process |
| `update_interval_ms` | int | `1000` | How often to feed mount position updates to dome/derotator (in-process) |
| `poll_interval_ms` | int | `10000` | How often to poll status (weather, power, focuser) |

---

## 2. Weather Service (`astro_weather_server`)

### 2.1 Configuration File: `config/weather_config.json`

```json
{
  "rules": {
    "min_temperature_c": -20.0,
    "max_temperature_c": 50.0,
    "max_wind_speed_ms": 15.0,
    "max_wind_gust_ms": 20.0,
    "auto_park_on_rain": true,
    "max_rain_rate_mmh": 5.0,
    "max_cloud_cover_percent": 90.0,
    "max_humidity_percent": 95.0,
    "dew_point_tolerance_c": 3.0,
    "auto_park_enabled": true,
    "check_interval_seconds": 10,
    "debounce_seconds": 30,
    "notify_on_alert": true,
    "close_dome_on_danger": false
  },
  "sensors": {
    "rain": { "enabled": false, "gpio_pin": -1 },
    "wind": { "enabled": false, "gpio_pin": -1, "calibration_factor": 1.0 },
    "cloud": { "enabled": false, "device": "/dev/i2c-1", "address": 90 }
  },
  "gps": { "enabled": false },
  "api_source": {
    "provider": "",
    "api_key": "",
    "latitude": 0.0,
    "longitude": 0.0,
    "station_id": ""
  }
}
```

### 2.2 Rules Reference

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `min_temperature_c` | double | -20.0 | Minimum safe temperature [°C] |
| `max_temperature_c` | double | 50.0 | Maximum safe temperature [°C] |
| `max_wind_speed_ms` | double | 15.0 | Max safe sustained wind [m/s] (~54 km/h) |
| `max_wind_gust_ms` | double | 20.0 | Max safe wind gust [m/s] (~72 km/h) |
| `auto_park_on_rain` | bool | true | Park mount when rain detected |
| `max_rain_rate_mmh` | double | 5.0 | Max rain rate before park [mm/h] |
| `max_cloud_cover_percent` | double | 90.0 | Max cloud cover for observing [%] |
| `max_humidity_percent` | double | 95.0 | Max safe humidity [%] |
| `dew_point_tolerance_c` | double | 3.0 | Required margin above dew point [°C] |
| `auto_park_enabled` | bool | true | Enable automatic parking on alerts |
| `check_interval_seconds` | int | 10 | Sensor read interval [s] |
| `debounce_seconds` | int | 30 | Wait time before triggering alert [s] |
| `notify_on_alert` | bool | true | Send notification on alerts |
| `close_dome_on_danger` | bool | false | Auto-close dome (requires dome service) |

### 2.3 Sensors

| Sensor | Fields | Description |
|--------|--------|-------------|
| **Rain** | `gpio_pin` | GPIO pin for rain sensor (e.g. 17 for BCM) |
| **Wind** | `gpio_pin`, `calibration_factor` | GPIO pin + calibration multiplier |
| **Cloud** | `device`, `address` | I²C device (e.g. `/dev/i2c-1`) and sensor address (e.g. 0x5A) |

### 2.4 API Sources

| Provider | Fields | Description |
|----------|--------|-------------|
| `openweathermap` | `api_key`, `latitude`, `longitude` | OpenWeatherMap One Call API 3.0 |
| `weathergov` | `latitude`, `longitude`, `station_id` | US National Weather Service API |
| `imgw` | `station_id` | Polish Institute of Meteorology (IMGW) |

### 2.5 Running

```bash
# Run with default config
./build/bin/astro_weather_server

# Custom address and config
./build/bin/astro_weather_server \
    --address 0.0.0.0:50055 \
    --config config/weather_config.json

# Enable SSL
./build/bin/astro_weather_server \
    --address 0.0.0.0:50055 \
    --config config/weather_config.json \
    --ssl /path/to/cert.pem /path/to/key.pem

# Systemd
sudo systemctl enable astro-weather-server
sudo systemctl start astro-weather-server
```

### 2.6 Integration Behaviour

When enabled (`weather.enabled: true`), the mount controller:
1. **Creates a `WeatherClient`** that polls the weather service every `poll_interval_ms` milliseconds
2. **On weather caution** — sends a `WARNING` notification via the notification engine
3. **On weather warning** — sends an `ERROR` notification
4. **On weather danger** — sends a `CRITICAL` notification and **auto-parks the mount**
5. **On weather improvement** — sends an `INFO` all-clear notification
6. **On service offline** — sends a `WARNING` notification

---

## 3. Power Service (`astro_power_server`)

### 3.1 Configuration File: `config/power_config.json`

```json
{
  "hal_type": "simulated",
  "low_voltage_threshold": 11.5,
  "poll_interval_s": 10
}
```

### 3.2 Parameter Reference

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `hal_type` | string | `"simulated"` | Power HAL backend: `simulated` (extensible to `i2c`, `serial`) |
| `low_voltage_threshold` | double | 11.5 | Voltage threshold for low-battery alarm [V] |
| `poll_interval_s` | int | 10 | Sensor read interval [s] |

### 3.3 Running

```bash
# Run with default config (simulated power data)
./build/bin/astro_power_server

# Custom address and config
./build/bin/astro_power_server \
    --address 0.0.0.0:50056 \
    --config config/power_config.json

# Systemd
sudo systemctl enable astro-power-server
sudo systemctl start astro-power-server
```

### 3.4 Integration Behaviour

When enabled (`power.enabled: true`), the mount controller:
1. **Creates a `PowerService::Stub`** that polls the power service every `poll_interval_ms` milliseconds
2. **On startup** — logs initial power status (voltage, current, charge percentage, battery status)
3. **On battery power** — logs warning with estimated runtime remaining
4. **On charge < 20%** — **auto-parks the mount** immediately
5. In the main loop (`main.cpp`), power status is checked at the configured interval

### 3.5 Status Fields (gRPC `PowerStatus`)

| Field | Type | Description |
|-------|------|-------------|
| `voltage_v` | double | Current battery voltage [V] |
| `current_a` | double | Current draw [A] |
| `power_w` | double | Power consumption [W] |
| `capacity_ah` | double | Battery capacity [Ah] |
| `charge_percent` | double | State of charge [%] |
| `charging` | bool | Battery is charging |
| `on_battery` | bool | Running on battery (no external power) |
| `temperature_c` | double | Battery temperature [°C] |
| `estimated_runtime_min` | double | Estimated remaining runtime [min] |
| `input_voltage_v` | double | Input/Power-supply voltage [V] |
| `output_voltage_v` | double | Regulated output voltage [V] |
| `outputs[]` | repeated | Per-output status (id, name, enabled, voltage, current, overload) |

---

## 4. Complete Example

### 4.1 Enabling Everything

In `config/default.json`:

```json
{
  "external_services": {
    "dome": {
      "enabled": true,
      "address": "127.0.0.1:50051",
      "update_interval_ms": 1000
    },
    "derotator": {
      "enabled": true,
      "address": "127.0.0.1:50051",
      "update_interval_ms": 1000
    },
    "focuser": {
      "enabled": true,
      "address": "127.0.0.1:50051",
      "poll_interval_ms": 5000
    },
    "weather": {
      "enabled": true,
      "address": "127.0.0.1:50055",
      "poll_interval_ms": 10000
    },
    "power": {
      "enabled": true,
      "address": "127.0.0.1:50056",
      "poll_interval_ms": 10000
    }
  }
}
```

### 4.2 Startup Sequence

```bash
# Start the external (stand-alone) weather and power services
./build/bin/astro_weather_server --config config/weather_config.json &
./build/bin/astro_power_server --config config/power_config.json &

# Start the mount controller.
# Dome, derotator and focuser are hosted IN-PROCESS and served on the
# unified gRPC port 50051 — no separate processes or ports are needed.
./build/bin/astro_mount_controller config/default.json
```

### 4.3 Verification

```bash
# Check services are listening (all on the unified port 50051 + weather/power)
ss -tlnp | grep -E '50051|5005[5-7]'

# Test dome service (in-process, unified port 50051)
grpcurl -plaintext 127.0.0.1:50051 astro_dome.DomeService/GetStatus

# Test derotator service (in-process, unified port 50051)
grpcurl -plaintext 127.0.0.1:50051 astro_derotator.DerotatorService/GetStatus

# Test focuser service (in-process, unified port 50051)
grpcurl -plaintext 127.0.0.1:50051 astro_mount.FocuserService/GetFocuserPosition

# Test weather service
grpcurl -plaintext 127.0.0.1:50055 astro_mount.WeatherService/GetWeatherStatus

# Test power service
grpcurl -plaintext 127.0.0.1:50056 astro_mount.PowerService/GetPowerStatus

# Check mount controller logs for integration messages
journalctl -u astro-mount-controller | grep -i "weather\|power\|dome\|derotator\|focuser"
```

---

## 5. Architecture Diagram

```
┌─────────────────────────────────────────────────────────┐
│  Mount Controller Process (main.cpp)                    │
│                                                         │
│  ┌──────────────┐   ┌────────────────────────────────┐  │
│  │ MountController│   │ Integration Clients            │  │
│  │ State Machine  │   │                                │  │
│  │ Tracking Loop  │   │ WeatherClient ──gRPC──► :50055 │  │
│  │ Parking        │   │ PowerStub     ──gRPC──► :50056 │  │
│  └──────────────┘   │ Dome/Derotator/Focuser          │  │
│                      │  (in-process, served on :50051) │  │
│                      └────────────────────────────────┘  │
└─────────────────────────────────────────────────────────┘
                              │
                    gRPC      │
                              ▼
              ┌──────────────────────────┐
              │  astro_weather_server    │
              │  :50055                  │
              │  ┌────────────────────┐   │
              │  │ WeatherMonitor     │   │
              │  │ ├─ RainSensor      │   │
              │  │ ├─ WindSensor      │   │
              │  │ ├─ CloudSensor     │   │
              │  │ └─ WeatherApiSrc   │   │
              │  └────────────────────┘   │
              └──────────────────────────┘

              ┌──────────────────────────┐
              │  astro_power_server      │
              │  :50056                  │
              │  ┌────────────────────┐   │
              │  │ PowerManager       │   │
              │  │ └─ PowerControl    │   │
              │  │    (simulated/i2c) │   │
              │  └────────────────────┘   │
              └──────────────────────────┘
```

---

## 6. Notification Integration

When weather or power events trigger safety actions, notifications are sent via the `NotificationEngine`:

| Event | Severity | Category | Auto-action |
|-------|----------|----------|-------------|
| Weather caution | `WARNING` | `WEATHER` | Log |
| Weather warning | `ERROR` | `WEATHER` | Log |
| Weather danger | `CRITICAL` | `WEATHER` | **Park mount** |
| Weather improved | `INFO` | `WEATHER` | Log |
| Weather offline | `WARNING` | `WEATHER` | Log |
| Battery low (<20%) | `CRITICAL` | `POWER` | **Park mount** |
| On battery power | `WARNING` | `POWER` | Log |

---

## 7. Source Code References

| Component | Files |
|-----------|-------|
| Integration config struct | [`include/config/configuration.h:220`](include/config/configuration.h:220) — `ExternalIntegrationConfig` |
| Config JSON parsing | [`src/config/configuration.cpp:734`](src/config/configuration.cpp:734) — `getExternalIntegrationConfig()` |
| Weather server | [`weather/src/main.cpp`](weather/src/main.cpp), [`weather/src/weather_server.cpp`](weather/src/weather_server.cpp), [`weather/src/weather_service_impl.cpp`](weather/src/weather_service_impl.cpp) |
| Weather client | [`include/controllers/weather_client.h`](include/controllers/weather_client.h), [`src/controllers/weather_client.cpp`](src/controllers/weather_client.cpp) |
| Weather factory | [`weather/src/weather_factory.cpp`](weather/src/weather_factory.cpp) |
| Power server | [`power/src/main.cpp`](power/src/main.cpp), [`power/src/power_server.cpp`](power/src/power_server.cpp), [`power/src/power_service_impl.cpp`](power/src/power_service_impl.cpp) |
| Mount controller integration | [`src/main.cpp`](src/main.cpp:282) — weather setup, [`src/main.cpp:306`](src/main.cpp:306) — power setup |
| Main loop polling | [`src/main.cpp:420`](src/main.cpp:420) — power status check |
| Build targets | [`CMakeLists.txt:482`](CMakeLists.txt:482) — `astro_weather_server`, [`CMakeLists.txt:499`](CMakeLists.txt:499) — `astro_power_server` |
| Systemd service | [`scripts/astro-weather-server.service`](scripts/astro-weather-server.service), [`scripts/astro-power-server.service`](scripts/astro-power-server.service) |
