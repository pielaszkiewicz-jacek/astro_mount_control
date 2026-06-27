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
const fs = require('fs');
const path = require('path');
const grpc = require('@grpc/grpc-js');
const protoLoader = require('@grpc/proto-loader');

// ─── Configuration ───────────────────────────────────────────────────────────

const config = {
  grpc: {
    // Use 127.0.0.1 (IPv4) instead of 'localhost' to avoid IPv4/IPv6
    // dual-stack mismatch.  The C++ gRPC server binds to 0.0.0.0 (IPv4),
    // but Node.js may resolve 'localhost' to ::1 (IPv6) first and get
    // ECONNREFUSED.
    host: process.env.GRPC_HOST || '127.0.0.1',
    port: parseInt(process.env.GRPC_PORT, 10) || 50051,
  },
  db: {
    host: process.env.DB_GRPC_HOST || '127.0.0.1',
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
  logging: {
    directory: process.env.LOG_DIRECTORY || '/var/log/astro-mount',
    fileName: process.env.LOG_FILE_NAME || 'astro-mount.log',
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

  const sslStatus = config.ssl.enabled ? ' (TLS)' : ' (insecure)';
  console.log(`[gRPC] Connected to mount controller at ${address}${sslStatus}`);
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
      ssl: config.ssl.enabled,
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
      const ssl = controller.ssl !== undefined ? controller.ssl : config.ssl.enabled;

      if (typeof host !== 'string' || host.length === 0) {
        return res.status(400).json({ error: 'Invalid controller host' });
      }
      if (typeof port !== 'number' || port < 1 || port > 65535) {
        return res.status(400).json({ error: 'Invalid controller port (1-65535)' });
      }

      config.grpc.host = host;
      config.grpc.port = port;
      config.ssl.enabled = ssl;
      console.log(`[ssl] Proxy SSL ${ssl ? 'ENABLED' : 'DISABLED'} for gRPC connection to ${host}:${port}`);
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
        controller: { host: config.grpc.host, port: config.grpc.port, ssl: config.ssl.enabled },
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
 * POST /api/track
 * Slew to equatorial coordinates and start tracking (sidereal).
 * Maps to gRPC: TrackObject()
 * Body: { ra: number, dec: number }
 */
app.post('/api/track', async (req, res) => {
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

    await grpcCall('TrackObject', { ra, dec });
    res.json({ success: true, message: `Tracking RA=${ra}h, Dec=${dec}° (sidereal)` });
  } catch (err) {
    res.status(502).json({ error: 'Track failed', details: err.message });
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
 * POST /api/home
 * Home mount — set reference position for tracking origin.
 * Maps to gRPC: Home(MountHomingRequest)
 * Body: { axis1: number, axis2: number } (telescope degrees)
 */
app.post('/api/home', async (req, res) => {
  try {
    const { axis1, axis2 } = req.body || {};
    if (axis1 == null || axis2 == null) {
      return res.status(400).json({ error: 'axis1 and axis2 (telescope degrees) are required' });
    }
    await grpcCall('Home', { axis1: Number(axis1), axis2: Number(axis2) });
    res.json({ success: true, message: `Mount homed to axis1=${axis1}°, axis2=${axis2}°` });
  } catch (err) {
    res.status(502).json({ error: 'Home failed', details: err.message });
  }
});

/**
 * POST /api/state/save
 * Save the current mount controller state (park position, calibration data, etc.)
 * Maps to gRPC: SaveState()
 * Body (optional): { file_path?: string, include_measurements?: boolean }
 */
app.post('/api/state/save', async (req, res) => {
  try {
    const { file_path, include_measurements } = req.body || {};
    const request = {};
    if (file_path) request.file_path = file_path;
    if (include_measurements !== undefined) request.include_measurements = include_measurements;

    const result = await grpcCall('SaveState', request);
    res.json({
      success: true,
      message: `State saved to ${result.file_path}`,
      file_path: result.file_path,
      file_size: result.file_size,
    });
  } catch (err) {
    res.status(502).json({ error: 'Failed to save mount state', details: err.message });
  }
});

/**
 * POST /api/state/load
 * Load a previously saved mount controller state.
 * Maps to gRPC: LoadState()
 * Body (optional): { file_path?: string }
 */
app.post('/api/state/load', async (req, res) => {
  try {
    const { file_path } = req.body || {};
    const request = {};
    if (file_path) request.file_path = file_path;

    await grpcCall('LoadState', request);
    res.json({ success: true, message: 'Mount state restored successfully' });
  } catch (err) {
    res.status(502).json({ error: 'Failed to restore mount state', details: err.message });
  }
});

/**
 * POST /api/state/upload-and-load
 * Accept an uploaded state file from the browser, save it to a temporary
 * location on the server, then call gRPC LoadState to restore the controller.
 *
 * Body: { file_content: string, file_name: string }
 *
 * The file is written to data/uploads/<timestamp>_<original_name> so the
 * gRPC backend (running from the project root) can read it.
 */
app.post('/api/state/upload-and-load', async (req, res) => {
  try {
    const { file_content, file_name } = req.body || {};
    if (!file_content) {
      return res.status(400).json({ error: 'Missing file_content in request body' });
    }

    // Build the upload path relative to the project root
    // server.js runs from web/proxy/, so ../.. takes us to the project root
    const projectRoot = path.resolve(__dirname, '../..');
    const uploadDir = path.join(projectRoot, 'data', 'uploads');

    // Ensure upload directory exists
    fs.mkdirSync(uploadDir, { recursive: true });

    // Use a timestamp prefix to avoid name collisions
    const timestamp = Date.now();
    const safeName = (file_name || 'mount_state.json').replace(/[^a-zA-Z0-9._-]/g, '_');
    const tempFilePath = path.join(uploadDir, `${timestamp}_${safeName}`);

    // Write the uploaded content to a temp file
    fs.writeFileSync(tempFilePath, file_content, 'utf-8');
    console.log(`[upload-and-load] Written ${file_content.length} bytes to ${tempFilePath}`);

    // Call gRPC LoadState with the temp file path
    const grpcRequest = { file_path: tempFilePath };
    await grpcCall('LoadState', grpcRequest);

    console.log(`[upload-and-load] State restored successfully from ${tempFilePath}`);
    res.json({ success: true, message: `Mount state restored from ${file_name || 'uploaded file'}` });

    // Clean up: remove the temp file after successful load (fire-and-forget)
    fs.unlink(tempFilePath, (unlinkErr) => {
      if (unlinkErr) {
        console.warn(`[upload-and-load] Failed to clean up temp file ${tempFilePath}:`, unlinkErr.message);
      } else {
        console.log(`[upload-and-load] Cleaned up temp file ${tempFilePath}`);
      }
    });
  } catch (err) {
    console.error('[upload-and-load] Error:', err.message);
    res.status(502).json({ error: 'Failed to restore mount state from uploaded file', details: err.message });
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

    // If network SSL settings were changed, sync the proxy's gRPC client.
    // The controller needs a restart to apply SSL on the server side, but
    // the proxy's client-side SSL can be toggled immediately so it's ready
    // when the controller restarts with SSL enabled.
    let sslToggled = false;
    if (configData.network_enable_ssl !== undefined) {
      config.ssl.enabled = !!configData.network_enable_ssl;
      sslToggled = true;
    }
    if (configData.grpc_address !== undefined) {
      config.grpc.host = configData.grpc_address;
    }
    if (configData.grpc_port !== undefined) {
      config.grpc.port = configData.grpc_port;
    }
    if (sslToggled) {
      console.log(`[ssl] Proxy SSL ${config.ssl.enabled ? 'ENABLED' : 'DISABLED'} (synced from controller config)`);
      createGrpcClient();
    }

    res.json({ success: true, message: 'Configuration updated successfully' });
  } catch (err) {
    res.status(502).json({ error: 'Failed to update configuration', details: err.message });
  }
});

/**
 * POST /api/restart
 * Soft restart: reload config, reinitialize, preserving calibrations.
 * Maps to gRPC: RestartController()
 */
app.post('/api/restart', async (req, res) => {
  try {
    await grpcCall('RestartController', {});
    res.json({ success: true, message: 'Controller soft-restarted (calibrations preserved)' });
  } catch (err) {
    res.status(502).json({ error: 'Restart failed', details: err.message });
  }
});

/**
 * POST /api/hard-restart
 * Hard restart: reload config, reinitialize, discarding ALL calibrations.
 * Maps to gRPC: HardRestartController()
 */
app.post('/api/hard-restart', async (req, res) => {
  try {
    await grpcCall('HardRestartController', {});
    res.json({ success: true, message: 'Controller hard-restarted (calibrations discarded)' });
  } catch (err) {
    res.status(502).json({ error: 'Hard restart failed', details: err.message });
  }
});

/**
 * GET /api/mount/orientation
 * Get the mount orientation quaternion.
 * Maps to gRPC: GetMountOrientation()
 */
app.get('/api/mount/orientation', async (req, res) => {
  try {
    const result = await grpcCall('GetMountOrientation', {});
    res.json({ qx: result.qx, qy: result.qy, qz: result.qz, qw: result.qw });
  } catch (err) {
    res.status(502).json({ error: 'Failed to get mount orientation', details: err.message });
  }
});

/**
 * POST /api/mount/orientation
 * Set the mount orientation quaternion (for CASUAL mount type).
 * Maps to gRPC: SetMountOrientation()
 * Body: { qx, qy, qz, qw }
 */
app.post('/api/mount/orientation', async (req, res) => {
  try {
    const { qx, qy, qz, qw } = req.body;
    if (qx === undefined || qy === undefined || qz === undefined || qw === undefined) {
      return res.status(400).json({ error: 'Missing required fields: qx, qy, qz, qw' });
    }
    await grpcCall('SetMountOrientation', { qx, qy, qz, qw });
    res.json({ success: true, message: 'Mount orientation updated' });
  } catch (err) {
    res.status(502).json({ error: 'Failed to set mount orientation', details: err.message });
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
    const { axis_id, velocity, acceleration } = req.body;

    console.log('[proxy] POST /api/axis/move body:', JSON.stringify(req.body));

    if (axis_id === undefined || velocity === undefined) {
      return res.status(400).json({ error: 'Missing required fields: axis_id, velocity' });
    }

    if (![0, 1].includes(axis_id)) {
      return res.status(400).json({ error: 'axis_id must be 0 or 1' });
    }

    if (typeof velocity !== 'number' || isNaN(velocity)) {
      return res.status(400).json({ error: 'velocity must be a valid number' });
    }

    const grpcReq = {
      axis_id,
      mode: 'VELOCITY_CONTROL',
      target_velocity: velocity,
      relative: false,
    };
    // Pass acceleration when explicitly provided; fall back to 50.0 default
    if (typeof acceleration === 'number' && isFinite(acceleration) && acceleration > 0) {
      grpcReq.acceleration = acceleration;
    } else {
      grpcReq.acceleration = 50.0;
    }

    console.log('[proxy] ControlAxis gRPC request:', JSON.stringify(grpcReq));
    await grpcCall('ControlAxis', grpcReq);
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
    const { axis_id, deceleration } = req.body;

    console.log('[proxy] POST /api/axis/stop body:', JSON.stringify(req.body));

    if (axis_id === undefined) {
      return res.status(400).json({ error: 'Missing required field: axis_id' });
    }

    if (![0, 1].includes(axis_id)) {
      return res.status(400).json({ error: 'axis_id must be 0 or 1' });
    }

    const stopReq = { axis_id, decelerate: true };
    if (typeof deceleration === 'number' && isFinite(deceleration) && deceleration > 0) {
      stopReq.deceleration = deceleration;
    }

    console.log('[proxy] StopAxis gRPC request:', JSON.stringify(stopReq));
    await grpcCall('StopAxis', stopReq);
    res.json({ success: true, message: `Axis ${axis_id} stopping` });
  } catch (err) {
    res.status(502).json({ error: 'Axis stop failed', details: err.message });
  }
});

/**
 * POST /api/axis/move-relative
 * Move an axis by a relative offset using position control.
 * Maps to gRPC: ControlAxis() with POSITION_CONTROL, relative=true
 * Body: { axis_id: number, offset_deg: number, velocity?: number }
 */
app.post('/api/axis/move-relative', async (req, res) => {
  try {
    const { axis_id, offset_deg, velocity, acceleration, deceleration } = req.body;

    if (axis_id === undefined || offset_deg === undefined) {
      return res.status(400).json({ error: 'Missing required fields: axis_id, offset_deg' });
    }
    if (![0, 1].includes(axis_id)) {
      return res.status(400).json({ error: 'axis_id must be 0 or 1' });
    }
    if (typeof offset_deg !== 'number' || isNaN(offset_deg)) {
      return res.status(400).json({ error: 'offset_deg must be a valid number' });
    }

    const grpcRequest = {
      axis_id,
      mode: 'POSITION_CONTROL',
      target_position: offset_deg,
      relative: true,
    };
    // Pass max_velocity only when explicitly provided; backend falls back to
    // config.max_slew_rate when the field is absent (0).
    if (typeof velocity === 'number' && isFinite(velocity) && velocity > 0) {
      grpcRequest.max_velocity = velocity;
    }
    if (typeof acceleration === 'number' && isFinite(acceleration) && acceleration > 0) {
      grpcRequest.acceleration = acceleration;
    }
    if (typeof deceleration === 'number' && isFinite(deceleration) && deceleration > 0) {
      grpcRequest.deceleration = deceleration;
    }

    await grpcCall('ControlAxis', grpcRequest);
    res.json({ success: true, message: `Axis ${axis_id} moving by ${offset_deg}°` });
  } catch (err) {
    res.status(502).json({ error: 'Axis relative move failed', details: err.message });
  }
});

/**
 * POST /api/axis/move-absolute
 * Move an axis to an absolute position using position control.
 * Maps to gRPC: ControlAxis() with POSITION_CONTROL, relative=false
 * Body: { axis_id: number, target_deg: number, velocity?: number }
 */
app.post('/api/axis/move-absolute', async (req, res) => {
  try {
    const { axis_id, target_deg, velocity, acceleration, deceleration } = req.body;

    if (axis_id === undefined || target_deg === undefined) {
      return res.status(400).json({ error: 'Missing required fields: axis_id, target_deg' });
    }
    if (![0, 1].includes(axis_id)) {
      return res.status(400).json({ error: 'axis_id must be 0 or 1' });
    }
    if (typeof target_deg !== 'number' || isNaN(target_deg)) {
      return res.status(400).json({ error: 'target_deg must be a valid number' });
    }

    const grpcRequest = {
      axis_id,
      mode: 'POSITION_CONTROL',
      target_position: target_deg,
      relative: false,
    };
    if (typeof velocity === 'number' && isFinite(velocity) && velocity > 0) {
      grpcRequest.max_velocity = velocity;
    }
    if (typeof acceleration === 'number' && isFinite(acceleration) && acceleration > 0) {
      grpcRequest.acceleration = acceleration;
    }
    if (typeof deceleration === 'number' && isFinite(deceleration) && deceleration > 0) {
      grpcRequest.deceleration = deceleration;
    }

    await grpcCall('ControlAxis', grpcRequest);
    res.json({ success: true, message: `Axis ${axis_id} moving to ${target_deg}°` });
  } catch (err) {
    res.status(502).json({ error: 'Axis absolute move failed', details: err.message });
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
 * GET /api/hal/config
 * Returns the HAL configuration flattened for the settings UI.
 */
app.get('/api/hal/config', async (req, res) => {
  try {
    const halConfig = await grpcCall('GetHALConfig', {});
    // Flatten HALConfig proto to flat JSON with hal_* prefixes
    const flat = {};

    // Top-level fields
    if (halConfig.type) flat.hal_type = halConfig.type;
    if (halConfig.name) flat.hal_name = halConfig.name;

    // Gamepad subsection
    if (halConfig.gamepad) {
      flat.hal_gamepad_device_path = halConfig.gamepad.device_path || '';
      flat.hal_gamepad_deadzone = halConfig.gamepad.dead_zone ?? 0.15;
      flat.hal_gamepad_sensitivity = halConfig.gamepad.sensitivity ?? 1.0;
      flat.hal_gamepad_poll_interval_ms = halConfig.gamepad.read_frequency ?? 50;
      flat.hal_gamepad_autostart = halConfig.gamepad.autostart ?? false;
    }

    // Simulated subsection
    if (halConfig.simulated) {
      flat.hal_simulated_enabled = halConfig.simulated.enable_simulation !== false;
      flat.hal_simulated_update_rate = halConfig.simulated.simulation_update_rate ?? 100;
      flat.hal_simulated_position_noise = halConfig.simulated.position_noise_stddev ?? 0.001;
      flat.hal_simulated_velocity_noise = halConfig.simulated.velocity_noise_stddev ?? 0.01;
      flat.hal_simulated_error_probability = halConfig.simulated.error_probability ?? 0.01;
    }

    // Safety subsection
    if (halConfig.safety) {
      flat.hal_safety_enable_limits = halConfig.safety.enable_limits !== false;
      flat.hal_safety_emergency_stop = halConfig.safety.enable_emergency_stop !== false;
      flat.hal_safety_timeout_ms = halConfig.safety.emergency_stop_timeout_ms ?? 1000;
      flat.hal_safety_temp_monitoring = halConfig.safety.enable_temperature_monitoring ?? false;
      flat.hal_safety_current_monitoring = halConfig.safety.enable_current_monitoring ?? false;
      flat.hal_safety_voltage_monitoring = halConfig.safety.enable_voltage_monitoring ?? false;
      flat.hal_safety_min_voltage = halConfig.safety.min_voltage ?? 0;
      flat.hal_safety_max_voltage = halConfig.safety.max_voltage ?? 0;
      flat.hal_safety_monitoring_rate = halConfig.safety.monitoring_rate ?? 10;
    }

    // CanOpen subsection
    if (halConfig.canopen) {
      flat.hal_canopen_library = halConfig.canopen.library || '';
      flat.hal_canopen_interface = halConfig.canopen.interface_name || '';
      flat.hal_canopen_bitrate = halConfig.canopen.bitrate ?? 1000000;
      flat.hal_canopen_node_id = halConfig.canopen.node_id ?? 1;
      flat.hal_canopen_sync = halConfig.canopen.use_sync !== false;
      flat.hal_canopen_sync_period = halConfig.canopen.sync_period_ms ?? 10;
      flat.hal_canopen_sdo_timeout = halConfig.canopen.sdo_timeout_ms ?? 1000;
      // Aliases for Settings UI "HAL" group (expects hal_can_* prefix)
      flat.hal_can_interface = halConfig.canopen.interface_name || '';
      flat.hal_can_node_id = halConfig.canopen.node_id ?? 1;
      flat.hal_can_baud_rate = halConfig.canopen.bitrate ?? 1000000;
      flat.hal_heartbeat_interval_ms = halConfig.canopen.sync_period_ms ?? 100;
      flat.hal_pdo_mapping_mode = String(halConfig.canopen.pdo_update_rate ?? 100);
    }

    // PID subsection
    if (halConfig.pid_params) {
      flat.hal_pid_kp = halConfig.pid_params.kp ?? 1.0;
      flat.hal_pid_ki = halConfig.pid_params.ki ?? 0.0;
      flat.hal_pid_kd = halConfig.pid_params.kd ?? 0.0;
      flat.hal_pid_integral_limit = halConfig.pid_params.integral_limit ?? 1000;
      flat.hal_pid_output_limit = halConfig.pid_params.output_limit ?? 1000;
      flat.hal_pid_anti_windup_gain = halConfig.pid_params.anti_windup_gain ?? 0.1;
      flat.hal_pid_anti_windup = halConfig.pid_params.enable_anti_windup !== false;
    }

    res.json(flat);
  } catch (err) {
    res.status(502).json({ error: 'HAL config fetch failed', details: err.message });
  }
});

/**
 * POST /api/hal/config
 * Updates HAL configuration from flat JSON (reverse of GET).
 */
app.post('/api/hal/config', async (req, res) => {
  try {
    const body = req.body;
    const halConfig = {};

    // Rebuild nested structure from flat keys
    if (body.hal_type !== undefined) halConfig.type = body.hal_type;
    if (body.hal_name !== undefined) halConfig.name = body.hal_name;

    // Gamepad
    const gp = {};
    let hasGp = false;
    if (body.hal_gamepad_device_path !== undefined) { gp.device_path = body.hal_gamepad_device_path; hasGp = true; }
    if (body.hal_gamepad_deadzone !== undefined) { gp.dead_zone = Number(body.hal_gamepad_deadzone); hasGp = true; }
    if (body.hal_gamepad_sensitivity !== undefined) { gp.sensitivity = Number(body.hal_gamepad_sensitivity); hasGp = true; }
    if (body.hal_gamepad_poll_interval_ms !== undefined) { gp.read_frequency = Number(body.hal_gamepad_poll_interval_ms); hasGp = true; }
    if (body.hal_gamepad_autostart !== undefined) { gp.autostart = body.hal_gamepad_autostart; hasGp = true; }
    if (hasGp) halConfig.gamepad = gp;

    // Simulated
    const sim = {};
    let hasSim = false;
    if (body.hal_simulated_enabled !== undefined) { sim.enable_simulation = body.hal_simulated_enabled; hasSim = true; }
    if (body.hal_simulated_update_rate !== undefined) { sim.simulation_update_rate = Number(body.hal_simulated_update_rate); hasSim = true; }
    if (body.hal_simulated_position_noise !== undefined) { sim.position_noise_stddev = Number(body.hal_simulated_position_noise); hasSim = true; }
    if (body.hal_simulated_velocity_noise !== undefined) { sim.velocity_noise_stddev = Number(body.hal_simulated_velocity_noise); hasSim = true; }
    if (body.hal_simulated_error_probability !== undefined) { sim.error_probability = Number(body.hal_simulated_error_probability); hasSim = true; }
    if (hasSim) halConfig.simulated = sim;

    // Safety
    const saf = {};
    let hasSaf = false;
    if (body.hal_safety_enable_limits !== undefined) { saf.enable_limits = body.hal_safety_enable_limits; hasSaf = true; }
    if (body.hal_safety_emergency_stop !== undefined) { saf.enable_emergency_stop = body.hal_safety_emergency_stop; hasSaf = true; }
    if (body.hal_safety_timeout_ms !== undefined) { saf.emergency_stop_timeout_ms = Number(body.hal_safety_timeout_ms); hasSaf = true; }
    if (body.hal_safety_temp_monitoring !== undefined) { saf.enable_temperature_monitoring = body.hal_safety_temp_monitoring; hasSaf = true; }
    if (body.hal_safety_current_monitoring !== undefined) { saf.enable_current_monitoring = body.hal_safety_current_monitoring; hasSaf = true; }
    if (body.hal_safety_voltage_monitoring !== undefined) { saf.enable_voltage_monitoring = body.hal_safety_voltage_monitoring; hasSaf = true; }
    if (body.hal_safety_min_voltage !== undefined) { saf.min_voltage = Number(body.hal_safety_min_voltage); hasSaf = true; }
    if (body.hal_safety_max_voltage !== undefined) { saf.max_voltage = Number(body.hal_safety_max_voltage); hasSaf = true; }
    if (body.hal_safety_monitoring_rate !== undefined) { saf.monitoring_rate = Number(body.hal_safety_monitoring_rate); hasSaf = true; }
    if (hasSaf) halConfig.safety = saf;

    // CanOpen
    const co = {};
    let hasCo = false;
    if (body.hal_canopen_library !== undefined) { co.library = body.hal_canopen_library; hasCo = true; }
    if (body.hal_canopen_interface !== undefined) { co.interface_name = body.hal_canopen_interface; hasCo = true; }
    if (body.hal_canopen_bitrate !== undefined) { co.bitrate = Number(body.hal_canopen_bitrate); hasCo = true; }
    if (body.hal_canopen_node_id !== undefined) { co.node_id = Number(body.hal_canopen_node_id); hasCo = true; }
    if (body.hal_canopen_sync !== undefined) { co.use_sync = body.hal_canopen_sync; hasCo = true; }
    if (body.hal_canopen_sync_period !== undefined) { co.sync_period_ms = Number(body.hal_canopen_sync_period); hasCo = true; }
    if (body.hal_canopen_sdo_timeout !== undefined) { co.sdo_timeout_ms = Number(body.hal_canopen_sdo_timeout); hasCo = true; }
    // Aliases for Settings UI "HAL" group (hal_can_* prefix)
    if (body.hal_can_interface !== undefined) { co.interface_name = body.hal_can_interface; hasCo = true; }
    if (body.hal_can_node_id !== undefined) { co.node_id = Number(body.hal_can_node_id); hasCo = true; }
    if (body.hal_can_baud_rate !== undefined) { co.bitrate = Number(body.hal_can_baud_rate); hasCo = true; }
    if (body.hal_heartbeat_interval_ms !== undefined) { co.sync_period_ms = Number(body.hal_heartbeat_interval_ms); hasCo = true; }
    if (body.hal_pdo_mapping_mode !== undefined) { co.pdo_update_rate = Number(body.hal_pdo_mapping_mode); hasCo = true; }
    if (hasCo) halConfig.canopen = co;

    // PID
    const pid = {};
    let hasPid = false;
    if (body.hal_pid_kp !== undefined) { pid.kp = Number(body.hal_pid_kp); hasPid = true; }
    if (body.hal_pid_ki !== undefined) { pid.ki = Number(body.hal_pid_ki); hasPid = true; }
    if (body.hal_pid_kd !== undefined) { pid.kd = Number(body.hal_pid_kd); hasPid = true; }
    if (body.hal_pid_integral_limit !== undefined) { pid.integral_limit = Number(body.hal_pid_integral_limit); hasPid = true; }
    if (body.hal_pid_output_limit !== undefined) { pid.output_limit = Number(body.hal_pid_output_limit); hasPid = true; }
    if (body.hal_pid_anti_windup_gain !== undefined) { pid.anti_windup_gain = Number(body.hal_pid_anti_windup_gain); hasPid = true; }
    if (body.hal_pid_anti_windup !== undefined) { pid.enable_anti_windup = body.hal_pid_anti_windup; hasPid = true; }
    if (hasPid) halConfig.pid_params = pid;

    await grpcCall('SetHALConfig', { config: halConfig });
    res.json({ success: true });
  } catch (err) {
    res.status(502).json({ error: 'HAL config update failed', details: err.message });
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

    if (typeof altitude !== 'number' || altitude < -90 || altitude > 90) {
      return res.status(400).json({ error: 'altitude must be in range [-90, 90]' });
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

// ─── Bootstrap Mode & Auto-Bootstrap (plan §6.1) ──────────────────────────

/**
* GET /api/calibration/bootstrap/mode
* Get the current bootstrap mode and encoder type.
* Maps to gRPC: GetBootstrapStatus()
*/
app.get('/api/calibration/bootstrap/mode', async (req, res) => {
  try {
      const status = await grpcCall('GetBootstrapStatus', {});
      res.json({
          mode: status.bootstrap_mode,
          encoder_type_absolute: status.encoder_type_absolute,
      });
  } catch (err) {
      res.status(502).json({ error: 'Failed to get bootstrap mode', details: err.message });
  }
});

/**
* POST /api/calibration/bootstrap/mode
* Set the bootstrap mode (MANUAL=0, HYBRID=1, AUTOMATIC=2).
* Maps to gRPC: SetBootstrapMode()
* Body: { mode: number }
*/
app.post('/api/calibration/bootstrap/mode', async (req, res) => {
  try {
      const { mode } = req.body;
      if (mode === undefined || ![0, 1, 2].includes(mode)) {
          return res.status(400).json({ error: 'mode must be 0 (MANUAL), 1 (HYBRID), or 2 (AUTOMATIC)' });
      }
      await grpcCall('SetBootstrapMode', { mode });
      res.json({ success: true, mode });
  } catch (err) {
      res.status(502).json({ error: 'Failed to set bootstrap mode', details: err.message });
  }
});

/**
* POST /api/calibration/bootstrap/auto-run
* Start automatic bootstrap procedure.
* Maps to gRPC: RunAutomaticBootstrap()
* Body (optional): { target_star_names?: string[], min_measurements?: number,
*                    max_alignment_error_arcsec?: number, proceed_to_tpoint?: boolean }
*/
app.post('/api/calibration/bootstrap/auto-run', async (req, res) => {
  try {
      const request = req.body || {};
      await grpcCall('RunAutomaticBootstrap', request);
      res.json({ success: true, message: 'Automatic bootstrap started' });
  } catch (err) {
      res.status(502).json({ error: 'Failed to start automatic bootstrap', details: err.message });
  }
});

/**
* GET /api/calibration/bootstrap/auto-status
* Get automatic bootstrap status and progress.
* Maps to gRPC: GetAutoBootstrapStatus()
*/
app.get('/api/calibration/bootstrap/auto-status', async (req, res) => {
  try {
      const status = await grpcCall('GetAutoBootstrapStatus', {});
      res.json(status);
  } catch (err) {
      res.status(502).json({ error: 'Failed to get auto-bootstrap status', details: err.message });
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
    if (constellation) request.constellation = constellation;

    const result = await dbGrpcCall('SearchObjects', request);

    // Safety post-filter: if the backend didn't filter by constellation (e.g. old binary),
    // apply client-side filtering on the returned page.
    if (constellation && result.objects && result.objects.length > 0) {
      const constelUpper = constellation.trim().toUpperCase();
      const filtered = result.objects.filter(obj => {
        if (!obj.custom_fields) return false;
        const cf = obj.custom_fields;
        let objConstel = '';
        if (typeof cf === 'object' && cf.constellation) {
          objConstel = cf.constellation.toUpperCase();
        }
        if (typeof cf === 'string') {
          const match = cf.toUpperCase().match(/CONSTELLATION:(\w+)/);
          if (match) objConstel = match[1];
        }
        return objConstel === constelUpper;
      });
      // Only apply post-filter if the backend returned items that don't match
      // (indicating the backend didn't have the constellation filter)
      if (filtered.length < result.objects.length) {
        result.objects = filtered;
        result.total_count = result.objects.length;
        const pageSize = parseInt(req.query.pageSize, 10) || 20;
        result.total_pages = Math.ceil(result.objects.length / pageSize) || 1;
      }
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
    position: {
      axis1: state.current_position?.axis1 ?? 0,
      axis2: state.current_position?.axis2 ?? 0,
    },
    telescope: {
      axis1: state.telescope_axis1 ?? 0,
      axis2: state.telescope_axis2 ?? 0,
    },
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
    actual_rate_axis1: state.actual_rate_axis1 ?? 0,
    actual_rate_axis2: state.actual_rate_axis2 ?? 0,
    pier_side: state.pier_side,
    meridian_flipped: state.meridian_flipped,
    time_to_meridian: state.time_to_meridian,
    temperature: state.temperature,
    pressure: state.pressure,
    humidity: state.humidity,
    pointing_error: state.pointing_error,
    tracking_performance: state.tracking_performance,
    bootstrap_calibrated: state.bootstrap_status?.calibrated || false,
    tpoint_calibrated: false, // TPOINT status not yet exposed in ControllerState proto
    bootstrap_status: state.bootstrap_status
      ? {
          calibrated: state.bootstrap_status.calibrated,
          measurement_count: state.bootstrap_status.measurement_count,
          bootstrap_mode: state.bootstrap_status.bootstrap_mode,
          encoder_type_absolute: state.bootstrap_status.encoder_type_absolute,
          reference_position_known: state.bootstrap_status.reference_position_known,
          state: state.bootstrap_status.state,
        }
      : null,
  };
}

// ─── Log Streaming (SSE) ─────────────────────────────────────────────────────

const LOG_DIR = config.logging.directory;
const LOG_FILE = path.join(LOG_DIR, config.logging.fileName);
let sseClients = [];
let logWatchInterval = null;
let lastKnownSize = 0;
let logReadPosition = 0;

/**
 * Get the full path to the current log file.
 * If the configured file doesn't exist, try to find any .log file.
 */
function getLogFilePath() {
  if (fs.existsSync(LOG_FILE)) return LOG_FILE;
  try {
    if (fs.existsSync(LOG_DIR)) {
      const files = fs.readdirSync(LOG_DIR)
        .filter(f => f.endsWith('.log'))
        .sort();
      if (files.length > 0) return path.join(LOG_DIR, files[files.length - 1]);
    }
  } catch (_) { /* ignore */ }
  return LOG_FILE;
}

/**
 * Read new log entries since the last read position.
 * Returns an array of { timestamp, level, message } objects.
 */
function readNewLogEntries() {
  const filePath = getLogFilePath();
  if (!fs.existsSync(filePath)) return [];

  try {
    const stats = fs.statSync(filePath);
    if (stats.size <= logReadPosition) {
      // File may have been rotated — reset position
      if (stats.size < logReadPosition) {
        logReadPosition = 0;
      }
      return [];
    }

    const fd = fs.openSync(filePath, 'r');
    const buffer = Buffer.alloc(stats.size - logReadPosition);
    fs.readSync(fd, buffer, 0, buffer.length, logReadPosition);
    fs.closeSync(fd);

    logReadPosition = stats.size;
    const text = buffer.toString('utf-8');
    return parseLogLines(text);
  } catch (err) {
    console.error('[LogStream] Error reading log file:', err.message);
    return [];
  }
}

/**
 * Parse raw log lines into structured entries.
 * Handles spdlog default format: [2024-01-01 12:34:56.789] [level] message
 */
function parseLogLines(text) {
  const entries = [];
  const lines = text.split('\n');

  for (const line of lines) {
    const trimmed = line.trim();
    if (!trimmed) continue;

    // Try to parse spdlog format: [timestamp] [level] [name] message
    // The pattern is: [%Y-%m-%d %H:%M:%S.%e] [%^%l%$] [%n] %v
    const match = trimmed.match(/^\[(.+?)\]\s+\[(.+?)\]\s+\[(.+?)\]\s+(.*)/);
    if (match) {
      entries.push({
        timestamp: match[1],
        level: match[2].toLowerCase(),
        message: match[4],
      });
    } else {
      // Fallback: treat entire line as message
      entries.push({
        timestamp: new Date().toISOString(),
        level: 'info',
        message: trimmed,
      });
    }
  }
  return entries;
}

/**
 * Broadcast an event to all connected SSE clients.
 */
function broadcastSSE(event, data) {
  const payload = `event: ${event}\ndata: ${JSON.stringify(data)}\n\n`;
  sseClients = sseClients.filter(client => {
    try {
      client.write(payload);
      return true;
    } catch (_) {
      return false;
    }
  });
}

/**
 * Poll log file for changes and broadcast new entries.
 */
function pollLogFile() {
  try {
    const filePath = getLogFilePath();
    if (!fs.existsSync(filePath)) return;

    const stats = fs.statSync(filePath);
    if (stats.size === lastKnownSize) return;
    lastKnownSize = stats.size;

    const entries = readNewLogEntries();
    if (entries.length > 0) {
      broadcastSSE('log', entries);
    }
  } catch (err) {
    console.error('[LogStream] Poll error:', err.message);
  }
}

/**
 * Start polling the log file for changes.
 */
function startLogPolling(initialPosition) {
  if (logWatchInterval) return;
  // Only initialize positions if not already set by the SSE init handler
  if (initialPosition !== undefined) {
    logReadPosition = initialPosition;
    lastKnownSize = initialPosition;
  } else if (logReadPosition === 0 && lastKnownSize === 0) {
    const filePath = getLogFilePath();
    if (fs.existsSync(filePath)) {
      logReadPosition = fs.statSync(filePath).size;
      lastKnownSize = logReadPosition;
    }
  }
  logWatchInterval = setInterval(pollLogFile, 500);
}

/**
 * Stop polling the log file.
 */
function stopLogPolling() {
  if (logWatchInterval) {
    clearInterval(logWatchInterval);
    logWatchInterval = null;
  }
}

/**
 * GET /api/logs
 * Returns recent log entries (last N lines from the log file).
 */
app.get('/api/logs', (req, res) => {
  try {
    const maxLines = parseInt(req.query.lines, 10) || 200;
    const filePath = getLogFilePath();

    if (!fs.existsSync(filePath)) {
      return res.json({ logs: [], file: filePath });
    }

    const content = fs.readFileSync(filePath, 'utf-8');
    const allEntries = parseLogLines(content);
    const recent = allEntries.slice(-maxLines);

    res.json({ logs: recent, file: filePath });
  } catch (err) {
    res.status(500).json({ error: 'Failed to read log file', details: err.message });
  }
});

/**
 * GET /api/logs/stream
 * Server-Sent Events endpoint for real-time log streaming.
 */
app.get('/api/logs/stream', (req, res) => {
  res.writeHead(200, {
    'Content-Type': 'text/event-stream',
    'Cache-Control': 'no-cache',
    'Connection': 'keep-alive',
    'X-Accel-Buffering': 'no',
  });

  // Send initial keepalive
  res.write(':ok\n\n');

  // Send recent logs on connect
  try {
    const filePath = getLogFilePath();
    if (fs.existsSync(filePath)) {
      const content = fs.readFileSync(filePath, 'utf-8');
      const allEntries = parseLogLines(content);
      const recent = allEntries.slice(-100);
      res.write(`event: init\ndata: ${JSON.stringify({ logs: recent })}\n\n`);
      // Record file position AFTER reading init data, BEFORE starting polling
      // to prevent missing entries written between init read and poll start
      const stats = fs.statSync(filePath);
      logReadPosition = stats.size;
      lastKnownSize = stats.size;
    }
  } catch (_) { /* ignore */ }

  sseClients.push(res);

  // Start polling if not already running
  startLogPolling();

  // Send periodic keepalive to prevent proxy timeouts
  const keepAlive = setInterval(() => {
    try {
      res.write(':keepalive\n\n');
    } catch (_) {
      clearInterval(keepAlive);
    }
  }, 15000);

  req.on('close', () => {
    clearInterval(keepAlive);
    sseClients = sseClients.filter(c => c !== res);
    if (sseClients.length === 0) {
      stopLogPolling();
    }
  });
});

// ─── Field Rotation & Derotator REST API Endpoints ───────────────────────────

/**
 * GET /api/derotator/status
 * Get current derotator status (enabled, homed, position, rates).
 * Maps to gRPC: GetDerotatorStatus()
 */
app.get('/api/derotator/status', async (req, res) => {
  try {
    const status = await grpcCall('GetDerotatorStatus', {});
    res.json(status);
  } catch (err) {
    res.status(502).json({ error: 'Failed to get derotator status', details: err.message });
  }
});

/**
 * GET /api/field-rotation/params
 * Get current field rotation parameters (rate, enabled, altitude/azimuth).
 * Maps to gRPC: GetFieldRotationParams()
 */
app.get('/api/field-rotation/params', async (req, res) => {
  try {
    const params = await grpcCall('GetFieldRotationParams', {});
    res.json(params);
  } catch (err) {
    res.status(502).json({ error: 'Failed to get field rotation params', details: err.message });
  }
});

/**
 * POST /api/field-rotation/enable
 * Enable or disable field rotation compensation.
 * Maps to gRPC: EnableFieldRotation()
 * Body: FieldRotationParams (enabled, latitude, altitude, azimuth, etc.)
 */
app.post('/api/field-rotation/enable', async (req, res) => {
  try {
    const params = req.body;
    if (params.enabled === undefined) {
      return res.status(400).json({ error: 'Missing required field: enabled' });
    }
    await grpcCall('EnableFieldRotation', params);
    res.json({ success: true, message: params.enabled ? 'Field rotation enabled' : 'Field rotation disabled' });
  } catch (err) {
    res.status(502).json({ error: 'Failed to toggle field rotation', details: err.message });
  }
});


// ─── Gamepad State Endpoint ──────────────────────────────────────────────────

/**
 * GET /api/hal/gamepad/state
 * Returns the current gamepad/joystick state (axes, buttons, connection status).
 * Maps to gRPC: GetHALStatus() with gamepad subsection when available.
 *
 * When the gRPC backend has a dedicated GetGamepadState RPC, this endpoint
 * will forward the call. For now, it returns a default/empty state structure
 * matching the GamepadState proto.
 */
app.get('/api/hal/gamepad/state', async (req, res) => {
  try {
    // Attempt to get gamepad state from the HAL status gRPC call.
    // If the backend has gamepad data, it will be included.
    const halStatus = await grpcCall('GetHALStatus', {});

    // Build gamepad state from HAL status if available,
    // otherwise return a zero/default state.
    const gamepadState = {
      connected: halStatus.gamepad ? (halStatus.gamepad.connected || false) : false,
      device_name: halStatus.gamepad ? (halStatus.gamepad.device_name || '') : '',
      // Analog axes (normalized to [-1.0, 1.0])
      axis_lx: halStatus.gamepad ? (halStatus.gamepad.axis_lx || 0.0) : 0.0,
      axis_ly: halStatus.gamepad ? (halStatus.gamepad.axis_ly || 0.0) : 0.0,
      axis_rx: halStatus.gamepad ? (halStatus.gamepad.axis_rx || 0.0) : 0.0,
      axis_ry: halStatus.gamepad ? (halStatus.gamepad.axis_ry || 0.0) : 0.0,
      // Analog triggers (range [-1.0, 1.0]; 0.0 = released)
      axis_trigger_l: halStatus.gamepad ? (halStatus.gamepad.axis_trigger_l || 0.0) : 0.0,
      axis_trigger_r: halStatus.gamepad ? (halStatus.gamepad.axis_trigger_r || 0.0) : 0.0,
      // D-Pad / POV hat (in degrees, -1.0 = neutral)
      pov_hat: halStatus.gamepad ? (halStatus.gamepad.pov_hat !== undefined ? halStatus.gamepad.pov_hat : -1.0) : -1.0,
      // Semantic buttons
      button_stop: halStatus.gamepad ? (halStatus.gamepad.button_stop || false) : false,
      button_emergency_stop: halStatus.gamepad ? (halStatus.gamepad.button_emergency_stop || false) : false,
      button_park: halStatus.gamepad ? (halStatus.gamepad.button_park || false) : false,
      button_speed_up: halStatus.gamepad ? (halStatus.gamepad.button_speed_up || false) : false,
      button_speed_down: halStatus.gamepad ? (halStatus.gamepad.button_speed_down || false) : false,
      button_manual_toggle: halStatus.gamepad ? (halStatus.gamepad.button_manual_toggle || false) : false,
      button_home: halStatus.gamepad ? (halStatus.gamepad.button_home || false) : false,
      // Axis and button counts
      axis_count: halStatus.gamepad ? (halStatus.gamepad.axis_count || 0) : 0,
      button_count: halStatus.gamepad ? (halStatus.gamepad.button_count || 0) : 0,
      // Navigation mode and calibration status
      gamepad_mode: halStatus.gamepad ? (halStatus.gamepad.gamepad_mode !== undefined ? halStatus.gamepad.gamepad_mode : 0) : 0,
      bootstrap_calibrated: halStatus.gamepad ? (halStatus.gamepad.bootstrap_calibrated || false) : false,
      tpoint_calibrated: halStatus.gamepad ? (halStatus.gamepad.tpoint_calibrated || false) : false,
      max_velocity: halStatus.gamepad ? (halStatus.gamepad.max_velocity || 5.0) : 5.0,
    };

    res.json(gamepadState);
  } catch (err) {
    // If the gRPC call fails (e.g., GetHALStatus not implemented yet),
    // return a default disconnected state so the UI can still render.
    res.json({
      connected: false,
      device_name: '',
      axis_lx: 0.0, axis_ly: 0.0, axis_rx: 0.0, axis_ry: 0.0,
      axis_trigger_l: 0.0, axis_trigger_r: 0.0,
      pov_hat: -1.0,
      button_stop: false, button_emergency_stop: false,
      button_park: false, button_speed_up: false,
      button_speed_down: false, button_manual_toggle: false,
      button_home: false,
      axis_count: 0, button_count: 0,
      max_velocity: 5.0,
    });
  }
});

/**
 * POST /api/hal/gamepad/start
 * Starts the gamepad manual-control loop (axis velocity commands).
 * The gamepad input device must already be open (auto-opened on startup).
 */
app.post('/api/hal/gamepad/start', async (req, res) => {
  try {
    await grpcCall('StartGamepad', {});
    res.json({ success: true, message: 'Gamepad control loop started' });
  } catch (err) {
    res.status(500).json({ error: err.message });
  }
});

/**
 * POST /api/hal/gamepad/stop
 * Stops the gamepad manual-control loop.
 */
app.post('/api/hal/gamepad/stop', async (req, res) => {
  try {
    await grpcCall('StopGamepad', {});
    res.json({ success: true, message: 'Gamepad control loop stopped' });
  } catch (err) {
    res.status(500).json({ error: err.message });
  }
});

/**
 * POST /api/hal/gamepad/mode
 * Sets the gamepad navigation mode (RAW=0, CELESTIAL=1, ALT_AZ=2).
 * Body: { mode: 0|1|2 }
 */
app.post('/api/hal/gamepad/mode', async (req, res) => {
  try {
    const mode = parseInt(req.body.mode, 10);
    if (isNaN(mode) || mode < 0 || mode > 2) {
      return res.status(400).json({ error: 'Invalid mode. Must be 0 (RAW), 1 (CELESTIAL), or 2 (ALT_AZ).' });
    }
    await grpcCall('SetGamepadMode', { mode: mode });
    res.json({ success: true, message: 'Gamepad mode set to ' + mode });
  } catch (err) {
    res.status(500).json({ error: err.message });
  }
});


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
