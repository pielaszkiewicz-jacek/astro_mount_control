# Astronomical Mount Controller — Web Interface

## Overview

The Web Interface provides browser-based remote control of the astronomical mount controller via an HTTP/JSON proxy that bridges to the gRPC backend services. It is a single-page application (SPA) composed of vanilla JavaScript modules with no framework dependencies.

### Architecture

```
┌──────────────┐    HTTP/JSON     ┌────────────────────┐    gRPC     ┌─────────────────────┐
│   Browser    │ ◄──────────────► │  Express Proxy     │ ◄────────► │  Mount Controller   │
│  (SPA)       │   port 8080      │  web/proxy/server.js│  port 50051│  (C++ gRPC server)  │
│              │                  │                     │            │                     │
│  index.html  │                  │  Static files:      │            │  Slew, Track,       │
│  app.js      │                  │  ./public/          │            │  Park, Calibrate... │
│  api.js      │                  │                     │            └─────────────────────┘
│  components/ │                  │  CORS                │
│              │                  │  .env config         │            ┌─────────────────────┐
└──────────────┘                  │                     │    gRPC    │  Object Database    │
                                  │                     │ ◄────────► │  (C++ gRPC server)  │
                                  │                     │  port 50052│                     │
                                  └─────────────────────┘            │  Catalog CRUD,      │
                                                                     │  Search, Import     │
                                                                     └─────────────────────┘
```

### Directory Structure

```
web/
├── proxy/
│   ├── .env.example        # Environment configuration template
│   ├── package.json        # Node.js dependencies
│   ├── package-lock.json
│   └── server.js           # Express proxy server (HTTP/JSON → gRPC)
├── public/
│   ├── index.html          # SPA entry point (all DOM structure)
│   ├── css/
│   │   └── style.css       # Complete application styles
│   └── js/
│       ├── app.js          # Main application module (tabs, polling, themes)
│       ├── api.js          # HTTP/JSON API client
│       ├── utils.js        # Shared utilities (formatting, DOM helpers)
│       ├── logger.js       # Client-side logging
│       └── components/
│           ├── mountControl.js   # Mount control tab (slew, axis pad, state)
│           ├── mountStatus.js    # Mount status tab (real-time display)
│           ├── database.js       # Object database tab (CRUD, search, import)
│           ├── calibration.js    # Calibration tab (Bootstrap + TPOINT)
│           ├── tracking.js       # Ephemeris tracking tab (moving objects)
│           └── settings.js       # Settings tab (18 config groups, import/export)
└── README.md               # This file
```

---

## Quick Start

### Prerequisites

- **Node.js** ≥ 18.0.0
- **Mount Controller** gRPC server running (default `localhost:50051`)
- **Object Database** gRPC server running (default `localhost:50052`)

### Installation

```bash
# Navigate to the proxy directory
cd web/proxy

# Install dependencies
npm install

# Configure environment (edit as needed)
cp .env.example .env
```

### Configuration (`.env`)

| Variable | Default | Description |
|----------|---------|-------------|
| `GRPC_HOST` | `localhost` | Mount Controller gRPC host |
| `GRPC_PORT` | `50051` | Mount Controller gRPC port |
| `DB_GRPC_HOST` | `localhost` | Object Database gRPC host |
| `DB_GRPC_PORT` | `50052` | Object Database gRPC port |
| `PROXY_HOST` | `0.0.0.0` | Proxy server bind address |
| `PROXY_PORT` | `8080` | Proxy server HTTP port |
| `ENABLE_SSL` | `false` | Enable TLS for gRPC connections |
| `SSL_CERT_PATH` | `` | Path to SSL certificate file |
| `SSL_KEY_PATH` | `` | Path to SSL key file |
| `CORS_ORIGINS` | `http://localhost:8080` | Comma-separated allowed CORS origins |
| `LOG_LEVEL` | `dev` | Morgan logger format (`dev`, `combined`, `tiny`) |

### Running

