/**
 * Notifications Panel Component (R1 / N1 / N6)
 *
 * Uses the real /api/notifications backend (NotificationService hosted
 * in-process) — status, channel configuration and test notifications.
 * N1: the panel now saves the channel configuration via
 *     POST /api/notifications/configure (previously the fields were only
 *     cosmetic and never sent to the backend).
 * N6: the status is auto-refreshed periodically instead of only on demand.
 */
const NotificationsComponent = (() => {
  'use strict';

  function render() {
    return `
      <div id="notifications-panel" class="panel">
        <h2>🔔 Notifications</h2>
        <div id="notifications-status" class="notification-status" style="margin-bottom:12px;">
          Loading status…
        </div>

        <div class="control-form">
          <h3 style="margin:16px 0 8px; font-size:0.9rem; color:var(--color-text-secondary);">Email Channel</h3>
          <div class="form-row">
            <div class="form-group">
              <label for="notif-smtp-host" class="form-label">SMTP Host</label>
              <input type="text" id="notif-smtp-host" class="form-input" value="localhost">
            </div>
            <div class="form-group">
              <label for="notif-smtp-port" class="form-label">Port</label>
              <input type="number" id="notif-smtp-port" class="form-input" value="587">
            </div>
            <div class="form-group">
              <label for="notif-smtp-tls" class="form-label"><input type="checkbox" id="notif-smtp-tls" checked style="margin-right:4px;">Use TLS</label>
            </div>
          </div>
          <div class="form-row">
            <div class="form-group">
              <label for="notif-from" class="form-label">From</label>
              <input type="email" id="notif-from" class="form-input" value="astro-mount@localhost">
            </div>
            <div class="form-group">
              <label for="notif-to" class="form-label">To</label>
              <input type="text" id="notif-to" class="form-input" placeholder="user@example.com">
            </div>
          </div>

          <h3 style="margin:16px 0 8px; font-size:0.9rem; color:var(--color-text-secondary);">Webhook Channel</h3>
          <div class="form-row">
            <div class="form-group">
              <label for="notif-webhook-url" class="form-label">URL</label>
              <input type="url" id="notif-webhook-url" class="form-input">
            </div>
            <div class="form-group">
              <label for="notif-webhook-token" class="form-label">Auth Token</label>
              <input type="password" id="notif-webhook-token" class="form-input">
            </div>
          </div>

          <h3 style="margin:16px 0 8px; font-size:0.9rem; color:var(--color-text-secondary);">Event Filters</h3>
          <div class="form-row">
            <div class="form-group">
              <label for="notif-severity" class="form-label">Min Severity</label>
              <select id="notif-severity" class="form-input form-select">
                <option value="0">DEBUG</option>
                <option value="1" selected>INFO</option>
                <option value="2">WARNING</option>
                <option value="3">ERROR</option>
                <option value="4">CRITICAL</option>
              </select>
            </div>
          </div>

          <div class="action-grid" style="margin-top:12px;">
            <button class="btn btn-primary" onclick="NotificationsComponent.saveConfig()">💾 Save Configuration</button>
            <button class="btn btn-secondary" onclick="NotificationsComponent.sendTest()">✉️ Send Test Notification</button>
            <button class="btn btn-secondary" onclick="NotificationsComponent.refreshStatus()">⟳ Refresh</button>
          </div>
        </div>
      </div>`;
  }

  /**
   * Fetch and render the notification service status.
   */
  async function refreshStatus() {
    try {
      const data = await Api.get('/notifications/status');
      const el = document.getElementById('notifications-status');
      if (el) {
        el.innerHTML =
          `<b>Configured:</b> ${data.configured ? '✅' : '❌'} · ` +
          `<b>Channels:</b> ${data.active_channels || 0} · ` +
          `<b>Sent (total):</b> ${data.events_sent_total || 0} · ` +
          `<b>Sent (1h):</b> ${data.events_sent_last_hour || 0} · ` +
          `<b>Failed:</b> ${data.events_failed || 0} · ` +
          `<b>Queued:</b> ${data.events_queued || 0}` +
          (data.last_error ? ` · <span style="color:var(--color-danger)">${data.last_error}</span>` : '');
      }
    } catch (err) {
      App.showServiceUnavailable('panel-notifications', 'Notification service');
    }
  }

  /**
   * N1: save the channel configuration from the form fields to the backend.
   * Builds a NotificationConfig proto-compatible payload and POSTs it to
   * /api/notifications/configure.
   */
  async function saveConfig() {
    const toList = (document.getElementById('notif-to').value || '')
      .split(',').map(s => s.trim()).filter(Boolean);

    const payload = {
      channels: [],
      enabled_events: {},
      min_severity: parseInt(document.getElementById('notif-severity').value, 10) || 1,
      notify_on_error: true,
      notify_on_weather_alert: true,
      notify_on_session_end: false,
      notify_on_power_low: true,
      aggregate_messages: false,
      aggregation_interval_minutes: 5,
    };

    // Email channel (only if at least one recipient is set).
    const smtpHost = document.getElementById('notif-smtp-host').value;
    const fromAddr = document.getElementById('notif-from').value;
    if (smtpHost && fromAddr && toList.length > 0) {
      payload.channels.push({
        type: 'CHANNEL_EMAIL',
        enabled: true,
        email: {
          smtp_host: smtpHost,
          smtp_port: parseInt(document.getElementById('notif-smtp-port').value, 10) || 587,
          use_tls: document.getElementById('notif-smtp-tls').checked,
          from_address: fromAddr,
          to_addresses: toList,
          subject_prefix: '[AstroMount]',
        },
      });
    }

    // Webhook channel (only if a URL is set).
    const webhookUrl = document.getElementById('notif-webhook-url').value;
    if (webhookUrl) {
      payload.channels.push({
        type: 'CHANNEL_WEBHOOK',
        enabled: true,
        webhook: {
          url: webhookUrl,
          method: 'POST',
          headers: {},
          auth_token: document.getElementById('notif-webhook-token').value || '',
          timeout_seconds: 10,
          retry_count: 3,
        },
      });
    }

    try {
      const resp = await Api.post('/notifications/configure', payload);
      if (resp && resp.success) {
        App.showToast('Notification configuration saved', 'success');
        refreshStatus();
      } else {
        App.showToast('Notification configuration applied', 'success');
        refreshStatus();
      }
    } catch (err) {
      App.showToast(`Failed to save configuration: ${err.message}`, 'error');
    }
  }

  /**
   * Send a test notification through the configured channels.
   * @param {string} label
   */
  async function sendTest(label) {
    try {
      await Api.post('/notifications/test', { message: `Test notification from ${label || 'Notifications'}` });
      App.showToast(`Test notification sent (${label || 'Notifications'})`, 'success');
    } catch (err) {
      App.showToast(`Failed to send test notification: ${err.message}`, 'error');
    }
  }

  // N6: periodic auto-refresh (every 10 s) once the panel is rendered.
  let refreshTimer = null;
  function startAutoRefresh() {
    if (refreshTimer) return;
    refreshTimer = setInterval(() => {
      // Only refresh when the panel is visible to avoid background chatter.
      const panel = document.getElementById('panel-notifications');
      if (panel && panel.classList.contains('active')) {
        refreshStatus();
      }
    }, 10000);
  }

  // Kick off the initial load + auto-refresh when this module loads.
  if (typeof window !== 'undefined' && window.addEventListener) {
    window.addEventListener('load', () => {
      refreshStatus();
      startAutoRefresh();
    });
    // Re-fetch whenever the tab becomes active.
    const panel = document.getElementById('panel-notifications');
    if (panel) {
      panel.addEventListener('click', refreshStatus);
    }
  }

  return { render, refreshStatus, saveConfig, sendTest };
})();
