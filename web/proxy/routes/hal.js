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
    // Speed-dependent PID gain scheduling (RAM writes, volatile)
    hal_mf7025v2_speed_pid_adaptation_enabled:    (proto.mf7025v2 && proto.mf7025v2.speed_pid_adaptation_enabled !== undefined) ? proto.mf7025v2.speed_pid_adaptation_enabled : false,
    hal_mf7025v2_speed_pid_adaptation_update_ms:  (proto.mf7025v2 && proto.mf7025v2.speed_pid_adaptation_update_ms) || 50,
    hal_mf7025v2_speed_pid_schedule:              (proto.mf7025v2 && Array.isArray(proto.mf7025v2.speed_pid_schedule))
      ? proto.mf7025v2.speed_pid_schedule.map((e) => ({
          speed_rpm: e.speed_rpm || 0,
          current_kp: e.current_kp || 0,
          current_ki: e.current_ki || 0,
          speed_kp: e.speed_kp || 0,
          speed_ki: e.speed_ki || 0,
          speed_filter_hz: e.speed_filter_hz || 0,
          position_kp: e.position_kp || 0,
          position_ki: e.position_ki || 0,
        }))
      : [],
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
    // PID params
    hal_pid_kp:                (proto.pid_params && proto.pid_params.kp) || 0,
    hal_pid_ki:                (proto.pid_params && proto.pid_params.ki) || 0,
    hal_pid_kd:                (proto.pid_params && proto.pid_params.kd) || 0,
    hal_pid_integral_limit:    (proto.pid_params && proto.pid_params.integral_limit) || 0,
    hal_pid_output_limit:      (proto.pid_params && proto.pid_params.output_limit) || 0,
    hal_pid_anti_windup_gain:  (proto.pid_params && proto.pid_params.anti_windup_gain) || 0,
    hal_pid_enable_anti_windup:(proto.pid_params && proto.pid_params.enable_anti_windup) || false,
    // Safety config
    hal_safety_enable_limits:                (proto.safety && proto.safety.enable_limits) || false,
    hal_safety_enable_emergency_stop:        (proto.safety && proto.safety.enable_emergency_stop) || false,
    hal_safety_emergency_stop_timeout_ms:    (proto.safety && proto.safety.emergency_stop_timeout_ms) || 0,
    hal_safety_enable_temperature_monitoring:(proto.safety && proto.safety.enable_temperature_monitoring) || false,
    hal_safety_enable_current_monitoring:    (proto.safety && proto.safety.enable_current_monitoring) || false,
    hal_safety_enable_voltage_monitoring:    (proto.safety && proto.safety.enable_voltage_monitoring) || false,
    hal_safety_min_voltage:     (proto.safety && proto.safety.min_voltage) || 0,
    hal_safety_max_voltage:     (proto.safety && proto.safety.max_voltage) || 0,
    hal_safety_monitoring_rate: (proto.safety && proto.safety.monitoring_rate) || 0,
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
 *
 * Supported fields (mapped to proto):
 *   hal_mf7025v2_can_trace              → config.can_trace
 *   hal_mf7025v2_can_trace_read_state   → config.can_trace_read_state
 *   hal_mf7025v2_can_interface          → config.mf7025v2.can_interface
 *   hal_mf7025v2_bitrate                → config.mf7025v2.bitrate
 *   hal_mf7025v2_sdo_timeout_ms         → config.mf7025v2.sdo_timeout_ms
 *   hal_mf7025v2_position_units_per_degree → config.mf7025v2.position_units_per_degree
 *   hal_mf7025v2_velocity_units_per_dps    → config.mf7025v2.velocity_units_per_dps
 *   hal_axis0_invert_direction          → config.axes[0].invert_direction
 *   hal_axis1_invert_direction          → config.axes[1].invert_direction
 *   hal_gamepad_*                       → config.gamepad.*
 *   hal_pid_*                           → config.pid_params.*
 *   hal_safety_*                        → config.safety.*
 *
 * Fields NOT available in the gRPC proto (configure via config file):
 *   hal_interface_type, hal_can_*, hal_heartbeat_interval_ms, hal_pdo_mapping_mode
 */
