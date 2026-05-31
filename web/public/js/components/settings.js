/**
 * Astronomical Mount Controller - Settings Component
 *
 * Renders all configuration parameters in collapsible, editable groups.
 * Supports per-group and global "restore defaults" functionality.
 * All changes are persisted via the API.
 */

const SettingsComponent = (() => {
  'use strict';

  const { $, formatNumber } = Utils;

  // ─── Config Group Definitions ─────────────────────────────────────────────
  // Each group defines: label, fields[], and a path mapping to the API keys.
  // Field types: 'number', 'text', 'select', 'checkbox', 'text-array'

  const CONFIG_GROUPS = [
    {
      id: 'logging',
      label: 'Logging',
      fields: [
        { key: 'log_level', label: 'Log Level', type: 'select', options: ['DEBUG', 'INFO', 'WARN', 'ERROR'] },
        { key: 'log_directory', label: 'Log Directory', type: 'text' },
        { key: 'log_rotation_days', label: 'Log Rotation (days)', type: 'number', min: 1, max: 365 },
        { key: 'log_max_file_size_mb', label: 'Max File Size (MB)', type: 'number', min: 1, max: 1024 },
        { key: 'log_console_output', label: 'Console Output', type: 'checkbox' },
      ],
    },
    {
      id: 'network',
      label: 'Network',
      fields: [
        { key: 'grpc_address', label: 'gRPC Address', type: 'text' },
        { key: 'grpc_port', label: 'gRPC Port', type: 'number', min: 1, max: 65535 },
        { key: 'network_max_connections', label: 'Max Connections', type: 'number', min: 1, max: 1000 },
        { key: 'network_enable_ssl', label: 'Enable SSL', type: 'checkbox' },
        { key: 'network_ssl_cert_path', label: 'SSL Certificate Path', type: 'text' },
        { key: 'network_ssl_key_path', label: 'SSL Key Path', type: 'text' },
      ],
    },
    {
      id: 'canopen',
      label: 'CANopen',
      fields: [
        { key: 'canopen_interface', label: 'Interface', type: 'text' },
        { key: 'canopen_node_id', label: 'Node ID', type: 'number', min: 1, max: 127 },
        { key: 'canopen_baud_rate', label: 'Baud Rate', type: 'select', options: ['100000', '250000', '500000', '1000000'] },
        { key: 'canopen_enable_sync', label: 'Enable SYNC', type: 'checkbox' },
        { key: 'canopen_sync_interval_ms', label: 'SYNC Interval (ms)', type: 'number', min: 10, max: 10000 },
      ],
    },
    {
      id: 'mount_location',
      label: 'Mount Location',
      fields: [
        { key: 'latitude', label: 'Latitude (°)', type: 'number', min: -90, max: 90, step: 0.0001 },
        { key: 'longitude', label: 'Longitude (°)', type: 'number', min: -180, max: 180, step: 0.0001 },
        { key: 'altitude', label: 'Altitude (m)', type: 'number', min: -500, max: 10000 },
        { key: 'mount_height', label: 'Mount Height (m)', type: 'number', min: 0, max: 50, step: 0.1 },
      ],
    },
    {
      id: 'mount_general',
      label: 'Mount General',
      fields: [
        { key: 'mount_type', label: 'Mount Type', type: 'select', options: ['EQUATORIAL', 'ALT_AZ', 'CASUAL', 'UNKNOWN'] },
        { key: 'max_slew_rate', label: 'Max Slew Rate (°/s)', type: 'number', min: 0.1, max: 50, step: 0.1 },
        { key: 'max_tracking_rate', label: 'Max Tracking Rate (°/s)', type: 'number', min: 0.0001, max: 0.1, step: 0.000001 },
        { key: 'slew_acceleration', label: 'Slew Acceleration (°/s²)', type: 'number', min: 0.01, max: 20, step: 0.1 },
        { key: 'tracking_acceleration', label: 'Tracking Acceleration (°/s²)', type: 'number', min: 0.0001, max: 1, step: 0.0001 },
      ],
    },
    {
      id: 'mount_environmental',
      label: 'Mount Environmental',
      fields: [
        { key: 'default_temperature', label: 'Default Temperature (°C)', type: 'number', min: -50, max: 60, step: 0.1 },
        { key: 'default_pressure', label: 'Default Pressure (hPa)', type: 'number', min: 500, max: 1100, step: 0.01 },
        { key: 'default_humidity', label: 'Default Humidity', type: 'number', min: 0, max: 1, step: 0.01 },
      ],
    },
    {
      id: 'mount_encoders',
      label: 'Mount Encoders',
      fields: [
        { key: 'use_encoders', label: 'Enable Encoders', type: 'checkbox' },
        { key: 'encoders_absolute', label: 'Absolute Encoders', type: 'checkbox' },
        { key: 'encoder_resolution_config', label: 'Encoder Resolution (counts/rev)', type: 'number', min: 1, max: 10000000 },
      ],
    },
    {
      id: 'mount_tolerances',
      label: 'Mount Tolerances',
      fields: [
        { key: 'position_tolerance', label: 'Position Tolerance (°)', type: 'number', min: 0.001, max: 10, step: 0.01 },
        { key: 'rate_tolerance', label: 'Rate Tolerance (°/s)', type: 'number', min: 0.0001, max: 1, step: 0.001 },
      ],
    },
    {
      id: 'mount_meridian_flip',
      label: 'Meridian Flip',
      fields: [
        { key: 'meridian_flip_enabled', label: 'Enable Auto Flip', type: 'checkbox' },
        { key: 'meridian_flip_delay_minutes', label: 'Delay After Meridian (min)', type: 'number', min: 0, max: 60, step: 0.5 },
        { key: 'meridian_flip_hysteresis_degrees', label: 'Hysteresis (°)', type: 'number', min: 0, max: 10, step: 0.1 },
        { key: 'meridian_flip_timeout_seconds', label: 'Flip Timeout (s)', type: 'number', min: 10, max: 600, step: 5 },
      ],
    },
    {
      id: 'mount_soft_limits',
      label: 'Soft Limits',
      fields: [
        { key: 'soft_limits_enabled', label: 'Enable Soft Limits', type: 'checkbox' },
        { key: 'soft_limit_axis1_min', label: 'Axis 1 Min (°)', type: 'number', min: -360, max: 360, step: 0.1 },
        { key: 'soft_limit_axis1_max', label: 'Axis 1 Max (°)', type: 'number', min: -360, max: 360, step: 0.1 },
        { key: 'soft_limit_axis2_min', label: 'Axis 2 Min (°)', type: 'number', min: -360, max: 360, step: 0.1 },
        { key: 'soft_limit_axis2_max', label: 'Axis 2 Max (°)', type: 'number', min: -360, max: 360, step: 0.1 },
        { key: 'soft_limit_warning_degrees', label: 'Warning Zone (°)', type: 'number', min: 0, max: 90, step: 0.1 },
        { key: 'soft_limit_deceleration_degrees', label: 'Deceleration Zone (°)', type: 'number', min: 0, max: 90, step: 0.1 },
        { key: 'soft_limit_tracking_rate_factor', label: 'Min Rate Factor', type: 'number', min: 0, max: 1, step: 0.01 },
      ],
    },
    {
      id: 'mount_park',
      label: 'Park Position',
      fields: [
        { key: 'park_position_axis1', label: 'Axis 1 Park Position (°)', type: 'number', min: -360, max: 360, step: 0.1 },
        { key: 'park_position_axis2', label: 'Axis 2 Park Position (°)', type: 'number', min: -360, max: 360, step: 0.1 },
      ],
    },
    {
      id: 'mount_atmosphere',
      label: 'Atmospheric Correction',
      fields: [
        { key: 'enable_refraction_correction', label: 'Enable Refraction Correction', type: 'checkbox' },
      ],
    },
    {
      id: 'mount_orientation',
      label: 'Mount Orientation (Quaternion)',
      fields: [
        { key: 'mount_orientation', label: 'Orientation (qx, qy, qz, qw)', type: 'quaternion', sub_fields: ['qx', 'qy', 'qz', 'qw'] },
      ],
    },
    {
      id: 'ha_axis_params',
      label: 'HA Axis Physical Parameters',
      fields: [
        { key: 'ha_axis_params', label: '', type: 'nested_group', nested_label: 'HA Axis' },
      ],
      sub_groups: [
        {
          id: 'ha_motor',
          label: 'Motor',
          fields: [
            { key: 'motor_steps_per_rev', label: 'Motor Steps/Rev', type: 'number', min: 1, max: 10000 },
            { key: 'motor_microstepping', label: 'Microstepping', type: 'number', min: 1, max: 256 },
            { key: 'motor_step_angle', label: 'Step Angle (arcsec)', type: 'number', min: 0.1, max: 360, step: 0.01 },
          ],
        },
        {
          id: 'ha_encoder',
          label: 'Encoder',
          fields: [
            { key: 'encoder_resolution', label: 'Resolution (counts/rev)', type: 'number', min: 1, max: 10000000 },
            { key: 'encoder_counts_per_arcsec', label: 'Counts/arcsec', type: 'number', min: 0.0001, max: 100, step: 0.0001 },
            { key: 'encoder_quantization_error', label: 'Quantization Error (arcsec)', type: 'number', min: 0, max: 1000, step: 0.1 },
          ],
        },
        {
          id: 'ha_gear',
          label: 'Gear',
          fields: [
            { key: 'gear_ratio', label: 'Gear Ratio', type: 'number', min: 1, max: 10000, step: 0.1 },
            { key: 'worm_ratio', label: 'Worm Ratio', type: 'number', min: 1, max: 10000, step: 0.1 },
            { key: 'worm_teeth', label: 'Worm Teeth', type: 'number', min: 1, max: 1000 },
            { key: 'worm_wheel_teeth', label: 'Worm Wheel Teeth', type: 'number', min: 1, max: 10000 },
          ],
        },
        {
          id: 'ha_cyclic_error',
          label: 'Cyclic Error',
          fields: [
            { key: 'cyclic_error_amplitude', label: 'Amplitude (arcsec)', type: 'number', min: 0, max: 100, step: 0.1 },
            { key: 'cyclic_error_period', label: 'Period (°)', type: 'number', min: 0.1, max: 3600, step: 0.1 },
            { key: 'cyclic_harmonics', label: 'Harmonics (comma-separated)', type: 'text' },
          ],
        },
        {
          id: 'ha_backlash',
          label: 'Backlash',
          fields: [
            { key: 'backlash', label: 'Backlash (arcsec)', type: 'number', min: 0, max: 1000, step: 0.1 },
            { key: 'backlash_temp_coeff', label: 'Temp Coefficient (arcsec/°C)', type: 'number', min: 0, max: 10, step: 0.001 },
          ],
        },
        {
          id: 'ha_stiffness',
          label: 'Stiffness & Thermal',
          fields: [
            { key: 'axis_stiffness', label: 'Axis Stiffness (arcsec/Nm)', type: 'number', min: 0, max: 100, step: 0.01 },
            { key: 'torsional_compliance', label: 'Torsional Compliance (rad/Nm)', type: 'number', min: 0, step: 1e-7 },
            { key: 'expansion_coeff', label: 'Expansion Coeff (1/°C)', type: 'number', min: 0, step: 1e-7 },
            { key: 'temp_gear_error_coeff', label: 'Gear Error Temp Coeff (arcsec/°C)', type: 'number', min: 0, max: 10, step: 0.001 },
            { key: 'calibration_temp', label: 'Calibration Temp (°C)', type: 'number', min: -50, max: 60, step: 0.1 },
          ],
        },
      ],
    },
    {
      id: 'dec_axis_params',
      label: 'Dec Axis Physical Parameters',
      fields: [
        { key: 'dec_axis_params', label: '', type: 'nested_group', nested_label: 'Dec Axis' },
      ],
      sub_groups: [
        {
          id: 'dec_motor',
          label: 'Motor',
          fields: [
            { key: 'motor_steps_per_rev', label: 'Motor Steps/Rev', type: 'number', min: 1, max: 10000 },
            { key: 'motor_microstepping', label: 'Microstepping', type: 'number', min: 1, max: 256 },
            { key: 'motor_step_angle', label: 'Step Angle (arcsec)', type: 'number', min: 0.1, max: 360, step: 0.01 },
          ],
        },
        {
          id: 'dec_encoder',
          label: 'Encoder',
          fields: [
            { key: 'encoder_resolution', label: 'Resolution (counts/rev)', type: 'number', min: 1, max: 10000000 },
            { key: 'encoder_counts_per_arcsec', label: 'Counts/arcsec', type: 'number', min: 0.0001, max: 100, step: 0.0001 },
            { key: 'encoder_quantization_error', label: 'Quantization Error (arcsec)', type: 'number', min: 0, max: 1000, step: 0.1 },
          ],
        },
        {
          id: 'dec_gear',
          label: 'Gear',
          fields: [
            { key: 'gear_ratio', label: 'Gear Ratio', type: 'number', min: 1, max: 10000, step: 0.1 },
            { key: 'worm_ratio', label: 'Worm Ratio', type: 'number', min: 1, max: 10000, step: 0.1 },
            { key: 'worm_teeth', label: 'Worm Teeth', type: 'number', min: 1, max: 1000 },
            { key: 'worm_wheel_teeth', label: 'Worm Wheel Teeth', type: 'number', min: 1, max: 10000 },
          ],
        },
        {
          id: 'dec_cyclic_error',
          label: 'Cyclic Error',
          fields: [
            { key: 'cyclic_error_amplitude', label: 'Amplitude (arcsec)', type: 'number', min: 0, max: 100, step: 0.1 },
            { key: 'cyclic_error_period', label: 'Period (°)', type: 'number', min: 0.1, max: 3600, step: 0.1 },
            { key: 'cyclic_harmonics', label: 'Harmonics (comma-separated)', type: 'text' },
          ],
        },
        {
          id: 'dec_backlash',
          label: 'Backlash',
          fields: [
            { key: 'backlash', label: 'Backlash (arcsec)', type: 'number', min: 0, max: 1000, step: 0.1 },
            { key: 'backlash_temp_coeff', label: 'Temp Coefficient (arcsec/°C)', type: 'number', min: 0, max: 10, step: 0.001 },
          ],
        },
        {
          id: 'dec_stiffness',
          label: 'Stiffness & Thermal',
          fields: [
            { key: 'axis_stiffness', label: 'Axis Stiffness (arcsec/Nm)', type: 'number', min: 0, max: 100, step: 0.01 },
            { key: 'torsional_compliance', label: 'Torsional Compliance (rad/Nm)', type: 'number', min: 0, step: 1e-7 },
            { key: 'expansion_coeff', label: 'Expansion Coeff (1/°C)', type: 'number', min: 0, step: 1e-7 },
            { key: 'temp_gear_error_coeff', label: 'Gear Error Temp Coeff (arcsec/°C)', type: 'number', min: 0, max: 10, step: 0.001 },
            { key: 'calibration_temp', label: 'Calibration Temp (°C)', type: 'number', min: -50, max: 60, step: 0.1 },
          ],
        },
      ],
    },
    {
      id: 'telescope',
      label: 'Telescope',
      fields: [
        { key: 'focal_length', label: 'Focal Length (mm)', type: 'number', min: 1, max: 50000, step: 0.1 },
        { key: 'aperture', label: 'Aperture (mm)', type: 'number', min: 1, max: 50000, step: 0.1 },
        { key: 'tube_length', label: 'Tube Length (mm)', type: 'number', min: 1, max: 50000, step: 0.1 },
        { key: 'camera_model', label: 'Camera Model', type: 'text' },
        { key: 'pixel_size', label: 'Pixel Size (µm)', type: 'number', min: 0.1, max: 100, step: 0.01 },
        { key: 'sensor_width', label: 'Sensor Width (px)', type: 'number', min: 1, max: 50000 },
        { key: 'sensor_height', label: 'Sensor Height (px)', type: 'number', min: 1, max: 50000 },
      ],
    },
    {
      id: 'guider',
      label: 'Guider',
      fields: [
        { key: 'guider_enabled', label: 'Enable Guider', type: 'checkbox' },
        { key: 'guider_connection_string', label: 'Connection String', type: 'text' },
        { key: 'guider_max_correction', label: 'Max Correction (arcsec)', type: 'number', min: 0.1, max: 100, step: 0.1 },
        { key: 'guider_aggression', label: 'Aggression', type: 'number', min: 0, max: 1, step: 0.01 },
        { key: 'guider_exposure_time_ms', label: 'Exposure Time (ms)', type: 'number', min: 10, max: 60000 },
        { key: 'guider_binning', label: 'Binning', type: 'select', options: ['1', '2', '3', '4'] },
      ],
    },
    {
      id: 'kalman',
      label: 'Kalman Filter',
      fields: [
        { key: 'process_noise', label: 'Process Noise', type: 'number', min: 0.0001, max: 100, step: 0.001 },
        { key: 'measurement_noise', label: 'Measurement Noise', type: 'number', min: 0.0001, max: 100, step: 0.001 },
        { key: 'kalman_adaptive_q', label: 'Adaptive Q', type: 'checkbox' },
        { key: 'kalman_adaptive_r', label: 'Adaptive R', type: 'checkbox' },
        { key: 'kalman_innovation_threshold', label: 'Innovation Threshold', type: 'number', min: 0.1, max: 100, step: 0.1 },
        { key: 'kalman_max_iterations', label: 'Max Iterations', type: 'number', min: 1, max: 1000 },
      ],
    },
    {
      id: 'tpoint',
      label: 'TPOINT Calibration',
      fields: [
        { key: 'tpoint_enabled_terms', label: 'Enabled Terms (bitmask)', type: 'number', min: 0, max: 65535 },
        { key: 'tpoint_min_measurements', label: 'Min Measurements', type: 'number', min: 1, max: 1000 },
        { key: 'tpoint_max_residual', label: 'Max Residual (arcsec)', type: 'number', min: 0.1, max: 1000, step: 0.1 },
        { key: 'tpoint_auto_calibrate', label: 'Auto Calibrate', type: 'checkbox' },
      ],
    },
    {
      id: 'derotator',
      label: 'Derotator',
      fields: [
        { key: 'derotator_type', label: 'Type', type: 'select', options: ['CANOPEN', 'STEPPER', 'SERVO', 'CUSTOM'] },
        { key: 'derotator_enabled', label: 'Enabled', type: 'checkbox' },
        { key: 'derotator_connection_string', label: 'Connection String', type: 'text' },
        { key: 'derotator_gear_ratio', label: 'Gear Ratio', type: 'number', min: 0.1, max: 10000, step: 0.1 },
        { key: 'derotator_max_speed', label: 'Max Speed (°/s)', type: 'number', min: 0.1, max: 180, step: 0.1 },
        { key: 'derotator_max_acceleration', label: 'Max Acceleration (°/s²)', type: 'number', min: 0.1, max: 180, step: 0.1 },
        { key: 'derotator_backlash', label: 'Backlash (arcsec)', type: 'number', min: 0, max: 1000, step: 0.1 },
        { key: 'derotator_absolute_encoder', label: 'Absolute Encoder', type: 'checkbox' },
        { key: 'derotator_encoder_resolution', label: 'Encoder Resolution', type: 'number', min: 1, max: 10000000 },
        { key: 'derotator_homing_offset', label: 'Homing Offset (°)', type: 'number', min: -360, max: 360, step: 0.1 },
      ],
    },
    {
      id: 'field_rotation',
      label: 'Field Rotation',
      fields: [
        { key: 'field_rotation_enabled', label: 'Enabled', type: 'checkbox' },
        { key: 'field_rotation_latitude', label: 'Latitude (°)', type: 'number', min: -90, max: 90, step: 0.0001 },
        { key: 'field_rotation_altitude', label: 'Altitude (°)', type: 'number', min: -90, max: 90, step: 0.1 },
        { key: 'field_rotation_azimuth', label: 'Azimuth (°)', type: 'number', min: 0, max: 360, step: 0.1 },
        { key: 'field_rotation_computed_rate', label: 'Computed Rate (°/s)', type: 'number', min: -10, max: 10, step: 0.000001 },
        { key: 'field_rotation_applied_correction', label: 'Applied Correction (°)', type: 'number', min: -360, max: 360, step: 0.001 },
        { key: 'field_rotation_temperature', label: 'Temperature (°C)', type: 'number', min: -50, max: 60, step: 0.1 },
        { key: 'field_rotation_flexure_correction', label: 'Flexure Correction', type: 'number', min: -10, max: 10, step: 0.001 },
      ],
    },
    {
      id: 'hal',
      label: 'HAL (Hardware Abstraction Layer)',
      fields: [
        { key: 'hal_interface_type', label: 'Interface Type', type: 'select', options: ['CANopen', 'Serial', 'Ethernet', 'Simulated', 'Custom'] },
        { key: 'hal_can_interface', label: 'CAN Interface', type: 'text' },
        { key: 'hal_can_node_id', label: 'CAN Node ID', type: 'number', min: 1, max: 127 },
        { key: 'hal_can_baud_rate', label: 'CAN Baud Rate', type: 'number', min: 10000, max: 10000000 },
        { key: 'hal_heartbeat_interval_ms', label: 'Heartbeat Interval (ms)', type: 'number', min: 10, max: 60000 },
        { key: 'hal_pdo_mapping_mode', label: 'PDO Mapping Mode', type: 'text' },
      ],
    },
    {
      id: 'hal_gamepad',
      label: 'HAL - Gamepad',
      fields: [
        { key: 'hal_gamepad_device_path', label: 'Device Path', type: 'text' },
        { key: 'hal_gamepad_deadzone', label: 'Deadzone', type: 'number', min: 0, max: 1, step: 0.01 },
        { key: 'hal_gamepad_sensitivity', label: 'Sensitivity', type: 'number', min: 0.1, max: 10, step: 0.1 },
        { key: 'hal_gamepad_poll_interval_ms', label: 'Poll Interval (ms)', type: 'number', min: 5, max: 1000 },
      ],
    },
  ];

  // ─── Config Group Icons ───────────────────────────────────────────────────
  // Simple SVG icons for each config group category.

  const GROUP_ICONS = {
    logging:          '<svg viewBox="0 0 24 24" width="16" height="16" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M14 2H6a2 2 0 0 0-2 2v16a2 2 0 0 0 2 2h12a2 2 0 0 0 2-2V8z"/><polyline points="14 2 14 8 20 8"/><line x1="16" y1="13" x2="8" y2="13"/><line x1="16" y1="17" x2="8" y2="17"/><polyline points="10 9 9 9 8 9"/></svg>',
    network:          '<svg viewBox="0 0 24 24" width="16" height="16" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M22 12h-2.48a8 8 0 0 1-15.04 0H2"/><path d="M22 12a10 10 0 0 0-20 0"/><line x1="12" y1="2" x2="12" y2="22"/><path d="M8 12a4 4 0 0 0 8 0"/></svg>',
    canopen:          '<svg viewBox="0 0 24 24" width="16" height="16" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M4 4h16v16H4z"/><line x1="8" y1="8" x2="8" y2="16"/><line x1="12" y1="8" x2="12" y2="16"/><line x1="16" y1="8" x2="16" y2="16"/></svg>',
    mount_location:   '<svg viewBox="0 0 24 24" width="16" height="16" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M21 10c0 7-9 13-9 13s-9-6-9-13a9 9 0 0 1 18 0z"/><circle cx="12" cy="10" r="3"/></svg>',
    mount_general:    '<svg viewBox="0 0 24 24" width="16" height="16" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><circle cx="12" cy="12" r="3"/><path d="M19.4 15a1.65 1.65 0 0 0 .33 1.82l.06.06a2 2 0 0 1-2.83 2.83l-.06-.06a1.65 1.65 0 0 0-1.82-.33 1.65 1.65 0 0 0-1 1.51V21a2 2 0 0 1-4 0v-.09A1.65 1.65 0 0 0 9 19.4a1.65 1.65 0 0 0-1.82.33l-.06.06a2 2 0 0 1-2.83-2.83l.06-.06A1.65 1.65 0 0 0 4.68 15a1.65 1.65 0 0 0-1.51-1H3a2 2 0 0 1 0-4h.09A1.65 1.65 0 0 0 4.6 9a1.65 1.65 0 0 0-.33-1.82l-.06-.06a2 2 0 0 1 2.83-2.83l.06.06A1.65 1.65 0 0 0 9 4.68a1.65 1.65 0 0 0 1-1.51V3a2 2 0 0 1 4 0v.09a1.65 1.65 0 0 0 1 1.51 1.65 1.65 0 0 0 1.82-.33l.06-.06a2 2 0 0 1 2.83 2.83l-.06.06A1.65 1.65 0 0 0 19.4 9a1.65 1.65 0 0 0 1.51 1H21a2 2 0 0 1 0 4h-.09a1.65 1.65 0 0 0-1.51 1z"/></svg>',
    mount_environmental: '<svg viewBox="0 0 24 24" width="16" height="16" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M12 2.69l5.66 5.66a8 8 0 1 1-11.31 0z"/><line x1="12" y1="9" x2="12" y2="13"/><line x1="12" y1="17" x2="12.01" y2="17"/></svg>',
    mount_encoders:   '<svg viewBox="0 0 24 24" width="16" height="16" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><circle cx="12" cy="12" r="10"/><path d="M12 2a15 15 0 0 1 0 20 15 15 0 0 1 0-20z"/><line x1="12" y1="12" x2="17" y2="7"/><line x1="12" y1="12" x2="8" y2="16"/></svg>',
    mount_tolerances: '<svg viewBox="0 0 24 24" width="16" height="16" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><circle cx="12" cy="12" r="10"/><circle cx="12" cy="12" r="6"/><circle cx="12" cy="12" r="2"/></svg>',
    mount_meridian_flip: '<svg viewBox="0 0 24 24" width="16" height="16" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><polyline points="1 4 1 10 7 10"/><polyline points="23 20 23 14 17 14"/><path d="M20.49 9A9 9 0 0 0 5.64 5.64L1 10m22 4l-4.64 4.36A9 9 0 0 1 3.51 15"/></svg>',
    mount_soft_limits:'<svg viewBox="0 0 24 24" width="16" height="16" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M12 22s8-4 8-10V5l-8-3-8 3v7c0 6 8 10 8 10z"/></svg>',
    mount_park:       '<svg viewBox="0 0 24 24" width="16" height="16" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M3 9l9-7 9 7v11a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2z"/><polyline points="9 22 9 12 15 12 15 22"/></svg>',
    mount_atmosphere: '<svg viewBox="0 0 24 24" width="16" height="16" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M18 10h-1.26A8 8 0 1 0 9 20h9a5 5 0 0 0 0-10z"/></svg>',
    mount_orientation:'<svg viewBox="0 0 24 24" width="16" height="16" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><circle cx="12" cy="12" r="10"/><polygon points="16.24 7.76 14.12 14.12 7.76 16.24 9.88 9.88 16.24 7.76"/></svg>',
    ha_axis_params:   '<svg viewBox="0 0 24 24" width="16" height="16" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><circle cx="12" cy="5" r="2.5"/><circle cx="5" cy="19" r="2.5"/><circle cx="19" cy="19" r="2.5"/><line x1="12" y1="7.5" x2="5" y2="16.5"/><line x1="12" y1="7.5" x2="19" y2="16.5"/><line x1="5" y1="19" x2="19" y2="19"/></svg>',
    dec_axis_params:  '<svg viewBox="0 0 24 24" width="16" height="16" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><circle cx="12" cy="5" r="2.5"/><circle cx="5" cy="19" r="2.5"/><circle cx="19" cy="19" r="2.5"/><line x1="12" y1="7.5" x2="5" y2="16.5"/><line x1="12" y1="7.5" x2="19" y2="16.5"/><line x1="5" y1="19" x2="19" y2="19"/></svg>',
    telescope:        '<svg viewBox="0 0 24 24" width="16" height="16" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M12 3l4 7-4 7-4-7z"/><line x1="12" y1="3" x2="12" y2="17"/><line x1="8" y1="17" x2="16" y2="17"/><line x1="10" y1="20" x2="14" y2="20"/><line x1="12" y1="17" x2="12" y2="20"/></svg>',
    guider:           '<svg viewBox="0 0 24 24" width="16" height="16" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><circle cx="12" cy="12" r="10"/><line x1="12" y1="2" x2="12" y2="6"/><line x1="12" y1="18" x2="12" y2="22"/><line x1="2" y1="12" x2="6" y2="12"/><line x1="18" y1="12" x2="22" y2="12"/><circle cx="12" cy="12" r="3"/></svg>',
    kalman:           '<svg viewBox="0 0 24 24" width="16" height="16" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><polyline points="22 12 18 12 15 21 9 3 6 12 2 12"/></svg>',
    tpoint:           '<svg viewBox="0 0 24 24" width="16" height="16" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><circle cx="12" cy="12" r="10"/><circle cx="12" cy="12" r="1"/><line x1="12" y1="2" x2="12" y2="12"/><line x1="12" y1="12" x2="16" y2="16"/></svg>',
    derotator:        '<svg viewBox="0 0 24 24" width="16" height="16" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><polyline points="23 4 23 10 17 10"/><polyline points="1 20 1 14 7 14"/><path d="M3.51 9a9 9 0 0 1 14.85-3.36L23 10M1 14l4.64 4.36A9 9 0 0 0 20.49 15"/></svg>',
    field_rotation:   '<svg viewBox="0 0 24 24" width="16" height="16" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M21.5 2v6h-6M2.5 22v-6h6M2 11.5a10 10 0 0 1 18.8-4.3M22 12.5a10 10 0 0 1-18.8 4.2"/></svg>',
    hal:              '<svg viewBox="0 0 24 24" width="16" height="16" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><rect x="4" y="4" width="16" height="16" rx="2" ry="2"/><rect x="9" y="9" width="6" height="6"/><line x1="9" y1="4" x2="9" y2="9"/><line x1="15" y1="4" x2="15" y2="9"/><line x1="9" y1="15" x2="9" y2="20"/><line x1="15" y1="15" x2="15" y2="20"/><line x1="4" y1="9" x2="9" y2="9"/><line x1="15" y1="9" x2="20" y2="9"/></svg>',
    hal_gamepad:      '<svg viewBox="0 0 24 24" width="16" height="16" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><line x1="6" y1="12" x2="10" y2="12"/><line x1="8" y1="10" x2="8" y2="14"/><line x1="15" y1="13" x2="15.01" y2="13"/><line x1="18" y1="11" x2="18.01" y2="11"/><rect x="2" y="6" width="20" height="12" rx="2"/></svg>',
    // Sub-group icons (axis physical parameters)
    ha_motor:         '<svg viewBox="0 0 24 24" width="14" height="14" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><circle cx="12" cy="12" r="3"/><path d="M12 9V4l-3 3m3-3l3 3"/><path d="M12 15v5l-3-3m3 3l3-3"/></svg>',
    ha_encoder:       '<svg viewBox="0 0 24 24" width="14" height="14" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><circle cx="12" cy="12" r="10"/><path d="M12 2a15 15 0 0 1 0 20 15 15 0 0 1 0-20z"/><line x1="12" y1="12" x2="17" y2="7"/><line x1="12" y1="12" x2="8" y2="16"/></svg>',
    ha_gear:          '<svg viewBox="0 0 24 24" width="14" height="14" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><circle cx="12" cy="12" r="4"/><path d="M12 2v4m0 12v4M2 12h4m12 0h4"/><circle cx="12" cy="12" r="2"/></svg>',
    ha_cyclic_error:  '<svg viewBox="0 0 24 24" width="14" height="14" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><polyline points="22 12 18 12 15 21 9 3 6 12 2 12"/><circle cx="12" cy="12" r="2"/></svg>',
    ha_backlash:      '<svg viewBox="0 0 24 24" width="14" height="14" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><line x1="5" y1="12" x2="19" y2="12"/><polyline points="12 5 19 12 12 19"/></svg>',
    ha_stiffness:     '<svg viewBox="0 0 24 24" width="14" height="14" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M12 2v20M2 12h20"/><path d="M4 4l16 16M20 4L4 20"/></svg>',
    dec_motor:        '<svg viewBox="0 0 24 24" width="14" height="14" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><circle cx="12" cy="12" r="3"/><path d="M12 9V4l-3 3m3-3l3 3"/><path d="M12 15v5l-3-3m3 3l3-3"/></svg>',
    dec_encoder:      '<svg viewBox="0 0 24 24" width="14" height="14" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><circle cx="12" cy="12" r="10"/><path d="M12 2a15 15 0 0 1 0 20 15 15 0 0 1 0-20z"/><line x1="12" y1="12" x2="17" y2="7"/><line x1="12" y1="12" x2="8" y2="16"/></svg>',
    dec_gear:         '<svg viewBox="0 0 24 24" width="14" height="14" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><circle cx="12" cy="12" r="4"/><path d="M12 2v4m0 12v4M2 12h4m12 0h4"/><circle cx="12" cy="12" r="2"/></svg>',
    dec_cyclic_error: '<svg viewBox="0 0 24 24" width="14" height="14" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><polyline points="22 12 18 12 15 21 9 3 6 12 2 12"/><circle cx="12" cy="12" r="2"/></svg>',
    dec_backlash:     '<svg viewBox="0 0 24 24" width="14" height="14" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><line x1="5" y1="12" x2="19" y2="12"/><polyline points="12 5 19 12 12 19"/></svg>',
    dec_stiffness:    '<svg viewBox="0 0 24 24" width="14" height="14" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M12 2v20M2 12h20"/><path d="M4 4l16 16M20 4L4 20"/></svg>',
  };

  // ─── Internal State ───────────────────────────────────────────────────────

  let currentConfig = null;
  let isDirty = {};

  // ─── Public API ───────────────────────────────────────────────────────────

  /**
   * Load and render the configuration data.
   * Called by the main app when the settings tab becomes active.
   */
  async function loadConfig() {
    const configEl = $('#config-content');
    if (!configEl) return;

    configEl.innerHTML = '<div class="status-placeholder">Loading configuration...</div>';

    try {
      const configData = await Api.getConfig();
      currentConfig = configData;
      renderConfig(configData, configEl);

      // Show the Reset All button
      const resetAllBtn = $('#btn-reset-all-config');
      if (resetAllBtn) resetAllBtn.style.display = 'inline-flex';
    } catch (err) {
      configEl.innerHTML = `
        <div class="status-placeholder" style="color: var(--color-danger);">
          Failed to load configuration: ${escapeHtml(err.message)}
        </div>
      `;
    }
  }

  // ─── Rendering ────────────────────────────────────────────────────────────

  /**
   * Render all configuration groups into the container.
   */
  function renderConfig(config, container) {
    container.innerHTML = '';

    CONFIG_GROUPS.forEach(group => {
      const groupEl = createGroupElement(group, config);
      container.appendChild(groupEl);
    });
  }

  /**
   * Create a collapsible <details> element for a config group.
   */
  function createGroupElement(group, config) {
    const details = document.createElement('details');
    details.className = 'form-section config-group';
    details.id = `cfg-group-${group.id}`;

    const summary = document.createElement('summary');
    const icon = GROUP_ICONS[group.id] || '';
    summary.innerHTML = `<span class="disclosure-arrow">&#x25B6;</span><span class="config-summary-content">${icon}<span>${group.label}</span></span>`;

    // Update arrow direction when group toggles
    details.addEventListener('toggle', () => {
      const arrow = summary.querySelector('.disclosure-arrow');
      if (arrow) {
        arrow.classList.toggle('open', details.open);
      }
    });

    details.appendChild(summary);

    const body = document.createElement('div');
    body.className = 'form-section-body config-group-body';

    // If this group has sub_groups (like axis params), render those instead
    if (group.sub_groups && group.sub_groups.length > 0) {
      const parentKey = group.fields.find(f => f.type === 'nested_group');
      const parentData = parentKey ? config[parentKey.key] : config;

      group.sub_groups.forEach(sub => {
        const subDetails = document.createElement('details');
        subDetails.className = 'form-section config-sub-group';
        subDetails.id = `cfg-sub-${group.id}-${sub.id}`;

        const subSummary = document.createElement('summary');
        const subIcon = GROUP_ICONS[sub.id] || '';
        subSummary.innerHTML = `<span class="disclosure-arrow">&#x25B6;</span><span class="config-summary-content">${subIcon}<span>${sub.label}</span></span>`;

        // Update arrow direction when sub-group toggles
        subDetails.addEventListener('toggle', () => {
          const arrow = subSummary.querySelector('.disclosure-arrow');
          if (arrow) {
            arrow.classList.toggle('open', subDetails.open);
          }
        });

        subDetails.appendChild(subSummary);

        const subBody = document.createElement('div');
        subBody.className = 'form-section-body config-group-body';

        sub.fields.forEach(field => {
          if (field.type === 'nested_group') return;
          const value = parentData ? getNestedValue(parentData, field.key) : undefined;
          const fieldEl = createFieldElement(field, value, `${group.id}.${sub.id}`);
          subBody.appendChild(fieldEl);
        });

        subDetails.appendChild(subBody);

        // Sub-group action buttons
        const subActions = document.createElement('div');
        subActions.className = 'config-group-actions';
        subActions.innerHTML = `
          <button class="btn btn-primary btn-sm btn-save-group" data-group="${group.id}" data-sub="${sub.id}">Save</button>
          <button class="btn btn-sm btn-reset-group" data-group="${group.id}" data-sub="${sub.id}">Restore Defaults</button>
          <span class="config-group-status" id="cfg-status-${group.id}-${sub.id}"></span>
        `;
        subBody.appendChild(subActions);

        details.appendChild(subDetails);
      });
    } else {
      // Render regular fields
      group.fields.forEach(field => {
        if (field.type === 'nested_group') return;
        let value;
        if (field.type === 'quaternion' && field.sub_fields) {
          // For quaternion, show a text input with comma-separated values
          const qData = config[field.key];
          if (qData && typeof qData === 'object') {
            value = field.sub_fields.map(sf => qData[sf] !== undefined ? qData[sf] : '0').join(', ');
          } else {
            value = '1.0, 0.0, 0.0, 0.0';
          }
        } else {
          value = config[field.key] !== undefined ? config[field.key] : undefined;
        }
        const fieldEl = createFieldElement(field, value, group.id);
        body.appendChild(fieldEl);
      });
    }

    details.appendChild(body);

    // Group action buttons (for non-sub-group groups)
    if (!group.sub_groups || group.sub_groups.length === 0) {
      const actions = document.createElement('div');
      actions.className = 'config-group-actions';
      actions.innerHTML = `
        <button class="btn btn-primary btn-sm btn-save-group" data-group="${group.id}">Save</button>
        <button class="btn btn-sm btn-reset-group" data-group="${group.id}">Restore Defaults</button>
        <span class="config-group-status" id="cfg-status-${group.id}"></span>
      `;
      body.appendChild(actions);
    }

    return details;
  }

  /**
   * Create a form field element based on its type.
   */
  function createFieldElement(field, value, groupId) {
    const wrapper = document.createElement('div');
    wrapper.className = 'config-field';

    // Quaternion type
    if (field.type === 'quaternion') {
      wrapper.innerHTML = `
        <label class="config-field-label">${field.label}</label>
        <input type="text" class="form-input config-input" data-group="${groupId}" data-key="${field.key}" data-type="quaternion"
          value="${escapeHtml(String(value || ''))}" />
      `;
      return wrapper;
    }

    // Checkbox
    if (field.type === 'checkbox') {
      wrapper.className = 'config-field config-field-checkbox';
      wrapper.innerHTML = `
        <label class="checkbox-label config-checkbox-label">
          <input type="checkbox" class="config-input" data-group="${groupId}" data-key="${field.key}" data-type="checkbox"
            ${value ? 'checked' : ''} />
          ${field.label}
        </label>
      `;
      return wrapper;
    }

    // Select
    if (field.type === 'select') {
      const options = (field.options || []).map(opt => `
        <option value="${escapeHtml(opt)}" ${String(value) === String(opt) ? 'selected' : ''}>${escapeHtml(opt)}</option>
      `).join('');
      wrapper.innerHTML = `
        <label class="config-field-label">${field.label}</label>
        <select class="form-input form-select config-input" data-group="${groupId}" data-key="${field.key}" data-type="select">
          ${options}
        </select>
      `;
      return wrapper;
    }

    // Number
    if (field.type === 'number') {
      const min = field.min !== undefined ? `min="${field.min}"` : '';
      const max = field.max !== undefined ? `max="${field.max}"` : '';
      const step = field.step !== undefined ? `step="${field.step}"` : 'step="any"';
      wrapper.innerHTML = `
        <label class="config-field-label">${field.label}</label>
        <input type="number" class="form-input config-input" data-group="${groupId}" data-key="${field.key}" data-type="number"
          ${min} ${max} ${step} value="${value !== undefined ? escapeHtml(String(value)) : ''}" />
      `;
      return wrapper;
    }

    // Text (default)
    wrapper.innerHTML = `
      <label class="config-field-label">${field.label}</label>
      <input type="text" class="form-input config-input" data-group="${groupId}" data-key="${field.key}" data-type="text"
        value="${escapeHtml(String(value || ''))}" />
    `;
    return wrapper;
  }

  // ─── Data Helpers ─────────────────────────────────────────────────────────

  /**
   * Get a nested value from an object by dot-separated path.
   */
  function getNestedValue(obj, path) {
    if (!obj || !path) return undefined;
    const keys = path.split('.');
    let current = obj;
    for (const key of keys) {
      if (current === null || current === undefined) return undefined;
      current = current[key];
    }
    return current;
  }

  /**
   * Collect all field values from a config group into an object.
   */
  function collectGroupData(groupId) {
    const group = CONFIG_GROUPS.find(g => g.id === groupId);
    if (!group) return {};

    const data = {};

    // Collect top-level fields
    const inputs = document.querySelectorAll(`#cfg-group-${groupId} .config-input`);
    inputs.forEach(input => {
      const key = input.dataset.key;
      const type = input.dataset.type;
      const fieldGroup = input.dataset.group || groupId;

      if (!key) return;

      let value;
      if (type === 'checkbox') {
        value = input.checked;
      } else if (type === 'number') {
        value = parseFloat(input.value);
        if (isNaN(value)) value = 0;
      } else if (type === 'quaternion') {
        // Parse comma-separated quaternion values
        const parts = input.value.split(',').map(s => parseFloat(s.trim()));
        value = {
          qx: parts[0] || 1.0,
          qy: parts[1] || 0.0,
          qz: parts[2] || 0.0,
          qw: parts[3] || 0.0,
        };
      } else {
        value = input.value;
      }
      data[key] = value;
    });

    // Also handle axis physical parameters (nested sub-groups)
    if (group.sub_groups) {
      const parentKey = group.fields.find(f => f.type === 'nested_group');
      const key = parentKey ? parentKey.key : groupId;
      const nestedData = {};

      group.sub_groups.forEach(sub => {
        const subInputs = document.querySelectorAll(`#cfg-sub-${groupId}-${sub.id} .config-input`);
        subInputs.forEach(input => {
          const fieldKey = input.dataset.key;
          const type = input.dataset.type;
          if (!fieldKey) return;

          let value;
          if (type === 'checkbox') {
            value = input.checked;
          } else if (type === 'number') {
            value = parseFloat(input.value);
            if (isNaN(value)) value = 0;
          } else {
            value = input.value;
          }
          nestedData[fieldKey] = value;
        });
      });

      data[key] = nestedData;
    }

    return data;
  }

  // ─── Save / Reset Handlers ────────────────────────────────────────────────

  /**
   * Save a single configuration group.
   */
  async function saveGroup(groupId, subId) {
    const group = CONFIG_GROUPS.find(g => g.id === groupId);
    if (!group) return;

    const statusEl = document.getElementById(`cfg-status-${groupId}${subId ? '-' + subId : ''}`);
    if (statusEl) statusEl.textContent = 'Saving...';

    try {
      const data = collectGroupData(groupId);

      // If saving a sub-group within axis params, only send that sub-group's data
      if (subId && group.sub_groups) {
        const parentKey = group.fields.find(f => f.type === 'nested_group');
        const key = parentKey ? parentKey.key : groupId;
        const subGroup = group.sub_groups.find(s => s.id === subId);
        if (subGroup && key) {
          const subData = {};
          subGroup.fields.forEach(f => {
            if (f.type === 'nested_group') return;
            const input = document.querySelector(`#cfg-sub-${groupId}-${subId} .config-input[data-key="${f.key}"]`);
            if (input) {
              const type = input.dataset.type;
              if (type === 'checkbox') subData[f.key] = input.checked;
              else if (type === 'number') subData[f.key] = parseFloat(input.value) || 0;
              else subData[f.key] = input.value;
            }
          });
          const payload = {};
          payload[key] = currentConfig && currentConfig[key]
            ? { ...currentConfig[key], ...subData }
            : subData;
          await Api.updateConfig(payload);
        }
      } else {
        await Api.updateConfig(data);
      }

      if (statusEl) {
        statusEl.textContent = '✅ Saved';
        statusEl.style.color = 'var(--color-success)';
        setTimeout(() => { if (statusEl) statusEl.textContent = ''; }, 3000);
      }

      // Reload config to reflect changes
      const fresh = await Api.getConfig();
      currentConfig = fresh;
    } catch (err) {
      if (statusEl) {
        statusEl.textContent = `❌ ${err.message}`;
        statusEl.style.color = 'var(--color-danger)';
      }
    }
  }

  /**
   * Reset a configuration group to defaults.
   */
  async function resetGroup(groupId, subId) {
    const statusEl = document.getElementById(`cfg-status-${groupId}${subId ? '-' + subId : ''}`);
    if (statusEl) statusEl.textContent = 'Resetting...';

    try {
      // Try API reset first
      const groupPath = subId ? `${groupId}.${subId}` : groupId;
      await Api.resetGroupConfig(groupPath);

      if (statusEl) {
        statusEl.textContent = '✅ Reset to defaults';
        statusEl.style.color = 'var(--color-success)';
        setTimeout(() => { if (statusEl) statusEl.textContent = ''; }, 3000);
      }

      // Reload config to reflect changes
      const fresh = await Api.getConfig();
      currentConfig = fresh;
      const container = $('#config-content');
      if (container) renderConfig(fresh, container);
    } catch (err) {
      // If API reset fails, try reloading
      try {
        if (statusEl) statusEl.textContent = 'Reloading...';
        const fresh = await Api.getConfig();
        currentConfig = fresh;
        const container = $('#config-content');
        if (container) renderConfig(fresh, container);
      } catch (e) {
        if (statusEl) {
          statusEl.textContent = `❌ ${err.message}`;
          statusEl.style.color = 'var(--color-danger)';
        }
      }
    }
  }

  /**
   * Reset all configuration to defaults.
   */
  async function resetAllConfig() {
    try {
      await Api.resetConfig();

      App.showToast('All configuration reset to defaults', 'success');

      // Reload
      const fresh = await Api.getConfig();
      currentConfig = fresh;
      const container = $('#config-content');
      if (container) renderConfig(fresh, container);
    } catch (err) {
      App.showToast(`Reset failed: ${err.message}`, 'error');
    }
  }

  // ─── Export / Import Configuration ────────────────────────────────────────

  /**
   * Export the current configuration as a downloadable JSON file.
   */
  async function handleExportConfig() {
    try {
      const configData = await Api.getConfig();
      const blob = new Blob([JSON.stringify(configData, null, 2)], { type: 'application/json' });
      const url = URL.createObjectURL(blob);
      const a = document.createElement('a');
      a.href = url;
      const now = new Date();
      const dateStr = now.toISOString().slice(0, 19).replace(/[:-]/g, '');
      a.download = `mount-config-${dateStr}.json`;
      document.body.appendChild(a);
      a.click();
      document.body.removeChild(a);
      URL.revokeObjectURL(url);
      App.showToast('✅ Configuration exported successfully', 'success', 3000);
    } catch (err) {
      App.showToast(`Export failed: ${err.message}`, 'error');
    }
  }

  /**
   * Import configuration from a JSON file selected by the user.
   * Reads the file and applies it via the API.
   */
  function handleImportConfig() {
    const input = document.createElement('input');
    input.type = 'file';
    input.accept = '.json,application/json';
    input.addEventListener('change', async (event) => {
      const file = event.target.files[0];
      if (!file) return;

      // Validate file extension
      if (!file.name.endsWith('.json')) {
        App.showToast('Please select a .json file', 'error');
        return;
      }

      try {
        const text = await file.text();
        let configData;
        try {
          configData = JSON.parse(text);
        } catch (e) {
          App.showToast('Invalid JSON file', 'error');
          return;
        }

        if (typeof configData !== 'object' || configData === null || Array.isArray(configData)) {
          App.showToast('Config file must contain a JSON object', 'error');
          return;
        }

        await Api.updateConfig(configData);
        App.showToast(`✅ Configuration imported from ${file.name}`, 'success', 4000);

        // Reload config to reflect changes
        const fresh = await Api.getConfig();
        currentConfig = fresh;
        const container = $('#config-content');
        if (container) renderConfig(fresh, container);
      } catch (err) {
        App.showToast(`Import failed: ${err.message}`, 'error');
      }
    });
    input.click();
  }

  // ─── Event Binding ────────────────────────────────────────────────────────

  /**
   * Bind events for save/reset buttons, export/import, and the reset-all button.
   */
  function initEventHandlers() {
    // Delegate click events for save/reset buttons
    document.addEventListener('click', (e) => {
      const saveBtn = e.target.closest('.btn-save-group');
      if (saveBtn) {
        const group = saveBtn.dataset.group;
        const sub = saveBtn.dataset.sub;
        if (group) saveGroup(group, sub);
        return;
      }

      const resetBtn = e.target.closest('.btn-reset-group');
      if (resetBtn) {
        const group = resetBtn.dataset.group;
        const sub = resetBtn.dataset.sub;
        if (group) resetGroup(group, sub);
        return;
      }

      const resetAllBtn = e.target.closest('#btn-reset-all-config');
      if (resetAllBtn) {
        if (confirm('Reset ALL configuration to default values? This cannot be undone.')) {
          resetAllConfig();
        }
        return;
      }

      const exportBtn = e.target.closest('#btn-export-config');
      if (exportBtn) {
        handleExportConfig();
        return;
      }

      const importBtn = e.target.closest('#btn-import-config');
      if (importBtn) {
        handleImportConfig();
        return;
      }
    });
  }

  // ─── Utility ──────────────────────────────────────────────────────────────

  function escapeHtml(str) {
    if (str === null || str === undefined) return '';
    return String(str)
      .replace(/&/g, '&')
      .replace(/</g, '<')
      .replace(/>/g, '>')
      .replace(/"/g, '"')
      .replace(/'/g, '&#039;');
  }

  // ─── Address Configuration (unchanged) ──────────────────────────────────

  /**
   * Load current gRPC addresses and populate the form fields.
   */
  async function loadAddresses() {
    const hostCtrl = $('#ctrl-address-host');
    const portCtrl = $('#ctrl-address-port');
    const hostDb = $('#db-address-host');
    const portDb = $('#db-address-port');
    const statusEl = $('#address-status');

    if (!hostCtrl || !portCtrl || !hostDb || !portDb) return;

    try {
      const addr = await Api.getAddresses();
      hostCtrl.value = addr.controller.host;
      portCtrl.value = addr.controller.port;
      hostDb.value = addr.database.host;
      portDb.value = addr.database.port;
      if (statusEl) statusEl.textContent = '';
    } catch (err) {
      if (statusEl) statusEl.textContent = `Failed to load addresses: ${err.message}`;
    }
  }

  /**
   * Save gRPC addresses from the form and reconnect.
   */
  async function saveAddresses() {
    const hostCtrl = $('#ctrl-address-host');
    const portCtrl = $('#ctrl-address-port');
    const hostDb = $('#db-address-host');
    const portDb = $('#db-address-port');
    const statusEl = $('#address-status');

    if (!hostCtrl || !portCtrl || !hostDb || !portDb) return;

    const controllerHost = hostCtrl.value.trim();
    const controllerPort = parseInt(portCtrl.value, 10);
    const databaseHost = hostDb.value.trim();
    const databasePort = parseInt(portDb.value, 10);

    // Validate
    if (!controllerHost) {
      if (statusEl) statusEl.textContent = 'Controller host cannot be empty';
      return;
    }
    if (isNaN(controllerPort) || controllerPort < 1 || controllerPort > 65535) {
      if (statusEl) statusEl.textContent = 'Controller port must be between 1 and 65535';
      return;
    }
    if (!databaseHost) {
      if (statusEl) statusEl.textContent = 'Database host cannot be empty';
      return;
    }
    if (isNaN(databasePort) || databasePort < 1 || databasePort > 65535) {
      if (statusEl) statusEl.textContent = 'Database port must be between 1 and 65535';
      return;
    }

    const saveBtn = $('#btn-save-addresses');
    if (saveBtn) {
      saveBtn.disabled = true;
      saveBtn.textContent = 'Saving...';
    }
    if (statusEl) statusEl.textContent = '';

    try {
      const result = await Api.setAddresses({
        controller: { host: controllerHost, port: controllerPort },
        database: { host: databaseHost, port: databasePort },
      });
      if (statusEl) {
        statusEl.textContent = `✅ ${result.message}`;
        statusEl.style.color = 'var(--color-success)';
      }
    } catch (err) {
      if (statusEl) {
        statusEl.textContent = `❌ ${err.message}`;
        statusEl.style.color = 'var(--color-danger)';
      }
    } finally {
      if (saveBtn) {
        saveBtn.disabled = false;
        saveBtn.textContent = 'Save & Reconnect';
      }
    }
  }

  /**
   * Bind address form events.
   */
  function initAddressForm() {
    const saveBtn = $('#btn-save-addresses');
    if (saveBtn) {
      saveBtn.addEventListener('click', saveAddresses);
    }
  }

  // ─── Initialization ───────────────────────────────────────────────────────

  function init() {
    initEventHandlers();
    initAddressForm();
  }

  // Run init
  init();

  // Public API
  return { loadConfig, loadAddresses, initAddressForm };
})();