```bash
# Start the proxy server
cd web/proxy
npm start

# Or for development with auto-restart on changes:
npm run dev
```

The proxy serves the SPA at `http://localhost:8080` and provides the HTTP/JSON API at the same origin.

---

## User Interface — Tab Reference

### Status Tab

Real-time mount status dashboard with four information cards:

- **Mount Status** — Current state (IDLE/SLEWING/TRACKING/PARKED/ERROR), encoder status, guider status, pier side, meridian flip status, time to meridian
- **Position** — Axis 1/2 positions (degrees), tracking rates in RA/Dec (arcsec/s)
- **Environment** — Temperature (°C), pressure (hPa), humidity (%)
- **Tracking** — Tracked object name, RA/Dec (hms/dms format), tracking error (arcsec), tracking performance (%), pointing error (arcsec)

When an object name is present in the tracking data, the status component automatically performs a database lookup to display additional object details (type, catalog, magnitude, constellation, spectral type, distance).

**Badges:**
- Connection indicator (green = connected, red = disconnected)
- Database connection indicator (green = connected, red = offline)

### Control Tab

Mount control panel with four functional sections:

#### Slew to Coordinates
- Enter Right Ascension (0–24 hours) and Declination (–90° to +90°) and click **Slew**
- Sends `POST /slew` with `{ ra, dec }`

#### Quick Actions
- **Stop** — Stop all mount movement
- **Park** — Park mount at configured park position
- **Unpark** — Restore mount to operational state
- **Clear Errors** — Clear error flags

#### Axis Control Pad
A four-directional control pad with two operating modes:

- **Velocity Mode** (default): Press and hold a direction button to move continuously. Release to stop. The speed slider (0.1–5.0 °/s) controls movement velocity.
  - When the mount is **calibrated** (astronomical mode): Nudges use coordinate offsets — RA+/RA– for axis 1, Dec+/Dec– for axis 2 on equatorial mounts, or Alt+/Az+ for alt-az mounts
  - When **uncalibrated** (low-level mode): Direct velocity control of the axes via `POST /axis/move`

- **Step Mode**: Single click moves by a configured step size (0.01–90°). In calibrated mode, uses coordinate offset nudges. In uncalibrated mode, moves at current speed for a duration calculated from step size / speed.

- **EMERGENCY STOP** — Immediately halts all axis movement

#### Mount State
- **Save State** — Save current mount controller state (positions, calibration, orientation) to a file on the server. Enter directory path and filename, or click **Now** to auto-generate `mount_state_YYYY-MM-DD_HHMMSS.json`
- **Load State** — Restore mount state from a previously saved file on the server
- **Upload & Load** — Select a state file from your local computer to upload and load

### Settings Tab

Configuration management with three sections:

#### Server Info
Displays proxy port, poll interval, and version information.

#### Connection Addresses
- Configure gRPC host/port for both the Mount Controller and Object Database
- Click **Save & Reconnect** to apply changes

#### Configuration Groups
The configuration is organized into 18 collapsible groups, each with **Save** and **Restore Defaults** buttons:

