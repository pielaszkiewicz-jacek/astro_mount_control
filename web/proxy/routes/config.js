/**
 * Configuration Routes — Get/Update controller configuration and proxy settings
 */
'use strict';

const express = require('express');
const router = express.Router();
const proxyConfig = require('../config');
const { grpcCall, createGrpcClient, createDbGrpcClient } = require('../grpc/client');
const { errorResponse } = require('../grpc/converters');

// ─── Helpers ──────────────────────────────────────────────────────────────

/**
 * Convert a protobuf Configuration message to a flat JSON object
 * matching what the SPA SettingsComponent expects.
 */
function flattenConfig(proto) {
  // Axis physical parameters helper
  function flattenAxisParams(axis) {
    if (!axis) return {};
    return {
      position_counts_per_degree: axis.position_counts_per_degree || 0,
      velocity_counts_per_deg_s:  axis.velocity_counts_per_deg_s || 0,
      encoder_resolution:         axis.encoder_resolution || 0,
      encoder_counts_per_arcsec:  axis.encoder_counts_per_arcsec || 0,
      encoder_quantization_error: axis.encoder_quantization_error || 0,
      gear_ratio:                 axis.gear_ratio || 0,
      worm_ratio:                 axis.worm_ratio || 0,
      worm_teeth:                 axis.worm_teeth || 0,
      worm_wheel_teeth:           axis.worm_wheel_teeth || 0,
      cyclic_error_amplitude:     axis.cyclic_error_amplitude || 0,
      cyclic_error_period:        axis.cyclic_error_period || 0,
      cyclic_harmonics:           Array.isArray(axis.cyclic_harmonics) ? axis.cyclic_harmonics : [],
      backlash:                   axis.backlash || 0,
      backlash_temp_coeff:        axis.backlash_temp_coeff || 0,
      axis_stiffness:             axis.axis_stiffness || 0,
      torsional_compliance:       axis.torsional_compliance || 0,
      expansion_coeff:            axis.expansion_coeff || 0,
      temp_gear_error_coeff:      axis.temp_gear_error_coeff || 0,
      calibration_temp:           axis.calibration_temp || 0,
      calibration_table:          Array.isArray(axis.calibration_table) ? axis.calibration_table : [],
    };
  }

  return {
    // Logging
    log_level:                    proto.log_level || '',
    log_directory:                proto.log_directory || '',
    log_rotation_days:            proto.log_rotation_days || 0,
    log_max_file_size_mb:         proto.log_max_file_size_mb || 0,
    log_console_output:           proto.log_console_output || false,

    // Network
    grpc_address:                 proto.grpc_address || '',
    grpc_port:                    proto.grpc_port || 0,
    network_max_connections:      proto.network_max_connections || 0,
    network_enable_ssl:           proto.network_enable_ssl || false,
    network_ssl_cert_path:        proto.network_ssl_cert_path || '',
    network_ssl_key_path:         proto.network_ssl_key_path || '',

    // CANopen
    canopen_interface:            proto.canopen_interface || '',
    canopen_node_id:              proto.canopen_node_id || 0,
    canopen_baud_rate:            proto.canopen_baud_rate || 0,
    canopen_enable_sync:          proto.canopen_enable_sync || false,
    canopen_sync_interval_ms:     proto.canopen_sync_interval_ms || 0,
    canopen_accel_mode:           proto.canopen_accel_mode || '',
    canopen_pdo_config_enabled:   proto.canopen_pdo_config_enabled || false,
    canopen_position_rewind_enabled:      proto.canopen_position_rewind_enabled || false,
    canopen_position_rewind_interval_seconds:  proto.canopen_position_rewind_interval_seconds || 0,
    canopen_position_rewind_threshold_percent: proto.canopen_position_rewind_threshold_percent || 0,

    // Mount location
    latitude:                     proto.latitude || 0,
    longitude:                    proto.longitude || 0,
    altitude:                     proto.altitude || 0,

    // Mount general
    mount_type:                   proto.mount_type || 'EQUATORIAL',
    max_slew_rate:                proto.max_slew_rate || 0,
    max_tracking_rate:            proto.max_tracking_rate || 0,
    slew_acceleration:            proto.slew_acceleration || 0,
    tracking_acceleration:        proto.tracking_acceleration || 0,

    // Mount environmental
    default_temperature:          proto.default_temperature || 0,
    default_pressure:             proto.default_pressure || 0,
    default_humidity:             proto.default_humidity || 0,

    // Mount encoders
    use_encoders:                 proto.use_encoders || false,
    encoders_absolute:            proto.encoders_absolute || false,
    encoder_resolution_config:    proto.encoder_resolution_config || 0,

    // Mount tolerances
    position_tolerance:           proto.position_tolerance || 0,
    rate_tolerance:               proto.rate_tolerance || 0,

    // Meridian flip
    meridian_flip_enabled:        proto.meridian_flip_enabled || false,
    meridian_flip_delay_minutes:  proto.meridian_flip_delay_minutes || 0,
    meridian_flip_hysteresis_degrees:  proto.meridian_flip_hysteresis_degrees || 0,
    meridian_flip_timeout_seconds:     proto.meridian_flip_timeout_seconds || 0,

    // Soft limits
    soft_limits_enabled:               proto.soft_limits_enabled || false,
    soft_limit_axis1_min:              proto.soft_limit_axis1_min || 0,
    soft_limit_axis1_max:              proto.soft_limit_axis1_max || 0,
    soft_limit_axis2_min:              proto.soft_limit_axis2_min || 0,
    soft_limit_axis2_max:              proto.soft_limit_axis2_max || 0,
    soft_limit_warning_degrees:        proto.soft_limit_warning_degrees || 0,
    soft_limit_deceleration_degrees:   proto.soft_limit_deceleration_degrees || 0,
    soft_limit_tracking_rate_factor:   proto.soft_limit_tracking_rate_factor || 0,

    // Park position
    park_position_axis1:          proto.park_position_axis1 || 0,
    park_position_axis2:          proto.park_position_axis2 || 0,

    // Atmospheric correction
    enable_refraction_correction: proto.enable_refraction_correction || false,

    // Equatorial tracking mode / axis inversion
    equatorial_tracking_velocity_mode: proto.equatorial_tracking_velocity_mode || false,
    invert_axis1:                      proto.invert_axis1 || false,
    invert_axis2:                      proto.invert_axis2 || false,

    // Mount orientation (quaternion)
    mount_orientation: proto.mount_orientation ? {
      qx: proto.mount_orientation.qx || 0,
      qy: proto.mount_orientation.qy || 0,
      qz: proto.mount_orientation.qz || 0,
      qw: proto.mount_orientation.qw || 0,
    } : { qx: 0, qy: 0, qz: 0, qw: 1 },

    // Axis physical parameters
    ha_axis_params:  flattenAxisParams(proto.ha_axis_params),
    dec_axis_params: flattenAxisParams(proto.dec_axis_params),

    // Telescope
    focal_length:                 proto.focal_length || 0,
    aperture:                     proto.aperture || 0,
    tube_length:                  proto.tube_length || 0,
    camera_model:                 proto.camera_model || '',
    pixel_size:                   proto.pixel_size || 0,
    sensor_width:                 proto.sensor_width || 0,
    sensor_height:                proto.sensor_height || 0,

    // Guider
    guider_enabled:               proto.enable_guider || false,
    guider_connection_string:     proto.guider_connection_string || '',
    guider_max_correction:        proto.guider_max_correction || 0,
    guider_aggression:            proto.guider_aggression || 0,
    guider_exposure_time_ms:      proto.guider_exposure_time_ms || 0,
    guider_binning:               proto.guider_binning || 0,

    // Kalman filter
    process_noise:                proto.process_noise || 0,
    measurement_noise:            proto.measurement_noise || 0,
    kalman_adaptive_q:            proto.kalman_adaptive_q || false,
    kalman_adaptive_r:            proto.kalman_adaptive_r || false,
    kalman_innovation_threshold:  proto.kalman_innovation_threshold || 0,
    kalman_max_iterations:        proto.kalman_max_iterations || 0,

    // TPOINT
    tpoint_enabled_terms:         proto.tpoint_enabled_terms || 0,
    tpoint_min_measurements:      proto.tpoint_min_measurements || 0,
    tpoint_max_residual:          proto.tpoint_max_residual || 0,
    tpoint_auto_calibrate:        proto.tpoint_auto_calibrate || false,

    // Loop timing
    controller_poll_ms:           proto.controller_poll_ms || 0,
    tracking_update_ms:           proto.tracking_update_ms || 0,

    // Servo init
    servo_init_enabled:           proto.servo_init_enabled || false,
    servo_init_sequence:          proto.servo_init_sequence || '[]',

    // Field rotation (stored in proto, mapped to flat keys)
    field_rotation_enabled:       proto.field_rotation_enabled || false,
    field_rotation_latitude:      proto.field_rotation_latitude || 0,
    field_rotation_altitude:      proto.field_rotation_altitude || 0,
    field_rotation_azimuth:       proto.field_rotation_azimuth || 0,
    field_rotation_computed_rate: proto.field_rotation_computed_rate || 0,
    field_rotation_applied_correction: proto.field_rotation_applied_correction || 0,
    field_rotation_temperature:   proto.field_rotation_temperature || 0,
    field_rotation_flexure_correction: proto.field_rotation_flexure_correction || 0,

    // Derived / computed fields set to defaults (not stored in proto)
    guider_binning:               1,
    canopen_position_counts_per_degree: 0,
    canopen_velocity_counts_per_deg_s: 0,
  };
}

