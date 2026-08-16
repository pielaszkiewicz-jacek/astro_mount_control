/**
 * Observation Sequencer Panel Component
 *
 * Unified card-based style matching the existing UI.
 */
const SequencerComponent = (() => {
  'use strict';

  function render() {
    return `
      <div class="sequencer-dashboard">
        <div class="sensor-grid">
          <div class="sensor-card">
            <span class="sensor-label">State</span>
            <span class="sensor-value" id="seq-state">IDLE</span>
          </div>
          <div class="sensor-card">
            <span class="sensor-label">Target</span>
            <span class="sensor-value"><span id="seq-target">--</span> / <span id="seq-targets">--</span></span>
          </div>
          <div class="sensor-card">
            <span class="sensor-label">Exposure</span>
            <span class="sensor-value"><span id="seq-exposure">--</span> / <span id="seq-exposures">--</span></span>
          </div>
          <div class="sensor-card">
            <span class="sensor-label">Progress</span>
            <span class="sensor-value" id="seq-progress-pct">0%</span>
          </div>
        </div>

        <div class="progress-bar" style="margin:8px 0;">
          <div id="seq-progress" style="width:0%; height:100%; background:var(--color-primary); border-radius:4px; transition:width 0.3s ease;"></div>
        </div>

        <div class="action-grid">
          <button class="btn btn-primary" onclick="SequencerComponent.start()">▶ Start</button>
          <button class="btn btn-danger" onclick="SequencerComponent.stop()">⏹ Stop</button>
          <button class="btn btn-warning" onclick="SequencerComponent.pause()">⏸ Pause</button>
          <button class="btn btn-secondary" onclick="SequencerComponent.resume()">▶ Resume</button>
        </div>

        <h3 style="margin:16px 0 8px; font-size:0.9rem; color:var(--color-text-secondary);">Target List</h3>
        <div id="seq-target-list" class="control-form" style="max-height:200px; overflow-y:auto;"></div>

        <div class="control-form" style="margin-top:12px;">
          <div class="form-row">
            <div class="form-group">
              <label for="seq-new-name" class="form-label">Name</label>
              <input type="text" id="seq-new-name" class="form-input" placeholder="Target name">
            </div>
            <div class="form-group">
              <label for="seq-new-ra" class="form-label">RA (hours)</label>
              <input type="number" id="seq-new-ra" class="form-input" step="0.001" placeholder="0.0">
            </div>
            <div class="form-group">
              <label for="seq-new-dec" class="form-label">Dec (°)</label>
              <input type="number" id="seq-new-dec" class="form-input" step="0.001" placeholder="0.0">
            </div>
            <div class="form-group" style="align-self:flex-end;">
              <button class="btn btn-secondary" onclick="SequencerComponent.addTarget()">+ Add</button>
            </div>
          </div>
          <button class="btn btn-secondary" onclick="SequencerComponent.loadPlan()">📂 Load Observation Plan</button>
        </div>

        <h3 style="margin:16px 0 8px; font-size:0.9rem; color:var(--color-text-secondary);">Session Log</h3>
        <div id="seq-log" class="control-form" style="max-height:150px; overflow-y:auto; font-size:0.82rem; color:var(--color-text-secondary);">
          <em>No log entries yet.</em>
        </div>
      </div>`;
  }

  function start() { Api.post('/api/sequencer/start', {}); }
  function stop() { Api.post('/api/sequencer/stop', {}); }
  function pause() { Api.post('/api/sequencer/pause', {}); }
  function resume() { Api.post('/api/sequencer/resume', {}); }

  function addTarget() {
    const name = document.getElementById('seq-new-name').value;
    const ra = parseFloat(document.getElementById('seq-new-ra').value);
    const dec = parseFloat(document.getElementById('seq-new-dec').value);
    if (name && !isNaN(ra) && !isNaN(dec)) {
      const list = document.getElementById('seq-target-list');
      const entry = document.createElement('div');
      entry.className = 'form-row';
      entry.style.cssText = 'padding:4px 0; border-bottom:1px solid var(--color-border); font-size:0.85rem;';
      entry.innerHTML = `<span style="font-weight:600;">${name}</span> <span style="color:var(--color-text-secondary);">RA=${ra.toFixed(3)}h Dec=${dec.toFixed(3)}°</span>`;
      list.appendChild(entry);
      document.getElementById('seq-new-name').value = '';
      document.getElementById('seq-new-ra').value = '';
      document.getElementById('seq-new-dec').value = '';
    }
  }

  function loadPlan() {
    Api.post('/api/sequencer/load', {}).then(r => {
      if (r.success) alert('Plan loaded');
    });
  }

  function refreshStatus() {
    Api.get('/api/sequencer/status').then(data => {
      document.getElementById('seq-state').textContent = data.state || 'IDLE';
      document.getElementById('seq-target').textContent = data.current_target || '--';
      document.getElementById('seq-targets').textContent = data.total_targets || '--';
      document.getElementById('seq-exposure').textContent = data.current_exposure || '--';
      document.getElementById('seq-exposures').textContent = data.total_exposures || '--';
      const pct = data.progress_percent || 0;
      document.getElementById('seq-progress-pct').textContent = pct.toFixed(1) + '%';
      const bar = document.getElementById('seq-progress');
      if (bar) bar.style.width = pct + '%';
    }).catch(() => App.showServiceUnavailable('panel-sequencer', 'Sequencer service'));
  }

  return { render, start, stop, pause, resume, addTarget, loadPlan, refreshStatus };
})();
