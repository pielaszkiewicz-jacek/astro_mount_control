/**
 * Notifications Panel Component
 */
const NotificationsComponent = (() => {
  'use strict';

  function render() {
    return `
      <div id="notifications-panel" class="panel">
        <h2>🔔 Notifications</h2>
        <div class="notification-config">
          <h3>Email Channel</h3>
          <label>SMTP Host: <input type="text" id="notif-smtp-host" value="localhost"></label>
          <label>Port: <input type="number" id="notif-smtp-port" value="587"></label>
          <label>Use TLS: <input type="checkbox" id="notif-smtp-tls" checked></label>
          <label>From: <input type="email" id="notif-from" value="astro-mount@localhost"></label>
          <label>To: <input type="text" id="notif-to" placeholder="user@example.com"></label>
          <button onclick="NotificationsComponent.testEmail()">Test Email</button>

          <h3>Webhook Channel</h3>
          <label>URL: <input type="url" id="notif-webhook-url"></label>
          <label>Auth Token: <input type="password" id="notif-webhook-token"></label>
          <button onclick="NotificationsComponent.testWebhook()">Test Webhook</button>

          <h3>Event Filters</h3>
          <label>Min Severity:
            <select id="notif-severity">
              <option value="0">DEBUG</option>
              <option value="1" selected>INFO</option>
              <option value="2">WARNING</option>
              <option value="3">ERROR</option>
              <option value="4">CRITICAL</option>
            </select>
          </label>
          <label><input type="checkbox" id="notif-errors" checked> Notify on Errors</label>
          <label><input type="checkbox" id="notif-weather" checked> Notify on Weather Alerts</label>
        </div>
        <div class="notification-log">
          <h3>Recent Events</h3>
          <div id="notif-event-list" class="log-list"></div>
        </div>
      </div>`;
  }

  function testEmail() { alert('Test email would be sent'); }
  function testWebhook() { alert('Test webhook would be sent'); }

  return { render, testEmail, testWebhook };
})();
