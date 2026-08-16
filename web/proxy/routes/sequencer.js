/**
 * Observation Sequencer Routes
 *
 * Proxies sequencer operations to the SequencerService gRPC service
 * (astro_sequencer_server, default port 50057) via a dedicated client.
 * No silent simulated fallback — an unreachable service returns an explicit
 * 503 so the UI never shows fake data (P1 fix).
 */
'use strict';

const express = require('express');
const router = express.Router();
const { sequencerGrpcCall } = require('../grpc/client');
const { errorResponse } = require('../grpc/converters');

/**
 * POST /api/sequencer/start
 * Start the observation sequencer.
 */
router.post('/start', async (req, res) => {
  try {
    await sequencerGrpcCall('StartSequencer', {});
    res.json({ success: true, message: 'Sequencer started' });
  } catch (err) {
    errorResponse(res, 503, 'Sequencer service unavailable', err.message);
  }
});

/**
 * POST /api/sequencer/stop
 * Stop the observation sequencer.
 */
router.post('/stop', async (req, res) => {
  try {
    await sequencerGrpcCall('StopSequencer', {});
    res.json({ success: true, message: 'Sequencer stopped' });
  } catch (err) {
    errorResponse(res, 503, 'Sequencer service unavailable', err.message);
  }
});

/**
 * POST /api/sequencer/pause
 * Pause the observation sequencer.
 */
router.post('/pause', async (req, res) => {
  try {
    await sequencerGrpcCall('PauseSequencer', {});
    res.json({ success: true, message: 'Sequencer paused' });
  } catch (err) {
    errorResponse(res, 503, 'Sequencer service unavailable', err.message);
  }
});

/**
 * POST /api/sequencer/resume
 * Resume the observation sequencer.
 */
router.post('/resume', async (req, res) => {
  try {
    await sequencerGrpcCall('StartSequencer', {});
    res.json({ success: true, message: 'Sequencer resumed' });
  } catch (err) {
    errorResponse(res, 503, 'Sequencer service unavailable', err.message);
  }
});

/**
 * POST /api/sequencer/load
 * Load an observation plan.
 */
router.post('/load', async (req, res) => {
  try {
    const plan = req.body;
    await sequencerGrpcCall('LoadPlan', plan || {});
    res.json({ success: true, message: 'Observation plan loaded' });
  } catch (err) {
    errorResponse(res, 503, 'Sequencer service unavailable', err.message);
  }
});

/**
 * GET /api/sequencer/status
 * Returns current sequencer status.
 */
router.get('/status', async (req, res) => {
  try {
    const status = await sequencerGrpcCall('GetSequencerStatus', {});
    res.json({
      state: status.state || 'IDLE',
      current_target: status.current_target || '',
      current_target_index: status.current_target_index || 0,
      total_targets: status.total_targets || 0,
      current_exposure: status.current_exposure || 0,
      total_exposures: status.total_exposures || 0,
      progress_percent: status.progress_percent || 0,
      current_action: status.current_action || '',
      elapsed_time_s: status.elapsed_time_s || 0,
      remaining_time_s: status.remaining_time_s || 0,
    });
  } catch (err) {
    errorResponse(res, 503, 'Sequencer service unavailable', err.message);
  }
});

module.exports = router;
