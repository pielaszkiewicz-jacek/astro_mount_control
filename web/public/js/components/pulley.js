/**
 * Pulley Controller Panel Component
 *
 * Controls observatory pulley/linear actuator systems in the unified
 * card-based style matching the existing UI.
 */
const PulleyComponent = (() => {
  'use strict';

  function render() {
    return `
      <div class="pulley-dashboard">
        <div class="pulley-visualization">
          <div class="pulley-track">
            <div class="pulley-carriage" id="pulley-carriage" style="bottom:0%;">
              <div class="pulley-load"></div>
            </div>
            <div class="pulley-endstop top"></div>
            <div class="pulley-endstop bottom"></div>
          </div>
        </div>

        <div class="sensor-grid">
          <div class="sensor-card">
            <span class="sensor-label">Position</span>
            <span class="sensor-value" id="pulley-position">0%</span>
          </div>
          <div class="sensor-card">
            <span class="sensor-label">Target</span>
            <span class="sensor-value" id="pulley-target">0%</span>
          </div>
          <div class="sensor-card">
            <span class="sensor-label">Speed</span>
            <span class="sensor-value" id="pulley-speed">50%</span>
          </div>
          <div class="sensor-card">
            <span class="sensor-label">Motor Current</span>
            <span class="sensor-value" id="pulley-current">-- A</span>
          </div>
          <div class="sensor-card">
            <span class="sensor-label">Temperature</span>
            <span class="sensor-value" id="pulley-temp">--°C</span>
          </div>
          <div class="sensor-card">
            <span class="sensor-label">Homed</span>
            <span class="sensor-value" id="pulley-homed">No</span>
          </div>
        </div>

        <div class="control-form" style="margin-top:8px;">
          <div class="form-row" style="gap:16px;">
            <div class="form-group">
              <span class="form-label">Status</span>
              <span id="pulley-state" style="font-weight:600;">Stopped</span>
            </div>
            <div class="form-group">
              <span class="form-label">Deployed</span>
              <span id="pulley-deployed">No</span>
            </div>
            <div class="form-group">
              <span class="form-label">Limits</span>
              <span id="pulley-limits">Lower</span>
            </div>
            <div class="form-group">
              <span class="form-label">Error</span>
              <span id="pulley-error">None</span>
            </div>
          </div>
        </div>

        <h3 style="margin:16px 0 8px; font-size:0.9rem; color:var(--color-text-secondary);">Position Control</h3>
        <div class="control-form">
          <div class="form-group">
            <label for="pulley-position-slider" class="form-label">Position: <span id="pulley-pos-label">0</span>%</label>
            <input type="range" id="pulley-position-slider" class="speed-slider" min="0" max="100" value="0" step="1"
                   oninput="document.getElementById('pulley-pos-label').textContent=this.value">
          </div>
          <div class="form-group">
            <label for="pulley-speed-slider" class="form-label">Speed: <span id="pulley-speed-label">50</span>%</label>
            <input type="range" id="pulley-speed-slider" class="speed-slider" min="1" max="100" value="50" step="1"
                   oninput="document.getElementById('pulley-speed-label').textContent=this.value">
          </div>
          <div class="action-grid" style="grid-template-columns:repeat(4, 1fr);">
            <button class="btn btn-primary" onclick="PulleyComponent.deploy()">⬆ Deploy</button>
            <button class="btn btn-warning" onclick="PulleyComponent.retract()">⬇ Retract</button>
            <button class="btn btn-danger" onclick="PulleyComponent.stop()">⏹ Stop</button>
            <button class="btn btn-secondary" onclick="PulleyComponent.goToPosition()">Go To</button>
          </div>
        </div>

        <h3 style="margin:16px 0 8px; font-size:0.9rem; color:var(--color-text-secondary);">Setup</h3>
        <div class="control-form">
          <div class="action-grid" style="grid-template-columns:1fr 1fr;">
            <button class="btn btn-secondary" onclick="PulleyComponent.home()">🏠 Home/Calibrate</button>
            <button class="btn btn-secondary" onclick="PulleyComponent.refreshStatus()">⟳ Refresh</button>
          </div>
          <div class="form-group" style="margin-top:8px;">
            <label for="pulley-type" class="form-label">Deployment Type</label>
            <select id="pulley-type" class="form-input form-select" style="width:auto;">
              <option value="generic">Generic Linear Actuator</option>
              <option value="flat_panel">Flat-Field Panel</option>
              <option value="curtain">Curtain/Blind</option>
              <option value="ventilation">Ventilation Louver</option>
              <option value="cable">Cable Management</option>
            </select>
          </div>
        </div>
      </div>`;
  }

  function deploy() {
    const speed = parseInt(document.getElementById('pulley-speed-slider').value);
    Api.post('/api/pulley/deploy', { position_percent: 100, speed_percent: speed });
  }

  function retract() {
    Api.post('/api/pulley/retract', {});
  }

  function stop() {
    Api.post('/api/pulley/stop', {});
  }

  function goToPosition() {
    const pos = parseInt(document.getElementById('pulley-position-slider').value);
    const speed = parseInt(document.getElementById('pulley-speed-slider').value);
    Api.post('/api/pulley/position', { position_percent: pos, speed_percent: speed });
  }

  function home() {
    Api.post('/api/pulley/home', {});
  }

  function refreshStatus() {
    Api.get('/api/pulley/status').then(data => {
      document.getElementById('pulley-position').textContent = (data.position_percent || 0) + '%';
      document.getElementById('pulley-target').textContent = (data.target_position_percent || 0) + '%';
      document.getElementById('pulley-speed').textContent = (data.speed_percent || 50) + '%';
      document.getElementById('pulley-current').textContent = (data.motor_current_a || 0).toFixed(2) + ' A';
      document.getElementById('pulley-temp').textContent = (data.temperature_c || 0).toFixed(1) + '°C';

      document.getElementById('pulley-state').textContent = data.moving ? '🔵 Moving' : 'Stopped';
      document.getElementById('pulley-state').style.color = data.moving ? 'var(--color-warning)' : 'var(--color-text)';
      document.getElementById('pulley-homed').textContent = data.homed ? 'Yes' : 'No';
      document.getElementById('pulley-deployed').textContent = data.deployed ? 'Yes' : 'No';

      const limits = [];
      if (data.at_upper_limit) limits.push('Upper');
      if (data.at_lower_limit) limits.push('Lower');
      document.getElementById('pulley-limits').textContent = limits.length ? limits.join(', ') : 'None';

      const errEl = document.getElementById('pulley-error');
      errEl.textContent = data.error ? data.error_message : 'None';
      errEl.style.color = data.error ? 'var(--color-danger)' : 'var(--color-text-secondary)';

      const carriage = document.getElementById('pulley-carriage');
      if (carriage) carriage.style.bottom = Math.max(0, Math.min(100, data.position_percent || 0)) + '%';

      const slider = document.getElementById('pulley-position-slider');
      if (slider) slider.value = data.position_percent || 0;
      const label = document.getElementById('pulley-pos-label');
      if (label) label.textContent = data.position_percent || 0;

      const typeSel = document.getElementById('pulley-type');
      if (typeSel && data.deployment_type) {
        for (let opt of typeSel.options) {
          if (opt.value === data.deployment_type) { opt.selected = true; break; }
        }
      }
    }).catch(() => {});
  }

  return { render, deploy, retract, stop, goToPosition, home, refreshStatus };
})();
