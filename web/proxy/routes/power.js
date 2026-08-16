/**
 * Power Management Routes
 *
 * Proxies power operations to the PowerService gRPC service
 * (astro_power_server, default port 50056) via a dedicated client.
 * No silent simulated fallback — an unreachable service returns an explicit
 * 503 so the UI never shows fake data (P1 fix).
 */
'use strict';

const express = require('express');
const router = express.Router();
const { powerGrpcCall } = require('../grpc/client');
const { errorResponse } = require('../grpc/converters');

/**
 * GET /api/power/status
 * Returns current power status.
 */
router.get('/status', async (req, res) => {
  try {
    const status = await powerGrpcCall('GetPowerStatus', {});
    res.json({
      voltage_v: status.voltage_v || 0,
      current_a: status.current_a || 0,
      power_w: status.power_w || 0,
      capacity_ah: status.capacity_ah || 0,
      charge_percent: status.charge_percent || 0,
      charging: status.charging || false,
      on_battery: status.on_battery || false,
      temperature_c: status.temperature_c || 0,
      estimated_runtime_min: status.estimated_runtime_min || 0,
      input_voltage_v: status.input_voltage_v || 0,
      output_voltage_v: status.output_voltage_v || 0,
      outputs: (status.outputs || []).map(o => ({
        id: o.id,
        name: o.name,
        enabled: o.enabled,
        voltage_v: o.voltage_v,
        current_a: o.current_a,
        overload: o.overload,
      })),
    });
  } catch (err) {
    errorResponse(res, 503, 'Power service unavailable', err.message);
  }
});

/**
 * POST /api/power/output
 * Set power output state.
 * Body: { output_id: number, enabled: boolean }
 */
router.post('/output', async (req, res) => {
  try {
    const { output_id, enabled } = req.body;
    await powerGrpcCall('SetPowerOutput', { output_id, enabled: !!enabled });
    res.json({ success: true });
  } catch (err) {
    errorResponse(res, 503, 'Power service unavailable', err.message);
  }
});

/**
 * GET /api/power/history
 * Returns power history.
 */
router.get('/history', async (req, res) => {
  try {
    const { start_time, end_time, max_points } = req.query;
    const history = await powerGrpcCall('GetPowerHistory', {
      start_time: start_time || null,
      end_time: end_time || null,
      max_points: parseInt(max_points) || 100,
    });
    res.json({
      readings: (history.readings || []).map(r => ({
        voltage_v: r.voltage_v,
        current_a: r.current_a,
        power_w: r.power_w,
        charge_percent: r.charge_percent,
        timestamp: r.timestamp,
      })),
      total_points: history.total_points || 0,
    });
  } catch (err) {
    errorResponse(res, 503, 'Power service unavailable', err.message);
  }
});

module.exports = router;
