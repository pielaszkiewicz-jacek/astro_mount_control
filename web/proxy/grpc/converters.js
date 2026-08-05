/**
 * Astro Mount Controller - gRPC ↔ JSON Converters
 *
 * Converts protobuf message formats to JSON-friendly responses.
 */
'use strict';

/**
 * Format the mount controller state for JSON response.
 */
function formatState(state) {
  // Proto ControllerState fields → UI-compatible JSON.
  // See proto/mount_controller.proto message ControllerState for source fields.
  const pos = state.current_position || {};
  const tracked = state.tracked_object || {};
  const coords = tracked.coordinates || {};

  return {
    // --- Mount state ---
    status: state.status || 'UNKNOWN',

    // --- Axis positions (servo / motor shaft) ---
    position: {
      axis1: pos.axis1 || 0,
      axis2: pos.axis2 || 0,
    },

    // --- Telescope positions (after gear ratio) ---
    telescope: {
      axis1: state.telescope_axis1 || 0,
      axis2: state.telescope_axis2 || 0,
    },

    // --- Tracking rates (already in arcsec/s from proto) ---
    tracking_rate_ra: state.tracking_rate_ra || 0,
    tracking_rate_dec: state.tracking_rate_dec || 0,

    // --- Actual motor axis velocities (from CANopen hardware, deg/s) ---
    actual_rate_axis1: state.actual_rate_axis1 || 0,
    actual_rate_axis2: state.actual_rate_axis2 || 0,

    // --- Encoders / guider ---
    encoders_enabled: state.encoders_enabled || false,
    guider_active: state.guider_active || false,

    // --- Meridian / pier side ---
    pier_side: state.pier_side || 1,
    meridian_flipped: state.meridian_flipped || false,
    time_to_meridian: state.time_to_meridian || 0,

    // --- Environment ---
    temperature: state.temperature || 0,
    pressure: state.pressure || 0,
    humidity: state.humidity || 0,

    // --- Tracked object ---
    tracked_object: tracked.coordinates ? {
      name: coords.name || '',
      ra: coords.ra || 0,
      dec: coords.dec || 0,
      tracking_error_ra: tracked.tracking_error_ra || 0,
      tracking_error_dec: tracked.tracking_error_dec || 0,
    } : null,

    // --- Performance ---
    tracking_performance: state.tracking_performance || 0,
    pointing_error: state.pointing_error || 0,

    // --- Legacy fields (kept for backward compatibility) ---
    axis1: {
      position: pos.axis1 || 0,
      target: 0,
      rate: state.tracking_rate_ra || 0,
      actualRate: state.actual_rate_axis1 || 0,
    },
    axis2: {
      position: pos.axis2 || 0,
      target: 0,
      rate: state.tracking_rate_dec || 0,
      actualRate: state.actual_rate_axis2 || 0,
    },
    telescope_legacy: {
      axis1Position: state.telescope_axis1 || 0,
      axis2Position: state.telescope_axis2 || 0,
    },
    tracking: {
      errorRA: tracked.tracking_error_ra || 0,
      errorDec: tracked.tracking_error_dec || 0,
      ra: coords.ra || 0,
      dec: coords.dec || 0,
    },
    encodersActive: state.encoders_enabled || false,
    guiderActive: state.guider_active || false,
    tpointCalibrated: (state.tpoint_params && state.tpoint_params.calibrated) || false,
    meridianFlip: {
      pending: false,
      inProgress: state.meridian_flipped || false,
      pierSide: state.pier_side || 1,
      timeToMeridian: state.time_to_meridian || 0,
    },
    softLimits: {
      warningActive: false,
      decelerationActive: false,
      distanceAxis1: 0,
      distanceAxis2: 0,
      warningMessage: '',
    },
    bootstrap: {
      calibrated: (state.bootstrap_status && state.bootstrap_status.calibrated) || false,
      mode: (state.bootstrap_status && state.bootstrap_status.mode) || 0,
      measurementCount: (state.bootstrap_status && state.bootstrap_status.measurement_count) || 0,
    },
    timestamp: (state.state_time && state.state_time.seconds)
      ? new Date(state.state_time.seconds * 1000).toISOString()
      : new Date().toISOString(),
    error: state.error_message || '',
  };
}

/**
 * Format calibration status response.
 */
function formatCalibrationStatus(result) {
  return {
    success: result.success || false,
    rmsRA: result.rms_ra_arcsec || 0,
    rmsDec: result.rms_dec_arcsec || 0,
    measurements: result.measurement_count || 0,
    message: result.message || '',
  };
}

/**
 * Format TPOINT calibration status response.
 */
function formatTPointStatus(result) {
  return {
    success: result.success || false,
    measurementCount: result.measurement_count || 0,
    rmsArcsec: result.rms_residual_arcsec || 0,
    maxResidualArcsec: result.max_residual_arcsec || 0,
    chiSquared: result.chi_squared || 0,
    enabledTerms: result.enabled_terms || 0,
  };
}

/**
 * Error response helper.
 */
function errorResponse(res, statusCode, message, details = '') {
  return res.status(statusCode).json({
    error: message,
    details: details || undefined,
  });
}

module.exports = {
  formatState,
  formatCalibrationStatus,
  formatTPointStatus,
  errorResponse,
};
