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
  return {
    state: state.state || 'UNKNOWN',
    axis1: {
      position: state.axis1_position || 0,
      target: state.axis1_target || 0,
      rate: state.axis1_rate || 0,
      actualRate: state.actual_axis1_rate || 0,
    },
    axis2: {
      position: state.axis2_position || 0,
      target: state.axis2_target || 0,
      rate: state.axis2_rate || 0,
      actualRate: state.actual_axis2_rate || 0,
    },
    telescope: {
      axis1Position: state.telescope_axis1_position || 0,
      axis2Position: state.telescope_axis2_position || 0,
    },
    tracking: {
      errorRA: state.tracking_error_ra || 0,
      errorDec: state.tracking_error_dec || 0,
      ra: state.tracking_ra || 0,
      dec: state.tracking_dec || 0,
    },
    encodersActive: state.encoders_active || false,
    guiderActive: state.guider_active || false,
    tpointCalibrated: state.tpoint_calibrated || false,
    meridianFlip: {
      pending: state.meridian_flip_pending || false,
      inProgress: state.meridian_flip_in_progress || false,
      pierSide: state.pier_side || 1,
      timeToMeridian: state.time_to_meridian || 0,
    },
    softLimits: {
      warningActive: state.soft_limit_warning_active || false,
      decelerationActive: state.soft_limit_deceleration_active || false,
      distanceAxis1: state.soft_limit_distance_axis1 || 0,
      distanceAxis2: state.soft_limit_distance_axis2 || 0,
      warningMessage: state.soft_limit_warning_message || '',
    },
    bootstrap: {
      calibrated: state.bootstrap_calibrated || false,
      mode: state.bootstrap_mode || 0,
      measurementCount: state.bootstrap_measurement_count || 0,
    },
    timestamp: state.timestamp || new Date().toISOString(),
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
