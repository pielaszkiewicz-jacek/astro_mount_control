/**
 * Ephemeris Tracking Routes
 */
'use strict';

const express = require('express');
const router = express.Router();
const { grpcCall } = require('../grpc/client');
const { errorResponse } = require('../grpc/converters');

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

    const result = await grpcCall('StartEphemerisTracking', {
      object_id,
      start_time: start_time || new Date().toISOString(),
      lead_time_seconds: lead_time_seconds || 30.0,
      wait_at_start: wait_at_start !== undefined ? wait_at_start : true,
      enable_prediction: enable_prediction || false,
      tracking_mode: tracking_mode || 'continuous',
    });
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
