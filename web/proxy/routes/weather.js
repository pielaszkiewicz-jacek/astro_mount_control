/**
 * Weather Monitoring Routes
 *
 * Proxies weather operations to the WeatherService gRPC service
 * (astro_weather_server, default port 50055) via a dedicated client.
 * No silent simulated fallback — an unreachable service returns an explicit
 * 503 so the UI never shows fake data (P1 fix).
 *
 * R8: besides GET /status the route now also exposes GET /history,
 * POST /rules, GET /rules (configured rules) and GET /alerts/stream
 * (Server-Sent Events alert stream).
 */
'use strict';

const express = require('express');
const router = express.Router();
const path = require('path');
const fs = require('fs');
const { weatherGrpcCall, getWeatherGrpcClient } = require('../grpc/client');
const { errorResponse } = require('../grpc/converters');

/**
 * GET /api/weather/status
 * Returns current weather conditions.
 */
router.get('/status', async (req, res) => {
  try {
    const status = await weatherGrpcCall('GetWeatherStatus', {});
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
    errorResponse(res, 503, 'Weather service unavailable', err.message);
  }
});

/**
 * GET /api/weather/history
 * Returns historical weather data.
 */
router.get('/history', async (req, res) => {
  try {
    const { start_time, end_time, max_points } = req.query;
    const history = await weatherGrpcCall('GetWeatherHistory', {
      start_time: start_time || null,
      end_time: end_time || null,
      max_points: parseInt(max_points, 10) || 100,
    });
    res.json({
      readings: (history.readings || []).map(r => ({
        timestamp: r.timestamp,
        temperature_c: r.temperature_c,
        humidity_percent: r.humidity_percent,
        pressure_hpa: r.pressure_hpa,
        wind_speed_ms: r.wind_speed_ms,
        rain_rate_mmh: r.rain_rate_mmh,
        cloud_cover_percent: r.cloud_cover_percent,
      })),
      total_points: history.total_points || 0,
    });
  } catch (err) {
    errorResponse(res, 503, 'Weather service unavailable', err.message);
  }
});

/**
 * POST /api/weather/rules
 * Configure weather alert rules.
 * Body: WeatherRules (min/max temp, wind, rain, cloud limits, ...)
 */
router.post('/rules', async (req, res) => {
  try {
    await weatherGrpcCall('SetWeatherRules', req.body || {});
    res.json({ success: true, message: 'Weather rules applied' });
  } catch (err) {
    errorResponse(res, 503, 'Weather service unavailable', err.message);
  }
});

/**
 * GET /api/weather/rules
 * Return the currently configured weather alert rules (read from the weather
 * service config file — this is the real configuration, not simulated data).
 */
router.get('/rules', async (req, res) => {
  try {
    const configPath = process.env.WEATHER_CONFIG_PATH ||
      path.join(__dirname, '../../../config/weather_config.json');
    const raw = fs.readFileSync(configPath, 'utf8');
    const config = JSON.parse(raw);
    res.json({ rules: config.rules || {} });
  } catch (err) {
    errorResponse(res, 500, 'Failed to read weather rules config', err.message);
  }
});

/**
 * GET /api/weather/alerts/stream
 * Server-Sent Events stream of live weather alerts, backed by the
 * SubscribeWeatherAlerts server-streaming gRPC method. The client receives
 * one `data` event per alert; the stream closes when the backend ends or the
 * client disconnects. If the backend is unreachable a 503 is returned.
 */
router.get('/alerts/stream', (req, res) => {
  let stream;
  try {
    const client = getWeatherGrpcClient();
    stream = client.subscribeWeatherAlerts({}, {
      deadline: new Date(Date.now() + 3600 * 1000), // 1h
    });
  } catch (err) {
    return errorResponse(res, 503, 'Weather service unavailable', err.message);
  }

  res.writeHead(200, {
    'Content-Type': 'text/event-stream',
    'Cache-Control': 'no-cache',
    Connection: 'keep-alive',
  });
  res.write('retry: 5000\n\n');

  const send = (event, data) => {
    res.write(`event: ${event}\n`);
    res.write(`data: ${JSON.stringify(data)}\n\n`);
  };

  stream.on('data', (alert) => {
    send('alert', {
      id: alert.id,
      timestamp: alert.timestamp ? (alert.timestamp.seconds || 0) : 0,
      severity: alert.severity,
      condition: alert.condition,
      message: alert.message,
      current_value: alert.current_value,
      threshold_value: alert.threshold_value,
      suggested_action: alert.suggested_action,
      auto_action_taken: alert.auto_action_taken,
    });
  });

  const end = () => { try { res.end(); } catch (e) { /* already closed */ } };
  stream.on('error', (err) => {
    send('error', { message: err.message });
    end();
  });
  stream.on('end', end);
  req.on('close', () => {
    try { if (stream) stream.cancel(); } catch (e) { /* not connected */ }
  });
});

module.exports = router;
