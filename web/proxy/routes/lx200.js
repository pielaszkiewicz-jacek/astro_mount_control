/**
 * LX200 Serial Interface Routes
 */
'use strict';

const express = require('express');
const router = express.Router();
const { grpcCall } = require('../grpc/client');
const { errorResponse } = require('../grpc/converters');

/**
 * GET /api/lx200/status
 * Returns the LX200 server status.
 */
router.get('/status', async (req, res) => {
  try {
    const status = await grpcCall('GetLx200Status', {});
    res.json({
      enabled: status.enabled || false,
      running: status.running || false,
      port: status.port || '',
      baud_rate: status.baud_rate || 9600,
    });
  } catch (err) {
    errorResponse(res, 503, 'LX200 server unreachable', err.message);
  }
});

/**
 * POST /api/lx200/start
 * Start the LX200 serial server.
 */
router.post('/start', async (req, res) => {
  try {
    const status = await grpcCall('StartLx200', {});
    res.json({
      success: status.running || false,
      running: status.running || false,
      port: status.port || '',
      baud_rate: status.baud_rate || 9600,
    });
  } catch (err) {
    errorResponse(res, 502, 'Failed to start LX200 server', err.message);
  }
});

/**
 * POST /api/lx200/stop
 * Stop the LX200 serial server.
 */
router.post('/stop', async (req, res) => {
  try {
    const status = await grpcCall('StopLx200', {});
    res.json({
      success: true,
      running: status.running || false,
      port: status.port || '',
      baud_rate: status.baud_rate || 9600,
    });
  } catch (err) {
    errorResponse(res, 502, 'Failed to stop LX200 server', err.message);
  }
});

module.exports = router;