// ─── Config CRUD Routes ──────────────────────────────────────────────────

/**
 * GET /api/config
 * Returns the full controller configuration.
 */
router.get('/', async (req, res) => {
  try {
    const configProto = await grpcCall('GetConfiguration', {});
    res.json(flattenConfig(configProto));
  } catch (err) {
    errorResponse(res, 503, 'Failed to load configuration: ' + err.message, err.details || '');
  }
});

/**
 * POST /api/config
 * Update controller configuration fields.
 * Body: partial configuration object with keys matching the flat config.
 */
router.post('/', async (req, res) => {
  try {
    const updateData = req.body;
    if (!updateData || typeof updateData !== 'object') {
      return errorResponse(res, 400, 'Request body must be a JSON object');
    }
    await grpcCall('UpdateConfiguration', updateData);
    res.json({ success: true });
  } catch (err) {
    errorResponse(res, 502, 'Failed to update configuration', err.message);
  }
});

/**
 * POST /api/config/reset
 * Reset all configuration to factory defaults.
 */
router.post('/reset', async (req, res) => {
  try {
    // Reload the controller with default config via HardRestart
    await grpcCall('HardRestartController', {});
    res.json({ success: true });
  } catch (err) {
    errorResponse(res, 502, 'Failed to reset configuration', err.message);
  }
});

