/**
 * Focuser Control Routes
 *
 * Proxies focuser operations to the backend FocuserService gRPC service.
 * Falls back to simulated data when the service is unavailable.
 */
'use strict';

const express = require('express');
const router = express.Router();
const { grpcCall } = require('../grpc/client');
const { errorResponse } = require('../grpc/converters');

/**
 * POST /api/focuser/move
 * Move focuser to absolute position.
 * Body: { position: number, speed?: number }
 */
router.post('/move', async (req, res) => {
  try {
    const { position, speed } = req.body;
    await grpcCall('MoveFocuser', {
      position: parseInt(position) || 0,
      speed: parseInt(speed) || 50,
    });
    res.json({ success: true, message: `Focuser moving to position ${position}` });
  } catch (err) {
    errorResponse(res, 502, 'Failed to move focuser', err.message);
  }
});

/**
 * POST /api/focuser/halt
 * Halt focuser motion.
 */
router.post('/halt', async (req, res) => {
  try {
    await grpcCall('HaltFocuser', {});
    res.json({ success: true, message: 'Focuser halted' });
  } catch (err) {
    errorResponse(res, 502, 'Failed to halt focuser', err.message);
  }
});

/**
 * POST /api/focuser/autofocus
 * Run auto-focus routine.
 * Body: { start_position, end_position, step_size }
 */
router.post('/autofocus', async (req, res) => {
  try {
    const { start_position, end_position, step_size } = req.body;
    await grpcCall('RunAutoFocus', {
      start_position: parseInt(start_position) || 0,
      end_position: parseInt(end_position) || 100000,
      step_size: parseInt(step_size) || 500,
    });
    res.json({ success: true, message: 'Auto-focus started' });
  } catch (err) {
    errorResponse(res, 502, 'Failed to start auto-focus', err.message);
  }
});

/**
 * GET /api/focuser/status
 * Returns current focuser status.
 */
router.get('/status', async (req, res) => {
  try {
    const status = await grpcCall('GetFocuserPosition', {});
    res.json({
      position: status.position || 0,
      max_position: status.max_position || 100000,
      moving: status.moving || false,
      connected: status.connected || false,
      temperature_c: status.temperature_c || 0,
      hfd: status.hfd || 0,
      model_name: status.model_name || '',
      error_message: status.error_message || '',
    });
  } catch (err) {
    res.json({
      position: 50000,
      max_position: 100000,
      moving: false,
      connected: false,
      temperature_c: 20,
      hfd: 0,
      model_name: '',
      error_message: '',
    });
  }
});

module.exports = router;
