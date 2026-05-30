# Web Interface

Responsive web interface for the Astronomical Mount Controller, supporting mobile devices (phones, tablets) and desktop computers (stationary, laptops).

## Architecture

```
web/
├── proxy/                    # HTTP/JSON → gRPC bridge server
│   ├── package.json
│   ├── server.js             # Express server (port 8080)
│   └── .env.example          # Configuration template
├── public/                   # Static frontend SPA
│   ├── index.html            # Entry point with card-based layout
│   ├── css/
│   │   └── style.css         # Responsive styles (mobile-first)
│   └── js/
│       ├── app.js            # Core app: tabs, polling, toasts
│       ├── api.js            # API client layer
│       ├── utils.js          # Shared utilities
│       └── components/       # Card components
│           ├── mountStatus.js
│           ├── mountControl.js
│           ├── settings.js
│           └── database.js   # Object database management
└── README.md
```

### Communication Flow

```
Browser (SPA) ──HTTP/JSON──> Proxy (port 8080) ──gRPC──> Mount Controller (port 50051)
                                         └──gRPC──> Object Database (port 50052)
```

The proxy server (`proxy/server.js`) translates browser-friendly REST/JSON calls into protobuf-based gRPC requests:
- `/api/*` → `astro_mount.MountControllerService` (mount control on port 50051)
- `/api/db/*` → `astro_objects.ObjectDatabaseService` (object database on port 50052)

## Prerequisites

- Node.js 18+ (tested with Node.js 24)
- npm
- Running Astronomical Mount Controller (gRPC server on port 50051)

## Quick Start

### 1. Install dependencies

```bash
cd web/proxy
npm install
```

### 2. Configure (optional)

Copy and edit the environment configuration:

```bash
cp .env.example .env
```

Default values:
| Variable       | Default           | Description                |
|----------------|-------------------|----------------------------|
| `GRPC_HOST`    | `localhost`       | Mount controller host      |
| `GRPC_PORT`    | `50051`           | Mount controller gRPC port |
| `PROXY_PORT`   | `8080`            | Web proxy HTTP port        |
| `PROXY_HOST`   | `0.0.0.0`         | Web proxy bind address     |
| `CORS_ORIGINS` | `http://localhost:8080` | Allowed CORS origins |

### 3. Start the proxy server

```bash
cd web/proxy
npm start
```

### 4. Open the interface

Open your browser to: **http://localhost:8080**

## Interface Overview

The web interface uses a **card-based tab layout** designed for both mobile and desktop screens:

### Tabs (Cards)

| Tab         | Cards                        | Description                              |
|-------------|------------------------------|------------------------------------------|
| **Status**  | Mount Status, Position, Environment, Tracking | Real-time mount state, coordinates, environmental data, tracking performance |
| **Control** | Slew to Coordinates, Axis Control, Quick Actions | Slew to RA/Dec, 4-direction axis pad with speed control, Stop, Park, Unpark, Clear Errors |
| **Settings** | Configuration, Server Info | Location, mount parameters, network config, server info |
| **Database** | Stats, Search, Object List, Object Detail, Add New Object | Browse, search, create, edit, delete astronomical objects; manage favorites; slew to selected object |

### Responsive Layout

- **Mobile** (<600px): Single column, full-width cards
- **Tablet** (600-1024px): Two-column grid
- **Desktop** (1024-1440px): Two-column grid with larger cards
- **Large Desktop** (>1440px): Three-column grid

## API Endpoints

The proxy server exposes the following REST endpoints:

### Mount Controller Endpoints

| Method | Path                     | Description                            | gRPC Mapping            |
|--------|--------------------------|----------------------------------------|-------------------------|
| GET    | `/api/status`            | Mount controller state                 | `GetState()`            |
| POST   | `/api/slew`              | Slew to equatorial coordinates         | `SlewToCoordinates()`   |
| POST   | `/api/stop`              | Stop all movement                      | `Stop()`                |
| POST   | `/api/park`              | Park the mount                         | `Park()`                |
| POST   | `/api/unpark`            | Unpark the mount                       | `Unpark()`              |
| POST   | `/api/clear-errors`      | Clear error state                      | `ClearErrors()`         |
| GET    | `/api/config`            | Get configuration                      | `GetConfiguration()`    |
| GET    | `/api/health`            | Health check                           | `CheckHealth()`         |
| POST   | `/api/axis/move`         | Move axis at velocity (low-level)      | `ControlAxis()`         |
| POST   | `/api/axis/stop`         | Stop axis movement (decelerate)        | `StopAxis()`            |
| POST   | `/api/axis/emergency-stop` | Emergency halt all axes              | `EmergencyStop()`       |
| POST   | `/api/axis/slew-horizontal` | Slew to altitude/azimuth coordinates | `SlewToHorizontal()`    |

