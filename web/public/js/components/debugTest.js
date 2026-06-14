/**
 * Astronomical Mount Controller - Tests Component
 *
 * Provides a "Tests" tab for simulating and testing the mount controller.
 * Capabilities:
 *   - Mount type selection (EQUATORIAL, ALT_AZ, CASUAL)
 *   - CASUAL mount orientation via axis angles → quaternion
 *   - Euler angles (ZYX: yaw→pitch→roll) → rotation matrix → quaternion
 *   - Predefined Euler angles for EQUATORIAL and ALT_AZ mount types
 *   - Reference object coordinate transformation (celestial → telescope frame)
 *   - Physical servo slew commands for initial calibration
 *   - Axis position override for simulation
 *   - Calibration error injection (polar misalignment, non-perpendicularity, etc.)
 *   - Test action buttons (slew, track, flip, error injection)
 *   - Simulation output console
 */

const DebugTestComponent = (() => {
  'use strict';

  const { $, $$ } = Utils;

  // ─── State ────────────────────────────────────────────────────────────

  /** Current mount type: 0=EQUATORIAL, 1=ALT_AZ, 3=CASUAL */
  let mountType = 3;

  /** Cached observer latitude/longitude for predefined Euler angles */
  let observerLatitude = 52.0;
  let observerLongitude = 21.0;

  /** Current 3x3 rotation matrix (flat array, row-major) */
  let rotationMatrix = [
    1, 0, 0,
    0, 1, 0,
    0, 0, 1
  ];

  // ─── Initialization ───────────────────────────────────────────────────

  function init() {
    buildTestsHelpContent();
    // Re-render help content when language changes
    document.addEventListener('i18n:applied', buildTestsHelpContent);
    bindMountTypeSelector();
    bindCasualOrientation();
    bindEulerAnglesSection();
    bindReferenceObjectSection();
    bindCardToggles();
    bindAxisPositionSync();
    bindTestActions();
    bindClearOutput();
    bindFieldRotationSection();
    bindAstrometricCorrections();
    // Listen for tab switches to load current state
    listenForTabActivation();
  }

  /**
   * Listen for tab activation to populate fields with current controller state.
   */
  function listenForTabActivation() {
    const tab = $('.tab-btn[data-tab="debug"]');
    if (!tab) return;

    tab.addEventListener('click', () => {
      loadCurrentOrientation();
      loadCurrentConfig();
      // Re-run angle input enhancement for dynamically-shown inputs
      Utils.enhanceAllAngleInputs(document.getElementById('panel-debug'));
    });
  }

  // ─── Mount Type Selector ──────────────────────────────────────────────

  function bindMountTypeSelector() {
    const select = $('#debug-mount-type');
    const casualOrient = $('#debug-casual-orient');
    const axis1Label = $('#debug-pos-axis1-label');
    const axis2Label = $('#debug-pos-axis2-label');

    console.log('[DebugTest] bindMountTypeSelector: select found=', !!select, 'casualOrient=', !!casualOrient, 'axis1Label=', !!axis1Label, 'axis2Label=', !!axis2Label);

    if (!select) { console.log('[DebugTest] bindMountTypeSelector: NO SELECT ELEMENT - aborting!'); return; }

    select.addEventListener('change', () => {
      console.log('[DebugTest] mount-type change fired. select.value=', select.value);
      mountType = parseInt(select.value, 10);
      console.log('[DebugTest] mountType set to', mountType);
      if (casualOrient) {
        casualOrient.style.display = (mountType === 3) ? 'block' : 'none';
        console.log('[DebugTest] casualOrient display=', casualOrient.style.display);
      }
      updateAxisLabels(axis1Label, axis2Label);
      console.log('[DebugTest] calling onMountTypeChanged...');
      onMountTypeChanged();
      console.log('[DebugTest] onMountTypeChanged done');
    });

    // Initial state
    if (casualOrient) {
      casualOrient.style.display = (mountType === 3) ? 'block' : 'none';
    }
    updateAxisLabels(axis1Label, axis2Label);
    onMountTypeChanged();
  }

  function updateAxisLabels(label1, label2) {
    if (!label1 || !label2) return;
    switch (mountType) {
      case 0: // EQUATORIAL
        label1.textContent = I18n.t('tests.axis1_equatorial');
        label2.textContent = I18n.t('tests.axis2_equatorial');
        break;
      case 1: // ALT_AZ
        label1.textContent = I18n.t('tests.axis1_altaz');
        label2.textContent = I18n.t('tests.axis2_altaz');
        break;
      case 3: // CASUAL
        label1.textContent = I18n.t('tests.axis1_casual');
        label2.textContent = I18n.t('tests.axis2_casual');
        break;
      default:
        label1.textContent = I18n.t('tests.axis1_default');
        label2.textContent = I18n.t('tests.axis2_default');
    }
  }

  // ─── CASUAL Orientation (Axis Angles → Quaternion) ────────────────────

  function bindCasualOrientation() {
    const altInput = $('#debug-axis1-alt');
    const azInput = $('#debug-axis1-az');
    const applyBtn = $('#btn-debug-apply-orient');

    if (altInput) {
      altInput.addEventListener('input', () => updateQuaternionFromAngles());
    }
    if (azInput) {
      azInput.addEventListener('input', () => updateQuaternionFromAngles());
    }

    if (applyBtn) {
      applyBtn.addEventListener('click', applyOrientationToController);
    }
  }

  /**
   * Compute quaternion from axis1 altitude/azimuth and update read-only fields.
   * Uses the same algorithm as MountController::MountOrientation::setFromAxisAngles().
   */
  function updateQuaternionFromAngles() {
    const altInput = $('#debug-axis1-alt');
    const azInput = $('#debug-axis1-az');

    const altDeg = altInput ? parseFloat(altInput.value) : 45;
    const azDeg = azInput ? parseFloat(azInput.value) : 0;

    if (isNaN(altDeg) || isNaN(azDeg)) return;

    const altRad = altDeg * Math.PI / 180;
    const azRad = azDeg * Math.PI / 180;

    const north = Math.cos(altRad) * Math.cos(azRad);
    const east  = Math.cos(altRad) * Math.sin(azRad);
    const up    = Math.sin(altRad);

    // Q_conj: rotate (0,0,1) to ENU axis1 direction
    // v = (0,0,1) × (north, east, up) = (-east, north, 0)
    // s = 1 + up
    const vx = -east;
    const vy =  north;
    const vz = 0.0;
    const s  = 1.0 + up;

    const norm = Math.sqrt(vx*vx + vy*vy + vz*vz + s*s);
    let qx, qy, qz, qw;
    if (norm > 0) {
      // Q = conjugate(Q_conj): negate vector part
      qx = -vx / norm;
      qy = -vy / norm;
      qz = -vz / norm;
      qw =  s / norm;
    } else {
      qx = 0; qy = 0; qz = 0; qw = 1;
    }

    const qxEl = $('#debug-qx');
    const qyEl = $('#debug-qy');
    const qzEl = $('#debug-qz');
    const qwEl = $('#debug-qw');

    if (qxEl) qxEl.value = qx.toFixed(6);
    if (qyEl) qyEl.value = qy.toFixed(6);
    if (qzEl) qzEl.value = qz.toFixed(6);
    if (qwEl) qwEl.value = qw.toFixed(6);
  }

  /**
   * Send the quaternion to the controller via SetMountOrientation gRPC.
   */
  async function applyOrientationToController() {
    const qxEl = $('#debug-qx');
    const qyEl = $('#debug-qy');
    const qzEl = $('#debug-qz');
    const qwEl = $('#debug-qw');

    const orient = {
      qx: qxEl ? parseFloat(qxEl.value) : 0,
      qy: qyEl ? parseFloat(qyEl.value) : 0,
      qz: qzEl ? parseFloat(qzEl.value) : 0,
      qw: qwEl ? parseFloat(qwEl.value) : 1,
    };

    try {
      await Api.setMountOrientation(orient);
      logOutput(`Mount orientation applied: qx=${orient.qx.toFixed(6)}, qy=${orient.qy.toFixed(6)}, qz=${orient.qz.toFixed(6)}, qw=${orient.qw.toFixed(6)}`);
      App.showToast('Mount orientation applied', 'success');
    } catch (err) {
      logOutput(`ERROR: Failed to apply orientation: ${err.message}`, true);
      App.showToast(`Orientation failed: ${err.message}`, 'error');
    }
  }

  /**
   * Load current orientation from the controller and populate fields.
   */
  async function loadCurrentOrientation() {
    try {
      const orient = await Api.getMountOrientation();
      const qxEl = $('#debug-qx');
      const qyEl = $('#debug-qy');
      const qzEl = $('#debug-qz');
      const qwEl = $('#debug-qw');
      if (qxEl) qxEl.value = (orient.qx || 0).toFixed(6);
      if (qyEl) qyEl.value = (orient.qy || 0).toFixed(6);
      if (qzEl) qzEl.value = (orient.qz || 0).toFixed(6);
      if (qwEl) qwEl.value = (orient.qw || 1).toFixed(6);
      logOutput(`Loaded orientation: qx=${(orient.qx||0).toFixed(4)}, qy=${(orient.qy||0).toFixed(4)}, qz=${(orient.qz||0).toFixed(4)}, qw=${(orient.qw||1).toFixed(4)}`);
    } catch (err) {
      logOutput(`Could not load orientation: ${err.message}`, true);
    }
  }

  /**
   * Load current config to sync mount type selector and observer coordinates.
   */
  async function loadCurrentConfig() {
    try {
      const config = await Api.getConfig();
      const select = $('#debug-mount-type');
      if (select && config.mount_type !== undefined) {
        select.value = config.mount_type;
        mountType = config.mount_type;
        select.dispatchEvent(new Event('change'));
        logOutput(`Loaded config: mount_type=${mountType}`);
      }
      // Cache observer coordinates for Euler angle precomputation
      if (config.latitude !== undefined) {
        observerLatitude = config.latitude;
      }
      if (config.longitude !== undefined) {
        observerLongitude = config.longitude;
      }
      logOutput(`Observer location: lat=${observerLatitude}°, lon=${observerLongitude}°`);
    } catch (err) {
      logOutput(`Could not load config: ${err.message}`, true);
    }
  }

  // ─── Euler Angles Section ───────────────────────────────────────────────

  /**
   * Bind event handlers for the Euler angles & rotation matrix section.
   */
  function bindEulerAnglesSection() {
    const applyBtn = $('#btn-debug-apply-euler');
    const resetBtn = $('#btn-debug-reset-euler');

    // Auto-update rotation matrix on Euler angle input changes
    const yawInput = $('#debug-euler-yaw');
    const pitchInput = $('#debug-euler-pitch');
    const rollInput = $('#debug-euler-roll');

    if (yawInput) yawInput.addEventListener('input', () => {
      computeRotationMatrixFromInputs();
      updateRotationMatrixDisplay();
    });
    if (pitchInput) pitchInput.addEventListener('input', () => {
      computeRotationMatrixFromInputs();
      updateRotationMatrixDisplay();
    });
    if (rollInput) rollInput.addEventListener('input', () => {
      computeRotationMatrixFromInputs();
      updateRotationMatrixDisplay();
    });

    if (applyBtn) {
      applyBtn.addEventListener('click', () => {
        computeRotationMatrixFromInputs();
        updateRotationMatrixDisplay();
        applyEulerToController();
      });
    }

    if (resetBtn) {
      resetBtn.addEventListener('click', resetEulerToMountDefaults);
    }
  }

  /**
   * Called when mount type changes — update Euler angles to predefined values
   * for EQUATORIAL and ALT_AZ, and enable/disable inputs accordingly.
   */
  function onMountTypeChanged() {
    console.log('[DebugTest] onMountTypeChanged called, mountType=', mountType);
    const yawInput = $('#debug-euler-yaw');
    const pitchInput = $('#debug-euler-pitch');
    const rollInput = $('#debug-euler-roll');
    const hint = $('#debug-euler-hint');
    console.log('[DebugTest] yawInput found=', !!yawInput, 'pitchInput=', !!pitchInput, 'rollInput=', !!rollInput, 'hint=', !!hint);

    switch (mountType) {
      case 0: // EQUATORIAL — polar axis aligned to celestial pole
        // Pitch = 90° - latitude (tilt from horizontal to polar axis)
        // Yaw = 0 (aligned to north meridian)
        // Roll = 0
        if (yawInput) { yawInput.value = '0.0'; yawInput.readOnly = true; }
        if (pitchInput) { pitchInput.value = (90.0 - observerLatitude).toFixed(4); pitchInput.readOnly = true; }
        if (rollInput) { rollInput.value = '0.0'; rollInput.readOnly = true; }
        if (hint) hint.textContent = I18n.t('tests.euler_hint_eq');
        break;

      case 1: // ALT_AZ — horizontal mount, identity orientation
        if (yawInput) { yawInput.value = '0.0'; yawInput.readOnly = true; }
        if (pitchInput) { pitchInput.value = '0.0'; pitchInput.readOnly = true; }
        if (rollInput) { rollInput.value = '0.0'; rollInput.readOnly = true; }
        if (hint) hint.textContent = I18n.t('tests.euler_hint_altaz');
        break;

      case 3: // CASUAL — user-defined
      default:
        if (yawInput) yawInput.readOnly = false;
        if (pitchInput) pitchInput.readOnly = false;
        if (rollInput) rollInput.readOnly = false;
        if (hint) hint.textContent = I18n.t('tests.euler_hint_casual');
        break;
    }

    console.log('[DebugTest] onMountTypeChanged switch done, calling computeRotationMatrix...');
    computeRotationMatrixFromInputs();
    console.log('[DebugTest] computeRotationMatrix done, calling updateRotationMatrixDisplay...');
    updateRotationMatrixDisplay();
    console.log('[DebugTest] updateRotationMatrixDisplay done');
  }

  /**
   * Reset Euler angles to the predefined defaults for the current mount type.
   */
  function resetEulerToMountDefaults() {
    onMountTypeChanged();
    logOutput('Euler angles reset to mount type defaults.');
    App.showToast('Euler angles reset to defaults', 'info');
  }

  /**
   * Compute rotation matrix from the Euler angle input fields.
   * Uses ZYX intrinsic convention: R = Rz(yaw) * Ry(pitch) * Rx(roll)
   */
  function computeRotationMatrixFromInputs() {
    const yawDeg = readFloat('debug-euler-yaw', 0);
    const pitchDeg = readFloat('debug-euler-pitch', 0);
    const rollDeg = readFloat('debug-euler-roll', 0);

    rotationMatrix = eulerZYXToMatrix(yawDeg, pitchDeg, rollDeg);
  }

  /**
   * Convert Euler angles (ZYX intrinsic = yaw, pitch, roll) in degrees
   * to a 3x3 rotation matrix (flat array, row-major).
   *
   * R = Rz(yaw) * Ry(pitch) * Rx(roll)
   * Where:
   *   Rz(ψ) = rotation around Z by ψ (yaw)
   *   Ry(θ) = rotation around Y by θ (pitch)
   *   Rx(φ) = rotation around X by φ (roll)
   *
   * This matrix transforms vectors from the local ENU frame to the mount frame.
   */
  function eulerZYXToMatrix(yawDeg, pitchDeg, rollDeg) {
    const yaw = yawDeg * Math.PI / 180;
    const pitch = pitchDeg * Math.PI / 180;
    const roll = rollDeg * Math.PI / 180;

    const cy = Math.cos(yaw);
    const sy = Math.sin(yaw);
    const cp = Math.cos(pitch);
    const sp = Math.sin(pitch);
    const cr = Math.cos(roll);
    const sr = Math.sin(roll);

    // R = Rz(yaw) * Ry(pitch) * Rx(roll)
    // Row 0
    const r00 = cy * cp;
    const r01 = cy * sp * sr - sy * cr;
    const r02 = cy * sp * cr + sy * sr;
    // Row 1
    const r10 = sy * cp;
    const r11 = sy * sp * sr + cy * cr;
    const r12 = sy * sp * cr - cy * sr;
    // Row 2
    const r20 = -sp;
    const r21 = cp * sr;
    const r22 = cp * cr;

    return [r00, r01, r02, r10, r11, r12, r20, r21, r22];
  }

  /**
   * Update the rotation matrix display (3×3 table) and quaternion display.
   */
  function updateRotationMatrixDisplay() {
    const m = rotationMatrix;
    setSpanText('debug-rm-00', m[0]);
    setSpanText('debug-rm-01', m[1]);
    setSpanText('debug-rm-02', m[2]);
    setSpanText('debug-rm-10', m[3]);
    setSpanText('debug-rm-11', m[4]);
    setSpanText('debug-rm-12', m[5]);
    setSpanText('debug-rm-20', m[6]);
    setSpanText('debug-rm-21', m[7]);
    setSpanText('debug-rm-22', m[8]);

    // Compute and display the quaternion
    const q = rotationMatrixToQuaternion(rotationMatrix);
    setSpanText('debug-euler-qx', q.qx);
    setSpanText('debug-euler-qy', q.qy);
    setSpanText('debug-euler-qz', q.qz);
    setSpanText('debug-euler-qw', q.qw);
  }

  function setSpanText(id, value) {
    const el = $(`#${id}`);
    if (el) el.textContent = value.toFixed(4);
  }

  /**
   * Convert rotation matrix to quaternion and apply to controller.
   */
  async function applyEulerToController() {
    const q = rotationMatrixToQuaternion(rotationMatrix);

    // Update quaternion display fields
    const qxEl = $('#debug-qx');
    const qyEl = $('#debug-qy');
    const qzEl = $('#debug-qz');
    const qwEl = $('#debug-qw');
    if (qxEl) qxEl.value = q.qx.toFixed(6);
    if (qyEl) qyEl.value = q.qy.toFixed(6);
    if (qzEl) qzEl.value = q.qz.toFixed(6);
    if (qwEl) qwEl.value = q.qw.toFixed(6);

    // Send to controller
    try {
      await Api.setMountOrientation({ qx: q.qx, qy: q.qy, qz: q.qz, qw: q.qw });
      logOutput(`Euler angles applied. Rotation matrix updated. Quaternion: qx=${q.qx.toFixed(6)}, qy=${q.qy.toFixed(6)}, qz=${q.qz.toFixed(6)}, qw=${q.qw.toFixed(6)}`);
      App.showToast('Euler angles & rotation matrix applied', 'success');
    } catch (err) {
      logOutput(`ERROR: Failed to apply orientation from Euler angles: ${err.message}`, true);
      App.showToast(`Euler apply failed: ${err.message}`, 'error');
    }
  }

  /**
   * Convert a 3x3 rotation matrix (flat array, row-major) to a unit quaternion.
   * Returns { qx, qy, qz, qw }.
   */
  function rotationMatrixToQuaternion(m) {
    const r00 = m[0], r01 = m[1], r02 = m[2];
    const r10 = m[3], r11 = m[4], r12 = m[5];
    const r20 = m[6], r21 = m[7], r22 = m[8];

    const trace = r00 + r11 + r22;
    let qx, qy, qz, qw;

    if (trace > 0) {
      const s = 0.5 / Math.sqrt(trace + 1.0);
      qw = 0.25 / s;
      qx = (r21 - r12) * s;
      qy = (r02 - r20) * s;
      qz = (r10 - r01) * s;
    } else if (r00 > r11 && r00 > r22) {
      const s = 2.0 * Math.sqrt(1.0 + r00 - r11 - r22);
      qw = (r21 - r12) / s;
      qx = 0.25 * s;
      qy = (r01 + r10) / s;
      qz = (r02 + r20) / s;
    } else if (r11 > r22) {
      const s = 2.0 * Math.sqrt(1.0 + r11 - r00 - r22);
      qw = (r02 - r20) / s;
      qx = (r01 + r10) / s;
      qy = 0.25 * s;
      qz = (r12 + r21) / s;
    } else {
      const s = 2.0 * Math.sqrt(1.0 + r22 - r00 - r11);
      qw = (r10 - r01) / s;
      qx = (r02 + r20) / s;
      qy = (r12 + r21) / s;
      qz = 0.25 * s;
    }

    // Normalize
    const norm = Math.sqrt(qx*qx + qy*qy + qz*qz + qw*qw);
    if (norm > 0) {
      qx /= norm; qy /= norm; qz /= norm; qw /= norm;
    }

    return { qx, qy, qz, qw };
  }

  // ─── Card Toggle (Collapse/Expand) ─────────────────────────────────────

  /**
   * Bind click handlers for collapsible cards in the debug panel.
   * Clicking anywhere on the card header toggles the card-collapsed class.
   * The toggle button shows '+' when collapsed and '−' when expanded.
   */
  function bindCardToggles() {
    document.querySelectorAll('#panel-debug .card-collapsible').forEach(card => {
      const toggleBtn = card.querySelector('.card-toggle-btn');
      const header = card.querySelector('.card-header');

      const doToggle = () => {
        const collapsed = card.classList.toggle('card-collapsed');
        if (toggleBtn) {
          toggleBtn.textContent = collapsed ? '+' : '\u2212';
        }
      };

      // Click on the dedicated toggle button
      if (toggleBtn) {
        toggleBtn.addEventListener('click', (e) => {
          e.stopPropagation(); // prevent double-fire from header click
          doToggle();
        });
      }

      // Click anywhere on the card header (but not on interactive children)
      if (header) {
        header.addEventListener('click', (e) => {
          // Don't toggle when clicking buttons, inputs, selects, or links inside the header
          if (e.target.closest('button, input, select, a, label')) return;
          doToggle();
        });
      }
    });
  }

  // ─── Reference Object & Calibration Section ──────────────────────────────

  /**
   * Bind event handlers for the reference object section.
   */
  function bindReferenceObjectSection() {
    const transformBtn = $('#btn-debug-transform');
    const slewBtn = $('#btn-debug-slew-ref');
    const importBtn = $('#btn-debug-import-calib');

    if (transformBtn) {
      transformBtn.addEventListener('click', transformReferenceCoordinates);
    }
    if (slewBtn) {
      slewBtn.addEventListener('click', slewToTelescopeFrame);
    }
    if (importBtn) {
      importBtn.addEventListener('click', importFromCalibration);
    }
  }

  /**
   * Import the currently selected object from the Calibration tab
   * into the Tests tab reference object fields.
   */
  function importFromCalibration() {
    const sel = CalibrationComponent.getSelectedObject();
    // Prefer bootstrap selection, fall back to TPOINT
    const obj = sel.bs || sel.tp;
    if (!obj) {
      App.showToast('No object selected in Calibration tab', 'warning');
      return;
    }
    if (obj.ra_hours == null || obj.dec_degrees == null) {
      App.showToast('Selected object has no coordinates', 'error');
      return;
    }

    const raEl = $('#debug-ref-ra');
    const decEl = $('#debug-ref-dec');
    if (raEl) {
      if (raEl.setAngleDecimal) raEl.setAngleDecimal(obj.ra_hours);
      else raEl.value = obj.ra_hours;
    }
    if (decEl) {
      if (decEl.setAngleDecimal) decEl.setAngleDecimal(obj.dec_degrees);
      else decEl.value = obj.dec_degrees;
    }

    logOutput(`Imported from Calibration: ${obj.name || '(unnamed)'} — RA=${Number(obj.ra_hours).toFixed(4)}h, Dec=${Number(obj.dec_degrees).toFixed(2)}°`);
    App.showToast(`Imported: ${obj.name || 'object'} (RA=${Number(obj.ra_hours).toFixed(2)}h, Dec=${Number(obj.dec_degrees).toFixed(2)}°)`, 'success');
  }

  /**
   * Transform reference object celestial coordinates (RA/Dec) to telescope-frame
   * coordinates using the current rotation matrix.
   *
   * Steps:
   *   1. Convert RA/Dec to a unit vector in the celestial (ICRS) frame
   *   2. Convert to local ENU frame (requires observer latitude/LST)
   *   3. Apply rotation matrix R (ENU → Mount frame)
   *   4. Convert mount-frame unit vector back to axis angles (HA-like, Dec-like)
   */
  function transformReferenceCoordinates() {
    // Ensure rotation matrix is up-to-date
    computeRotationMatrixFromInputs();
    updateRotationMatrixDisplay();

    const raHours = readFloat('debug-ref-ra', 0);
    const decDeg = readFloat('debug-ref-dec', 0);

    if (isNaN(raHours) || isNaN(decDeg)) {
      logOutput('ERROR: Invalid reference coordinates.', true);
      App.showToast('Invalid reference coordinates', 'error');
      return;
    }

    const raRad = raHours * 15 * Math.PI / 180; // RA in radians (1h = 15°)
    const decRad = decDeg * Math.PI / 180;

    // Step 1: Celestial unit vector (ICRS, equatorial)
    // x points to vernal equinox, z to north celestial pole
    const cx = Math.cos(decRad) * Math.cos(raRad);
    const cy = Math.cos(decRad) * Math.sin(raRad);
    const cz = Math.sin(decRad);

    // Step 2: Convert celestial → ENU (East, North, Up)
    // We need the local sidereal time (LST). Approximate from observer longitude
    // and current UTC time.
    const lstRad = computeApproximateLST();

    // Rotation from celestial (equatorial) to ENU:
    // First rotate by LST around Z to align with local meridian,
    // then rotate by (90° - latitude) around East (X) axis.
    const sinLST = Math.sin(lstRad);
    const cosLST = Math.cos(lstRad);
    const sinLat = Math.sin(observerLatitude * Math.PI / 180);
    const cosLat = Math.cos(observerLatitude * Math.PI / 180);

    // Celestial → Horizontal (Az/Alt): rotate around Z by LST, then around X by (90°-lat)
    // ENU vector: E = -sinLST*cx + cosLST*cy
    //             N = -sinLat*cosLST*cx - sinLat*sinLST*cy + cosLat*cz
    //             U =  cosLat*cosLST*cx + cosLat*sinLST*cy + sinLat*cz
    const enuE = -sinLST * cx + cosLST * cy;
    const enuN = -sinLat * cosLST * cx - sinLat * sinLST * cy + cosLat * cz;
    const enuU =  cosLat * cosLST * cx + cosLat * sinLST * cy + sinLat * cz;

    // Step 3: Apply rotation matrix R (ENU → Mount frame)
    const m = rotationMatrix;
    const mfX = m[0] * enuE + m[1] * enuN + m[2] * enuU;
    const mfY = m[3] * enuE + m[4] * enuN + m[5] * enuU;
    const mfZ = m[6] * enuE + m[7] * enuN + m[8] * enuU;

    // Normalize
    const mfNorm = Math.sqrt(mfX*mfX + mfY*mfY + mfZ*mfZ);
    const nx = mfX / mfNorm;
    const ny = mfY / mfNorm;
    const nz = mfZ / mfNorm;

    // Step 4: Convert mount-frame vector to axis angles
    // Axis 1 (HA-like): azimuthal angle in the mount's XY plane, measured from +X
    // Axis 2 (Dec-like): elevation angle from the XY plane
    const axis1Deg = Math.atan2(ny, nx) * 180 / Math.PI;
    const axis2Deg = Math.asin(nz) * 180 / Math.PI;

    // Display transformed coordinates
    const axis1El = $('#debug-tf-axis1');
    const axis2El = $('#debug-tf-axis2');
    if (axis1El) axis1El.value = axis1Deg.toFixed(4);
    if (axis2El) axis2El.value = axis2Deg.toFixed(4);

    logOutput(`Transform: RA=${raHours.toFixed(3)}h, Dec=${decDeg.toFixed(3)}° → Axis1=${axis1Deg.toFixed(4)}°, Axis2=${axis2Deg.toFixed(4)}°`);
    logOutput(`  Celestial vector: [${cx.toFixed(4)}, ${cy.toFixed(4)}, ${cz.toFixed(4)}]`);
    logOutput(`  ENU vector: [${enuE.toFixed(4)}, ${enuN.toFixed(4)}, ${enuU.toFixed(4)}]`);
    logOutput(`  Mount-frame vector: [${nx.toFixed(4)}, ${ny.toFixed(4)}, ${nz.toFixed(4)}]`);
    App.showToast(`Transformed: Axis1=${axis1Deg.toFixed(2)}°, Axis2=${axis2Deg.toFixed(2)}°`, 'info');
  }

  /**
   * Compute an approximate Local Sidereal Time (LST) in radians.
   * Uses observer longitude and current UTC time.
   */
  function computeApproximateLST() {
    const now = new Date();

    // Compute Julian Date
    const jd = computeJulianDate(now);

    // GMST in hours: GMST = 18.697374558 + 24.06570982441908 * (JD - 2451545.0)
    const gmstHours = (18.697374558 + 24.06570982441908 * (jd - 2451545.0)) % 24;
    const gmstPositive = ((gmstHours % 24) + 24) % 24;

    // LST = GMST + longitude/15
    const lstHours = (gmstPositive + observerLongitude / 15.0) % 24;
    const lstPositive = ((lstHours % 24) + 24) % 24;

    return lstPositive * Math.PI / 12; // Convert hours to radians
  }

  /**
   * Compute Julian Date from a JavaScript Date object.
   */
  function computeJulianDate(date) {
    const y = date.getUTCFullYear();
    const m = date.getUTCMonth() + 1;
    const d = date.getUTCDate() + date.getUTCHours() / 24 + date.getUTCMinutes() / 1440 + date.getUTCSeconds() / 86400;

    let year = y;
    let month = m;
    if (month <= 2) {
      year -= 1;
      month += 12;
    }

    const A = Math.floor(year / 100);
    const B = 2 - A + Math.floor(A / 4);

    return Math.floor(365.25 * (year + 4716)) + Math.floor(30.6001 * (month + 1)) + d + B - 1524.5;
  }

  /**
   * Send physical slew commands to the servo motors for the transformed
   * telescope-frame coordinates.
   *
   * Uses the moveAxisRelative API to slew both axes to the computed positions.
   * Gets the current axis positions from live status first, then computes
   * the relative offsets needed.
   */
  async function slewToTelescopeFrame() {
    // First, ensure we have fresh transformed coordinates
    transformReferenceCoordinates();

    const axis1Target = readFloat('debug-tf-axis1', 0);
    const axis2Target = readFloat('debug-tf-axis2', 0);

    if (isNaN(axis1Target) || isNaN(axis2Target)) {
      logOutput('ERROR: No valid telescope-frame coordinates. Run Transform first.', true);
      App.showToast('Transform coordinates first', 'error');
      return;
    }

    try {
      // Get current mount position from live state
      const state = App.getLastState();
      if (!state || !state.position) {
        logOutput('ERROR: No live status data available. Cannot determine current position.', true);
        App.showToast('No live status available', 'error');
        return;
      }

      const currentAxis1 = state.position.axis1 || 0;
      const currentAxis2 = state.position.axis2 || 0;

      // Compute relative offsets
      let offset1 = axis1Target - currentAxis1;
      let offset2 = axis2Target - currentAxis2;

      // Normalize axis1 offset to [-180, 180] for shortest path
      while (offset1 > 180) offset1 -= 360;
      while (offset1 < -180) offset1 += 360;

      logOutput(`--- Slew to Telescope-Frame Position ---`);
      logOutput(`Current position: Axis1=${currentAxis1.toFixed(4)}°, Axis2=${currentAxis2.toFixed(4)}°`);
      logOutput(`Target position:  Axis1=${axis1Target.toFixed(4)}°, Axis2=${axis2Target.toFixed(4)}°`);
      logOutput(`Relative offset:  Axis1=${offset1.toFixed(4)}°, Axis2=${offset2.toFixed(4)}°`);

      // Send slew commands to both axes
      // We move axis2 (Dec/Altitude) first if it needs to move, then axis1
      const promises = [];

      if (Math.abs(offset2) > 0.001) {
        logOutput(`Slewing Axis2 by ${offset2.toFixed(4)}°...`);
        promises.push(Api.moveAxisRelative(1, offset2));
      }

      if (Math.abs(offset1) > 0.001) {
        logOutput(`Slewing Axis1 by ${offset1.toFixed(4)}°...`);
        promises.push(Api.moveAxisRelative(0, offset1));
      }

      if (promises.length === 0) {
        logOutput('Already at target position. No slew needed.');
        App.showToast('Already at target position', 'info');
        return;
      }

      await Promise.all(promises);
      logOutput('Slew commands sent to servos successfully.');
      App.showToast('Servo slew commands sent', 'success');
    } catch (err) {
      logOutput(`ERROR: Servo slew failed: ${err.message}`, true);
      App.showToast(`Servo slew failed: ${err.message}`, 'error');
    }
  }

  // ─── Axis Position Sync ────────────────────────────────────────────────

  function bindAxisPositionSync() {
    const syncBtn = $('#btn-debug-sync-pos');
    if (syncBtn) {
      syncBtn.addEventListener('click', syncPositionsFromLive);
    }
  }

  function syncPositionsFromLive() {
    const state = App.getLastState();
    if (!state || !state.position) {
      App.showToast('No live status data available', 'error');
      return;
    }

    const axis1Input = $('#debug-pos-axis1');
    const axis2Input = $('#debug-pos-axis2');
    const v1 = state.position.axis1 || 0;
    const v2 = state.position.axis2 || 0;
    if (axis1Input) {
      if (axis1Input.setAngleDecimal) axis1Input.setAngleDecimal(v1);
      else axis1Input.value = v1.toFixed(4);
    }
    if (axis2Input) {
      if (axis2Input.setAngleDecimal) axis2Input.setAngleDecimal(v2);
      else axis2Input.value = v2.toFixed(4);
    }

    logOutput(`Synced axis positions: axis1=${state.position.axis1?.toFixed(4)}°, axis2=${state.position.axis2?.toFixed(4)}°`);
    App.showToast('Axis positions synced from live status', 'info');
  }

  // ─── Test Actions ──────────────────────────────────────────────────────

  function bindTestActions() {
    bindBtn('btn-debug-test-slew-eq', testSlewEquatorial);
    bindBtn('btn-debug-test-slew-horiz', testSlewHorizontal);
    bindBtn('btn-debug-test-track', testTracking);
    bindBtn('btn-debug-test-flip', testMeridianFlip);
    bindBtn('btn-debug-inject-errors', injectCalibrationErrors);
    bindBtn('btn-debug-clear-errors', clearInjectedErrors);
    bindBtn('btn-debug-dump-state', dumpControllerState);
    bindBtn('btn-debug-reset-sim', resetSimulation);
  }

  function bindBtn(id, handler) {
    const btn = $(`#${id}`);
    if (btn) btn.addEventListener('click', handler);
  }

  // -- Test: Slew to Equatorial coordinates --

  async function testSlewEquatorial() {
    logOutput('--- Test: Slew to Equatorial ---');
    const ra = 10.5;   // hours
    const dec = 41.27; // degrees (Andromeda area)
    logOutput(`Slewing to RA=${ra}h, Dec=${dec}°`);

    try {
      const result = await Api.slewToCoordinates(ra, dec);
      logOutput(`Result: ${JSON.stringify(result)}`);
      App.showToast(`Slew to RA=${ra}h, Dec=${dec}° initiated`, 'info');
    } catch (err) {
      logOutput(`ERROR: ${err.message}`, true);
      App.showToast(`Slew failed: ${err.message}`, 'error');
    }
  }

  // -- Test: Slew to Horizontal coordinates --

  async function testSlewHorizontal() {
    logOutput('--- Test: Slew to Horizontal ---');
    const alt = 45.0;
    const az = 180.0;
    logOutput(`Slewing to Alt=${alt}°, Az=${az}°`);

    try {
      const result = await Api.slewHorizontal(alt, az);
      logOutput(`Result: ${JSON.stringify(result)}`);
      App.showToast(`Slew to Alt=${alt}°, Az=${az}° initiated`, 'info');
    } catch (err) {
      logOutput(`ERROR: ${err.message}`, true);
      App.showToast(`Slew failed: ${err.message}`, 'error');
    }
  }

  // -- Test: Start Tracking --

  async function testTracking() {
    logOutput('--- Test: Start Tracking ---');
    // Track Polaris-like coordinates
    const ra = 2.53;
    const dec = 89.26;

    // First update mount type if CASUAL is selected
    if (mountType === 3) {
      await ensureCasualOrientation();
    }

    logOutput(`Starting tracking: RA=${ra}h, Dec=${dec}°, mountType=${mountType}`);

    try {
      // We use UpdateConfiguration to set mount type before tracking
      if (mountType !== undefined) {
        await Api.updateConfig({ mount_type: mountType });
      }
      const result = await Api.startTracking(ra + '', { mode: 'SIDEREAL' });
      logOutput(`Result: ${JSON.stringify(result)}`);
      App.showToast('Tracking started (SIDEREAL)', 'info');
    } catch (err) {
      logOutput(`ERROR: ${err.message}`, true);
      App.showToast(`Tracking failed: ${err.message}`, 'error');
    }
  }

  // -- Test: Meridian Flip --

  async function testMeridianFlip() {
    logOutput('--- Test: Meridian Flip ---');
    logOutput('Executing meridian flip...');

    try {
      await Api.updateConfig({ meridian_flip_enabled: true, meridian_flip_delay_minutes: 0 });
      logOutput('Meridian flip config updated. Flip will auto-trigger when tracking near meridian.');
      logOutput('To force an immediate flip, position axis1 near 0° (meridian) and start tracking.');
      App.showToast('Meridian flip config set', 'info');
    } catch (err) {
      logOutput(`ERROR: ${err.message}`, true);
      App.showToast(`Flip setup failed: ${err.message}`, 'error');
    }
  }

  // -- Inject Calibration Errors --

  async function injectCalibrationErrors() {
    logOutput('--- Inject Calibration Errors ---');

    const polarAltErr = readFloat('debug-err-polar-alt', 0);
    const polarAzErr  = readFloat('debug-err-polar-az', 0);
    const nonperpHa   = readFloat('debug-err-nonperp-ha', 0);
    const collimation = readFloat('debug-err-collimation', 0);
    const flexure     = readFloat('debug-err-flexure', 0);
    const backlash    = readFloat('debug-err-backlash', 0);

    logOutput(`Polar Alt Error: ${polarAltErr}°`);
    logOutput(`Polar Az Error:  ${polarAzErr}°`);
    logOutput(`HA/Dec Non-Perp: ${nonperpHa}°`);
    logOutput(`Collimation:     ${collimation}°`);
    logOutput(`Tube Flexure:    ${flexure}°`);
    logOutput(`Backlash:        ${backlash}°`);

    try {
      const configUpdate = {};

      if (polarAltErr !== 0 || polarAzErr !== 0) {
        const currentConfig = await Api.getConfig();
        if (currentConfig) {
          configUpdate.latitude = (currentConfig.latitude || 0) + polarAltErr;
          configUpdate.longitude = (currentConfig.longitude || 0) + polarAzErr;
        }
      }

      if (Object.keys(configUpdate).length > 0) {
        await Api.updateConfig(configUpdate);
        logOutput(`Config updated: ${JSON.stringify(configUpdate)}`);
      }

      logOutput('Calibration errors injected. Run TPoint calibration to measure them.');
      App.showToast('Calibration errors injected', 'success');
    } catch (err) {
      logOutput(`ERROR: ${err.message}`, true);
      App.showToast(`Error injection failed: ${err.message}`, 'error');
    }
  }

  // -- Clear Injected Errors --

  async function clearInjectedErrors() {
    logOutput('--- Clear Injected Errors ---');

    const fields = [
      'debug-err-polar-alt', 'debug-err-polar-az',
      'debug-err-nonperp-ha', 'debug-err-collimation',
      'debug-err-flexure', 'debug-err-backlash',
    ];
    fields.forEach(id => {
      const el = $(`#${id}`);
      if (el) el.value = '0.0';
    });

    try {
      await Api.resetConfig();
      logOutput('Configuration reset to factory defaults.');
      App.showToast('Injected errors cleared', 'success');
    } catch (err) {
      logOutput(`ERROR: ${err.message}`, true);
      App.showToast(`Reset failed: ${err.message}`, 'error');
    }
  }

  // -- Dump Controller State --

  async function dumpControllerState() {
    logOutput('--- Dump Controller State ---');

    try {
      const status = await Api.getStatus();
      logOutput(JSON.stringify(status, null, 2));

      const config = await Api.getConfig();
      logOutput('--- Current Configuration ---');
      logOutput(JSON.stringify(config, null, 2));

      App.showToast('State dumped to output', 'info');
    } catch (err) {
      logOutput(`ERROR: ${err.message}`, true);
      App.showToast(`Dump failed: ${err.message}`, 'error');
    }
  }

  // -- Reset Simulation --

  async function resetSimulation() {
    logOutput('--- Reset Simulation ---');

    try {
      await Api.resetConfig();
      logOutput('Configuration reset to factory defaults');

      // Reset axis positions
      const axis1El = $('#debug-pos-axis1');
      const axis2El = $('#debug-pos-axis2');
      if (axis1El) axis1El.value = '0.0';
      if (axis2El) axis2El.value = '0.0';

      // Reset mount orientation to identity
      const qxEl = $('#debug-qx');
      const qyEl = $('#debug-qy');
      const qzEl = $('#debug-qz');
      const qwEl = $('#debug-qw');
      if (qxEl) qxEl.value = '0.000000';
      if (qyEl) qyEl.value = '0.000000';
      if (qzEl) qzEl.value = '0.000000';
      if (qwEl) qwEl.value = '1.000000';

      // Reset mount type to CASUAL
      const select = $('#debug-mount-type');
      if (select) {
        select.value = '3';
        mountType = 3;
        select.dispatchEvent(new Event('change'));
      }

      // Reset error fields
      const errorFields = [
        'debug-err-polar-alt', 'debug-err-polar-az',
        'debug-err-nonperp-ha', 'debug-err-collimation',
        'debug-err-flexure', 'debug-err-backlash',
      ];
      errorFields.forEach(id => {
        const el = $(`#${id}`);
        if (el) el.value = '0.0';
      });

      logOutput('Simulation fully reset.');
      App.showToast('Simulation reset complete', 'success');
    } catch (err) {
      logOutput(`ERROR: ${err.message}`, true);
      App.showToast(`Reset failed: ${err.message}`, 'error');
    }
  }

  // ─── Field Rotation Section ────────────────────────────────────────────

  function bindFieldRotationSection() {
    const computeBtn = $('#btn-debug-compute-field-rotation');
    const enableBtn = $('#btn-debug-enable-field-rotation');
    const disableBtn = $('#btn-debug-disable-field-rotation');
    const refreshBtn = $('#btn-debug-refresh-derotator');

    if (computeBtn) computeBtn.addEventListener('click', computeFieldRotation);
    if (enableBtn) enableBtn.addEventListener('click', () => toggleFieldRotation(true));
    if (disableBtn) disableBtn.addEventListener('click', () => toggleFieldRotation(false));
    if (refreshBtn) refreshBtn.addEventListener('click', refreshDerotatorStatus);

    // Auto-compute on RA/Dec input change
    const raInput = $('#debug-fr-ra');
    const decInput = $('#debug-fr-dec');
    if (raInput) raInput.addEventListener('input', computeFieldRotation);
    if (decInput) decInput.addEventListener('input', computeFieldRotation);
  }

  /**
   * Compute the field rotation rate for the target position.
   * Follows the same algorithm as AstronomicalCalculations::calculateFieldRotation().
   */
  function computeFieldRotation() {
    const raHours = readFloat('debug-fr-ra', 0);
    const decDeg = readFloat('debug-fr-dec', 45);

    if (isNaN(raHours) || isNaN(decDeg)) return;

    const jd = computeJulianDate(new Date());
    const lat = observerLatitude;

    // Step 1: Convert RA/Dec to horizontal coordinates
    const lstRad = computeApproximateLST();
    const raRad = raHours * 15 * Math.PI / 180;
    const decRad = decDeg * Math.PI / 180;
    const latRad = lat * Math.PI / 180;

    const sinLST = Math.sin(lstRad);
    const cosLST = Math.cos(lstRad);
    const sinLat = Math.sin(latRad);
    const cosLat = Math.cos(latRad);
    const sinDec = Math.sin(decRad);
    const cosDec = Math.cos(decRad);

    // Celestial → ENU
    const cx = cosDec * Math.cos(raRad);
    const cy = cosDec * Math.sin(raRad);
    const cz = sinDec;

    const enuE = -sinLST * cx + cosLST * cy;
    const enuN = -sinLat * cosLST * cx - sinLat * sinLST * cy + cosLat * cz;
    const enuU =  cosLat * cosLST * cx + cosLat * sinLST * cy + sinLat * cz;

    const altDeg = Math.asin(Math.max(-1, Math.min(1, enuU))) * 180 / Math.PI;
    const azDeg = Math.atan2(enuE, enuN) * 180 / Math.PI;

    // Step 2: Compute parallactic angle q
    // q = atan2(sin(HA), tan(lat)*cos(dec) - sin(dec)*cos(HA))
    const haRad = lstRad - raRad;
    const tanLat = Math.tan(Math.min(89.999 * Math.PI / 180, Math.abs(latRad))) * (latRad >= 0 ? 1 : -1);
    const qNum = Math.sin(haRad);
    const qDen = tanLat * cosDec - sinDec * Math.cos(haRad);
    const qRad = Math.atan2(qNum, qDen);
    const qDeg = qRad * 180 / Math.PI;

    // Step 3: Compute field rotation rate
    // dq/dt = -cos(lat) * cos(az) / cos(alt) * ω_earth  (for alt-az mount)
    const altRad = altDeg * Math.PI / 180;
    const azRad = azDeg * Math.PI / 180;
    const earthRateRadPerSec = 2 * Math.PI / 86164.0905; // sidereal rate

    let fieldRotRateDegPerSec = 0;
    if (Math.abs(Math.cos(altRad)) > 0.001) {
      fieldRotRateDegPerSec = -Math.cos(latRad) * Math.cos(azRad) / Math.cos(altRad) * earthRateRadPerSec * 180 / Math.PI;
    }
    const fieldRotRateDegPerHour = fieldRotRateDegPerSec * 3600;

    // Step 4: Adjust for mount orientation yaw (for CASUAL mounts)
    let adjustedRate = fieldRotRateDegPerSec;
    if (mountType === 3 && rotationMatrix) {
      const q = rotationMatrixToQuaternion(rotationMatrix);
      const sinYaw = 2.0 * (q.qw * q.qz + q.qx * q.qy);
      const cosYaw = 1.0 - 2.0 * (q.qy * q.qy + q.qz * q.qz);
      const mountYawDeg = Math.atan2(sinYaw, cosYaw) * 180 / Math.PI;
      // Mount yaw modifies the effective field rotation
      // For CASUAL mounts, we report the raw rate; the derotator handles orientation
    }

    // Display results
    setInputValue('debug-fr-parallactic', qDeg.toFixed(3));
    setInputValue('debug-fr-rate', fieldRotRateDegPerHour.toFixed(4));
    setInputValue('debug-fr-alt', altDeg.toFixed(3));
    setInputValue('debug-fr-az', ((azDeg % 360) + 360) % 360);

    logOutput('Field rotation: RA=' + raHours.toFixed(3) + 'h, Dec=' + decDeg.toFixed(3) + '\u00B0 -> Parallactic=' + qDeg.toFixed(2) + '\u00B0, Rate=' + fieldRotRateDegPerHour.toFixed(3) + '\u00B0/h, Alt=' + altDeg.toFixed(2) + '\u00B0, Az=' + (((azDeg % 360) + 360) % 360).toFixed(2) + '\u00B0');
  }

  /**
   * Fetch derotator status from the backend and update the display.
   */
  async function refreshDerotatorStatus() {
    try {
      const status = await Api.getDerotatorStatus();
      const badge = $('#derotator-status-badge');
      const enabledEl = $('#derotator-enabled');
      const homedEl = $('#derotator-homed');
      const angleEl = $('#derotator-angle');
      const rateEl = $('#derotator-rate');

      if (badge) {
        badge.textContent = status.enabled ? 'ACTIVE' : 'INACTIVE';
        badge.className = 'status-badge ' + (status.enabled ? 'tracking' : 'idle');
      }
      if (enabledEl) enabledEl.textContent = status.enabled ? 'Yes' : 'No';
      if (homedEl) homedEl.textContent = status.homed ? 'Yes' : 'No';
      if (angleEl) angleEl.textContent = (status.current_angle || 0).toFixed(2) + '°';
      if (rateEl) rateEl.textContent = (status.rotation_rate || 0).toFixed(4) + '°/s';

      logOutput('Derotator: enabled=' + status.enabled + ', homed=' + status.homed + ', angle=' + ((status.current_angle || 0).toFixed(2)) + '\u00B0, rate=' + ((status.rotation_rate || 0).toFixed(4)) + '\u00B0/s');
    } catch (err) {
      logOutput(`Derotator status unavailable: ${err.message}`, true);
    }
  }

  /**
   * Enable or disable field rotation compensation via the backend.
   */
  async function toggleFieldRotation(enabled) {
    try {
      computeFieldRotation(); // ensure latest computed rate is available
      const altDeg = readFloat('debug-fr-alt', 45);
      const azDeg = readFloat('debug-fr-az', 0);

      const params = {
        enabled: enabled,
        latitude: observerLatitude,
        altitude: altDeg,
        azimuth: ((azDeg % 360) + 360) % 360,
        computed_rate: 0,
        applied_correction: 0,
        temperature: 15.0,
        flexure_correction: 0,
      };

      await Api.enableFieldRotation(params);
      logOutput(`Field rotation ${enabled ? 'ENABLED' : 'DISABLED'} (Alt=${altDeg.toFixed(2)}°, Az=${params.azimuth.toFixed(2)}°)`);
      App.showToast(`Field rotation ${enabled ? 'enabled' : 'disabled'}`, 'success');

      // Refresh status after a short delay
      setTimeout(refreshDerotatorStatus, 500);
    } catch (err) {
      logOutput(`Field rotation toggle failed: ${err.message}`, true);
      App.showToast(`Toggle failed: ${err.message}`, 'error');
    }
  }

  // ─── Astrometric Corrections Section ────────────────────────────────────

  function bindAstrometricCorrections() {
    const computeBtn = $('#btn-debug-compute-apparent');
    if (computeBtn) computeBtn.addEventListener('click', computeApparentPlace);
  }

  /**
   * Compute the apparent place of a celestial object by applying:
   *   1. Proper motion (if provided)
   *   2. Precession (J2000 → current epoch, IAU 2006)
   *   3. Nutation (IAU 2000A dominant terms)
   *   4. Annual aberration
   *   5. Atmospheric refraction (to topocentric altitude)
   */
  function computeApparentPlace() {
    const raHours = readFloat('debug-ac-ra', 10.5);
    const decDeg = readFloat('debug-ac-dec', 41.27);
    const pmRa = readFloat('debug-ac-pm-ra', 0);
    const pmDec = readFloat('debug-ac-pm-dec', 0);
    const temp = readFloat('debug-ac-temp', 15);
    const pressure = readFloat('debug-ac-pressure', 1013.25);

    const applyPrec = $('#debug-ac-precession')?.checked ?? true;
    const applyNut = $('#debug-ac-nutation')?.checked ?? true;
    const applyAber = $('#debug-ac-aberration')?.checked ?? true;
    const applyRef = $('#debug-ac-refraction')?.checked ?? true;

    const jd = computeJulianDate(new Date());
    const J2000 = 2451545.0;

    let ra = raHours * 15; // degrees
    let dec = decDeg;
    let totalDRA = 0;  // total Δα in arcsec
    let totalDDec = 0; // total Δδ in arcsec

    logOutput('--- Astrometric Corrections ---');
    logOutput(`Input (J2000):  RA=${raHours.toFixed(5)}h, Dec=${dec.toFixed(5)}°`);
    logOutput(`Julian date:    ${jd.toFixed(3)}`);

    // Step 0: Apply proper motion (from J2000 to current epoch)
    if (pmRa !== 0 || pmDec !== 0) {
      const dtYears = (jd - J2000) / 365.25;
      const decRad = dec * Math.PI / 180;
      const cosDec = Math.cos(decRad);
      let dRA = 0, dDec = 0;
      if (cosDec > 0.0001) {
        dRA = pmRa * dtYears / cosDec; // arcsec of angle
      }
      dDec = pmDec * dtYears;
      ra += dRA / 3600 * 15; // arcsec → hours-worth of degrees
      dec += dDec / 3600;
      totalDRA += dRA;
      totalDDec += dDec;
      logOutput(`Proper motion (${dtYears.toFixed(1)} yr):  Δα=${dRA.toFixed(3)}", Δδ=${dDec.toFixed(3)}"`);
    }

    // Step 1: Precession (J2000 → current epoch)
    if (applyPrec) {
      const result = applyPrecessionJS(ra / 15, dec, J2000, jd);
      const dRA = (result[0] * 15 - ra) / 15 * 3600; // arcsec
      const dDec = (result[1] - dec) * 3600;
      ra = result[0] * 15;
      dec = result[1];
      totalDRA += dRA;
      totalDDec += dDec;
      logOutput(`Precession (IAU 2006): Δα=${dRA.toFixed(3)}", Δδ=${dDec.toFixed(3)}"`);
    }

    // Step 2: Nutation
    if (applyNut) {
      const result = applyNutationJS(ra / 15, dec, jd);
      const dRA = (result[0] * 15 - ra) / 15 * 3600;
      const dDec = (result[1] - dec) * 3600;
      ra = result[0] * 15;
      dec = result[1];
      totalDRA += dRA;
      totalDDec += dDec;
      logOutput(`Nutation (IAU 2000A): Δα=${dRA.toFixed(3)}", Δδ=${dDec.toFixed(3)}"`);
    }

    // Step 3: Annual aberration
    if (applyAber) {
      const result = applyAberrationJS(ra / 15, dec, jd);
      const dRA = (result[0] * 15 - ra) / 15 * 3600;
      const dDec = (result[1] - dec) * 3600;
      ra = result[0] * 15;
      dec = result[1];
      totalDRA += dRA;
      totalDDec += dDec;
      logOutput(`Aberration (annual): Δα=${dRA.toFixed(3)}", Δδ=${dDec.toFixed(3)}"`);
    }

    // Display apparent coordinates
    setInputValue('debug-ac-apparent-ra', (ra / 15).toFixed(6));
    setInputValue('debug-ac-apparent-dec', dec.toFixed(5));
    setInputValue('debug-ac-correction-ra', totalDRA.toFixed(3));
    setInputValue('debug-ac-correction-dec', totalDDec.toFixed(3));

    logOutput(`Apparent place: RA=${(ra/15).toFixed(6)}h, Dec=${dec.toFixed(5)}°`);
    logOutput(`Total correction: Δα=${totalDRA.toFixed(3)}", Δδ=${totalDDec.toFixed(3)}"`);

    // Step 4: Compute altitude and refraction
    const lstRad = computeApproximateLST();
    const raRad = ra * Math.PI / 180;
    const decRad = dec * Math.PI / 180;
    const latRad = observerLatitude * Math.PI / 180;

    const haRad = lstRad - raRad;
    const sinAlt = Math.sin(latRad) * Math.sin(decRad) + Math.cos(latRad) * Math.cos(decRad) * Math.cos(haRad);
    const altDeg = Math.asin(Math.max(-1, Math.min(1, sinAlt))) * 180 / Math.PI;

    if (applyRef && altDeg > -5) {
      const refraction = computeAtmosphericRefractionJS(altDeg, temp, pressure);
      logOutput(`Altitude: ${altDeg.toFixed(3)}° → Refraction: ${refraction.toFixed(3)}° (${(refraction*60).toFixed(2)}')`);
    } else if (altDeg <= -5) {
      logOutput(`Object below horizon (Alt=${altDeg.toFixed(3)}°), refraction not applied`);
    }

    App.showToast(`Apparent: RA=${(ra/15).toFixed(4)}h, Dec=${dec.toFixed(4)}°`, 'info');
  }

  /**
   * Apply precession from fromEpoch to toEpoch (Julian Dates).
   * Uses IAU 2006 precession model (3-angle formulation: zeta, z, theta).
   * Returns [ra_hours, dec_degrees].
   */
  function applyPrecessionJS(raHours, decDeg, fromEpoch, toEpoch) {
    const J2000 = 2451545.0;
    // Centuries from J2000
    const T_from = (fromEpoch - J2000) / 36525.0;
    const T_to = (toEpoch - J2000) / 36525.0;

    // IAU 2006 precession angles in arcseconds
    function zeta(T)   { return (2306.083227 + (0.2988499 + 0.01801828 * T) * T) * T; }
    function z(T)      { return (2306.083227 + (1.0947342 + 0.01826837 * T) * T) * T; }
    function theta(T)  { return (2004.191747 + (-0.4269353 - 0.04182309 * T) * T) * T; }

    const zeta_rad  = zeta(T_from)  / 3600 * Math.PI / 180;
    const z_rad     = z(T_from)     / 3600 * Math.PI / 180;
    const theta_rad = theta(T_from) / 3600 * Math.PI / 180;

    // Rotation from fromEpoch to J2000: R3(-z) * R2(theta) * R3(-zeta)
    const raRad = raHours * 15 * Math.PI / 180;
    const decRad = decDeg * Math.PI / 180;

    const cosDec = Math.cos(decRad);
    const sinDec = Math.sin(decRad);

    // First rotation: R3(-zeta)
    let a = raRad - zeta_rad;
    // Second rotation: R2(theta) - rotate around Y
    const x = cosDec * Math.cos(a);
    const y = cosDec * Math.sin(a);
    const z_val = sinDec;
    const ct = Math.cos(theta_rad);
    const st = Math.sin(theta_rad);
    const x2 = ct * x + st * z_val;
    const y2 = y;
    const z2 = -st * x + ct * z_val;
    // Third rotation: R3(-z)
    const x3 = Math.cos(z_rad) * x2 - Math.sin(z_rad) * y2;
    const y3 = Math.sin(z_rad) * x2 + Math.cos(z_rad) * y2;
    const z3 = z2;

    let ra_from = Math.atan2(y3, x3);
    let dec_from = Math.asin(Math.max(-1, Math.min(1, z3)));

    // Now precess from J2000 to toEpoch
    const zeta_to  = zeta(T_to)  / 3600 * Math.PI / 180;
    const z_to     = z(T_to)     / 3600 * Math.PI / 180;
    const theta_to = theta(T_to) / 3600 * Math.PI / 180;

    // Forward precession: R3(zeta_to) * R2(-theta_to) * R3(z_to)
    a = ra_from + z_to;
    const xf = Math.cos(dec_from) * Math.cos(a);
    const yf = Math.cos(dec_from) * Math.sin(a);
    const zf = Math.sin(dec_from);
    const ct2 = Math.cos(theta_to);
    const st2 = Math.sin(theta_to);
    const xf2 = ct2 * xf - st2 * zf;
    const yf2 = yf;
    const zf2 = st2 * xf + ct2 * zf;

    let ra_to = Math.atan2(yf2, xf2) + zeta_rad;
    let dec_to = Math.asin(Math.max(-1, Math.min(1, zf2)));

    // Normalize RA
    ra_to = ((ra_to % (2 * Math.PI)) + 2 * Math.PI) % (2 * Math.PI);

    return [ra_to * 180 / Math.PI / 15, dec_to * 180 / Math.PI];
  }

  /**
   * Apply nutation correction using dominant IAU 2000A terms.
   * Returns [ra_hours, dec_degrees] — nutated mean place.
   */
  function applyNutationJS(raHours, decDeg, jd) {
    const J2000 = 2451545.0;
    const T = (jd - J2000) / 36525.0; // Julian centuries from J2000

    // Fundamental arguments (IAU 2006 expressions)
    const rev = 360.0;
    const D2R = Math.PI / 180;

    // Mean anomaly of the Moon
    const l  = (134.96341138 + (477198.86739813 + (0.0086975 + 1.6667e-6 * T) * T) * T) % rev;
    // Mean anomaly of the Sun
    const lp = (357.52910918 + (35999.05029117 + (-0.0001559 - 4.0e-8 * T) * T) * T) % rev;
    // Moon's mean argument of latitude
    const F  = ( 93.27209062 + (483202.01745773 + (-0.0033655 - 5.8333e-7 * T) * T) * T) % rev;
    // Mean elongation of the Moon from the Sun
    const D  = (297.85019547 + (445267.11144625 + (-0.0017602 + 2.2222e-6 * T) * T) * T) % rev;
    // Longitude of the ascending node of the Moon
    const Om = (125.04455501 + (-1934.13626197 + (0.0020708 + 2.2222e-6 * T) * T) * T) % rev;

    const lr = l * D2R, lpr = lp * D2R, Fr = F * D2R, Dr = D * D2R, Omr = Om * D2R;

    // Nutation in longitude (Δψ) and obliquity (Δε) — dominant 5 terms
    // Amplitudes in 0.1 mas, converted to radians
    const MAS2RAD = 0.001 / 3600 * D2R;

    // Term 1: 18.6-year lunar node (-171996 - 174.2*T, 92025 + 8.9*T) * sin(Ω)
    let dpsi = (-171996.0 - 174.2 * T) * Math.sin(Omr);
    let deps = ( 92025.0 + 8.9 * T) * Math.cos(Omr);

    // Term 2: semi-annual (-13187 - 1.6*T, 5736 - 3.1*T) * sin(2F - 2D + 2Ω)
    let arg = 2 * Fr - 2 * Dr + 2 * Omr;
    dpsi += (-13187.0 - 1.6 * T) * Math.sin(arg);
    deps += (5736.0 - 3.1 * T) * Math.cos(arg);

    // Term 3: fortnightly (-2274 - 0.2*T, 977 - 0.5*T) * sin(2F + 2Ω)
    arg = 2 * Fr + 2 * Omr;
    dpsi += (-2274.0 - 0.2 * T) * Math.sin(arg);
    deps += (977.0 - 0.5 * T) * Math.cos(arg);

    // Term 4: monthly (2062 + 0.2*T, -895 + 0.5*T) * sin(l')
    dpsi += (2062.0 + 0.2 * T) * Math.sin(lpr);
    deps += (-895.0 + 0.5 * T) * Math.cos(lpr);

    // Term 5: 13.7-day (1426 - 3.4*T, 54 - 0.1*T) * sin(l)
    dpsi += (1426.0 - 3.4 * T) * Math.sin(lr);
    deps += (54.0 - 0.1 * T) * Math.cos(lr);

    // Additional significant terms for better accuracy
    // Term 6: (712 + 0.1*T) * sin(l)  — already covered
    // Term 7: monthly (-517 + 1.2*T, 224 - 0.6*T) * sin(2D - l)
    arg = 2 * Dr - lr;
    dpsi += (-517.0 + 1.2 * T) * Math.sin(arg);
    deps += (224.0 - 0.6 * T) * Math.cos(arg);

    dpsi *= MAS2RAD;
    deps *= MAS2RAD;

    // Mean obliquity (IAU 2006)
    const eps0 = computeObliquity(jd) * D2R;

    // Apply nutation to coordinates
    const raRad = raHours * 15 * D2R;
    const decRad = decDeg * D2R;
    const eps = eps0 + deps;

    const sinRA = Math.sin(raRad);
    const cosRA = Math.cos(raRad);
    const tanDec = Math.tan(decRad);

    const dRA = (Math.cos(eps) + Math.sin(eps) * sinRA * tanDec) * dpsi - Math.cos(raRad) * tanDec * deps;
    const dDec = Math.sin(eps) * cosRA * dpsi + sinRA * deps;

    const raNut = raRad + dRA;
    const decNut = decRad + dDec;

    let raOut = raNut * 180 / Math.PI / 15;
    raOut = ((raOut % 24) + 24) % 24;

    return [raOut, decNut * 180 / Math.PI];
  }

  /**
   * Apply annual aberration correction.
   * Returns [ra_hours, dec_degrees].
   */
  function applyAberrationJS(raHours, decDeg, jd) {
    const D2R = Math.PI / 180;
    const C_AUDAY = 173.1446326846693; // speed of light in AU/day

    // Compute Earth's barycentric velocity (simplified elliptical orbit)
    const J2000 = 2451545.0;
    const T = (jd - J2000) / 36525.0;

    // Earth orbital elements (approx)
    const L = (280.46646 + 36000.76983 * T + 0.0003032 * T * T) % 360;
    const g = (357.52911 + 35999.05029 * T - 0.0001537 * T * T) % 360;
    const e = 0.016708634 - 0.000042037 * T;
    const eps = computeObliquity(jd);

    const Lr = L * D2R;
    const gr = g * D2R;

    // Earth-Sun distance in AU
    const R = 1.000001018 * (1 - e * e) / (1 + e * Math.cos(gr));

    // Earth's orbital velocity: v_orb ≈ 2π * R / year in AU/day
    // Better: compute from vis-viva: v² = GM * (2/R - 1/a), GM ≈ 4π² AU³/yr²
    const vOrbAUPerDay = 2 * Math.PI / 365.25 * (1 + e * Math.cos(gr)) / Math.sqrt(1 - e * e);

    // Velocity vector in ecliptic coordinates
    // Direction: perpendicular to Sun direction, in orbital plane
    const sunLon = Lr + Math.PI + 2 * e * Math.sin(gr); // solar longitude (approx)
    const velLon = sunLon + Math.PI / 2; // velocity direction ~perpendicular
    const velLat = 0;

    const vxEcl = vOrbAUPerDay * Math.cos(velLat) * Math.cos(velLon);
    const vyEcl = vOrbAUPerDay * Math.cos(velLat) * Math.sin(velLon);
    const vzEcl = vOrbAUPerDay * Math.sin(velLat);

    // Convert ecliptic velocity to equatorial
    const epsR = eps * D2R;
    const vx = vxEcl;
    const vy = vyEcl * Math.cos(epsR) - vzEcl * Math.sin(epsR);
    const vz = vyEcl * Math.sin(epsR) + vzEcl * Math.cos(epsR);

    // Direction to star (unit vector)
    const raRad = raHours * 15 * D2R;
    const decRad = decDeg * D2R;
    const sx = Math.cos(decRad) * Math.cos(raRad);
    const sy = Math.cos(decRad) * Math.sin(raRad);
    const sz = Math.sin(decRad);

    // Aberration correction in radians
    const dot = vx * sx + vy * sy + vz * sz;
    const betaX = (vx - dot * sx) / C_AUDAY;
    const betaY = (vy - dot * sy) / C_AUDAY;
    const betaZ = (vz - dot * sz) / C_AUDAY;

    const raAber = raRad + (betaY * Math.cos(raRad) - betaX * Math.sin(raRad)) / Math.cos(decRad);
    const decAber = decRad + (betaZ * Math.cos(decRad) - (betaX * Math.cos(raRad) + betaY * Math.sin(raRad)) * Math.sin(decRad));

    let raOut = raAber * 180 / Math.PI / 15;
    raOut = ((raOut % 24) + 24) % 24;

    return [raOut, decAber * 180 / Math.PI];
  }

  /**
   * Compute atmospheric refraction correction using the Saemundsson formula.
   * Same formula as the C++ backend (AstronomicalCalculations::applyAtmosphericRefraction).
   * @param {number} altitude - Apparent altitude in degrees
   * @param {number} temperature - Temperature in Celsius
   * @param {number} pressure - Pressure in hPa
   * @returns {number} Refraction correction in degrees (positive = lifts object)
   */
  function computeAtmosphericRefractionJS(altitude, temperature, pressure) {
    if (altitude < -5) return 0;
    const MAX_ALT = 85.0;
    const altClamped = Math.min(altitude, MAX_ALT);
    const t_k = temperature + 273.15;
    const p_corr = pressure * 283.15 / (t_k * 1.33322);

    const altRad = altClamped * Math.PI / 180;
    const tanArg = (altClamped + 10.3 / (altClamped + 5.11)) * Math.PI / 180;
    let r = 1.02 / Math.tan(tanArg);
    if (r < 0) r = 0;

    r *= p_corr / 1010.0 * 283.15 / t_k;
    return r / 60.0; // arcmin → degrees
  }

  /**
   * Compute mean obliquity of the ecliptic (IAU 2006).
   * @param {number} jd - Julian Date
   * @returns {number} Obliquity in degrees
   */
  function computeObliquity(jd) {
    const J2000 = 2451545.0;
    const T = (jd - J2000) / 36525.0;
    // IAU 2006 expression
    const eps0 = 23.439279444444445
               + (-0.013010213611111111
               + (-0.00000016388888888889
               + (0.0000005036111111111) * T) * T) * T;
    return eps0;
  }

  // ─── Helpers ───────────────────────────────────────────────────────────

  /**
   * Set an input element's value (for readonly display fields).
   */
  function setInputValue(id, value) {
    const el = $(`#${id}`);
    if (!el) return;
    if (typeof value === 'number') {
      el.value = value.toFixed(4);
    } else {
      el.value = value;
    }
  }

  /**
   * Ensure CASUAL orientation is applied before tracking.
   */
  async function ensureCasualOrientation() {
    const qxEl = $('#debug-qx');
    const qyEl = $('#debug-qy');
    const qzEl = $('#debug-qz');
    const qwEl = $('#debug-qw');

    const orient = {
      qx: qxEl ? parseFloat(qxEl.value) : 0,
      qy: qyEl ? parseFloat(qyEl.value) : 0,
      qz: qzEl ? parseFloat(qzEl.value) : 0,
      qw: qwEl ? parseFloat(qwEl.value) : 1,
    };

    try {
      await Api.setMountOrientation(orient);
      logOutput(`CASUAL orientation set: qw=${orient.qw.toFixed(4)}`);
    } catch (err) {
      logOutput(`WARNING: Could not set CASUAL orientation: ${err.message}`, true);
    }
  }

  function readFloat(id, defaultValue) {
    const el = $(`#${id}`);
    if (!el) return defaultValue;
    // If the input has been enhanced with angle support, use the decimal getter
    if (el.getAngleDecimal) {
      const val = el.getAngleDecimal();
      return isFinite(val) ? val : defaultValue;
    }
    const val = parseFloat(el.value);
    return isNaN(val) ? defaultValue : val;
  }

  // ─── Output Console ────────────────────────────────────────────────────

  function bindClearOutput() {
    const btn = $('#btn-debug-clear-output');
    if (btn) {
      btn.addEventListener('click', () => {
        const output = $('#debug-output');
        if (output) output.textContent = '';
      });
    }
  }

  /**
   * Append a message to the debug output console.
   * @param {string} message
   * @param {boolean} [isError=false]
   */
  function logOutput(message, isError) {
    const output = $('#debug-output');
    if (!output) return;

    const timestamp = new Date().toISOString().substring(11, 23);
    const prefix = isError ? `[${timestamp}] [ERROR] ` : `[${timestamp}] `;
    output.textContent += prefix + message + '\n';

    // Auto-scroll to bottom
    output.scrollTop = output.scrollHeight;
  }

  // ─── Help Content Builder ──────────────────────────────────────────────

  /**
   * Build the "How to Use the Tests Tab" help content dynamically.
   * Called once during init so translations are applied at the right time.
   */
  function buildTestsHelpContent() {
    const container = $('#tests-help-content');
    if (!container) return;

    const t = I18n.t.bind(I18n);

    const steps = [
      {
        num: '1',
        titleKey: 'tests.help_step1_title',
        bodyHtml: '<p>' + t('tests.help_step1_intro') + '</p>'
          + '<ol>'
          + '<li>' + t('tests.help_step1_li1') + '</li>'
          + '<li>' + t('tests.help_step1_li2') + '</li>'
          + '<li>' + t('tests.help_step1_li3') + '</li>'
          + '<li>' + t('tests.help_step1_li4') + '</li>'
          + '<li>' + t('tests.help_step1_li5') + '</li>'
          + '</ol>',
        open: true
      },
      {
        num: '2',
        titleKey: 'tests.help_step2_title',
        bodyHtml: '<ol>'
          + '<li>' + t('tests.help_step2_li1') + '</li>'
          + '<li>' + t('tests.help_step2_li2') + '</li>'
          + '<li>' + t('tests.help_step2_li3') + '</li>'
          + '<li>' + t('tests.help_step2_li4') + '</li>'
          + '<li>' + t('tests.help_step2_li5') + '</li>'
          + '</ol>'
      },
      {
        num: '3',
        titleKey: 'tests.help_step3_title',
        bodyHtml: '<p>' + t('tests.help_step3_intro') + '</p>'
          + '<ul>'
          + '<li>' + t('tests.help_step3_li1') + '</li>'
          + '<li>' + t('tests.help_step3_li2') + '</li>'
          + '<li>' + t('tests.help_step3_li3') + '</li>'
          + '<li>' + t('tests.help_step3_li4') + '</li>'
          + '<li>' + t('tests.help_step3_li5') + '</li>'
          + '</ul>'
          + '<p>' + t('tests.help_step3_footer') + '</p>'
      },
      {
        num: '4',
        titleKey: 'tests.help_step4_title',
        bodyHtml: '<ol>'
          + '<li>' + t('tests.help_step4_li1') + '</li>'
          + '<li>' + t('tests.help_step4_li2') + '</li>'
          + '<li>' + t('tests.help_step4_li3') + '</li>'
          + '<li>' + t('tests.help_step4_li4') + '</li>'
          + '</ol>'
      },
      {
        num: '5',
        titleKey: 'tests.help_step5_title',
        bodyHtml: '<ol>'
          + '<li>' + t('tests.help_step5_li1') + '</li>'
          + '<li>' + t('tests.help_step5_li2') + '</li>'
          + '<li>' + t('tests.help_step5_li3') + '</li>'
          + '<li>' + t('tests.help_step5_li4') + '</li>'
          + '</ol>'
      },
      {
        num: '6',
        titleKey: 'tests.help_step6_title',
        bodyHtml: '<ul>'
          + '<li>' + t('tests.help_step6_li1') + '</li>'
          + '<li>' + t('tests.help_step6_li2') + '</li>'
          + '<li>' + t('tests.help_step6_li3') + '</li>'
          + '<li>' + t('tests.help_step6_li4') + '</li>'
          + '<li>' + t('tests.help_step6_li5') + '</li>'
          + '</ul>'
          + '<p>' + t('tests.help_step6_footer') + '</p>'
      },
      {
        num: '\u2194',
        titleKey: 'tests.help_step7_title',
        bodyHtml: '<p><strong>' + t('tests.help_step7_intro') + '</strong></p>'
          + '<p>' + t('tests.help_step7_body') + '</p>'
          + '<ul>'
          + '<li>' + t('tests.help_step7_li1') + '</li>'
          + '<li>' + t('tests.help_step7_li2') + '</li>'
          + '<li>' + t('tests.help_step7_li3') + '</li>'
          + '</ul>'
          + '<p>' + t('tests.help_step7_footer') + '</p>'
      }
    ];

    let html = '<p><strong>' + t('tests.help_purpose_label') + '</strong> ' + t('tests.help_purpose_text') + '</p>';

    steps.forEach(step => {
      html += '<details class="calibration-help-step"' + (step.open ? ' open' : '') + '>'
        + '<summary class="calibration-help-step-summary">'
        + '<span class="calibration-help-step-number">' + step.num + '</span>'
        + t(step.titleKey)
        + '</summary>'
        + '<div class="calibration-help-step-body">'
        + step.bodyHtml
        + '</div>'
        + '</details>';
    });

    container.innerHTML = html;
  }

  // ─── Public API ────────────────────────────────────────────────────────

  return {
    init,
    logOutput,
  };
})();
