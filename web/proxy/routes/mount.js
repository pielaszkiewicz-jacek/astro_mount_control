/**
 * Mount Control Routes — Slew, Track, Park, Stop
 */
'use strict';

const express = require('express');
const router = express.Router();
const { grpcCall } = require('../grpc/client');
const { formatState, errorResponse } = require('../grpc/converters');

/**
 * GET /api/status
 * Returns the current mount controller state.
 */
router.get('/status', async (req, res) => {
  try {
    const state = await grpcCall('GetState', {});
    // DEBUG: log raw gRPC position data
    const cp = state.current_position;
    console.log('[DEBUG /api/status] current_position:', cp ? JSON.stringify(cp) : 'MISSING',
                '| telescope_axis1:', state.telescope_axis1,
                '| telescope_axis2:', state.telescope_axis2,
                '| actual_rate_axis1:', state.actual_rate_axis1,
                '| actual_rate_axis2:', state.actual_rate_axis2);
    res.json(formatState(state));
  } catch (err) {
    errorResponse(res, 503, 'Mount controller unreachable', err.message);
  }
});

/**
 * POST /api/slew
 * Slew to equatorial coordinates.
 * Body: { ra: number, dec: number }
 */
router.post('/slew', async (req, res) => {
  try {
    const { ra, dec } = req.body;

    if (ra === undefined || dec === undefined) {
      return errorResponse(res, 400, 'Missing required fields: ra, dec');
    }
    if (typeof ra !== 'number' || ra < 0 || ra >= 24) {
      return errorResponse(res, 400, 'RA must be a number in range [0, 24)');
    }
    if (typeof dec !== 'number' || dec < -90 || dec > 90) {
      return errorResponse(res, 400, 'Dec must be a number in range [-90, 90]');
    }

    await grpcCall('SlewToCoordinates', { ra, dec });
    res.json({ success: true, message: `Slewing to RA=${ra}h, Dec=${dec}°` });
  } catch (err) {
    errorResponse(res, 502, 'Slew failed', err.message);
  }
});

/**
 * POST /api/track
 * Slew to equatorial coordinates and start tracking (sidereal).
 * Body: { ra: number, dec: number }
 */
router.post('/track', async (req, res) => {
  try {
    const { ra, dec } = req.body;

    if (ra === undefined || dec === undefined) {
      return errorResponse(res, 400, 'Missing required fields: ra, dec');
    }
    if (typeof ra !== 'number' || ra < 0 || ra >= 24) {
      return errorResponse(res, 400, 'RA must be a number in range [0, 24)');
    }
    if (typeof dec !== 'number' || dec < -90 || dec > 90) {
      return errorResponse(res, 400, 'Dec must be a number in range [-90, 90]');
    }

    await grpcCall('TrackObject', { ra, dec });
    res.json({ success: true, message: `Tracking RA=${ra}h, Dec=${dec}°` });
  } catch (err) {
    errorResponse(res, 502, 'Track failed', err.message);
  }
});

/**
 * POST /api/stop
 * Stop all motion.
 */
router.post('/stop', async (req, res) => {
  try {
    await grpcCall('Stop', {});
    res.json({ success: true, message: 'Mount stopped' });
  } catch (err) {
    errorResponse(res, 502, 'Stop failed', err.message);
  }
});

/**
 * POST /api/park
 * Park the mount.
 */
router.post('/park', async (req, res) => {
  try {
    await grpcCall('Park', {});
    res.json({ success: true, message: 'Park initiated' });
  } catch (err) {
    errorResponse(res, 502, 'Park failed', err.message);
  }
});

/**
 * POST /api/unpark
 * Unpark the mount.
 */
router.post('/unpark', async (req, res) => {
  try {
    await grpcCall('Unpark', {});
    res.json({ success: true, message: 'Unparked' });
  } catch (err) {
    errorResponse(res, 502, 'Unpark failed', err.message);
  }
});

module.exports = router;
