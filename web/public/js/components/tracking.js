/**
 * Astronomical Mount Controller - Ephemeris Tracking Component
 *
 * Manages the tracking tab UI for moving objects (satellites, comets,
 * asteroids, planets). Provides real-time status display, start/stop
 * controls, and ephemeris metrics.
 *
 * Data flow:
 *   TrackingComponent polls tracking status when active
 *   → displays current state, position, rates, errors
 *   → start/stop buttons call API methods
 */
const TrackingComponent = (() => {
  'use strict';

  const { $, formatNumber, formatRA, formatDec, formatTime } = Utils;

  // ─── Polling State ────────────────────────────────────────────────────────

  /** @type {number|null} Interval ID for tracking status polling */
  let pollInterval = null;

  /** @type {boolean} Whether the tracking component is currently visible */
  let isActive = false;

  // Polling interval in milliseconds
  const POLL_INTERVAL_MS = 2000;

  // ─── Tracking State Labels ─────────────────────────────────────────────────

  const TRACKING_STATE_LABELS = {
    IDLE: 'Idle',
    SLEWING_TO_START: 'Slewing to Start',
    WAITING_AT_START: 'Waiting at Start',
    TRACKING: 'Tracking',
    PREDICTING: 'Predicting',
    ENDED: 'Ended',
    ERROR: 'Error',
  };

  const TRACKING_STATE_CLASSES = {
    IDLE: 'idle',
    SLEWING_TO_START: 'slewing',
    WAITING_AT_START: 'warning',
    TRACKING: 'tracking',
    PREDICTING: 'tracking',
    ENDED: 'muted',
    ERROR: 'error',
  };

  // ─── Initialization ───────────────────────────────────────────────────────

  function init() {
    bindEvents();
  }

  function bindEvents() {
    // Start tracking by object ID
    const startBtn = $('#tracking-start-btn');
    if (startBtn) {
      startBtn.addEventListener('click', handleStartTracking);
    }

    // Start tracking with ephemeris data (JSON upload)
    const startDataBtn = $('#tracking-start-data-btn');
    if (startDataBtn) {
      startDataBtn.addEventListener('click', handleStartTrackingWithData);
    }

    // Stop tracking
    const stopBtn = $('#tracking-stop-btn');
    if (stopBtn) {
      stopBtn.addEventListener('click', handleStopTracking);
    }

    // Upload ephemeris
    const uploadBtn = $('#tracking-upload-btn');
    if (uploadBtn) {
      uploadBtn.addEventListener('click', handleUploadEphemeris);
    }

    // Clear cache
    const clearCacheBtn = $('#tracking-clear-cache-btn');
    if (clearCacheBtn) {
      clearCacheBtn.addEventListener('click', handleClearCache);
    }
  }

  // ─── Event Handlers ───────────────────────────────────────────────────────

  async function handleStartTracking() {
    const idInput = $('#tracking-object-id');
    if (!idInput || !idInput.value.trim()) {
      App.showToast('Please enter an object ID', 'error');
      return;
    }

    const startBtn = $('#tracking-start-btn');
    if (startBtn) startBtn.disabled = true;

    try {
      const status = await Api.startTracking(idInput.value.trim());
      App.showToast(
        `Tracking started: ${status.object_name || status.object_id} (${TRACKING_STATE_LABELS[status.state] || status.state})`,
        'success'
      );
      // Immediately refresh status display
      await loadTrackingStatus();
    } catch (err) {
      App.showToast(`Failed to start tracking: ${err.message}`, 'error');
    } finally {
      if (startBtn) startBtn.disabled = false;
    }
  }

  async function handleStartTrackingWithData() {
    const dataInput = $('#tracking-ephemeris-json');
    if (!dataInput || !dataInput.value.trim()) {
      App.showToast('Please enter ephemeris JSON data', 'error');
      return;
    }

    const startBtn = $('#tracking-start-data-btn');
    if (startBtn) startBtn.disabled = true;

    try {
      let ephemerisData;
      try {
        ephemerisData = JSON.parse(dataInput.value.trim());
      } catch {
        App.showToast('Invalid JSON format', 'error');
        if (startBtn) startBtn.disabled = false;
        return;
      }

      const status = await Api.startTrackingWithData(ephemerisData);
      App.showToast(
        `Tracking started: ${status.object_name || status.object_id}`,
        'success'
      );
      await loadTrackingStatus();
    } catch (err) {
      App.showToast(`Failed to start tracking: ${err.message}`, 'error');
    } finally {
      if (startBtn) startBtn.disabled = false;
    }
  }

  async function handleStopTracking() {
    const stopBtn = $('#tracking-stop-btn');
    if (stopBtn) stopBtn.disabled = true;

    try {
      await Api.stopTracking();
      App.showToast('Tracking stopped', 'info');
      await loadTrackingStatus();
      await loadTrackingMetrics();
    } catch (err) {
      App.showToast(`Failed to stop tracking: ${err.message}`, 'error');
    } finally {
      if (stopBtn) stopBtn.disabled = false;
    }
  }

  async function handleUploadEphemeris() {
    const dataInput = $('#tracking-upload-json');
    if (!dataInput || !dataInput.value.trim()) {
      App.showToast('Please enter ephemeris JSON data', 'error');
      return;
    }

    const uploadBtn = $('#tracking-upload-btn');
    if (uploadBtn) uploadBtn.disabled = true;

    try {
      let ephemerisData;
      try {
        ephemerisData = JSON.parse(dataInput.value.trim());
      } catch {
        App.showToast('Invalid JSON format', 'error');
        if (uploadBtn) uploadBtn.disabled = false;
        return;
      }

      await Api.uploadEphemeris(ephemerisData);
      App.showToast(`Ephemeris uploaded for '${ephemerisData.object_id || ephemerisData.object_name}'`, 'success');
      dataInput.value = '';
    } catch (err) {
      App.showToast(`Upload failed: ${err.message}`, 'error');
    } finally {
      if (uploadBtn) uploadBtn.disabled = false;
    }
  }

  async function handleClearCache() {
    const clearBtn = $('#tracking-clear-cache-btn');
    if (clearBtn) clearBtn.disabled = true;

    try {
      await Api.clearEphemerisCache();
      App.showToast('Ephemeris cache cleared', 'success');
      await loadTrackingStatus();
      await loadTrackingMetrics();
    } catch (err) {
      App.showToast(`Failed to clear cache: ${err.message}`, 'error');
    } finally {
      if (clearBtn) clearBtn.disabled = false;
    }
  }

  // ─── Data Loading ─────────────────────────────────────────────────────────

  /**
   * Fetch and display the current tracking status.
   */
  async function loadTrackingStatus() {
    try {
      const status = await Api.getTrackingStatus();
      updateTrackingStatusUI(status);
    } catch (err) {
      showStatusPlaceholder('Tracking service unavailable');
    }
  }

  /**
   * Fetch and display tracking metrics.
   */
  async function loadTrackingMetrics() {
    try {
      const metrics = await Api.getTrackingMetrics();
      updateTrackingMetricsUI(metrics);
    } catch {
      // Metrics may not be available — that's OK
      showMetricsPlaceholder('No metrics available');
    }
  }

  // ─── UI Updates ───────────────────────────────────────────────────────────

  /**
   * Update the tracking status card with data from the API.
   * @param {object} status - EphemerisTrackStatus from API
   */
  function updateTrackingStatusUI(status) {
    const container = $('#tracking-status-content');
    if (!container) return;

    // Update state badge
    const badge = $('#tracking-state-badge');
    if (badge) {
      const state = status.state || 'IDLE';
      const stateLabel = TRACKING_STATE_LABELS[state] || state;
      const stateClass = TRACKING_STATE_CLASSES[state] || 'idle';
      badge.className = `status-badge ${stateClass}`;
      badge.textContent = stateLabel;
    }

    // Determine if tracking is active
    const isActive_ = ['SLEWING_TO_START', 'WAITING_AT_START', 'TRACKING', 'PREDICTING'].includes(status.state);

    const pos = status.current_position || {};
    const target = status.target_position || {};

    container.innerHTML = `
      <div class="stat-row">
        <span class="stat-label">Object</span>
        <span class="stat-value">${escapeHtml(status.object_name || status.object_id || '—')}</span>
      </div>
      <div class="stat-row">
        <span class="stat-label">Object ID</span>
        <span class="stat-value mono">${escapeHtml(status.object_id || '—')}</span>
      </div>
      <div class="stat-row">
        <span class="stat-label">Current RA</span>
        <span class="stat-value highlight">${pos.ra !== undefined ? formatRA(pos.ra) : '—'}</span>
      </div>
      <div class="stat-row">
        <span class="stat-label">Current Dec</span>
        <span class="stat-value highlight">${pos.dec !== undefined ? formatDec(pos.dec) : '—'}</span>
      </div>
      <div class="stat-row">
        <span class="stat-label">Target RA</span>
        <span class="stat-value">${target.ra !== undefined ? formatRA(target.ra) : '—'}</span>
      </div>
      <div class="stat-row">
        <span class="stat-label">Target Dec</span>
        <span class="stat-value">${target.dec !== undefined ? formatDec(target.dec) : '—'}</span>
      </div>
      <div class="stat-row">
        <span class="stat-label">Position Error</span>
        <span class="stat-value ${getErrorClass(status.position_error_arcsec)}">
          ${status.position_error_arcsec !== undefined && status.position_error_arcsec !== null
            ? formatNumber(status.position_error_arcsec, 2) + '"'
            : '—'}
        </span>
      </div>
      <div class="stat-row">
        <span class="stat-label">RA Rate</span>
        <span class="stat-value">${status.ra_rate !== undefined ? formatNumber(status.ra_rate, 4) + ' "/s' : '—'}</span>
      </div>
      <div class="stat-row">
        <span class="stat-label">Dec Rate</span>
        <span class="stat-value">${status.dec_rate !== undefined ? formatNumber(status.dec_rate, 4) + ' "/s' : '—'}</span>
      </div>
      <div class="stat-row">
        <span class="stat-label">Time Remaining</span>
        <span class="stat-value">${status.time_remaining_seconds !== undefined
          ? formatTimeRemaining(status.time_remaining_seconds)
          : '—'}</span>
      </div>
      <div class="stat-row">
        <span class="stat-label">Earth Rotation</span>
        <span class="stat-value ${status.earth_rotation_corrected ? 'success' : ''}">
          ${status.earth_rotation_corrected
            ? '<span class="status-icon success"></span> Corrected'
            : '<span class="status-icon muted"></span> Not Applied'}
        </span>
      </div>
      ${status.error_message ? `
        <div class="stat-row">
          <span class="stat-label">Error</span>
          <span class="stat-value danger">${escapeHtml(status.error_message)}</span>
        </div>
      ` : ''}
      ${status.warnings && status.warnings.length > 0 ? `
        <div class="stat-row">
          <span class="stat-label">Warnings</span>
          <span class="stat-value warning">${status.warnings.join('; ')}</span>
        </div>
      ` : ''}
    `;

    // Update button states
    const startBtn = $('#tracking-start-btn');
    const stopBtn = $('#tracking-stop-btn');

    if (startBtn) startBtn.disabled = isActive_;
    if (stopBtn) stopBtn.disabled = !isActive_;
  }

  /**
   * Update the tracking metrics card.
   * @param {object} metrics - EphemerisMetrics from API
   */
  function updateTrackingMetricsUI(metrics) {
    const container = $('#tracking-metrics-content');
    if (!container) return;

    if (!metrics || !metrics.object_id) {
      container.innerHTML = `<div class="status-placeholder">No tracking metrics available</div>`;
      return;
    }

    container.innerHTML = `
      <div class="stat-row">
        <span class="stat-label">Object</span>
        <span class="stat-value">${escapeHtml(metrics.object_name || metrics.object_id)}</span>
      </div>
      <div class="stat-row">
        <span class="stat-label">Type</span>
        <span class="stat-value">${escapeHtml(metrics.object_type || '—')}</span>
      </div>
      <div class="stat-row">
        <span class="stat-label">Total Track Time</span>
        <span class="stat-value">${formatDuration(metrics.total_track_time_seconds)}</span>
      </div>
      <div class="stat-row">
        <span class="stat-label">Avg Position Error</span>
        <span class="stat-value ${getErrorClass(metrics.avg_position_error_arcsec)}">
          ${metrics.avg_position_error_arcsec !== undefined
            ? formatNumber(metrics.avg_position_error_arcsec, 2) + '"'
            : '—'}
        </span>
      </div>
      <div class="stat-row">
        <span class="stat-label">Max Position Error</span>
        <span class="stat-value ${getErrorClass(metrics.max_position_error_arcsec)}">
          ${metrics.max_position_error_arcsec !== undefined
            ? formatNumber(metrics.max_position_error_arcsec, 2) + '"'
            : '—'}
        </span>
      </div>
      <div class="stat-row">
        <span class="stat-label">Avg Rate Error</span>
        <span class="stat-value">${metrics.avg_tracking_rate_error !== undefined
          ? formatNumber(metrics.avg_tracking_rate_error, 4) + ' "/s'
          : '—'}</span>
      </div>
      <div class="stat-row">
        <span class="stat-label">Predictions</span>
        <span class="stat-value">${metrics.prediction_count !== undefined ? metrics.prediction_count : '—'}</span>
      </div>
      <div class="stat-row">
        <span class="stat-label">Prediction Accuracy</span>
        <span class="stat-value">${metrics.prediction_accuracy !== undefined
          ? formatNumber(metrics.prediction_accuracy, 2) + '"'
          : '—'}</span>
      </div>
      <div class="stat-row">
        <span class="stat-label">Earth Rotation Correction</span>
        <span class="stat-value ${metrics.earth_rotation_applied ? 'success' : ''}">
          ${metrics.earth_rotation_applied ? 'Applied' : 'Not Applied'}
        </span>
      </div>
    `;
  }

  function showStatusPlaceholder(message) {
    const container = $('#tracking-status-content');
    if (container) {
      container.innerHTML = `<div class="status-placeholder">${message}</div>`;
    }
    const badge = $('#tracking-state-badge');
    if (badge) {
      badge.className = 'status-badge idle';
      badge.textContent = '—';
    }
  }

  function showMetricsPlaceholder(message) {
    const container = $('#tracking-metrics-content');
    if (container) {
      container.innerHTML = `<div class="status-placeholder">${message}</div>`;
    }
  }

  // ─── Polling ──────────────────────────────────────────────────────────────

  /**
   * Start polling for tracking status.
   * Called when the tracking tab becomes active.
   */
  function startPolling() {
    if (pollInterval) return;
    isActive = true;
    loadTrackingStatus();
    loadTrackingMetrics();
    pollInterval = setInterval(() => {
      loadTrackingStatus();
      loadTrackingMetrics();
    }, POLL_INTERVAL_MS);
  }

  /**
   * Stop polling for tracking status.
   * Called when the tracking tab becomes hidden.
   */
  function stopPolling() {
    isActive = false;
    if (pollInterval) {
      clearInterval(pollInterval);
      pollInterval = null;
    }
  }

  // ─── Helpers ──────────────────────────────────────────────────────────────

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
   * Format time remaining (seconds) into a human-readable string.
   * @param {number} seconds
   * @returns {string}
   */
  function formatTimeRemaining(seconds) {
    if (seconds === null || seconds === undefined) return '—';
    if (seconds < 0) return '0s';
    if (seconds < 60) return `${Math.round(seconds)}s`;
    const mins = Math.floor(seconds / 60);
    const secs = Math.round(seconds % 60);
    if (mins < 60) return `${mins}m ${secs}s`;
    const hours = Math.floor(mins / 60);
    const remainMins = mins % 60;
    return `${hours}h ${remainMins}m ${secs}s`;
  }

  /**
   * Format duration in seconds into a human-readable string.
   * @param {number} seconds
   * @returns {string}
   */
  function formatDuration(seconds) {
    if (seconds === null || seconds === undefined) return '—';
    if (seconds < 60) return `${seconds}s`;
    const mins = Math.floor(seconds / 60);
    const secs = Math.round(seconds % 60);
    if (mins < 60) return `${mins}m ${secs}s`;
    const hours = Math.floor(mins / 60);
    const remainMins = mins % 60;
    return `${hours}h ${remainMins}m`;
  }

  /**
   * Escape HTML special characters.
   * @param {string} str
   * @returns {string}
   */
  function escapeHtml(str) {
    if (!str) return '';
    const div = document.createElement('div');
    div.textContent = str;
    return div.innerHTML;
  }

  // ─── Public API ───────────────────────────────────────────────────────────

  return {
    init,
    startPolling,
    stopPolling,
  };
})();
