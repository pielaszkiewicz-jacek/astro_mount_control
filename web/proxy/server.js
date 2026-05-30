/**
 * Astronomical Mount Controller - Web Proxy Server
 *
 * HTTP/JSON proxy that bridges web applications to the gRPC backend.
 * Exposes RESTful endpoints consumed by the SPA frontend.
 *
 * Architecture:
 *   Browser (SPA) → HTTP/JSON → Proxy Server → gRPC → Mount Controller
 *
 * This proxy translates between browser-friendly JSON APIs and
 * the protobuf-based gRPC service of the mount controller.
 */

require('dotenv').config();

const express = require('express');
const cors = require('cors');
const morgan = require('morgan');
const path = require('path');
const grpc = require('@grpc/grpc-js');
const protoLoader = require('@grpc/proto-loader');

// ─── Configuration ───────────────────────────────────────────────────────────

const config = {
  grpc: {
    host: process.env.GRPC_HOST || 'localhost',
    port: parseInt(process.env.GRPC_PORT, 10) || 50051,
  },
  db: {
    host: process.env.DB_GRPC_HOST || 'localhost',
    port: parseInt(process.env.DB_GRPC_PORT, 10) || 50052,
  },
  proxy: {
    host: process.env.PROXY_HOST || '0.0.0.0',
    port: parseInt(process.env.PROXY_PORT, 10) || 8080,
  },
  cors: {
    origins: (process.env.CORS_ORIGINS || 'http://localhost:8080').split(',').map(s => s.trim()),
  },
  ssl: {
    enabled: process.env.ENABLE_SSL === 'true',
    certPath: process.env.SSL_CERT_PATH || '',
    keyPath: process.env.SSL_KEY_PATH || '',
  },
};

// ─── gRPC Client Setup (Mount Controller) ────────────────────────────────────

const MOUNT_PROTO_PATH = path.join(__dirname, '../../proto/mount_controller.proto');

const mountPackageDefinition = protoLoader.loadSync(MOUNT_PROTO_PATH, {
  keepCase: true,
  longs: String,
  enums: String,
  defaults: true,
  oneofs: true,
});

const mountProtoDescriptor = grpc.loadPackageDefinition(mountPackageDefinition);
const mountProto = mountProtoDescriptor.astro_mount;

let grpcClient = null;

/**
 * Creates or recreates the gRPC client connection to the mount controller.
 */
function createGrpcClient() {
  const address = `${config.grpc.host}:${config.grpc.port}`;

  if (grpcClient) {
    grpcClient.close();
  }

  const credentials = config.ssl.enabled
    ? grpc.credentials.createSsl()
    : grpc.credentials.createInsecure();

  grpcClient = new mountProto.MountControllerService(address, credentials);

  console.log(`[gRPC] Connected to mount controller at ${address}`);
  return grpcClient;
}

// ─── gRPC Client Setup (Object Database) ─────────────────────────────────────

const DB_PROTO_PATH = path.join(__dirname, '../../db/proto/object_database.proto');

const dbPackageDefinition = protoLoader.loadSync(DB_PROTO_PATH, {
  keepCase: true,
  longs: String,
  enums: String,
  defaults: true,
  oneofs: true,
});

const dbProtoDescriptor = grpc.loadPackageDefinition(dbPackageDefinition);
const dbProto = dbProtoDescriptor.astro_objects;

let dbGrpcClient = null;

/**
 * Creates or recreates the gRPC client connection to the object database.
 */
function createDbGrpcClient() {
  const address = `${config.db.host}:${config.db.port}`;

  if (dbGrpcClient) {
    dbGrpcClient.close();
  }

  const credentials = config.ssl.enabled
    ? grpc.credentials.createSsl()
    : grpc.credentials.createInsecure();

  // Allow large messages (e.g. HYG catalog ~14MB CSV data)
  // Set to 64MB to accommodate future growth
  const channelOptions = {
    'grpc.max_receive_message_length': 64 * 1024 * 1024,
    'grpc.max_send_message_length': 64 * 1024 * 1024,
  };

  dbGrpcClient = new dbProto.ObjectDatabaseService(address, credentials, channelOptions);

  console.log(`[gRPC] Connected to object database at ${address}`);
  return dbGrpcClient;
}

/**
 * Wraps a database gRPC call into a Promise for async/await usage.
 * @param {string} method - The gRPC method name
 * @param {object} request - The request payload
 * @param {number} [timeoutSeconds=10] - Timeout in seconds (default 10, use 300+ for large imports)
 */
function dbGrpcCall(method, request = {}, timeoutSeconds = 10) {
  return new Promise((resolve, reject) => {
    if (!dbGrpcClient) {
      reject(new Error('Database gRPC client not initialized'));
      return;
    }

    const deadline = new Date();
    deadline.setSeconds(deadline.getSeconds() + timeoutSeconds);

    dbGrpcClient[method](request, { deadline }, (error, response) => {
      if (error) {
        reject(error);
      } else {
        resolve(response);
      }
    });
  });
}

/**
 * Wraps a gRPC call into a Promise for async/await usage.
 */
function grpcCall(method, request = {}) {
  return new Promise((resolve, reject) => {
    if (!grpcClient) {
      reject(new Error('gRPC client not initialized'));
      return;
    }

    const deadline = new Date();
    deadline.setSeconds(deadline.getSeconds() + 5); // 5s timeout

    grpcClient[method](request, { deadline }, (error, response) => {
      if (error) {
        reject(error);
      } else {
        resolve(response);
      }
    });
  });
}

// ─── Express App Setup ───────────────────────────────────────────────────────

const app = express();

// Middleware
app.use(cors({
  origin: config.cors.origins,
  methods: ['GET', 'POST', 'PUT', 'DELETE', 'OPTIONS'],
  allowedHeaders: ['Content-Type', 'Authorization'],
}));

app.use(express.json());
app.use(morgan(config.proxy.host === '0.0.0.0' ? 'dev' : 'combined'));

// Serve static frontend files with no-cache headers for development
app.use(express.static(path.join(__dirname, '../public'), {
  maxAge: 0,
  etag: false,
  lastModified: false,
  setHeaders: (res, filePath) => {
    res.setHeader('Cache-Control', 'no-store, no-cache, must-revalidate, proxy-revalidate');
    res.setHeader('Pragma', 'no-cache');
    res.setHeader('Expires', '0');
  },
}));

// ─── Address Configuration Endpoints ─────────────────────────────────────────

/**
 * GET /api/config/addresses
 * Returns the current gRPC addresses for mount controller and database.
 */
app.get('/api/config/addresses', (req, res) => {
  res.json({
    controller: {
      host: config.grpc.host,
      port: config.grpc.port,
    },
    database: {
      host: config.db.host,
      port: config.db.port,
    },
  });
});

/**
 * POST /api/config/addresses
 * Update the gRPC addresses and reconnect clients.
 * Body: { controller?: { host, port }, database?: { host, port } }
 */
app.post('/api/config/addresses', async (req, res) => {
  try {
    const { controller, database } = req.body;
    const reconnected = [];

    if (controller) {
      const host = controller.host || config.grpc.host;
      const port = controller.port || config.grpc.port;

      if (typeof host !== 'string' || host.length === 0) {
        return res.status(400).json({ error: 'Invalid controller host' });
      }
      if (typeof port !== 'number' || port < 1 || port > 65535) {
        return res.status(400).json({ error: 'Invalid controller port (1-65535)' });
      }

      config.grpc.host = host;
      config.grpc.port = port;
      createGrpcClient();
      reconnected.push('controller');
    }

    if (database) {
      const host = database.host || config.db.host;
      const port = database.port || config.db.port;

      if (typeof host !== 'string' || host.length === 0) {
        return res.status(400).json({ error: 'Invalid database host' });
      }
      if (typeof port !== 'number' || port < 1 || port > 65535) {
        return res.status(400).json({ error: 'Invalid database port (1-65535)' });
      }

      config.db.host = host;
      config.db.port = port;
      createDbGrpcClient();
      reconnected.push('database');
    }

    res.json({
      success: true,
      message: `Reconnected: ${reconnected.join(', ')}`,
      addresses: {
        controller: { host: config.grpc.host, port: config.grpc.port },
        database: { host: config.db.host, port: config.db.port },
      },
    });
  } catch (err) {
    res.status(500).json({ error: 'Failed to update addresses', details: err.message });
  }
});

