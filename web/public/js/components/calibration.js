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

  function bsStateLabel(state) {
    return I18n.t('cal.bs_state.' + state, state);
  }

  const BOOTSTRAP_BADGE_CLASSES = {
    NOT_CALIBRATED: 'idle',
    MEASUREMENTS_COLLECTING: 'slewing',
    CALIBRATING: 'slewing',
    CALIBRATED: 'tracking',
    NEEDS_MORE_MEASUREMENTS: 'idle',
    ERROR: 'error',
  };

  function getBootstrapModeLabel(mode) {
    const key = mode === 0 ? 'cal.mode_manual' : (mode === 1 ? 'cal.mode_hybrid' : (mode === 2 ? 'cal.mode_automatic' : null));
    return key ? I18n.t(key, String(mode)) : String(mode);
  }

  function getAutoBootstrapStateLabel(state) {
    return I18n.t('cal.auto_state.' + state, String(state));
  }

  // ─── State ────────────────────────────────────────────────────────────────

  /** Bootstrap: currently selected reference object for measurement */
  let bsSelectedObject = null;
  /** TPOINT: currently selected reference object for measurement */
  let tpSelectedObject = null;
  /** Current bootstrap mode value (0=MANUAL, 1=HYBRID, 2=AUTOMATIC) */
  let currentBootstrapMode = 0;

  // ─── Polling ─────────────────────────────────────────────────────────────

  let pollInterval = null;
  let isPolling = false;
  const POLL_INTERVAL_MS = 5000;

  // ─── Initialization ──────────────────────────────────────────────────────

  function init() {
    bindEvents();
    buildHelpContent();
    // Re-render help content when language changes
    document.addEventListener('i18n:applied', buildHelpContent);
  }

  /**
   * Build the calibration help section using translated HTML from i18n dictionary.
   * The help content contains rich HTML (strong, kbd, em, ol, li, etc.) that
   * cannot be handled by simple data-i18n textContent replacements.
   */
  function buildHelpContent() {
    const container = $('#calibration-help-content');
    if (!container) return;

    const html = `
      <p>${I18n.t('cal.help_intro')}</p>

      <details class="calibration-help-step" open>
        <summary class="calibration-help-step-summary">
          <span class="calibration-help-step-number">1</span>
          <span>${I18n.t('cal.step1_title')}</span>
        </summary>
        <div class="calibration-help-step-body">
          <ol>
            <li>${I18n.t('cal.step1_li1')}</li>
            <li>${I18n.t('cal.step1_li2')}</li>
            <li>${I18n.t('cal.step1_li3')}</li>
            <li>${I18n.t('cal.step1_li4')}</li>
            <li>${I18n.t('cal.step1_li5')}</li>
          </ol>
        </div>
      </details>

      <details class="calibration-help-step">
        <summary class="calibration-help-step-summary">
          <span class="calibration-help-step-number">2</span>
          <span>${I18n.t('cal.step2_title')}</span>
        </summary>
        <div class="calibration-help-step-body">
          <p>${I18n.t('cal.step2_p1')}</p>
          <p>${I18n.t('cal.step2_p2')}</p>
        </div>
      </details>

      <details class="calibration-help-step">
        <summary class="calibration-help-step-summary">
          <span class="calibration-help-step-number">3</span>
          <span>${I18n.t('cal.step3_title')}</span>
        </summary>
        <div class="calibration-help-step-body">
          <ol>
            <li>${I18n.t('cal.step3_li1')}</li>
            <li>${I18n.t('cal.step3_li2')}</li>
            <li>${I18n.t('cal.step3_li3')}</li>
          </ol>
        </div>
      </details>

      <details class="calibration-help-step">
        <summary class="calibration-help-step-summary">
          <span class="calibration-help-step-number">4</span>
          <span>${I18n.t('cal.step4_title')}</span>
        </summary>
        <div class="calibration-help-step-body">
          <p>${I18n.t('cal.step4_p1')}</p>
          <ul>
            <li>${I18n.t('cal.step4_li1')}</li>
            <li>${I18n.t('cal.step4_li2')}</li>
            <li>${I18n.t('cal.step4_li3')}</li>
          </ul>
        </div>
      </details>

      <details class="calibration-help-step">
        <summary class="calibration-help-step-summary">
          <span class="calibration-help-step-number">5</span>
          <span>${I18n.t('cal.step5_title')}</span>
        </summary>
        <div class="calibration-help-step-body">
          <ul>
            <li>${I18n.t('cal.step5_li1')}</li>
            <li>${I18n.t('cal.step5_li2')}</li>
            <li>${I18n.t('cal.step5_li3')}</li>
            <li>${I18n.t('cal.step5_li4')}</li>
            <li>${I18n.t('cal.step5_li5')}</li>
          </ul>
        </div>
      </details>`;

    container.innerHTML = html;
  }

  /**
   * Toggle the calibration help instructions card.
   */
  function toggleHelp() {
    const card = $('#card-calibration-help');
    const btn = $('#btn-toggle-calibration-help');
    if (!card) return;
    const collapsed = card.classList.toggle('card-collapsed');
    if (btn) btn.textContent = collapsed ? '+' : '\u2212';
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

    // Bootstrap Mode selector
    const setModeBtn = $('#btn-set-bootstrap-mode');
    if (setModeBtn) setModeBtn.addEventListener('click', handleSetBootstrapMode);

    // Auto-bootstrap
    const runAutoBsBtn = $('#btn-run-auto-bootstrap');
    if (runAutoBsBtn) runAutoBsBtn.addEventListener('click', handleRunAutoBootstrap);

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
      resultsContainer.innerHTML = `<div class="calibration-search-hint">${I18n.t('cal.search.hint')}</div>`;
      return;
    }

    resultsContainer.innerHTML = `<div class="calibration-search-hint">${I18n.t('cal.search.searching')}</div>`;

    try {
      const result = await Api.searchObjects({ query });
      const objects = result.objects || [];

      if (objects.length === 0) {
        resultsContainer.innerHTML = `<div class="calibration-search-hint">${I18n.t('cal.search.no_results')}</div>`;
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
        html += `<button class="btn btn-sm btn-primary calibration-search-item-select" data-type="${type}" data-id="${escapeHtml(obj.id || obj.name)}">${I18n.t('cal.search.select_btn')}</button>`;
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
      resultsContainer.innerHTML = `<div class="calibration-search-hint" style="color:var(--color-danger);">${I18n.t('cal.search.error', { message: escapeHtml(err.message) })}</div>`;
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

    // Auto-fill coordinates — use setAngleDecimal for enhanced inputs
    const raInput = $(`#${prefix}-expected-ra`);
    const decInput = $(`#${prefix}-expected-dec`);
    if (raInput && obj.ra_hours != null) {
      if (raInput.setAngleDecimal) {
        raInput.setAngleDecimal(obj.ra_hours);
      } else {
        raInput.value = obj.ra_hours;
      }
    }
    if (decInput && obj.dec_degrees != null) {
      if (decInput.setAngleDecimal) {
        decInput.setAngleDecimal(obj.dec_degrees);
      } else {
        decInput.value = obj.dec_degrees;
      }
    }

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
      App.showToast(I18n.t('cal.msg.no_ref_selected'), 'error');
      return;
    }

    const ra = obj.ra_hours;
    const dec = obj.dec_degrees;
    if (ra == null || dec == null) {
      App.showToast(I18n.t('cal.msg.no_coords'), 'error');
      return;
    }

    // First slew to the object
    try {
      await Api.slewToCoordinates(ra, dec);
      App.showToast(I18n.t('cal.msg.slewing_to', { name: obj.name || 'object' }), 'info', 3000);

      // Wait a moment for the slew to start, then add measurement
      setTimeout(async () => {
        await addMeasurement(type, obj, ra, dec);
      }, 2000);
    } catch (err) {
      App.showToast(I18n.t('cal.msg.slew_failed', { message: err.message }), 'error');
    }
  }

  /**
   * Add a measurement directly without slewing (uses current mount position).
   * @param {'bs'|'tp'} type
   */
  async function handleAddMeasurement(type) {
    const obj = type === 'bs' ? bsSelectedObject : tpSelectedObject;
    if (!obj) {
      App.showToast(I18n.t('cal.msg.no_ref_selected'), 'error');
      return;
    }

    const prefix = type === 'bs' ? 'bs' : 'tp';
    const raInput = $(`#${prefix}-expected-ra`);
    const decInput = $(`#${prefix}-expected-dec`);

    const ra = raInput ? parseFloat(raInput.value) : obj.ra_hours;
    const dec = decInput ? parseFloat(decInput.value) : obj.dec_degrees;

    if (isNaN(ra) || isNaN(dec)) {
      App.showToast(I18n.t('cal.msg.valid_coords_required'), 'error');
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
      resultDiv.textContent = I18n.t('cal.msg.measurement_added', { name: obj.name || 'object', ra: Number(ra).toFixed(4), dec: Number(dec).toFixed(2) });
      resultDiv.style.display = 'block';

      App.showToast(I18n.t('cal.msg.measurement_added_toast', { name: obj.name || 'object' }), 'success');

      // Refresh status
      if (type === 'bs') {
        await loadBootstrapStatus();
      } else {
        await loadTPointStatus();
      }
    } catch (err) {
      resultDiv.className = 'calibration-result error';
      resultDiv.textContent = I18n.t('cal.msg.measurement_failed', { message: err.message });
      resultDiv.style.display = 'block';
      App.showToast(I18n.t('cal.msg.measurement_failed_toast', { message: err.message }), 'error');
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
        resultDiv.textContent = I18n.t('cal.msg.status_load_failed', { message: err.message });
        resultDiv.className = 'calibration-result error';
        resultDiv.style.display = 'block';
      }
    }
  }

  function updateBootstrapUI(status) {
    const stateLabel = status.state || 'NOT_CALIBRATED';
    const stateText = bsStateLabel(stateLabel);

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
    setText('#bootstrap-tpoint-ready', status.ready_for_tpoint ? I18n.t('cal.yes', 'Yes') : I18n.t('cal.no', 'No'));
    setText('#bootstrap-last-calibration', status.last_calibration ? formatTimestamp(status.last_calibration) : '—');

    // New BootstrapStatus fields
    const mode = status.bootstrap_mode;
    currentBootstrapMode = mode != null ? mode : currentBootstrapMode;
    const modeSelect = $('#bootstrap-mode-select');
    if (modeSelect) modeSelect.value = String(currentBootstrapMode);
    setText('#bootstrap-encoder-type', status.encoder_type_absolute ? I18n.t('cal.absolute', 'Absolute') : I18n.t('cal.incremental', 'Incremental'));
    setText('#bootstrap-ref-position-known', status.reference_position_known ? I18n.t('cal.yes', 'Yes') : I18n.t('cal.no', 'No'));
    setText('#bootstrap-manual-meas-needed', status.manual_measurements_needed != null ? String(status.manual_measurements_needed) : '—');
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

      let msg = I18n.t('cal.msg.bootstrap_completed');
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
      resultDiv.textContent = I18n.t('cal.msg.bootstrap_failed', { message: err.message });
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
      resultDiv.textContent = I18n.t('cal.msg.bootstrap_cleared');
      resultDiv.style.display = 'block';
      await loadBootstrapStatus();
    } catch (err) {
      resultDiv.className = 'calibration-result error';
      resultDiv.textContent = I18n.t('cal.msg.clear_failed', { message: err.message });
      resultDiv.style.display = 'block';
    } finally {
      btn.disabled = false;
    }
  }

  // ─── Bootstrap Mode ──────────────────────────────────────────────────────

  async function handleSetBootstrapMode() {
    const select = $('#bootstrap-mode-select');
    const resultDiv = $('#bootstrap-result');
    if (!select || !resultDiv) return;

    const mode = parseInt(select.value, 10);
    if (isNaN(mode) || mode < 0 || mode > 2) {
      App.showToast(I18n.t('cal.msg.invalid_mode'), 'error');
      return;
    }

    try {
      await Api.setBootstrapMode(mode);
      currentBootstrapMode = mode;
      App.showToast(I18n.t('cal.msg.mode_set', { mode: getBootstrapModeLabel(mode) }), 'success');
      await loadBootstrapStatus();
    } catch (err) {
      resultDiv.className = 'calibration-result error';
      resultDiv.textContent = I18n.t('cal.msg.mode_set_failed', { message: err.message });
      resultDiv.style.display = 'block';
      App.showToast(I18n.t('cal.msg.mode_set_failed_toast', { message: err.message }), 'error');
    }
  }

  // ─── Automatic Bootstrap ─────────────────────────────────────────────────

  async function handleRunAutoBootstrap() {
    const btn = $('#btn-run-auto-bootstrap');
    const resultDiv = $('#bootstrap-result');
    if (!btn || !resultDiv) return;

    const minMeasInput = $('#auto-bs-min-measurements');
    const maxErrorInput = $('#auto-bs-max-error');
    const targetStarsInput = $('#auto-bs-target-stars');
    const proceedCheckbox = $('#auto-bs-proceed-tpoint');

    const options = {};
    if (minMeasInput) {
      const val = parseInt(minMeasInput.value, 10);
      if (!isNaN(val) && val >= 3) options.min_measurements = val;
    }
    if (maxErrorInput) {
      const val = parseInt(maxErrorInput.value, 10);
      if (!isNaN(val) && val > 0) options.max_alignment_error_arcsec = val;
    }
    if (targetStarsInput) {
      const stars = targetStarsInput.value.trim();
      if (stars) {
        options.target_star_names = stars.split(',').map(s => s.trim()).filter(Boolean);
      }
    }
    if (proceedCheckbox && proceedCheckbox.checked) {
      options.proceed_to_tpoint = true;
    }

    btn.disabled = true;
    resultDiv.style.display = 'none';

    try {
      await Api.runAutomaticBootstrap(options);
      App.showToast(I18n.t('cal.msg.auto_started'), 'success');
      // Show progress section and start polling
      const progressDiv = $('#auto-bootstrap-progress');
      if (progressDiv) progressDiv.style.display = 'block';
      await loadAutoBootstrapStatus();
    } catch (err) {
      resultDiv.className = 'calibration-result error';
      resultDiv.textContent = I18n.t('cal.msg.auto_failed', { message: err.message });
      resultDiv.style.display = 'block';
      App.showToast(I18n.t('cal.msg.auto_failed_toast', { message: err.message }), 'error');
    } finally {
      btn.disabled = false;
    }
  }

  async function loadAutoBootstrapStatus() {
    try {
      const status = await Api.getAutoBootstrapStatus();
      updateAutoBootstrapUI(status);
    } catch (err) {
      // Silently ignore — auto-bootstrap may not be available yet
    }
  }

  function updateAutoBootstrapUI(status) {
    if (!status) return;

    const progressDiv = $('#auto-bootstrap-progress');
    if (!progressDiv) return;

    // Show progress section when not idle
    const stateVal = status.state;
    if (stateVal !== undefined && stateVal !== 0) {
      progressDiv.style.display = 'block';
    }

    const stateLabel = getAutoBootstrapStateLabel(stateVal);
    setText('#auto-bs-state', stateLabel);

    // Progress bar
    const pct = status.progress_percent != null ? Math.round(status.progress_percent) : 0;
    setText('#auto-bs-progress-text', `${pct}%`);
    const bar = $('#auto-bs-progress-bar');
    if (bar) bar.style.width = `${pct}%`;

    // Measurements
    setText('#auto-bs-meas-collected', String(status.measurements_collected ?? 0));
    setText('#auto-bs-meas-target', String(status.measurements_target ?? 0));

    // Current target star
    setText('#auto-bs-current-star', status.current_target_star || '—');

    // State message
    setText('#auto-bs-state-message', status.state_message || '');

    // Error message
    const errorDiv = $('#auto-bs-error');
    if (errorDiv) {
      if (status.error_message) {
        errorDiv.textContent = status.error_message;
        errorDiv.style.display = 'block';
      } else {
        errorDiv.style.display = 'none';
      }
    }

    // Update badge if completed or in error
    if (stateVal === 3) {
      App.showToast(I18n.t('cal.msg.auto_completed'), 'success');
    } else if (stateVal === 4) {
      App.showToast(I18n.t('cal.msg.auto_error', { message: status.error_message || 'Unknown error' }), 'error');
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
        resultDiv.textContent = I18n.t('cal.msg.tpoint_status_failed', { message: err.message });
        resultDiv.className = 'calibration-result error';
        resultDiv.style.display = 'block';
      }
    }
  }

  function updateTPointUI(params) {
    const badge = $('#tpoint-status-badge');
    if (badge) {
      const isCalibrated = params.calibrated;
      badge.textContent = isCalibrated ? I18n.t('cal.calibrated_yes', 'CALIBRATED') : I18n.t('cal.calibrated_no', 'INACTIVE');
      badge.className = isCalibrated ? 'status-badge tracking' : 'status-badge idle';
    }

    setText('#tpoint-measurement-count', String(params.measurement_count ?? params.coefficients?.length ?? 0));
    setText('#tpoint-residual-rms', formatArcsec(params.residual_rms));
    setText('#tpoint-max-residual', formatArcsec(params.residual_max));
    setText('#tpoint-chi-squared', params.chi_squared != null ? Number(params.chi_squared).toFixed(3) : '—');
    setText('#tpoint-last-update', params.last_update ? formatTimestamp(params.last_update) : '—');
    setText('#tpoint-calibrated', params.calibrated ? I18n.t('cal.yes', 'Yes') : I18n.t('cal.no', 'No'));
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
      resultDiv.textContent = I18n.t('cal.msg.tpoint_completed');
      resultDiv.style.display = 'block';
      await loadTPointStatus();
    } catch (err) {
      resultDiv.className = 'calibration-result error';
      resultDiv.textContent = I18n.t('cal.msg.tpoint_failed', { message: err.message });
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
      resultDiv.textContent = I18n.t('cal.msg.tpoint_cleared');
      resultDiv.style.display = 'block';
      await loadTPointStatus();
    } catch (err) {
      resultDiv.className = 'calibration-result error';
      resultDiv.textContent = I18n.t('cal.msg.clear_failed', { message: err.message });
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
    loadAutoBootstrapStatus();

    // Periodic refresh
    pollInterval = setInterval(() => {
      loadBootstrapStatus();
      loadTPointStatus();
      loadAutoBootstrapStatus();
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

  /**
   * Get the currently selected calibration reference object.
   * Used by the Tests tab to import the object for transform/slew.
   * @returns {{ bs: object|null, tp: object|null }}
   */
  function getSelectedObject() {
    return { bs: bsSelectedObject, tp: tpSelectedObject };
  }

  return {
    init,
    startPolling,
    stopPolling,
    loadBootstrapStatus,
    loadTPointStatus,
    toggleHelp,
    getSelectedObject,
  };
})();
