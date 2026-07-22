/**
 * Ephemeris Tracking Routes
 */
'use strict';

const express = require('express');
const router = express.Router();
const { grpcCall } = require('../grpc/client');
const { errorResponse } = require('../grpc/converters');

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

module.exports = router;
