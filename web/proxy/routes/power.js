/**
 * Power Management Routes
 *
 * Proxies power operations to the backend Power gRPC service.
 * Falls back to simulated data when the service is unavailable.
 */
'use strict';

const express = require('express');
const router = express.Router();
const { grpcCall } = require('../grpc/client');
const { errorResponse } = require('../grpc/converters');

/**
 * GET /api/power/status
 * Returns current power status.
 */
router.get('/status', async (req, res) => {
  try {
    const status = await grpcCall('GetPowerStatus', {});
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
    // Simulated power status when gRPC unavailable
    res.json({
      voltage_v: 12.5,
      current_a: 1.2,
      power_w: 15.0,
      capacity_ah: 50,
      charge_percent: 85,
      charging: false,
      on_battery: true,
      temperature_c: 22.5,
      estimated_runtime_min: 240,
      input_voltage_v: 13.8,
      output_voltage_v: 12.0,
      outputs: [
        { id: 0, name: 'Mount', enabled: true, voltage_v: 12.0, current_a: 0.8, overload: false },
        { id: 1, name: 'Camera', enabled: true, voltage_v: 12.0, current_a: 0.3, overload: false },
        { id: 2, name: 'Focuser', enabled: false, voltage_v: 12.0, current_a: 0.0, overload: false },
      ],
    });
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
    await grpcCall('SetPowerOutput', { output_id, enabled: !!enabled });
    res.json({ success: true });
  } catch (err) {
    errorResponse(res, 502, 'Failed to set power output', err.message);
  }
});

/**
 * GET /api/power/history
 * Returns power history.
 */
router.get('/history', async (req, res) => {
  try {
    const { start_time, end_time, max_points } = req.query;
    const history = await grpcCall('GetPowerHistory', {
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
    res.json({ readings: [], total_points: 0 });
  }
});

module.exports = router;