// ─── REST API Endpoints ──────────────────────────────────────────────────────

/**
 * GET /api/status
 * Returns the current mount controller state.
 * Maps to gRPC: GetState()
 */
app.get('/api/status', async (req, res) => {
  try {
    const state = await grpcCall('GetState', {});
    res.json(formatState(state));
  } catch (err) {
    res.status(503).json({ error: 'Mount controller unreachable', details: err.message });
  }
});

/**
 * POST /api/slew
 * Slew to equatorial coordinates.
 * Maps to gRPC: SlewToCoordinates()
 * Body: { ra: number, dec: number }
 */
app.post('/api/slew', async (req, res) => {
  try {
    const { ra, dec } = req.body;

    if (ra === undefined || dec === undefined) {
      return res.status(400).json({ error: 'Missing required fields: ra, dec' });
    }

    if (typeof ra !== 'number' || ra < 0 || ra >= 24) {
      return res.status(400).json({ error: 'RA must be a number in range [0, 24)' });
    }

    if (typeof dec !== 'number' || dec < -90 || dec > 90) {
      return res.status(400).json({ error: 'Dec must be a number in range [-90, 90]' });
    }

    await grpcCall('SlewToCoordinates', { ra, dec });
    res.json({ success: true, message: `Slewing to RA=${ra}h, Dec=${dec}°` });
  } catch (err) {
    res.status(502).json({ error: 'Slew failed', details: err.message });
  }
});

/**
 * POST /api/stop
 * Stop all mount movement.
 * Maps to gRPC: Stop()
 */
app.post('/api/stop', async (req, res) => {
  try {
    await grpcCall('Stop', {});
    res.json({ success: true, message: 'Mount stopped' });
  } catch (err) {
    res.status(502).json({ error: 'Stop failed', details: err.message });
  }
});

/**
 * POST /api/park
 * Park the mount.
 * Maps to gRPC: Park()
 */
app.post('/api/park', async (req, res) => {
  try {
    await grpcCall('Park', {});
    res.json({ success: true, message: 'Mount parking' });
  } catch (err) {
    res.status(502).json({ error: 'Park failed', details: err.message });
  }
});

/**
 * POST /api/unpark
 * Unpark the mount.
 * Maps to gRPC: Unpark()
 */
app.post('/api/unpark', async (req, res) => {
  try {
    await grpcCall('Unpark', {});
    res.json({ success: true, message: 'Mount unparked' });
  } catch (err) {
    res.status(502).json({ error: 'Unpark failed', details: err.message });
  }
});

/**
 * POST /api/clear-errors
 * Clear any error state in the mount controller.
 * Maps to gRPC: ClearErrors()
 */
app.post('/api/clear-errors', async (req, res) => {
  try {
    await grpcCall('ClearErrors', {});
    res.json({ success: true, message: 'Errors cleared' });
  } catch (err) {
    res.status(502).json({ error: 'ClearErrors failed', details: err.message });
  }
});

/**
 * GET /api/config
 * Get the current mount controller configuration.
 * Maps to gRPC: GetConfiguration()
 */
app.get('/api/config', async (req, res) => {
  try {
    const configData = await grpcCall('GetConfiguration', {});
    res.json(configData);
  } catch (err) {
    res.status(503).json({ error: 'Cannot fetch configuration', details: err.message });
  }
});

/**
 * POST /api/config
 * Update the mount controller configuration.
 * Maps to gRPC: UpdateConfiguration()
 * Body: Partial configuration object with fields to update
 */
app.post('/api/config', async (req, res) => {
  try {
    const configData = req.body;
    if (!configData || Object.keys(configData).length === 0) {
      return res.status(400).json({ error: 'Request body must contain configuration data' });
    }

    await grpcCall('UpdateConfiguration', configData);
    res.json({ success: true, message: 'Configuration updated successfully' });
  } catch (err) {
    res.status(502).json({ error: 'Failed to update configuration', details: err.message });
  }
});

/**
 * POST /api/config/reset
 * Reset all configuration to factory defaults.
 * Maps to gRPC: ResetConfiguration() (if available) or UpdateConfiguration() with defaults
 */
app.post('/api/config/reset', async (req, res) => {
  try {
    // Try ResetConfiguration RPC if available, otherwise fall back to updating with empty config
    await grpcCall('ResetConfiguration', {});
    res.json({ success: true, message: 'Configuration reset to defaults' });
  } catch (err) {
    res.status(502).json({ error: 'Failed to reset configuration', details: err.message });
  }
});

/**
 * POST /api/config/reset-group
 * Reset a specific configuration group to defaults.
 * Maps to gRPC: ResetConfigurationGroup()
 * Body: { group: string }
 */
app.post('/api/config/reset-group', async (req, res) => {
  try {
    const { group } = req.body;
    if (!group) {
      return res.status(400).json({ error: 'Missing required field: group' });
    }

    await grpcCall('ResetConfigurationGroup', { group });
    res.json({ success: true, message: `Configuration group '${group}' reset to defaults` });
  } catch (err) {
    res.status(502).json({ error: 'Failed to reset configuration group', details: err.message });
  }
});

/**
 * POST /api/axis/move
 * Move an axis at a specified velocity (low-level control for uncalibrated mounts).
 * Maps to gRPC: ControlAxis() with VELOCITY_CONTROL
 * Body: { axis_id: number, velocity: number }
 *   axis_id: 0=HA/RA/Azimuth, 1=Dec/Altitude
 *   velocity: positive/negative for direction (deg/s)
 */
app.post('/api/axis/move', async (req, res) => {
  try {
    const { axis_id, velocity } = req.body;

    if (axis_id === undefined || velocity === undefined) {
      return res.status(400).json({ error: 'Missing required fields: axis_id, velocity' });
    }

    if (![0, 1].includes(axis_id)) {
      return res.status(400).json({ error: 'axis_id must be 0 or 1' });
    }

    if (typeof velocity !== 'number' || isNaN(velocity)) {
      return res.status(400).json({ error: 'velocity must be a valid number' });
    }

    await grpcCall('ControlAxis', {
      axis_id,
      mode: 'VELOCITY_CONTROL',
      target_velocity: velocity,
      relative: true,
    });
    res.json({ success: true, message: `Axis ${axis_id} moving at ${velocity}°/s` });
  } catch (err) {
    res.status(502).json({ error: 'Axis move failed', details: err.message });
  }
});

/**
 * POST /api/axis/stop
 * Stop an axis movement (smooth deceleration).
 * Maps to gRPC: StopAxis()
 * Body: { axis_id: number }
 */
app.post('/api/axis/stop', async (req, res) => {
  try {
    const { axis_id } = req.body;

    if (axis_id === undefined) {
      return res.status(400).json({ error: 'Missing required field: axis_id' });
    }

    if (![0, 1].includes(axis_id)) {
      return res.status(400).json({ error: 'axis_id must be 0 or 1' });
    }

    await grpcCall('StopAxis', { axis_id, decelerate: true });
    res.json({ success: true, message: `Axis ${axis_id} stopping` });
  } catch (err) {
    res.status(502).json({ error: 'Axis stop failed', details: err.message });
  }
});

/**
 * POST /api/axis/emergency-stop
 * Emergency stop — halt all axes immediately.
 * Maps to gRPC: EmergencyStop()
 * Body: { axis_id?: number } — defaults to -1 (all axes)
 */
app.post('/api/axis/emergency-stop', async (req, res) => {
  try {
    const axis_id = req.body.axis_id !== undefined ? req.body.axis_id : -1;
    await grpcCall('EmergencyStop', { axis_id, reset_after: false });
    res.json({ success: true, message: 'Emergency stop — all axes halted' });
  } catch (err) {
    res.status(502).json({ error: 'Emergency stop failed', details: err.message });
  }
});

