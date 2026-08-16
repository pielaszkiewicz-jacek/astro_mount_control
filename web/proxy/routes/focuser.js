/**
 * Focuser Control Routes
 *
 * Proxies focuser operations to the FocuserService gRPC service, which is
 * hosted IN-PROCESS inside the mount controller (unified gRPC port 50051).
 * No silent simulated fallback — an unreachable service returns an explicit
 * 503 so the UI never shows fake data (P1 fix).
 */
'use strict';

const express = require('express');
const router = express.Router();
const { getFocuserGrpcClient, focuserGrpcCall } = require('../grpc/client');
const { errorResponse } = require('../grpc/converters');

/**
 * POST /api/focuser/move
 * Move focuser to absolute position.
 * Body: { position: number, speed?: number }
 */
router.post('/move', async (req, res) => {
  try {
    const { position, speed } = req.body;
    await focuserGrpcCall('MoveFocuser', {
      position: parseInt(position) || 0,
      speed: parseInt(speed) || 50,
    });
    res.json({ success: true, message: `Focuser moving to position ${position}` });
  } catch (err) {
    errorResponse(res, 503, 'Focuser service unavailable', err.message);
  }
});

/**
 * POST /api/focuser/halt
 * Halt focuser motion.
 */
router.post('/halt', async (req, res) => {
  try {
    await focuserGrpcCall('HaltFocuser', {});
    res.json({ success: true, message: 'Focuser halted' });
  } catch (err) {
    errorResponse(res, 503, 'Focuser service unavailable', err.message);
  }
});

/**
 * POST /api/focuser/autofocus
 * Run the auto-focus routine.
 *
 * RunAutoFocus is a SERVER-STREAMING RPC: the service streams per-step
 * progress and finishes with a "complete" message before ending the stream.
 * The proxy collects the stream and responds once it finishes.
 * Body: { start_position, end_position, step_size }
 */
router.post('/autofocus', async (req, res) => {
  const { start_position, end_position, step_size } = req.body;
  const request = {
    start_position: parseInt(start_position) || 0,
    end_position: parseInt(end_position) || 100000,
    step_size: parseInt(step_size) || 500,
  };

  const progress = [];
  let lastStatus = '';
  try {
    const client = getFocuserGrpcClient();
    const stream = client.runAutoFocus(request, {
      deadline: new Date(Date.now() + 600000), // 10 minutes
    });
    await new Promise((resolve, reject) => {
      stream.on('data', (msg) => {
        lastStatus = msg.status || '';
        progress.push({
          current_step: msg.current_step,
          total_steps: msg.total_steps,
          position: msg.position,
          hfd: msg.hfd,
          temperature_c: msg.temperature_c,
          best_position: msg.best_position,
          best_hfd: msg.best_hfd,
          status: lastStatus,
        });
      });
      stream.on('end', resolve);
      stream.on('error', reject);
    });
    res.json({
      success: lastStatus === 'complete',
      message: lastStatus === 'complete' ? 'Auto-focus complete' : 'Auto-focus finished',
      progress,
    });
  } catch (err) {
    errorResponse(res, 503, 'Focuser service unavailable', err.message);
  }
});

/**
 * GET /api/focuser/status
 * Returns current focuser status.
 */
router.get('/status', async (req, res) => {
  try {
    const status = await focuserGrpcCall('GetFocuserPosition', {});
    res.json({
      position: status.position || 0,
      max_position: status.max_position || 100000,
      moving: status.moving || false,
      connected: status.connected || false,
      temperature_c: status.temperature_c || 0,
      hfd: status.hfd || 0,
      model_name: status.model_name || '',
      error_message: status.error_message || '',
    });
  } catch (err) {
    errorResponse(res, 503, 'Focuser service unavailable', err.message);
  }
});

module.exports = router;
