/**
 * Calibration Routes — Bootstrap and TPOINT
 */
'use strict';

const express = require('express');
const router = express.Router();
const { grpcCall } = require('../grpc/client');
const { formatCalibrationStatus, formatTPointStatus, errorResponse } = require('../grpc/converters');

// ─── Bootstrap Status ─────────────────────────────────────────────────────

/**
 * GET /api/calibration/bootstrap/status
 * Returns current bootstrap calibration status.
 */
router.get('/bootstrap/status', async (req, res) => {
  try {
    const status = await grpcCall('GetBootstrapStatus', {});
    res.json({
      calibrated: status.calibrated || false,
      last_calibration: status.last_calibration || null,
      measurement_count: status.measurement_count || 0,
      current_alignment_error_arcsec: status.current_alignment_error_arcsec || 0,
      ready_for_tpoint: status.ready_for_tpoint || false,
      state: status.state || 'NOT_CALIBRATED',
      state_message: status.state_message || '',
      min_measurements_required: status.min_measurements_required || 0,
      min_measurements_for_tpoint: status.min_measurements_for_tpoint || 0,
      bootstrap_mode: status.bootstrap_mode || 0,
      encoder_type_absolute: status.encoder_type_absolute || false,
      reference_position_known: status.reference_position_known || false,
      estimated_encoder_offset_deg: status.estimated_encoder_offset_deg || 0,
      manual_measurements_needed: status.manual_measurements_needed || 0,
    });
  } catch (err) {
    errorResponse(res, 502, 'Failed to load bootstrap status', err.message);
  }
});

// ─── Bootstrap Measurements ───────────────────────────────────────────────

/**
 * POST /api/calibration/bootstrap/measurements
 * Add a bootstrap measurement.
 * Body: { observed_ra, observed_dec, expected_ra, expected_dec, mount_ha?, mount_dec? }
 */
router.post('/bootstrap/measurements', async (req, res) => {
  try {
    const { observed_ra, observed_dec, expected_ra, expected_dec, mount_ha, mount_dec } = req.body;

    if ([observed_ra, observed_dec, expected_ra, expected_dec].some(v => v === undefined || v === null)) {
      return errorResponse(res, 400, 'Missing required fields: observed_ra, observed_dec, expected_ra, expected_dec');
    }

    await grpcCall('AddBootstrapMeasurement', {
      observed: {
        ra: observed_ra,
        dec: observed_dec,
      },
      expected: {
        ra: expected_ra,
        dec: expected_dec,
      },
      mount_position: {
        axis1: mount_ha || 0,
        axis2: mount_dec !== undefined ? mount_dec : expected_dec,
      },
    });

    // Fetch updated status for measurement count
    const status = await grpcCall('GetBootstrapStatus', {});
    res.json({ success: true, measurementCount: status.measurement_count || 0 });
  } catch (err) {
    errorResponse(res, 502, 'Failed to add bootstrap measurement', err.message);
  }
});

/**
 * DELETE /api/calibration/bootstrap/measurements
 * Clear all bootstrap measurements.
 */
router.delete('/bootstrap/measurements', async (req, res) => {
  try {
    await grpcCall('ClearBootstrapMeasurements', {});
    res.json({ success: true, message: 'Bootstrap measurements cleared' });
  } catch (err) {
    errorResponse(res, 502, 'Failed to clear bootstrap measurements', err.message);
  }
});

// ─── Bootstrap Calibration ────────────────────────────────────────────────

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
 * Clear all bootstrap measurements (legacy path alias).
 */
router.post('/bootstrap/clear', async (req, res) => {
  try {
    await grpcCall('ClearBootstrapMeasurements', {});
    res.json({ success: true, message: 'Bootstrap measurements cleared' });
  } catch (err) {
    errorResponse(res, 502, 'Failed to clear bootstrap measurements', err.message);
  }
});

// ─── Bootstrap Mode ───────────────────────────────────────────────────────

/**
 * GET /api/calibration/bootstrap/mode
 * Returns current bootstrap mode.
 */
