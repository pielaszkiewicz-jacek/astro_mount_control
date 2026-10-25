/**
 * Auto-Bootstrap Orchestrator
 *
 * Implements the automatic bootstrap procedure at the proxy level (plan §7.1
 * Level 2). The C++ layer only validates preconditions and records the mode;
 * the actual slewing/measurement loop lives here so the mount can be driven
 * through the existing gRPC RPCs.
 *
 * Flow (per selected star):
 *   1. Slew to the star coordinates (SlewToCoordinates).
 *   2. Wait until the mount leaves the SLEWING state.
 *   3. Ask the operator to center the star and confirm (AWAITING_CONFIRMATION).
 *   4. Read the current mount position and add a measurement
 *      (observed = current on-sky position, expected = catalog coordinates).
 * After the requested stars have been measured the bootstrap calibration is
 * run. Optionally (proceed_to_tpoint) the same loop is repeated for TPOINT
 * measurements and TPointCalibration is run afterwards.
 */

'use strict';

const { grpcCall } = require('./grpc/client');
const builtinCatalog = require('./builtin-catalog');

// Session state machine (must stay in sync with the frontend calibration.js
// state codes and the i18n cal.auto_state.* labels).
const STATE = {
  IDLE: 0,
  SLEWING: 1,
  AWAITING_CONFIRMATION: 2,
  ADDING_MEASUREMENT: 3,
  CALIBRATING: 4,
  COMPLETED: 5,
  ERROR: 6,
  CANCELLED: 7,
  PLATE_SOLVING: 8,
};

const SLEW_POLL_INTERVAL_MS = 1000;
const SLEW_TIMEOUT_MS = 10 * 60 * 1000; // 10 minutes
const SETTLE_DELAY_MS = 2500;            // let the mount settle after slewing

let session = null;

function sleep(ms) {
  return new Promise((resolve) => setTimeout(resolve, ms));
}

function setState(s, state, message) {
  s.state = state;
  if (message !== undefined) s.stateMessage = message;
}

/**
 * Resolve a star name to { name, ra [hours], dec [degrees] } using the
 * built-in catalog. Returns null when the star cannot be resolved.
 */
function resolveStar(name) {
  const query = String(name || '').trim();
  if (!query) return null;

  const results = builtinCatalog.searchObjects({ name: query, limit: 5 });
  const objects = (results && results.objects) || [];
  // Prefer an exact name match, otherwise accept the first partial match.
  const exact = objects.find((o) =>
    (o.name || '').toLowerCase() === query.toLowerCase() ||
    (o.alternate_names || '').toLowerCase().split(',').map((s) => s.trim()).includes(query.toLowerCase())
  );
  const obj = exact || objects[0];
  // builtin-catalog formats coordinates as ra_hours / dec_degrees.
  if (obj && typeof obj.ra_hours === 'number' && typeof obj.dec_degrees === 'number') {
    return { name: obj.name || query, ra: obj.ra_hours, dec: obj.dec_degrees };
  }
  return null;
}

function readMountState() {
  return grpcCall('GetState', {});
}

async function waitForSlewComplete(s) {
  const started = Date.now();

  // Phase 1 — wait until the slew actually starts. The RPC returns as soon as
  // the command is accepted, so a poll taken immediately afterwards can still
  // observe the previous IDLE/TRACKING state. Without this phase the loop
  // could return instantly and record a measurement at the previous star's
  // position, leaving the mount pointed at the wrong object after calibration.
  while (Date.now() - started < SLEW_TIMEOUT_MS) {
    if (s.cancelled) throw new Error('Cancelled');
    let status;
    try {
      const st = await readMountState();
      status = String(st.status || '');
    } catch (err) {
      // The controller may be momentarily unreachable; keep polling.
      status = '';
    }

    if (status === 'ERROR') {
      throw new Error(`Mount entered ERROR state while slewing to ${s.currentTarget.name}`);
    }
    if (status === 'SLEWING') {
      break;
    }
    await sleep(SLEW_POLL_INTERVAL_MS);
  }
  if (Date.now() - started >= SLEW_TIMEOUT_MS) {
    throw new Error(`Timed out waiting for slew to start for ${s.currentTarget.name}`);
  }

  // Phase 2 — wait until the slew finishes.
  while (Date.now() - started < SLEW_TIMEOUT_MS) {
    if (s.cancelled) throw new Error('Cancelled');
    let status;
    try {
      const st = await readMountState();
      status = String(st.status || '');
    } catch (err) {
      status = '';
    }

    if (status === 'ERROR') {
      throw new Error(`Mount entered ERROR state while slewing to ${s.currentTarget.name}`);
    }
    if (status !== 'SLEWING' && status !== 'UNKNOWN' && status !== '') {
      // Give the mechanics a moment to settle before the operator confirms.
      await sleep(SETTLE_DELAY_MS);
      return;
    }
    await sleep(SLEW_POLL_INTERVAL_MS);
  }
  throw new Error(`Timed out waiting for slew to ${s.currentTarget.name}`);
}

