/**
 * Astronomical Mount Controller - Mount Status Component
 *
 * Renders the mount status card with real-time state information.
 * This is a framework component — extend with additional data displays
 * by adding new render functions and calling them in render().
 */

const MountStatusComponent = (() => {
  'use strict';

  const { $, formatNumber, formatRA, formatDec } = Utils;

  /**
   * Update the mount status card with new state data.
   * Called by the main app's polling loop.
   *
   * @param {object} state - Formatted controller state from the API
   */
  function render(state) {
    if (!state) {
      showPlaceholder('mount-status-content', 'Connecting to mount controller...');
      showPlaceholder('position-content', 'Waiting for data...');
      showPlaceholder('environment-content', 'Waiting for data...');
      showPlaceholder('tracking-content', 'Waiting for data...');
      return;
    }

    // Update status badge
    const badge = $('#status-badge');
    if (badge) {
      const status = (state.status || 'UNKNOWN').toLowerCase();
      badge.className = `status-badge ${status}`;
      badge.textContent = state.status || 'UNKNOWN';
    }

    // Mount status card
    const statusEl = $('#mount-status-content');
    if (statusEl) {
      statusEl.innerHTML = `
        <div class="stat-row">
          <span class="stat-label">Status</span>
          <span class="stat-value ${getStatusClass(state.status)}">${state.status || 'UNKNOWN'}</span>
        </div>
        <div class="stat-row">
          <span class="stat-label">Encoders</span>
          <span class="stat-value ${state.encoders_enabled ? 'success' : 'muted'}">
            ${state.encoders_enabled ? '<span class="status-icon success"></span> Enabled' : '<span class="status-icon danger"></span> Disabled'}
          </span>
        </div>
        <div class="stat-row">
          <span class="stat-label">Guider</span>
          <span class="stat-value ${state.guider_active ? 'success' : ''}">
            ${state.guider_active ? '<span class="status-icon success"></span> Active' : '<span class="status-icon muted"></span> Inactive'}
          </span>
        </div>
        <div class="stat-row">
          <span class="stat-label">Pier Side</span>
          <span class="stat-value">${state.pier_side === 1 ? 'East' : state.pier_side === -1 ? 'West' : '—'}</span>
        </div>
        <div class="stat-row">
          <span class="stat-label">Meridian Flip</span>
          <span class="stat-value">${state.meridian_flipped ? '<span class="status-icon success"></span> Done' : '—'}</span>
        </div>
        <div class="stat-row">
          <span class="stat-label">Time to Meridian</span>
          <span class="stat-value">${formatNumber(state.time_to_meridian, 2)} h</span>
        </div>
      `;
    }

    // Position card
    const posEl = $('#position-content');
    if (posEl) {
      posEl.innerHTML = `
        <div class="stat-row">
          <span class="stat-label">Axis 1</span>
          <span class="stat-value highlight">${formatNumber(state.position?.axis1, 4)}°</span>
        </div>
        <div class="stat-row">
          <span class="stat-label">Axis 2</span>
          <span class="stat-value highlight">${formatNumber(state.position?.axis2, 4)}°</span>
        </div>
        <div class="stat-row">
          <span class="stat-label">Tracking Rate RA</span>
          <span class="stat-value">${formatNumber(state.tracking_rate_ra, 4)} "/s</span>
        </div>
        <div class="stat-row">
          <span class="stat-label">Tracking Rate Dec</span>
          <span class="stat-value">${formatNumber(state.tracking_rate_dec, 4)} "/s</span>
        </div>
      `;
    }

    // Environment card
    const envEl = $('#environment-content');
    if (envEl) {
      envEl.innerHTML = `
        <div class="stat-row">
          <span class="stat-label">Temperature</span>
          <span class="stat-value">${formatNumber(state.temperature, 1)} °C</span>
        </div>
        <div class="stat-row">
          <span class="stat-label">Pressure</span>
          <span class="stat-value">${formatNumber(state.pressure, 1)} hPa</span>
        </div>
        <div class="stat-row">
          <span class="stat-label">Humidity</span>
          <span class="stat-value">${formatNumber(state.humidity, 1)} %</span>
        </div>
      `;
    }

    // Tracking card
    const trackEl = $('#tracking-content');
    if (trackEl) {
      const tracked = state.tracked_object;
      trackEl.innerHTML = tracked ? `
        <div class="stat-row">
          <span class="stat-label">Target</span>
          <span class="stat-value">${tracked.name || 'Unknown'}</span>
        </div>
        <div class="stat-row">
          <span class="stat-label">RA</span>
          <span class="stat-value">${formatRA(tracked.ra)}</span>
        </div>
        <div class="stat-row">
          <span class="stat-label">Dec</span>
          <span class="stat-value">${formatDec(tracked.dec)}</span>
        </div>
        <div class="stat-row">
          <span class="stat-label">Tracking Error RA</span>
          <span class="stat-value ${getErrorClass(tracked.tracking_error_ra)}">
            ${formatNumber(tracked.tracking_error_ra, 2)}"
          </span>
        </div>
        <div class="stat-row">
          <span class="stat-label">Tracking Error Dec</span>
          <span class="stat-value ${getErrorClass(tracked.tracking_error_dec)}">
            ${formatNumber(tracked.tracking_error_dec, 2)}"
          </span>
        </div>
        <div class="stat-row">
          <span class="stat-label">Tracking Performance</span>
          <span class="stat-value ${getPerformanceClass(state.tracking_performance)}">
            ${formatNumber(state.tracking_performance, 1)}%
          </span>
        </div>
        <div class="stat-row">
          <span class="stat-label">Pointing Error</span>
          <span class="stat-value ${getErrorClass(state.pointing_error)}">
            ${formatNumber(state.pointing_error, 2)}"
          </span>
        </div>
      ` : `
        <div class="status-placeholder">Not tracking any object</div>
      `;
    }
  }

  /**
   * Show a placeholder message in a card body.
   * @param {string} elementId
   * @param {string} message
   */
  function showPlaceholder(elementId, message) {
    const el = $(`#${elementId}`);
    if (el) {
      el.innerHTML = `<div class="status-placeholder">${message}</div>`;
    }
  }

  /**
   * Get CSS class based on mount status.
   * @param {string} status
   * @returns {string}
   */
  function getStatusClass(status) {
    switch ((status || '').toLowerCase()) {
      case 'tracking':  return 'success';
      case 'slewing':   return 'warning';
      case 'error':     return 'danger';
      case 'parked':    return 'highlight';
      default:          return '';
    }
  }

  /**
   * Get CSS class based on error magnitude.
   * @param {number} error - Error in arcseconds
   * @returns {string}
   */
  function getErrorClass(error) {
    if (error === null || error === undefined) return '';
    if (error > 2.0) return 'danger';
    if (error > 0.5) return 'warning';
    return 'success';
  }

  /**
   * Get CSS class based on performance percentage.
   * @param {number} perf
   * @returns {string}
   */
  function getPerformanceClass(perf) {
    if (perf === null || perf === undefined) return '';
    if (perf < 80) return 'danger';
    if (perf < 95) return 'warning';
    return 'success';
  }

  // Public API
  return { render };
})();
