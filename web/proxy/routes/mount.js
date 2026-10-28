/**
 * Mount Control Routes — Slew, Track, Park, Stop
 */
'use strict';

const express = require('express');
const router = express.Router();
const { grpcCall } = require('../grpc/client');
const { formatState, errorResponse } = require('../grpc/converters');

// Mount geographic location (latitude/longitude/altitude) rarely changes, so
// cache the value from GetConfiguration to avoid an extra gRPC call on every
// /api/status poll.
let locationCache = { value: null, expiresAt: 0 };
const LOCATION_CACHE_TTL_MS = 60000;

async function getMountLocation() {
  const now = Date.now();
  if (locationCache.value && now < locationCache.expiresAt) {
    return locationCache.value;
  }
  try {
    const cfg = await grpcCall('GetConfiguration', {});
    const location = {
      latitude: Number(cfg.latitude || 0),
      longitude: Number(cfg.longitude || 0),
      altitude: Number(cfg.altitude || 0),
    };
    locationCache = { value: location, expiresAt: now + LOCATION_CACHE_TTL_MS };
    return location;
  } catch (_) {
    return locationCache.value || null;
  }
}

/**
 * GET /api/status
 * Returns the current mount controller state.
 */
router.get('/status', async (req, res) => {
  try {
    const state = await grpcCall('GetState', {});
    const formatted = formatState(state);
    formatted.location = await getMountLocation();
    // DEBUG: log raw gRPC position data
    const cp = state.current_position;
    console.log('[DEBUG /api/status] current_position:', cp ? JSON.stringify(cp) : 'MISSING',
                '| telescope_axis1:', state.telescope_axis1,
                '| telescope_axis2:', state.telescope_axis2,
                '| actual_rate_axis1:', state.actual_rate_axis1,
                '| actual_rate_axis2:', state.actual_rate_axis2);
    res.json(formatted);
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
