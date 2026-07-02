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
      restartRequired: false,
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
      restartRequired: true,
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
      restartRequired: true,
      fields: [
        { key: 'canopen_interface', label: 'Interface', type: 'text' },
        { key: 'canopen_node_id', label: 'Node ID', type: 'number', min: 1, max: 127 },
        { key: 'canopen_baud_rate', label: 'Baud Rate', type: 'select', options: ['100000', '250000', '500000', '1000000'] },
        { key: 'canopen_enable_sync', label: 'Enable SYNC', type: 'checkbox' },
        { key: 'canopen_sync_interval_ms', label: 'SYNC Interval (ms)', type: 'number', min: 10, max: 10000 },
        { key: 'canopen_accel_mode', label: 'Accel/Decel Mode', type: 'select', options: ['time', 'rate'] },
        { key: 'canopen_pdo_config_enabled', label: 'Write PDO Mappings', type: 'checkbox' },
        { key: 'canopen_position_rewind_enabled', label: 'Position Rewind', type: 'checkbox', help: 'Master switch for the CANopen position rewind mechanism. When disabled, the drive absolute position counter is never reset, regardless of the interval or threshold settings below.' },
        { key: 'canopen_position_rewind_interval_seconds', label: 'Rewind Interval (s)', type: 'number', min: 0, max: 86400, step: 60, help: 'Periodically resets the CANopen drive position counter to prevent overflow beyond ±1,000,000 counts during long tracking sessions.' },
        { key: 'canopen_position_rewind_threshold_percent', label: 'Rewind Threshold (%)', type: 'number', min: 0, max: 100, step: 5, help: 'If the drive position reaches this % of the 1,000,000 count limit, an immediate rewind is triggered without waiting for the time interval.' },
        { key: 'canopen_position_counts_per_degree', label: 'Position Counts/°', type: 'number', min: 0.001, max: 100000, step: 0.001 },
        { key: 'canopen_velocity_counts_per_deg_s', label: 'Velocity Counts per °/s', type: 'number', min: 0.001, max: 100000, step: 0.001 },
      ],
    },
    {
      id: 'mount_location',
      label: 'Mount Location',
      restartRequired: false,
      fields: [
        { key: 'latitude', label: 'Latitude (°)', type: 'number', angleType: 'deg' },
        { key: 'longitude', label: 'Longitude (°)', type: 'number', angleType: 'deg' },
        { key: 'altitude', label: 'Altitude (m)', type: 'number', min: -500, max: 10000 },
      ],
    },
    {
      id: 'mount_general',
      label: 'Mount General',
      restartRequired: false,
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
      restartRequired: false,
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
        { key: 'meridian_flip_hysteresis_degrees', label: 'Hysteresis (°)', type: 'number', angleType: 'deg' },
        { key: 'meridian_flip_timeout_seconds', label: 'Flip Timeout (s)', type: 'number', min: 10, max: 600, step: 5 },
      ],
    },
    {
      id: 'mount_soft_limits',
      label: 'Soft Limits',
      fields: [
        { key: 'soft_limits_enabled', label: 'Enable Soft Limits', type: 'checkbox' },
        { key: 'soft_limit_axis1_min', label: 'Axis 1 Min (°)', type: 'number', angleType: 'deg' },
        { key: 'soft_limit_axis1_max', label: 'Axis 1 Max (°)', type: 'number', angleType: 'deg' },
        { key: 'soft_limit_axis2_min', label: 'Axis 2 Min (°)', type: 'number', angleType: 'deg' },
        { key: 'soft_limit_axis2_max', label: 'Axis 2 Max (°)', type: 'number', angleType: 'deg' },
        { key: 'soft_limit_warning_degrees', label: 'Warning Zone (°)', type: 'number', angleType: 'deg' },
        { key: 'soft_limit_deceleration_degrees', label: 'Deceleration Zone (°)', type: 'number', angleType: 'deg' },
        { key: 'soft_limit_tracking_rate_factor', label: 'Min Rate Factor', type: 'number', min: 0, max: 1, step: 0.01 },
      ],
    },
    {
      id: 'mount_park',
      label: 'Park Position',
      fields: [
        { key: 'park_position_axis1', label: 'Axis 1 Park Position (°)', type: 'number', angleType: 'deg' },
        { key: 'park_position_axis2', label: 'Axis 2 Park Position (°)', type: 'number', angleType: 'deg' },
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
          id: 'ha_encoder',
          label: 'Encoder',
          fields: [
            { key: 'position_counts_per_degree', label: 'CANopen Pos. Counts/°', type: 'number', min: 0.001, max: 100000, step: 0.001 },
            { key: 'velocity_counts_per_deg_s', label: 'CANopen Vel. Counts per °/s', type: 'number', min: 0.001, max: 100000, step: 0.001 },
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
          id: 'dec_encoder',
          label: 'Encoder',
          fields: [
            { key: 'position_counts_per_degree', label: 'CANopen Pos. Counts/°', type: 'number', min: 0.001, max: 100000, step: 0.001 },
            { key: 'velocity_counts_per_deg_s', label: 'CANopen Vel. Counts per °/s', type: 'number', min: 0.001, max: 100000, step: 0.001 },
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
        { key: 'derotator_homing_offset', label: 'Homing Offset (°)', type: 'number', angleType: 'deg' },
      ],
    },
    {
      id: 'loop_timing',
      label: 'Loop Timing',
      fields: [
        { key: 'controller_poll_ms', label: 'Main Loop Poll (ms)', type: 'number', min: 10, max: 500, step: 5 },
        { key: 'tracking_update_ms', label: 'Tracking Update (ms)', type: 'number', min: 5, max: 200, step: 5 },
      ],
    },
    {
      id: 'field_rotation',
      label: 'Field Rotation',
      fields: [
        { key: 'field_rotation_enabled', label: 'Enabled', type: 'checkbox' },
        { key: 'field_rotation_latitude', label: 'Latitude (°)', type: 'number', angleType: 'deg' },
        { key: 'field_rotation_altitude', label: 'Altitude (°)', type: 'number', angleType: 'deg' },
        { key: 'field_rotation_azimuth', label: 'Azimuth (°)', type: 'number', angleType: 'deg' },
        { key: 'field_rotation_computed_rate', label: 'Computed Rate (°/s)', type: 'number', min: -10, max: 10, step: 0.000001 },
        { key: 'field_rotation_applied_correction', label: 'Applied Correction (°)', type: 'number', angleType: 'deg' },
        { key: 'field_rotation_temperature', label: 'Temperature (°C)', type: 'number', min: -50, max: 60, step: 0.1 },
        { key: 'field_rotation_flexure_correction', label: 'Flexure Correction', type: 'number', min: -10, max: 10, step: 0.001 },
      ],
    },
    {
      id: 'servo_init',
      label: 'Servo Initialization (SDO Sequence)',
      fields: [
        { key: 'servo_init_enabled', label: 'Enable Custom Init Sequence', type: 'checkbox' },
        { key: 'servo_init_sequence', label: 'SDO Sequence (JSON array)', type: 'textarea' },
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
        { key: 'hal_gamepad_autostart', label: 'Auto-Start After Boot', type: 'checkbox' },
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
    loop_timing:      '<svg viewBox="0 0 24 24" width="16" height="16" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><circle cx="12" cy="12" r="10"/><polyline points="12 6 12 12 16 14"/></svg>',
    servo_init:       '<svg viewBox="0 0 24 24" width="16" height="16" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M4 19.5A2.5 2.5 0 0 1 6.5 17H20"/><path d="M6.5 2H20v20H6.5A2.5 2.5 0 0 1 4 19.5v-15A2.5 2.5 0 0 1 6.5 2z"/><line x1="8" y1="7" x2="16" y2="7"/><line x1="8" y1="11" x2="16" y2="11"/><line x1="8" y1="15" x2="12" y2="15"/></svg>',
    hal:              '<svg viewBox="0 0 24 24" width="16" height="16" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><rect x="4" y="4" width="16" height="16" rx="2" ry="2"/><rect x="9" y="9" width="6" height="6"/><line x1="9" y1="4" x2="9" y2="9"/><line x1="15" y1="4" x2="15" y2="9"/><line x1="9" y1="15" x2="9" y2="20"/><line x1="15" y1="15" x2="15" y2="20"/><line x1="4" y1="9" x2="9" y2="9"/><line x1="15" y1="9" x2="20" y2="9"/></svg>',
    hal_gamepad:      '<svg viewBox="0 0 24 24" width="16" height="16" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><line x1="6" y1="12" x2="10" y2="12"/><line x1="8" y1="10" x2="8" y2="14"/><line x1="15" y1="13" x2="15.01" y2="13"/><line x1="18" y1="11" x2="18.01" y2="11"/><rect x="2" y="6" width="20" height="12" rx="2"/></svg>',
    // Sub-group icons (axis physical parameters)
    ha_encoder:       '<svg viewBox="0 0 24 24" width="14" height="14" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><circle cx="12" cy="12" r="10"/><path d="M12 2a15 15 0 0 1 0 20 15 15 0 0 1 0-20z"/><line x1="12" y1="12" x2="17" y2="7"/><line x1="12" y1="12" x2="8" y2="16"/></svg>',
    ha_gear:          '<svg viewBox="0 0 24 24" width="14" height="14" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><circle cx="12" cy="12" r="4"/><path d="M12 2v4m0 12v4M2 12h4m12 0h4"/><circle cx="12" cy="12" r="2"/></svg>',
    ha_cyclic_error:  '<svg viewBox="0 0 24 24" width="14" height="14" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><polyline points="22 12 18 12 15 21 9 3 6 12 2 12"/><circle cx="12" cy="12" r="2"/></svg>',
    ha_backlash:      '<svg viewBox="0 0 24 24" width="14" height="14" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><line x1="5" y1="12" x2="19" y2="12"/><polyline points="12 5 19 12 12 19"/></svg>',
    ha_stiffness:     '<svg viewBox="0 0 24 24" width="14" height="14" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M12 2v20M2 12h20"/><path d="M4 4l16 16M20 4L4 20"/></svg>',
    dec_encoder:      '<svg viewBox="0 0 24 24" width="14" height="14" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><circle cx="12" cy="12" r="10"/><path d="M12 2a15 15 0 0 1 0 20 15 15 0 0 1 0-20z"/><line x1="12" y1="12" x2="17" y2="7"/><line x1="12" y1="12" x2="8" y2="16"/></svg>',
    dec_gear:         '<svg viewBox="0 0 24 24" width="14" height="14" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><circle cx="12" cy="12" r="4"/><path d="M12 2v4m0 12v4M2 12h4m12 0h4"/><circle cx="12" cy="12" r="2"/></svg>',
    dec_cyclic_error: '<svg viewBox="0 0 24 24" width="14" height="14" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><polyline points="22 12 18 12 15 21 9 3 6 12 2 12"/><circle cx="12" cy="12" r="2"/></svg>',
    dec_backlash:     '<svg viewBox="0 0 24 24" width="14" height="14" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><line x1="5" y1="12" x2="19" y2="12"/><polyline points="12 5 19 12 12 19"/></svg>',
    dec_stiffness:    '<svg viewBox="0 0 24 24" width="14" height="14" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M12 2v20M2 12h20"/><path d="M4 4l16 16M20 4L4 20"/></svg>',
  };

  // ─── Group-Level Help Descriptions ────────────────────────────────────────

  const GROUP_HELP = {
    logging: 'Konfiguracja systemu logowania: poziom szczegó\u0142owo\u015bci, katalog docelowy, rotacja, rozmiar plików oraz wypisywanie na konsol\u0119.',
    network: 'Konfiguracja serwera gRPC: adres nas\u0142uchiwania, port, maksymalna liczba po\u0142\u0105cze\u0144 oraz opcjonalne szyfrowanie TLS.',
    canopen: 'Konfiguracja magistrali CANopen: interfejs SocketCAN, w\u0142asny Node ID sterownika, szybko\u015b\u0107 transmisji oraz parametry SYNC.',
    mount_location: 'Wspó\u0142rz\u0119dne geograficzne obserwatorium: szeroko\u015b\u0107 i d\u0142ugo\u015b\u0107 geograficzna oraz wysoko\u015b\u0107 n.p.m.',
    mount_general: 'Parametry globalne monta\u017cu: typ monta\u017cu (paralaktyczny/azymutalny), maksymalne pr\u0119dko\u015bci i przyspieszenia przewijania i trackingu.',
    mount_environmental: 'Domy\u015blne parametry \u015brodowiskowe: temperatura, ci\u015bnienie i wilgotno\u015b\u0107 dla oblicze\u0144 refrakcji atmosferycznej.',
    mount_encoders: 'Konfiguracja enkoderów: czy u\u017cywa\u0107 enkoderów, czy s\u0105 absolutne oraz ich rozdzielczo\u015b\u0107.',
    mount_tolerances: 'Tolerancje pozycji i pr\u0119dko\u015bci dla uznania targetu za osi\u0105gni\u0119ty.',
    mount_meridian_flip: 'Automatyczne przej\u015bcia przez po\u0142udnik (meridian flip): opó\u017anienie, histereza i timeout.',
    mount_soft_limits: 'Mi\u0119kkie limity pozycji osi: zakresy ruchu, strefy ostrze\u017cenia i hamowania.',
    mount_park: 'Pozycja parkowania monta\u017cu: k\u0105ty obu osi po zaparkowaniu.',
    mount_atmosphere: 'Korekcja refrakcji atmosferycznej: czy w\u0142\u0105czy\u0107 automatyczn\u0105 korekcj\u0119.',
    mount_orientation: 'Kwaternion orientacji monta\u017cu: definiuje przekszta\u0142cenie mi\u0119dzy uk\u0142adem monta\u017cu a uk\u0142adem niebieskim.',
    ha_axis_params: 'Parametry fizyczne osi HA (Hour Angle / Godzinnej): silnik, enkoder, przek\u0142adnia, b\u0142\u0105d cykliczny, luz, sztywno\u015b\u0107.',
    dec_axis_params: 'Parametry fizyczne osi Dec (Deklinacji): silnik, enkoder, przek\u0142adnia, b\u0142\u0105d cykliczny, luz, sztywno\u015b\u0107.',
    telescope: 'Parametry teleskopu: ogniskowa, apertura, wymiary tubusa, model kamery i parametry matrycy.',
    guider: 'Konfiguracja autoguidera: w\u0142\u0105czenie, parametry po\u0142\u0105czenia, korekcja, agresywno\u015b\u0107, ekspozycja.',
    kalman: 'Filtr Kalmana do estymacji pozycji: szumy procesu i pomiaru, adaptacyjno\u015b\u0107, próg innowacji.',
    tpoint: 'Model TPoint do korekcji b\u0142\u0119dów systematycznych monta\u017cu: aktywne terminy, pomiary, residua.',
    derotator: 'Derotator pola obrazu: typ, prze\u0142o\u017cenie, pr\u0119dko\u015bci, enkoder, pozycja domowa.',
    field_rotation: 'Obliczona rotacja pola: korekcja flexury, temperatura, parametry geometryczne.',
    loop_timing: 'Czasy p\u0119tli g\u0142ównej sterownika: interwa\u0142 odpytywania CANopen i cz\u0119stotliwo\u015b\u0107 aktualizacji trackingu.',
    servo_init: 'Sekwencja inicjalizacyjna SDO dla serwonap\u0119dów: w\u0142\u0105czenie/wy\u0142\u0105czenie oraz lista wpisów JSON ({axis, index, subindex, value, description}) wysy\u0142anych podczas startu.',
    hal: 'Warstwa abstrakcji sprz\u0119towej (HAL): typ interfejsu, parametry CAN, heartbeat i mapowanie PDO.',
    hal_gamepad: 'Konfiguracja gamepada: \u015bcie\u017cka urz\u0105dzenia, strefa martwa, czu\u0142o\u015b\u0107, cz\u0119stotliwo\u015b\u0107 odczytu.',
    // Sub-group help
    ha_encoder: 'Konfiguracja enkodera osi HA: rozdzielczo\u015b\u0107, liczba impulsów, b\u0142\u0105d kwantyzacji.',
    ha_gear: 'Przek\u0142adnia mechaniczna osi HA: prze\u0142o\u017cenie ca\u0142kowite, \u015blimakowe, liczba z\u0119bów.',
    ha_cyclic_error: 'B\u0142\u0105d cykliczny osi HA: amplituda, okres, harmoniczne.',
    ha_backlash: 'Luz mechaniczny osi HA: warto\u015b\u0107 luzu i wspó\u0142czynnik temperaturowy.',
    ha_stiffness: 'Sztywno\u015b\u0107 i w\u0142a\u015bciwo\u015bci termiczne osi HA: podatno\u015b\u0107 skr\u0119tna, rozszerzalno\u015b\u0107, temperatura kalibracji.',
    dec_encoder: 'Konfiguracja enkodera osi Dec: rozdzielczo\u015b\u0107, liczba impulsów, b\u0142\u0105d kwantyzacji.',
    dec_gear: 'Przek\u0142adnia mechaniczna osi Dec: prze\u0142o\u017cenie ca\u0142kowite, \u015blimakowe, liczba z\u0119bów.',
    dec_cyclic_error: 'B\u0142\u0105d cykliczny osi Dec: amplituda, okres, harmoniczne.',
    dec_backlash: 'Luz mechaniczny osi Dec: warto\u015b\u0107 luzu i wspó\u0142czynnik temperaturowy.',
    dec_stiffness: 'Sztywno\u015b\u0107 i w\u0142a\u015bciwo\u015bci termiczne osi Dec: podatno\u015b\u0107 skr\u0119tna, rozszerzalno\u015b\u0107, temperatura kalibracji.',
  };

  // ─── Per-Field Help Descriptions ──────────────────────────────────────────

  const PARAM_HELP = {
    // ── Logging ──
    log_level: {
      description: 'Poziom szczegó\u0142owo\u015bci logów systemowych. DEBUG – najbardziej szczegó\u0142owy (wszystkie komunikaty), INFO – informacje o normalnym dzia\u0142aniu, WARN – ostrze\u017cenia, ERROR – b\u0142\u0119dy, CRITICAL – krytyczne awarie.',
      defaultValue: 'INFO',
      type: 'string (wybór)',
      range: 'DEBUG, INFO, WARN, ERROR, CRITICAL',
    },
    log_directory: {
      description: 'Katalog docelowy plików logów. Podaj pe\u0142n\u0105 \u015bcie\u017ck\u0119 bezwzgl\u0119dn\u0105 na serwerze. Katalog musi istnie\u0107 i mie\u0107 prawa zapisu dla procesu sterownika.',
      defaultValue: '/var/log/astro-mount',
      type: 'string (\u015bcie\u017cka)',
      range: 'dowolna prawid\u0142owa \u015bcie\u017cka bezwzgl\u0119dna',
    },
    log_rotation_days: {
      description: 'Rotacja (archiwizacja) logów po N dniach. Starsze pliki s\u0105 automatycznie usuwane. Warto\u015b\u0107 0 oznacza brak rotacji.',
      defaultValue: '7',
      type: 'integer',
      range: '1 – 365',
    },
    log_max_file_size_mb: {
      description: 'Maksymalny rozmiar pojedynczego pliku logu w megabajtach. Po przekroczeniu tworzony jest nowy plik, a stary jest archiwizowany.',
      defaultValue: '100',
      type: 'integer',
      range: '1 – 1024',
    },
    log_console_output: {
      description: 'Czy wypisywa\u0107 logi równie\u017c na konsol\u0119 (stdout/stderr). Przydatne przy uruchomieniu w terminalu, mo\u017cna wy\u0142\u0105czy\u0107 dla demona.',
      defaultValue: 'true (w\u0142\u0105czone)',
      type: 'boolean',
      range: 'true / false',
    },

    // ── Network ──
    grpc_address: {
      description: 'Adres IPv4, na którym serwer gRPC nas\u0142uchuje po\u0142\u0105cze\u0144. 0.0.0.0 oznacza wszystkie interfejsy sieciowe. Dla bezpiecze\u0144stwa ustaw 127.0.0.1, je\u015bli proxy dzia\u0142a lokalnie.',
      defaultValue: '0.0.0.0',
      type: 'string (adres IPv4)',
      range: 'prawid\u0142owy adres IPv4',
    },
    grpc_port: {
      description: 'Port serwera gRPC dla komunikacji zewn\u0119trznej (web UI, aplikacje klienckie). Upewnij si\u0119, \u017ce port nie jest blokowany przez zapor\u0119 sieciow\u0105.',
      defaultValue: '50051',
      type: 'integer',
      range: '1 – 65535',
    },
    network_max_connections: {
      description: 'Maksymalna liczba równoczesnych po\u0142\u0105cze\u0144 gRPC. Przy du\u017cej liczbie klientów zwi\u0119ksz t\u0119 warto\u015b\u0107. Wp\u0142ywa na zu\u017cycie pami\u0119ci.',
      defaultValue: '10',
      type: 'integer',
      range: '1 – 1000',
    },
    network_enable_ssl: {
      description: 'Czy w\u0142\u0105czy\u0107 szyfrowanie TLS dla po\u0142\u0105cze\u0144 gRPC. Wymaga prawid\u0142owo skonfigurowanych \u015bcie\u017cek certyfikatu i klucza SSL.',
      defaultValue: 'false (wy\u0142\u0105czone)',
      type: 'boolean',
      range: 'true / false',
    },
    network_ssl_cert_path: {
      description: '\u015acie\u017cka do pliku certyfikatu SSL (.pem/.crt) na serwerze. Wymagane, gdy enable_ssl = true.',
      defaultValue: '"" (pusty)',
      type: 'string (\u015bcie\u017cka pliku)',
      range: 'dowolna \u015bcie\u017cka do pliku .pem/.crt',
    },
    network_ssl_key_path: {
      description: '\u015acie\u017cka do pliku klucza prywatnego SSL (.key) na serwerze. Wymagane, gdy enable_ssl = true. Przechowuj w bezpiecznym miejscu.',
      defaultValue: '"" (pusty)',
      type: 'string (\u015bcie\u017cka pliku)',
      range: 'dowolna \u015bcie\u017cka do pliku .key',
    },

    // ── CANopen ──
    canopen_interface: {
      description: 'Nazwa interfejsu SocketCAN w systemie Linux. Standardowo can0, can1 dla fizycznych interfejsów CAN, vcan0 dla symulacji (testy).',
      defaultValue: 'can0',
      type: 'string',
      range: 'nazwa interfejsu CAN (can0, can1, vcan0, ...)',
    },
    canopen_node_id: {
      description: 'CANopen Node ID sterownika – w\u0142asny adres urz\u0105dzenia na magistrali CAN (CiA 301). To NIE jest adres serwonap\u0119dów – te konfiguruje si\u0119 w sekcji HAL > osie > can_node_id.',
      defaultValue: '1',
      type: 'integer',
      range: '1 – 127',
    },
    canopen_baud_rate: {
      description: 'Szybko\u015b\u0107 transmisji magistrali CAN w bodach. Wszystkie urz\u0105dzenia na magistrali musz\u0105 mie\u0107 t\u0119 sam\u0105 warto\u015b\u0107. Dla d\u0142ugich przewodów (ponad 100m) u\u017cyj ni\u017cszej warto\u015bci.',
      defaultValue: '1000000 (1 Mbit/s)',
      type: 'select',
      range: '100000, 250000, 500000, 1000000',
    },
    canopen_enable_sync: {
      description: 'Czy w\u0142\u0105czy\u0107 cykliczny SYNC (CiA 301 §7.2.4). SYNC synchronizuje wszystkie w\u0119z\u0142y CANopen, umo\u017cliwiaj\u0105c synchroniczne PDO.',
      defaultValue: 'true (w\u0142\u0105czone)',
      type: 'boolean',
      range: 'true / false',
    },
    canopen_sync_interval_ms: {
      description: 'Interwa\u0142 mi\u0119dzy kolejnymi komunikatami SYNC w milisekundach. Krótszy interwa\u0142 = wi\u0119ksza precyzja czasowa, ale wi\u0119ksze obci\u0105\u017cenie magistrali.',
      defaultValue: '100',
      type: 'integer',
      range: '10 – 10000',
    },

    // ── Mount Location ──
    latitude: {
      description: 'Szeroko\u015b\u0107 geograficzna obserwatorium w stopniach. Dodatnia dla pó\u0142kuli pó\u0142nocnej (N), ujemna dla po\u0142udniowej (S).',
      defaultValue: '52.0° N',
      type: 'float',
      range: '-90.0 do 90.0',
    },
    longitude: {
      description: 'D\u0142ugo\u015b\u0107 geograficzna obserwatorium w stopniach. Dodatnia na wschód (E) od Greenwich, ujemna na zachód (W).',
      defaultValue: '21.0° E',
      type: 'float',
      range: '-180.0 do 180.0',
    },
    altitude: {
      description: 'Wysoko\u015b\u0107 n.p.m. obserwatorium w metrach. Wp\u0142ywa na obliczenia refrakcji atmosferycznej i ci\u015bnienia.',
      defaultValue: '100.0',
      type: 'float',
      range: '-500 do 10000',
    },
    // ── Mount General ──
    mount_type: {
      description: 'Typ monta\u017cu: EQUATORIAL (paralaktyczny, wymaga gwiazdowej pr\u0119dko\u015bci korekcyjnej tylko w jednej osi), ALT_AZ (azymutalny, wymaga korekcji w obu osiach), CASUAL (niestandardowy).',
      defaultValue: 'EQUATORIAL',
      type: 'select',
      range: 'EQUATORIAL, ALT_AZ, CASUAL, UNKNOWN',
    },
    max_slew_rate: {
      description: 'Maksymalna pr\u0119dko\u015b\u0107 przewijania monta\u017cu w stopniach na sekund\u0119. Wy\u017csze warto\u015bci = szybsze przemieszczanie, ale wi\u0119ksze obci\u0105\u017cenie mechaniki.',
      defaultValue: '5.0',
      type: 'float',
      range: '0.1 – 50.0',
    },
    max_tracking_rate: {
      description: 'Maksymalna pr\u0119dko\u015b\u0107 trackingu w stopniach na sekund\u0119. Standardowo ~0.004178°/s (1× pr\u0119dko\u015b\u0107 gwiazdowa). Wy\u017csze warto\u015bci dla korekcji ksi\u0119\u017cycowej/s\u0142onecznej.',
      defaultValue: '0.004178',
      type: 'float',
      range: '0.0001 – 0.1',
    },
    slew_acceleration: {
      description: 'Przyspieszenie przewijania w stopniach na sekund\u0119 kwadrat. Wy\u017csze warto\u015bci = szybsze osi\u0105ganie pr\u0119dko\u015bci, ale wi\u0119ksze obci\u0105\u017cenie mechaniki i silników.',
      defaultValue: '1.0',
      type: 'float',
      range: '0.01 – 20.0',
    },
    tracking_acceleration: {
      description: 'Przyspieszenie trackingu w stopniach na sekund\u0119 kwadrat. Niska warto\u015b\u0107 zapewnia p\u0142ynne zmiany pr\u0119dko\u015bci bez szarpni\u0119\u0107.',
      defaultValue: '0.001',
      type: 'float',
      range: '0.0001 – 1.0',
    },

    // ── Mount Environmental ──
    default_temperature: {
      description: 'Domy\u015blna temperatura otoczenia w stopniach Celsjusza (°C). U\u017cywana do oblicze\u0144 refrakcji atmosferycznej oraz modeli termicznych osi.',
      defaultValue: '15.0',
      type: 'float',
      range: '-50.0 do 60.0',
    },
    default_pressure: {
      description: 'Domy\u015blne ci\u015bnienie atmosferyczne w hektopaskalach (hPa). U\u017cywane do oblicze\u0144 refrakcji atmosferycznej. Standardowe ci\u015bnienie na poziomie morza: 1013.25 hPa.',
      defaultValue: '1013.25',
      type: 'float',
      range: '500.0 – 1100.0',
    },
    default_humidity: {
      description: 'Domy\u015blna wilgotno\u015b\u0107 wzgl\u0119dna (0.0 = 0%, 1.0 = 100%). U\u017cywana w zaawansowanych modelach refrakcji atmosferycznej.',
      defaultValue: '0.5 (50%)',
      type: 'float',
      range: '0.0 – 1.0',
    },

    // ── Mount Encoders ──
    use_encoders: {
      description: 'Czy u\u017cywa\u0107 enkoderów do sprz\u0119\u017cenia zwrotnego pozycji. Je\u015bli wy\u0142\u0105czone, system polega na estymacji pozycji z silników (przyrostowo).',
      defaultValue: 'true (w\u0142\u0105czone)',
      type: 'boolean',
      range: 'true / false',
    },
    encoders_absolute: {
      description: 'Czy enkodery s\u0105 absolutne (true) czy inkrementalne (false). Enkodery absolutne pami\u0119taj\u0105 pozycj\u0119 po w\u0142\u0105czeniu zasilania, inkrementalne wymagaj\u0105 referencji (homing).',
      defaultValue: 'true (absolutne)',
      type: 'boolean',
      range: 'true / false',
    },
    encoder_resolution_config: {
      description: 'Rozdzielczo\u015b\u0107 enkoderów w liczbie impulsów na pe\u0142ny obrót (counts per revolution). Wy\u017csza warto\u015b\u0107 = wi\u0119ksza precyzja, ale wi\u0119ksze obci\u0105\u017cenie obliczeniowe.',
      defaultValue: '16384',
      type: 'integer',
      range: '1 – 10000000',
    },

    // ── Mount Tolerances ──
    position_tolerance: {
      description: 'Tolerancja pozycji w stopniach. Gdy ró\u017cnica mi\u0119dzy pozycj\u0105 docelow\u0105 a aktualn\u0105 jest mniejsza od tej warto\u015bci, target jest uznawany za osi\u0105gni\u0119ty.',
      defaultValue: '0.1',
      type: 'float',
      range: '0.001 – 10.0',
    },
    rate_tolerance: {
      description: 'Tolerancja pr\u0119dko\u015bci w stopniach na sekund\u0119. Gdy pr\u0119dko\u015b\u0107 osi\u0105gnie docelow\u0105 warto\u015b\u0107 z dok\u0142adno\u015bci\u0105 do tego zakresu, uznaje si\u0119 j\u0105 za stabiln\u0105.',
      defaultValue: '0.01',
      type: 'float',
      range: '0.0001 – 1.0',
    },

    // ── Meridian Flip ──
    meridian_flip_enabled: {
      description: 'Czy w\u0142\u0105czy\u0107 automatyczne przej\u015bcie przez po\u0142udnik (meridian flip). Dla monta\u017cu paralaktycznego – zmiana strony rury po przekroczeniu po\u0142udnika.',
      defaultValue: 'true (w\u0142\u0105czone)',
      type: 'boolean',
      range: 'true / false',
    },
    meridian_flip_delay_minutes: {
      description: 'Opó\u017anienie w minutach po osi\u0105gni\u0119ciu po\u0142udnika przed rozpocz\u0119ciem flipa. Pozwala na doko\u0144czenie ekspozycji bez przerywania.',
      defaultValue: '5.0',
      type: 'float',
      range: '0.0 – 60.0',
    },
    meridian_flip_hysteresis_degrees: {
      description: 'Histereza flipa w stopniach – zapobiega oscylacjom (wielokrotnym flipom) wokó\u0142 granicy po\u0142udnika przy wietrznej pogodzie.',
      defaultValue: '0.5',
      type: 'float',
      range: '0.0 – 10.0',
    },
    meridian_flip_timeout_seconds: {
      description: 'Maksymalny czas trwania manewru meridian flip w sekundach. Po przekroczeniu tego czasu operacja jest przerywana i zg\u0142aszany jest b\u0142\u0105d.',
      defaultValue: '120.0',
      type: 'float',
      range: '10 – 600',
    },

    // ── Soft Limits ──
    soft_limits_enabled: {
      description: 'Czy w\u0142\u0105czy\u0107 mi\u0119kkie limity pozycji osi. Limity zapobiegaj\u0105 ruchowi poza dozwolony zakres, chroni\u0105c sprz\u0119t przed uszkodzeniem.',
      defaultValue: 'true (w\u0142\u0105czone)',
      type: 'boolean',
      range: 'true / false',
    },
    soft_limit_axis1_min: {
      description: 'Minimalna dozwolona pozycja osi 1 (HA/Azm) w stopniach. Ruch poni\u017cej tej warto\u015bci jest blokowany.',
      defaultValue: '-270.0',
      type: 'float',
      range: '-360.0 do 360.0',
    },
    soft_limit_axis1_max: {
      description: 'Maksymalna dozwolona pozycja osi 1 (HA/Azm) w stopniach. Ruch powy\u017cej tej warto\u015bci jest blokowany.',
      defaultValue: '270.0',
      type: 'float',
      range: '-360.0 do 360.0',
    },
    soft_limit_axis2_min: {
      description: 'Minimalna dozwolona pozycja osi 2 (Dec/Alt) w stopniach. Ruch poni\u017cej tej warto\u015bci jest blokowany.',
      defaultValue: '-5.0',
      type: 'float',
      range: '-360.0 do 360.0',
    },
    soft_limit_axis2_max: {
      description: 'Maksymalna dozwolona pozycja osi 2 (Dec/Alt) w stopniach. Ruch powy\u017cej tej warto\u015bci jest blokowany.',
      defaultValue: '185.0',
      type: 'float',
      range: '-360.0 do 360.0',
    },
    soft_limit_warning_degrees: {
      description: 'Strefa ostrze\u017cenia przed osi\u0105gni\u0119ciem limitu w stopniach. W tej strefie pr\u0119dko\u015b\u0107 jest redukowana zgodnie z tracking_rate_factor.',
      defaultValue: '10.0',
      type: 'float',
      range: '0.0 – 90.0',
    },
    soft_limit_deceleration_degrees: {
      description: 'Strefa hamowania przed limitem w stopniach. W tej strefie monta\u017c delikatnie zwalnia do ca\u0142kowitego zatrzymania na granicy limitu.',
      defaultValue: '5.0',
      type: 'float',
      range: '0.0 – 90.0',
    },
    soft_limit_tracking_rate_factor: {
      description: 'Wspó\u0142czynnik redukcji pr\u0119dko\u015bci w strefie ostrze\u017cenia (0.0 = zatrzymanie, 1.0 = pe\u0142na pr\u0119dko\u015b\u0107). Ni\u017csza warto\u015b\u0107 = wi\u0119ksze spowolnienie.',
      defaultValue: '0.1',
      type: 'float',
      range: '0.0 – 1.0',
    },

    // ── Park Position ──
    park_position_axis1: {
      description: 'Pozycja parkowania osi 1 (HA/Azm) w stopniach. Po zaparkowaniu monta\u017c ustawia si\u0119 w tej pozycji i wy\u0142\u0105cza nap\u0119dy.',
      defaultValue: '0.0',
      type: 'float',
      range: '-360.0 do 360.0',
    },
    park_position_axis2: {
      description: 'Pozycja parkowania osi 2 (Dec/Alt) w stopniach. Dla monta\u017cu paralaktycznego typowo 90° (skierowanie na polarn\u0105).',
      defaultValue: '90.0',
      type: 'float',
      range: '-360.0 do 360.0',
    },

    // ── Atmospheric Correction ──
    enable_refraction_correction: {
      description: 'Czy w\u0142\u0105czy\u0107 automatyczn\u0105 korekcj\u0119 refrakcji atmosferycznej. Refrakcja powoduje pozorne podniesienie obiektów nad horyzontem – korekcja poprawia celowanie przy niskich wysoko\u015bciach.',
      defaultValue: 'true (w\u0142\u0105czone)',
      type: 'boolean',
      range: 'true / false',
    },

    // ── Mount Orientation (Quaternion) ──
    mount_orientation: {
      description: 'Kwaternion orientacji monta\u017cu (qx, qy, qz, qw). Definiuje przekszta\u0142cenie mi\u0119dzy uk\u0142adem wspó\u0142rz\u0119dnych monta\u017cu a uk\u0142adem niebieskim. Warto\u015bci jako lista oddzielona przecinkami.',
      defaultValue: '1.0, 0.0, 0.0, 0.0',
      type: 'float[4] (kwaternion)',
      range: 'kwaternion jednostkowy (|q| = 1)',
    },

    // ── Telescope ──
    focal_length: {
      description: 'Ogniskowa teleskopu w milimetrach (mm). Wp\u0142ywa na skal\u0119 obrazu (arcsec/pixel) oraz obliczenia trackingu i rotacji pola.',
      defaultValue: '2000.0',
      type: 'float',
      range: '1.0 – 50000.0',
    },
    aperture: {
      description: '\u015arednica / apertura teleskopu w milimetrach (mm). Wp\u0142ywa na zdolno\u015b\u0107 zbierania \u015bwiat\u0142a i rozdzielczo\u015b\u0107 teoretyczn\u0105.',
      defaultValue: '200.0',
      type: 'float',
      range: '1.0 – 50000.0',
    },
    tube_length: {
      description: 'D\u0142ugo\u015b\u0107 tubusa teleskopu w milimetrach (mm). U\u017cywana w obliczeniach flexury i momentów bezw\u0142adno\u015bci.',
      defaultValue: '1800.0',
      type: 'float',
      range: '1.0 – 50000.0',
    },
    camera_model: {
      description: 'Model kamery astronomicznej (ci\u0105g znaków). S\u0142u\u017cy wy\u0142\u0105cznie do celów informacyjnych i logowania.',
      defaultValue: 'ASI1600',
      type: 'string (tekst)',
      range: 'dowolny ci\u0105g znaków',
    },
    pixel_size: {
      description: 'Rozmiar pojedynczego piksela matrycy w mikrometrach (µm). Wp\u0142ywa na skal\u0119 obrazu (arcsec/pixel) przy danej ogniskowej.',
      defaultValue: '3.8',
      type: 'float',
      range: '0.1 – 100.0',
    },
    sensor_width: {
      description: 'Szeroko\u015b\u0107 matrycy kamery w pikselach. U\u017cywana do oblicze\u0144 pola widzenia i rotacji.',
      defaultValue: '4656',
      type: 'integer',
      range: '1 – 50000',
    },
    sensor_height: {
      description: 'Wysoko\u015b\u0107 matrycy kamery w pikselach. U\u017cywana do oblicze\u0144 pola widzenia i rotacji.',
      defaultValue: '3520',
      type: 'integer',
      range: '1 – 50000',
    },

    // ── Guider ──
    guider_enabled: {
      description: 'Czy w\u0142\u0105czy\u0107 autoguider. Autoguider koryguje pozycj\u0119 monta\u017cu na podstawie obrazu z kamery prowadz\u0105cej, kompensuj\u0105c b\u0142\u0119dy trackingu.',
      defaultValue: 'false (wy\u0142\u0105czone)',
      type: 'boolean',
      range: 'true / false',
    },
    guider_connection_string: {
      description: 'Ci\u0105g po\u0142\u0105czenia do kamery guidowej. Format zale\u017cny od u\u017cywanego protoko\u0142u (np. INDI: "indi://localhost:7624/my_ccd").',
      defaultValue: '"" (pusty)',
      type: 'string',
      range: 'dowolny ci\u0105g znaków',
    },
    guider_max_correction: {
      description: 'Maksymalna korekcja pozycji przez autoguider w sekundach k\u0105towych (arcsec). Zapobiega zbyt agresywnym korektom.',
      defaultValue: '10.0',
      type: 'float',
      range: '0.1 – 100.0',
    },
    guider_aggression: {
      description: 'Agresywno\u015b\u0107 korekcji autoguidera (0.0 = brak korekcji, 1.0 = pe\u0142na korekcja). Ni\u017csze warto\u015bci = bardziej stabilny, ale wolniejszy tracking.',
      defaultValue: '0.5',
      type: 'float',
      range: '0.0 – 1.0',
    },
    guider_exposure_time_ms: {
      description: 'Czas ekspozycji klatki guidowej w milisekundach (ms). D\u0142u\u017cszy czas = wi\u0119cej gwiazd do prowadzenia, ale wolniejsza reakcja na zmiany.',
      defaultValue: '2000',
      type: 'integer',
      range: '10 – 60000',
    },
    guider_binning: {
      description: 'Binning kamery guidowej (1 = brak, 2 = 2×2, 3 = 3×3, 4 = 4×4). Wy\u017cszy binning = wi\u0119ksza czu\u0142o\u015b\u0107, ale mniejsza rozdzielczo\u015b\u0107.',
      defaultValue: '2',
      type: 'select',
      range: '1, 2, 3, 4',
    },

    // ── Kalman Filter ──
    process_noise: {
      description: 'Szum procesu (Q) filtru Kalmana. Wy\u017csza warto\u015b\u0107 = szybsza adaptacja do zmian, ale wi\u0119ksze wahania estymaty. Ni\u017csza = g\u0142adsza, ale wolniejsza odpowied\u017a.',
      defaultValue: '0.01',
      type: 'float',
      range: '0.0001 – 100.0',
    },
    measurement_noise: {
      description: 'Szum pomiaru (R) filtru Kalmana. Wy\u017csza warto\u015b\u0107 = wi\u0119ksze wyg\u0142adzenie (ufasz modelowi bardziej ni\u017c pomiarom). Ni\u017csza = szybsza reakcja na pomiary.',
      defaultValue: '1.0',
      type: 'float',
      range: '0.0001 – 100.0',
    },
    kalman_adaptive_q: {
      description: 'Adaptacyjne Q – filtr automatycznie dostosowuje szum procesu do aktualnych warunków. Zalecane w\u0142\u0105czenie dla zmiennych warunków.',
      defaultValue: 'true (w\u0142\u0105czone)',
      type: 'boolean',
      range: 'true / false',
    },
    kalman_adaptive_r: {
      description: 'Adaptacyjne R – filtr automatycznie dostosowuje szum pomiaru. W\u0142\u0105czenie poprawia dzia\u0142anie przy zmiennej jako\u015bci pomiarów.',
      defaultValue: 'false (wy\u0142\u0105czone)',
      type: 'boolean',
      range: 'true / false',
    },
    kalman_innovation_threshold: {
      description: 'Próg innowacji w sigma – warto\u015bci odstaj\u0105ce powy\u017cej tego progu s\u0105 odrzucane jako outliery. Chroni przed gwa\u0142townymi skokami spowodowanymi b\u0142\u0119dami pomiaru.',
      defaultValue: '3.0',
      type: 'float',
      range: '0.1 – 100.0',
    },
    kalman_max_iterations: {
      description: 'Maksymalna liczba iteracji korekcji filtru Kalmana. Wi\u0119cej iteracji = dok\u0142adniejsza estymacja kosztem wi\u0119kszego obci\u0105\u017cenia CPU.',
      defaultValue: '10',
      type: 'integer',
      range: '1 – 1000',
    },

    // ── TPOINT ──
    tpoint_enabled_terms: {
      description: 'Maska bitowa w\u0142\u0105czonych wyrazów modelu TPoint. Ka\u017cdy bit odpowiada jednemu terminowi korekcji (np. IH, ID, CH, ME, MA, ...). 65535 = wszystkie dost\u0119pne terminy.',
      defaultValue: '65535',
      type: 'integer (bitmask)',
      range: '0 – 65535',
    },
    tpoint_min_measurements: {
      description: 'Minimalna liczba pomiarów wymagana do przeprowadzenia kalibracji TPoint. Wi\u0119cej pomiarów = dok\u0142adniejszy model, ale d\u0142u\u017cszy czas zbierania.',
      defaultValue: '10',
      type: 'integer',
      range: '1 – 1000',
    },
    tpoint_max_residual: {
      description: 'Maksymalny dopuszczalny residuum pomiaru TPoint w sekundach k\u0105towych (arcsec). Pomiar przekraczaj\u0105cy ten próg jest odrzucany jako outlier.',
      defaultValue: '30.0',
      type: 'float',
      range: '0.1 – 1000.0',
    },
    tpoint_auto_calibrate: {
      description: 'Automatyczna kalibracja TPoint – po zebraniu minimalnej liczby pomiarów model jest automatycznie obliczany i stosowany.',
      defaultValue: 'true (w\u0142\u0105czone)',
      type: 'boolean',
      range: 'true / false',
    },

    // ── Derotator ──
    derotator_type: {
      description: 'Typ derotatora: CANOPEN (przez magistral\u0119 CANopen), STEPPER (silnik krokowy), SERVO (serwonap\u0119d), CUSTOM (w\u0142asna implementacja).',
      defaultValue: 'CANOPEN',
      type: 'select',
      range: 'CANOPEN, STEPPER, SERVO, CUSTOM',
    },
    derotator_enabled: {
      description: 'Czy w\u0142\u0105czy\u0107 derotator. Derotator kompensuje rotacj\u0119 pola obrazu spowodowan\u0105 ruchem monta\u017cu.',
      defaultValue: 'false (wy\u0142\u0105czone)',
      type: 'boolean',
      range: 'true / false',
    },
    derotator_connection_string: {
      description: 'Ci\u0105g po\u0142\u0105czenia derotatora (je\u015bli dotyczy). Format zale\u017cny od typu i protoko\u0142u.',
      defaultValue: '"" (pusty)',
      type: 'string',
      range: 'dowolny ci\u0105g znaków',
    },
    derotator_gear_ratio: {
      description: 'Prze\u0142o\u017cenie mechaniczne derotatora. Stosunek obrotów silnika do obrotów derotatora.',
      defaultValue: '10.0',
      type: 'float',
      range: '0.1 – 10000.0',
    },
    derotator_max_speed: {
      description: 'Maksymalna pr\u0119dko\u015b\u0107 obrotowa derotatora w stopniach na sekund\u0119 (°/s).',
      defaultValue: '5.0',
      type: 'float',
      range: '0.1 – 180.0',
    },
    derotator_max_acceleration: {
      description: 'Maksymalne przyspieszenie derotatora w stopniach na sekund\u0119 kwadrat (°/s²).',
      defaultValue: '2.0',
      type: 'float',
      range: '0.1 – 180.0',
    },
    derotator_backlash: {
      description: 'Luz mechaniczny derotatora w sekundach k\u0105towych (arcsec). Kompensowany przez system sterowania.',
      defaultValue: '2.0',
      type: 'float',
      range: '0.0 – 1000.0',
    },
    derotator_absolute_encoder: {
      description: 'Czy derotator ma enkoder absolutny. Enkoder absolutny zna swoj\u0105 pozycj\u0119 po w\u0142\u0105czeniu zasilania, nie wymaga homingu.',
      defaultValue: 'true (absolutny)',
      type: 'boolean',
      range: 'true / false',
    },
    derotator_encoder_resolution: {
      description: 'Rozdzielczo\u015b\u0107 enkodera derotatora w liczbie impulsów na obrót.',
      defaultValue: '16384',
      type: 'integer',
      range: '1 – 10000000',
    },
    derotator_homing_offset: {
      description: 'Offset (przesuni\u0119cie) pozycji domowej derotatora w stopniach (°). Umo\u017cliwia kalibracj\u0119 punktu zerowego.',
      defaultValue: '0.0',
      type: 'float',
      range: '-360.0 do 360.0',
    },

    canopen_pdo_config_enabled: {
      description: 'Zapisuje mapowanie PDO (Process Data Objects) do serwonap\u0119du podczas inicjalizacji. Mo\u017ce nadpisa\u0107 parametry fabryczne nap\u0119du.',
      defaultValue: 'false (wy\u0142\u0105czone)',
      type: 'boolean',
      range: 'true / false',
    },

    canopen_position_rewind_enabled: {
      description: 'Główny przełącznik mechanizmu przewijania pozycji CANopen. Gdy wyłączone, licznik bezwzględnej pozycji napędu nigdy nie jest resetowany, niezależnie od ustawień interwału i progu poniżej.',
      defaultValue: 'true (włączone)',
      type: 'boolean',
      range: 'true / false',
    },

    canopen_position_rewind_interval_seconds: {
      description: 'Okresowo resetuje licznik pozycji sterownika CANopen, aby zapobiec przekroczeniu limitu \u00B11\u00A0000\u00A0000 zlicze\u0144 podczas d\u0142ugich sesji \u015bledzenia. Operacja nie powoduje fizycznego ruchu — zmienia tylko wewn\u0119trzny offset. Ustaw 0 aby wy\u0142\u0105czy\u0107.',
      defaultValue: '3600 (1 godzina)',
      type: 'number',
      range: '0 – 86400 (0 = wy\u0142\u0105czone)',
    },

    canopen_position_rewind_threshold_percent: {
      description: 'Je\u015bli pozycja sterownika CANopen (w zliczeniach) osi\u0105gnie ten procent limitu 1\u00A0000\u00A0000, przewini\u0119cie jest wykonywane natychmiast, bez oczekiwania na up\u0142yw interwa\u0142u czasowego. Ustaw 0 aby wy\u0142\u0105czy\u0107 sprawdzanie progu.',
      defaultValue: '80',
      type: 'number',
      range: '0 – 100 (0 = wy\u0142\u0105czone)',
    },

    // ── Loop Timing ──
    controller_poll_ms: {
      description: 'Interwa\u0142 g\u0142ównej p\u0119tli sterownika w milisekundach. Okre\u015bla cz\u0119stotliwo\u015b\u0107 odpytywania enkoderów i CANopen (domy\u015blnie 50 ms = 20 Hz).',
      defaultValue: '50',
      type: 'integer',
      range: '10 – 500',
    },
    tracking_update_ms: {
      description: 'Interwa\u0142 aktualizacji trackingu w milisekundach. Okre\u015bla cz\u0119stotliwo\u015b\u0107 korekcji pozycji podczas \u015bledzenia (domy\u015blnie 20 ms = 50 Hz).',
      defaultValue: '20',
      type: 'integer',
      range: '5 – 200',
    },

    // ── Field Rotation ──
    field_rotation_enabled: {
      description: 'Czy w\u0142\u0105czy\u0107 korekcj\u0119 rotacji pola. Oblicza i kompensuje rotacj\u0119 pola obrazu spowodowan\u0105 ruchem monta\u017cu.',
      defaultValue: 'false (wy\u0142\u0105czone)',
      type: 'boolean',
      range: 'true / false',
    },
    field_rotation_latitude: {
      description: 'Szeroko\u015b\u0107 geograficzna obserwatora w stopniach dla oblicze\u0144 rotacji pola.',
      defaultValue: '52.0',
      type: 'float',
      range: '-90.0 do 90.0',
    },
    field_rotation_altitude: {
      description: 'Wysoko\u015b\u0107 (altitude) celu nad horyzontem w stopniach dla obliczenia rotacji pola.',
      defaultValue: '0.0',
      type: 'float',
      range: '-90.0 do 90.0',
    },
    field_rotation_azimuth: {
      description: 'Azymut celu w stopniach dla obliczenia rotacji pola.',
      defaultValue: '0.0',
      type: 'float',
      range: '0.0 do 360.0',
    },
    field_rotation_computed_rate: {
      description: 'Obliczona pr\u0119dko\u015b\u0107 rotacji pola w stopniach na sekund\u0119. Warto\u015b\u0107 wyliczana automatycznie na podstawie pozycji i ruchu monta\u017cu.',
      defaultValue: '0.0',
      type: 'float',
      range: '-10.0 do 10.0',
    },
    field_rotation_applied_correction: {
      description: 'Zastosowana korekcja rotacji pola w stopniach. Aktualna warto\u015b\u0107 korekcji na\u0142o\u017cona na derotator.',
      defaultValue: '0.0',
      type: 'float',
      range: '-360.0 do 360.0',
    },
    field_rotation_temperature: {
      description: 'Temperatura dla korekcji rotacji pola w stopniach Celsjusza. Wp\u0142ywa na obliczenia termiczne derotatora.',
      defaultValue: '15.0',
      type: 'float',
      range: '-50.0 do 60.0',
    },
    field_rotation_flexure_correction: {
      description: 'Korekcja flexury (ugina\u0107) dla rotacji pola. Kompensuje odkszta\u0142cenia mechaniczne wp\u0142ywaj\u0105ce na rotacj\u0119.',
      defaultValue: '0.0',
      type: 'float',
      range: '-10.0 do 10.0',
    },

    // ── HAL ──
    hal_interface_type: {
      description: 'Typ backendu warstwy abstrakcji sprz\u0119towej (HAL). CANopen – fizyczna magistrala CAN, Serial – port szeregowy, Ethernet – sie\u0107 Ethernet, Simulated – symulacja (testy), Custom – w\u0142asna implementacja.',
      defaultValue: 'CANopen',
      type: 'select',
      range: 'CANopen, Serial, Ethernet, Simulated, Custom',
    },
    hal_can_interface: {
      description: 'Nazwa interfejsu SocketCAN dla warstwy HAL. Mo\u017ce si\u0119 ró\u017cni\u0107 od interfejsu w sekcji CANopen.',
      defaultValue: 'can0',
      type: 'string',
      range: 'nazwa interfejsu CAN (can0, can1, vcan0)',
    },
    hal_can_node_id: {
      description: 'Lokalny CANopen Node ID sterownika w warstwie HAL. Adres w\u0142asny urz\u0105dzenia na magistrali CAN.',
      defaultValue: '1',
      type: 'integer',
      range: '1 – 127',
    },
    hal_can_baud_rate: {
      description: 'Szybko\u015b\u0107 transmisji CAN w warstwie HAL w bodach.',
      defaultValue: '1000000',
      type: 'integer',
      range: '10000 – 10000000',
    },
    hal_heartbeat_interval_ms: {
      description: 'Interwa\u0142 wysy\u0142ania Heartbeat NMT w milisekundach (CiA 301). Okre\u015bla jak cz\u0119sto sterownik informuje sie\u0107 o swoim stanie.',
      defaultValue: '100',
      type: 'integer',
      range: '10 – 60000',
    },
    hal_pdo_mapping_mode: {
      description: 'Tryb mapowania PDO (Process Data Object). Okre\u015bla, które obiekty s\u0105 mapowane do PDO dla szybkiej komunikacji cyklicznej.',
      defaultValue: '"" (domy\u015blny)',
      type: 'string',
      range: 'dowolny ci\u0105g znaków (zale\u017cny od biblioteki CANopen)',
    },

    // ── HAL Gamepad ──
    hal_gamepad_device_path: {
      description: '\u015acie\u017fka urz\u0105dzenia wej\u015bciowego gamepada w systemie Linux (np. /dev/input/js0, /dev/input/event3). U\u017cyj `ls /dev/input/` aby znale\u017a\u0107 w\u0142a\u015bciwe urz\u0105dzenie.',
      defaultValue: '"" (pusty)',
      type: 'string (\u015bcie\u017fka)',
      range: '/dev/input/*',
    },
    hal_gamepad_deadzone: {
      description: 'Strefa martwa analogowych osi gamepada (0.0–1.0). Warto\u015bci w tej strefie s\u0105 ignorowane, zapobiegaj\u0105c dryftowi osi.',
      defaultValue: '0.15',
      type: 'float',
      range: '0.0 – 1.0',
    },
    hal_gamepad_sensitivity: {
      description: 'Czu\u0142o\u015b\u0107 osi analogowych gamepada. Wy\u017csza warto\u015b\u0107 = wi\u0119kszy ruch przy tym samym wychyleniu.',
      defaultValue: '1.0',
      type: 'float',
      range: '0.1 – 10.0',
    },
    hal_gamepad_poll_interval_ms: {
      description: 'Cz\u0119stotliwo\u015b\u0107 odczytu stanu gamepada w milisekundach. Ni\u017csza warto\u015b\u0107 = szybsza reakcja, ale wi\u0119ksze obci\u0105\u017cenie CPU.',
      defaultValue: '50',
      type: 'integer',
      range: '5 – 1000',
    },
    hal_gamepad_autostart: {
      description: 'Automatycznie uruchamia p\u0119tl\u0119 gamepada po starcie systemu. Nie wymaga r\u0119cznego w\u0142\u0105czania z poziomu UI.',
      defaultValue: 'false (wy\u0142\u0105czone)',
      type: 'boolean',
      range: 'true / false',
    },

    // ── Axis Physical Parameters (shared between HA and Dec) ──
    encoder_resolution: {
      description: 'Rozdzielczo\u015b\u0107 enkodera w liczbie impulsów na pe\u0142ny obrót (counts per revolution). Wy\u017csza warto\u015b\u0107 = wy\u017csza precyzja (ale wolniejszy maksymalny odczyt).',
      defaultValue: '16384',
      type: 'integer',
      range: '1 – 10000000',
    },
    encoder_counts_per_arcsec: {
      description: 'Liczba impulsów enkodera na sekund\u0119 k\u0105tow\u0105 (arcsec). Stosunek rozdzielczo\u015bci enkodera do rzeczywistego przesuni\u0119cia k\u0105towego osi.',
      defaultValue: '0.0126',
      type: 'float',
      range: '0.0001 – 100.0',
    },
    encoder_quantization_error: {
      description: 'B\u0142\u0105d kwantyzacji enkodera w milisekundach k\u0105towych (mas). Minimalny b\u0142\u0105d wynikaj\u0105cy z dyskretnej natury pomiaru enkoderem.',
      defaultValue: '39.6',
      type: 'float',
      range: '0.0 – 1000.0',
    },
    gear_ratio: {
      description: 'Ca\u0142kowite prze\u0142o\u017cenie przek\u0142adni mi\u0119dzy wa\u0142em silnika a osi\u0105 monta\u017cu. Stosunek obrotów silnika do obrotów osi.',
      defaultValue: '360.0',
      type: 'float',
      range: '1.0 – 10000.0',
    },
    worm_ratio: {
      description: 'Prze\u0142o\u017cenie przek\u0142adni \u015blimakowej. Liczba obrotów \u015blimaka potrzebna do jednego obrotu ko\u0142a \u015blimacznicy.',
      defaultValue: '180.0',
      type: 'float',
      range: '1.0 – 10000.0',
    },
    worm_teeth: {
      description: 'Liczba z\u0119bów \u015blimaka (zazwyczaj 1 – pojedynczy start). Dla \u015blimaków wielokrotnych (multi-start) podaj liczb\u0119 startów.',
      defaultValue: '1',
      type: 'integer',
      range: '1 – 1000',
    },
    worm_wheel_teeth: {
      description: 'Liczba z\u0119bów ko\u0142a \u015blimacznicy. Okre\u015bla prze\u0142o\u017cenie wraz z liczb\u0105 z\u0119bów \u015blimaka.',
      defaultValue: '180',
      type: 'integer',
      range: '1 – 10000',
    },
    cyclic_error_amplitude: {
      description: 'Amplituda b\u0142\u0119du cyklicznego w sekundach k\u0105towych (arcsec). B\u0142\u0105d okresowy zwi\u0105zany z przek\u0142adni\u0105 \u015blimakow\u0105, powtarzaj\u0105cy si\u0119 co obrót ko\u0142a \u015blimacznicy.',
      defaultValue: '15.2 (HA) / 12.8 (Dec)',
      type: 'float',
      range: '0.0 – 100.0',
    },
    cyclic_error_period: {
      description: 'Okres b\u0142\u0119du cyklicznego w stopniach (°). Zazwyczaj 360° (jeden pe\u0142ny obrót ko\u0142a \u015blimacznicy).',
      defaultValue: '360.0',
      type: 'float',
      range: '0.1 – 3600.0',
    },
    cyclic_harmonics: {
      description: 'Wspó\u0142czynniki harmonicznych b\u0142\u0119du cyklicznego jako lista oddzielona przecinkami. Pozwala na modelowanie z\u0142o\u017conych przebiegów okresowych.',
      defaultValue: 'zale\u017cne od osi',
      type: 'string (lista)',
      range: 'dowolne warto\u015bci liczbowe',
    },
    backlash: {
      description: 'Luz mechaniczny w sekundach k\u0105towych (arcsec). Kompensowany przez system sterowania podczas zmiany kierunku ruchu.',
      defaultValue: '8.5 (HA) / 6.3 (Dec)',
      type: 'float',
      range: '0.0 – 1000.0',
    },
    backlash_temp_coeff: {
      description: 'Wspó\u0142czynnik temperaturowy luzu w sekundach k\u0105towych na stopie\u0144 Celsjusza (arcsec/°C). Okre\u015bla zmian\u0119 luzu wraz z temperatur\u0105.',
      defaultValue: '0.02 (HA) / 0.015 (Dec)',
      type: 'float',
      range: '0.0 – 10.0',
    },
    axis_stiffness: {
      description: 'Sztywno\u015b\u0107 osi w sekundach k\u0105towych na niutonometr (arcsec/Nm). Okre\u015bla odkszta\u0142cenie osi pod wp\u0142ywem obci\u0105\u017cenia.',
      defaultValue: '0.5 (HA) / 0.6 (Dec)',
      type: 'float',
      range: '0.0 – 100.0',
    },
    torsional_compliance: {
      description: 'Podatno\u015b\u0107 skr\u0119tna w radianach na niutonometr (rad/Nm). Odwrotno\u015b\u0107 sztywno\u015bci skr\u0119tnej – okre\u015bla zdolno\u015b\u0107 osi do odkszta\u0142ce\u0144 skr\u0119tnych.',
      defaultValue: '1.0×10⁻⁶ (HA) / 1.2×10⁻⁶ (Dec)',
      type: 'float',
      range: '0.0 – (bez ogranicze\u0144)',
    },
    expansion_coeff: {
      description: 'Wspó\u0142czynnik rozszerzalno\u015bci cieplnej materia\u0142ów osi w 1/°C. Okre\u015bla zmian\u0119 wymiarów geometrycznych pod wp\u0142ywem temperatury.',
      defaultValue: '11.0×10⁻⁶ (1/°C)',
      type: 'float',
      range: '0.0 – (bez ogranicze\u0144)',
    },
    temp_gear_error_coeff: {
      description: 'Wspó\u0142czynnik b\u0142\u0119du przek\u0142adni zale\u017cnego od temperatury w arcsec/°C. Okre\u015bla wp\u0142yw temperatury na dok\u0142adno\u015b\u0107 przek\u0142adni.',
      defaultValue: '0.05 (HA) / 0.04 (Dec)',
      type: 'float',
      range: '0.0 – 10.0',
    },
    calibration_temp: {
      description: 'Temperatura kalibracji osi w stopniach Celsjusza (°C). Temperatura, w której przeprowadzono kalibracj\u0119 mechaniczn\u0105.',
      defaultValue: '20.0',
      type: 'float',
      range: '-50.0 do 60.0',
    },
  // ── Servo Init ──
  servo_init_enabled: {
    description: 'W\u0142\u0105cza wysy\u0142anie niestandardowej sekwencji SDO do serwonap\u0119dów podczas inicjalizacji. Sekwencja zawiera wpisy konfiguracyjne producenta (np. rozdzielczo\u015b\u0107 mikrokroków 0x2005, ustawienia enkodera 0x2006) wysy\u0142ane po uruchomieniu magistrali CANopen.',
    defaultValue: 'false (wy\u0142\u0105czone)',
    type: 'boolean',
    range: 'true / false',
  },
  servo_init_sequence: {
    description: 'Tablica JSON wpisów SDO do wys\u0142ania. Ka\u017cdy wpis: {"axis": 0|1, "index": "0x2005", "subindex": 0, "value": 16, "description": "HA axis microstep"}. O\u015b 0 = HA, O\u015b 1 = Dec. Indeksy per instrukcja serwonap\u0119du (sekcja 3.2.1).',
    defaultValue: '[] (pusta tablica)',
    type: 'string (JSON array)',
    range: 'JSON array of SDO entry objects',
  },
  };

  // ─── Internal State ───────────────────────────────────────────────────────

  let currentConfig = null;
  let isDirty = {};
  let gamepadPollTimer = null;
  let gamepadControlActive = false;
  const GAMEPAD_POLL_MS = 2000;

  async function fetchAndRenderGamepadState() {
    const panel = $('#gamepad-state-panel');
    if (!panel) return;
    try {
      const state = await Api.getGamepadState();
      renderGamepadState(state, panel);
    } catch (err) {
      panel.innerHTML = '<div class="gamepad-state-error">' + I18n.t('cfg.gamepad.poll_error', 'Gamepad state unavailable') + ': ' + Utils.escapeHtml(err.message) + '</div>';
    }
  }

  function renderGamepadState(s, panel) {
    const connIcon = s.connected ? '\u{1F7E2}' : '\u{1F534}';
    const connLabel = s.connected ? I18n.t('cfg.gamepad.connected', 'Connected') : I18n.t('cfg.gamepad.disconnected', 'Disconnected');
    const deviceName = s.device_name || I18n.t('cfg.gamepad.no_device', 'No device detected');

    const axisBar = (value, label) => {
      const clamped = Math.max(-1, Math.min(1, value));
      const pct = Math.abs(clamped) * 100;
      const dir = clamped < 0 ? 'neg' : 'pos';
      const barLeft = clamped < 0 ? 50 - pct / 2 : 50;
      const barWidth = pct / 2;
      return '<div class="gamepad-axis-row"><span class="gamepad-axis-label">' + Utils.escapeHtml(label) + '</span><span class="gamepad-axis-bar-track"><span class="gamepad-axis-bar-fill ' + dir + '" style="left:' + barLeft + '%;width:' + barWidth + '%"></span></span><span class="gamepad-axis-value">' + clamped.toFixed(3) + '</span></div>';
    };

    const btnIndicator = (pressed, label) => {
      const cls = pressed ? 'gamepad-btn-active' : 'gamepad-btn-inactive';
      const icon = pressed ? '\u25CF' : '\u25CB';
      return '<span class="gamepad-btn-indicator ' + cls + '" title="' + Utils.escapeHtml(label) + '">' + icon + ' ' + Utils.escapeHtml(label) + '</span>';
    };

    const povLabel = s.pov_hat >= 0 ? s.pov_hat.toFixed(0) + '\u00B0' : I18n.t('cfg.gamepad.neutral', 'Neutral');

    var ctrlHtml = '<div class="gamepad-ctrl-row">'
      + '<button id="gamepad-ctrl-btn" class="cfg-btn" style="font-size:0.8rem;padding:4px 12px;">'
      + (gamepadControlActive ? I18n.t('cfg.gamepad.stop_ctrl', 'Stop Manual Control') : I18n.t('cfg.gamepad.start_ctrl', 'Start Manual Control'))
      + '</button>'
      + '</div>';

    // Navigation mode selector
    const currentMode = s.gamepad_mode !== undefined ? s.gamepad_mode : 0;
    const bootstrapCal = s.bootstrap_calibrated || false;
    const tpointCal = s.tpoint_calibrated || false;
    const modeNames = [
      I18n.t('cfg.gamepad.mode_raw', 'Raw Axes'),
      I18n.t('cfg.gamepad.mode_celestial', 'Celestial (RA/Dec)'),
      I18n.t('cfg.gamepad.mode_alt_az', 'Alt-Az'),
      I18n.t('cfg.gamepad.mode_precision', 'Precision (RA/Dec, slow)')
    ];
    var modeSelectHtml = '<div class="gamepad-mode-row"><span class="gamepad-mode-label">' + I18n.t('cfg.gamepad.mode_label', 'Nav Mode') + ':</span>'
      + '<select id="gamepad-mode-select" class="gamepad-mode-select">';
    for (var mi = 0; mi < 4; mi++) {
      var disabled = false;
      if (mi > 0 && !bootstrapCal) disabled = true;
      if (mi === 3 && !tpointCal) disabled = true;
      var selected = (mi === currentMode) ? ' selected' : '';
      modeSelectHtml += '<option value="' + mi + '"' + (disabled ? ' disabled' : '') + selected + '>' + Utils.escapeHtml(modeNames[mi]) + (disabled ? ' (' + I18n.t('cfg.gamepad.needs_cal', 'needs calibration') + ')' : '') + '</option>';
    }
    modeSelectHtml += '</select></div>';

    panel.innerHTML = '<div class="gamepad-state-header"><span class="gamepad-connection-status">' + connIcon + ' ' + connLabel + '</span><span class="gamepad-device-name">' + Utils.escapeHtml(deviceName) + '</span></div>'
      + ctrlHtml
      + modeSelectHtml
      + '<div class="gamepad-state-grid">'
      + '<div class="gamepad-state-section"><div class="gamepad-section-title">' + I18n.t('cfg.gamepad.axes', 'Analog Axes') + '</div>'
      + axisBar(s.axis_lx, 'Left X') + axisBar(s.axis_ly, 'Left Y') + axisBar(s.axis_rx, 'Right X') + axisBar(s.axis_ry, 'Right Y')
      + axisBar(s.axis_trigger_l, 'Trig. L') + axisBar(s.axis_trigger_r, 'Trig. R')
      + '<div class="gamepad-axis-row"><span class="gamepad-axis-label">' + I18n.t('cfg.gamepad.pov_hat', 'POV Hat') + '</span><span class="gamepad-axis-value" style="margin-left:auto">' + povLabel + '</span></div>'
      + '</div>'
      + '<div class="gamepad-state-section"><div class="gamepad-section-title">' + I18n.t('cfg.gamepad.buttons', 'Buttons') + '</div>'
      + '<div class="gamepad-buttons-grid">'
      + btnIndicator(s.button_stop, 'STOP') + btnIndicator(s.button_emergency_stop, 'E-STOP')
      + btnIndicator(s.button_park, 'PARK') + btnIndicator(s.button_home, 'HOME')
      + btnIndicator(s.button_speed_up, 'SPD+') + btnIndicator(s.button_speed_down, 'SPD\u2212')
      + btnIndicator(s.button_manual_toggle, 'MANUAL')
      + '</div>'
      + '<div class="gamepad-info-row"><span>' + I18n.t('cfg.gamepad.axes_count', 'Axes') + ': ' + (s.axis_count || 0) + '</span><span>' + I18n.t('cfg.gamepad.buttons_count', 'Buttons') + ': ' + (s.button_count || 0) + '</span><span>' + I18n.t('cfg.gamepad.speed', 'Speed') + ': ' + (s.max_velocity || 5.0).toFixed(1) + ' \u00B0/s</span></div>'
      + '</div></div>';

    // Attach click handler after DOM update
    var btn = document.getElementById('gamepad-ctrl-btn');
    if (btn) {
      btn.onclick = toggleGamepadControl;
    }

    // Attach mode select change handler
    var modeSelect = document.getElementById('gamepad-mode-select');
    if (modeSelect) {
      modeSelect.onchange = async function() {
        var newMode = parseInt(this.value, 10);
        try {
          await Api.setGamepadMode(newMode);
          console.log('Gamepad mode set to', newMode);
        } catch (err) {
          console.error('Failed to set gamepad mode:', err);
        }
      };
    }
  }

  function startGamepadPolling() {
    stopGamepadPolling();
    fetchAndRenderGamepadState();
    gamepadPollTimer = setInterval(fetchAndRenderGamepadState, GAMEPAD_POLL_MS);
  }

  function stopGamepadPolling() {
    if (gamepadPollTimer) {
      clearInterval(gamepadPollTimer);
      gamepadPollTimer = null;
    }
  }

  async function toggleGamepadControl() {
    var btn = document.getElementById('gamepad-ctrl-btn');
    if (!btn) return;

    if (gamepadControlActive) {
      // Stop
      btn.disabled = true;
      btn.textContent = '...';
      try {
        await Api.stopGamepad();
        gamepadControlActive = false;
      } catch (err) {
        console.error('Failed to stop gamepad control:', err);
      }
      btn.disabled = false;
    } else {
      // Start
      btn.disabled = true;
      btn.textContent = '...';
      try {
        await Api.startGamepad();
        gamepadControlActive = true;
      } catch (err) {
        console.error('Failed to start gamepad control:', err);
      }
      btn.disabled = false;
    }
    // Re-render to update button label
    fetchAndRenderGamepadState();
  }

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
      // Also load HAL config and merge it into the flat config object
      try {
        const halData = await Api.getHALConfig();
        Object.assign(configData, halData);
      } catch (halErr) {
        console.warn('[Settings] Failed to load HAL config, using defaults:', halErr.message);
      }
      currentConfig = configData;
      renderConfig(configData, configEl);

      // Enhance angle inputs with DMS/HMS formatting
      Utils.enhanceAllAngleInputs(configEl);

      // Start live gamepad state polling (panel injected by renderConfig)
      startGamepadPolling();

      // Show the Reset All and Restart buttons
      const resetAllBtn = $('#btn-reset-all-config');
      if (resetAllBtn) resetAllBtn.style.display = 'inline-flex';
      const restartSoftBtn = $('#btn-restart-soft');
      if (restartSoftBtn) restartSoftBtn.style.display = 'inline-flex';
      const restartHardBtn = $('#btn-restart-hard');
      if (restartHardBtn) restartHardBtn.style.display = 'inline-flex';
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
    const groupHelpAttr = GROUP_HELP[group.id] ? ` data-help-group="${group.id}"` : '';
    const groupLabel = I18n.t('cfg.group.' + group.id, group.label);
    summary.innerHTML = `<span class="disclosure-arrow">&#x25B6;</span><span class="config-summary-content">${icon}<span>${groupLabel}</span><button class="help-icon"${groupHelpAttr} aria-label="Poka\u017c opis grupy: ${groupLabel}">i</button></span>`;

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
        const subHelpAttr = GROUP_HELP[sub.id] ? ` data-help-group="${sub.id}"` : '';
        const subLabel = I18n.t('cfg.group.' + sub.id, sub.label);
        subSummary.innerHTML = `<span class="disclosure-arrow">&#x25B6;</span><span class="config-summary-content">${subIcon}<span>${subLabel}</span><button class="help-icon"${subHelpAttr} aria-label="Poka\u017c opis grupy: ${subLabel}">i</button></span>`;

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
          <button class="btn btn-primary btn-sm btn-save-group" data-group="${group.id}" data-sub="${sub.id}">${I18n.t('cfg.btn.save', 'Save')}</button>
          <button class="btn btn-sm btn-reset-group" data-group="${group.id}" data-sub="${sub.id}">${I18n.t('cfg.btn.restore_defaults', 'Restore Defaults')}</button>
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
        <button class="btn btn-primary btn-sm btn-save-group" data-group="${group.id}">${I18n.t('cfg.btn.save', 'Save')}</button>
        <button class="btn btn-sm btn-reset-group" data-group="${group.id}">${I18n.t('cfg.btn.restore_defaults', 'Restore Defaults')}</button>
        <span class="config-group-status" id="cfg-status-${group.id}"></span>
      `;
      body.appendChild(actions);
    }

    // Inject live gamepad state panel for the HAL Gamepad group
    if (group.id === 'hal_gamepad') {
      const gamepadPanel = document.createElement('div');
      gamepadPanel.id = 'gamepad-state-panel';
      gamepadPanel.className = 'gamepad-state-panel';
      gamepadPanel.innerHTML = '<div class="status-placeholder">' + I18n.t('cfg.gamepad.loading', 'Loading gamepad state...') + '</div>';
      body.appendChild(gamepadPanel);
    }

    return details;
  }

  /**
   * Create a form field element based on its type.
   */
  function createFieldElement(field, value, groupId) {
    const wrapper = document.createElement('div');
    wrapper.className = 'config-field';

    const fieldLabel = I18n.t('cfg.field.' + field.key, field.label);

    // Quaternion type
    if (field.type === 'quaternion') {
      wrapper.innerHTML = `
        <label class="config-field-label">${fieldLabel}
          <button class="help-icon" data-help-key="${field.key}" aria-label="Pokaż opis parametru: ${fieldLabel}">i</button>
        </label>
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
          ${fieldLabel}
          <button class="help-icon" data-help-key="${field.key}" aria-label="Pokaż opis parametru: ${fieldLabel}">i</button>
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
        <label class="config-field-label">${fieldLabel}
          <button class="help-icon" data-help-key="${field.key}" aria-label="Pokaż opis parametru: ${fieldLabel}">i</button>
        </label>
        <select class="form-input form-select config-input" data-group="${groupId}" data-key="${field.key}" data-type="select">
          ${options}
        </select>
      `;
      return wrapper;
    }

    // Number (or angle with DMS/HMS support)
    if (field.type === 'number') {
      if (field.angleType) {
        // Angle field: render as text with data-angle-type for DMS/HMS formatting
        wrapper.innerHTML = `
          <label class="config-field-label">${fieldLabel}
            <button class="help-icon" data-help-key="${field.key}" aria-label="Pokaż opis parametru: ${fieldLabel}">i</button>
          </label>
          <input type="text" class="form-input config-input" data-group="${groupId}" data-key="${field.key}" data-type="number"
            data-angle-type="${escapeHtml(field.angleType)}" value="${value !== undefined ? escapeHtml(String(value)) : ''}" />
        `;
      } else {
        const min = field.min !== undefined ? `min="${field.min}"` : '';
        const max = field.max !== undefined ? `max="${field.max}"` : '';
        const step = field.step !== undefined ? `step="${field.step}"` : 'step="any"';
        wrapper.innerHTML = `
          <label class="config-field-label">${fieldLabel}
            <button class="help-icon" data-help-key="${field.key}" aria-label="Pokaż opis parametru: ${fieldLabel}">i</button>
          </label>
          <input type="number" class="form-input config-input" data-group="${groupId}" data-key="${field.key}" data-type="number"
            ${min} ${max} ${step} value="${value !== undefined ? escapeHtml(String(value)) : ''}" />
        `;
      }
      return wrapper;
    }

    // Textarea (multiline)
    if (field.type === 'textarea') {
      wrapper.innerHTML = `
        <label class="config-field-label">${fieldLabel}
          <button class="help-icon" data-help-key="${field.key}" aria-label="Pokaż opis parametru: ${fieldLabel}">i</button>
        </label>
        <textarea class="form-input config-input" data-group="${groupId}" data-key="${field.key}" data-type="textarea"
          rows="5" style="font-family:monospace; font-size:0.75rem; min-height:60px;"
        >${escapeHtml(String(value || ''))}</textarea>
      `;
      return wrapper;
    }

    // Text (default)
    wrapper.innerHTML = `
      <label class="config-field-label">${fieldLabel}
        <button class="help-icon" data-help-key="${field.key}" aria-label="Pokaż opis parametru: ${fieldLabel}">i</button>
      </label>
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
        // Use enhanced angle getter if available
        if (input.getAngleDecimal) {
          value = input.getAngleDecimal();
          if (!isFinite(value)) value = 0;
        } else {
          value = parseFloat(input.value);
          if (isNaN(value)) value = 0;
        }
      } else if (type === 'select') {
        // Select values: try to parse as number if purely numeric
        const raw = input.value;
        if (/^-?\d+$/.test(raw)) {
          value = parseInt(raw, 10);
        } else if (/^-?\d+\.?\d*$/.test(raw)) {
          value = parseFloat(raw);
        } else {
          value = raw;
        }
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
    if (statusEl) statusEl.textContent = I18n.t('cfg.status.saving');

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
        // Route HAL groups to separate endpoint
        if (groupId.startsWith('hal')) {
          await Api.setHALConfig(data);
        } else {
          await Api.updateConfig(data);
        }
      }

      if (statusEl) {
        const needsRestart = group.restartRequired !== undefined ? group.restartRequired : true;
        if (needsRestart) {
          statusEl.innerHTML = '✅ Saved <span style="color:var(--color-warning,orange);font-weight:bold;" title="' + I18n.t('cfg.status.restart_hint', 'This change requires a controller restart to take effect') + '">⚠ Restart required</span>';
        } else {
          statusEl.textContent = '✅ Saved';
        }
        statusEl.style.color = 'var(--color-success)';
        setTimeout(() => { if (statusEl) statusEl.textContent = ''; }, 6000);
      }

      // Reload config to reflect changes
      const fresh = await Api.getConfig();
      // Also reload HAL data
      try {
        const halData = await Api.getHALConfig();
        Object.assign(fresh, halData);
      } catch (e) { /* ignore */ }
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
    if (statusEl) statusEl.textContent = I18n.t('cfg.status.resetting');

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
        if (statusEl) statusEl.textContent = I18n.t('cfg.status.reloading');
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

  /**
   * Execute a controller restart via API.
   * @param {boolean} hard - If true, perform hard restart (discard calibrations)
   */
  async function handleRestart(hard) {
    const label = hard ? 'HARD RESTART' : 'SOFT RESTART';
    console.log(`[Settings] ${label} initiated`);
    App.showToast(`${label} in progress...`, 'info');

    // Disable both buttons during restart
    const softBtn = $('#btn-restart-soft');
    const hardBtn = $('#btn-restart-hard');
    if (softBtn) softBtn.disabled = true;
    if (hardBtn) hardBtn.disabled = true;

    try {
      if (hard) {
        await Api.hardRestartController();
      } else {
        await Api.restartController();
      }
      App.showToast(`${label} completed successfully`, 'success');

      // Reload config after restart
      const fresh = await Api.getConfig();
      currentConfig = fresh;
      const container = $('#config-content');
      if (container) renderConfig(fresh, container);
    } catch (err) {
      App.showToast(`${label} failed: ${err.message}`, 'error');
    } finally {
      if (softBtn) softBtn.disabled = false;
      if (hardBtn) hardBtn.disabled = false;
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

      const restartSoftBtn = e.target.closest('#btn-restart-soft');
      if (restartSoftBtn) {
        if (confirm('Soft restart the controller?\\n\\nThis will reload the configuration from disk and reinitialize all subsystems.\\nBootstrap and TPOINT calibrations will be PRESERVED.\\n\\nThe mount MUST be idle — tracking and slewing will be stopped.')) {
          handleRestart(false);
        }
        return;
      }

      const restartHardBtn = e.target.closest('#btn-restart-hard');
      if (restartHardBtn) {
        if (confirm('⚠ HARD RESTART the controller?\\n\\nThis will reload the configuration from disk and reinitialize all subsystems.\\nALL calibrations (bootstrap, TPOINT) will be PERMANENTLY DISCARDED.\\n\\nThe mount MUST be idle — tracking and slewing will be stopped.\\n\\nThis cannot be undone.')) {
          if (confirm('FINAL CONFIRMATION: Really discard ALL calibration data?')) {
            handleRestart(true);
          }
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

      const helpIcon = e.target.closest('.help-icon');
      if (helpIcon) {
        const key = helpIcon.dataset.helpKey;
        const group = helpIcon.dataset.helpGroup;
        const subgroup = helpIcon.dataset.helpSubgroup;
        if (key) {
          showHelpPopup(key, 'param', helpIcon);
        } else if (group) {
          showHelpPopup(group, 'group', helpIcon);
        } else if (subgroup) {
          showHelpPopup(subgroup, 'group', helpIcon);
        }
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

  // ─── Help Tooltip ────────────────────────────────────────────────────────

  /**
   * Show a help tooltip ("dymek") near the clicked help icon.
   * @param {string} key - The parameter key or group/subgroup ID
   * @param {'param'|'group'} type - Whether this is a parameter or group help
   * @param {HTMLElement} anchor - The clicked help icon element
   */
  function showHelpPopup(key, type, anchor) {
    // Remove any existing tooltip first
    const existing = document.querySelector('.help-tooltip');
    if (existing) existing.remove();

    const isParam = type === 'param';
    const helpData = isParam ? PARAM_HELP[key] : GROUP_HELP[key];
    if (!helpData) {
      console.warn('HelpPopup: no data for', key, type);
      return;
    }

    // Build content
    let bodyHtml;
    if (isParam) {
      const desc = I18n.t('cfg.param.' + key + '.desc', helpData.description || 'Brak opisu.');
      const defVal = I18n.t('cfg.param.' + key + '.default', helpData.defaultValue !== undefined ? String(helpData.defaultValue) : '—');
      const typeVal = I18n.t('cfg.param.' + key + '.type', helpData.type || '—');
      const rangeVal = I18n.t('cfg.param.' + key + '.range', helpData.range || '—');
      const labelType = I18n.t('cfg.help.label_type', 'Typ');
      const labelRange = I18n.t('cfg.help.label_range', 'Zakres');
      const labelDefault = I18n.t('cfg.help.label_default', 'Domyślnie');
      const labelClose = I18n.t('cfg.help.label_close', 'Zamknij');

      bodyHtml = `
        <div class="help-tip-header">
          <span class="help-tip-title">${escapeHtml(key)}</span>
          <button class="help-tip-close" aria-label="${labelClose}">&times;</button>
        </div>
        <div class="help-tip-body">
          <p class="help-tip-desc">${escapeHtml(desc)}</p>
          <table class="help-tip-table">
            <tr><th>${labelType}</th><td>${escapeHtml(typeVal)}</td></tr>
            <tr><th>${labelRange}</th><td>${escapeHtml(rangeVal)}</td></tr>
            <tr><th>${labelDefault}</th><td><code>${escapeHtml(defVal)}</code></td></tr>
          </table>
        </div>
      `;
    } else {
      const desc = I18n.t('cfg.help.' + key, helpData || '');
      const labelClose = I18n.t('cfg.help.label_close', 'Zamknij');
      bodyHtml = `
        <div class="help-tip-header">
          <span class="help-tip-title">${escapeHtml(key)}</span>
          <button class="help-tip-close" aria-label="${labelClose}">&times;</button>
        </div>
        <div class="help-tip-body">
          <p class="help-tip-desc">${escapeHtml(desc)}</p>
        </div>
      `;
    }

    // Create tooltip element
    const tooltip = document.createElement('div');
    tooltip.className = 'help-tooltip';
    tooltip.innerHTML = bodyHtml;
    document.body.appendChild(tooltip);

    // Position tooltip near the anchor
    positionTooltip(tooltip, anchor);

    // Close handlers
    const closeTooltip = () => {
      tooltip.remove();
    };

    const closeBtn = tooltip.querySelector('.help-tip-close');
    if (closeBtn) closeBtn.addEventListener('click', closeTooltip);

    // Close on Escape
    const escHandler = function(e) {
      if (e.key === 'Escape') {
        closeTooltip();
        document.removeEventListener('keydown', escHandler);
      }
    };
    document.addEventListener('keydown', escHandler);

    // Close on click outside tooltip (delayed to avoid immediate trigger from the help-icon click)
    setTimeout(() => {
      document.addEventListener('click', function outsideHandler(e) {
        if (!tooltip.contains(e.target)) {
          closeTooltip();
          document.removeEventListener('click', outsideHandler);
        }
      });
    }, 0);
  }

  /**
   * Position a tooltip element near an anchor, with smart viewport-aware placement.
   * @param {HTMLElement} tooltip
   * @param {HTMLElement} anchor
   */
  function positionTooltip(tooltip, anchor) {
    const anchorRect = anchor.getBoundingClientRect();
    const tipWidth = 320;
    const tipMaxHeight = 360;
    const gap = 6; // gap between anchor and tooltip

    // Determine placement: prefer below, then above
    const spaceBelow = window.innerHeight - anchorRect.bottom;
    const spaceAbove = anchorRect.top;
    const placeBelow = spaceBelow >= tipMaxHeight + gap || spaceBelow >= spaceAbove;

    let top, left;

    if (placeBelow) {
      top = anchorRect.bottom + gap;
    } else {
      top = anchorRect.top - gap - tipMaxHeight;
      // cap so it's not above viewport
      if (top < 4) top = 4;
    }

    // Horizontal: center on anchor, but keep within viewport
    left = anchorRect.left + anchorRect.width / 2 - tipWidth / 2;
    if (left < 8) left = 8;
    if (left + tipWidth > window.innerWidth - 8) {
      left = window.innerWidth - tipWidth - 8;
    }

    // Store which side the arrow should be on
    tooltip.dataset.placement = placeBelow ? 'bottom' : 'top';

    // Apply position
    tooltip.style.position = 'fixed';
    tooltip.style.left = left + 'px';
    tooltip.style.top = top + 'px';
    tooltip.style.width = tipWidth + 'px';
    tooltip.style.maxHeight = tipMaxHeight + 'px';

    // Adjust arrow horizontal position to point at the anchor center
    const anchorCenterX = anchorRect.left + anchorRect.width / 2;
    const arrowOffset = anchorCenterX - left;
    tooltip.style.setProperty('--tip-arrow-offset', arrowOffset + 'px');
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
      if (statusEl) statusEl.textContent = I18n.t('settings.addr_load_failed', { message: err.message });
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
      if (statusEl) statusEl.textContent = I18n.t('settings.ctrl_host_empty');
      return;
    }
    if (isNaN(controllerPort) || controllerPort < 1 || controllerPort > 65535) {
      if (statusEl) statusEl.textContent = I18n.t('settings.ctrl_port_range');
      return;
    }
    if (!databaseHost) {
      if (statusEl) statusEl.textContent = I18n.t('settings.db_host_empty');
      return;
    }
    if (isNaN(databasePort) || databasePort < 1 || databasePort > 65535) {
      if (statusEl) statusEl.textContent = I18n.t('settings.db_port_range');
      return;
    }

    const saveBtn = $('#btn-save-addresses');
    if (saveBtn) {
      saveBtn.disabled = true;
      saveBtn.textContent = I18n.t('cfg.status.saving');
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
        saveBtn.textContent = I18n.t('settings.save_reconnect');
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

  // ─── Help Content ─────────────────────────────────────────────────────────

  function buildSettingsHelpContent() {
    const container = $('#settings-help-content');
    if (!container) return;

    const t = I18n.t.bind(I18n);

    const steps = [
      { num: '1', titleKey: 'settings.help_step1_title', open: true,
        bodyHtml: '<ol><li>' + t('settings.help_step1_li1') + '</li><li>' + t('settings.help_step1_li2') + '</li><li>' + t('settings.help_step1_li3') + '</li></ol>' },
      { num: '2', titleKey: 'settings.help_step2_title', open: false,
        bodyHtml: '<ol><li>' + t('settings.help_step2_li1') + '</li><li>' + t('settings.help_step2_li2') + '</li><li>' + t('settings.help_step2_li3') + '</li></ol>' },
      { num: '3', titleKey: 'settings.help_step3_title', open: false,
        bodyHtml: '<ol><li>' + t('settings.help_step3_li1') + '</li><li>' + t('settings.help_step3_li2') + '</li><li>' + t('settings.help_step3_li3') + '</li></ol>' },
      { num: '4', titleKey: 'settings.help_step4_title', open: false,
        bodyHtml: '<ul><li>' + t('settings.help_step4_li1') + '</li><li>' + t('settings.help_step4_li2') + '</li><li>' + t('settings.help_step4_li3') + '</li></ul>' }
    ];

    let html = '<p><strong>' + t('settings.help_purpose_label') + '</strong> ' + t('settings.help_purpose_text') + '</p>';

    steps.forEach(function(step) {
      html += '<details class="calibration-help-step"' + (step.open ? ' open' : '') + '>'
        + '<summary class="calibration-help-step-summary">'
        + '<span class="calibration-help-step-number">' + step.num + '</span>'
        + t(step.titleKey) + '</summary>'
        + '<div class="calibration-help-step-body">' + step.bodyHtml + '</div>'
        + '</details>';
    });

    container.innerHTML = html;
  }

  // ─── Initialization ───────────────────────────────────────────────────────

  function init() {
    buildSettingsHelpContent();
    document.addEventListener('i18n:applied', buildSettingsHelpContent);
    bindHelpToggle('card-settings-help');
    initEventHandlers();
    initAddressForm();
  }

  function bindHelpToggle(cardId) {
    const card = $('#' + cardId);
    if (!card) return;
    const toggleBtn = card.querySelector('.card-toggle-btn');
    const header = card.querySelector('.card-header');
    const doToggle = function() {
      const collapsed = card.classList.toggle('card-collapsed');
      if (toggleBtn) toggleBtn.textContent = collapsed ? '+' : '\u2212';
    };
    if (toggleBtn) { toggleBtn.addEventListener('click', function(e) { e.stopPropagation(); doToggle(); }); }
    if (header) { header.addEventListener('click', function(e) { if (e.target.closest('button, input, select, a, label')) return; doToggle(); }); }
  }

  // Run init
  init();

  // Public API
  return { loadConfig, loadAddresses, initAddressForm, stopGamepadPolling };
})();