/**
 * POST /api/axis/slew-horizontal
 * Slew to horizontal coordinates (for calibrated alt-az mounts).
 * Maps to gRPC: SlewToHorizontal()
 * Body: { altitude: number, azimuth: number }
 */
app.post('/api/axis/slew-horizontal', async (req, res) => {
  try {
    let { altitude, azimuth } = req.body;

    if (altitude === undefined || azimuth === undefined) {
      return res.status(400).json({ error: 'Missing required fields: altitude, azimuth' });
    }

    if (typeof altitude !== 'number' || altitude < 0 || altitude > 90) {
      return res.status(400).json({ error: 'altitude must be in range [0, 90]' });
    }

    if (typeof azimuth !== 'number' || azimuth < 0 || azimuth > 360) {
      return res.status(400).json({ error: 'azimuth must be in range [0, 360]' });
    }

    await grpcCall('SlewToHorizontal', { altitude, azimuth });
    res.json({ success: true, message: `Slewing to Alt=${altitude}°, Az=${azimuth}°` });
  } catch (err) {
    res.status(502).json({ error: 'Slew horizontal failed', details: err.message });
  }
});

/**
 * GET /api/health
 * Health check endpoint.
 * Maps to gRPC: CheckHealth()
 */
app.get('/api/health', async (req, res) => {
  try {
    const health = await grpcCall('CheckHealth', {});
    res.json(health);
  } catch (err) {
    res.status(503).json({
      status: 'UNHEALTHY',
      error: 'Mount controller not responding',
      details: err.message,
    });
  }
});

// ─── Calibration REST API Endpoints ─────────────────────────────────────────

/**
 * GET /api/calibration/bootstrap/status
 * Get the current bootstrap calibration status.
 * Maps to gRPC: GetBootstrapStatus()
 */
app.get('/api/calibration/bootstrap/status', async (req, res) => {
  try {
    const status = await grpcCall('GetBootstrapStatus', {});
    res.json(status);
  } catch (err) {
    res.status(502).json({ error: 'Failed to get bootstrap status', details: err.message });
  }
});

/**
 * POST /api/calibration/bootstrap/measurements
 * Add a bootstrap measurement for initial alignment.
 * Maps to gRPC: AddBootstrapMeasurement()
 * Body: BootstrapMeasurement fields
 */
app.post('/api/calibration/bootstrap/measurements', async (req, res) => {
  try {
    const measurement = req.body;
    if (!measurement || !measurement.expected) {
      return res.status(400).json({ error: 'Measurement must include expected coordinates' });
    }

    await grpcCall('AddBootstrapMeasurement', measurement);
    res.json({ success: true, message: 'Bootstrap measurement added' });
  } catch (err) {
    res.status(502).json({ error: 'Failed to add bootstrap measurement', details: err.message });
  }
});

/**
 * POST /api/calibration/bootstrap/run
 * Run bootstrap calibration to compute initial alignment.
 * Maps to gRPC: RunBootstrapCalibration()
 */
app.post('/api/calibration/bootstrap/run', async (req, res) => {
  try {
    const result = await grpcCall('RunBootstrapCalibration', {});
    res.json(result);
  } catch (err) {
    res.status(502).json({ error: 'Bootstrap calibration failed', details: err.message });
  }
});

/**
 * DELETE /api/calibration/bootstrap/measurements
 * Clear all bootstrap measurements.
 * Maps to gRPC: ClearBootstrapMeasurements()
 */
app.delete('/api/calibration/bootstrap/measurements', async (req, res) => {
  try {
    await grpcCall('ClearBootstrapMeasurements', {});
    res.json({ success: true, message: 'Bootstrap measurements cleared' });
  } catch (err) {
    res.status(502).json({ error: 'Failed to clear bootstrap measurements', details: err.message });
  }
});

/**
 * GET /api/calibration/tpoint/parameters
 * Get TPOINT calibration parameters and status.
 * Maps to gRPC: GetTPointParameters()
 */
app.get('/api/calibration/tpoint/parameters', async (req, res) => {
  try {
    const params = await grpcCall('GetTPointParameters', {});
    res.json(params);
  } catch (err) {
    res.status(502).json({ error: 'Failed to get TPOINT parameters', details: err.message });
  }
});

/**
 * POST /api/calibration/tpoint/measurements
 * Add a TPOINT measurement for precise calibration.
 * Maps to gRPC: AddTPointMeasurement()
 * Body: Measurement fields
 */
app.post('/api/calibration/tpoint/measurements', async (req, res) => {
  try {
    const measurement = req.body;
    if (!measurement || !measurement.expected) {
      return res.status(400).json({ error: 'Measurement must include expected coordinates' });
    }

    await grpcCall('AddTPointMeasurement', measurement);
    res.json({ success: true, message: 'TPOINT measurement added' });
  } catch (err) {
    res.status(502).json({ error: 'Failed to add TPOINT measurement', details: err.message });
  }
});

/**
 * POST /api/calibration/tpoint/run
 * Run TPOINT calibration to compute precise pointing model.
 * Maps to gRPC: RunTPointCalibration()
 */
app.post('/api/calibration/tpoint/run', async (req, res) => {
  try {
    await grpcCall('RunTPointCalibration', {});
    res.json({ success: true, message: 'TPOINT calibration completed' });
  } catch (err) {
    res.status(502).json({ error: 'TPOINT calibration failed', details: err.message });
  }
});

/**
 * DELETE /api/calibration/tpoint/measurements
 * Clear all TPOINT measurements.
 * Maps to gRPC: ClearTPointMeasurements()
 */
app.delete('/api/calibration/tpoint/measurements', async (req, res) => {
  try {
    await grpcCall('ClearTPointMeasurements', {});
    res.json({ success: true, message: 'TPOINT measurements cleared' });
  } catch (err) {
    res.status(502).json({ error: 'Failed to clear TPOINT measurements', details: err.message });
  }
});

// ─── Ephemeris Tracking REST API Endpoints ────────────────────────────────────

/**
 * GET /api/tracking/status
 * Get the current ephemeris tracking status.
 * Maps to gRPC: GetEphemerisTrackStatus()
 */
app.get('/api/tracking/status', async (req, res) => {
  try {
    const status = await grpcCall('GetEphemerisTrackStatus', {});
    res.json(status);
  } catch (err) {
    res.status(502).json({ error: 'Failed to get tracking status', details: err.message });
  }
});

/**
 * POST /api/tracking/start
 * Start ephemeris tracking for a cached object by ID.
 * Maps to gRPC: StartEphemerisTracking()
 * Body: { object_id: string, start_time?: string (ISO), wait_at_start?: boolean, slew_margin_seconds?: number }
 */
app.post('/api/tracking/start', async (req, res) => {
  try {
    const { object_id, start_time, wait_at_start, slew_margin_seconds } = req.body;

    if (!object_id) {
      return res.status(400).json({ error: 'Missing required field: object_id' });
    }

    const request = { object_id };

    if (start_time) {
      request.start_time = start_time;
    }
    if (wait_at_start !== undefined) {
      request.wait_at_start = wait_at_start;
    }
    if (slew_margin_seconds !== undefined) {
      request.slew_margin_seconds = slew_margin_seconds;
    }

    const status = await grpcCall('StartEphemerisTracking', request);
    res.json(status);
  } catch (err) {
    res.status(502).json({ error: 'Failed to start tracking', details: err.message });
  }
});

/**
 * POST /api/tracking/start-with-data
 * Upload ephemeris data and start tracking in one call.
 * Maps to gRPC: StartEphemerisTrackingWithData()
 * Body: EphemerisTrackRequest (includes EphemerisData + tracking options)
 */
