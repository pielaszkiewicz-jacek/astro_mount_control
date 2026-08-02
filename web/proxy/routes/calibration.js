/**
 * Calibration Routes — Bootstrap and TPOINT
 */
'use strict';

const express = require('express');
const router = express.Router();
const { grpcCall } = require('../grpc/client');
const { formatCalibrationStatus, formatTPointStatus, errorResponse } = require('../grpc/converters');

/**
 * POST /api/calibration/bootstrap/add
 * Add a bootstrap measurement.
 * Body: { observed_ra, observed_dec, expected_ra, expected_dec, mount_ha?, mount_dec? }
 */
router.post('/bootstrap/add', async (req, res) => {
  try {
    const { observed_ra, observed_dec, expected_ra, expected_dec, mount_ha, mount_dec } = req.body;

    if ([observed_ra, observed_dec, expected_ra, expected_dec].some(v => v === undefined)) {
      return errorResponse(res, 400, 'Missing required fields: observed_ra, observed_dec, expected_ra, expected_dec');
    }

    const result = await grpcCall('AddBootstrapMeasurement', {
      observed_ra, observed_dec, expected_ra, expected_dec,
      mount_ha: mount_ha || 0,
      mount_dec: mount_dec !== undefined ? mount_dec : expected_dec,
    });
    res.json({ success: result.success, measurementCount: result.measurement_count });
  } catch (err) {
    errorResponse(res, 502, 'Failed to add bootstrap measurement', err.message);
  }
});

/**
 * POST /api/calibration/bootstrap/run
 * Run bootstrap calibration.
 */
router.post('/bootstrap/run', async (req, res) => {
  try {
    const result = await grpcCall('RunBootstrapCalibration', {});
    res.json(formatCalibrationStatus(result));
  } catch (err) {
    errorResponse(res, 502, 'Bootstrap calibration failed', err.message);
  }
});

/**
 * POST /api/calibration/bootstrap/clear
 * Clear all bootstrap measurements.
 */
router.post('/bootstrap/clear', async (req, res) => {
  try {
    await grpcCall('ClearBootstrapMeasurements', {});
    res.json({ success: true, message: 'Bootstrap measurements cleared' });
  } catch (err) {
    errorResponse(res, 502, 'Failed to clear bootstrap measurements', err.message);
  }
});

/**
 * POST /api/calibration/tpoint/add
 * Add a TPOINT measurement.
 */
router.post('/tpoint/add', async (req, res) => {
  try {
    const { observed_ra, observed_dec, expected_ra, expected_dec, mount_ha, mount_dec, temperature, pressure, humidity } = req.body;

    if ([observed_ra, observed_dec, expected_ra, expected_dec].some(v => v === undefined)) {
      return errorResponse(res, 400, 'Missing required fields: observed_ra, observed_dec, expected_ra, expected_dec');
    }

    const result = await grpcCall('AddTPointMeasurement', {
      observed_ra, observed_dec, expected_ra, expected_dec,
      mount_ha: mount_ha || 0,
      mount_dec: mount_dec || expected_dec,
      temperature: temperature || 15.0,
      pressure: pressure || 1013.25,
      humidity: humidity || 0.5,
    });
    res.json({ success: result.success, measurementCount: result.measurement_count });
  } catch (err) {
    errorResponse(res, 502, 'Failed to add TPOINT measurement', err.message);
  }
});

/**
 * POST /api/calibration/tpoint/run
 * Run TPOINT calibration.
 */
router.post('/tpoint/run', async (req, res) => {
  try {
    const result = await grpcCall('RunTPointCalibration', {});
    res.json(formatTPointStatus(result));
  } catch (err) {
    errorResponse(res, 502, 'TPOINT calibration failed', err.message);
  }
});

/**
 * POST /api/calibration/tpoint/clear
 * Clear all TPOINT measurements.
 */
router.post('/tpoint/clear', async (req, res) => {
  try {
    await grpcCall('ClearTPointMeasurements', {});
    res.json({ success: true, message: 'TPOINT measurements cleared' });
  } catch (err) {
    errorResponse(res, 502, 'Failed to clear TPOINT measurements', err.message);
  }
});

module.exports = router;