function waitForConfirmation(s) {
  return new Promise((resolve, reject) => {
    s.confirm = { resolve, reject };
  });
}

async function addMeasurement(s, observedOverride) {
  const target = s.currentTarget;
  setState(s, STATE.ADDING_MEASUREMENT, `Dodawanie pomiaru: ${target.name}`);

  const st = await readMountState();
  const observedRa = observedOverride && typeof observedOverride.ra === 'number'
    ? observedOverride.ra
    : (typeof st.current_ra === 'number' ? st.current_ra : 0);
  const observedDec = observedOverride && typeof observedOverride.dec === 'number'
    ? observedOverride.dec
    : (typeof st.current_dec === 'number' ? st.current_dec : 0);
  const mountHa = typeof st.telescope_axis1 === 'number' ? st.telescope_axis1 : 0;
  const mountDec = typeof st.telescope_axis2 === 'number' ? st.telescope_axis2 : 0;

  if (s.phase === 'BOOTSTRAP') {
    await grpcCall('AddBootstrapMeasurement', {
      observed: { ra: observedRa, dec: observedDec },
      expected: { ra: target.ra, dec: target.dec },
      mount_position: { axis1: mountHa, axis2: mountDec },
    });
  } else {
    await grpcCall('AddTPointMeasurement', {
      observed: { ra: observedRa, dec: observedDec },
      expected: { ra: target.ra, dec: target.dec },
      mount_position: { axis1: mountHa, axis2: mountDec },
    });
  }

  s.measurements.push({ name: target.name });
  s.progress = Math.min(90, Math.round((s.measurements.length / Math.max(1, s.targetCount)) * 90));
}

async function ensureMode(s) {
  const status = await grpcCall('GetBootstrapStatus', {});
  const encoderAbsolute = !!(status && status.encoder_type_absolute);
  // AUTOMATIC requires absolute encoders (or a prior calibration reference);
  // HYBRID is the safe choice for incremental encoders and still allows the
  // automatic slew/measure loop below. Plate solving provides an absolute
  // reference, so it also permits AUTOMATIC mode.
  const mode = s.plateSolve || encoderAbsolute ? 2 : 1;
  await grpcCall('SetBootstrapMode', { mode });
  s.mode = mode;
}

async function plateSolveField(s) {
  // Lazily require the plate solver so the dependency is only pulled in when
  // full automation is actually requested.
  const { solveField } = require('./plate-solver');
  setState(s, STATE.PLATE_SOLVING, `Plate solving: ${s.currentTarget.name}`);

  const st = await readMountState();
  const hint = {
    ra_hours: typeof st.current_ra === 'number' ? st.current_ra : s.currentTarget.ra,
    dec_degrees: typeof st.current_dec === 'number' ? st.current_dec : s.currentTarget.dec,
  };
  return solveField(hint);
}

async function runSlewMeasureLoop(s) {
  for (let i = 0; i < s.stars.length; i += 1) {
    if (s.cancelled) return;
    s.currentIndex = i;
    s.currentTarget = s.stars[i];

    const phaseLabel = s.phase === 'BOOTSTRAP' ? 'Bootstrap' : 'TPOINT';
    setState(s, STATE.SLEWING, `${phaseLabel}: slew do ${s.currentTarget.name}`);
    await grpcCall('SlewToCoordinates', { ra: s.currentTarget.ra, dec: s.currentTarget.dec });
    await waitForSlewComplete(s);

    if (s.autoTrackAfterSlew) {
      // Enter tracking so the target stays centered while the operator
      // centers it / the plate solver captures the field.
      await grpcCall('TrackObject', { ra: s.currentTarget.ra, dec: s.currentTarget.dec });
    }

    if (s.plateSolve) {
      const solved = await plateSolveField(s);
      await addMeasurement(s, solved);
    } else {
      setState(
        s,
        STATE.AWAITING_CONFIRMATION,
        `Wycentruj ${s.currentTarget.name} i potwierdź pomiar (${phaseLabel})`
      );
      await waitForConfirmation(s);
      await addMeasurement(s);
    }
  }
}