| # | Group | Key Fields |
|---|-------|------------|
| 1 | Logging | level, directory, rotation_days, max_file_size_mb, console_output |
| 2 | Network | grpc_address, grpc_port, max_connections, enable_ssl, ssl_cert_path, ssl_key_path |
| 3 | CANopen | interface, node_id, baud_rate (100k/250k/500k/1M), enable_sync, sync_interval_ms |
| 4 | Mount Location | latitude, longitude, altitude, mount_height |
| 5 | Mount General | mount_type (EQUATORIAL/ALT_AZ/CASUAL/UNKNOWN), max_slew_rate, max_tracking_rate, slew_acceleration, tracking_acceleration |
| 6 | Mount Environmental | default_temperature, default_pressure, default_humidity |
| 7 | Mount Encoders | use_encoders, absolute_encoders, encoder_resolution_config |
| 8 | Mount Tolerances | position_tolerance, rate_tolerance |
| 9 | Meridian Flip | enable, delay_minutes, hysteresis_degrees, timeout_seconds |
| 10 | Soft Limits | enabled, axis1_min/max, axis2_min/max, warning_degrees, deceleration_degrees, tracking_rate_factor |
| 11 | Park Position | axis1, axis2 |
| 12 | Atmospheric Correction | enable_refraction_correction |
| 13 | Mount Orientation | quaternion (qx, qy, qz, qw) |
| 14 | HA Axis Physical Parameters | Motor, Encoder, Gear, Cyclic Error, Backlash, Stiffness & Thermal (6 sub-groups) |
| 15 | Dec Axis Physical Parameters | Motor, Encoder, Gear, Cyclic Error, Backlash, Stiffness & Thermal (6 sub-groups) |
| 16 | Telescope | focal_length, aperture, tube_length, camera_model, pixel_size, sensor_width, sensor_height |
| 17 | Guider | enabled, connection_string, max_correction, aggression, exposure_time_ms, binning |
| 18 | Kalman Filter | process_noise, measurement_noise, adaptive_q, adaptive_r, innovation_threshold, max_iterations |
| 19 | TPOINT Calibration | enabled_terms bitmask, min_measurements, max_residual, auto_calibrate |
| 20 | Derotator | type, enabled, connection_string, gear_ratio, max_speed, max_acceleration, backlash, absolute_encoder, encoder_resolution, homing_offset |
| 21 | Field Rotation | enabled, latitude, altitude, azimuth, computed_rate, applied_correction, temperature, flexure_correction |
| 22 | HAL | interface_type, can_interface, can_node_id, can_baud_rate, heartbeat_interval_ms, pdo_mapping_mode |
| 23 | HAL - Gamepad | device_path, deadzone, sensitivity, poll_interval_ms |

**Global Actions:**
- **Export Config** — Download all configuration as a JSON file (`mount-config-YYYYMMDDTHHMMSS.json`)
- **Import Config** — Upload a configuration JSON file to apply settings
- **Reset All to Defaults** — Reset entire configuration (hidden by default, shown after clicking in the card)

### Calibration Tab

Two-stage calibration workflow with reference object search from the database.

#### Bootstrap Calibration (Coarse Alignment)
Used for initial alignment. States: `NOT_CALIBRATED` → `MEASUREMENTS_COLLECTING` → `CALIBRATING` → `CALIBRATED` / `ERROR`

1. Search for a reference star/object in the database
2. Select it to populate expected coordinates
3. Click **Slew and Measure** to slew and automatically add a measurement (after 2s delay)
4. Or click **Add Measurement** to use the current mount position
5. Repeat for ≥3 measurements distributed across the sky
6. Click **Run Bootstrap Calibration** to compute the alignment
7. **Clear Measurements** to discard all bootstrap data

#### TPOINT Calibration (Precise Pointing Model)
Fine-tunes mount geometry with 21-parameter model. Requires the mount to be in a calibrated state.

1. Search for a reference object in the database
2. Select and slew to it, then add a measurement
3. Repeat for ≥10 measurements distributed across the celestial sphere
4. Click **Run TPOINT Calibration** to compute the pointing model
5. Results display: RMS residual, max residual, chi-squared, number of measurements, last update timestamp

#### Quick Help
Toggle the calibration help card for detailed instructions.

### Database Tab

Full astronomical object database management with SQLite backend.

#### Browse & Search
- **Browse**: Paginated list of all objects (default 20 per page)
- **Search**: Filter by name, type, constellation, catalogs, magnitude range, favorites only
- **Sort**: Sort by name, RA, dec, magnitude, type, catalog
- **Object Detail**: Click any object to view full details — basic info, coordinates (J2000 RA/Dec, proper motion, parallax, distance), magnitudes (V/B/J/H/K), physical properties (spectral type, luminosity, temperature, mass, radius), notes

