/**
 * Astronomical Mount Controller - Mount Control Component
 *
 * Handles the mount control UI: slew form, quick action buttons,
 * and axis control pad (4-directional movement with speed control).
 *
 * Axis control behavior depends on calibration state:
 * - Uncalibrated → low-level ControlAxis (velocity mode)
 * - Calibrated   → coordinate-aware SlewToHorizontal/SlewToCoordinates
 *
 * Mount type (equatorial vs alt-az) affects axis labels and movement.
 */

const MountControlComponent = (() => {
  'use strict';

  const { $ } = Utils;

  // ─── Axis Control State ──────────────────────────────────────────────

  /** Current mount type: 'equatorial' | 'alt_az' | 'unknown' */
  let currentMountType = 'unknown';

  /** Whether the mount is calibrated (TRACKING or SLEWING state) */
  let isCalibrated = false;

  /** Gear ratios for HA and Dec axes (servo → telescope) */
  let haAxisGearRatio = 360.0;
  let decAxisGearRatio = 360.0;

  /** Whether speed/step refers to telescope axis (true) or servo motor (false) */
  let speedRefTelescope = false;

  /** Currently active axis movement timer (for continuous velocity control) */
  let activeAxisTimer = null;

  /** Currently moving axis: 0 (HA/RA/Az) or 1 (Dec/Alt) */
  let activeAxisId = -1;

  /** Current movement direction sign: 1 (forward) or -1 (backward) */
  let activeDirection = 0;

  /** Axis control mode: 'velocity' (press-hold-release-stop) or 'step' (single click, move by set amount) */
  let axisControlMode = 'velocity';

  /** Step size for step mode in degrees */
  let stepSizeDeg = 1.0;

  // ─── Help Content ───────────────────────────────────────────────────

  function buildControlHelpContent() {
    const container = $('#control-help-content');
    if (!container) return;

    const t = I18n.t.bind(I18n);

    const steps = [
      { num: '1', titleKey: 'control.help_step1_title', open: true,
        bodyHtml: '<ol><li>' + t('control.help_step1_li1') + '</li><li>' + t('control.help_step1_li2') + '</li><li>' + t('control.help_step1_li3') + '</li></ol>' },
      { num: '2', titleKey: 'control.help_step2_title', open: false,
        bodyHtml: '<ol><li>' + t('control.help_step2_li1') + '</li><li>' + t('control.help_step2_li2') + '</li><li>' + t('control.help_step2_li3') + '</li></ol>' },
      { num: '3', titleKey: 'control.help_step3_title', open: false,
        bodyHtml: '<ol><li>' + t('control.help_step3_li1') + '</li><li>' + t('control.help_step3_li2') + '</li><li>' + t('control.help_step3_li3') + '</li></ol>' },
      { num: '4', titleKey: 'control.help_step4_title', open: false,
        bodyHtml: '<ul><li>' + t('control.help_step4_li1') + '</li><li>' + t('control.help_step4_li2') + '</li><li>' + t('control.help_step4_li3') + '</li></ul>' }
    ];

    let html = '<p><strong>' + t('control.help_purpose_label') + '</strong> ' + t('control.help_purpose_text') + '</p>';

    steps.forEach(function(step) {
      html += '<details class="calibration-help-step"' + (step.open ? ' open' : '') + '>'
        + '<summary class="calibration-help-step-summary">'
        + '<span class="calibration-help-step-number">' + step.num + '</span>'
        + t(step.titleKey) + '</summary>'
        + '<div class="calibration-help-step-body">' + step.bodyHtml + '</div>'
        + '</details>';
    });

    container.innerHTML = html;
  }

  // ─── Initialization ─────────────────────────────────────────────────

  /**
   * Initialize the mount control component.
   * Binds all event listeners.
   */
  function init() {
    buildControlHelpContent();
    document.addEventListener('i18n:applied', buildControlHelpContent);
    bindHelpToggle('card-control-help');
    initSlewForm();
    initSlewAndTrackBtn();
    initSlewSearch();
    initQuickActions();
    initAxisControl();
    initAxisModeControls();
    initStateFileInput();
    // Populate slew form from last calibration object (delayed until CalibrationComponent is ready)
    setTimeout(populateSlewFromLastObject, 500);
  }

  /**
   * Bind collapse/expand toggle for the help card.
   * Clicking the header or the +/− button toggles card-collapsed.
   */
  function bindHelpToggle(cardId) {
    const card = $('#' + cardId);
    if (!card) return;
    const toggleBtn = card.querySelector('.card-toggle-btn');
    const header = card.querySelector('.card-header');

    const doToggle = function() {
      const collapsed = card.classList.toggle('card-collapsed');
      if (toggleBtn) toggleBtn.textContent = collapsed ? '+' : '\u2212';
    };

    if (toggleBtn) {
      toggleBtn.addEventListener('click', function(e) {
        e.stopPropagation();
        doToggle();
      });
    }

    if (header) {
      header.addEventListener('click', function(e) {
        if (e.target.closest('button, input, select, a, label')) return;
        doToggle();
      });
    }
  }

  /**
   * Build the full file path from the directory + filename inputs.
   * Handles trailing-slash / double-slash joining.
   */
  function getStateFilePath() {
    const dirInput = $('#state-dir-path');
    const nameInput = $('#state-file-name');
    const dir = dirInput ? dirInput.value.trim() : 'data/';
    const name = nameInput ? nameInput.value.trim() : '';
    if (!name) return dir.replace(/\/+$/, '') + '/mount_state.json';
    const dirClean = dir.replace(/\/+$/, '');
    const nameClean = name.replace(/^\/+/, '');
    return dirClean ? dirClean + '/' + nameClean : nameClean;
  }

  /**
   * Generate a default state filename with current date and time.
   */
  function generateStateFileName() {
    const now = new Date();
    const pad = (n) => String(n).padStart(2, '0');
    const dateStr = `${now.getFullYear()}-${pad(now.getMonth() + 1)}-${pad(now.getDate())}`;
    const timeStr = `${pad(now.getHours())}${pad(now.getMinutes())}${pad(now.getSeconds())}`;
    return `mount_state_${dateStr}_${timeStr}.json`;
  }

  /**
   * Initialize the state file path input with "Now" button.
   */
  function initStateFileInput() {
    const nowBtn = $('#btn-state-default-name');
    if (nowBtn) {
      nowBtn.addEventListener('click', () => {
        const input = $('#state-file-name');
        if (input) {
          input.value = generateStateFileName();
        }
      });
    }
  }

  /**
   * Initialize the hidden file picker for loading state from a local file.
   * When the user selects a file via the native OS picker, reads the content
   * and uploads it to the server to restore controller state.
   */
  function initStateFilePicker() {
    const fileInput = $('#state-file-picker');
    if (!fileInput) return;

    fileInput.addEventListener('change', async (event) => {
      const file = event.target.files && event.target.files[0];
      if (!file) return;

      const reader = new FileReader();
      reader.onload = async (loadEvent) => {
        const fileContent = loadEvent.target.result;

        const loadBtn = $('#btn-load-state');
        if (loadBtn) loadBtn.disabled = true;

        try {
          await Api.uploadAndLoadState(fileContent, file.name);
          App.showToast('✅ Mount state restored successfully from ' + file.name, 'success', 4000);
        } catch (err) {
          App.showToast(`Restore state failed: ${err.message}`, 'error');
        } finally {
          if (loadBtn) loadBtn.disabled = false;
          // Reset the file input so the same file can be picked again
          fileInput.value = '';
        }
      };

      reader.onerror = () => {
        App.showToast('Failed to read the selected file', 'error');
        fileInput.value = '';
      };

      reader.readAsText(file);
    });
  }

  // ─── Slew Form ───────────────────────────────────────────────────────

  function initSlewForm() {
    const slewForm = $('#slew-form');
    if (slewForm) {
      slewForm.addEventListener('submit', handleSlew);
    }
  }

  async function handleSlew(event) {
    event.preventDefault();

    const raInput = $('#slew-ra');
    const decInput = $('#slew-dec');
    const submitBtn = $('#slew-form .btn-primary');

    if (!raInput || !decInput) return;

    // Use enhanced angle input getters if available, fall back to parseFloat
    const ra = raInput.getAngleDecimal ? raInput.getAngleDecimal() : parseFloat(raInput.value);
    const dec = decInput.getAngleDecimal ? decInput.getAngleDecimal() : parseFloat(decInput.value);

    if (isNaN(ra) || ra < 0 || ra >= 24) {
      App.showToast('RA must be between 0 and 24 hours', 'error');
      return;
    }

    if (isNaN(dec) || dec < -90 || dec > 90) {
      App.showToast('Dec must be between -90 and 90 degrees', 'error');
      return;
    }

    if (submitBtn) submitBtn.disabled = true;

    try {
      const result = await Api.slewToCoordinates(ra, dec);
      const raStr = Utils.formatRA(ra);
      const decStr = Utils.formatDec(dec);
      App.showToast(result.message || `Slewing to RA=${raStr}, Dec=${decStr}`, 'success');
    } catch (err) {
      App.showToast(`Slew failed: ${err.message}`, 'error');
    } finally {
      if (submitBtn) submitBtn.disabled = false;
    }
  }

  // ─── Slew & Track Button ─────────────────────────────────────────────

  function initSlewAndTrackBtn() {
    const btn = $('#btn-slew-and-track');
    if (btn) btn.addEventListener('click', handleSlewAndTrack);
  }

  async function handleSlewAndTrack() {
    const raInput = $('#slew-ra');
    const decInput = $('#slew-dec');
    const btn = $('#btn-slew-and-track');

    if (!raInput || !decInput) return;

    const ra = raInput.getAngleDecimal ? raInput.getAngleDecimal() : parseFloat(raInput.value);
    const dec = decInput.getAngleDecimal ? decInput.getAngleDecimal() : parseFloat(decInput.value);

    if (isNaN(ra) || ra < 0 || ra >= 24) {
      App.showToast('RA must be between 0 and 24 hours', 'error');
      return;
    }

    if (isNaN(dec) || dec < -90 || dec > 90) {
      App.showToast('Dec must be between -90 and 90 degrees', 'error');
      return;
    }

    if (btn) btn.disabled = true;

    try {
      const result = await Api.trackObject(ra, dec);
      const raStr = Utils.formatRA(ra);
      const decStr = Utils.formatDec(dec);
      App.showToast(result.message || `Tracking RA=${raStr}, Dec=${decStr}`, 'success');
    } catch (err) {
      App.showToast(`Track failed: ${err.message}`, 'error');
    } finally {
      if (btn) btn.disabled = false;
    }
  }

  // ─── Auto-populate from Calibration ──────────────────────────────────

  /**
   * Populate the Slew form and object info display from the last object
   * used in calibration (if any). Formats coordinates as sexagesimal.
   */
  function populateSlewFromLastObject() {
    const obj = CalibrationComponent.getLastUsedObject();

    // Populate RA/Dec inputs if object is available
    if (obj) {
      const raInput = $('#slew-ra');
      const decInput = $('#slew-dec');
      if (raInput && obj.ra_hours != null) {
        if (raInput.setAngleDecimal) raInput.setAngleDecimal(obj.ra_hours);
        else raInput.value = obj.ra_hours;
      }
      if (decInput && obj.dec_degrees != null) {
        if (decInput.setAngleDecimal) decInput.setAngleDecimal(obj.dec_degrees);
        else decInput.value = obj.dec_degrees;
      }
    }

    // Show/hide details vs empty message
    const detailsRow = $('#slew-last-details');
    const emptyMsg = $('#slew-last-empty');

    if (obj) {
      if (detailsRow) detailsRow.style.display = 'flex';
      if (emptyMsg) emptyMsg.style.display = 'none';

      const nameEl = $('#slew-last-name');
      if (nameEl) nameEl.textContent = obj.name || '(unnamed)';

      const catalogEl = $('#slew-last-catalog');
      if (catalogEl) catalogEl.textContent = obj.catalog_id || '';

      const raEl = $('#slew-last-ra');
      if (raEl && obj.ra_hours != null) raEl.textContent = Utils.formatRA(obj.ra_hours);

      const decEl = $('#slew-last-dec');
      if (decEl && obj.dec_degrees != null) decEl.textContent = Utils.formatDec(obj.dec_degrees);

      const magEl = $('#slew-last-mag');
      if (magEl) {
        magEl.textContent = obj.v_magnitude != null ? Number(obj.v_magnitude).toFixed(1) : '—';
      }

      const typeEl = $('#slew-last-type');
      if (typeEl) {
        const typeLabel = obj.type ? obj.type.replace(/_/g, ' ').toLowerCase() : '';
        typeEl.textContent = typeLabel;
      }
    } else {
      if (detailsRow) detailsRow.style.display = 'none';
      if (emptyMsg) emptyMsg.style.display = 'block';
    }
  }

  // ─── Slew Object Search ──────────────────────────────────────────────

  function initSlewSearch() {
    const searchInput = $('#slew-object-search');
    const searchBtn = $('#btn-slew-search');

    if (searchBtn) {
      searchBtn.addEventListener('click', () => handleSlewSearch());
    }
    if (searchInput) {
      searchInput.addEventListener('keydown', (e) => {
        if (e.key === 'Enter') { e.preventDefault(); handleSlewSearch(); }
      });
    }
  }

  async function handleSlewSearch() {
    const input = $('#slew-object-search');
    const resultsEl = $('#slew-search-results');
    if (!input || !resultsEl) return;

    const query = input.value.trim();
    if (!query) {
      resultsEl.innerHTML = '';
      return;
    }

    resultsEl.innerHTML = `<div class="calibration-search-hint">Searching…</div>`;

    try {
      const result = await Api.searchObjects({ query });
      const objects = result.objects || [];

      if (objects.length === 0) {
        resultsEl.innerHTML = `<div class="calibration-search-hint">No results for "${Utils.escapeHtml(query)}"</div>`;
        return;
      }

      let html = '<div class="calibration-search-list">';
      objects.slice(0, 12).forEach(obj => {
        const ra = obj.ra_hours != null ? Number(obj.ra_hours).toFixed(4) : '—';
        const dec = obj.dec_degrees != null ? Number(obj.dec_degrees).toFixed(2) : '—';
        const mag = obj.v_magnitude != null ? Number(obj.v_magnitude).toFixed(1) : '—';
        const name = Utils.escapeHtml(obj.name || '(unnamed)');
        const catalog = obj.catalog_id ? Utils.escapeHtml(obj.catalog_id) : '';

        html += `<div class="calibration-search-item" data-ra="${ra}" data-dec="${obj.dec_degrees}" data-name="${Utils.escapeHtml(obj.name || '')}" data-catalog="${catalog}" data-mag="${mag}" data-type="${Utils.escapeHtml(obj.type || '')}">`;
        html += `<div class="calibration-search-item-name">${name}`;
        if (catalog) html += ` <span class="calibration-search-item-catalog">${catalog}</span>`;
        html += `</div>`;
        html += `<div class="calibration-search-item-coords">RA: ${ra}h &nbsp; Dec: ${dec}° &nbsp; Mag: ${mag}</div>`;
        html += `<button class="btn btn-sm btn-primary calibration-search-item-select">Select</button>`;
        html += `</div>`;
      });
      html += '</div>';
      resultsEl.innerHTML = html;

      // Bind click on item row
      resultsEl.querySelectorAll('.calibration-search-item').forEach(item => {
        item.addEventListener('click', () => {
          const ra = parseFloat(item.dataset.ra);
          const dec = parseFloat(item.dataset.dec);
          if (!isNaN(ra) && !isNaN(dec)) {
            populateSlewFromSearch(item.dataset);
          }
        });
      });

      // Bind "Select" buttons
      resultsEl.querySelectorAll('.calibration-search-item-select').forEach(btn => {
        btn.addEventListener('click', (e) => {
          e.stopPropagation();
          const item = btn.closest('.calibration-search-item');
          if (item) {
            const ra = parseFloat(item.dataset.ra);
            const dec = parseFloat(item.dataset.dec);
            if (!isNaN(ra) && !isNaN(dec)) {
              populateSlewFromSearch(item.dataset);
            }
          }
        });
      });

    } catch (err) {
      resultsEl.innerHTML = `<div class="calibration-search-hint" style="color:var(--color-danger);">Search error: ${Utils.escapeHtml(err.message)}</div>`;
    }
  }

  /**
   * Populate the slew form and last-object display from a search result.
   * @param {DOMStringMap} data - dataset from the clicked search item
   */
  function populateSlewFromSearch(data) {
    const raInput = $('#slew-ra');
    const decInput = $('#slew-dec');
    const ra = parseFloat(data.ra);
    const dec = parseFloat(data.dec);

    if (raInput && !isNaN(ra)) {
      if (raInput.setAngleDecimal) raInput.setAngleDecimal(ra);
      else raInput.value = ra.toFixed(4);
    }
    if (decInput && !isNaN(dec)) {
      if (decInput.setAngleDecimal) decInput.setAngleDecimal(dec);
      else decInput.value = dec.toFixed(4);
    }

    // Update "last object" display
    const detailsRow = $('#slew-last-details');
    const emptyMsg = $('#slew-last-empty');
    if (detailsRow) detailsRow.style.display = 'flex';
    if (emptyMsg) emptyMsg.style.display = 'none';

    const nameEl = $('#slew-last-name');
    if (nameEl) nameEl.textContent = data.name || '(unnamed)';

    const catalogEl = $('#slew-last-catalog');
    if (catalogEl) catalogEl.textContent = data.catalog || '';

    const raEl = $('#slew-last-ra');
    if (raEl && !isNaN(ra)) raEl.textContent = Utils.formatRA(ra);

    const decEl = $('#slew-last-dec');
    if (decEl && !isNaN(dec)) decEl.textContent = Utils.formatDec(dec);

    const magEl = $('#slew-last-mag');
    if (magEl) magEl.textContent = data.mag || '—';

    const typeEl = $('#slew-last-type');
    if (typeEl) typeEl.textContent = (data.type || '').replace(/_/g, ' ').toLowerCase();

    // Clear search results
    const resultsEl = $('#slew-search-results');
    if (resultsEl) resultsEl.innerHTML = '';

    App.showToast(`Selected ${data.name || 'object'}: RA ${Utils.formatRA(ra)}, Dec ${Utils.formatDec(dec)}`, 'info');
  }

  // ─── Quick Actions ───────────────────────────────────────────────────

  function initQuickActions() {
    const stopBtn = $('#btn-stop');
    if (stopBtn) stopBtn.addEventListener('click', handleStop);

    const parkBtn = $('#btn-park');
    if (parkBtn) parkBtn.addEventListener('click', handlePark);

    const unparkBtn = $('#btn-unpark');
    if (unparkBtn) unparkBtn.addEventListener('click', handleUnpark);

    const homeBtn = $('#btn-home');
    if (homeBtn) homeBtn.addEventListener('click', handleHome);

    const clearBtn = $('#btn-clear-errors');
    if (clearBtn) clearBtn.addEventListener('click', handleClearErrors);

    const saveStateBtn = $('#btn-save-state');
    if (saveStateBtn) saveStateBtn.addEventListener('click', handleSaveState);

    const loadStateBtn = $('#btn-load-state');
    if (loadStateBtn) loadStateBtn.addEventListener('click', handleLoadState);
  }

  async function handleStop() {
    const btn = $('#btn-stop');
    if (btn) btn.disabled = true;
    try {
      const result = await Api.stopMount();
      App.showToast(result.message || 'Mount stopped', 'success');
    } catch (err) {
      App.showToast(`Stop failed: ${err.message}`, 'error');
    } finally {
      if (btn) btn.disabled = false;
    }
  }

  async function handlePark() {
    const btn = $('#btn-park');
    if (btn) btn.disabled = true;
    try {
      const result = await Api.parkMount();
      App.showToast(result.message || 'Mount parking', 'info');
    } catch (err) {
      App.showToast(`Park failed: ${err.message}`, 'error');
    } finally {
      if (btn) btn.disabled = false;
    }
  }

  async function handleUnpark() {
    const btn = $('#btn-unpark');
    if (btn) btn.disabled = true;
    try {
      const result = await Api.unparkMount();
      App.showToast(result.message || 'Mount unparked', 'success');
    } catch (err) {
      App.showToast(`Unpark failed: ${err.message}`, 'error');
    } finally {
      if (btn) btn.disabled = false;
    }
  }

  async function handleHome() {
    const btn = $('#btn-home');
    const input1 = $('#home-axis1');
    const input2 = $('#home-axis2');
    if (btn) btn.disabled = true;

    const axis1 = input1?.getAngleDecimal ? input1.getAngleDecimal() : 0;
    const axis2 = input2?.getAngleDecimal ? input2.getAngleDecimal() : 0;

    if (!isFinite(axis1) || !isFinite(axis2)) {
      App.showToast('Invalid axis values — enter DMS format (e.g. 0:0:0.00)', 'error');
      if (btn) btn.disabled = false;
      return;
    }

    if (!confirm(`Set Home reference to:\n  Axis 1 = ${axis1.toFixed(4)}°\n  Axis 2 = ${axis2.toFixed(4)}°\n\nThis does not move the mount. Continue?`)) {
      if (btn) btn.disabled = false;
      return;
    }

    try {
      const result = await Api.homeMount(axis1, axis2);
      App.showToast(result.message || `Mount homed to axis1=${axis1.toFixed(2)}°, axis2=${axis2.toFixed(2)}°`, 'success');

      // Force immediate status refresh so the UI shows the new
      // positions without waiting for the next poll cycle.
      try {
        const freshState = await Api.getStatus();
        MountStatusComponent.render(freshState);
      } catch (_) { /* non-critical */ }
    } catch (err) {
      App.showToast(`Home failed: ${err.message}`, 'error');
    } finally {
      if (btn) btn.disabled = false;
    }
  }

  async function handleClearErrors() {
    const btn = $('#btn-clear-errors');
    if (btn) btn.disabled = true;
    try {
      const result = await Api.clearErrors();
      App.showToast(result.message || 'Errors cleared', 'success');
    } catch (err) {
      App.showToast(`Clear errors failed: ${err.message}`, 'error');
    } finally {
      if (btn) btn.disabled = false;
    }
  }

  // ─── Save / Restore State ────────────────────────────────────────────

  async function handleSaveState() {
    const btn = $('#btn-save-state');
    if (btn) btn.disabled = true;
    try {
      const file_path = getStateFilePath();
      const options = { file_path };
      const result = await Api.saveState(options);
      App.showToast(`✅ State saved: ${result.file_path} (${(result.file_size / 1024).toFixed(1)} KB)`, 'success', 5000);
    } catch (err) {
      App.showToast(`Save state failed: ${err.message}`, 'error');
    } finally {
      if (btn) btn.disabled = false;
    }
  }

  async function handleLoadState() {
    const btn = $('#btn-load-state');
    if (btn) btn.disabled = true;
    try {
      const file_path = getStateFilePath();
      const result = await Api.loadState({ file_path });
      App.showToast(`✅ State restored: ${file_path}`, 'success', 5000);
    } catch (err) {
      App.showToast(`Load state failed: ${err.message}`, 'error');
    } finally {
      if (btn) btn.disabled = false;
    }
  }

  // ─── Axis Mode Selector ──────────────────────────────────────────────

  /**
   * Initialize the axis mode toggle (Velocity ↔ Step) and step size input.
   */
  function initAxisModeControls() {
    const toggleBtn = $('#btn-axis-mode-toggle');
    const stepControl = $('#axis-step-control');
    const stepInput = $('#axis-step-size');
    const toggleLabel = $('#axis-mode-toggle-label');

    if (!toggleBtn) return;

    toggleBtn.addEventListener('click', () => {
      if (axisControlMode === 'velocity') {
        axisControlMode = 'step';
        if (toggleLabel) toggleLabel.textContent = I18n.t('axis.mode_step');
        if (stepControl) stepControl.style.display = 'flex';
        toggleBtn.title = I18n.t('axis.mode_step_title');
      } else {
        axisControlMode = 'velocity';
        if (toggleLabel) toggleLabel.textContent = I18n.t('axis.mode_velocity');
        if (stepControl) stepControl.style.display = 'none';
        toggleBtn.title = I18n.t('axis.mode_velocity_title');
      }
    });

    if (stepInput) {
      stepInput.addEventListener('change', () => {
        const val = parseFloat(stepInput.value);
        if (!isNaN(val) && val > 0) {
          stepSizeDeg = val;
        } else {
          stepInput.value = stepSizeDeg.toFixed(1);
        }
      });
    }
  }

  /**
   * Get the current step size from the input, falling back to stored value.
   * @returns {number} Step size in degrees
   */
  function getStepSize(axisId = 0) {
    const input = $('#axis-step-size');
    let val = 1.0;
    if (input) {
      const parsed = parseFloat(input.value);
      if (!isNaN(parsed) && parsed > 0) val = parsed;
      else val = stepSizeDeg;
    }
    // When in telescope-axis mode, step refers to telescope angle,
    // so convert to servo angle via gear ratio
    if (speedRefTelescope) {
      const gear = (axisId === 1) ? decAxisGearRatio : haAxisGearRatio;
      return val * gear;
    }
    return val;
  }

  // ─── Axis Control ────────────────────────────────────────────────────

  /**
   * Initialize the axis control pad: bind pointer events to axis buttons,
   * speed slider, and emergency stop.
   */
  function initAxisControl() {
    const speedSlider = $('#axis-speed');
    if (speedSlider) {
      speedSlider.addEventListener('input', updateSpeedLabel);
      // Sync slider max with the backend max_slew_rate config
      syncSpeedSliderMax(speedSlider);
    }

    const accelSlider = $('#axis-accel');
    if (accelSlider) {
      accelSlider.addEventListener('input', updateAccelLabel);
    }

    const decelSlider = $('#axis-decel');
    if (decelSlider) {
      decelSlider.addEventListener('input', updateDecelLabel);
    }

    // Bind speed reference toggle (servo motor vs telescope axis)
    const refToggle = $('#axis-speed-ref-toggle');
    if (refToggle) {
      refToggle.addEventListener('change', onSpeedRefToggleChanged);
    }
    // Load gear ratios for telescope-axis speed conversion
    loadGearRatios();
  }

  /**
   * Fetch gear ratios from backend config.
   */
  async function loadGearRatios() {
    try {
      const config = await Api.getConfig();
      // Gear ratios may be under ha_axis_params / dec_axis_params sub-objects
      // or directly on the config (depending on API version). Try both paths.
      const haGear = config?.ha_axis_params?.gear_ratio ?? config?.ha_axis_gear_ratio;
      const decGear = config?.dec_axis_params?.gear_ratio ?? config?.dec_axis_gear_ratio;
      if (haGear && haGear > 0) haAxisGearRatio = haGear;
      if (decGear && decGear > 0) decAxisGearRatio = decGear;
      updateGearInfoLabel();
      syncSpeedSliderMax($('#axis-speed'));
      console.log('[AxisCtrl] Gear ratios: HA=%.1f:1, Dec=%.1f:1', haAxisGearRatio, decAxisGearRatio);
    } catch (err) {
      console.warn('[AxisCtrl] Could not load gear ratios, using defaults: %s', err.message);
    }
  }

  /**
   * Toggle between servo motor and telescope axis speed reference.
   */
  function onSpeedRefToggleChanged() {
    const toggle = $('#axis-speed-ref-toggle');
    speedRefTelescope = toggle ? toggle.checked : false;
    updateGearInfoLabel();
    syncSpeedSliderMax($('#axis-speed'));
    console.log('[AxisCtrl] Speed reference: %s', speedRefTelescope ? 'telescope axis' : 'servo motor');
  }

  /**
   * Update the gear info label showing the conversion ratio.
   */
  function updateGearInfoLabel() {
    const info = $('#axis-speed-gear-info');
    if (info) {
      const avgGear = (haAxisGearRatio + decAxisGearRatio) / 2;
      if (speedRefTelescope) {
        info.textContent = `(×${avgGear.toFixed(0)}:1 → servo)`;
      } else {
        info.textContent = `(÷${avgGear.toFixed(0)}:1 → telescope)`;
      }
    }

    const estopBtn = $('#btn-emergency-stop');
    if (estopBtn) estopBtn.addEventListener('click', handleEmergencyStop);

    // Bind axis direction buttons with press-and-hold support
    bindAxisButton('btn-axis-up',    1,  1);   // Axis 1 (Dec/Alt), positive direction
    bindAxisButton('btn-axis-down',  1, -1);   // Axis 1 (Dec/Alt), negative direction
    bindAxisButton('btn-axis-left',  0, -1);   // Axis 0 (HA/RA/Az), negative direction
    bindAxisButton('btn-axis-right', 0,  1);   // Axis 0 (HA/RA/Az), positive direction
  }

  /**
   * Bind pointer/mouse/touch events to an axis direction button.
   *
   * @param {string} buttonId - Element ID of the button
   * @param {number} axisId   - 0 (horizontal) or 1 (vertical)
   * @param {number} direction - +1 (forward/right/up) or -1 (backward/left/down)
   */
  function bindAxisButton(buttonId, axisId, direction) {
    const btn = $(`#${buttonId}`);
    if (!btn) return;

    // Mouse events
    btn.addEventListener('mousedown', (e) => {
      e.preventDefault();
      startAxisMovement(axisId, direction);
    });
    btn.addEventListener('mouseup', () => stopAxisMovement(axisId));
    btn.addEventListener('mouseleave', () => stopAxisMovement(axisId));

    // Touch events
    btn.addEventListener('touchstart', (e) => {
      e.preventDefault();
      startAxisMovement(axisId, direction);
    }, { passive: false });
    btn.addEventListener('touchend', (e) => {
      e.preventDefault();
      stopAxisMovement(axisId);
    }, { passive: false });
    btn.addEventListener('touchcancel', () => stopAxisMovement(axisId));
  }

  /**
   * Fetch max_slew_rate from backend config and update the speed slider's max.
   * If the current slider value exceeds the new max, clamp it.
   * @param {HTMLInputElement} slider - The speed range input
   */
  async function syncSpeedSliderMax(slider) {
    try {
      const config = await Api.getConfig();
      const maxSpeed = config.max_slew_rate;
      if (maxSpeed && typeof maxSpeed === 'number' && maxSpeed > 0) {
        // In telescope-axis mode, the effective max is reduced by gear ratio
        const avgGear = (haAxisGearRatio + decAxisGearRatio) / 2;
        const effectiveMax = speedRefTelescope ? (maxSpeed / avgGear) : maxSpeed;
        const step = speedRefTelescope ? Math.max(0.001, effectiveMax / 500) : 0.1;
        slider.max = effectiveMax;
        slider.step = step;
        slider.min = speedRefTelescope ? 0.001 : 0.1;
        if (parseFloat(slider.value) > effectiveMax) {
          slider.value = effectiveMax;
        }
        updateSpeedLabel();
      }
    } catch (err) {
      console.warn('[AxisCtrl] Could not sync speed slider max: %s', err.message);
    }
  }

  /**
   * Update the speed label when slider changes.
   */
  function updateSpeedLabel() {
    const slider = $('#axis-speed');
    const label = $('#axis-speed-label');
    if (slider && label) {
      const val = parseFloat(slider.value);
      if (speedRefTelescope && val < 0.01) {
        // Show in arcsec/s for very small telescope-axis speeds
        label.textContent = (val * 3600).toFixed(1) + '"';
      } else {
        label.textContent = val.toFixed(val < 0.1 ? 3 : 1);
      }
    }
  }

  /**
   * Update the acceleration label when slider changes.
   */
  function updateAccelLabel() {
    const slider = $('#axis-accel');
    const label = $('#axis-accel-label');
    if (slider && label) {
      const val = parseFloat(slider.value);
      label.textContent = val.toFixed(val < 1 ? 1 : 0);
    }
  }

  /**
   * Update the deceleration label when slider changes.
   */
  function updateDecelLabel() {
    const slider = $('#axis-decel');
    const label = $('#axis-decel-label');
    if (slider && label) {
      const val = parseFloat(slider.value);
      label.textContent = val.toFixed(val < 1 ? 1 : 0);
    }
  }

  /**
   * Get the current speed from the slider, accounting for gear ratio.
   * When in telescope-axis mode, converts telescope speed → servo speed.
   * @param {number} [axisId=0] - Axis ID (0=HA, 1=Dec) for correct gear ratio
   * @returns {number} Speed in servo °/s (what the API expects)
   */
  function getCurrentSpeed(axisId = 0) {
    const slider = $('#axis-speed');
    const rawSpeed = slider ? parseFloat(slider.value) : 1.0;
    if (speedRefTelescope) {
      // Convert telescope speed → servo speed using the correct gear ratio
      const gear = (axisId === 1) ? decAxisGearRatio : haAxisGearRatio;
      return rawSpeed * gear;
    }
    return rawSpeed;
  }

  /**
   * Get the current acceleration from the slider.
   * @returns {number} Acceleration in °/s²
   */
  function getCurrentAcceleration() {
    const slider = $('#axis-accel');
    return slider ? parseFloat(slider.value) : 50.0;
  }

  /**
   * Get the current deceleration from the slider.
   * @returns {number} Deceleration in °/s²
   */
  function getCurrentDeceleration() {
    const slider = $('#axis-decel');
    return slider ? parseFloat(slider.value) : 50.0;
  }

  /**
   * Update the calibration state based on latest status.
   * Called by the polling loop via the public setCalibrationState().
   *
   * @param {object} state - Controller state from API
   * @param {string} mountType - 'equatorial' | 'alt_az' | 'unknown'
   */
  function setCalibrationState(state, mountType) {
    const wasCalibrated = isCalibrated;
    currentMountType = mountType || 'unknown';

    // Mount is considered calibrated when bootstrap or TPOINT is done,
    // or when actively slewing/tracking.
    const status = (state && state.status || '').toUpperCase();
    const hasCalibration = (state && (state.bootstrap_calibrated || state.tpoint_calibrated));
    isCalibrated = hasCalibration || (status === 'TRACKING' || status === 'SLEWING');

    // When calibration just completed, auto-populate from last used object
    if (!wasCalibrated && isCalibrated) {
      populateSlewFromLastObject();
    }

    updateAxisModeIndicator();
  }

  /**
   * Update the axis mode badge and info text based on calibration + mount type.
   */
  function updateAxisModeIndicator() {
    const badge = $('#axis-mode-badge');
    const infoText = $('#axis-mode-text');

    if (!badge || !infoText) return;

    // Build axis labels based on mount type
    let horizLabel, vertLabel;
    if (currentMountType === 'alt_az') {
      horizLabel = 'Azimuth';
      vertLabel = 'Altitude';
    } else if (currentMountType === 'casual') {
      horizLabel = 'Axis 1';
      vertLabel = 'Axis 2';
    } else {
      // equatorial (default) or unknown
      horizLabel = 'RA';
      vertLabel = 'Dec';
    }

    let mountTypeLabel;
    if (currentMountType === 'alt_az') {
      mountTypeLabel = I18n.t('tests.alt_az');
    } else if (currentMountType === 'casual') {
      mountTypeLabel = I18n.t('tests.casual');
    } else {
      mountTypeLabel = I18n.t('tests.equatorial');
    }
    if (isCalibrated) {
      badge.className = 'status-badge tracking';
      badge.textContent = I18n.t('axis.mode_astro');
      infoText.textContent = I18n.t('axis.mode_calibrated_info', { horiz: horizLabel, vert: vertLabel, type: mountTypeLabel });
    } else {
      badge.className = 'status-badge idle';
      badge.textContent = I18n.t('axis.mode_low_level');
      infoText.textContent = I18n.t('axis.mode_uncalibrated_info', { horiz: horizLabel, vert: vertLabel });
    }
  }

  /**
   * Start moving an axis in a given direction.
   *
   * In uncalibrated mode: sends ControlAxis with velocity (continuous movement).
   * In calibrated mode: sends a single nudge via SlewToHorizontal or SlewToCoordinates.
   *
   * @param {number} axisId   - 0 (horizontal) or 1 (vertical)
   * @param {number} direction - +1 or -1
   */
  function startAxisMovement(axisId, direction) {
    console.log('[AxisCtrl] startAxisMovement: axisId=%d, direction=%d, mode=%s, isCalibrated=%s',
                axisId, direction, axisControlMode, isCalibrated);

    if (axisControlMode === 'step') {
      // Step mode: single click triggers a complete move by stepSize degrees
      performStepMove(axisId, direction);
      return;
    }

    // Velocity mode: prevent duplicate, then start continuous movement
    if (activeAxisTimer) {
      console.log('[AxisCtrl] startAxisMovement: stopping previous movement on axis %d', activeAxisId);
      stopAxisMovement(activeAxisId);
    }

    activeAxisId = axisId;
    activeDirection = direction;

    if (isCalibrated) {
      // Calibrated velocity mode: repeated coordinate nudges on a timer
      console.log('[AxisCtrl] startAxisMovement: starting CALIBRATED velocity mode, interval 250ms');
      if (activeAxisTimer) clearInterval(activeAxisTimer);
      activeAxisTimer = setInterval(() => {
        const speed = getCurrentSpeed(axisId);
        const delta = speed * 0.25; // nudge 4× per second
        performCalibratedNudge(axisId, direction, delta);
      }, 250);
    } else {
      // Uncalibrated velocity mode: single ControlAxis (controller maintains velocity)
      console.log('[AxisCtrl] startAxisMovement: starting UNCALIBRATED velocity mode');
      performVelocityMove(axisId, direction);
    }
  }

  /**
   * Stop axis movement.
   * In uncalibrated mode: sends StopAxis.
   * In calibrated mode: no-op (nudge is instantaneous).
   *
   * @param {number} axisId
   */
  function stopAxisMovement(axisId) {
    if (axisControlMode === 'step') {
      // Step mode: movement auto-completes via its own timeout, nothing to stop here
      return;
    }

    // Velocity mode: stop continuous movement
    if (activeAxisTimer) {
      clearInterval(activeAxisTimer);
      activeAxisTimer = null;
    }

    if (!isCalibrated && activeAxisId >= 0) {
      const deceleration = getCurrentDeceleration();
      Api.stopAxis(axisId, deceleration).catch(() => {});
    }
    // Calibrated velocity mode: handled by clearing the interval above

    activeAxisId = -1;
    activeDirection = 0;
  }

  /**
   * Perform a velocity-based axis move (uncalibrated mode).
   * Sends ControlAxis on press and keeps velocity active (single shot for
   * velocity mode — the controller maintains velocity until StopAxis).
   *
   * @param {number} axisId
   * @param {number} direction
   */
  async function performVelocityMove(axisId, direction) {
    const speed = getCurrentSpeed(axisId);
    const velocity = direction * speed;
    const acceleration = getCurrentAcceleration();
    console.log('[AxisCtrl] performVelocityMove: axisId=%d, direction=%d, speed=%f, velocity=%f, accel=%f, isCalibrated=%s',
                axisId, direction, speed, velocity, acceleration, isCalibrated);

    try {
      await Api.moveAxis(axisId, velocity, acceleration);
      console.log('[AxisCtrl] performVelocityMove: SUCCESS axisId=%d, velocity=%f, accel=%f', axisId, velocity, acceleration);
    } catch (err) {
      console.error('[AxisCtrl] performVelocityMove: FAILED axisId=%d, velocity=%f, error=%s', axisId, velocity, err.message);
      App.showToast(`Axis move failed: ${err.message}`, 'error');
    }
  }

  /**
   * Perform a coordinate-aware nudge (calibrated mode).
   * Uses the current position from App's last known state to compute
   * a small offset, then calls the appropriate slew RPC.
   *
   * @param {number} axisId   - 0 (RA/Az) or 1 (Dec/Alt)
   * @param {number} direction - +1 or -1
   */
  async function performCalibratedNudge(axisId, direction, overrideDelta) {
    console.log('[AxisCtrl] performCalibratedNudge: axisId=%d, direction=%d, overrideDelta=%s',
                axisId, direction, overrideDelta !== undefined ? overrideDelta.toFixed(4) : 'undefined');
    const state = App.getLastState();
    if (!state || !state.position) {
      App.showToast('No position data available for nudge', 'error');
      return;
    }

    const speed = getCurrentSpeed(axisId);
    const delta = (overrideDelta !== undefined) ? overrideDelta : speed * 0.5;
    const offset = direction * delta;

    try {
      if (currentMountType === 'alt_az') {
        // Alt-Az: use SlewToHorizontal
        const currentAlt = state.position.axis2; // Axis 2 = Altitude
        const currentAz = state.position.axis1;  // Axis 1 = Azimuth

        let newAlt = axisId === 1 ? currentAlt + offset : currentAlt;
        let newAz = axisId === 0 ? currentAz + offset : currentAz;

        // Clamp to valid ranges
        newAlt = Math.max(0, Math.min(90, newAlt));
        newAz = ((newAz % 360) + 360) % 360;

        await Api.slewHorizontal(newAlt, newAz);
        App.showToast(`Nudging to Alt=${newAlt.toFixed(2)}°, Az=${newAz.toFixed(2)}°`, 'success', 1500);
      } else {
        // Equatorial: use SlewToCoordinates
        // Compute offset in RA (hours) and Dec (degrees)
        // The position.axis1 is HA for equatorial, but we need RA
        // Use tracked_object ra/dec if available, otherwise estimate from position
        let currentRa, currentDec;

        if (state.tracked_object && state.tracked_object.ra !== undefined) {
          currentRa = state.tracked_object.ra;
          currentDec = state.tracked_object.dec;
        } else {
          // Fallback: use axis positions as rough estimate
          // axis1 = HA (hours), axis2 = Dec (degrees) for equatorial
          currentRa = state.position.axis1; // Approximate
          currentDec = state.position.axis2;
        }

        // RA offset in hours (15 degrees per hour)
        const raDelta = axisId === 0 ? offset / 15 : 0;
        const decDelta = axisId === 1 ? offset : 0;

        let newRa = currentRa + raDelta;
        let newDec = currentDec + decDelta;

        // Normalize
        newRa = ((newRa % 24) + 24) % 24;
        newDec = Math.max(-90, Math.min(90, newDec));

        await Api.slewToCoordinates(newRa, newDec);
        App.showToast(`Nudging to RA=${newRa.toFixed(4)}h, Dec=${newDec.toFixed(2)}°`, 'success', 1500);
      }
    } catch (err) {
      App.showToast(`Nudge failed: ${err.message}`, 'error');
    }
  }

  /**
   * Perform a step move (Mode 1): move axis by the configured step size at the current speed.
   * Works in both calibrated and uncalibrated states.
   *
   * @param {number} axisId   - 0 (horizontal) or 1 (vertical)
   * @param {number} direction - +1 or -1
   */
  async function performStepMove(axisId, direction) {
    const speed = getCurrentSpeed(axisId);
    const stepSize = getStepSize(axisId);
    const offset = direction * stepSize;
    console.log('[AxisCtrl] performStepMove: axisId=%d, direction=%d, speed=%f, stepSize=%f, isCalibrated=%s',
                axisId, direction, speed, stepSize, isCalibrated);

    activeAxisId = axisId;
    activeDirection = direction;

    if (isCalibrated) {
      // Calibrated: single coordinate nudge by stepSize degrees
      // The speed slider controls the slew velocity used to reach the target.
      await performCalibratedNudge(axisId, direction, stepSize);
      App.showToast(`Step ${stepSize.toFixed(1)}° on axis ${axisId}`, 'success', 1500);
    } else {
      // Uncalibrated: use POSITION_CONTROL with relative offset.
      // The drive handles the CiA 402 profile position move — acceleration,
      // deceleration, and automatic stop at the target.  No timer needed.
      // Pass the speed slider value as max_velocity so the drive respects it.
      const offset = direction * stepSize;
      const acceleration = getCurrentAcceleration();
      const deceleration = getCurrentDeceleration();
      console.log('[AxisCtrl] performStepMove uncalibrated: offset=%f°, speed=%f °/s, accel=%f °/s², decel=%f °/s²',
                  offset, speed, acceleration, deceleration);

      try {
        await Api.moveAxisRelative(axisId, offset, speed, acceleration, deceleration);
        console.log('[AxisCtrl] performStepMove: POSITION_CONTROL SUCCESS');
        App.showToast(`Step ${stepSize.toFixed(1)}° on axis ${axisId}`, 'success', 1500);
      } catch (err) {
        console.error('[AxisCtrl] performStepMove: FAILED: %s', err.message);
        App.showToast(`Step move failed: ${err.message}`, 'error');
      }
      activeAxisId = -1;
      activeDirection = 0;
    }
  }

  async function handleEmergencyStop() {
    // Stop any active movement first
    if (activeAxisTimer) {
      clearInterval(activeAxisTimer);
      activeAxisTimer = null;
    }
    activeAxisId = -1;
    activeDirection = 0;

    const btn = $('#btn-emergency-stop');
    if (btn) btn.disabled = true;

    try {
      await Api.emergencyStop();
      App.showToast('🛑 EMERGENCY STOP — all axes halted!', 'error', 5000);
    } catch (err) {
      App.showToast(`Emergency stop failed: ${err.message}`, 'error');
    } finally {
      if (btn) btn.disabled = false;
    }
  }

  // ─── Public API ──────────────────────────────────────────────────────

  return {
    init,
    setCalibrationState,
  };
})();
