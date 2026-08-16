/**
 * Dome Control Routes
 *
 * Proxies dome operations to the DomeService gRPC service (hosted IN-PROCESS
 * on the mount controller's unified port 50051).
 * No silent simulated fallback — an unreachable service returns an explicit
 * 503 so the UI never shows fake data (P1 fix).
 */
'use strict';

const express = require('express');
const router = express.Router();
const { domeGrpcCall, createDomeGrpcClient } = require('../grpc/client');
const { errorResponse } = require('../grpc/converters');

// Try to initialise the dome gRPC client on first import
try {
  createDomeGrpcClient();
} catch (err) {
  console.warn('[DomeRoutes] Dome gRPC client will be created on first request');
}

/**
 * POST /api/dome/open
 * Open the dome shutter.
 */
router.post('/open', async (req, res) => {
  try {
    await domeGrpcCall('OpenShutter', {});
    res.json({ success: true, message: 'Dome opening' });
  } catch (err) {
    errorResponse(res, 502, 'Failed to open dome', err.message);
  }
});

/**
 * POST /api/dome/close
 * Close the dome shutter.
 */
router.post('/close', async (req, res) => {
  try {
    await domeGrpcCall('CloseShutter', {});
    res.json({ success: true, message: 'Dome closing' });
  } catch (err) {
    errorResponse(res, 502, 'Failed to close dome', err.message);
  }
});

/**
 * POST /api/dome/rotate
 * Rotate dome to azimuth.
 * Body: { azimuth_deg: number }
 */
router.post('/rotate', async (req, res) => {
  try {
    const { azimuth_deg } = req.body;
    await domeGrpcCall('RotateTo', { azimuth_deg: azimuth_deg || 0 });
    res.json({ success: true, message: `Dome rotating to ${azimuth_deg}°` });
  } catch (err) {
    errorResponse(res, 502, 'Failed to rotate dome', err.message);
  }
});

/**
 * POST /api/dome/park
 * Park the dome.
 */
router.post('/park', async (req, res) => {
  try {
    await domeGrpcCall('Park', {});
    res.json({ success: true, message: 'Dome parking' });
  } catch (err) {
    errorResponse(res, 502, 'Failed to park dome', err.message);
  }
});

/**
 * POST /api/dome/unpark
 * Unpark the dome.
 */
router.post('/unpark', async (req, res) => {
  try {
    await domeGrpcCall('Unpark', {});
    res.json({ success: true, message: 'Dome unparked' });
  } catch (err) {
    errorResponse(res, 502, 'Failed to unpark dome', err.message);
  }
});

/**
 * POST /api/dome/home
 * Rotate dome to home position.
 */
router.post('/home', async (req, res) => {
  try {
    await domeGrpcCall('GoHome', {});
    res.json({ success: true, message: 'Dome moving to home' });
  } catch (err) {
    errorResponse(res, 502, 'Failed to move dome home', err.message);
  }
});

/**
 * POST /api/dome/halt
 * Stop all dome movement.
 */
router.post('/halt', async (req, res) => {
  try {
    await domeGrpcCall('Halt', {});
    res.json({ success: true, message: 'Dome halted' });
  } catch (err) {
    errorResponse(res, 502, 'Failed to halt dome', err.message);
  }
});

/**
 * POST /api/dome/autosync
 * Enable or disable auto-sync with mount.
 * Body: { enabled: boolean }
 */
router.post('/autosync', async (req, res) => {
  try {
    const { enabled } = req.body;
    await domeGrpcCall('SetAutoSync', { enabled: !!enabled });
    res.json({ success: true, message: `Auto-sync ${enabled ? 'enabled' : 'disabled'}` });
  } catch (err) {
    errorResponse(res, 502, 'Failed to set auto-sync', err.message);
  }
});

/**
 * GET /api/dome/status
 * Returns current dome status.
 */
router.get('/status', async (req, res) => {
  try {
    const status = await domeGrpcCall('GetStatus', {});
    res.json({
      dome_type: status.dome_type || 0,
      state: status.state || 0,
      azimuth_deg: status.azimuth_deg || 0,
      target_azimuth_deg: status.target_azimuth_deg || 0,
      can_rotate: status.can_rotate || false,
      can_open: status.can_open || false,
      shutter_open: status.shutter_open || false,
      aperture_deg: status.aperture_deg || 0,
      parked: status.parked || false,
      sync_enabled: status.sync_enabled || false,
      sync_offset_deg: status.sync_offset_deg || 0,
      error_message: status.error_message || '',
      home_azimuth_deg: status.home_azimuth_deg || 0,
      park_azimuth_deg: status.park_azimuth_deg || 0,
      connected: status.connected || false,
      moving: status.moving || false,
    });
  } catch (err) {
    errorResponse(res, 503, 'Dome service unavailable', err.message);
  }
});

module.exports = router;
