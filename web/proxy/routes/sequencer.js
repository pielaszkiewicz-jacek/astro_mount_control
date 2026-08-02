/**
 * Observation Sequencer Routes
 *
 * Proxies sequencer operations to the backend SequencerService gRPC service.
 * Falls back to simulated data when the service is unavailable.
 */
'use strict';

const express = require('express');
const router = express.Router();
const { grpcCall } = require('../grpc/client');
const { errorResponse } = require('../grpc/converters');

/**
 * POST /api/sequencer/start
 * Start the observation sequencer.
 */
router.post('/start', async (req, res) => {
  try {
    await grpcCall('StartSequencer', {});
    res.json({ success: true, message: 'Sequencer started' });
  } catch (err) {
    errorResponse(res, 502, 'Failed to start sequencer', err.message);
  }
});

/**
 * POST /api/sequencer/stop
 * Stop the observation sequencer.
 */
router.post('/stop', async (req, res) => {
  try {
    await grpcCall('StopSequencer', {});
    res.json({ success: true, message: 'Sequencer stopped' });
  } catch (err) {
    errorResponse(res, 502, 'Failed to stop sequencer', err.message);
  }
});

/**
 * POST /api/sequencer/pause
 * Pause the observation sequencer.
 */
router.post('/pause', async (req, res) => {
  try {
    await grpcCall('PauseSequencer', {});
    res.json({ success: true, message: 'Sequencer paused' });
  } catch (err) {
    errorResponse(res, 502, 'Failed to pause sequencer', err.message);
  }
});

/**
 * POST /api/sequencer/resume
 * Resume the observation sequencer.
 */
router.post('/resume', async (req, res) => {
  try {
    await grpcCall('StartSequencer', {});
    res.json({ success: true, message: 'Sequencer resumed' });
  } catch (err) {
    errorResponse(res, 502, 'Failed to resume sequencer', err.message);
  }
});

/**
 * POST /api/sequencer/load
 * Load an observation plan.
 */
router.post('/load', async (req, res) => {
  try {
    const plan = req.body;
    await grpcCall('LoadPlan', plan || {});
    res.json({ success: true, message: 'Observation plan loaded' });
  } catch (err) {
    errorResponse(res, 502, 'Failed to load observation plan', err.message);
  }
});

/**
 * GET /api/sequencer/status
 * Returns current sequencer status.
 */
router.get('/status', async (req, res) => {
  try {
    const status = await grpcCall('GetSequencerStatus', {});
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
    res.json({
      state: 'IDLE',
      current_target: '',
      current_target_index: 0,
      total_targets: 0,
      current_exposure: 0,
      total_exposures: 0,
      progress_percent: 0,
      current_action: '',
      elapsed_time_s: 0,
      remaining_time_s: 0,
    });
  }
});

module.exports = router;
