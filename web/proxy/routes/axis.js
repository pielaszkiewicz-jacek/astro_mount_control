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
    const { axis, velocity, acceleration } = req.body;

    if (axis === undefined || velocity === undefined) {
      return errorResponse(res, 400, 'Missing required fields: axis, velocity');
    }
    if (typeof axis !== 'number' || axis < 0 || axis > 1) {
      return errorResponse(res, 400, 'Axis must be 0 or 1');
    }
    if (typeof velocity !== 'number' || Math.abs(velocity) > 10000) {
      return errorResponse(res, 400, 'Velocity must be a number in range [-10000, 10000]');
    }

    await grpcCall('ControlAxis', {
      axis_id: axis,
      mode: 1,                    // VELOCITY_CONTROL
      target_velocity: velocity,
      acceleration: acceleration || 0
    });
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
    const { axis, deceleration } = req.body;

    if (axis === undefined) {
      return errorResponse(res, 400, 'Missing required field: axis');
    }

    await grpcCall('StopAxis', {
      axis_id: axis,
      decelerate: true,
      deceleration: deceleration || 50
    });
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

/**
 * POST /api/axis/move-relative
 * Move axis by a relative offset using position control.
 * Body: { axis: number, offset_deg: number, velocity?: number, acceleration?: number, deceleration?: number }
 */
router.post('/move-relative', async (req, res) => {
  try {
    const { axis, offset_deg, velocity, acceleration, deceleration } = req.body;

    if (axis === undefined || offset_deg === undefined) {
      return errorResponse(res, 400, 'Missing required fields: axis, offset_deg');
    }
    if (typeof axis !== 'number' || axis < 0 || axis > 1) {
      return errorResponse(res, 400, 'Axis must be 0 or 1');
    }

    await grpcCall('ControlAxis', {
      axis_id: axis,
      mode: 0,                        // POSITION_CONTROL
      target_position: offset_deg,
      max_velocity: velocity || 0,
      acceleration: acceleration || 0,
      relative: true
    });
    res.json({ success: true, message: `Axis ${axis} moved by ${offset_deg}°` });
  } catch (err) {
    errorResponse(res, 502, 'Step move failed', err.message);
  }
});

/**
 * POST /api/axis/move-absolute
 * Move axis to an absolute position using position control.
 * Body: { axis: number, target_deg: number, velocity?: number, acceleration?: number, deceleration?: number }
 */
router.post('/move-absolute', async (req, res) => {
  try {
    const { axis, target_deg, velocity, acceleration, deceleration } = req.body;

    if (axis === undefined || target_deg === undefined) {
      return errorResponse(res, 400, 'Missing required fields: axis, target_deg');
    }
    if (typeof axis !== 'number' || axis < 0 || axis > 1) {
      return errorResponse(res, 400, 'Axis must be 0 or 1');
    }

    await grpcCall('ControlAxis', {
      axis_id: axis,
      mode: 0,                        // POSITION_CONTROL
      target_position: target_deg,
      max_velocity: velocity || 0,
      acceleration: acceleration || 0,
      relative: false
    });
    res.json({ success: true, message: `Axis ${axis} moved to ${target_deg}°` });
  } catch (err) {
    errorResponse(res, 502, 'Absolute move failed', err.message);
  }
});

module.exports = router;
