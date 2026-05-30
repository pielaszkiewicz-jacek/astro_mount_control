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

  // ─── Initialization ─────────────────────────────────────────────────

  /**
   * Initialize the mount control component.
   * Binds all event listeners.
   */
  function init() {
    initSlewForm();
    initQuickActions();
    initAxisControl();
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

  // ─── Axis Control ────────────────────────────────────────────────────

  /**
   * Initialize the axis control pad: bind pointer events to axis buttons,
   * speed slider, and emergency stop.
   */
  function initAxisControl() {
    const speedSlider = $('#axis-speed');
    if (speedSlider) {
      speedSlider.addEventListener('input', updateSpeedLabel);
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
    // Prevent duplicate calls
    if (activeAxisTimer) {
      stopAxisMovement(activeAxisId);
    }

    activeAxisId = axisId;
    activeDirection = direction;

    if (isCalibrated) {
      // Calibrated mode: single coordinate nudge
      performCalibratedNudge(axisId, direction);
    } else {
      // Uncalibrated mode: continuous velocity control
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
    if (activeAxisTimer) {
      clearInterval(activeAxisTimer);
      activeAxisTimer = null;
    }

    if (!isCalibrated && activeAxisId >= 0) {
      // Only stop if we were moving this axis
      Api.stopAxis(axisId).catch(() => {});
    }

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

    try {
      await Api.moveAxis(axisId, velocity);
      App.showToast(`Axis ${axisId} moving at ${velocity.toFixed(1)}°/s`, 'info', 1500);
    } catch (err) {
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
  async function performCalibratedNudge(axisId, direction) {
    const state = App.getLastState();
    if (!state || !state.position) {
      App.showToast('No position data available for nudge', 'error');
      return;
    }

    const speed = getCurrentSpeed();
    const delta = speed * 0.5; // Nudge by half-second worth of movement (degrees)
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
   * Handle emergency stop button.
   */
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
