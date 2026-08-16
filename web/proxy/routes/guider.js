/**
 * ST4 Guider Routes
 *
 * Proxies ST4 guider operations to the St4GuiderService gRPC service, which is
 * hosted IN-PROCESS inside the mount controller (unified gRPC port 50051) —
 * Phase 2 (P2). No silent simulated fallback — an unreachable service returns
 * an explicit 503 so the UI never shows fake data.
 */
'use strict';

const express = require('express');
const router = express.Router();
const { getGuiderGrpcClient, guiderGrpcCall } = require('../grpc/client');
const { errorResponse } = require('../grpc/converters');

/**
 * POST /api/guider/start
 * Start guiding.
 * Body: { ... } (optional config overrides)
 */
router.post('/start', async (req, res) => {
  try {
    const config = req.body || {};
    await guiderGrpcCall('StartGuiding', config);
    res.json({ success: true, message: 'Guiding started' });
  } catch (err) {
    errorResponse(res, 503, 'Guider service unavailable', err.message);
  }
});

/**
 * POST /api/guider/stop
 * Stop guiding.
 */
router.post('/stop', async (req, res) => {
  try {
    await guiderGrpcCall('StopGuiding', {});
    res.json({ success: true, message: 'Guiding stopped' });
  } catch (err) {
    errorResponse(res, 503, 'Guider service unavailable', err.message);
  }
});

/**
 * POST /api/guider/calibrate
 * Run ST4 calibration.
 *
 * Calibrate is a SERVER-STREAMING RPC: the service streams progress and ends
 * with a "complete" message. The proxy collects the stream and responds once
 * it finishes.
 * Body: { pulse_duration_ms, steps, step_delay_ms }
 */
router.post('/calibrate', async (req, res) => {
  const params = req.body || {};
  const request = {
    pulse_duration_ms: params.pulse_duration_ms || 500,
    steps: params.steps || 10,
    step_delay_ms: params.step_delay_ms || 1000,
  };

  const progress = [];
  try {
    const client = getGuiderGrpcClient();
    const stream = client.calibrate(request, {
      deadline: new Date(Date.now() + 120000), // 2 minutes
    });
    await new Promise((resolve, reject) => {
      stream.on('data', (msg) => {
        progress.push({
          direction: msg.direction,
          step: msg.step,
          total_steps: msg.total_steps,
          position_arcsec: msg.position_arcsec,
          complete: msg.complete,
          calibration_arcsec_per_ms: msg.calibration_arcsec_per_ms,
        });
      });
      stream.on('end', resolve);
      stream.on('error', reject);
    });
    const last = progress[progress.length - 1] || {};
    res.json({
      success: !!last.complete,
      message: last.complete ? 'Guider calibration complete' : 'Guider calibration finished',
      progress,
    });
  } catch (err) {
    errorResponse(res, 503, 'Guider service unavailable', err.message);
  }
});

/**
 * POST /api/guider/config
 * Apply guider configuration.
 * Body: { interface_type, device_path, ... }
 */
router.post('/config', async (req, res) => {
  try {
    const b = req.body || {};
    // Pass the full St4GuiderConfig through — including guide-loop parameters
    // (aggression, invert, min/max pulse) consumed by the guiding loop, and the
    // PHD2 endpoint (allows PHD2 to run on a different host).
    await guiderGrpcCall('StartGuiding', {
      interface_type: b.interface_type || 'simulated',
      device_path: b.device_path || '',
      aggression: b.aggression || 1.0,
      min_pulse_ms: b.min_pulse_ms || 10,
      max_pulse_ms: b.max_pulse_ms || 3000,
      invert_ra: !!b.invert_ra,
      invert_dec: !!b.invert_dec,
      phd2_host: b.phd2_host || 'localhost',
      phd2_port: b.phd2_port || 4400,
    });
    res.json({ success: true, message: 'Guider configuration applied' });
  } catch (err) {
    errorResponse(res, 503, 'Guider service unavailable', err.message);
  }
});

/**
 * GET /api/guider/status
 * Returns current guider status.
 */
router.get('/status', async (req, res) => {
  try {
    const status = await guiderGrpcCall('GetStatus', {});
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
    errorResponse(res, 503, 'Guider service unavailable', err.message);
  }
});

module.exports = router;
