/**
 * Ephemeris Tracking Routes
 */
'use strict';

const express = require('express');
const router = express.Router();
const { grpcCall, dbGrpcCall } = require('../grpc/client');
const { errorResponse } = require('../grpc/converters');
const builtinCatalog = require('../builtin-catalog');

/**
 * GET /api/tracking/status
 * Get the current ephemeris tracking status.
 */
router.get('/status', async (req, res) => {
  try {
    const status = await grpcCall('GetEphemerisTrackStatus', {});
    res.json(status);
  } catch (err) {
    errorResponse(res, 503, 'Failed to get ephemeris tracking status', err.message);
  }
});

/**
 * GET /api/tracking/metrics
 * Get ephemeris tracking metrics.
 */
router.get('/metrics', async (req, res) => {
  try {
    const metrics = await grpcCall('GetEphemerisMetrics', {});
    res.json(metrics);
  } catch (err) {
    errorResponse(res, 503, 'Failed to get ephemeris tracking metrics', err.message);
  }
});

/**
 * Resolve a fixed celestial object (star/DSO) to { ra [hours], dec [degrees] }
 * by looking it up in the object database and the built-in catalog. Returns
 * null when the object cannot be resolved.
 */
async function resolveFixedObject(objectId) {
  try {
    const result = await dbGrpcCall('SearchObjects', { query: objectId, limit: 1 }, 5);
    const obj = result && result.objects && result.objects[0];
    if (obj && typeof obj.ra_hours === 'number' && typeof obj.dec_degrees === 'number') {
      return { ra: obj.ra_hours, dec: obj.dec_degrees, name: obj.name || objectId };
    }
  } catch (_) {
    // Database unavailable — fall through to the built-in catalog.
  }

  const builtin = builtinCatalog.searchObjects({ name: objectId, limit: 1 });
  const obj = builtin && builtin.objects && builtin.objects[0];
  if (obj && typeof obj.ra_hours === 'number' && typeof obj.dec_degrees === 'number') {
    return { ra: obj.ra_hours, dec: obj.dec_degrees, name: obj.name || objectId };
  }
  return null;
}

/**
 * POST /api/tracking/start
 * Start ephemeris tracking for an object.
 * Body: { object_id, start_time?, lead_time_seconds?, wait_at_start?, enable_prediction?, tracking_mode? }
 */
router.post('/start', async (req, res) => {
  try {
    const { object_id, start_time, lead_time_seconds, wait_at_start, enable_prediction, tracking_mode } = req.body;

    if (!object_id) {
      return errorResponse(res, 400, 'Missing required field: object_id');
    }

    // start_time is a google.protobuf.Timestamp { seconds, nanos }. Accept an
    // ISO-8601 string, epoch milliseconds (number or numeric string), or a
    // pre-formatted timestamp object from the caller.
    let startTimestamp;
    if (start_time && typeof start_time === 'object' && 'seconds' in start_time) {
      startTimestamp = {
        seconds: Math.floor(Number(start_time.seconds) || 0),
        nanos: Math.floor(Number(start_time.nanos) || 0),
      };
    } else {
      let startMs = Date.now();
      if (start_time !== undefined && start_time !== null && start_time !== '') {
        const parsed = Date.parse(start_time);
        if (!Number.isNaN(parsed)) {
          startMs = parsed;
        } else if (!Number.isNaN(Number(start_time))) {
          startMs = Number(start_time);
        }
      }
      startTimestamp = {
        seconds: Math.floor(startMs / 1000),
        nanos: (startMs % 1000) * 1000000,
      };
    }

    let result;
    try {
      result = await grpcCall('StartEphemerisTracking', {
        object_id,
        start_time: startTimestamp,
        lead_time_seconds: lead_time_seconds || 30.0,
        wait_at_start: wait_at_start !== undefined ? wait_at_start : true,
        enable_prediction: enable_prediction || false,
        tracking_mode: tracking_mode || 'continuous',
      });
    } catch (err) {
      // NOT_FOUND (5) means the object is not in the ephemeris cache. For a
      // fixed celestial object (star/DSO) fall back to sidereal tracking.
      if (err && err.code === 5) {
        const fixed = await resolveFixedObject(object_id);
        if (fixed) {
          await grpcCall('TrackObject', { ra: fixed.ra, dec: fixed.dec });
          return res.json({
            success: true,
            trackingId: `sidereal-${object_id}`,
            message: `Tracking ${fixed.name} (sidereal)`,
          });
        }
      }
      return errorResponse(res, 502, 'Failed to start ephemeris tracking', err.message);
    }

    res.json({ success: true, trackingId: result.tracking_id, message: `Tracking ${object_id}` });
  } catch (err) {
    errorResponse(res, 502, 'Failed to start ephemeris tracking', err.message);
  }
});

