/**
 * PID Controller Calibration Component
 *
 * Web UI for automated PID gain calibration of the motor drive loops
 * (current, speed, position).  The calibration runs asynchronously in the
 * backend; this component polls /api/pidcal/status while a session is active
 * and renders the full session details plus the winning combinations.
 */
const PidCalibrationComponent = (() => {
  'use strict';

  const LOOP_LABELS = {
    1: 'Current',
    2: 'Speed',
    3: 'Position',
  };
  const COEFF_LABELS = {
    1: 'Kp',
    2: 'Ki',
    3: 'Kd',
    4: 'All',
  };

  let pollTimer = null;

  function render() {
    return `
      <div class="pidcal-dashboard">
        <div class="sensor-grid">
          <div class="sensor-card">
            <span class="sensor-label">State</span>
            <span class="sensor-value" id="pidcal-state">IDLE</span>
          </div>
          <div class="sensor-card">
            <span class="sensor-label">Progress</span>
            <span class="sensor-value" id="pidcal-progress">0%</span>
          </div>
          <div class="sensor-card">
            <span class="sensor-label">Tested</span>
            <span class="sensor-value" id="pidcal-tested">0 / 0</span>
          </div>
          <div class="sensor-card">
            <span class="sensor-label">Current Speed</span>
            <span class="sensor-value" id="pidcal-speed">--</span>
          </div>
          <div class="sensor-card">
            <span class="sensor-label">Best @ current speed</span>
            <span class="sensor-value" id="pidcal-best" style="font-size:0.8rem;">--</span>
          </div>
        </div>

        <h3 style="margin:16px 0 8px; font-size:0.9rem; color:var(--color-text-secondary);">Calibration Parameters</h3>
        <div class="control-form">
          <div class="form-row">
            <div class="form-group">
              <label for="pidcal-axis" class="form-label">Axis</label>
              <select id="pidcal-axis" class="form-input form-select" style="width:auto;">
                <option value="0">Axis 1</option>
                <option value="1">Axis 2</option>
              </select>
            </div>
            <div class="form-group">
              <label for="pidcal-loop" class="form-label">Loop</label>
              <select id="pidcal-loop" class="form-input form-select" style="width:auto;">
                <option value="2" selected>Speed</option>
                <option value="3">Position</option>
                <option value="1">Current</option>
              </select>
            </div>
            <div class="form-group">
              <label for="pidcal-coeff" class="form-label">Coefficient</label>
              <select id="pidcal-coeff" class="form-input form-select" style="width:auto;">
                <option value="1" selected>Kp</option>
                <option value="2">Ki</option>
                <option value="3">Kd</option>
                <option value="4">All (Kp + Ki + Kd)</option>
              </select>
            </div>
          </div>

          <div class="form-row">
            <div class="form-group">
              <label class="form-label">Min speed (motor °/s)</label>
              <input type="number" id="pidcal-min-speed" class="form-input" value="30" step="1">
            </div>
            <div class="form-group">
              <label class="form-label">Max speed (motor °/s)</label>
              <input type="number" id="pidcal-max-speed" class="form-input" value="120" step="1">
            </div>
            <div class="form-group">
              <label class="form-label">Speed step (motor °/s)</label>
              <input type="number" id="pidcal-speed-step" class="form-input" value="30" step="1">
            </div>
          </div>

          <div class="form-row">
            <div class="form-group">
              <label class="form-label">Kp min / max / step / base</label>
              <div class="form-row" style="gap:4px;">
                <input type="number" id="pidcal-kp-min" class="form-input" value="10" step="1">
                <input type="number" id="pidcal-kp-max" class="form-input" value="100" step="1">
                <input type="number" id="pidcal-kp-step" class="form-input" value="10" step="1">
                <input type="number" id="pidcal-kp-base" class="form-input" value="40" step="1">
              </div>
            </div>
            <div class="form-group">
              <label class="form-label">Ki min / max / step / base</label>
              <div class="form-row" style="gap:4px;">
                <input type="number" id="pidcal-ki-min" class="form-input" value="10" step="1">
                <input type="number" id="pidcal-ki-max" class="form-input" value="100" step="1">
                <input type="number" id="pidcal-ki-step" class="form-input" value="10" step="1">
                <input type="number" id="pidcal-ki-base" class="form-input" value="40" step="1">
              </div>
            </div>
            <div class="form-group">
              <label class="form-label">Kd min / max / step / base</label>
              <div class="form-row" style="gap:4px;">
                <input type="number" id="pidcal-kd-min" class="form-input" value="0" step="1">
                <input type="number" id="pidcal-kd-max" class="form-input" value="50" step="1">
                <input type="number" id="pidcal-kd-step" class="form-input" value="10" step="1">
                <input type="number" id="pidcal-kd-base" class="form-input" value="10" step="1">
              </div>
            </div>
          </div>

          <div class="form-row">
            <div class="form-group">
              <label class="form-label">Current loop Kp (fixed)</label>
              <input type="number" id="pidcal-current-kp" class="form-input" value="50" step="1">
            </div>
            <div class="form-group">
              <label class="form-label">Current loop Ki (fixed)</label>
              <input type="number" id="pidcal-current-ki" class="form-input" value="50" step="1">
            </div>
            <div class="form-group">
              <span class="form-label" style="font-size:0.75rem; color:var(--color-text-secondary);">Used only for Speed/Position loop — keeps the current loop unchanged.</span>
            </div>
          </div>

          <div class="form-row">
            <div class="form-group">
              <label class="form-label">Measurement time (s)</label>
              <input type="number" id="pidcal-measure" class="form-input" value="1.0" step="0.1">
            </div>
            <div class="form-group">
              <label class="form-label">Settle time (s)</label>
              <input type="number" id="pidcal-settle" class="form-input" value="0.3" step="0.1">
            </div>
            <div class="form-group">
              <label class="form-label">Save path</label>
              <input type="text" id="pidcal-save-path" class="form-input" placeholder="config/pid_calibration_results.json">
            </div>
          </div>

          <div class="action-grid" style="grid-template-columns:repeat(4, 1fr);">
            <button class="btn btn-primary" onclick="PidCalibrationComponent.start()">▶ Start</button>
            <button class="btn btn-warning" onclick="PidCalibrationComponent.stop()">⏹ Stop</button>
            <button class="btn btn-secondary" onclick="PidCalibrationComponent.refresh()">⟳ Refresh</button>
            <button class="btn btn-secondary" onclick="PidCalibrationComponent.save()">💾 Save</button>
          </div>
        </div>

        <div id="pidcal-message" style="margin-top:8px; font-weight:600;"></div>

        <h3 style="margin:16px 0 8px; font-size:0.9rem; color:var(--color-text-secondary);">Results</h3>
        <div id="pidcal-results" style="overflow-x:auto;">No results yet.</div>
      </div>`;
  }

  function num(id) {
    const el = document.getElementById(id);
    return el ? Number(el.value) : 0;
  }

  function start() {
    const body = {
      axis_id: Number(document.getElementById('pidcal-axis').value),
      loop: Number(document.getElementById('pidcal-loop').value),
      coefficient: Number(document.getElementById('pidcal-coeff').value),
      min_speed_dps: num('pidcal-min-speed'),
      max_speed_dps: num('pidcal-max-speed'),
      speed_step_dps: num('pidcal-speed-step'),
      min_kp: num('pidcal-kp-min'),
      max_kp: num('pidcal-kp-max'),
      kp_step: num('pidcal-kp-step'),
      min_ki: num('pidcal-ki-min'),
      max_ki: num('pidcal-ki-max'),
      ki_step: num('pidcal-ki-step'),
      min_kd: num('pidcal-kd-min'),
      max_kd: num('pidcal-kd-max'),
      kd_step: num('pidcal-kd-step'),
      base_kp: num('pidcal-kp-base'),
      base_ki: num('pidcal-ki-base'),
      base_kd: num('pidcal-kd-base'),
      measurement_time_s: num('pidcal-measure'),
      settle_time_s: num('pidcal-settle'),
      current_kp: num('pidcal-current-kp'),
      current_ki: num('pidcal-current-ki'),
    };

    Api.post('/api/pidcal/start', body).then((data) => {
      showMessage(data.message || (data.success ? 'Calibration started' : 'Failed to start'), data.success);
      ensurePolling();
    }).catch((err) => showMessage(err.message, false));
  }

  function stop() {
    Api.post('/api/pidcal/stop', {}).then(() => {
      showMessage('Stop requested', true);
      refresh();
    }).catch((err) => showMessage(err.message, false));
  }

  function save() {
    const path = document.getElementById('pidcal-save-path').value.trim();
    Api.post('/api/pidcal/save', { file_path: path }).then((data) => {
      showMessage(data.message + (data.file_path ? ': ' + data.file_path : ''), data.success);
    }).catch((err) => showMessage(err.message, false));
  }

  function refresh() {
    Api.get('/api/pidcal/status').then(renderStatus).catch(() => {
      showMessage('PID calibration service unavailable', false);
    });
  }

  function renderStatus(data) {
    const stateEl = document.getElementById('pidcal-state');
    if (stateEl) {
      stateEl.textContent = data.state || 'IDLE';
      const colors = { RUNNING: 'var(--color-warning)', COMPLETED: 'var(--color-success, #4caf50)', ERROR: 'var(--color-danger)', STOPPED: 'var(--color-text-secondary)' };
      stateEl.style.color = colors[data.state] || 'var(--color-text)';
    }
    document.getElementById('pidcal-progress').textContent = (data.progress_percent || 0).toFixed(1) + '%';
    document.getElementById('pidcal-tested').textContent = `${data.tested_combinations || 0} / ${data.total_combinations || 0}`;
    document.getElementById('pidcal-speed').textContent = data.current_speed_dps != null ? Number(data.current_speed_dps).toFixed(2) + '°/s' : '--';

    const bestEl = document.getElementById('pidcal-best');
    if (bestEl) {
      const best = data.current_speed_best;
      if (best && best.score !== undefined && best.score !== null) {
        bestEl.textContent = `Kp=${best.kp} Ki=${best.ki} Kd=${best.kd} · v=${Number(best.target_speed_dps).toFixed(2)}°/s · mean=${Number(best.mean_speed_dps).toFixed(3)}°/s · σ=${Number(best.stddev_speed_dps).toFixed(4)}`;
      } else {
        bestEl.textContent = '--';
      }
    }

    if (data.message) showMessage(data.message, data.state === 'COMPLETED');

    renderResults(data.last_result);

    if (data.running) {
      ensurePolling();
    } else {
      stopPolling();
    }
  }

  function renderResults(result) {
    const container = document.getElementById('pidcal-results');
    if (!container) return;
    if (!result || !result.points || result.points.length === 0) {
      container.textContent = result && result.error_message ? result.error_message : 'No results yet.';
      return;
    }

    let html = '';
    const loop = LOOP_LABELS[result.loop] || String(result.loop);
    const coeff = COEFF_LABELS[result.coefficient] || String(result.coefficient);
    html += `<p style="margin:4px 0;">Axis ${result.axis_id} · ${loop} loop · coefficient ${coeff}</p>`;

    if (result.overall_winner) {
      const w = result.overall_winner;
      const err = Math.abs(w.mean_speed_dps - w.target_speed_dps);
      html += `<div class="sensor-card" style="margin-bottom:8px;">
        <span class="sensor-label">Overall winner (${coeff})</span>
        <span class="sensor-value">Kp=${w.kp} Ki=${w.ki} Kd=${w.kd} · v=${Number(w.target_speed_dps).toFixed(2)}°/s · mean=${Number(w.mean_speed_dps).toFixed(3)}°/s · σ=${Number(w.stddev_speed_dps).toFixed(4)} · err=${err.toFixed(4)}</span>
      </div>`;
    }

    html += '<table class="table" style="width:100%; font-size:0.85rem;"><thead><tr>' +
      '<th>Target (°/s)</th><th>Kp</th><th>Ki</th><th>Kd</th><th>Mean</th><th>Err</th><th>StdDev</th><th>Score</th><th>Samples</th></tr></thead><tbody>';

    result.points.forEach(p => {
      const err = Math.abs(p.mean_speed_dps - p.target_speed_dps);
      html += `<tr>
        <td>${Number(p.target_speed_dps).toFixed(2)}</td>
        <td>${p.kp}</td><td>${p.ki}</td><td>${p.kd}</td>
        <td>${Number(p.mean_speed_dps).toFixed(3)}</td>
        <td>${err.toFixed(4)}</td>
        <td>${Number(p.stddev_speed_dps).toFixed(4)}</td>
        <td>${Number(p.score).toFixed(4)}</td>
        <td>${p.sample_count}</td></tr>`;
    });
    html += '</tbody></table>';
    container.innerHTML = html;
  }

  function showMessage(text, ok) {
    const el = document.getElementById('pidcal-message');
    if (!el) return;
    el.textContent = text || '';
    el.style.color = ok === false ? 'var(--color-danger)' : 'var(--color-text-secondary)';
  }

  function ensurePolling() {
    if (pollTimer) return;
    pollTimer = setInterval(refresh, 1500);
  }

  function stopPolling() {
    if (pollTimer) {
      clearInterval(pollTimer);
      pollTimer = null;
    }
  }

  return { render, start, stop, refresh, save };
})();
