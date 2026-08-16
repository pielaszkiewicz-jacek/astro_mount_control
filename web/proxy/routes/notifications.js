/**
 * Notifications Routes
 *
 * Proxies notification operations to the NotificationService gRPC service,
 * which is hosted IN-PROCESS inside the mount controller (unified gRPC port
 * 50051) — R1. No silent simulated fallback — an unreachable service returns
 * an explicit 503.
 */
'use strict';

const express = require('express');
const router = express.Router();
const { notificationsGrpcCall } = require('../grpc/client');
const { errorResponse } = require('../grpc/converters');

/**
 * GET /api/notifications/status
 * Returns notification system status (channels, events sent, etc.).
 */
router.get('/status', async (req, res) => {
  try {
    const status = await notificationsGrpcCall('GetNotificationStatus', {});
    res.json({
      configured: status.configured || false,
      active_channels: status.active_channels || 0,
      events_sent_total: status.events_sent_total || 0,
      events_sent_last_hour: status.events_sent_last_hour || 0,
      events_queued: status.events_queued || 0,
      events_failed: status.events_failed || 0,
      last_error: status.last_error || '',
      last_event_time: status.last_event_time || null,
    });
  } catch (err) {
    errorResponse(res, 503, 'Notification service unavailable', err.message);
  }
});

/**
 * POST /api/notifications/configure
 * Configure notification channels and event filters.
 * Body: NotificationConfig proto (channels, min_severity, enabled_events, ...)
 */
router.post('/configure', async (req, res) => {
  try {
    await notificationsGrpcCall('ConfigureNotifications', req.body || {});
    res.json({ success: true, message: 'Notification configuration applied' });
  } catch (err) {
    errorResponse(res, 503, 'Notification service unavailable', err.message);
  }
});

/**
 * POST /api/notifications/test
 * Send a test notification through configured channels.
 * Body: { message?: string }
 */
router.post('/test', async (req, res) => {
  try {
    const { message } = req.body || {};
    await notificationsGrpcCall('SendTestNotification', { message: message || '' });
    res.json({ success: true, message: 'Test notification sent' });
  } catch (err) {
    errorResponse(res, 503, 'Notification service unavailable', err.message);
  }
});

module.exports = router;