#### Create & Edit
- **Create Object**: Full form with all fields matching the protobuf schema
- **Edit Object**: Pre-fills edit form from existing object data
- **Delete Object**: Confirmation dialog before deletion

#### Favorites
- Toggle favorite status with the star button in the list or detail view
- Filter to show only favorites

#### Slew to Object
From the detail view, click **Slew to Object** to populate the Control tab's RA/Dec fields and switch tabs.

#### Database Statistics
Collapsible panel showing: total objects, favorites, user-added objects, average magnitude, breakdown by type (top 8) and catalog (top 5), last update timestamp.

#### Import Catalogs
Three import methods:

- **File Import**: Select a local file, choose format (CSV/JSON) and catalog name, optionally overwrite existing
- **URL Import**: Fetch catalog data from a URL
- **Preset Import**: One-click import of built-in catalogs:
  - `messier` — 110 Messier objects
  - `ngc` — 7,840 NGC objects (from OpenNGC)
  - `ic` — 5,386 IC objects (from OpenNGC)
  - `caldwell` — 109 Caldwell objects
  - `hyg` — ~120,000 HIPPARCOS/Tycho stars (HYG Database v3)
  - `bright_stars` — Bright stars from HYG (magnitude < 6.5)
  - `sao` — SAO catalog stars

Import results display: imported, skipped, updated counts, import time, and any errors (collapsible).

### Tracking Tab

Ephemeris tracking for moving objects (satellites, comets, asteroids, planets).

#### Tracking States
`IDLE` → `SLEWING_TO_START` → `WAITING_AT_START` → `TRACKING` → `PREDICTING` → `ENDED` → `ERROR`

Each state is displayed with a color-coded badge.

#### Start Tracking
Three methods:

- **By Object ID**: Enter an object identifier string and click **Start Tracking**
- **With Data**: Paste JSON ephemeris data and start tracking immediately
- **Upload Ephemeris**: Upload ephemeris JSON data to the server

#### Stop Tracking & Clear Cache
- **Stop Tracking** — End the current tracking session
- **Clear Cache** — Clear cached ephemeris data on the server

#### Status Card
Displays: Object name/ID, current RA/Dec, target RA/Dec, position error (arcsec), RA/Dec rates (arcsec/s), time remaining (h/m/s), earth rotation correction status, error/warning messages.

#### Metrics Card
Displays: Object name, type, total track time, average/max position error, average rate error, prediction count, prediction accuracy, earth rotation applied.

---

## HTTP/JSON API Reference

All endpoints are served by the proxy on `http://<host>:<port>` (default `http://localhost:8080`).

### Status

#### `GET /status`
Get the current mount controller state.

**Response:**
```json
{
  "status": "IDLE|SLEWING|TRACKING|PARKED|ERROR",
  "position": { "axis1": 0.0, "axis2": 0.0 },
  "encoders_enabled": true,
  "guider_active": false,
  "pier_side": "EAST|WEST",
  "meridian_flipped": false,
  "time_to_meridian": 2.5,
  "tracking_rate_ra": 15.041,
  "tracking_rate_dec": 0.0,
  "tracked_object": {
    "name": "M31",
    "ra": 0.842,
    "dec": 41.269,
    "tracking_error_ra": 0.5,
    "tracking_error_dec": 0.3
  },
  "tracking_performance": 98.5,
  "pointing_error": 1.2,
  "temperature": 15.0,
  "pressure": 1013.25,
  "humidity": 0.5,
  "error_message": ""
}
```

#### `GET /db/health`
Check object database connectivity.

**Response:** `{ "connected": true }` or 503 error with `{ "error": "message" }`

### Mount Control

#### `POST /slew`
Slew to equatorial coordinates.

**Request:** `{ "ra": 18.6156, "dec": 38.7836 }`

#### `POST /axis/slew-horizontal`
Slew to horizontal coordinates.

**Request:** `{ "altitude": 45.0, "azimuth": 180.0 }`

#### `POST /stop`
Stop all mount movement.

