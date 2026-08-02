/**
 * Astro Mount Controller - Proxy Server Configuration
 *
 * Single source of truth for all configuration values,
 * loaded from environment variables with sensible defaults.
 */
'use strict';

require('dotenv').config();

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
  dome: {
    host: process.env.DOME_GRPC_HOST || '127.0.0.1',
    port: parseInt(process.env.DOME_GRPC_PORT, 10) || 50053,
  },
  derotator: {
    host: process.env.DEROTATOR_GRPC_HOST || '127.0.0.1',
    port: parseInt(process.env.DEROTATOR_GRPC_PORT, 10) || 50054,
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

// External service integration flags — controls which tabs are shown in the UI.
// Set these via environment variables (EXT_SERVICE_*) or leave disabled by default.
config.external_services = {
  dome:       process.env.EXT_SERVICE_DOME === 'true',
  derotator:  process.env.EXT_SERVICE_DEROTATOR === 'true',
  weather:    process.env.EXT_SERVICE_WEATHER === 'true',
  power:      process.env.EXT_SERVICE_POWER === 'true',
  sequencer:  process.env.EXT_SERVICE_SEQUENCER === 'true',
  focuser:    process.env.EXT_SERVICE_FOCUSER === 'true',
};

module.exports = config;
