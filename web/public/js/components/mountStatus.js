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

  // Cache for database object details (keyed by object name)
  let _dbObjectCache = {};
  let _lastTrackedName = null;

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
  return { render };
})();
