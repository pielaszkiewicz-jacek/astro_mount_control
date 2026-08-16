/**
 * PEC (Periodic Error Correction) Routes
 *
 * Proxies PEC operations to the PECService gRPC service, which is hosted
 * IN-PROCESS inside the mount controller (unified gRPC port 50051) — Phase 2
 * (P2). No silent simulated fallback — an unreachable service returns an
 * explicit 503 so the UI never shows fake data.
 */
'use strict';

const express = require('express');
const router = express.Router();
const { getPecGrpcClient, pecGrpcCall } = require('../grpc/client');
const { errorResponse } = require('../grpc/converters');

/**
 * GET /api/pec/status
 * Returns current PEC status.
 */
router.get('/status', async (req, res) => {
  try {
    const status = await pecGrpcCall('GetPECStatus', {});
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
    errorResponse(res, 503, 'PEC service unavailable', err.message);
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
    await pecGrpcCall('SetPECEnabled', { enabled: !!enabled });
    res.json({ success: true });
  } catch (err) {
    errorResponse(res, 503, 'PEC service unavailable', err.message);
  }
});

/**
 * POST /api/pec/train/start
 * Start PEC training.
 *
 * StartTraining is a SERVER-STREAMING RPC: the service streams progress and
 * ends with a "complete" message. The proxy collects the stream and responds
 * once it finishes.
 * Body: { worm_cycle_seconds, num_harmonics, duration_cycles }
 */
router.post('/train/start', async (req, res) => {
  const { worm_cycle_seconds, num_harmonics } = req.body;
  const request = {
    worm_cycle_seconds: worm_cycle_seconds || 638,
    num_harmonics: num_harmonics || 8,
    duration_cycles: 3,
  };

  const progress = [];
  try {
    const client = getPecGrpcClient();
    const stream = client.startTraining(request, {
      deadline: new Date(Date.now() + 300000), // 5 minutes
    });
    await new Promise((resolve, reject) => {
      stream.on('data', (msg) => {
        progress.push({
          progress_percent: msg.progress_percent,
          elapsed_seconds: msg.elapsed_seconds,
          remaining_seconds: msg.remaining_seconds,
          current_error_arcsec: msg.current_error_arcsec,
          peak_error_arcsec: msg.peak_error_arcsec,
          rms_error_arcsec: msg.rms_error_arcsec,
          status: msg.status,
        });
      });
      stream.on('end', resolve);
      stream.on('error', reject);
    });
    const last = progress[progress.length - 1] || {};
    res.json({
      success: last.status === 'complete',
      message: last.status === 'complete' ? 'PEC training complete' : 'PEC training finished',
      progress,
    });
  } catch (err) {
    errorResponse(res, 503, 'PEC service unavailable', err.message);
  }
});

/**
 * POST /api/pec/train/stop
 * Stop PEC training.
 */
router.post('/train/stop', async (req, res) => {
  try {
    await pecGrpcCall('StopTraining', {});
    res.json({ success: true, message: 'PEC training stopped' });
  } catch (err) {
    errorResponse(res, 503, 'PEC service unavailable', err.message);
  }
});

/**
 * POST /api/pec/save
 * Save PEC data.
 */
router.post('/save', async (req, res) => {
  try {
    await pecGrpcCall('SavePECData', {});
    res.json({ success: true, message: 'PEC data saved' });
  } catch (err) {
    errorResponse(res, 503, 'PEC service unavailable', err.message);
  }
});

/**
 * POST /api/pec/load
 * Load PEC data.
 */
router.post('/load', async (req, res) => {
  try {
    await pecGrpcCall('LoadPECData', {});
    res.json({ success: true, message: 'PEC data loaded' });
  } catch (err) {
    errorResponse(res, 503, 'PEC service unavailable', err.message);
  }
});

module.exports = router;
