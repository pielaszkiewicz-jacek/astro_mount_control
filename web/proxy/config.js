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
    // Dome is hosted IN-PROCESS on the mount controller's unified gRPC port.
    // Default to the mount controller port (50051) so the web UI works out of
    // the box. Override with DOME_GRPC_HOST/DOME_GRPC_PORT when running the
    // standalone astro_dome_server instead.
    host: process.env.DOME_GRPC_HOST || '127.0.0.1',
    port: parseInt(process.env.DOME_GRPC_PORT, 10) || 50051,
  },
  derotator: {
    // Derotator is hosted IN-PROCESS on the mount controller's unified gRPC
    // port. Default to the mount controller port (50051). Override when using
    // the standalone astro_derotator_server.
    host: process.env.DEROTATOR_GRPC_HOST || '127.0.0.1',
    port: parseInt(process.env.DEROTATOR_GRPC_PORT, 10) || 50051,
  },
  weather: {
    // Separate process — astro_weather_server (default 50055).
    host: process.env.WEATHER_GRPC_HOST || '127.0.0.1',
    port: parseInt(process.env.WEATHER_GRPC_PORT, 10) || 50055,
  },
  power: {
    // Separate process — astro_power_server (default 50056).
    host: process.env.POWER_GRPC_HOST || '127.0.0.1',
    port: parseInt(process.env.POWER_GRPC_PORT, 10) || 50056,
  },
  sequencer: {
    // Separate process — astro_sequencer_server (default 50057).
    host: process.env.SEQUENCER_GRPC_HOST || '127.0.0.1',
    port: parseInt(process.env.SEQUENCER_GRPC_PORT, 10) || 50057,
  },
  focuser: {
    // Focuser is hosted IN-PROCESS on the mount controller's unified gRPC port
    // (50051). Override when using the standalone astro_focuser_server.
    host: process.env.FOCUSER_GRPC_HOST || '127.0.0.1',
    port: parseInt(process.env.FOCUSER_GRPC_PORT, 10) || 50051,
  },
  guider: {
    // ST4 guider is hosted IN-PROCESS on the mount controller's unified gRPC
    // port (50051) — Phase 2.
    host: process.env.GUIDER_GRPC_HOST || '127.0.0.1',
    port: parseInt(process.env.GUIDER_GRPC_PORT, 10) || 50051,
  },
  pec: {
    // PEC is hosted IN-PROCESS on the mount controller's unified gRPC port
    // (50051) — Phase 2.
    host: process.env.PEC_GRPC_HOST || '127.0.0.1',
    port: parseInt(process.env.PEC_GRPC_PORT, 10) || 50051,
  },
  camera: {
    // Camera is hosted IN-PROCESS on the mount controller's unified gRPC port
    // (50051) — R3 (simulated).
    host: process.env.CAMERA_GRPC_HOST || '127.0.0.1',
    port: parseInt(process.env.CAMERA_GRPC_PORT, 10) || 50051,
  },
  pulley: {
    // Pulley is hosted IN-PROCESS on the mount controller's unified gRPC port
    // (50051) — R3 (simulated).
    host: process.env.PULLEY_GRPC_HOST || '127.0.0.1',
    port: parseInt(process.env.PULLEY_GRPC_PORT, 10) || 50051,
  },
  notifications: {
    // Notification service is hosted IN-PROCESS on the mount controller's
    // unified gRPC port (50051) — R1.
    host: process.env.NOTIFICATIONS_GRPC_HOST || '127.0.0.1',
    port: parseInt(process.env.NOTIFICATIONS_GRPC_PORT, 10) || 50051,
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
  derotator:  process.env.EXT_SERVICE_DEROTATOR !== 'false',   // in-process, enabled by default
  weather:    process.env.EXT_SERVICE_WEATHER === 'true',
  power:      process.env.EXT_SERVICE_POWER === 'true',
  sequencer:  process.env.EXT_SERVICE_SEQUENCER === 'true',
  focuser:    process.env.EXT_SERVICE_FOCUSER !== 'false',     // in-process, enabled by default
  guider:     process.env.EXT_SERVICE_GUIDER !== 'false',      // in-process, enabled by default
  pec:        process.env.EXT_SERVICE_PEC !== 'false',         // in-process, enabled by default
  camera:     process.env.EXT_SERVICE_CAMERA !== 'false',      // in-process, enabled by default (R3)
  pulley:     process.env.EXT_SERVICE_PULLEY !== 'false',      // in-process, enabled by default (R3)
  notifications: process.env.EXT_SERVICE_NOTIFICATIONS !== 'false',  // in-process, enabled by default
};

module.exports = config;
