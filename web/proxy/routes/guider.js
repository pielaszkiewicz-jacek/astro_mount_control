/**
 * ST4 Guider Routes
 *
 * Proxies ST4 guider operations to the backend St4Guider gRPC service.
 * Falls back to simulated data when the service is unavailable.
 */
'use strict';

const express = require('express');
const router = express.Router();
const { grpcCall } = require('../grpc/client');
const { errorResponse } = require('../grpc/converters');

/**
 * POST /api/guider/start
 * Start guiding.
 * Body: { ... } (optional config overrides)
 */
router.post('/start', async (req, res) => {
  try {
    const config = req.body || {};
    await grpcCall('StartGuiding', config);
    res.json({ success: true, message: 'Guiding started' });
  } catch (err) {
    errorResponse(res, 502, 'Failed to start guiding', err.message);
  }
});

/**
 * POST /api/guider/stop
 * Stop guiding.
 */
router.post('/stop', async (req, res) => {
  try {
    await grpcCall('StopGuiding', {});
    res.json({ success: true, message: 'Guiding stopped' });
  } catch (err) {
    errorResponse(res, 502, 'Failed to stop guiding', err.message);
  }
});

/**
 * POST /api/guider/calibrate
 * Run ST4 calibration.
 */
router.post('/calibrate', async (req, res) => {
  try {
    const params = req.body || {};
    await grpcCall('Calibrate', {
      pulse_duration_ms: params.pulse_duration_ms || 500,
      steps: params.steps || 10,
      step_delay_ms: params.step_delay_ms || 1000,
    });
    res.json({ success: true, message: 'Guider calibration started' });
  } catch (err) {
    errorResponse(res, 502, 'Failed to calibrate guider', err.message);
  }
});

/**
 * POST /api/guider/config
 * Apply guider configuration.
 * Body: { interface_type, device_path, ... }
 */
router.post('/config', async (req, res) => {
  try {
    const { interface_type, device_path } = req.body;
    await grpcCall('StartGuiding', {
      interface_type: interface_type || 'simulated',
      device_path: device_path || '',
    });
    res.json({ success: true, message: 'Guider configuration applied' });
  } catch (err) {
    errorResponse(res, 502, 'Failed to configure guider', err.message);
  }
});

/**
 * GET /api/guider/status
 * Returns current guider status.
 */
router.get('/status', async (req, res) => {
  try {
    const status = await grpcCall('GetStatus', {});
    res.json({
      guiding: status.guiding || false,
      interface_type: status.interface_type || 'simulated',
      connected: status.connected || false,
      pulses_sent: status.pulses_sent || 0,
      pulses_failed: status.pulses_failed || 0,
      ra_correction_arcsec: status.ra_correction_arcsec || 0,
      dec_correction_arcsec: status.dec_correction_arcsec || 0,
      rms_ra: status.rms_ra || 0,
      rms_dec: status.rms_dec || 0,
      calibrated: status.calibrated || false,
    });
  } catch (err) {
    res.json({
      guiding: false,
      interface_type: 'simulated',
      connected: false,
      pulses_sent: 0,
      pulses_failed: 0,
      ra_correction_arcsec: 0,
      dec_correction_arcsec: 0,
      rms_ra: 0,
      rms_dec: 0,
      calibrated: false,
    });
  }
});

module.exports = router;