### Object Database Endpoints

| Method | Path                          | Description                        | gRPC Mapping               |
|--------|-------------------------------|------------------------------------|----------------------------|
| GET    | `/api/db/stats`               | Database statistics                | `GetDatabaseStats()`       |
| GET    | `/api/db/objects`             | List objects (paginated)           | `ListObjects()`            |
| GET    | `/api/db/objects/search`      | Search objects by query            | `SearchObjects()`          |
| POST   | `/api/db/objects`             | Create a new object                | `CreateObject()`           |
| GET    | `/api/db/objects/:id`         | Get object by ID                   | `GetObject()`              |
| PUT    | `/api/db/objects/:id`         | Update object by ID                | `UpdateObject()`           |
| DELETE | `/api/db/objects/:id`         | Delete object by ID                | `DeleteObject()`           |
| GET    | `/api/db/favorites`           | List favorite objects              | `GetFavorites()`           |
| POST   | `/api/db/favorites`           | Add object to favorites            | `AddToFavorites()`         |
| DELETE | `/api/db/favorites/:id`       | Remove object from favorites       | `RemoveFromFavorites()`    |
| GET    | `/api/db/categories`          | List all categories                | `ListCategories()`         |
| POST   | `/api/db/categories`          | Create a new category              | `CreateCategory()`         |
| GET    | `/api/db/tonight`             | Best objects for tonight           | `GetTonightBestObjects()`  |

### Axis Control API Details

#### `POST /api/axis/move`
Move a specific axis at a given velocity (low-level control for uncalibrated mounts).

**Request body:**
```json
{
  "axis_id": 0,
  "velocity": 1.5
}
```
- `axis_id`: `0` = HA/RA/Azimuth axis, `1` = Dec/Altitude axis
- `velocity`: Signed velocity in degrees/second (positive = forward, negative = backward)

**Response:** `{ "success": true, "message": "Axis 0 moving at 1.5°/s" }`

#### `POST /api/axis/stop`
Stop an axis with smooth deceleration.

**Request body:**
```json
{ "axis_id": 0 }
```

#### `POST /api/axis/emergency-stop`
Halt all axes immediately.

**Request body:** `{ "axis_id": -1 }` (defaults to all axes)

#### `POST /api/axis/slew-horizontal`
Slew to horizontal coordinates (altitude/azimuth) for calibrated alt-az mounts.

**Request body:**
```json
{
  "altitude": 45.0,
  "azimuth": 180.0
}
```
- `altitude`: 0–90 degrees
- `azimuth`: 0–360 degrees (North=0, East=90)

## Axis Control (Control Tab)

The **Axis Control** card provides a 4-directional pad for manual mount movement:

```
        [▲ Up / +Dec/+Alt]
[◄ Left / -RA/-Az]   [► Right / +RA/+Az]
        [▼ Down / -Dec/-Alt]
```

### Behavior by Calibration State

| Controller State | Mode | Behavior |
|-----------------|------|----------|
| **IDLE / PARKED / ERROR** (uncalibrated) | **Low-Level Mode** | Press‑and‑hold sends `ControlAxis` with `VELOCITY_CONTROL`. Release sends `StopAxis`. Movement is direct axis velocity control. |
| **TRACKING / SLEWING** (calibrated) | **Astronomical Mode** | Each press sends a coordinate nudge via `SlewToHorizontal` (alt‑az) or `SlewToCoordinates` (equatorial). Movement is coordinate‑aware. |

### Speed Control
A slider (0.1–5.0 °/s) controls the movement speed:
- **Uncalibrated**: Sets the axis velocity directly
- **Calibrated**: Sets the nudge distance (velocity × 0.5s)

### Mount Type Awareness
- **Equatorial**: Left/Right adjusts RA (axis 0), Up/Down adjusts Dec (axis 1)
- **Alt‑Az**: Left/Right adjusts Azimuth (axis 0), Up/Down adjusts Altitude (axis 1)
- Auto-detected from controller state (defaults to equatorial)

### Emergency Stop
The red **EMERGENCY STOP** button sends `EmergencyStop` with `axis_id=-1` to halt all axes immediately.

## Database Tab

The **Database** tab provides management of the astronomical object database via a separate gRPC service on port 50052.

### Cards