async function runLoop(s) {
  try {
    await ensureMode(s);

    s.phase = 'BOOTSTRAP';
    s.targetCount = Math.max(s.minMeasurements, s.stars.length);
    await runSlewMeasureLoop(s);

    setState(s, STATE.CALIBRATING, 'Uruchamianie kalibracji bootstrap...');
    const bootstrapResult = await grpcCall('RunBootstrapCalibration', {});
    s.bootstrapResult = bootstrapResult;

    if (s.proceedToTpoint && !s.cancelled) {
      s.phase = 'TPOINT';
      // Collect a TPOINT pass over the same stars, then fit the model.
      await runSlewMeasureLoop(s);

      setState(s, STATE.CALIBRATING, 'Uruchamianie kalibracji TPOINT...');
      await grpcCall('RunTPointCalibration', {});
    }

    if (s.cancelled) {
      setState(s, STATE.CANCELLED, 'Anulowano auto-bootstrap');
      return;
    }

    // Optionally start tracking the last measured target after calibration.
    if (s.autoTrackAfterCalibration && s.currentTarget) {
      await grpcCall('TrackObject', { ra: s.currentTarget.ra, dec: s.currentTarget.dec });
    }

    s.progress = 100;
    setState(s, STATE.COMPLETED, 'Auto-bootstrap zakończony pomyślnie');
  } catch (err) {
    if (s.cancelled) {
      setState(s, STATE.CANCELLED, 'Anulowano auto-bootstrap');
    } else {
      s.errorMessage = err.message || String(err);
      setState(s, STATE.ERROR, `Błąd auto-bootstrap: ${s.errorMessage}`);
    }
  } finally {
    s.confirm = null;
  }
}

/**
 * Start a new auto-bootstrap session.
 * @param {object} options - { target_star_names, min_measurements,
 *                              max_alignment_error_arcsec, proceed_to_tpoint,
 *                              plate_solve }
 */
function start(options = {}) {
  if (session && ![STATE.COMPLETED, STATE.ERROR, STATE.CANCELLED].includes(session.state)) {
    throw new Error('Auto-bootstrap jest już uruchomiony');
  }

  const names = Array.isArray(options.target_star_names) ? options.target_star_names : [];
  const stars = [];
  for (const name of names) {
    const star = resolveStar(name);
    if (!star) throw new Error(`Nie znaleziono gwiazdy: ${name}`);
    stars.push(star);
  }

  const minMeasurements = Math.max(3, Number(options.min_measurements) || 3);
  if (stars.length < minMeasurements) {
    throw new Error(`Wymagane co najmniej ${minMeasurements} gwiazd, podano ${stars.length}`);
  }

  session = {
    state: STATE.IDLE,
    stateMessage: 'Rozpoczynanie...',
    stars,
    minMeasurements,
    maxAlignmentErrorArcsec: Number(options.max_alignment_error_arcsec) || 60.0,
    proceedToTpoint: !!options.proceed_to_tpoint,
    plateSolve: !!options.plate_solve,
    autoTrackAfterSlew: !!options.auto_track_after_slew,
    autoTrackAfterCalibration: !!options.auto_track_after_calibration,
    phase: 'BOOTSTRAP',
    mode: 0,
    measurements: [],
    targetCount: Math.max(minMeasurements, stars.length),
    currentIndex: 0,
    currentTarget: null,
    progress: 0,
    errorMessage: '',
    bootstrapResult: null,
    cancelled: false,
    confirm: null,
  };

  // Run asynchronously; the client polls GET /auto-status for progress.
  runLoop(session).catch(() => {});

  return { success: true, message: 'Automatic bootstrap started' };
}

/**
 * Public status snapshot for the auto-status endpoint.
 */
function getStatus() {
  if (!session) {
    return {
      state: STATE.IDLE,
      state_message: '',
      current_target_star: '',
      measurements_collected: 0,
      measurements_target: 0,
      progress_percent: 0,
      error_message: '',
      awaiting_confirmation: false,
    };
  }

  return {
    state: session.state,
    state_message: session.stateMessage || '',
    current_target_star: session.currentTarget ? session.currentTarget.name : '',
    measurements_collected: session.measurements.length,
    measurements_target: session.targetCount,
    progress_percent: Math.round(session.progress || 0),
    error_message: session.errorMessage || '',
    awaiting_confirmation: session.state === STATE.AWAITING_CONFIRMATION,
  };
}

/**
 * Operator confirms that the current star is centered. Triggers the pending
 * measurement step in the running session.
 */
function confirmMeasurement() {
  if (!session) throw new Error('Brak aktywnej sesji auto-bootstrap');
  if (session.state !== STATE.AWAITING_CONFIRMATION) {
    throw new Error('Auto-bootstrap nie oczekuje na potwierdzenie pomiaru');
  }
  const confirm = session.confirm;
  if (!confirm || typeof confirm.resolve !== 'function') {
    throw new Error('Brak oczekującego potwierdzenia pomiaru');
  }
  session.confirm = null;
  confirm.resolve();
  return { success: true };
}

/**
 * Cancel the running session (safe to call at any time).
 */
function cancel() {
  if (!session) return { success: true };
  session.cancelled = true;
  const confirm = session.confirm;
  if (confirm && typeof confirm.reject === 'function') {
    session.confirm = null;
    confirm.reject(new Error('Cancelled'));
  }
  return { success: true, message: 'Cancellation requested' };
}

module.exports = {
  STATE,
  start,
  getStatus,
  confirmMeasurement,
  cancel,
};
