/**
 * Pulley Controller Routes
 *
 * Proxies pulley operations to the backend PulleyService gRPC service.
 * Falls back to simulated data when the service is unavailable.
 */
'use strict';

const express = require('express');
const router = express.Router();
const { grpcCall } = require('../grpc/client');
const { errorResponse } = require('../grpc/converters');

/**
 * POST /api/pulley/deploy
 * Deploy (extend) the pulley to a target position.
 * Body: { position_percent, speed_percent? }
 */
router.post('/deploy', async (req, res) => {
  try {
    const { position_percent, speed_percent } = req.body;
    await grpcCall('DeployPulley', {
      position_percent: parseInt(position_percent) || 100,
      speed_percent: parseInt(speed_percent) || 50,
    });
    res.json({ success: true, message: `Pulley deploying to ${position_percent}%` });
  } catch (err) {
    errorResponse(res, 502, 'Failed to deploy pulley', err.message);
  }
});

/**
 * POST /api/pulley/retract
 * Retract (rewind) the pulley fully.
 */
router.post('/retract', async (req, res) => {
  try {
    await grpcCall('RetractPulley', {});
    res.json({ success: true, message: 'Pulley retracting' });
  } catch (err) {
    errorResponse(res, 502, 'Failed to retract pulley', err.message);
  }
});

/**
 * POST /api/pulley/stop
 * Stop pulley motion immediately.
 */
router.post('/stop', async (req, res) => {
  try {
    await grpcCall('StopPulley', {});
    res.json({ success: true, message: 'Pulley stopped' });
  } catch (err) {
    errorResponse(res, 502, 'Failed to stop pulley', err.message);
  }
});

/**
 * POST /api/pulley/position
 * Move pulley to a specific absolute position [0..100%].
 * Body: { position_percent, speed_percent? }
 */
router.post('/position', async (req, res) => {
  try {
    const { position_percent, speed_percent } = req.body;
    await grpcCall('SetPulleyPosition', {
      position_percent: parseInt(position_percent) || 0,
      speed_percent: parseInt(speed_percent) || 50,
    });
    res.json({ success: true, message: `Pulley moving to ${position_percent}%` });
  } catch (err) {
    errorResponse(res, 502, 'Failed to set pulley position', err.message);
  }
});

/**
 * POST /api/pulley/home
 * Home/calibrate the pulley.
 */
router.post('/home', async (req, res) => {
  try {
    await grpcCall('HomePulley', {});
    res.json({ success: true, message: 'Pulley homing started' });
  } catch (err) {
    errorResponse(res, 502, 'Failed to home pulley', err.message);
  }
});

/**
 * POST /api/pulley/speed
 * Set pulley speed limit.
 * Body: { speed_percent: number }
 */
router.post('/speed', async (req, res) => {
  try {
    const { speed_percent } = req.body;
    await grpcCall('SetPulleySpeed', { speed_percent: parseInt(speed_percent) || 50 });
    res.json({ success: true, message: `Pulley speed set to ${speed_percent}%` });
  } catch (err) {
    errorResponse(res, 502, 'Failed to set pulley speed', err.message);
  }
});

/**
 * GET /api/pulley/status
 * Returns current pulley status.
 */
router.get('/status', async (req, res) => {
  try {
    const status = await grpcCall('GetPulleyStatus', {});
    res.json({
      position_percent: status.position_percent || 0,
      target_position_percent: status.target_position_percent || 0,
      moving: status.moving || false,
      deployed: status.deployed || false,
      homed: status.homed || false,
      speed_percent: status.speed_percent || 50,
      at_upper_limit: status.at_upper_limit || false,
      at_lower_limit: status.at_lower_limit || false,
      overload: status.overload || false,
      connected: status.connected || false,
      model_name: status.model_name || '',
      motor_current_a: status.motor_current_a || 0,
      temperature_c: status.temperature_c || 0,
      error: status.error || false,
      error_message: status.error_message || '',
      max_position_steps: status.max_position_steps || 0,
      travel_time_s: status.travel_time_s || 0,
      deployment_type: status.deployment_type || 'generic',
    });
  } catch (err) {
    // Simulated pulley status when gRPC unavailable
    res.json({
      position_percent: 0,
      target_position_percent: 0,
      moving: false,
      deployed: false,
      homed: false,
      speed_percent: 50,
      at_upper_limit: false,
      at_lower_limit: true,
      overload: false,
      connected: false,
      model_name: 'Simulated Pulley',
      motor_current_a: 0,
      temperature_c: 22,
      error: false,
      error_message: '',
      max_position_steps: 10000,
      travel_time_s: 15,
      deployment_type: 'generic',
    });
  }
});

module.exports = router;