| Card              | Description                                                  |
|-------------------|--------------------------------------------------------------|
| **Database Stats** | Total objects, favorites count, objects by type, by catalog, average magnitude, last update |
| **Search Objects** | Text search by name/catalog ID, filter by object type, max magnitude filter |
| **Objects**       | Scrollable, paginated list of objects showing name, type, magnitude, coordinates. Click to view details. |
| **Object Details** | Full detail view with coordinates (RA/Dec), magnitudes (V/B/J/H/K), spectral type, physical parameters, notes. Actions: Slew, Favorite, Delete. |
| **Add New Object** | Quick-create form for new objects: name, type, RA, Dec, magnitude, catalog ID |

### Features

- **Search**: Type-ahead search by object name or catalog ID. Combine with type and magnitude filters.
- **Browse**: Paginated browsing with sort by name (default). Navigate through pages with Prev/Next buttons.
- **Object Details**: Click any object in the list to see full details in a multi-column grid view.
- **Favorites**: Toggle favorite status with ★/☆ buttons. Favorites are persisted in the database.
- **Slew to Object**: Click the Slew button in the detail view to fill the Control tab's slew form and switch to it.
- **Create Object**: Add new objects via the form on the right. Name is required; coordinates, magnitude, type, and catalog ID are optional.
- **Delete Object**: Remove objects from the database via the Delete button in the detail view.

### How It Works

1. The proxy server (`server.js`) creates a second gRPC client connected to `astro_objects.ObjectDatabaseService` on port 50052.
2. REST endpoints under `/api/db/*` are proxied to the database gRPC service.
3. The `database.js` component manages the UI, calling API methods from `api.js`.
4. Database stats are lazy-loaded when the Database tab is first clicked.

### API Client Methods (api.js)

| Method              | Endpoint              | Description               |
|---------------------|-----------------------|---------------------------|
| `getDbStats()`      | `GET /api/db/stats`   | Database statistics       |
| `listObjects()`     | `GET /api/db/objects` | Paginated object list     |
| `searchObjects()`   | `GET /api/db/objects/search` | Search by query    |
| `getObject(id)`     | `GET /api/db/objects/:id` | Get single object     |
| `createObject(data)`| `POST /api/db/objects` | Create new object        |
| `updateObject(id, data)` | `PUT /api/db/objects/:id` | Update object     |
| `deleteObject(id)`  | `DELETE /api/db/objects/:id` | Delete object      |
| `getFavorites()`    | `GET /api/db/favorites` | List favorites          |
| `addFavorite(id)`   | `POST /api/db/favorites` | Add to favorites       |
| `removeFavorite(id)`| `DELETE /api/db/favorites/:id` | Remove from favorites |
| `listCategories()`  | `GET /api/db/categories` | List categories        |
| `createCategory(data)` | `POST /api/db/categories` | Create category      |
| `getTonightBest()`  | `GET /api/db/tonight` | Tonight's best objects   |

### Prerequisites

The object database gRPC service must be running on port 50052 (or configured via `DB_GRPC_HOST`/`DB_GRPC_PORT` in `.env`).

## Extending — Adding New Cards

The framework is designed for easy extension. To add a new card/tab:

### 1. Create a component

```javascript
// web/public/js/components/myNewCard.js
const MyNewCard = (() => {
  'use strict';

  function render(data) {
    // Update DOM with data
  }

  function init() {
    // Bind event listeners
  }

  return { render, init };
})();
```

### 2. Add tab button (index.html)

```html
<button class="tab-btn" data-tab="my-tab" role="tab">
  <span class="tab-icon">🌟</span>
  <span class="tab-label">My Tab</span>
</button>
```

### 3. Add tab panel with card(s) (index.html)

```html
<section id="panel-my-tab" class="tab-panel" role="tabpanel">
  <div class="card-grid">
    <div class="card">
      <div class="card-header">
        <h2 class="card-title">My Card</h2>
      </div>
      <div class="card-body" id="my-card-content">
        <div class="status-placeholder">Loading...</div>
      </div>
    </div>
  </div>
</section>
```

### 4. Register component (index.html)

```html
<script src="js/components/myNewCard.js"></script>
```

### 5. Initialize in app.js

```javascript
// In App.init():
MyNewCard.init();

// In poll() or via lazy-load when tab is activated:
MyNewCard.render(data);
```

## Development

Run the proxy in watch mode for development:

```bash
cd web/proxy
npm run dev
```

## Security

- CORS is configured to restrict access to specified origins
- Enable SSL/TLS for production deployments
- The proxy does not expose gRPC directly — only the REST API is accessible
- Input validation is performed both client-side (form validation) and server-side (Express)
