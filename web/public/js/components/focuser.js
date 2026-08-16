/**
 * Focuser Control Panel Component
 *
 * Unified card-based style matching the existing UI.
 */
const FocuserComponent = (() => {
  'use strict';

  function render() {
    return `
      <div class="focuser-dashboard">
        <div class="sensor-grid">
          <div class="sensor-card">
            <span class="sensor-label">Position</span>
            <span class="sensor-value"><span id="foc-position">0</span> / <span id="foc-max">100000</span></span>
          </div>
          <div class="sensor-card">
            <span class="sensor-label">Temperature</span>
            <span class="sensor-value" id="foc-temp">--°C</span>
          </div>
          <div class="sensor-card">
            <span class="sensor-label">HFD</span>
            <span class="sensor-value" id="foc-hfd">--</span>
          </div>
          <div class="sensor-card">
            <span class="sensor-label">Moving</span>
            <span class="sensor-value" id="foc-moving">No</span>
          </div>
        </div>

        <h3 style="margin:16px 0 8px; font-size:0.9rem; color:var(--color-text-secondary);">Manual Control</h3>
        <div class="control-form">
          <div class="form-row">
            <div class="form-group">
              <label for="foc-target" class="form-label">Target Position (steps)</label>
              <input type="number" id="foc-target" class="form-input" value="50000" min="0" style="width:160px;">
            </div>
            <div class="form-group">
              <label for="foc-speed" class="form-label">Speed: <span id="foc-speed-label">50</span>%</label>
              <input type="range" id="foc-speed" class="speed-slider" min="1" max="100" value="50"
                     oninput="document.getElementById('foc-speed-label').textContent=this.value">
            </div>
          </div>
          <div class="action-grid" style="grid-template-columns:1fr 1fr;">
            <button class="btn btn-primary" onclick="FocuserComponent.move()">Move</button>
            <button class="btn btn-danger" onclick="FocuserComponent.halt()">Halt</button>
          </div>
        </div>

        <h3 style="margin:16px 0 8px; font-size:0.9rem; color:var(--color-text-secondary);">Auto-Focus</h3>
        <div class="control-form">
          <div class="form-row">
            <div class="form-group">
              <label for="foc-af-start" class="form-label">Start Position</label>
              <input type="number" id="foc-af-start" class="form-input" value="40000" style="width:120px;">
            </div>
            <div class="form-group">
              <label for="foc-af-end" class="form-label">End Position</label>
              <input type="number" id="foc-af-end" class="form-input" value="60000" style="width:120px;">
            </div>
            <div class="form-group">
              <label for="foc-af-step" class="form-label">Step Size</label>
              <input type="number" id="foc-af-step" class="form-input" value="500" style="width:100px;">
            </div>
          </div>
          <button class="btn btn-warning" onclick="FocuserComponent.autoFocus()">🔍 Run Auto-Focus</button>
          <div class="progress-bar" style="margin-top:8px;">
            <div id="foc-af-progress" style="width:0%; height:100%; background:var(--color-primary); border-radius:4px; transition:width 0.3s ease;"></div>
          </div>
        </div>

        <div id="foc-curve" class="chart-placeholder" style="margin-top:16px; padding:20px; text-align:center; color:var(--color-text-muted); border:1px dashed var(--color-border); border-radius:8px;">
          📊 Focus Curve Chart
        </div>
      </div>`;
  }

  function move() {
    const pos = parseInt(document.getElementById('foc-target').value);
    Api.post('/api/focuser/move', { position: pos });
  }

  function halt() {
    Api.post('/api/focuser/halt', {});
  }

  function autoFocus() {
    const start = parseInt(document.getElementById('foc-af-start').value);
    const end = parseInt(document.getElementById('foc-af-end').value);
    const step = parseInt(document.getElementById('foc-af-step').value);
    Api.post('/api/focuser/autofocus', { start_position: start, end_position: end, step_size: step });
  }

  function refreshStatus() {
    Api.get('/api/focuser/status').then(data => {
      document.getElementById('foc-position').textContent = data.position || 0;
      document.getElementById('foc-max').textContent = data.max_position || 100000;
      document.getElementById('foc-temp').textContent = data.temperature_c ? data.temperature_c.toFixed(1) + '°C' : '--°C';
      document.getElementById('foc-hfd').textContent = data.hfd ? data.hfd.toFixed(2) : '--';
      document.getElementById('foc-moving').textContent = data.moving ? 'Yes' : 'No';
    }).catch(() => App.showServiceUnavailable('panel-focuser', 'Focuser service'));
  }

  return { render, move, halt, autoFocus, refreshStatus };
})();