app.post('/api/tracking/start-with-data', async (req, res) => {
  try {
    const body = req.body;
    if (!body || !body.ephemeris || !body.ephemeris.object_id) {
      return res.status(400).json({ error: 'Missing required field: ephemeris.object_id' });
    }

    const status = await grpcCall('StartEphemerisTrackingWithData', body);
    res.json(status);
  } catch (err) {
    res.status(502).json({ error: 'Failed to start tracking with data', details: err.message });
  }
});

/**
 * POST /api/tracking/upload
 * Upload ephemeris data for a moving object (satellite, comet, etc.).
 * Maps to gRPC: UploadEphemeris()
 * Body: EphemerisData
 */
app.post('/api/tracking/upload', async (req, res) => {
  try {
    const body = req.body;
    if (!body || !body.object_id) {
      return res.status(400).json({ error: 'Missing required field: object_id' });
    }

    await grpcCall('UploadEphemeris', body);
    res.json({ success: true, message: `Ephemeris uploaded for '${body.object_id}'` });
  } catch (err) {
    res.status(502).json({ error: 'Failed to upload ephemeris', details: err.message });
  }
});

/**
 * POST /api/tracking/stop
 * Stop the currently active ephemeris tracking.
 * Maps to gRPC: StopEphemerisTracking()
 */
app.post('/api/tracking/stop', async (req, res) => {
  try {
    await grpcCall('StopEphemerisTracking', {});
    res.json({ success: true, message: 'Ephemeris tracking stopped' });
  } catch (err) {
    res.status(502).json({ error: 'Failed to stop tracking', details: err.message });
  }
});

/**
 * GET /api/tracking/metrics
 * Get ephemeris tracking metrics.
 * Maps to gRPC: GetEphemerisMetrics()
 */
app.get('/api/tracking/metrics', async (req, res) => {
  try {
    const metrics = await grpcCall('GetEphemerisMetrics', {});
    res.json(metrics);
  } catch (err) {
    res.status(502).json({ error: 'Failed to get tracking metrics', details: err.message });
  }
});

/**
 * POST /api/tracking/clear-cache
 * Clear cached ephemeris data.
 * Maps to gRPC: ClearEphemerisCache()
 */
app.post('/api/tracking/clear-cache', async (req, res) => {
  try {
    await grpcCall('ClearEphemerisCache', {});
    res.json({ success: true, message: 'Ephemeris cache cleared' });
  } catch (err) {
    res.status(502).json({ error: 'Failed to clear ephemeris cache', details: err.message });
  }
});

// ─── Object Database REST API Endpoints ──────────────────────────────────────

/**
 * GET /api/db/stats
 * Get database statistics.
 * Maps to gRPC: GetDatabaseStats()
 */
app.get('/api/db/stats', async (req, res) => {
  try {
    const stats = await dbGrpcCall('GetDatabaseStats', {});
    res.json(stats);
  } catch (err) {
    res.status(503).json({ error: 'Database unreachable', details: err.message });
  }
});

/**
 * GET /api/db/objects
 * List objects with pagination.
 * Maps to gRPC: ListObjects()
 * Query: page, pageSize, sortBy, sortDescending, filterType, minMagnitude, maxMagnitude
 *
 * Note: sortBy is used as a raw SQL column name in ORDER BY (see
 * object_database_service.cpp:776). Map friendly names to actual columns.
 */
const SORT_COLUMN_MAP = {
  name: 'name',
  ra: 'ra_hours',
  dec: 'dec_degrees',
  magnitude: 'v_magnitude',
};
app.get('/api/db/objects', async (req, res) => {
  try {
    const {
      page = 1,
      pageSize = 20,
      sortBy,
      sortDescending,
      filterType,
      minMagnitude,
      maxMagnitude,
    } = req.query;

    const request = {
      page: parseInt(page, 10),
      page_size: parseInt(pageSize, 10),
    };

    if (sortBy) request.sort_by = SORT_COLUMN_MAP[sortBy] || sortBy;
    if (sortDescending === 'true') request.sort_descending = true;
    if (filterType) request.filter_type = filterType;
    if (minMagnitude) request.min_magnitude = parseFloat(minMagnitude);
    if (maxMagnitude) request.max_magnitude = parseFloat(maxMagnitude);

    const result = await dbGrpcCall('ListObjects', request);
    res.json(result);
  } catch (err) {
    res.status(503).json({ error: 'Failed to list objects', details: err.message });
  }
});

/**
 * GET /api/db/objects/search
 * Search objects by query.
 * Maps to gRPC: SearchObjects()
 * Query: query, objectType, raMin, raMax, decMin, decMax, minMagnitude, maxMagnitude
 */
app.get('/api/db/objects/search', async (req, res) => {
  try {
    const {
      query,
      objectType,
      raMin, raMax,
      decMin, decMax,
      minMagnitude, maxMagnitude,
      catalogs,
      favoritesOnly,
      visibleOnly,
      constellation,
    } = req.query;

    const request = {};
    if (query) request.query = query;
    if (objectType) request.object_type = objectType;
    if (raMin) request.ra_min = parseFloat(raMin);
    if (raMax) request.ra_max = parseFloat(raMax);
    if (decMin) request.dec_min = parseFloat(decMin);
    if (decMax) request.dec_max = parseFloat(decMax);
    if (minMagnitude) request.min_magnitude = parseFloat(minMagnitude);
    if (maxMagnitude) request.max_magnitude = parseFloat(maxMagnitude);
    if (catalogs) request.catalogs = catalogs.split(',');
    if (favoritesOnly === 'true') request.include_favorites_only = true;
    if (visibleOnly === 'true') request.include_visible_only = true;

    const result = await dbGrpcCall('SearchObjects', request);

    // Post-filter by constellation if requested.
    // Constellation is stored in the custom_fields column (e.g. "constellation:UMA"),
    // not as a dedicated database column, and the C++ server cannot be rebuilt
    // (SOFA compiler bug), so we filter at the proxy level.
    if (constellation && result.objects && result.objects.length > 0) {
      const constelUpper = constellation.trim().toUpperCase();
      // Debug: log first 3 objects' custom_fields to understand actual structure
      for (let i = 0; i < Math.min(3, result.objects.length); i++) {
        const obj = result.objects[i];
        console.log(`[DEBUG] obj[${i}] name=${obj.name}, custom_fields=`, JSON.stringify(obj.custom_fields), 'type=', typeof obj.custom_fields);
        if (obj.custom_fields && typeof obj.custom_fields === 'object') {
          console.log(`[DEBUG]   keys=`, Object.keys(obj.custom_fields));
          console.log(`[DEBUG]   constellation=`, obj.custom_fields.constellation);
        }
      }
      result.objects = result.objects.filter(obj => {
        if (!obj.custom_fields) return false;
        const cf = obj.custom_fields;
        let objConstel = '';
        // custom_fields is a map (object) where constellation is stored as a key
        if (typeof cf === 'object' && cf.constellation) {
          objConstel = cf.constellation.toUpperCase();
        }
        // Also handle if custom_fields is a string (JSON-like format)
        if (typeof cf === 'string') {
          const match = cf.toUpperCase().match(/CONSTELLATION:(\w+)/);
          if (match) objConstel = match[1];
        }
        return objConstel === constelUpper;
      });
      console.log(`[DEBUG] Constellation filter "${constelUpper}": ${result.objects.length} matches out of ${result.total_count} total`);
      result.total_count = result.objects.length;
      // Recalculate total_pages based on the original page size
      const pageSize = parseInt(req.query.pageSize, 10) || 20;
      result.total_pages = Math.ceil(result.objects.length / pageSize) || 1;
    }

    res.json(result);
  } catch (err) {
    res.status(503).json({ error: 'Search failed', details: err.message });
  }
});

/**
 * POST /api/db/objects
 * Create a new astronomical object.
 * Maps to gRPC: CreateObject()
 * Body: AstronomicalObject fields
 */
app.post('/api/db/objects', async (req, res) => {
  try {
    const objectData = req.body;
    if (!objectData.name) {
      return res.status(400).json({ error: 'Missing required field: name' });
    }

    // object_type is passed as-is (string like 'STAR').
    // The gRPC proto-loader with enums: String handles conversion automatically.

    const result = await dbGrpcCall('CreateObject', objectData);
    res.status(201).json(result);
  } catch (err) {
    res.status(502).json({ error: 'Create object failed', details: err.message });
  }
});

