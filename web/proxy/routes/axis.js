/**
 * Axis Control Routes — Move, Stop, Emergency Stop
 */
'use strict';

const express = require('express');
const router = express.Router();
const { grpcCall } = require('../grpc/client');
const { errorResponse } = require('../grpc/converters');

/**
 * POST /api/axis/move
 * Move a specific axis.
 * Body: { axis: number, velocity: number }
 */
router.post('/move', async (req, res) => {
  try {
    const { axis, velocity } = req.body;

    if (axis === undefined || velocity === undefined) {
      return errorResponse(res, 400, 'Missing required fields: axis, velocity');
    }
    if (typeof axis !== 'number' || axis < 0 || axis > 1) {
      return errorResponse(res, 400, 'Axis must be 0 or 1');
    }
    if (typeof velocity !== 'number' || Math.abs(velocity) > 10) {
      return errorResponse(res, 400, 'Velocity must be a number in range [-10, 10]');
    }

    await grpcCall('ControlAxis', { axis, velocity });
    res.json({ success: true, message: `Axis ${axis} moving at ${velocity}°/s` });
  } catch (err) {
    errorResponse(res, 502, 'Axis move failed', err.message);
  }
});

/**
 * POST /api/axis/stop
 * Stop a specific axis.
 * Body: { axis: number }
 */
router.post('/stop', async (req, res) => {
  try {
    const { axis } = req.body;

    if (axis === undefined) {
      return errorResponse(res, 400, 'Missing required field: axis');
    }

    await grpcCall('StopAxis', { axis });
    res.json({ success: true, message: `Axis ${axis} stopped` });
  } catch (err) {
    errorResponse(res, 502, 'Axis stop failed', err.message);
  }
});

/**
 * POST /api/axis/emergency-stop
 * Emergency stop all axes.
 */
router.post('/emergency-stop', async (req, res) => {
  try {
    await grpcCall('EmergencyStop', {});
    res.json({ success: true, message: 'Emergency stop activated' });
  } catch (err) {
    errorResponse(res, 502, 'Emergency stop failed', err.message);
  }
});

module.exports = router;
