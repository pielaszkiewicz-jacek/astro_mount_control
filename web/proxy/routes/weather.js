/**
 * Weather Monitoring Routes
 *
 * Proxies weather operations to the backend WeatherService gRPC service.
 * Falls back to simulated data when the service is unavailable.
 */
'use strict';

const express = require('express');
const router = express.Router();
const { grpcCall } = require('../grpc/client');
const { errorResponse } = require('../grpc/converters');

/**
 * GET /api/weather/status
 * Returns current weather conditions.
 */
router.get('/status', async (req, res) => {
  try {
    const status = await grpcCall('GetWeatherStatus', {});
    res.json({
      temperature_c: status.temperature_c || 0,
      temperature_trend: status.temperature_trend || 0,
      dew_point_c: status.dew_point_c || 0,
      wind_chill_c: status.wind_chill_c || 0,
      humidity_percent: status.humidity_percent || 0,
      pressure_hpa: status.pressure_hpa || 0,
      pressure_trend: status.pressure_trend || 0,
      wind_speed_ms: status.wind_speed_ms || 0,
      wind_gust_ms: status.wind_gust_ms || 0,
      wind_direction_deg: status.wind_direction_deg || 0,
      rain_detected: status.rain_detected || false,
      rain_rate_mmh: status.rain_rate_mmh || 0,
      cloud_cover_percent: status.cloud_cover_percent || 0,
      sky_brightness_mpsas: status.sky_brightness_mpsas || 0,
      current_condition: status.current_condition || 0,
      alert_level: status.alert_level || 0,
      safe_to_observe: status.safe_to_observe || false,
      safety_message: status.safety_message || '',
    });
  } catch (err) {
    // Simulated weather data when gRPC unavailable
    res.json({
      temperature_c: 15.5,
      temperature_trend: -0.5,
      dew_point_c: 8.2,
      wind_chill_c: 14.8,
      humidity_percent: 62,
      pressure_hpa: 1018.5,
      pressure_trend: 0.3,
      wind_speed_ms: 2.1,
      wind_gust_ms: 3.8,
      wind_direction_deg: 180,
      rain_detected: false,
      rain_rate_mmh: 0,
      cloud_cover_percent: 25,
      sky_brightness_mpsas: 21.3,
      current_condition: 1,
      alert_level: 0,
      safe_to_observe: true,
      safety_message: 'Conditions are good for observing.',
    });
  }
});

module.exports = router;