/**
 * GET /api/db/objects/:id
 * Get a single object by ID.
 * Maps to gRPC: GetObject()
 */
app.get('/api/db/objects/:id', async (req, res) => {
  try {
    const { id } = req.params;
    const object = await dbGrpcCall('GetObject', { id });
    res.json(object);
  } catch (err) {
    res.status(404).json({ error: 'Object not found', details: err.message });
  }
});

/**
 * PUT /api/db/objects/:id
 * Update an existing object.
 * Maps to gRPC: UpdateObject()
 * Body: AstronomicalObject fields (id is taken from URL)
 */
app.put('/api/db/objects/:id', async (req, res) => {
  try {
    const { id } = req.params;
    const objectData = { id, ...req.body };
    await dbGrpcCall('UpdateObject', objectData);
    res.json({ success: true, message: `Object ${id} updated` });
  } catch (err) {
    res.status(502).json({ error: 'Update object failed', details: err.message });
  }
});

/**
 * DELETE /api/db/objects/:id
 * Delete an object by ID.
 * Maps to gRPC: DeleteObject()
 */
app.delete('/api/db/objects/:id', async (req, res) => {
  try {
    const { id } = req.params;
    await dbGrpcCall('DeleteObject', { id });
    res.json({ success: true, message: `Object ${id} deleted` });
  } catch (err) {
    res.status(502).json({ error: 'Delete object failed', details: err.message });
  }
});

/**
 * GET /api/db/favorites
 * List all favorite objects.
 * Maps to gRPC: GetFavorites()
 */
app.get('/api/db/favorites', async (req, res) => {
  try {
    const favorites = await dbGrpcCall('GetFavorites', {});
    res.json(favorites);
  } catch (err) {
    res.status(503).json({ error: 'Cannot fetch favorites', details: err.message });
  }
});

/**
 * POST /api/db/favorites
 * Add an object to favorites.
 * Maps to gRPC: AddToFavorites()
 * Body: { object_id: string }
 */
app.post('/api/db/favorites', async (req, res) => {
  try {
    const { object_id } = req.body;
    if (!object_id) {
      return res.status(400).json({ error: 'Missing required field: object_id' });
    }
    await dbGrpcCall('AddToFavorites', { object_id });
    res.json({ success: true, message: `Object ${object_id} added to favorites` });
  } catch (err) {
    res.status(502).json({ error: 'Add to favorites failed', details: err.message });
  }
});

/**
 * DELETE /api/db/favorites/:id
 * Remove an object from favorites.
 * Maps to gRPC: RemoveFromFavorites()
 */
app.delete('/api/db/favorites/:id', async (req, res) => {
  try {
    const { id } = req.params;
    await dbGrpcCall('RemoveFromFavorites', { object_id: id });
    res.json({ success: true, message: `Object ${id} removed from favorites` });
  } catch (err) {
    res.status(502).json({ error: 'Remove from favorites failed', details: err.message });
  }
});

/**
 * GET /api/db/categories
 * List all categories.
 * Maps to gRPC: ListCategories()
 */
app.get('/api/db/categories', async (req, res) => {
  try {
    const categories = await dbGrpcCall('ListCategories', {});
    res.json(categories);
  } catch (err) {
    res.status(503).json({ error: 'Cannot fetch categories', details: err.message });
  }
});

/**
 * POST /api/db/categories
 * Create a new category.
 * Maps to gRPC: CreateCategory()
 * Body: { name: string, description?: string, color?: string, icon?: string }
 */
app.post('/api/db/categories', async (req, res) => {
  try {
    const { name, description, color, icon } = req.body;
    if (!name) {
      return res.status(400).json({ error: 'Missing required field: name' });
    }
    const result = await dbGrpcCall('CreateCategory', { name, description, color, icon });
    res.status(201).json(result);
  } catch (err) {
    res.status(502).json({ error: 'Create category failed', details: err.message });
  }
});

/**
 * POST /api/db/import
 * Import objects from uploaded data (file content as string).
 * Maps to gRPC: ImportCatalog()
 * Body: { format: string, data: string (file content), catalog_name?: string, overwrite?: boolean, field_mapping?: object }
 * Format values: CSV, JSON, FITS, VOTABLE, SIMBAD, NED, MPC
 */
app.post('/api/db/import', async (req, res) => {
  try {
    const { format, data, catalog_name, overwrite, field_mapping } = req.body;
    if (!format) {
      return res.status(400).json({ error: 'Missing required field: format (CSV|JSON|FITS|VOTABLE|SIMBAD|NED|MPC)' });
    }
    if (!data) {
      return res.status(400).json({ error: 'Missing required field: data (file content as string)' });
    }
    const request = {
      format: format.toUpperCase(),
      data: Buffer.from(data, 'utf-8'),
      catalog_name: catalog_name || '',
      overwrite: !!overwrite,
    };
    if (field_mapping) {
      request.field_mapping = field_mapping;
    }
    const result = await dbGrpcCall('ImportCatalog', request, 300);
    res.json(result);
  } catch (err) {
    res.status(502).json({ error: 'Import failed', details: err.message });
  }
});

/**
 * POST /api/db/import/url
 * Fetch a catalog from a remote URL and import it.
 * Maps to gRPC: ImportCatalog() after fetching URL content server-side
 * Body: { url: string, format: string, catalog_name?: string, overwrite?: boolean, field_mapping?: object }
 */
app.post('/api/db/import/url', async (req, res) => {
  try {
    const { url, format, catalog_name, overwrite, field_mapping } = req.body;
    if (!url) {
      return res.status(400).json({ error: 'Missing required field: url' });
    }
    if (!format) {
      return res.status(400).json({ error: 'Missing required field: format' });
    }

    // Fetch remote catalog data
    const https = require('https');
    const http = require('http');
    const protocol = url.startsWith('https') ? https : http;

    const remoteData = await new Promise((resolve, reject) => {
      protocol.get(url, (response) => {
        if (response.statusCode < 200 || response.statusCode >= 300) {
          reject(new Error(`HTTP ${response.statusCode}: ${response.statusMessage}`));
          return;
        }
        let data = '';
        response.on('data', chunk => data += chunk);
        response.on('end', () => resolve(data));
      }).on('error', (err) => reject(err));
    });

    const request = {
      format: format.toUpperCase(),
      data: Buffer.from(remoteData, 'utf-8'),
      catalog_name: catalog_name || '',
      overwrite: !!overwrite,
    };
    if (field_mapping) {
      request.field_mapping = field_mapping;
    }
    const result = await dbGrpcCall('ImportCatalog', request, 300);
    res.json(result);
  } catch (err) {
    res.status(502).json({ error: 'Import from URL failed', details: err.message });
  }
});

/**
 * GET /api/db/import/presets
 * Return a list of well-known astronomical catalog presets.
 * Each preset defines name, label, url, format, type, description, and optional field_mapping.
 */
