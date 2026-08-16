/**
 * PEC (Periodic Error Correction) Panel Component
 *
 * Displays PEC status and controls in the unified card-based style.
 */
const PECComponent = (() => {
  'use strict';

  function render() {
    return `
      <div class="pec-dashboard">
        <div class="sensor-grid">
          <div class="sensor-card">
            <span class="sensor-label">Status</span>
            <span class="sensor-value" id="pec-enabled">Disabled</span>
          </div>
          <div class="sensor-card">
            <span class="sensor-label">Trained</span>
            <span class="sensor-value" id="pec-trained">No</span>
          </div>
          <div class="sensor-card">
            <span class="sensor-label">Peak Error</span>
            <span class="sensor-value" id="pec-peak">--"</span>
          </div>
          <div class="sensor-card">
            <span class="sensor-label">RMS Error</span>
            <span class="sensor-value" id="pec-rms">--"</span>
          </div>
          <div class="sensor-card">
            <span class="sensor-label">Harmonics</span>
            <span class="sensor-value" id="pec-harmonics">--</span>
          </div>
          <div class="sensor-card">
            <span class="sensor-label">Phase</span>
            <span class="sensor-value" id="pec-phase">0°</span>
          </div>
        </div>

        <div class="control-form" style="margin-top:12px;">
          <label class="checkbox-label" style="gap:8px; font-size:0.9rem;">
            <input type="checkbox" id="pec-toggle" onchange="PECComponent.toggle()">
            Enable PEC Correction
          </label>
        </div>

        <h3 style="margin:16px 0 8px; font-size:0.9rem; color:var(--color-text-secondary);">Training Configuration</h3>
        <div class="control-form">
          <div class="form-row">
            <div class="form-group">
              <label for="pec-worm-cycle" class="form-label">Worm Cycle (s)</label>
              <input type="number" id="pec-worm-cycle" class="form-input" value="638" min="10" max="3600">
            </div>
            <div class="form-group">
              <label for="pec-harmonics-count" class="form-label">Harmonics</label>
              <input type="number" id="pec-harmonics-count" class="form-input" value="8" min="1" max="20">
            </div>
            <div class="form-group">
              <label for="pec-duration" class="form-label">Duration (cycles)</label>
              <input type="number" id="pec-duration" class="form-input" value="3" min="1" max="10">
            </div>
          </div>
          <div class="form-row" style="gap:8px;">
            <button class="btn btn-primary" onclick="PECComponent.startTraining()">▶ Start Training</button>
            <button class="btn btn-danger" onclick="PECComponent.stopTraining()">⏹ Stop Training</button>
          </div>
          <div class="progress-bar" style="margin-top:8px;">
            <div id="pec-progress" style="width:0%; height:100%; background:var(--color-primary); border-radius:4px; transition:width 0.3s ease;"></div>
          </div>
        </div>

        <h3 style="margin:16px 0 8px; font-size:0.9rem; color:var(--color-text-secondary);">Data Management</h3>
        <div class="action-grid" style="grid-template-columns:1fr 1fr;">
          <button class="btn btn-secondary" onclick="PECComponent.save()">💾 Save PEC Data</button>
          <button class="btn btn-secondary" onclick="PECComponent.load()">📂 Load PEC Data</button>
        </div>

        <div id="pec-chart" class="chart-placeholder" style="margin-top:16px; padding:20px; text-align:center; color:var(--color-text-muted); border:1px dashed var(--color-border); border-radius:8px;">
          Harmonics chart will appear here
        </div>
      </div>`;
  }

  function toggle() {
    const enabled = document.getElementById('pec-toggle').checked;
    Api.post('/api/pec/enable', { enabled });
  }

  function startTraining() {
    const worm = parseFloat(document.getElementById('pec-worm-cycle').value);
    const harm = parseInt(document.getElementById('pec-harmonics-count').value);
    const dur = parseInt(document.getElementById('pec-duration').value);
    Api.post('/api/pec/train/start', { worm_cycle_seconds: worm, num_harmonics: harm, duration_cycles: dur });
  }

  function stopTraining() { Api.post('/api/pec/train/stop', {}); }
  function save() { Api.post('/api/pec/save', {}); }
  function load() { Api.post('/api/pec/load', {}); }

  function refreshStatus() {
    Api.get('/api/pec/status').then(data => {
      document.getElementById('pec-enabled').textContent = data.enabled ? 'Enabled' : 'Disabled';
      document.getElementById('pec-trained').textContent = data.trained ? 'Yes' : 'No';
      document.getElementById('pec-peak').textContent = data.peak_error_arcsec ? data.peak_error_arcsec.toFixed(1) + '"' : '--"';
      document.getElementById('pec-rms').textContent = data.rms_error_arcsec ? data.rms_error_arcsec.toFixed(1) + '"' : '--"';
      document.getElementById('pec-harmonics').textContent = data.num_harmonics || '--';
      document.getElementById('pec-phase').textContent = (data.current_phase_deg || 0).toFixed(1) + '°';
      const toggle = document.getElementById('pec-toggle');
      if (toggle) toggle.checked = data.enabled;
    }).catch(() => App.showServiceUnavailable('panel-pec', 'PEC service'));
  }

  return { render, toggle, startTraining, stopTraining, save, load, refreshStatus };
})();
