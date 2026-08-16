/**
 * Pulley Controller Routes
 *
 * Proxies pulley operations to the PulleyService gRPC service, which is hosted
 * IN-PROCESS inside the mount controller (unified gRPC port 50051) — R3.
 * The service is a clearly-labelled SIMULATED linear actuator ("Simulated
 * Pulley") — the UI shows it as such via GET /status. No silent simulated
 * fallback here: an unreachable service returns an explicit 503.
 */
'use strict';

const express = require('express');
const router = express.Router();
const { getPulleyGrpcClient, pulleyGrpcCall } = require('../grpc/client');
const { errorResponse } = require('../grpc/converters');

/**
 * POST /api/pulley/deploy
 * Deploy (extend) the pulley to a target position.
 * Body: { position_percent, speed_percent? }
 */
router.post('/deploy', async (req, res) => {
  try {
    const body = req.body || {};
    await pulleyGrpcCall('DeployPulley', {
      position_percent: parseInt(body.position_percent, 10) || 100,
      speed_percent: parseInt(body.speed_percent, 10) || 50,
      synchronous: !!body.synchronous,
    });
    res.json({ success: true, message: 'Pulley deploying' });
  } catch (err) {
    errorResponse(res, 503, 'Pulley service unavailable', err.message);
  }
});

/**
 * POST /api/pulley/retract
 * Retract (rewind) the pulley fully.
 */
router.post('/retract', async (req, res) => {
  try {
    await pulleyGrpcCall('RetractPulley', {});
    res.json({ success: true, message: 'Pulley retracting' });
  } catch (err) {
    errorResponse(res, 503, 'Pulley service unavailable', err.message);
  }
});

/**
 * POST /api/pulley/stop
 * Stop pulley motion immediately.
 */
router.post('/stop', async (req, res) => {
  try {
    await pulleyGrpcCall('StopPulley', {});
    res.json({ success: true, message: 'Pulley stopped' });
  } catch (err) {
    errorResponse(res, 503, 'Pulley service unavailable', err.message);
  }
});

/**
 * POST /api/pulley/position
 * Move pulley to a specific absolute position [0..100%].
 * Body: { position_percent, speed_percent? }
 */
router.post('/position', async (req, res) => {
  try {
    const body = req.body || {};
    await pulleyGrpcCall('SetPulleyPosition', {
      position_percent: parseInt(body.position_percent, 10) || 0,
      speed_percent: parseInt(body.speed_percent, 10) || 50,
    });
    res.json({ success: true, message: 'Pulley position set' });
  } catch (err) {
    errorResponse(res, 503, 'Pulley service unavailable', err.message);
  }
});

/**
 * POST /api/pulley/home
 * Calibrate / home the pulley. HomePulley is a SERVER-STREAMING RPC: the
 * service streams progress and finishes with a "complete" message. The proxy
 * collects the stream and responds once it finishes.
 */
router.post('/home', async (req, res) => {
  const progress = [];
  try {
    const client = getPulleyGrpcClient();
    const stream = client.homePulley({}, {
      deadline: new Date(Date.now() + 60000), // 1 minute
    });
    let last = null;
    await new Promise((resolve, reject) => {
      stream.on('data', (msg) => {
        last = msg;
        progress.push({
          homing: msg.homing || false,
          phase: msg.phase || '',
          progress_percent: msg.progress_percent || 0,
          complete: msg.complete || false,
          error_message: msg.error_message || '',
        });
      });
      stream.on('end', resolve);
      stream.on('error', reject);
    });
    res.json({
      success: last ? !!last.complete : false,
      message: last && last.complete ? 'Pulley homed' : 'Pulley home finished',
      progress,
    });
  } catch (err) {
    errorResponse(res, 503, 'Pulley service unavailable', err.message);
  }
});

/**
 * POST /api/pulley/speed
 * Set pulley speed limit.
 * Body: { speed_percent }
 */
router.post('/speed', async (req, res) => {
  try {
    const speed = parseInt((req.body || {}).speed_percent, 10) || 50;
    await pulleyGrpcCall('SetPulleySpeed', { speed_percent: speed });
    res.json({ success: true, message: `Pulley speed set to ${speed}%` });
  } catch (err) {
    errorResponse(res, 503, 'Pulley service unavailable', err.message);
  }
});

/**
 * GET /api/pulley/status
 * Returns current pulley status.
 */
router.get('/status', async (req, res) => {
  try {
    const status = await pulleyGrpcCall('GetPulleyStatus', {});
    res.json({
      position_percent: status.position_percent || 0,
      target_position_percent: status.target_position_percent || 0,
      moving: status.moving || false,
      deployed: status.deployed || false,
      homed: status.homed || false,
      speed_percent: status.speed_percent || 0,
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
      deployment_type: status.deployment_type || '',
    });
  } catch (err) {
    errorResponse(res, 503, 'Pulley service unavailable', err.message);
  }
});

module.exports = router;