app.get('/api/db/import/presets', (req, res) => {
  const presets = [
    {
      name: 'messier',
      label: 'Messier Catalog',
      url: 'https://raw.githubusercontent.com/mattiaverga/OpenNGC/master/database_files/NGC.csv',
      format: 'CSV',
      type: 'star_cluster,nebula,galaxy',
      size: 110,
      description: '110 deep-sky objects catalogued by Charles Messier (M1–M110)',
      field_mapping: { name: 'Name', ra: 'RA', dec: 'Dec', type: 'Type', constellation: 'Const', magnitude: 'V-Mag' }
    },
    {
      name: 'ngc',
      label: 'NGC Catalog',
      url: 'https://raw.githubusercontent.com/mattiaverga/OpenNGC/master/database_files/NGC.csv',
      format: 'CSV',
      type: 'galaxy,nebula,star_cluster',
      size: 8000,
      description: 'New General Catalogue — ~8,000 deep-sky objects',
      field_mapping: { name: 'Name', ra: 'RA', dec: 'Dec', type: 'Type', constellation: 'Const', magnitude: 'V-Mag' }
    },
    {
      name: 'ic',
      label: 'IC Catalog',
      url: 'https://raw.githubusercontent.com/mattiaverga/OpenNGC/master/database_files/NGC.csv',
      format: 'CSV',
      type: 'galaxy,nebula,star_cluster',
      size: 5000,
      description: 'Index Catalogue — ~5,000 deep-sky objects (supplement to NGC)',
      field_mapping: { name: 'Name', ra: 'RA', dec: 'Dec', type: 'Type', constellation: 'Const', magnitude: 'V-Mag' }
    },
    {
      name: 'caldwell',
      label: 'Caldwell Catalog',
      url: 'https://raw.githubusercontent.com/mattiaverga/OpenNGC/master/database_files/addendum.csv',
      format: 'CSV',
      type: 'star_cluster,nebula,galaxy',
      size: 109,
      description: '109 bright deep-sky objects compiled by Sir Patrick Caldwell-Moore (C1–C109)',
      field_mapping: { name: 'Name', ra: 'RA', dec: 'Dec', type: 'Type', constellation: 'Const', magnitude: 'V-Mag' }
    },
    {
      name: 'hyg',
      label: 'HYG Database v4.1',
      url: 'https://raw.githubusercontent.com/astronexus/HYG-Database/master/hyg/CURRENT/hygdata_v41.csv',
      format: 'CSV',
      type: 'star',
      size: 120000,
      description: 'Hipparcos-Yale-Gliese star database v4.1 — ~120,000 stars with accurate coordinates and magnitudes',
      field_mapping: {
        name: 'proper', ra: 'ra', dec: 'dec', magnitude: 'mag', spectral_type: 'spect',
        b_magnitude: 'ci', catalog_id: 'hip', catalog_name: 'HYG', constellation: 'con',
        type: '*'
      }
    },
    {
      name: 'bright_stars',
      label: 'Bright Star Catalog (Yale)',
      url: 'https://raw.githubusercontent.com/astronexus/HYG-Database/master/hyg/CURRENT/hygdata_v41.csv',
      format: 'CSV',
      type: 'star',
      size: 9100,
      description: 'Yale Bright Star Catalog — ~9,100 stars brighter than magnitude 6.5 (filtered client-side)',
      field_mapping: { name: 'proper', ra: 'ra', dec: 'dec', magnitude: 'mag', spectral_type: 'spect', constellation: 'con', b_magnitude: 'ci', type: '*' }
    },
    {
      name: 'sao',
      label: 'SAO Star Catalog',
      url: 'https://raw.githubusercontent.com/astronexus/HYG-Database/master/hyg/CURRENT/hygdata_v41.csv',
      format: 'CSV',
      type: 'star',
      size: 259000,
      description: 'Smithsonian Astrophysical Observatory Star Catalog — ~259,000 stars (filtered by SAO field)',
      field_mapping: { name: 'proper', ra: 'ra', dec: 'dec', magnitude: 'mag', spectral_type: 'spect', constellation: 'con', b_magnitude: 'ci', type: '*' }
    },
  ];
  res.json({ presets });
});

/**
 * POST /api/db/import/preset/:name
 * Import a well-known catalog by preset name.
 * Fetches the catalog URL server-side, optionally filters CSV rows,
 * and imports via gRPC.
 *
 * Supported presets and their filter logic:
 *   - messier: fetch NGC.csv, keep rows where column M (index 23) is non-empty
 *   - ngc:     fetch NGC.csv, keep rows where column NGC (index 24) is non-empty
 *   - ic:      fetch NGC.csv, keep rows where Name starts with 'IC'
 *   - caldwell: fetch addendum.csv, keep rows where Name starts with 'C'
 *   - hyg, bright_stars, sao: fetch HYG v4.1, no filtering
 */
const OPENNGC_COL_M = 23;   // Messier number column index (0-based) — used for Messier filter

app.post('/api/db/import/preset/:name', async (req, res) => {
  try {
    const presetName = req.params.name;
    const { overwrite } = req.body;

    // Preset definitions (must match GET /presets)
    const presets = [
      { name: 'messier', url: 'https://raw.githubusercontent.com/mattiaverga/OpenNGC/master/database_files/NGC.csv', format: 'CSV', filter: 'col-m', field_mapping: { name: 'Name', ra: 'RA', dec: 'Dec', type: 'Type', constellation: 'Const', magnitude: 'V-Mag' } },
      { name: 'ngc', url: 'https://raw.githubusercontent.com/mattiaverga/OpenNGC/master/database_files/NGC.csv', format: 'CSV', filter: 'col-ngc', field_mapping: { name: 'Name', ra: 'RA', dec: 'Dec', type: 'Type', constellation: 'Const', magnitude: 'V-Mag' } },
      { name: 'ic', url: 'https://raw.githubusercontent.com/mattiaverga/OpenNGC/master/database_files/NGC.csv', format: 'CSV', filter: 'col-ic', field_mapping: { name: 'Name', ra: 'RA', dec: 'Dec', type: 'Type', constellation: 'Const', magnitude: 'V-Mag' } },
      { name: 'caldwell', url: 'https://raw.githubusercontent.com/mattiaverga/OpenNGC/master/database_files/addendum.csv', format: 'CSV', filter: 'caldwell-name', field_mapping: { name: 'Name', ra: 'RA', dec: 'Dec', type: 'Type', constellation: 'Const', magnitude: 'V-Mag' } },
      { name: 'hyg', url: 'https://raw.githubusercontent.com/astronexus/HYG-Database/master/hyg/CURRENT/hygdata_v41.csv', format: 'CSV', filter: null, field_mapping: { name: 'proper', ra: 'ra', dec: 'dec', magnitude: 'mag', spectral_type: 'spect', b_magnitude: 'ci', catalog_id: 'hip', catalog_name: 'HYG', constellation: 'con', type: '*' } },
      { name: 'bright_stars', url: 'https://raw.githubusercontent.com/astronexus/HYG-Database/master/hyg/CURRENT/hygdata_v41.csv', format: 'CSV', filter: null, field_mapping: { name: 'proper', ra: 'ra', dec: 'dec', magnitude: 'mag', spectral_type: 'spect', constellation: 'con', b_magnitude: 'ci', type: '*' } },
      { name: 'sao', url: 'https://raw.githubusercontent.com/astronexus/HYG-Database/master/hyg/CURRENT/hygdata_v41.csv', format: 'CSV', filter: null, field_mapping: { name: 'proper', ra: 'ra', dec: 'dec', magnitude: 'mag', spectral_type: 'spect', constellation: 'con', b_magnitude: 'ci', type: '*' } },
    ];

    const preset = presets.find(p => p.name === presetName);
    if (!preset) {
      return res.status(404).json({ error: `Unknown preset: "${presetName}". Available: ${presets.map(p => p.name).join(', ')}` });
    }

    // Fetch remote catalog data
    const https = require('https');
    const http = require('http');
    const protocol = preset.url.startsWith('https') ? https : http;

    const remoteData = await new Promise((resolve, reject) => {
      protocol.get(preset.url, (response) => {
        if (response.statusCode < 200 || response.statusCode >= 300) {
          reject(new Error(`HTTP ${response.statusCode}: ${response.statusMessage}`));
          return;
        }
        let data = '';
        response.on('data', chunk => data += chunk);
        response.on('end', () => resolve(data));
      }).on('error', (err) => reject(err));
    });

    // Apply CSV filtering for OpenNGC presets (single file contains multiple catalogs)
    let filteredData = remoteData;
    if (preset.filter === 'col-m') {
      // Keep only rows where column M (Messier number) is non-empty
      filteredData = filterOpenNGCByColumn(remoteData, OPENNGC_COL_M);
      // Transform names: replace NGC/IC identifier with "M<number>" so objects are
      // stored as "M1", "M31", etc. instead of "NGC0205", "NGC0224", etc.
      filteredData = enrichMessierNames(filteredData);
    } else if (preset.filter === 'col-ngc') {
      // Filter by Name column starting with "NGC" — the NGC annotation column (index 24)
      // is only filled for a small subset of objects and is NOT the primary identifier.
      filteredData = filterOpenNGCByNamePrefix(remoteData, 'NGC');
    } else if (preset.filter === 'col-ic') {
      // Filter by Name column starting with "IC" — same issue as NGC above.
      filteredData = filterOpenNGCByNamePrefix(remoteData, 'IC');
    } else if (preset.filter === 'caldwell-name') {
      // The Caldwell preset sources from addendum.csv (C009, C014, C041, C099),
      // but most Caldwell objects (105 out of 109) are in NGC.csv with their
      // Caldwell designation in the "Identifiers" column (index 27), not the Name column.
      // We need to:
      //   1. Fetch NGC.csv and extract+rename Caldwell objects via enrichCaldwellNames()
      //   2. Also fetch addendum.csv and keep rows where Name starts with 'C'
      //   3. Merge both sets (deduplicated)
      const ngcUrl = 'https://raw.githubusercontent.com/mattiaverga/OpenNGC/master/database_files/NGC.csv';
      const addendumUrl = 'https://raw.githubusercontent.com/mattiaverga/OpenNGC/master/database_files/addendum.csv';
      const [ngcRaw, addendumRaw] = await Promise.all([
        fetchUrl(ngcUrl),
        fetchUrl(addendumUrl),
      ]);
      // Extract Caldwell objects from NGC.csv: filter by Identifiers column and
      // rename Name to "C {number}" (e.g. "NGC7000" → "C 020")
      const caldwellFromNgc = enrichCaldwellNames(ngcRaw);
      // Extract Caldwell objects from addendum.csv (already correctly named)
      const caldwellFromAdd = filterCaldwellByName(addendumRaw);
      // Merge: deduplicate by Name
      const ngcLines = caldwellFromNgc ? caldwellFromNgc.split('\n') : [];
      const addLines = caldwellFromAdd ? caldwellFromAdd.split('\n') : [];
      const seen = new Set();
      const merged = [];
      // Process addendum first (takes priority for duplicates)
      for (let i = 0; i < addLines.length; i++) {
        const line = addLines[i].trim();
        if (!line) continue;
        if (i === 0) { merged.push(line); continue; } // header
        const name = line.split(';')[0].trim().toUpperCase();
        if (!seen.has(name)) { seen.add(name); merged.push(line); }
      }
      // Append NGC Caldwell rows not already in addendum
      for (let i = 1; i < ngcLines.length; i++) {
        const line = ngcLines[i].trim();
        if (!line) continue;
        const name = line.split(';')[0].trim().toUpperCase();
        if (!seen.has(name)) { seen.add(name); merged.push(line); }
      }
      filteredData = merged.length > 1 ? merged.join('\n') : (caldwellFromAdd || caldwellFromNgc || '');
    }

    // For HYG-based star catalogs, fill empty proper names with fallback identifiers
    // (HIP, HD, HR, Bayer-Flamsteed, or Gliese number) so that rows without a proper
    // name are not silently skipped by the server's "skip objects without a name" rule.
    if (presetName === 'hyg' || presetName === 'bright_stars' || presetName === 'sao') {
      filteredData = enrichHygNames(filteredData);
    }

    const request = {
      format: preset.format,
      data: Buffer.from(filteredData, 'utf-8'),
      catalog_name: presetName,
      overwrite: !!overwrite,
      field_mapping: preset.field_mapping,
    };
    const result = await dbGrpcCall('ImportCatalog', request, 300);
    res.json(result);
  } catch (err) {
    res.status(502).json({ error: `Import preset "${req.params.name}" failed`, details: err.message });
  }
});

