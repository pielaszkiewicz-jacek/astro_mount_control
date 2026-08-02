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
    await grpcCall('SetHALConfig', updateData);
    res.json({ success: true });
  } catch (err) {
    errorResponse(res, 502, 'Failed to update HAL configuration', err.message);
  }
});

module.exports = router;
