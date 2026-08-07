/**
 * HAL Configuration Routes — Get/Update hardware abstraction layer settings
 *
 * GET  /api/hal/config   — Returns the full HAL configuration (flattened)
 * POST /api/hal/config   — Updates HAL configuration fields
 */
'use strict';

const express = require('express');
const router = express.Router();
const { grpcCall } = require('../grpc/client');
const { errorResponse } = require('../grpc/converters');

/**
 * Flatten a protobuf HALConfig message into the JSON shape
 * the SPA settings component expects.
 */
function flattenHALConfig(proto) {
  return {
    hal_interface_type:        (proto.type !== undefined && proto.type !== null) ? String(proto.type) : 'simulated',
    hal_can_interface:         (proto.canopen && proto.canopen.interface_name) || '',
    hal_can_node_id:           (proto.canopen && proto.canopen.node_id) || 0,
    hal_can_baud_rate:         (proto.canopen && proto.canopen.bitrate) || 0,
    hal_heartbeat_interval_ms: (proto.canopen && proto.canopen.nmt && proto.canopen.nmt.heartbeat_period_ms) || 0,
    hal_pdo_mapping_mode:      '',
    // MF7025v2
    hal_mf7025v2_can_interface:           (proto.mf7025v2 && proto.mf7025v2.can_interface) || '',
    hal_mf7025v2_bitrate:                 (proto.mf7025v2 && proto.mf7025v2.bitrate) || 0,
    hal_mf7025v2_sdo_timeout_ms:          (proto.mf7025v2 && proto.mf7025v2.sdo_timeout_ms) || 0,
    hal_mf7025v2_position_units_per_degree:  (proto.mf7025v2 && proto.mf7025v2.position_units_per_degree) || 0,
    hal_mf7025v2_velocity_units_per_dps:     (proto.mf7025v2 && proto.mf7025v2.velocity_units_per_dps) || 0,
    // Gamepad
    hal_gamepad_device_path:   (proto.gamepad && proto.gamepad.device_path) || '',
    hal_gamepad_deadzone:      (proto.gamepad && proto.gamepad.deadzone) || 0.15,
    hal_gamepad_sensitivity:   (proto.gamepad && proto.gamepad.sensitivity) || 1.0,
    hal_gamepad_poll_interval_ms: (proto.gamepad && proto.gamepad.update_rate_hz) ? Math.round(1000 / proto.gamepad.update_rate_hz) : 20,
    hal_gamepad_autostart:     (proto.gamepad && proto.gamepad.autostart) || false,
    // MF7025v2 CAN trace flags
    hal_mf7025v2_can_trace:            (proto.can_trace !== undefined) ? proto.can_trace : true,
    hal_mf7025v2_can_trace_read_state: (proto.can_trace_read_state !== undefined) ? proto.can_trace_read_state : false,
    // Per-axis invert direction
    hal_axis0_invert_direction: (proto.axes && proto.axes[0]) ? (proto.axes[0].invert_direction || false) : false,
    hal_axis1_invert_direction: (proto.axes && proto.axes[1]) ? (proto.axes[1].invert_direction || false) : false,
  };
}

/**
 * GET /api/hal/config
 * Returns the flattened HAL configuration.
 */
router.get('/config', async (req, res) => {
  try {
    const halProto = await grpcCall('GetHALConfig', {});
    res.json(flattenHALConfig(halProto));
  } catch (err) {
    errorResponse(res, 503, 'Failed to load HAL configuration: ' + err.message, err.details || '');
  }
});

/**
 * POST /api/hal/config
 * Update HAL configuration.  Accepts a flat JSON object whose keys
 * are mapped to the corresponding protobuf fields in SetHALConfig.
 */
router.post('/config', async (req, res) => {
  try {
    const updateData = req.body;
    if (!updateData || typeof updateData !== 'object') {
      return errorResponse(res, 400, 'Request body must be a JSON object');
    }

    // Build the nested proto structure for HALConfigRequest.
    // HALConfigRequest { config: HALConfig { axes[], can_trace, can_trace_read_state, ... } }
    const configMsg = {};

    // Map flat UI keys to direct HALConfig proto fields
    if ('hal_mf7025v2_can_trace' in updateData) {
      configMsg.can_trace = updateData.hal_mf7025v2_can_trace;
    }
    if ('hal_mf7025v2_can_trace_read_state' in updateData) {
      configMsg.can_trace_read_state = updateData.hal_mf7025v2_can_trace_read_state;
    }

    // Map per-axis invert direction to nested axes[] structure
    // Proto: HALConfig.axes[] = AxisConfig { id, invert_direction }
    if ('hal_axis0_invert_direction' in updateData || 'hal_axis1_invert_direction' in updateData) {
      configMsg.axes = [];
      if ('hal_axis0_invert_direction' in updateData) {
        configMsg.axes.push({
          id: 0,
          invert_direction: !!updateData.hal_axis0_invert_direction,
        });
      }
      if ('hal_axis1_invert_direction' in updateData) {
        configMsg.axes.push({
          id: 1,
          invert_direction: !!updateData.hal_axis1_invert_direction,
        });
      }
    }

    const protoData = { config: configMsg };
    await grpcCall('SetHALConfig', protoData);
    res.json({ success: true });
  } catch (err) {
    errorResponse(res, 502, 'Failed to update HAL configuration', err.message);
  }
});

/**
 * GET /api/hal/gamepad/state
 * Returns the live gamepad state (axes, buttons, connection).
 */
router.get('/gamepad/state', async (req, res) => {
  try {
    const halStatus = await grpcCall('GetHALStatus', {});
    const gamepad = halStatus.gamepad || {};
    res.json(gamepad);
  } catch (err) {
    errorResponse(res, 503, 'Failed to get gamepad state: ' + err.message, err.details || '');
  }
});

/**
 * POST /api/hal/gamepad/start
 * Starts the gamepad manual-control loop (sends axis velocity commands).
 */
router.post('/gamepad/start', async (req, res) => {
  try {
    await grpcCall('StartGamepad', {});
    res.json({ success: true, message: 'Gamepad control started' });
  } catch (err) {
    errorResponse(res, 502, 'Failed to start gamepad control: ' + err.message, err.details || '');
  }
});

/**
 * POST /api/hal/gamepad/stop
 * Stops the gamepad manual-control loop.
 */
router.post('/gamepad/stop', async (req, res) => {
  try {
    await grpcCall('StopGamepad', {});
    res.json({ success: true, message: 'Gamepad control stopped' });
  } catch (err) {
    errorResponse(res, 502, 'Failed to stop gamepad control: ' + err.message, err.details || '');
  }
});

/**
 * POST /api/hal/gamepad/mode
 * Sets the gamepad navigation mode.
 * Body: { mode: number } — 0=RAW, 1=CELESTIAL, 2=ALT_AZ, 3=PRECISION
 */
router.post('/gamepad/mode', async (req, res) => {
  try {
    const { mode } = req.body;
    if (mode === undefined || mode === null) {
      return errorResponse(res, 400, 'Missing required field: mode');
    }
    await grpcCall('SetGamepadMode', { mode });
    res.json({ success: true, message: 'Gamepad mode set to ' + mode });
  } catch (err) {
    errorResponse(res, 502, 'Failed to set gamepad mode: ' + err.message, err.details || '');
  }
});

module.exports = router;