/**
 * Filter OpenNGC CSV data, keeping only the header and rows where the
 * Name (first column) starts with the given prefix (e.g. "NGC" or "IC").
 * The CSV uses semicolon (;) as delimiter.
 */
function filterOpenNGCByNamePrefix(csvData, prefix) {
  const lines = csvData.split('\n');
  if (lines.length === 0) return csvData;

  const header = lines[0];
  const filtered = [header];

  for (let i = 1; i < lines.length; i++) {
    const line = lines[i].trim();
    if (!line) continue;
    const name = line.split(';')[0].trim();
    // Use case-insensitive comparison since some names may be lowercase
    if (name.toUpperCase().startsWith(prefix.toUpperCase())) {
      filtered.push(line);
    }
  }

  return filtered.join('\n');
}

/**
 * Filter OpenNGC CSV data, keeping only the header and rows where the
 * column at colIndex is non-empty (used for Messier M column).
 * The CSV uses semicolon (;) as delimiter.
 */
function filterOpenNGCByColumn(csvData, colIndex) {
  const lines = csvData.split('\n');
  if (lines.length === 0) return csvData;

  const header = lines[0];
  const filtered = [header];

  for (let i = 1; i < lines.length; i++) {
    const line = lines[i].trim();
    if (!line) continue;
    const cols = line.split(';');
    const cell = cols[colIndex] ? cols[colIndex].trim() : '';
    if (cell !== '' && cell !== 'null') {
      filtered.push(line);
    }
  }

  return filtered.join('\n');
}

/**
 * Fetch a URL and return the response body as a string.
 * Uses https or http depending on the URL scheme.
 */
function fetchUrl(url) {
  const https = require('https');
  const http = require('http');
  const protocol = url.startsWith('https') ? https : http;
  return new Promise((resolve, reject) => {
    protocol.get(url, (response) => {
      if (response.statusCode < 200 || response.statusCode >= 300) {
        reject(new Error(`HTTP ${response.statusCode}: ${response.statusMessage}`));
        return;
      }
      let data = '';
      response.on('data', chunk => data += chunk);
      response.on('end', () => resolve(data));
    }).on('error', (err) => reject(err));
  });
}

/**
 * Extract Caldwell objects from OpenNGC CSV data (NGC.csv).
 * In OpenNGC's NGC.csv, Caldwell designations are NOT in the Name column —
 * they are stored in the "Identifiers" column (index 27, 0-based) as e.g.
 * "C 020,LBN 373". This function:
 *   1. Keeps only rows where the Identifiers column contains "C ddd"
 *   2. Replaces the Name column (index 0) with "C {number}" so the object
 *      is stored as "C 1", "C 2", ..., "C 109" instead of "NGC7000", etc.
 *
 * The CSV uses semicolon (;) as delimiter.
 */
function enrichCaldwellNames(csvData) {
  const lines = csvData.split('\n');
  if (lines.length < 2) return null;

  const header = lines[0];
  const result = [header];
  const IDENTIFIERS_COL = 27; // 0-based index of "Identifiers" column
  const NAME_COL = 0;         // 0-based index of "Name" column

  for (let i = 1; i < lines.length; i++) {
    const line = lines[i].trim();
    if (!line) continue;
    const cells = line.split(';');
    const identifiers = cells[IDENTIFIERS_COL] || '';
    // Match "C" followed by a space, then 1-3 digits (word boundary)
    const match = identifiers.match(/\bC\s+(\d{1,3})\b/);
    if (match) {
      // Replace the Name column with "C {number}"
      cells[NAME_COL] = 'C ' + match[1];
      result.push(cells.join(';'));
    }
  }

  return result.length > 1 ? result.join('\n') : null;
}

/**
 * Filter Caldwell catalog from OpenNGC CSV data.
 * Keeps rows where the Name (first column) starts with 'C' (optionally followed
 * by a space) and then a digit. Handles both "C1" and "C 1" name formats.
 */
function filterCaldwellByName(csvData) {
  const lines = csvData.split('\n');
  if (lines.length === 0) return csvData;

  const header = lines[0];
  const filtered = [header];

  for (let i = 1; i < lines.length; i++) {
    const line = lines[i].trim();
    if (!line) continue;
    const name = line.split(';')[0].trim();
    // Match "C" optionally followed by a space, then one or more digits (case-insensitive)
    if (/^C\s?\d+/i.test(name)) {
      filtered.push(line);
    }
  }

  return filtered.join('\n');
}

