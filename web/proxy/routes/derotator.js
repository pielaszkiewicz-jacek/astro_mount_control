/**
 * Derotator Control Routes
 *
 * Proxies derotator operations to the DerotatorService gRPC service (hosted
 * IN-PROCESS on the mount controller's unified port 50051).
 * No silent simulated fallback — an unreachable service returns an explicit
 * 503 so the UI never shows fake data (P1 fix).
 */
'use strict';

const express = require('express');
const router = express.Router();
const { derotatorGrpcCall } = require('../grpc/client');
const { errorResponse } = require('../grpc/converters');

/**
 * POST /api/derotator/mode
 * Set derotator mode.
 * Body: { mode: number } 0=Disabled, 1=Auto, 2=FixedAngle, 3=ManualRate
 */
router.post('/mode', async (req, res) => {
  try {
    const { mode } = req.body;
    await derotatorGrpcCall('SetMode', { mode });
    res.json({ success: true });
  } catch (err) {
    errorResponse(res, 502, 'Failed to set derotator mode', err.message);
  }
});

/**
 * POST /api/derotator/angle
 * Set derotator angle.
 * Body: { angle_deg: number }
 */
router.post('/angle', async (req, res) => {
  try {
    const { angle_deg } = req.body;
    await derotatorGrpcCall('SetAngle', { angle_deg });
    res.json({ success: true });
  } catch (err) {
    errorResponse(res, 502, 'Failed to set derotator angle', err.message);
  }
});

/**
 * POST /api/derotator/rate
 * Set derotator rate.
 * Body: { rate_deg_s: number }
 */
router.post('/rate', async (req, res) => {
  try {
    const { rate_deg_s } = req.body;
    await derotatorGrpcCall('SetRate', { rate_deg_s });
    res.json({ success: true });
  } catch (err) {
    errorResponse(res, 502, 'Failed to set derotator rate', err.message);
  }
});

/**
 * POST /api/derotator/home
 * Home the derotator.
 */
router.post('/home', async (req, res) => {
  try {
    await derotatorGrpcCall('Home', {});
    res.json({ success: true, message: 'Derotator homing started' });
  } catch (err) {
    errorResponse(res, 502, 'Failed to home derotator', err.message);
  }
});

/**
 * GET /api/derotator/status
 * Returns current derotator status.
 */
router.get('/status', async (req, res) => {
  try {
    const status = await derotatorGrpcCall('GetStatus', {});
    res.json({
      mode: status.mode || 0,
      homed: status.homed || false,
      current_position_deg: status.current_position_deg || 0,
      target_position_deg: status.target_position_deg || 0,
      current_rate_deg_s: status.current_rate_deg_s || 0,
      moving: status.moving || false,
      error: status.error || false,
      error_message: status.error_message || '',
      connected: status.connected || false,
    });
  } catch (err) {
    errorResponse(res, 503, 'Derotator service unavailable', err.message);
  }
});

/**
 * GET /api/derotator/field-rotation
 * Returns field rotation information.
 */
router.get('/field-rotation', async (req, res) => {
  try {
    const info = await derotatorGrpcCall('GetFieldRotation', {});
    res.json({
      current_angle_deg: info.current_angle_deg || 0,
      current_rate_arcsec_s: info.current_rate_arcsec_s || 0,
      predicted_angle_10min: info.predicted_angle_10min || 0,
      mount_type: info.mount_type || '',
    });
  } catch (err) {
    errorResponse(res, 503, 'Derotator service unavailable', err.message);
  }
});

module.exports = router;
