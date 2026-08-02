/**
 * PEC (Periodic Error Correction) Routes
 *
 * Proxies PEC operations to the backend PEC gRPC service.
 * Falls back to simulated data when the service is unavailable.
 */
'use strict';

const express = require('express');
const router = express.Router();
const { grpcCall } = require('../grpc/client');
const { errorResponse } = require('../grpc/converters');

/**
 * GET /api/pec/status
 * Returns current PEC status.
 */
router.get('/status', async (req, res) => {
  try {
    const status = await grpcCall('GetPECStatus', {});
    res.json({
      enabled: status.enabled || false,
      trained: status.trained || false,
      peak_error_arcsec: status.peak_error_arcsec || 0,
      rms_error_arcsec: status.rms_error_arcsec || 0,
      num_harmonics: status.num_harmonics || 0,
      worm_cycle_seconds: status.worm_cycle_seconds || 638,
      correction_arcsec: status.correction_arcsec || 0,
      current_phase_deg: status.current_phase_deg || 0,
      last_training: status.last_training || null,
    });
  } catch (err) {
    // Return simulated/empty status when gRPC unavailable
    res.json({
      enabled: false,
      trained: false,
      peak_error_arcsec: 0,
      rms_error_arcsec: 0,
      num_harmonics: 0,
      worm_cycle_seconds: 638,
      correction_arcsec: 0,
      current_phase_deg: 0,
      last_training: null,
    });
  }
});

/**
 * POST /api/pec/enable
 * Enable or disable PEC.
 * Body: { enabled: boolean }
 */
router.post('/enable', async (req, res) => {
  try {
    const { enabled } = req.body;
    await grpcCall('SetPECEnabled', { enabled: !!enabled });
    res.json({ success: true });
  } catch (err) {
    errorResponse(res, 502, 'Failed to set PEC', err.message);
  }
});

/**
 * POST /api/pec/train/start
 * Start PEC training.
 * Body: { worm_cycle_seconds, num_harmonics, duration_cycles }
 */
router.post('/train/start', async (req, res) => {
  try {
    const { worm_cycle_seconds, num_harmonics, duration_cycles } = req.body;
    await grpcCall('StartTraining', {
      worm_cycle_seconds: worm_cycle_seconds || 638,
      num_harmonics: num_harmonics || 8,
      duration_cycles: duration_cycles || 3,
    });
    res.json({ success: true, message: 'PEC training started' });
  } catch (err) {
    errorResponse(res, 502, 'Failed to start PEC training', err.message);
  }
});

/**
 * POST /api/pec/train/stop
 * Stop PEC training.
 */
router.post('/train/stop', async (req, res) => {
  try {
    await grpcCall('StopTraining', {});
    res.json({ success: true, message: 'PEC training stopped' });
  } catch (err) {
    errorResponse(res, 502, 'Failed to stop PEC training', err.message);
  }
});

/**
 * POST /api/pec/save
 * Save PEC data.
 */
router.post('/save', async (req, res) => {
  try {
    await grpcCall('SavePECData', {});
    res.json({ success: true, message: 'PEC data saved' });
  } catch (err) {
    errorResponse(res, 502, 'Failed to save PEC data', err.message);
  }
});

/**
 * POST /api/pec/load
 * Load PEC data.
 */
router.post('/load', async (req, res) => {
  try {
    await grpcCall('LoadPECData', {});
    res.json({ success: true, message: 'PEC data loaded' });
  } catch (err) {
    errorResponse(res, 502, 'Failed to load PEC data', err.message);
  }
});

module.exports = router;