router.get('/bootstrap/mode', async (req, res) => {
  try {
    const status = await grpcCall('GetBootstrapStatus', {});
    res.json({
      mode: status.bootstrap_mode || 0,
      encoder_type_absolute: status.encoder_type_absolute || false,
    });
  } catch (err) {
    errorResponse(res, 502, 'Failed to load bootstrap mode', err.message);
  }
});

/**
 * POST /api/calibration/bootstrap/mode
 * Set bootstrap mode.
 * Body: { mode: 0=MANUAL, 1=HYBRID, 2=AUTOMATIC }
 */
router.post('/bootstrap/mode', async (req, res) => {
  try {
    const { mode } = req.body;
    if (mode === undefined || mode === null) {
      return errorResponse(res, 400, 'Missing required field: mode');
    }

    await grpcCall('SetBootstrapMode', { mode });
    res.json({ success: true, mode });
  } catch (err) {
    errorResponse(res, 502, 'Failed to set bootstrap mode', err.message);
  }
});

// ─── Auto-Bootstrap ───────────────────────────────────────────────────────

/**
 * POST /api/calibration/bootstrap/auto-run
 * Start automatic bootstrap procedure.
 * Body: { target_star_names?, min_measurements?, max_alignment_error_arcsec?, proceed_to_tpoint? }
 */
router.post('/bootstrap/auto-run', async (req, res) => {
  try {
    const { target_star_names, min_measurements, max_alignment_error_arcsec, proceed_to_tpoint } = req.body;

    await grpcCall('RunAutomaticBootstrap', {
      target_star_names: target_star_names || [],
      min_measurements: min_measurements || 3,
      max_alignment_error_arcsec: max_alignment_error_arcsec || 60.0,
      proceed_to_tpoint: proceed_to_tpoint || false,
    });
    res.json({ success: true, message: 'Automatic bootstrap started' });
  } catch (err) {
    errorResponse(res, 502, 'Automatic bootstrap failed', err.message);
  }
});

/**
 * GET /api/calibration/bootstrap/auto-status
 * Returns current automatic bootstrap status and progress.
 */
router.get('/bootstrap/auto-status', async (req, res) => {
  try {
    const status = await grpcCall('GetAutoBootstrapStatus', {});
    res.json({
      state: status.state || 'IDLE',
      state_message: status.state_message || '',
      current_step: status.current_step || 0,
      total_steps: status.total_steps || 0,
      current_star: status.current_star || '',
      measurement_count: status.measurement_count || 0,
      error_message: status.error_message || '',
    });
  } catch (err) {
    errorResponse(res, 502, 'Failed to load auto-bootstrap status', err.message);
  }
});

// ─── TPOINT Status ────────────────────────────────────────────────────────

/**
 * GET /api/calibration/tpoint/parameters
 * Returns TPOINT calibration parameters and status.
 */
router.get('/tpoint/parameters', async (req, res) => {
  try {
    const params = await grpcCall('GetTPointParameters', {});
    res.json({
      calibrated: params.calibrated || false,
      coefficients: params.coefficients || [],
      chi_squared: params.chi_squared || 0,
      last_update: params.last_update || null,
    });
  } catch (err) {
    errorResponse(res, 502, 'Failed to load TPOINT parameters', err.message);
  }
});

// ─── TPOINT Measurements ──────────────────────────────────────────────────

/**
 * POST /api/calibration/tpoint/measurements
 * Add a TPOINT measurement.
 * Body: Full Measurement fields
 */
router.post('/tpoint/measurements', async (req, res) => {
  try {
    const { observed_ra, observed_dec, expected_ra, expected_dec,
            mount_ha, mount_dec, temperature, pressure, humidity,
            proper_motion_ra, proper_motion_dec, parallax, epoch } = req.body;

    if ([observed_ra, observed_dec, expected_ra, expected_dec].some(v => v === undefined || v === null)) {
      return errorResponse(res, 400, 'Missing required fields: observed_ra, observed_dec, expected_ra, expected_dec');
    }

    await grpcCall('AddTPointMeasurement', {
      observed: {
        ra: observed_ra,
        dec: observed_dec,
        temperature: temperature || 15.0,
        pressure: pressure || 1013.25,
        humidity: humidity || 0.5,
      },
      expected: {
        ra: expected_ra,
        dec: expected_dec,
        pm_ra: proper_motion_ra || 0,
        pm_dec: proper_motion_dec || 0,
        parallax: parallax || 0,
        epoch: epoch || 2000.0,
      },
      mount_position: {
        axis1: mount_ha || 0,
        axis2: mount_dec || expected_dec,
      },
      temperature: temperature || 15.0,
      pressure: pressure || 1013.25,
      humidity: humidity || 0.5,
    });

    // Fetch updated TPOINT parameters for measurement count
    const params = await grpcCall('GetTPointParameters', {});
    res.json({ success: true, measurementCount: params.measurement_count || 0 });
  } catch (err) {
    errorResponse(res, 502, 'Failed to add TPOINT measurement', err.message);
  }
});

