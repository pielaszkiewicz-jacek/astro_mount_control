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

  // ─── Initialization ─────────────────────────────────────────────────

  /**
   * Initialize the mount control component.
   * Binds all event listeners.
   */
  function init() {
    initSlewForm();
    initQuickActions();
    initAxisControl();
    initAxisModeControls();
    initStateFileInput();
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

    const ra = parseFloat(raInput.value);
    const dec = parseFloat(decInput.value);

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
      App.showToast(result.message || `Slewing to RA=${ra}h, Dec=${dec}°`, 'success');
    } catch (err) {
      App.showToast(`Slew failed: ${err.message}`, 'error');
    } finally {
      if (submitBtn) submitBtn.disabled = false;
    }
  }

  // ─── Quick Actions ───────────────────────────────────────────────────

  function initQuickActions() {
    const stopBtn = $('#btn-stop');
    if (stopBtn) stopBtn.addEventListener('click', handleStop);

    const parkBtn = $('#btn-park');
    if (parkBtn) parkBtn.addEventListener('click', handlePark);

    const unparkBtn = $('#btn-unpark');
    if (unparkBtn) unparkBtn.addEventListener('click', handleUnpark);

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
        if (toggleLabel) toggleLabel.textContent = 'Step';
        if (stepControl) stepControl.style.display = 'flex';
        toggleBtn.title = 'Step mode: single click moves axis by the set angle. Click to switch to Velocity mode.';
      } else {
        axisControlMode = 'velocity';
        if (toggleLabel) toggleLabel.textContent = 'Velocity';
        if (stepControl) stepControl.style.display = 'none';
        toggleBtn.title = 'Velocity mode: hold button to rotate axis, release to stop. Click to switch to Step mode.';
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
  function getStepSize() {
    const input = $('#axis-step-size');
    if (input) {
      const val = parseFloat(input.value);
      if (!isNaN(val) && val > 0) return val;
    }
    return stepSizeDeg;
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
        slider.max = maxSpeed;
        if (parseFloat(slider.value) > maxSpeed) {
          slider.value = maxSpeed;
        }
        updateSpeedLabel();
        console.log('[AxisCtrl] Speed slider max synced to config.max_slew_rate = %f', maxSpeed);
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
      label.textContent = parseFloat(slider.value).toFixed(1);
    }
  }

  /**
   * Get the current speed from the slider.
   * @returns {number} Speed in deg/s
   */
  function getCurrentSpeed() {
    const slider = $('#axis-speed');
    return slider ? parseFloat(slider.value) : 1.0;
  }

  /**
   * Update the calibration state based on latest status.
   * Called by the polling loop via the public setCalibrationState().
   *
   * @param {object} state - Controller state from API
   * @param {string} mountType - 'equatorial' | 'alt_az' | 'unknown'
   */
  function setCalibrationState(state, mountType) {
    currentMountType = mountType || 'unknown';

    // Mount is considered calibrated when in TRACKING or SLEWING state
    const status = (state && state.status || '').toUpperCase();
    isCalibrated = (status === 'TRACKING' || status === 'SLEWING');

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
    } else {
      // equatorial (default) or unknown
      horizLabel = 'RA';
      vertLabel = 'Dec';
    }

    if (isCalibrated) {
      badge.className = 'status-badge tracking';
      badge.textContent = 'Astronomical Mode';
      infoText.textContent = `Calibrated — nudging ${horizLabel}/${vertLabel} coordinates (${currentMountType === 'alt_az' ? 'Alt-Az' : 'Equatorial'} mount)`;
    } else {
      badge.className = 'status-badge idle';
      badge.textContent = 'Low-Level Mode';
      infoText.textContent = `Uncalibrated — direct axis velocity control (axis 0=${horizLabel}, axis 1=${vertLabel})`;
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
        const speed = getCurrentSpeed();
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
      Api.stopAxis(axisId).catch(() => {});
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
    const speed = getCurrentSpeed();
    const velocity = direction * speed;
    console.log('[AxisCtrl] performVelocityMove: axisId=%d, direction=%d, speed=%f, velocity=%f, isCalibrated=%s',
                axisId, direction, speed, velocity, isCalibrated);

    try {
      await Api.moveAxis(axisId, velocity);
      console.log('[AxisCtrl] performVelocityMove: SUCCESS axisId=%d, velocity=%f', axisId, velocity);
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

    const speed = getCurrentSpeed();
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
    const speed = getCurrentSpeed();
    const stepSize = getStepSize();
    const offset = direction * stepSize;
    console.log('[AxisCtrl] performStepMove: axisId=%d, direction=%d, speed=%f, stepSize=%f, isCalibrated=%s',
                axisId, direction, speed, stepSize, isCalibrated);

    activeAxisId = axisId;
    activeDirection = direction;

    if (isCalibrated) {
      // Calibrated: single coordinate nudge by stepSize degrees
      await performCalibratedNudge(axisId, direction, stepSize);
      App.showToast(`Step ${stepSize.toFixed(1)}° on axis ${axisId}`, 'success', 1500);
    } else {
      // Uncalibrated: use POSITION_CONTROL with relative offset.
      // The drive handles the CiA 402 profile position move — acceleration,
      // deceleration, and automatic stop at the target.  No timer needed.
      const offset = direction * stepSize;
      console.log('[AxisCtrl] performStepMove uncalibrated: offset=%f°', offset);

      try {
        await Api.moveAxisRelative(axisId, offset);
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
