/**
 * Configuration Routes — Get/Update addresses and settings
 */
'use strict';

const express = require('express');
const router = express.Router();
const config = require('../config');
const { createGrpcClient, createDbGrpcClient } = require('../grpc/client');
const { errorResponse } = require('../grpc/converters');

/**
 * GET /api/config/addresses
 * Returns the current gRPC addresses for mount controller and database.
 */
router.get('/addresses', (req, res) => {
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
router.post('/addresses', async (req, res) => {
  try {
    const { controller, database } = req.body;
    const reconnected = [];

    if (controller) {
      const host = controller.host || config.grpc.host;
      const port = controller.port || config.grpc.port;
      const ssl = controller.ssl !== undefined ? controller.ssl : config.ssl.enabled;

      if (typeof host !== 'string' || host.length === 0) {
        return errorResponse(res, 400, 'Invalid controller host');
      }
      if (typeof port !== 'number' || port < 1 || port > 65535) {
        return errorResponse(res, 400, 'Invalid controller port (1-65535)');
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
        return errorResponse(res, 400, 'Invalid database host');
      }
      if (typeof port !== 'number' || port < 1 || port > 65535) {
        return errorResponse(res, 400, 'Invalid database port (1-65535)');
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
    errorResponse(res, 500, 'Failed to update addresses', err.message);
  }
});

module.exports = router;