/**
 * DELETE /api/calibration/tpoint/measurements
 * Clear all TPOINT measurements.
 */
router.delete('/tpoint/measurements', async (req, res) => {
  try {
    await grpcCall('ClearTPointMeasurements', {});
    res.json({ success: true, message: 'TPOINT measurements cleared' });
  } catch (err) {
    errorResponse(res, 502, 'Failed to clear TPOINT measurements', err.message);
  }
});

// ─── TPOINT Calibration ───────────────────────────────────────────────────

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
 * Clear all TPOINT measurements (legacy path alias).
 */
router.post('/tpoint/clear', async (req, res) => {
  try {
    await grpcCall('ClearTPointMeasurements', {});
    res.json({ success: true, message: 'TPOINT measurements cleared' });
  } catch (err) {
    errorResponse(res, 502, 'Failed to clear TPOINT measurements', err.message);
  }
});

// ─── Bootstrap Add (legacy alias for backward compatibility) ──────────────

/**
 * POST /api/calibration/bootstrap/add
 * Legacy path — same as POST /bootstrap/measurements
 */
router.post('/bootstrap/add', async (req, res) => {
  try {
    const { observed_ra, observed_dec, expected_ra, expected_dec, mount_ha, mount_dec } = req.body;

    if ([observed_ra, observed_dec, expected_ra, expected_dec].some(v => v === undefined || v === null)) {
      return errorResponse(res, 400, 'Missing required fields: observed_ra, observed_dec, expected_ra, expected_dec');
    }

    await grpcCall('AddBootstrapMeasurement', {
      observed: { ra: observed_ra, dec: observed_dec },
      expected: { ra: expected_ra, dec: expected_dec },
      mount_position: {
        axis1: mount_ha || 0,
        axis2: mount_dec !== undefined ? mount_dec : expected_dec,
      },
    });

    const status = await grpcCall('GetBootstrapStatus', {});
    res.json({ success: true, measurementCount: status.measurement_count || 0 });
  } catch (err) {
    errorResponse(res, 502, 'Failed to add bootstrap measurement', err.message);
  }
});

/**
 * POST /api/calibration/tpoint/add
 * Legacy path — same as POST /tpoint/measurements
 */
router.post('/tpoint/add', async (req, res) => {
  try {
    const { observed_ra, observed_dec, expected_ra, expected_dec,
            mount_ha, mount_dec, temperature, pressure, humidity } = req.body;

    if ([observed_ra, observed_dec, expected_ra, expected_dec].some(v => v === undefined || v === null)) {
      return errorResponse(res, 400, 'Missing required fields: observed_ra, observed_dec, expected_ra, expected_dec');
    }

    await grpcCall('AddTPointMeasurement', {
      observed: {
        ra: observed_ra, dec: observed_dec,
        temperature: temperature || 15.0, pressure: pressure || 1013.25, humidity: humidity || 0.5,
      },
      expected: { ra: expected_ra, dec: expected_dec },
      mount_position: { axis1: mount_ha || 0, axis2: mount_dec || expected_dec },
      temperature: temperature || 15.0, pressure: pressure || 1013.25, humidity: humidity || 0.5,
    });

    const params = await grpcCall('GetTPointParameters', {});
    res.json({ success: true, measurementCount: params.measurement_count || 0 });
  } catch (err) {
    errorResponse(res, 502, 'Failed to add TPOINT measurement', err.message);
  }
});

module.exports = router;
