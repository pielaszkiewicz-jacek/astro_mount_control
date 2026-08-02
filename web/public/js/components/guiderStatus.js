/**
 * ST4 Guider Status Panel Component
 *
 * Unified card-based style matching the existing UI.
 */
const GuiderComponent = (() => {
  'use strict';

  function render() {
    return `
      <div class="guider-dashboard">
        <div class="sensor-grid">
          <div class="sensor-card">
            <span class="sensor-label">Status</span>
            <span class="sensor-value" id="guider-status">Stopped</span>
          </div>
          <div class="sensor-card">
            <span class="sensor-label">Interface</span>
            <span class="sensor-value" id="guider-interface">--</span>
          </div>
          <div class="sensor-card">
            <span class="sensor-label">Pulses Sent</span>
            <span class="sensor-value" id="guider-pulses">0</span>
          </div>
          <div class="sensor-card">
            <span class="sensor-label">RMS RA</span>
            <span class="sensor-value" id="guider-rms-ra">--"</span>
          </div>
          <div class="sensor-card">
            <span class="sensor-label">RMS Dec</span>
            <span class="sensor-value" id="guider-rms-dec">--"</span>
          </div>
          <div class="sensor-card">
            <span class="sensor-label">Calibrated</span>
            <span class="sensor-value" id="guider-calibrated">No</span>
          </div>
        </div>

        <div class="action-grid" style="margin-top:12px;">
          <button class="btn btn-primary" onclick="GuiderComponent.start()">▶ Start Guiding</button>
          <button class="btn btn-danger" onclick="GuiderComponent.stop()">⏹ Stop Guiding</button>
          <button class="btn btn-secondary" onclick="GuiderComponent.calibrate()">⚙ Calibrate</button>
        </div>

        <h3 style="margin:16px 0 8px; font-size:0.9rem; color:var(--color-text-secondary);">Interface Configuration</h3>
        <div class="control-form">
          <div class="form-row">
            <div class="form-group">
              <label for="guider-type" class="form-label">Type</label>
              <select id="guider-type" class="form-input form-select">
                <option value="simulated">Simulated</option>
                <option value="gpio_sysfs">GPIO sysfs</option>
                <option value="gpiolib">GPIO libgpiod</option>
                <option value="ftdi">FTDI FT232H</option>
                <option value="mcp2221">MCP2221</option>
                <option value="arduino">Arduino Serial</option>
              </select>
            </div>
            <div class="form-group">
              <label for="guider-device" class="form-label">Device Path</label>
              <input type="text" id="guider-device" class="form-input" value="/dev/ttyACM0" placeholder="/dev/ttyACM0">
            </div>
          </div>
          <div class="form-row">
            <div class="form-group">
              <label for="guider-aggression" class="form-label">Aggression: <span id="guider-aggression-label">0.8</span></label>
              <input type="range" id="guider-aggression" class="speed-slider" min="0" max="1" step="0.1" value="0.8"
                     oninput="document.getElementById('guider-aggression-label').textContent=this.value">
            </div>
            <div class="form-group">
              <label for="guider-min-pulse" class="form-label">Min Pulse (ms)</label>
              <input type="number" id="guider-min-pulse" class="form-input" value="10" min="1" style="width:100px;">
            </div>
            <div class="form-group">
              <label for="guider-max-pulse" class="form-label">Max Pulse (ms)</label>
              <input type="number" id="guider-max-pulse" class="form-input" value="3000" min="10" style="width:120px;">
            </div>
          </div>
          <button class="btn btn-secondary" onclick="GuiderComponent.applyConfig()">Apply Configuration</button>
        </div>

        <div id="guider-graph" class="chart-placeholder" style="margin-top:16px; padding:20px; text-align:center; color:var(--color-text-muted); border:1px dashed var(--color-border); border-radius:8px;">
          Correction graph will appear here
        </div>
      </div>`;
  }

  function start() { Api.post('/api/guider/start', {}); }
  function stop() { Api.post('/api/guider/stop', {}); }
  function calibrate() { Api.post('/api/guider/calibrate', {}); }
  function applyConfig() {
    const type = document.getElementById('guider-type').value;
    const device = document.getElementById('guider-device').value;
    Api.post('/api/guider/config', { interface_type: type, device_path: device });
  }

  function refreshStatus() {
    Api.get('/api/guider/status').then(data => {
      document.getElementById('guider-status').textContent = data.guiding ? '🔵 Guiding' : 'Stopped';
      document.getElementById('guider-interface').textContent = data.interface_type || '--';
      document.getElementById('guider-pulses').textContent = data.pulses_sent || 0;
      document.getElementById('guider-rms-ra').textContent = data.rms_ra ? data.rms_ra.toFixed(2) + '"' : '--"';
      document.getElementById('guider-rms-dec').textContent = data.rms_dec ? data.rms_dec.toFixed(2) + '"' : '--"';
      document.getElementById('guider-calibrated').textContent = data.calibrated ? 'Yes' : 'No';
    }).catch(() => {});
  }

  return { render, start, stop, calibrate, applyConfig, refreshStatus };
})();
