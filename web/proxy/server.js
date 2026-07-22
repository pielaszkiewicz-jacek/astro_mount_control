/**
 * Astronomical Mount Controller - Web Proxy Server
 *
 * HTTP/JSON proxy that bridges web applications to the gRPC backend.
 * Exposes RESTful endpoints consumed by the SPA frontend.
 *
 * Architecture:
 *   Browser (SPA) → HTTP/JSON → Proxy Server → gRPC → Mount Controller
 *
 * This file is the slim bootstrap (~200 lines). All route logic,
 * gRPC client management, and converters are in separate modules.
 */

'use strict';

const express = require('express');
const cors = require('cors');
const morgan = require('morgan');
const path = require('path');
const config = require('./config');
const { createGrpcClient, createDbGrpcClient } = require('./grpc/client');
const errorHandler = require('./middleware/errorHandler');

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
  setHeaders: (res) => {
    res.setHeader('Cache-Control', 'no-store, no-cache, must-revalidate, proxy-revalidate');
    res.setHeader('Pragma', 'no-cache');
    res.setHeader('Expires', '0');
  },
}));

// ─── Route Registration ──────────────────────────────────────────────────────

app.use('/api', require('./routes/mount'));
app.use('/api/axis', require('./routes/axis'));
app.use('/api/calibration', require('./routes/calibration'));
app.use('/api/tracking', require('./routes/tracking'));
app.use('/api/config', require('./routes/config'));
app.use('/api/state', require('./routes/state'));
app.use('/api/db', require('./routes/database'));
app.use('/api/db', require('./routes/health'));
app.use('/api/logs', require('./routes/logs'));

// ─── Error Handling ──────────────────────────────────────────────────────────

app.use(errorHandler);

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
  try {
    const gClient = require('./grpc/client');
    try { gClient.getGrpcClient().close(); } catch (e) { /* not initialized */ }
    try { gClient.getDbGrpcClient().close(); } catch (e) { /* not initialized */ }
  } catch (e) {
    // Ignore module loading errors during shutdown
  }
  process.exit(0);
});

process.on('SIGTERM', () => {
  console.log('\n[Shutdown] Received SIGTERM, closing gRPC clients...');
  try {
    const gClient = require('./grpc/client');
    try { gClient.getGrpcClient().close(); } catch (e) { /* not initialized */ }
    try { gClient.getDbGrpcClient().close(); } catch (e) { /* not initialized */ }
  } catch (e) {
    // Ignore module loading errors during shutdown
  }
  process.exit(0);
});
