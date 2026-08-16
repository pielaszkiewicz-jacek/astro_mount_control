/**
 * Camera Control Panel Component
 *
 * Unified card-based style matching the existing UI.
 */
const CameraComponent = (() => {
  'use strict';

  function render() {
    return `
      <div class="camera-dashboard">
        <h3 style="margin:0 0 8px; font-size:0.9rem; color:var(--color-text-secondary);">Camera Info</h3>
        <div class="sensor-grid">
          <div class="sensor-card">
            <span class="sensor-label">Model</span>
            <span class="sensor-value" id="cam-model">--</span>
          </div>
          <div class="sensor-card">
            <span class="sensor-label">Sensor</span>
            <span class="sensor-value" id="cam-sensor">--</span>
          </div>
          <div class="sensor-card">
            <span class="sensor-label">Chip</span>
            <span class="sensor-value" id="cam-chip">--</span>
          </div>
          <div class="sensor-card">
            <span class="sensor-label">Cooler</span>
            <span class="sensor-value" id="cam-cooler">--</span>
          </div>
        </div>

        <h3 style="margin:16px 0 8px; font-size:0.9rem; color:var(--color-text-secondary);">Exposure</h3>
        <div class="control-form">
          <div class="form-row">
            <div class="form-group">
              <label for="cam-exposure" class="form-label">Time (s)</label>
              <input type="number" id="cam-exposure" class="form-input" value="60" min="0.001" step="0.001" style="width:120px;">
            </div>
            <div class="form-group">
              <label for="cam-gain" class="form-label">Gain: <span id="cam-gain-label">0</span></label>
              <input type="range" id="cam-gain" class="speed-slider" min="0" max="100" value="0"
                     oninput="document.getElementById('cam-gain-label').textContent=this.value">
            </div>
            <div class="form-group">
              <label for="cam-binning" class="form-label">Binning</label>
              <select id="cam-binning" class="form-input form-select" style="width:80px;">
                <option value="1">1x1</option>
                <option value="2">2x2</option>
                <option value="3">3x3</option>
              </select>
            </div>
            <div class="form-group">
              <label for="cam-filter" class="form-label">Filter</label>
              <select id="cam-filter" class="form-input form-select" style="width:80px;">
                <option value="0">L</option>
                <option value="1">R</option>
                <option value="2">G</option>
                <option value="3">B</option>
                <option value="4">Ha</option>
              </select>
            </div>
          </div>
          <div class="action-grid" style="grid-template-columns:1fr 1fr;">
            <button class="btn btn-primary" onclick="CameraComponent.startExposure()">▶ Start Exposure</button>
            <button class="btn btn-danger" onclick="CameraComponent.abort()">⏹ Abort</button>
          </div>
          <div class="progress-bar" style="margin-top:8px;">
            <div id="cam-progress" style="width:0%; height:100%; background:var(--color-primary); border-radius:4px; transition:width 0.3s ease;"></div>
          </div>
        </div>

        <h3 style="margin:16px 0 8px; font-size:0.9rem; color:var(--color-text-secondary);">Cooler</h3>
        <div class="control-form">
          <div class="form-row">
            <div class="form-group">
              <label for="cam-cooler-target" class="form-label">Target Temperature (°C)</label>
              <input type="number" id="cam-cooler-target" class="form-input" value="-10" style="width:120px;">
            </div>
            <div class="form-group">
              <span class="form-label">Current</span>
              <span id="cam-cooler-current" style="font-weight:600;">--°C</span>
            </div>
            <div class="form-group">
              <span class="form-label">Cooler Power</span>
              <span id="cam-cooler-power" style="font-weight:600;">--%</span>
            </div>
            <div class="form-group" style="align-self:flex-end;">
              <button class="btn btn-secondary" onclick="CameraComponent.setCooler()">Set Cooler</button>
            </div>
          </div>
        </div>

        <div id="cam-preview" class="chart-placeholder" style="margin-top:16px; padding:40px; text-align:center; color:var(--color-text-muted); border:1px dashed var(--color-border); border-radius:8px;">
          📷 Preview image will appear here
        </div>
      </div>`;
  }

  function startExposure() {
    const time = parseFloat(document.getElementById('cam-exposure').value);
    const gain = parseInt(document.getElementById('cam-gain').value);
    const binning = parseInt(document.getElementById('cam-binning').value);
    const filter = parseInt(document.getElementById('cam-filter').value);
    Api.post('/api/camera/expose', { exposure_time_s: time, gain, binning, filter: { position: filter } });
  }

  function abort() { Api.post('/api/camera/abort', {}); }
  function setCooler() {
    const target = parseFloat(document.getElementById('cam-cooler-target').value);
    Api.post('/api/camera/cooler', { target_c: target, enabled: true })
      .then(refreshCooler);
  }

  function refreshCooler() {
    Api.get('/api/camera/cooler').then(data => {
      const cur = document.getElementById('cam-cooler-current');
      if (cur) cur.textContent = (data.current_c || 0).toFixed(1) + '°C';
      const pwr = document.getElementById('cam-cooler-power');
      if (pwr) pwr.textContent = (data.power_percent || 0).toFixed(0) + '%';
    }).catch(() => { /* cooler status is best-effort */ });
  }

  function refreshInfo() {
    Api.get('/api/camera/info').then(data => {
      document.getElementById('cam-model').textContent = data.name || '--';
      document.getElementById('cam-sensor').textContent = data.sensor_name || '--';
      document.getElementById('cam-chip').textContent = (data.width || '--') + '×' + (data.height || '--');
      document.getElementById('cam-cooler').textContent = data.has_cooler ? '✅ Yes' : 'No';
    }).catch(() => App.showServiceUnavailable('panel-camera', 'Camera service'));
    refreshCooler();
  }

  return { render, startExposure, abort, setCooler, refreshInfo };
})();