/**
 * POST /api/tracking/start-with-data
 * Upload ephemeris data and start tracking in one call.
 * Body: EphemerisTrackRequest (includes ephemeris + tracking params)
 */
router.post('/start-with-data', async (req, res) => {
  try {
    const { ephemeris, lead_time_seconds, enable_prediction, prediction_interval_hours,
            auto_start, tracking_mode, custom_rate_ra, custom_rate_dec } = req.body;

    if (!ephemeris || !ephemeris.object_id) {
      return errorResponse(res, 400, 'Missing required field: ephemeris.object_id');
    }

    const result = await grpcCall('StartEphemerisTrackingWithData', {
      ephemeris,
      lead_time_seconds: lead_time_seconds || 30.0,
      enable_prediction: enable_prediction || false,
      prediction_interval_hours: prediction_interval_hours || 24.0,
      auto_start: auto_start !== undefined ? auto_start : true,
      tracking_mode: tracking_mode || 'continuous',
      custom_rate_ra: custom_rate_ra || 0.0,
      custom_rate_dec: custom_rate_dec || 0.0,
    });
    res.json(result);
  } catch (err) {
    errorResponse(res, 502, 'Failed to start ephemeris tracking with data', err.message);
  }
});

/**
 * POST /api/tracking/upload
 * Upload ephemeris data for a moving object.
 * Body: EphemerisData
 */
router.post('/upload', async (req, res) => {
  try {
    const { object_id, object_name, object_type, points, interpolation_order,
            reference_frame, source, valid_from, valid_until } = req.body;

    if (!object_id) {
      return errorResponse(res, 400, 'Missing required field: object_id');
    }
    if (!points || !Array.isArray(points) || points.length === 0) {
      return errorResponse(res, 400, 'Missing or empty required field: points');
    }

    await grpcCall('UploadEphemeris', {
      object_id,
      object_name: object_name || object_id,
      object_type: object_type || 'unknown',
      points,
      interpolation_order: interpolation_order || 1,
      reference_frame: reference_frame || 'J2000',
      source: source || 'manual',
      valid_from: valid_from || null,
      valid_until: valid_until || null,
    });
    res.json({ success: true, message: `Ephemeris uploaded for '${object_name || object_id}'` });
  } catch (err) {
    errorResponse(res, 502, 'Failed to upload ephemeris', err.message);
  }
});

/**
 * POST /api/tracking/stop
 * Stop ephemeris tracking.
 */
router.post('/stop', async (req, res) => {
  try {
    await grpcCall('StopEphemerisTracking', {});
    res.json({ success: true, message: 'Ephemeris tracking stopped' });
  } catch (err) {
    errorResponse(res, 502, 'Failed to stop ephemeris tracking', err.message);
  }
});

/**
 * POST /api/tracking/clear-cache
 * Clear cached ephemeris data.
 */
router.post('/clear-cache', async (req, res) => {
  try {
    await grpcCall('ClearEphemerisCache', {});
    res.json({ success: true, message: 'Ephemeris cache cleared' });
  } catch (err) {
    errorResponse(res, 502, 'Failed to clear ephemeris cache', err.message);
  }
});

module.exports = router;
