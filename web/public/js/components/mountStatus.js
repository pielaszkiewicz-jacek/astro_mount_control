/**
 * Astronomical Mount Controller - Mount Status Component
 *
 * Renders the mount status card with real-time state information.
 * This is a framework component — extend with additional data displays
 * by adding new render functions and calling them in render().
 */

const MountStatusComponent = (() => {
  'use strict';

  const { $, formatNumber, formatRA, formatDec, formatAngleDeg } = Utils;

  // Cache for database object details (keyed by object name)
  let _dbObjectCache = {};
  let _lastTrackedName = null;

  // ─── Velocity Chart State ──────────────────────────────────────────────
  const CHART_MAX_POINTS = 60; // Rolling window: ~60 seconds at 1 Hz poll
  let _velDataAxis1 = [];       // Array of { time: Date, value: number }
  let _velDataAxis2 = [];
  let _chartInitialized = false;

  /**
   * Initialize the velocity chart canvas and data buffers.
   */
  function initChart() {
    if (_chartInitialized) return;
    _chartInitialized = true;
    _velDataAxis1 = [];
    _velDataAxis2 = [];
  }

  /**
   * Append velocity readings to the rolling buffer and render the chart.
   * @param {number} rate1 - Axis 1 velocity (arcsec/s)
   * @param {number} rate2 - Axis 2 velocity (arcsec/s)
   */
  function updateVelocityChart(rate1, rate2) {
    const canvas = $('#velocity-canvas');
    const container = $('#velocity-chart-content');
    if (!canvas || !container) return;

    // Initialize if first call
    if (!_chartInitialized) initChart();

    const now = Date.now();

    // Append new data points
    _velDataAxis1.push({ time: now, value: rate1 ?? 0 });
    _velDataAxis2.push({ time: now, value: rate2 ?? 0 });

    // Trim to rolling window
    while (_velDataAxis1.length > CHART_MAX_POINTS) _velDataAxis1.shift();
    while (_velDataAxis2.length > CHART_MAX_POINTS) _velDataAxis2.shift();

    // Always draw the chart, even with zero data — the drawChart function
    // handles empty/single-point datasets gracefully. Also removes the
    // "Waiting for data..." placeholder so the canvas is visible.
    const placeholder = container.querySelector('.status-placeholder');
    if (placeholder) placeholder.style.display = 'none';
    container.classList.remove('empty');
    drawChart(canvas);
  }

  /**
   * Redraw the velocity chart with existing buffered data.
   * Called when the Status tab becomes visible after being hidden,
   * to restore the chart that was skipped by drawChart's zero-dimension guard.
   */
  function redrawVelocityChart() {
    const canvas = $('#velocity-canvas');
    const container = $('#velocity-chart-content');
    if (!canvas || !container) return;
    if (_velDataAxis1.length === 0 && _velDataAxis2.length === 0) return;
    const placeholder = container.querySelector('.status-placeholder');
    if (placeholder) placeholder.style.display = 'none';
    container.classList.remove('empty');
    drawChart(canvas);
  }

  /**
   * Apply exponential moving average smoothing to a data series.
   * @param {Array<{time: number, value: number}>} data
   * @param {number} alpha - Smoothing factor (0–1, higher = less smoothing)
   * @returns {Array<{time: number, value: number}>} Smoothed data (same timestamps)
   */
  function smoothData(data, alpha) {
    if (!data || data.length < 2) return data;
    const smoothed = [{ time: data[0].time, value: data[0].value }];
    for (let i = 1; i < data.length; i++) {
      const prev = smoothed[i - 1].value;
      const curr = data[i].value;
      smoothed.push({
        time: data[i].time,
        value: alpha * curr + (1 - alpha) * prev
      });
    }
    return smoothed;
  }

  /**
   * Draw the velocity time-series chart on the canvas.
   * @param {HTMLCanvasElement} canvas
   */
  function drawChart(canvas) {
    const ctx = canvas.getContext('2d');
    const dpr = window.devicePixelRatio || 1;

    // Set canvas physical size to match CSS size * DPR
    const rect = canvas.getBoundingClientRect();
    const w = rect.width;
    const h = rect.height;

    // Skip drawing when canvas is not visible (e.g., Status tab hidden).
    // Setting canvas.width/height to 0 permanently shrinks the internal
    // resolution, so future draw calls would still produce blank output
    // even after the tab becomes visible again.
    if (w <= 0 || h <= 0) return;

    canvas.width = w * dpr;
    canvas.height = h * dpr;
    ctx.scale(dpr, dpr);

    // Chart margins
    const margin = { top: 8, right: 44, bottom: 22, left: 48 };
    const plotW = w - margin.left - margin.right;
    const plotH = h - margin.top - margin.bottom;

    // Smooth data for display (raw data preserved in _velDataAxis*)
    const EMA_ALPHA = 0.35;  // moderate smoothing — still responsive
    const smoothAxis1 = smoothData(_velDataAxis1, EMA_ALPHA);
    const smoothAxis2 = smoothData(_velDataAxis2, EMA_ALPHA);

    // Clear
    ctx.clearRect(0, 0, w, h);

    // Background
    ctx.fillStyle = '#0f0f1a';
    ctx.fillRect(0, 0, w, h);

    // Compute Y range from smoothed data (with padding for readability)
    let yMin = Infinity, yMax = -Infinity;
    const allData = smoothAxis1.concat(smoothAxis2);
    for (const pt of allData) {
      if (pt.value < yMin) yMin = pt.value;
      if (pt.value > yMax) yMax = pt.value;
    }
    // Ensure we have some range even with zero velocities
    if (yMax - yMin < 0.001) { yMin -= 0.1; yMax += 0.1; }
    // Add 10% padding
    const yRange = yMax - yMin;
    yMin -= yRange * 0.1;
    yMax += yRange * 0.1;

    // Compute X range from timestamps
    const now = Date.now();
    const xMin = now - (CHART_MAX_POINTS - 1) * 1000; // approximate
    // Use actual data range
    const tMin = smoothAxis1.length > 0 ? smoothAxis1[0].time : xMin;
    const tMax = smoothAxis1.length > 0 ? smoothAxis1[smoothAxis1.length - 1].time : now;
    const xRange = Math.max(tMax - tMin, 1000); // minimum 1 second range

    // Helper: map data space to pixel space
    function xPixel(t) {
      return margin.left + ((t - tMin) / xRange) * plotW;
    }
    function yPixel(v) {
      return margin.top + plotH - ((v - yMin) / (yMax - yMin)) * plotH;
    }

    // ── Grid lines ──
    ctx.strokeStyle = 'rgba(255,255,255,0.06)';
    ctx.lineWidth = 0.5;
    const yTicks = 5;
    for (let i = 0; i <= yTicks; i++) {
      const y = margin.top + (plotH / yTicks) * i;
      ctx.beginPath();
      ctx.moveTo(margin.left, y);
      ctx.lineTo(margin.left + plotW, y);
      ctx.stroke();
    }

    // ── Y-axis labels ──
    ctx.fillStyle = '#888';
    ctx.font = '9px monospace';
    ctx.textAlign = 'right';
    for (let i = 0; i <= yTicks; i++) {
      const val = yMax - (yRange / yTicks) * i;
      const y = margin.top + (plotH / yTicks) * i;
      ctx.fillText(val.toFixed(2), margin.left - 4, y + 3);
    }
    // Y-axis unit label
    ctx.fillStyle = '#666';
    ctx.textAlign = 'center';
    ctx.save();
    ctx.translate(10, margin.top + plotH / 2);
    ctx.rotate(-Math.PI / 2);
    ctx.fillText('"/s', 0, 0);
    ctx.restore();

    // ── X-axis time labels ──
    ctx.fillStyle = '#888';
    ctx.textAlign = 'center';
    const xTicks = 3;
    for (let i = 0; i <= xTicks; i++) {
      const t = tMin + (xRange / xTicks) * i;
      const x = xPixel(t);
      const d = new Date(t);
      const label = d.getHours().toString().padStart(2, '0') + ':'
                  + d.getMinutes().toString().padStart(2, '0') + ':'
                  + d.getSeconds().toString().padStart(2, '0');
      ctx.fillText(label, x, margin.top + plotH + 14);
    }

    // ── Draw Axis 1 (blue) ──
    if (smoothAxis1.length === 1) {
      // Single point: draw a dot
      ctx.fillStyle = '#4fc3f7';
      const x = xPixel(smoothAxis1[0].time);
      const y = yPixel(smoothAxis1[0].value);
      ctx.beginPath();
      ctx.arc(x, y, 3, 0, Math.PI * 2);
      ctx.fill();
    } else if (smoothAxis1.length > 1) {
      ctx.strokeStyle = '#4fc3f7';
      ctx.lineWidth = 1.5;
      ctx.lineJoin = 'round';
      ctx.beginPath();
      for (let i = 0; i < smoothAxis1.length; i++) {
        const x = xPixel(smoothAxis1[i].time);
        const y = yPixel(smoothAxis1[i].value);
        if (i === 0) ctx.moveTo(x, y);
        else ctx.lineTo(x, y);
      }
      ctx.stroke();
    }

    // ── Draw Axis 2 (green) ──
    if (smoothAxis2.length === 1) {
      ctx.fillStyle = '#81c784';
      const x = xPixel(smoothAxis2[0].time);
      const y = yPixel(smoothAxis2[0].value);
      ctx.beginPath();
      ctx.arc(x, y, 3, 0, Math.PI * 2);
      ctx.fill();
    } else if (smoothAxis2.length > 1) {
      ctx.strokeStyle = '#81c784';
      ctx.lineWidth = 1.5;
      ctx.lineJoin = 'round';
      ctx.beginPath();
      for (let i = 0; i < smoothAxis2.length; i++) {
        const x = xPixel(smoothAxis2[i].time);
        const y = yPixel(smoothAxis2[i].value);
        if (i === 0) ctx.moveTo(x, y);
        else ctx.lineTo(x, y);
      }
      ctx.stroke();
    }

    // ── Axis border ──
    ctx.strokeStyle = 'rgba(255,255,255,0.15)';
    ctx.lineWidth = 0.5;
    ctx.strokeRect(margin.left, margin.top, plotW, plotH);
  }

  /**
   * Format database object type name.
   * @param {string} type
   * @returns {string}
   */
  function formatTypeName(type) {
    if (!type) return '—';
    const map = {
      'STAR': 'Star',
      'GALAXY': 'Galaxy',
      'NEBULA': 'Nebula',
      'OPEN_CLUSTER': 'Open Cluster',
      'GLOBULAR_CLUSTER': 'Globular Cluster',
      'PLANETARY_NEBULA': 'Planetary Nebula',
      'ASTEROID': 'Asteroid',
      'COMET': 'Comet',
      'PLANET': 'Planet',
      'SATELLITE': 'Satellite',
      'OTHER': 'Other',
    };
    return map[type] || type;
  }

  /**
   * Fetch full database object details when tracking a DB object.
   * Caches by name to avoid repeated lookups.
   * @param {string} objectName
   */
  async function fetchDbObjectDetails(objectName) {
    if (!objectName || _dbObjectCache[objectName]) return;

    try {
      // Try to get by name via search
      const result = await Api.searchObjects({ name: objectName, limit: 1 });
      if (result && result.objects && result.objects.length > 0) {
        const obj = result.objects[0];
        _dbObjectCache[objectName] = {
          type: obj.object_type,
          vMag: obj.v_magnitude,
          catalogName: obj.catalog_name,
          catalogId: obj.catalog_id,
          constellation: obj.custom_fields?.constellation,
          spectralType: obj.spectral_type,
          distanceLy: obj.distance_ly,
        };
        // Re-render the tracking card with DB details
        updateTrackingCardWithDbInfo(objectName);
      } else {
        // Mark as not found in DB to avoid repeated lookups
        _dbObjectCache[objectName] = null;
      }
    } catch (e) {
      // Silently fail — DB may be unavailable
      _dbObjectCache[objectName] = null;
    }
  }

  /**
   * Update the tracking card to show cached DB object details.
   * @param {string} objectName
   */
  function updateTrackingCardWithDbInfo(objectName) {
    const trackEl = $('#tracking-content');
    if (!trackEl) return;

    const dbInfo = _dbObjectCache[objectName];
    if (!dbInfo) return;

    // Append DB details after existing content
    const dbSection = document.createElement('div');
    dbSection.id = 'tracking-db-info';
    dbSection.style.cssText = 'margin-top:8px; padding-top:8px; border-top:1px solid var(--border-color, #444);';

    let html = '<div style="font-size:0.85em; opacity:0.85; margin-bottom:4px;">Database Object</div>';
    if (dbInfo.type) {
      html += `<div class="stat-row"><span class="stat-label">Type</span><span class="stat-value">${formatTypeName(dbInfo.type)}</span></div>`;
    }
    if (dbInfo.catalogName && dbInfo.catalogId) {
      html += `<div class="stat-row"><span class="stat-label">Catalog</span><span class="stat-value">${dbInfo.catalogName} #${dbInfo.catalogId}</span></div>`;
    }
    if (dbInfo.vMag !== undefined && dbInfo.vMag !== null) {
      html += `<div class="stat-row"><span class="stat-label">V Magnitude</span><span class="stat-value">${formatNumber(dbInfo.vMag, 2)}</span></div>`;
    }
    if (dbInfo.constellation) {
      html += `<div class="stat-row"><span class="stat-label">Constellation</span><span class="stat-value">${dbInfo.constellation}</span></div>`;
    }
    if (dbInfo.spectralType) {
      html += `<div class="stat-row"><span class="stat-label">Spectral Type</span><span class="stat-value">${dbInfo.spectralType}</span></div>`;
    }
    if (dbInfo.distanceLy) {
      html += `<div class="stat-row"><span class="stat-label">Distance</span><span class="stat-value">${formatNumber(dbInfo.distanceLy, 1)} ly</span></div>`;
    }

    dbSection.innerHTML = html;
    trackEl.appendChild(dbSection);
  }

  /**
   * Remove the DB info section from the tracking card.
   */
  function removeDbInfoSection() {
    const existing = $('#tracking-db-info');
    if (existing) existing.remove();
  }

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

    // Update velocity chart with commanded tracking rates (already in arcsec/s).
    // Using the commanded rate (tracking_rate_*) instead of the raw CANopen
    // velocity (actual_rate_*) avoids oscillations: in position mode the drive
    // stops between updates, causing actual_rate to flicker 0 ↔ small values.
    updateVelocityChart(
      state.tracking_rate_ra ?? 0,
      state.tracking_rate_dec ?? 0
    );

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
        <div class="stat-section-label">Servo (motor shaft)</div>
        <div class="stat-row">
          <span class="stat-label">Axis 1</span>
          <span class="stat-value highlight">${formatAngleDeg(state.position?.axis1, false)}</span>
        </div>
        <div class="stat-row">
          <span class="stat-label">Axis 2</span>
          <span class="stat-value highlight">${formatAngleDeg(state.position?.axis2, false)}</span>
        </div>
        <div class="stat-section-label">Telescope (after gear ratio)</div>
        <div class="stat-row">
          <span class="stat-label">Axis 1</span>
          <span class="stat-value highlight">${formatAngleDeg(state.telescope?.axis1, false)}</span>
        </div>
        <div class="stat-row">
          <span class="stat-label">Axis 2</span>
          <span class="stat-value highlight">${formatAngleDeg(state.telescope?.axis2, false)}</span>
        </div>
        <div class="stat-section-label">Tracking</div>
        <div class="stat-row">
          <span class="stat-label">Rate RA</span>
          <span class="stat-value">${formatNumber(state.tracking_rate_ra, 4)} "/s</span>
        </div>
        <div class="stat-row">
          <span class="stat-label">Rate Dec</span>
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
      const objectName = tracked ? (tracked.name || '').trim() : '';

      // Track object name changes to manage DB info section
      if (_lastTrackedName !== objectName) {
        removeDbInfoSection();
        _lastTrackedName = objectName;
        // If tracking a new object, trigger async DB lookup
        if (objectName && !_dbObjectCache[objectName]) {
          fetchDbObjectDetails(objectName);
        }
      }

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

      // If DB info is already cached, append it immediately
      if (objectName && _dbObjectCache[objectName]) {
        updateTrackingCardWithDbInfo(objectName);
      }
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
  return { render, redrawVelocityChart };
})();