#### `POST /park`
Park the mount.

#### `POST /unpark`
Unpark the mount.

#### `POST /errors/clear`
Clear error flags.

### Axis Control

#### `POST /axis/move`
Move an axis at a specified velocity.

**Request:** `{ "axis_id": 0, "velocity": 1.5 }`

#### `POST /axis/stop`
Stop a specific axis.

**Request:** `{ "axis_id": 0 }`

#### `POST /axis/emergency-stop`
Immediate emergency stop of all axes.

**Request:** `{ "axis_id": 0 }` or empty body

### State Management

#### `POST /state/save`
Save mount state to a server-side file.

**Request:** `{ "file_path": "data/mount_state.json" }`

**Response:** `{ "success": true, "file_path": "...", "file_size": 1234 }`

#### `POST /state/load`
Load mount state from a server-side file.

**Request:** `{ "file_path": "data/mount_state.json" }`

#### `POST /state/upload-and-load`
Upload a state file from the browser and load it immediately.

**Request:** `{ "file_content": "{...json...}", "file_name": "mount_state.json" }`

The file is saved to `data/uploads/` and loaded via gRPC, then cleaned up.

### Configuration

#### `GET /config`
Get full configuration.

**Response:** Full configuration object matching `config/default.json` structure.

#### `PUT /config`
Update configuration.

**Request:** Partial or full configuration object.

#### `POST /config/reset`
Reset all configuration to defaults.

#### `POST /config/reset-group`
Reset a single configuration group.

**Request:** `{ "group": "logging" }`

#### `GET /config/addresses`
Get current gRPC connection addresses.

**Response:**
```json
{
  "addresses": {
    "controller": { "host": "localhost", "port": 50051 },
    "database": { "host": "localhost", "port": 50052 }
  }
}
```

#### `POST /config/addresses`
Set and reconnect to new gRPC addresses.

**Request:**
```json
{
  "controller": { "host": "192.168.1.100", "port": 50051 },
  "database": { "host": "192.168.1.100", "port": 50052 }
}
```

### Calibration — Bootstrap

#### `GET /calibration/bootstrap`
Get bootstrap calibration status.

**Response:**
```json
{
  "state": "MEASUREMENTS_COLLECTING",
  "measurements_count": 3,
  "alignment_error": null,
  "message": ""
}
```

#### `POST /calibration/bootstrap/measurements`
Add a bootstrap measurement.

**Request:**
```json
{
  "object_id": "HIP12345",
  "object_name": "Vega",
  "expected": { "ra": 18.6156, "dec": 38.7836 }
}
```

#### `POST /calibration/bootstrap/run`
Run bootstrap calibration.

**Response:**
```json
{
  "alignment_error": 0.5,
  "measurements_used": 5,
  "calibration_matrix": "..."
}
```

#### `POST /calibration/bootstrap/clear`
Clear all bootstrap measurements.

### Calibration — TPOINT

#### `GET /calibration/tpoint`
Get TPOINT calibration parameters.

**Response:**
```json
{
  "calibrated": true,
  "measurements_count": 15,
  "residual_rms": 1.2,
  "max_residual": 3.5,
  "chi_squared": 0.8,
  "last_update": "2026-05-31T12:00:00Z"
}
```

#### `POST /calibration/tpoint/measurements`
Add a TPOINT measurement.

**Request:** Same format as bootstrap measurement.

#### `POST /calibration/tpoint/run`
Run TPOINT calibration.

**Response:**
```json
{
  "residual_rms": 1.2,
  "measurements_used": 15,
  "parameters": { "IA": 0.5, "IE": -0.3, ... }
}
```

#### `POST /calibration/tpoint/clear`
Clear all TPOINT measurements.

### Tracking (Ephemeris)

#### `GET /tracking/status`
Get ephemeris tracking status.

