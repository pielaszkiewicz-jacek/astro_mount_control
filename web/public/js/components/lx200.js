/**
 * LX200 Serial Interface Management Component
 *
 * Manages the LX200 serial protocol server: start/stop, status display.
 */
const Lx200Component = (() => {
  'use strict';

  function render() {
    return `
      <div class="lx200-dashboard">
        <div class="card">
          <div class="card-header">
            <h2 class="card-title">LX200 Serial Interface</h2>
            <span id="lx200-status-badge" class="status-badge idle">STOPPED</span>
          </div>
          <div class="card-body">
            <div id="lx200-status-content">
              <div class="status-placeholder">Loading status...</div>
            </div>
            <div style="margin-top:12px; display:flex; gap:8px;">
              <button id="btn-lx200-start" class="btn btn-primary">Start LX200</button>
              <button id="btn-lx200-stop" class="btn btn-danger">Stop LX200</button>
              <button id="btn-lx200-refresh" class="btn btn-secondary">Refresh</button>
            </div>
            <div id="lx200-message" style="margin-top:8px; font-size:0.82rem;"></div>
          </div>
        </div>
      </div>`;
  }

  async function refreshStatus() {
    const content = document.getElementById('lx200-status-content');
    const badge = document.getElementById('lx200-status-badge');
    if (!content || !badge) return;

    try {
      const resp = await fetch('/api/lx200/status');
      const data = await resp.json();

      if (data.running) {
        badge.className = 'status-badge success';
        badge.textContent = 'RUNNING';
      } else if (data.enabled) {
        badge.className = 'status-badge warning';
        badge.textContent = 'ENABLED (stopped)';
      } else {
        badge.className = 'status-badge idle';
        badge.textContent = 'DISABLED';
      }

      content.innerHTML = `
        <div class="stat-row">
          <span class="stat-label">Status</span>
          <span class="stat-value ${data.running ? 'success' : ''}">${data.running ? 'Running' : 'Stopped'}</span>
        </div>
        <div class="stat-row">
          <span class="stat-label">Port</span>
          <span class="stat-value">${data.port || '—'}</span>
        </div>
        <div class="stat-row">
          <span class="stat-label">Baud Rate</span>
          <span class="stat-value">${data.baud_rate || '—'} bps</span>
        </div>
        <div class="stat-row">
          <span class="stat-label">Enabled</span>
          <span class="stat-value ${data.enabled ? 'success' : ''}">${data.enabled ? 'Yes' : 'No'}</span>
        </div>
      `;

      // Update button states
      const startBtn = document.getElementById('btn-lx200-start');
      const stopBtn = document.getElementById('btn-lx200-stop');
      if (startBtn) startBtn.disabled = data.running || !data.enabled;
      if (stopBtn) stopBtn.disabled = !data.running;
    } catch (err) {
      content.innerHTML = `<div class="status-placeholder" style="color:var(--color-danger);">Failed to load LX200 status: ${err.message}</div>`;
    }
  }

  async function startLx200() {
    const msgEl = document.getElementById('lx200-message');
    try {
      const resp = await fetch('/api/lx200/start', { method: 'POST' });
      const data = await resp.json();
      if (msgEl) {
        msgEl.style.color = data.running ? 'var(--color-success)' : 'var(--color-danger)';
        msgEl.textContent = data.running ? 'LX200 server started successfully.' : 'Failed to start LX200 server.';
      }
      await refreshStatus();
    } catch (err) {
      if (msgEl) {
        msgEl.style.color = 'var(--color-danger)';
        msgEl.textContent = `Error: ${err.message}`;
      }
    }
  }

  async function stopLx200() {
    const msgEl = document.getElementById('lx200-message');
    try {
      await fetch('/api/lx200/stop', { method: 'POST' });
      if (msgEl) {
        msgEl.style.color = 'var(--color-success)';
        msgEl.textContent = 'LX200 server stopped.';
      }
      await refreshStatus();
    } catch (err) {
      if (msgEl) {
        msgEl.style.color = 'var(--color-danger)';
        msgEl.textContent = `Error: ${err.message}`;
      }
    }
  }

  function init() {
    // Attach button handlers after DOM is populated
    const startBtn = document.getElementById('btn-lx200-start');
    const stopBtn = document.getElementById('btn-lx200-stop');
    const refreshBtn = document.getElementById('btn-lx200-refresh');

    if (startBtn) startBtn.addEventListener('click', startLx200);
    if (stopBtn) stopBtn.addEventListener('click', stopLx200);
    if (refreshBtn) refreshBtn.addEventListener('click', refreshStatus);
  }

  return { render, refreshStatus, init };
})();
