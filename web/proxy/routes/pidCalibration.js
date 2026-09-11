/**
 * PID Calibration Routes
 *
 * Proxies PID calibration operations to the PidCalibrationService gRPC service,
 * hosted IN-PROCESS inside the mount controller (unified gRPC port 50051).
 * Calibration runs asynchronously on the backend; the UI polls /status while
 * it is running.
 */
'use strict';

const express = require('express');
const router = express.Router();
const { pidCalibrationGrpcCall } = require('../grpc/client');
const { errorResponse } = require('../grpc/converters');

const NUMBER_KEYS = [
  'axis_id',
  'loop',
  'coefficient',
  'min_speed_dps',
  'max_speed_dps',
  'speed_step_dps',
  'min_kp',
  'max_kp',
  'kp_step',
  'min_ki',
  'max_ki',
  'ki_step',
  'min_kd',
  'max_kd',
  'kd_step',
  'base_kp',
  'base_ki',
  'base_kd',
  'measurement_time_s',
  'settle_time_s',
  'current_kp',
  'current_ki',
];

/**
 * Build a proto PidCalibrationRequest from a request body, coercing numeric
 * fields and dropping anything that is not a number.
 */
function buildRequest(body) {
  const req = {};
  NUMBER_KEYS.forEach((key) => {
    const v = body[key];
    if (v !== undefined && v !== null && v !== '') {
      req[key] = Number(v);
    }
  });
  return req;
}

/**
 * GET /api/pidcal/status
 * Returns the current calibration session status and the last result.
 */
router.get('/status', async (req, res) => {
  try {
    const status = await pidCalibrationGrpcCall('GetCalibrationStatus', {});
    res.json({
      running: status.running || false,
      state: status.state || 'IDLE',
      progress_percent: status.progress_percent || 0,
      tested_combinations: status.tested_combinations || 0,
      total_combinations: status.total_combinations || 0,
      current_speed_dps: status.current_speed_dps || 0,
      current_coefficient_value: status.current_coefficient_value || 0,
      current_speed_best: status.current_speed_best || null,
      message: status.message || '',
      last_result: status.last_result || null,
    });
  } catch (err) {
    errorResponse(res, 503, 'PID calibration service unavailable', err.message);
  }
});

/**
 * POST /api/pidcal/start
 * Start a calibration session.
 */
router.post('/start', async (req, res) => {
  try {
    const body = req.body || {};
    const request = buildRequest(body);

    if (request.axis_id === undefined || request.loop === undefined ||
        request.coefficient === undefined) {
      return errorResponse(res, 400, 'Missing required fields: axis_id, loop, coefficient');
    }

    const response = await pidCalibrationGrpcCall('StartCalibration', request, 10);
    res.json({ success: response.started, message: response.message });
  } catch (err) {
    errorResponse(res, 503, 'Failed to start PID calibration', err.message);
  }
});

/**
 * POST /api/pidcal/stop
 * Stop a running calibration session.
 */
router.post('/stop', async (req, res) => {
  try {
    await pidCalibrationGrpcCall('StopCalibration', {});
    res.json({ success: true, message: 'Calibration stop requested' });
  } catch (err) {
    errorResponse(res, 503, 'Failed to stop PID calibration', err.message);
  }
});

/**
 * POST /api/pidcal/save
 * Save the last calibration results to a JSON file.
 * Body: { file_path?: string }
 */
router.post('/save', async (req, res) => {
  try {
    const body = req.body || {};
    const response = await pidCalibrationGrpcCall('SaveCalibrationResults', {
      file_path: body.file_path || '',
    }, 10);
    res.json({ success: response.saved, file_path: response.file_path, message: response.message });
  } catch (err) {
    errorResponse(res, 503, 'Failed to save calibration results', err.message);
  }
});

module.exports = router;