**Response:**
```json
{
  "state": "TRACKING|IDLE|ERROR",
  "object_id": "SAT123",
  "current_ra": 10.5,
  "current_dec": 20.0,
  "target_ra": 10.52,
  "target_dec": 19.98,
  "position_error": 0.8,
  "ra_rate": 0.05,
  "dec_rate": -0.02,
  "time_remaining": 3600.0,
  "earth_rotation_corrected": true,
  "error_message": ""
}
```

#### `GET /tracking/metrics`
Get tracking performance metrics.

**Response:**
```json
{
  "object": "SAT123",
  "type": "SATELLITE",
  "total_track_time": 7200.0,
  "avg_position_error": 0.5,
  "max_position_error": 2.1,
  "avg_rate_error": 0.01,
  "predictions_count": 144,
  "prediction_accuracy": 98.2,
  "earth_rotation_applied": true
}
```

#### `POST /tracking/start`
Start tracking by object ID.

**Request:** `{ "object_id": "SAT123" }`

#### `POST /tracking/start-with-data`
Start tracking with inline ephemeris data.

**Request:** Full ephemeris track request object with waypoints.

#### `POST /tracking/upload`
Upload ephemeris data to the server.

**Request:** Raw ephemeris JSON data.

#### `POST /tracking/stop`
Stop the current tracking session.

#### `POST /tracking/clear-cache`
Clear cached ephemeris data on the server.

### Object Database — CRUD

#### `GET /db/objects`
List objects with pagination.

**Query params:** `page`, `page_size` (default 20)

**Response:**
```json
{
  "objects": [...],
  "total_count": 1000,
  "page": 1,
  "page_size": 20,
  "total_pages": 50
}
```

#### `GET /db/objects/search`
Search and filter objects.

**Query params:** `query`, `type`, `magnitude_min`, `magnitude_max`, `sort_by`, `sort_desc`, `favorites_only`, `visible_only`, `catalogs`, `constellation`, `page`, `page_size`

#### `GET /db/objects/:id`
Get a single object by ID.

#### `POST /db/objects`
Create a new astronomical object.

**Request:** Full object data matching the protobuf `AstronomicalObject` message.

#### `PUT /db/objects/:id`
Update an existing object.

**Request:** Full or partial object data.

#### `DELETE /db/objects/:id`
Delete an object.

### Database — Favorites

#### `GET /db/favorites`
Get all favorited objects.

#### `POST /db/favorites`
Add an object to favorites.

**Request:** `{ "object_id": "some-id" }`

#### `DELETE /db/favorites/:objectId`
Remove an object from favorites.

### Database — Categories

#### `GET /db/categories`
List all categories.

#### `POST /db/categories`
Create a new category.

**Request:** `{ "name": "My Catalog" }`

### Database — Utilities

#### `GET /db/stats`
Get database statistics (totals, breakdowns, last update).

#### `GET /db/tonight-best`
Get best objects for the current night.

**Query params:** `latitude`, `longitude`, `limit`, `min_altitude`, `max_magnitude`

### Database — Import/Export

#### `POST /db/import`
Import catalog data.

**Request:**
```json
{
  "format": "csv|json",
  "data": "...raw data string...",
  "options": {
    "catalog_name": "MyCatalog",
    "overwrite": false
  }
}
```

#### `POST /db/import/url`
Import catalog from a URL.

**Request:**
```json
{
  "url": "https://example.com/catalog.csv",
  "format": "csv",
  "options": {
    "catalog_name": "RemoteCatalog",
    "overwrite": false
  }
}
```

#### `GET /db/import/presets`
Get list of available preset catalogs.

**Response:**
```json
[
  { "name": "messier", "label": "Messier Catalog", "size": 110, "type": "OpenNGC" },
  { "name": "ngc", "label": "NGC Catalog", "size": 7840, "type": "OpenNGC" },
  { "name": "ic", "label": "IC Catalog", "size": 5386, "type": "OpenNGC" },
  { "name": "caldwell", "label": "Caldwell Catalog", "size": 109, "type": "HYG" },
  { "name": "hyg", "label": "HYG Database v3", "size": 120000, "type": "HYG" },
  { "name": "bright_stars", "label": "Bright Stars", "size": 5000, "type": "HYG" },
  { "name": "sao", "label": "SAO Catalog", "size": 10000, "type": "HYG" }
]
```

