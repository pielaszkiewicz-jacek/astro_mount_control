/**
 * Astronomical Mount Controller - Calibration Component
 *
 * Provides the UI for telescope calibration:
 * - Bootstrap calibration (initial/coarse alignment)
 * - TPOINT calibration (precise pointing model)
 * - Reference object search from the object database
 * - Add measurements with database objects
 *
 * Maps to gRPC RPCs:
 *   Bootstrap: AddBootstrapMeasurement, RunBootstrapCalibration,
 *              GetBootstrapStatus, ClearBootstrapMeasurements
 *   TPOINT:    AddTPointMeasurement, RunTPointCalibration,
 *              GetTPointParameters, ClearTPointMeasurements
 */

const CalibrationComponent = (() => {
  'use strict';

  const { $ } = Utils;

  // ─── Constants ────────────────────────────────────────────────────────────

  const BOOTSTRAP_STATE_LABELS = {
    NOT_CALIBRATED: 'Not Calibrated',
    MEASUREMENTS_COLLECTING: 'Collecting Measurements',
    CALIBRATING: 'Calibrating...',
    CALIBRATED: 'Calibrated',
    NEEDS_MORE_MEASUREMENTS: 'Needs More Measurements',
    ERROR: 'Error',
  };

  const BOOTSTRAP_BADGE_CLASSES = {
    NOT_CALIBRATED: 'idle',
    MEASUREMENTS_COLLECTING: 'slewing',
    CALIBRATING: 'slewing',
    CALIBRATED: 'tracking',
    NEEDS_MORE_MEASUREMENTS: 'idle',
    ERROR: 'error',
  };

  // ─── State ────────────────────────────────────────────────────────────────

  /** Bootstrap: currently selected reference object for measurement */
  let bsSelectedObject = null;
  /** TPOINT: currently selected reference object for measurement */
  let tpSelectedObject = null;

  // ─── Polling ─────────────────────────────────────────────────────────────

  let pollInterval = null;
  let isPolling = false;
  const POLL_INTERVAL_MS = 5000;

  // ─── Initialization ──────────────────────────────────────────────────────

  function init() {
    bindEvents();
  }

  /**
   * Toggle the calibration help instructions card.
   */
  function toggleHelp() {
    const card = $('#card-calibration-help');
    if (card) card.classList.toggle('card-collapsed');
  }

  // ─── Event Binding ───────────────────────────────────────────────────────

  function bindEvents() {
    // Calibration help toggle
    const helpToggle = $('#btn-toggle-calibration-help');
    if (helpToggle) helpToggle.addEventListener('click', toggleHelp);

    // Bootstrap buttons
    const runBsBtn = $('#btn-run-bootstrap');
    if (runBsBtn) runBsBtn.addEventListener('click', handleRunBootstrap);

    const clearBsBtn = $('#btn-clear-bootstrap');
    if (clearBsBtn) clearBsBtn.addEventListener('click', handleClearBootstrap);

    const refreshBsBtn = $('#btn-refresh-bootstrap');
    if (refreshBsBtn) refreshBsBtn.addEventListener('click', loadBootstrapStatus);

    const bsSearchBtn = $('#btn-bs-search');
    if (bsSearchBtn) bsSearchBtn.addEventListener('click', () => handleSearch('bs'));

    const bsSearchInput = $('#bs-ref-search');
    if (bsSearchInput) {
      bsSearchInput.addEventListener('keydown', (e) => {
        if (e.key === 'Enter') handleSearch('bs');
      });
    }

    const bsSlewBtn = $('#btn-bs-slew-and-measure');
    if (bsSlewBtn) bsSlewBtn.addEventListener('click', () => handleSlewAndMeasure('bs'));

    const bsAddBtn = $('#btn-bs-add-measurement');
    if (bsAddBtn) bsAddBtn.addEventListener('click', () => handleAddMeasurement('bs'));

    const bsClearBtn = $('#btn-bs-clear-selection');
    if (bsClearBtn) bsClearBtn.addEventListener('click', () => clearSelection('bs'));

    // TPOINT buttons
    const runTpBtn = $('#btn-run-tpoint');
    if (runTpBtn) runTpBtn.addEventListener('click', handleRunTPoint);

    const clearTpBtn = $('#btn-clear-tpoint');
    if (clearTpBtn) clearTpBtn.addEventListener('click', handleClearTPoint);

    const refreshTpBtn = $('#btn-refresh-tpoint');
    if (refreshTpBtn) refreshTpBtn.addEventListener('click', loadTPointStatus);

    const tpSearchBtn = $('#btn-tp-search');
    if (tpSearchBtn) tpSearchBtn.addEventListener('click', () => handleSearch('tp'));

    const tpSearchInput = $('#tp-ref-search');
    if (tpSearchInput) {
      tpSearchInput.addEventListener('keydown', (e) => {
        if (e.key === 'Enter') handleSearch('tp');
      });
    }

    const tpSlewBtn = $('#btn-tp-slew-and-measure');
    if (tpSlewBtn) tpSlewBtn.addEventListener('click', () => handleSlewAndMeasure('tp'));

    const tpAddBtn = $('#btn-tp-add-measurement');
    if (tpAddBtn) tpAddBtn.addEventListener('click', () => handleAddMeasurement('tp'));

    const tpClearBtn = $('#btn-tp-clear-selection');
    if (tpClearBtn) tpClearBtn.addEventListener('click', () => clearSelection('tp'));
  }

  // ─── Object Search (shared by both Bootstrap and TPOINT) ────────────────

  /**
   * Search the object database for reference objects.
   * @param {'bs'|'tp'} type - Which calibration type
   */
  async function handleSearch(type) {
    const prefix = type === 'bs' ? 'bs' : 'tp';
    const input = $(`#${prefix}-ref-search`);
    const resultsContainer = $(`#${prefix}-search-results`);

    if (!input || !resultsContainer) return;

    const query = input.value.trim();
    if (!query) {
      resultsContainer.innerHTML = '<div class="calibration-search-hint">Enter a name or catalog ID to search.</div>';
      return;
    }

    resultsContainer.innerHTML = '<div class="calibration-search-hint">Searching...</div>';

    try {
      const result = await Api.searchObjects({ query });
      const objects = result.objects || [];

      if (objects.length === 0) {
        resultsContainer.innerHTML = '<div class="calibration-search-hint">No objects found. Try a different search.</div>';
        return;
      }

      let html = '<div class="calibration-search-list">';
      objects.forEach(obj => {
        const ra = obj.ra_hours != null ? Number(obj.ra_hours).toFixed(4) : '—';
        const dec = obj.dec_degrees != null ? Number(obj.dec_degrees).toFixed(2) : '—';
        const mag = obj.v_magnitude != null ? Number(obj.v_magnitude).toFixed(1) : '—';
        html += `<div class="calibration-search-item" data-type="${type}" data-id="${escapeHtml(obj.id || obj.name)}">`;
        html += `<div class="calibration-search-item-name">${escapeHtml(obj.name || '(unnamed)')}`;
        if (obj.catalog_id) html += ` <span class="calibration-search-item-catalog">${escapeHtml(obj.catalog_id)}</span>`;
        html += `</div>`;
        html += `<div class="calibration-search-item-coords">RA: ${ra}h &nbsp; Dec: ${dec}° &nbsp; Mag: ${mag}</div>`;
        html += `<button class="btn btn-sm btn-primary calibration-search-item-select" data-type="${type}" data-id="${escapeHtml(obj.id || obj.name)}">Select</button>`;
        html += `</div>`;
      });
      html += '</div>';
      resultsContainer.innerHTML = html;

      // Bind "Select" buttons
      resultsContainer.querySelectorAll('.calibration-search-item-select').forEach(btn => {
        btn.addEventListener('click', (e) => {
          e.stopPropagation();
          const itemType = btn.dataset.type;
          const objId = btn.dataset.id;
          const obj = objects.find(o => (o.id || o.name) === objId);
          if (obj) selectReferenceObject(itemType, obj);
        });
      });

      // Also bind click on the item row
      resultsContainer.querySelectorAll('.calibration-search-item').forEach(item => {
        item.addEventListener('click', () => {
          const objId = item.dataset.id;
          const obj = objects.find(o => (o.id || o.name) === objId);
          if (obj) selectReferenceObject(item.dataset.type, obj);
        });
      });

    } catch (err) {
      resultsContainer.innerHTML = `<div class="calibration-search-hint" style="color:var(--color-danger);">Search failed: ${escapeHtml(err.message)}</div>`;
    }
  }

  /**
   * Select a reference object from search results and populate the measurement form.
   * @param {'bs'|'tp'} type
   * @param {object} obj - Astronomical object from the database
   */
  function selectReferenceObject(type, obj) {
    const prefix = type === 'bs' ? 'bs' : 'tp';

    if (type === 'bs') {
      bsSelectedObject = obj;
    } else {
      tpSelectedObject = obj;
    }

    // Show the measurement form
    const form = $(`#${prefix}-measurement-form`);
    if (form) form.style.display = 'block';

    // Set selected object name
    const nameEl = $(`#${prefix}-selected-name`);
    if (nameEl) nameEl.textContent = obj.name || '(unnamed)';

    // Auto-fill coordinates
    const raInput = $(`#${prefix}-expected-ra`);
    const decInput = $(`#${prefix}-expected-dec`);
    if (raInput && obj.ra_hours != null) raInput.value = obj.ra_hours;
    if (decInput && obj.dec_degrees != null) decInput.value = obj.dec_degrees;

    // Clear previous result
    const resultDiv = $(`#${prefix}-measurement-result`);
    if (resultDiv) {
      resultDiv.style.display = 'none';
      resultDiv.textContent = '';
    }

    // Scroll the form into view
    if (form) form.scrollIntoView({ behavior: 'smooth', block: 'nearest' });
  }

  /**
   * Clear the current selection for a calibration type.
   * @param {'bs'|'tp'} type
   */
  function clearSelection(type) {
    const prefix = type === 'bs' ? 'bs' : 'tp';

    if (type === 'bs') {
      bsSelectedObject = null;
    } else {
      tpSelectedObject = null;
    }

    const form = $(`#${prefix}-measurement-form`);
    if (form) form.style.display = 'none';

    const nameEl = $(`#${prefix}-selected-name`);
    if (nameEl) nameEl.textContent = '—';

    const raInput = $(`#${prefix}-expected-ra`);
    const decInput = $(`#${prefix}-expected-dec`);
    if (raInput) raInput.value = '';
    if (decInput) decInput.value = '';

    const resultDiv = $(`#${prefix}-measurement-result`);
    if (resultDiv) {
      resultDiv.style.display = 'none';
      resultDiv.textContent = '';
    }

    // Close the details section
    const section = $(`#${prefix}-measurement-section`);
    if (section) section.removeAttribute('open');
  }

  // ─── Slew and Add Measurement ────────────────────────────────────────────

  /**
   * Slew to the selected object, then add a measurement.
   * The mount records the actual position after slewing, and we send
   * the expected coordinates from the database object.
   * @param {'bs'|'tp'} type
   */
  async function handleSlewAndMeasure(type) {
    const obj = type === 'bs' ? bsSelectedObject : tpSelectedObject;
    if (!obj) {
      App.showToast('No reference object selected', 'error');
      return;
    }

    const ra = obj.ra_hours;
    const dec = obj.dec_degrees;
    if (ra == null || dec == null) {
      App.showToast('Selected object has no coordinates', 'error');
      return;
    }

    // First slew to the object
    try {
      await Api.slewToCoordinates(ra, dec);
      App.showToast(`Slewing to ${obj.name || 'object'}...`, 'info', 3000);

      // Wait a moment for the slew to start, then add measurement
      setTimeout(async () => {
        await addMeasurement(type, obj, ra, dec);
      }, 2000);
    } catch (err) {
      App.showToast(`Slew failed: ${err.message}`, 'error');
    }
  }

  /**
   * Add a measurement directly without slewing (uses current mount position).
   * @param {'bs'|'tp'} type
   */
  async function handleAddMeasurement(type) {
    const obj = type === 'bs' ? bsSelectedObject : tpSelectedObject;
    if (!obj) {
      App.showToast('No reference object selected', 'error');
      return;
    }

    const prefix = type === 'bs' ? 'bs' : 'tp';
    const raInput = $(`#${prefix}-expected-ra`);
    const decInput = $(`#${prefix}-expected-dec`);

    const ra = raInput ? parseFloat(raInput.value) : obj.ra_hours;
    const dec = decInput ? parseFloat(decInput.value) : obj.dec_degrees;

    if (isNaN(ra) || isNaN(dec)) {
      App.showToast('Valid expected coordinates are required', 'error');
      return;
    }

    await addMeasurement(type, obj, ra, dec);
  }

  /**
   * Send a measurement to the backend.
   * @param {'bs'|'tp'} type
   * @param {object} obj - The reference object
   * @param {number} ra - Expected RA in hours
   * @param {number} dec - Expected Dec in degrees
   */
  async function addMeasurement(type, obj, ra, dec) {
    const prefix = type === 'bs' ? 'bs' : 'tp';
    const resultDiv = $(`#${prefix}-measurement-result`);
    if (!resultDiv) return;

    resultDiv.style.display = 'none';

    const measurement = {
      object_id: obj.id || obj.name,
      object_name: obj.name || '',
      expected: {
        ra: ra,
        dec: dec,
      },
    };

    try {
      if (type === 'bs') {
        await Api.addBootstrapMeasurement(measurement);
      } else {
        await Api.addTPointMeasurement(measurement);
      }

      resultDiv.className = 'calibration-result success';
      resultDiv.textContent = `Measurement added for "${obj.name || 'object'}" (RA=${Number(ra).toFixed(4)}h, Dec=${Number(dec).toFixed(2)}°)`;
      resultDiv.style.display = 'block';

      App.showToast(`Measurement added for ${obj.name || 'object'}`, 'success');

      // Refresh status
      if (type === 'bs') {
        await loadBootstrapStatus();
      } else {
        await loadTPointStatus();
      }
    } catch (err) {
      resultDiv.className = 'calibration-result error';
      resultDiv.textContent = `Failed to add measurement: ${err.message}`;
      resultDiv.style.display = 'block';
      App.showToast(`Measurement failed: ${err.message}`, 'error');
    }
  }

  // ─── Bootstrap Calibration ───────────────────────────────────────────────

  async function loadBootstrapStatus() {
    const resultDiv = $('#bootstrap-result');
    try {
      const status = await Api.getBootstrapStatus();
      updateBootstrapUI(status);
      if (resultDiv) resultDiv.style.display = 'none';
    } catch (err) {
      if (resultDiv) {
        resultDiv.textContent = `Failed to load status: ${err.message}`;
        resultDiv.className = 'calibration-result error';
        resultDiv.style.display = 'block';
      }
    }
  }

  function updateBootstrapUI(status) {
    const stateLabel = status.state || 'NOT_CALIBRATED';
    const stateText = BOOTSTRAP_STATE_LABELS[stateLabel] || stateLabel;

    // Badge
    const badge = $('#bootstrap-status-badge');
    if (badge) {
      badge.textContent = stateText.toUpperCase();
      const cls = BOOTSTRAP_BADGE_CLASSES[stateLabel] || 'idle';
      badge.className = `status-badge ${cls}`;
    }

    // Stats
    setText('#bootstrap-state', stateText);
    setText('#bootstrap-measurement-count', String(status.measurement_count ?? 0));
    setText('#bootstrap-alignment-error', formatArcsec(status.current_alignment_error_arcsec));
    setText('#bootstrap-residual-rms', status.residual_rms_arcsec != null ? `${Number(status.residual_rms_arcsec).toFixed(2)}"` : '—');
    setText('#bootstrap-tpoint-ready', status.ready_for_tpoint ? 'Yes' : 'No');
    setText('#bootstrap-last-calibration', status.last_calibration ? formatTimestamp(status.last_calibration) : '—');
  }

  async function handleRunBootstrap() {
    const btn = $('#btn-run-bootstrap');
    const resultDiv = $('#bootstrap-result');
    if (!btn || !resultDiv) return;

    btn.disabled = true;
    resultDiv.style.display = 'none';

    try {
      const result = await Api.runBootstrapCalibration();
      resultDiv.className = 'calibration-result success';
      resultDiv.style.display = 'block';

      let msg = 'Bootstrap calibration completed.';
      if (result.alignment_error_arcsec != null) {
        msg += ` Alignment error: ${Number(result.alignment_error_arcsec).toFixed(2)}"`;
      }
      if (result.residual_rms_arcsec != null) {
        msg += ` RMS residual: ${Number(result.residual_rms_arcsec).toFixed(2)}"`;
      }
      if (result.measurement_count != null) {
        msg += ` (${result.measurement_count} measurements)`;
      }
      resultDiv.textContent = msg;

      await loadBootstrapStatus();
    } catch (err) {
      resultDiv.className = 'calibration-result error';
      resultDiv.textContent = `Calibration failed: ${err.message}`;
      resultDiv.style.display = 'block';
    } finally {
      btn.disabled = false;
    }
  }

  async function handleClearBootstrap() {
    const btn = $('#btn-clear-bootstrap');
    const resultDiv = $('#bootstrap-result');
    if (!btn || !resultDiv) return;

    btn.disabled = true;
    resultDiv.style.display = 'none';

    try {
      await Api.clearBootstrapMeasurements();
      resultDiv.className = 'calibration-result success';
      resultDiv.textContent = 'Bootstrap measurements cleared.';
      resultDiv.style.display = 'block';
      await loadBootstrapStatus();
    } catch (err) {
      resultDiv.className = 'calibration-result error';
      resultDiv.textContent = `Failed to clear: ${err.message}`;
      resultDiv.style.display = 'block';
    } finally {
      btn.disabled = false;
    }
  }

  // ─── TPOINT Calibration ──────────────────────────────────────────────────

  async function loadTPointStatus() {
    const resultDiv = $('#tpoint-result');
    try {
      const params = await Api.getTPointParameters();
      updateTPointUI(params);
      if (resultDiv) resultDiv.style.display = 'none';
    } catch (err) {
      if (resultDiv) {
        resultDiv.textContent = `Failed to load TPOINT status: ${err.message}`;
        resultDiv.className = 'calibration-result error';
        resultDiv.style.display = 'block';
      }
    }
  }

  function updateTPointUI(params) {
    const badge = $('#tpoint-status-badge');
    if (badge) {
      const isCalibrated = params.calibrated;
      badge.textContent = isCalibrated ? 'CALIBRATED' : 'INACTIVE';
      badge.className = isCalibrated ? 'status-badge tracking' : 'status-badge idle';
    }

    setText('#tpoint-measurement-count', String(params.measurement_count ?? params.coefficients?.length ?? 0));
    setText('#tpoint-residual-rms', formatArcsec(params.residual_rms));
    setText('#tpoint-max-residual', formatArcsec(params.residual_max));
    setText('#tpoint-chi-squared', params.chi_squared != null ? Number(params.chi_squared).toFixed(3) : '—');
    setText('#tpoint-last-update', params.last_update ? formatTimestamp(params.last_update) : '—');
    setText('#tpoint-calibrated', params.calibrated ? 'Yes' : 'No');
  }

  async function handleRunTPoint() {
    const btn = $('#btn-run-tpoint');
    const resultDiv = $('#tpoint-result');
    if (!btn || !resultDiv) return;

    btn.disabled = true;
    resultDiv.style.display = 'none';

    try {
      await Api.runTPointCalibration();
      resultDiv.className = 'calibration-result success';
      resultDiv.textContent = 'TPOINT calibration completed successfully.';
      resultDiv.style.display = 'block';
      await loadTPointStatus();
    } catch (err) {
      resultDiv.className = 'calibration-result error';
      resultDiv.textContent = `TPOINT calibration failed: ${err.message}`;
      resultDiv.style.display = 'block';
    } finally {
      btn.disabled = false;
    }
  }

  async function handleClearTPoint() {
    const btn = $('#btn-clear-tpoint');
    const resultDiv = $('#tpoint-result');
    if (!btn || !resultDiv) return;

    btn.disabled = true;
    resultDiv.style.display = 'none';

    try {
      await Api.clearTPointMeasurements();
      resultDiv.className = 'calibration-result success';
      resultDiv.textContent = 'TPOINT measurements cleared.';
      resultDiv.style.display = 'block';
      await loadTPointStatus();
    } catch (err) {
      resultDiv.className = 'calibration-result error';
      resultDiv.textContent = `Failed to clear: ${err.message}`;
      resultDiv.style.display = 'block';
    } finally {
      btn.disabled = false;
    }
  }

  // ─── Polling ─────────────────────────────────────────────────────────────

  /**
   * Start polling for calibration status updates.
   * Safe to call multiple times — only one interval runs at a time.
   */
  function startPolling() {
    if (isPolling) return;
    isPolling = true;

    // Initial load
    loadBootstrapStatus();
    loadTPointStatus();

    // Periodic refresh
    pollInterval = setInterval(() => {
      loadBootstrapStatus();
      loadTPointStatus();
    }, POLL_INTERVAL_MS);
  }

  /**
   * Stop polling for calibration status updates.
   */
  function stopPolling() {
    isPolling = false;
    if (pollInterval) {
      clearInterval(pollInterval);
      pollInterval = null;
    }
  }

  // ─── Helpers ─────────────────────────────────────────────────────────────

  function setText(id, value) {
    const el = $(id);
    if (el) el.textContent = value;
  }

  function formatArcsec(value) {
    if (value == null) return '—';
    return `${Number(value).toFixed(2)}"`;
  }

  function formatTimestamp(ts) {
    if (!ts) return '—';
    try {
      const date = new Date(ts.seconds ? ts.seconds * 1000 : ts);
      if (isNaN(date.getTime())) return '—';
      return date.toLocaleString();
    } catch {
      return '—';
    }
  }

  function escapeHtml(str) {
    if (!str) return '';
    const div = document.createElement('div');
    div.textContent = str;
    return div.innerHTML;
  }

  // ─── Public API ──────────────────────────────────────────────────────────

  return {
    init,
    startPolling,
    stopPolling,
    loadBootstrapStatus,
    loadTPointStatus,
    toggleHelp,
  };
})();
