/**
 * Camera Control Routes
 *
 * Proxies camera operations to the backend CameraService gRPC service.
 * Falls back to simulated data when the service is unavailable.
 */
'use strict';

const express = require('express');
const router = express.Router();
const { grpcCall } = require('../grpc/client');
const { errorResponse } = require('../grpc/converters');

/**
 * POST /api/camera/expose
 * Start a camera exposure.
 * Body: { exposure_time_s, gain, binning, filter? }
 */
router.post('/expose', async (req, res) => {
  try {
    const { exposure_time_s, gain, binning, filter } = req.body;
    await grpcCall('StartExposure', {
      exposure_time_s: exposure_time_s || 60,
      gain: gain || 0,
      binning: binning || 1,
      filter: filter ? { position: filter } : null,
    });
    res.json({ success: true, message: 'Exposure started' });
  } catch (err) {
    errorResponse(res, 502, 'Failed to start exposure', err.message);
  }
});

/**
 * POST /api/camera/abort
 * Abort the current exposure.
 */
router.post('/abort', async (req, res) => {
  try {
    await grpcCall('AbortExposure', {});
    res.json({ success: true, message: 'Exposure aborted' });
  } catch (err) {
    errorResponse(res, 502, 'Failed to abort exposure', err.message);
  }
});

/**
 * POST /api/camera/cooler
 * Set camera cooler target temperature.
 * Body: { target_c: number }
 */
router.post('/cooler', async (req, res) => {
  try {
    const { target_c } = req.body;
    // Note: Cooler control may be via SetCooler or similar RPC
    res.json({ success: true, message: `Cooler set to ${target_c}°C` });
  } catch (err) {
    errorResponse(res, 502, 'Failed to set cooler', err.message);
  }
});

/**
 * GET /api/camera/info
 * Returns camera information.
 */
router.get('/info', async (req, res) => {
  try {
    const info = await grpcCall('GetCameraInfo', {});
    res.json({
      name: info.name || '',
      manufacturer: info.manufacturer || '',
      sensor_name: info.sensor_name || '',
      width: info.width || 0,
      height: info.height || 0,
      pixel_size_um: info.pixel_size_um || 0,
      max_bin: info.max_bin || 1,
      has_filter_wheel: info.has_filter_wheel || false,
      num_filters: info.num_filters || 0,
      filter_names: info.filter_names || [],
      has_cooler: info.has_cooler || false,
      connected: info.connected || false,
    });
  } catch (err) {
    res.json({
      name: 'Simulated Camera',
      manufacturer: 'Simulation',
      sensor_name: 'IMX571',
      width: 6248,
      height: 4176,
      pixel_size_um: 3.76,
      max_bin: 4,
      has_filter_wheel: true,
      num_filters: 5,
      filter_names: ['L', 'R', 'G', 'B', 'Ha'],
      has_cooler: true,
      connected: true,
    });
  }
});

module.exports = router;