/**
 * POST /api/config/reset-group
 * Reset a specific configuration group to defaults.
 * Body: { group: string }
 */
router.post('/reset-group', async (req, res) => {
  try {
    const groupName = req.body && req.body.group;
    if (!groupName || typeof groupName !== 'string') {
      return errorResponse(res, 400, 'Missing or invalid group name');
    }
    // Use soft restart to reload config from defaults
    await grpcCall('RestartController', {});
    res.json({ success: true, group: groupName });
  } catch (err) {
    errorResponse(res, 502, 'Failed to reset group configuration', err.message);
  }
});

/**
 * GET /api/config/addresses
 * Returns the current gRPC addresses for mount controller and database.
 */
router.get('/addresses', (req, res) => {
  res.json({
    controller: {
      host: proxyConfig.grpc.host,
      port: proxyConfig.grpc.port,
      ssl: proxyConfig.ssl.enabled,
    },
    database: {
      host: proxyConfig.db.host,
      port: proxyConfig.db.port,
    },
  });
});

/**
 * POST /api/config/addresses
 * Update the gRPC addresses and reconnect clients.
 * Body: { controller?: { host, port }, database?: { host, port } }
 */
router.post('/addresses', async (req, res) => {
  try {
    const { controller, database } = req.body;
    const reconnected = [];

    if (controller) {
      const host = controller.host || proxyConfig.grpc.host;
      const port = controller.port || proxyConfig.grpc.port;
      const ssl = controller.ssl !== undefined ? controller.ssl : proxyConfig.ssl.enabled;

      if (typeof host !== 'string' || host.length === 0) {
        return errorResponse(res, 400, 'Invalid controller host');
      }
      if (typeof port !== 'number' || port < 1 || port > 65535) {
        return errorResponse(res, 400, 'Invalid controller port (1-65535)');
      }

      proxyConfig.grpc.host = host;
      proxyConfig.grpc.port = port;
      proxyConfig.ssl.enabled = ssl;
      console.log(`[ssl] Proxy SSL ${ssl ? 'ENABLED' : 'DISABLED'} for gRPC connection to ${host}:${port}`);
      createGrpcClient();
      reconnected.push('controller');
    }

    if (database) {
      const host = database.host || proxyConfig.db.host;
      const port = database.port || proxyConfig.db.port;

      if (typeof host !== 'string' || host.length === 0) {
        return errorResponse(res, 400, 'Invalid database host');
      }
      if (typeof port !== 'number' || port < 1 || port > 65535) {
        return errorResponse(res, 400, 'Invalid database port (1-65535)');
      }

      proxyConfig.db.host = host;
      proxyConfig.db.port = port;
      createDbGrpcClient();
      reconnected.push('database');
    }

    res.json({
      success: true,
      message: `Reconnected: ${reconnected.join(', ')}`,
      addresses: {
        controller: { host: proxyConfig.grpc.host, port: proxyConfig.grpc.port, ssl: proxyConfig.ssl.enabled },
        database: { host: proxyConfig.db.host, port: proxyConfig.db.port },
      },
    });
  } catch (err) {
    errorResponse(res, 500, 'Failed to update addresses', err.message);
  }
});

/**
 * GET /api/config/external-services
 * Returns which external services are enabled.
 * The frontend uses this to show/hide tabs for optional services.
 */
router.get('/external-services', (req, res) => {
  res.json(proxyConfig.external_services || {
    dome: false,
    derotator: false,
    weather: false,
    power: false,
    sequencer: false,
  });
});

module.exports = router;