router.post('/config', async (req, res) => {
  try {
    const updateData = req.body;
    if (!updateData || typeof updateData !== 'object') {
      return errorResponse(res, 400, 'Request body must be a JSON object');
    }

    // Build the nested proto structure for HALConfigRequest.
    // HALConfigRequest { config: HALConfig { axes[], gamepad, can_trace, can_trace_read_state } }
    const configMsg = {};

    // ── Direct HALConfig fields ───────────────────────────────────────────
    if ('hal_mf7025v2_can_trace' in updateData) {
      configMsg.can_trace = !!updateData.hal_mf7025v2_can_trace;
    }
    if ('hal_mf7025v2_can_trace_read_state' in updateData) {
      configMsg.can_trace_read_state = !!updateData.hal_mf7025v2_can_trace_read_state;
    }

    // ── MF7025v2 full config (CAN interface, bitrate, timeouts, scaling,
    //    speed-dependent PID gain schedule) ─────────────────────────────
    const mf7025v2Fields = [
      'hal_mf7025v2_can_interface',
      'hal_mf7025v2_bitrate',
      'hal_mf7025v2_sdo_timeout_ms',
      'hal_mf7025v2_position_units_per_degree',
      'hal_mf7025v2_velocity_units_per_dps',
      'hal_mf7025v2_speed_pid_adaptation_enabled',
      'hal_mf7025v2_speed_pid_adaptation_update_ms',
      'hal_mf7025v2_speed_pid_schedule',
    ];
    const hasMf7025v2Data = mf7025v2Fields.some(f => f in updateData);
    if (hasMf7025v2Data) {
      const mf7 = {};
      if ('hal_mf7025v2_can_interface' in updateData) {
        mf7.can_interface = String(updateData.hal_mf7025v2_can_interface);
      }
      if ('hal_mf7025v2_bitrate' in updateData) {
        mf7.bitrate = Number(updateData.hal_mf7025v2_bitrate) || 0;
      }
      if ('hal_mf7025v2_sdo_timeout_ms' in updateData) {
        mf7.sdo_timeout_ms = Number(updateData.hal_mf7025v2_sdo_timeout_ms) || 0;
      }
      if ('hal_mf7025v2_position_units_per_degree' in updateData) {
        mf7.position_units_per_degree = Number(updateData.hal_mf7025v2_position_units_per_degree) || 0;
      }
      if ('hal_mf7025v2_velocity_units_per_dps' in updateData) {
        mf7.velocity_units_per_dps = Number(updateData.hal_mf7025v2_velocity_units_per_dps) || 0;
      }
      // Speed-dependent PID gain scheduling (RAM writes, volatile)
      if ('hal_mf7025v2_speed_pid_adaptation_enabled' in updateData) {
        mf7.speed_pid_adaptation_enabled = !!updateData.hal_mf7025v2_speed_pid_adaptation_enabled;
      }
      if ('hal_mf7025v2_speed_pid_adaptation_update_ms' in updateData) {
        mf7.speed_pid_adaptation_update_ms = Number(updateData.hal_mf7025v2_speed_pid_adaptation_update_ms) || 50;
      }
      if ('hal_mf7025v2_speed_pid_schedule' in updateData &&
          Array.isArray(updateData.hal_mf7025v2_speed_pid_schedule)) {
        mf7.speed_pid_schedule = updateData.hal_mf7025v2_speed_pid_schedule.map((e) => ({
          speed_rpm: Number(e.speed_rpm) || 0,
          current_kp: Number(e.current_kp) || 0,
          current_ki: Number(e.current_ki) || 0,
          speed_kp: Number(e.speed_kp) || 0,
          speed_ki: Number(e.speed_ki) || 0,
          speed_filter_hz: Number(e.speed_filter_hz) || 0,
          position_kp: Number(e.position_kp) || 0,
          position_ki: Number(e.position_ki) || 0,
        }));
      }
      configMsg.mf7025v2 = mf7;
    }

    // ── Axis invert direction ─────────────────────────────────────────────
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

    // ── Gamepad config ────────────────────────────────────────────────────
    const gamepadFields = [
      'hal_gamepad_device_path',
      'hal_gamepad_deadzone',
      'hal_gamepad_sensitivity',
      'hal_gamepad_poll_interval_ms',
      'hal_gamepad_autostart',
    ];
    const hasGamepadData = gamepadFields.some(f => f in updateData);
    if (hasGamepadData) {
      const gp = {};
      if ('hal_gamepad_device_path' in updateData) {
        gp.device_path = String(updateData.hal_gamepad_device_path);
      }
      if ('hal_gamepad_deadzone' in updateData) {
        gp.dead_zone = Number(updateData.hal_gamepad_deadzone) || 0;
      }
      if ('hal_gamepad_sensitivity' in updateData) {
        gp.sensitivity = Number(updateData.hal_gamepad_sensitivity) || 1.0;
      }
      if ('hal_gamepad_poll_interval_ms' in updateData) {
        const pollMs = Number(updateData.hal_gamepad_poll_interval_ms);
        gp.read_frequency = pollMs > 0 ? Math.round(1000 / pollMs) : 50;
      }
      if ('hal_gamepad_autostart' in updateData) {
        gp.autostart = !!updateData.hal_gamepad_autostart;
      }
      configMsg.gamepad = gp;
    }

    // ── PID params config ────────────────────────────────────────────────
    const pidFields = [
      'hal_pid_kp', 'hal_pid_ki', 'hal_pid_kd',
      'hal_pid_integral_limit', 'hal_pid_output_limit',
      'hal_pid_anti_windup_gain', 'hal_pid_enable_anti_windup',
    ];
    const hasPidData = pidFields.some(f => f in updateData);
    if (hasPidData) {
      const pid = {};
      if ('hal_pid_kp' in updateData) pid.kp = Number(updateData.hal_pid_kp) || 0;
      if ('hal_pid_ki' in updateData) pid.ki = Number(updateData.hal_pid_ki) || 0;
      if ('hal_pid_kd' in updateData) pid.kd = Number(updateData.hal_pid_kd) || 0;
      if ('hal_pid_integral_limit' in updateData) pid.integral_limit = Number(updateData.hal_pid_integral_limit) || 0;
      if ('hal_pid_output_limit' in updateData) pid.output_limit = Number(updateData.hal_pid_output_limit) || 0;
      if ('hal_pid_anti_windup_gain' in updateData) pid.anti_windup_gain = Number(updateData.hal_pid_anti_windup_gain) || 0;
      if ('hal_pid_enable_anti_windup' in updateData) pid.enable_anti_windup = !!updateData.hal_pid_enable_anti_windup;
      configMsg.pid_params = pid;
    }

    // ── Safety config ────────────────────────────────────────────────────
    const safetyFields = [
      'hal_safety_enable_limits', 'hal_safety_enable_emergency_stop',
      'hal_safety_emergency_stop_timeout_ms',
      'hal_safety_enable_temperature_monitoring', 'hal_safety_enable_current_monitoring',
      'hal_safety_enable_voltage_monitoring',
      'hal_safety_min_voltage', 'hal_safety_max_voltage', 'hal_safety_monitoring_rate',
    ];
    const hasSafetyData = safetyFields.some(f => f in updateData);
    if (hasSafetyData) {
      const saf = {};
      if ('hal_safety_enable_limits' in updateData) saf.enable_limits = !!updateData.hal_safety_enable_limits;
      if ('hal_safety_enable_emergency_stop' in updateData) saf.enable_emergency_stop = !!updateData.hal_safety_enable_emergency_stop;
      if ('hal_safety_emergency_stop_timeout_ms' in updateData) saf.emergency_stop_timeout_ms = Number(updateData.hal_safety_emergency_stop_timeout_ms) || 0;
      if ('hal_safety_enable_temperature_monitoring' in updateData) saf.enable_temperature_monitoring = !!updateData.hal_safety_enable_temperature_monitoring;
      if ('hal_safety_enable_current_monitoring' in updateData) saf.enable_current_monitoring = !!updateData.hal_safety_enable_current_monitoring;
      if ('hal_safety_enable_voltage_monitoring' in updateData) saf.enable_voltage_monitoring = !!updateData.hal_safety_enable_voltage_monitoring;
      if ('hal_safety_min_voltage' in updateData) saf.min_voltage = Number(updateData.hal_safety_min_voltage) || 0;
      if ('hal_safety_max_voltage' in updateData) saf.max_voltage = Number(updateData.hal_safety_max_voltage) || 0;
      if ('hal_safety_monitoring_rate' in updateData) saf.monitoring_rate = Number(updateData.hal_safety_monitoring_rate) || 0;
      configMsg.safety = saf;
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