/**
 * Enrich HYG v4.1 CSV data by filling empty proper names with fallback identifiers.
 * HYG columns (0-based):
 *   0: hip   - HIP catalog number
 *   1: hd    - Henry Draper number
 *   2: hr    - Harvard Revised number
 *   3: gl    - Gliese number
 *   4: bf    - Bayer-Flamsteed designation
 *   5: proper - Proper name (mostly empty)
 * Fallback priority: proper > hip > hd > hr > bf > gl
 * Uses comma delimiter (HYG CSV format).
 */
function enrichHygNames(csvData) {
  const lines = csvData.split('\n');
  if (lines.length < 2) return csvData;

  // Clean header: strip surrounding quotes from column names before matching.
  // The HYG CSV has quoted headers like "id","hip","hd",..., so naive
  // cols.indexOf('hip') would fail because the array element is '"hip"'.
  const header = lines[0].toLowerCase();
  const cols = header.split(',').map(c => c.replace(/^"|"$/g, '').trim());
  const idx = {
    id: cols.indexOf('id'),
    hip: cols.indexOf('hip'),
    hd: cols.indexOf('hd'),
    hr: cols.indexOf('hr'),
    gl: cols.indexOf('gl'),
    bf: cols.indexOf('bf'),
    proper: cols.indexOf('proper'),
  };

  // If 'proper' column doesn't exist, nothing to do
  if (idx.proper === -1) return csvData;

  const result = [lines[0]]; // keep original header unchanged

  for (let i = 1; i < lines.length; i++) {
    const line = lines[i].trim();
    if (!line) continue;

    const cells = line.split(',');

    // Helper: extract cell value and strip surrounding quotes.
    // HYG data uses quoted empty strings ("" ) for missing values; without
    // stripping, '""' is truthy and bypasses the emptiness check.
    const getVal = (colIdx) => {
      if (colIdx === -1) return '';
      const v = cells[colIdx];
      if (!v) return '';
      return v.replace(/^"|"$/g, '').trim();
    };

    const proper = getVal(idx.proper);

    if (proper !== '') {
      // Already has a proper name, keep as-is
      result.push(line);
      continue;
    }

    // Generate fallback name from other identifiers (ordered by reliability)
    let fallback = '';

    if (idx.hip !== -1) {
      const v = getVal(idx.hip);
      if (v) fallback = 'HIP ' + v;
    }
    if (!fallback && idx.hd !== -1) {
      const v = getVal(idx.hd);
      if (v) fallback = 'HD ' + v;
    }
    if (!fallback && idx.hr !== -1) {
      const v = getVal(idx.hr);
      if (v) fallback = 'HR ' + v;
    }
    if (!fallback && idx.bf !== -1) {
      const v = getVal(idx.bf);
      if (v) fallback = v; // Bayer-Flamsteed designation, e.g. "α And"
    }
    if (!fallback && idx.gl !== -1) {
      const v = getVal(idx.gl);
      if (v) fallback = 'Gl ' + v;
    }
    // Last resort: use the HYG sequential ID so that every star gets a name
    if (!fallback && idx.id !== -1) {
      const v = getVal(idx.id);
      if (v) fallback = 'HYG ' + v;
    }

    if (fallback) {
      cells[idx.proper] = fallback;
      result.push(cells.join(','));
    } else {
      // No fallback available, keep row as-is (will be skipped by server)
      result.push(line);
    }
  }

  return result.join('\n');
}

/**
 * Transform OpenNGC CSV data for Messier import: replace the Name column (index 0)
 * with the Messier designation (e.g. "M1", "M31") taken from column M (index 23).
 * The CSV uses semicolon (;) as delimiter.
 */
function enrichMessierNames(csvData) {
  const lines = csvData.split('\n');
  if (lines.length < 2) return csvData;

  const result = [lines[0]]; // keep header unchanged

  for (let i = 1; i < lines.length; i++) {
    const line = lines[i].trim();
    if (!line) continue;

    const cols = line.split(';');
    const mValue = cols[OPENNGC_COL_M] ? cols[OPENNGC_COL_M].trim() : '';
    if (mValue !== '') {
      // Replace the Name column (index 0) with "M<number>"
      cols[0] = 'M' + mValue;
    }
    result.push(cols.join(';'));
  }

  return result.join('\n');
}

/**
 * GET /api/db/tonight
 * Get the best objects for tonight's observation.
 * Maps to gRPC: GetTonightBestObjects()
 * Query: latitude, longitude, altitude, minMagnitude, maxMagnitude, maxResults
 */
app.get('/api/db/tonight', async (req, res) => {
  try {
    const {
      latitude = 52.0,
      longitude = 21.0,
      altitude = 100,
      minMagnitude,
      maxMagnitude,
      maxResults = 20,
    } = req.query;

    const request = {
      latitude: parseFloat(latitude),
      longitude: parseFloat(longitude),
      altitude: parseFloat(altitude),
      max_results: parseInt(maxResults, 10),
    };

    if (minMagnitude) request.min_magnitude = parseFloat(minMagnitude);
    if (maxMagnitude) request.max_magnitude = parseFloat(maxMagnitude);

    const result = await dbGrpcCall('GetTonightBestObjects', request);
    res.json(result);
  } catch (err) {
    res.status(503).json({ error: 'Cannot fetch tonight\'s objects', details: err.message });
  }
});

// ─── Helper: Format ControllerState for JSON ─────────────────────────────────

function formatState(state) {
  return {
    status: state.status || 'UNKNOWN',
    position: state.current_position
      ? {
          axis1: state.current_position.axis1,
          axis2: state.current_position.axis2,
        }
      : null,
    tracked_object: state.tracked_object
      ? {
          ra: state.tracked_object.coordinates?.ra,
          dec: state.tracked_object.coordinates?.dec,
          name: state.tracked_object.coordinates?.object_id,
          tracking_error_ra: state.tracked_object.tracking_error_ra,
          tracking_error_dec: state.tracked_object.tracking_error_dec,
        }
      : null,
    encoders_enabled: state.encoders_enabled,
    guider_active: state.guider_active,
    tracking_rate_ra: state.tracking_rate_ra,
    tracking_rate_dec: state.tracking_rate_dec,
    pier_side: state.pier_side,
    meridian_flipped: state.meridian_flipped,
    time_to_meridian: state.time_to_meridian,
    temperature: state.temperature,
    pressure: state.pressure,
    humidity: state.humidity,
    pointing_error: state.pointing_error,
    tracking_performance: state.tracking_performance,
  };
}

// ─── Error Handling ──────────────────────────────────────────────────────────

app.use((err, req, res, _next) => {
  console.error('[Error]', err.stack || err.message || err);
  res.status(500).json({ error: 'Internal server error' });
});

// ─── Startup ─────────────────────────────────────────────────────────────────

createGrpcClient();
createDbGrpcClient();

app.listen(config.proxy.port, config.proxy.host, () => {
  console.log(`
╔══════════════════════════════════════════════════════════╗
║   Astro Mount Controller - Web Proxy Server            ║
║   Listening on http://${config.proxy.host}:${config.proxy.port}             ║
║   Mount gRPC:  ${config.grpc.host}:${config.grpc.port}                      ║
║   Database gRPC: ${config.db.host}:${config.db.port}                        ║
╚══════════════════════════════════════════════════════════╝
  `);
});

// Graceful shutdown
process.on('SIGINT', () => {
  console.log('\n[Shutdown] Received SIGINT, closing gRPC clients...');
  if (grpcClient) grpcClient.close();
  if (dbGrpcClient) dbGrpcClient.close();
  process.exit(0);
});

process.on('SIGTERM', () => {
  console.log('\n[Shutdown] Received SIGTERM, closing gRPC clients...');
  if (grpcClient) grpcClient.close();
  if (dbGrpcClient) dbGrpcClient.close();
  process.exit(0);
});