#### `POST /db/import/preset/:name`
Import a built-in catalog preset.

**Request:** `{ "options": { "overwrite": false } }`

---

## Theme & Layout

### Night-Vision Red Theme
Toggle the night-vision red theme via the moon icon button in the header. The preference is persisted in `localStorage` under the key `red-theme`. When active, all UI elements render in low-intensity red tones suitable for dark-adapted vision.

### Mobile Mode
Toggle mobile layout via the phone icon button. When active, the UI switches to a single-column layout for small screens. The preference is persisted in `localStorage` under the key `mobile-mode`.

### Toast Notifications
The app displays non-blocking toast notifications for success, error, and information messages. Toasts auto-dismiss after a configurable duration (default 3 seconds).

### Log Panel
A collapsible log panel at the bottom of the page displays client-side log messages for debugging.

---

## Troubleshooting

| Symptom | Likely Cause | Resolution |
|---------|-------------|------------|
| Connection badge shows red "Disconnected" | Mount Controller gRPC not running | Start `astro-mount-controller` on port 50051 |
| DB badge shows "DB Off" | Object Database gRPC not running | Start `astro-mount-db` on port 50052 |
| Proxy starts but immediately exits | Port 8080 already in use | Change `PROXY_PORT` in `.env` |
| CORS error in browser console | CORS_ORIGINS doesn't match | Set `CORS_ORIGINS=*` or match your origin |
| "Cannot find module" error | Dependencies not installed | Run `npm install` in `web/proxy/` |
| Slew commands return errors | Mount in ERROR or PARKED state | Check status tab, clear errors, unpark |
| Database shows no objects | No catalogs imported | Use Database tab → Import → Preset to import a catalog |
| Calibration fails | Too few measurements | Add more measurements distributed across the sky |
| Tracking shows "ENDED" immediately | No ephemeris data uploaded | Upload ephemeris data first |
| Settings changes not applied | Group not saved | Click **Save** within the specific config group |
| Page not found (404) | Wrong URL | Ensure you're accessing port 8080, not 50051 |

---

## Development

### File Conventions

- All JavaScript modules use the **IIFE (Immediately Invoked Function Expression)** pattern with `'use strict'`
- Modules expose public methods via a returned object
- DOM IDs follow the convention: `btn-*` for buttons, `card-*` for cards, `panel-*` for tab panels
- All event handlers use `addEventListener`, no inline event handlers
- HTML escaping: user-provided text is sanitized via `escapeHtml()` using `textContent` → `innerHTML` pattern

### Adding a New Component

1. Create a new file in `web/public/js/components/`
2. Wrap in an IIFE returning a public API object with at minimum `init()` and optionally `startPolling()`/`stopPolling()`
3. Add the component's HTML sections to `web/public/index.html`
4. Register the tab in `app.js` `initTabs()` method
5. Add the `<script>` tag to `index.html`
6. Add any new API methods to `api.js`

### Proxy Server

The proxy (`web/proxy/server.js`) uses the Express framework and dynamically loads gRPC service definitions from the project's `proto/` directory. It handles:

- Request validation and error transformation
- gRPC ↔ JSON type conversion (Timestamps, enums, nested messages)
- Catalog import data enrichment and filtering
- File upload and temporary storage for mount state
- CORS headers for cross-origin requests
- Static file serving with no-cache headers for the SPA

---

## See Also

- [System Architecture](../docs/en/architecture.md) — Full system architecture documentation
- [gRPC API Reference](../docs/en/api.md) — Complete gRPC API documentation
- [Installation Guide](../docs/en/installation.md) — Building and configuring the mount controller
- [Developer Onboarding](../docs/en/developer_onboarding.md) — Guide for new developers
