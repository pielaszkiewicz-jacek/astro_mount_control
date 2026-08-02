/**
 * Derotator Control Panel Component
 *
 * Unified card-based style matching the existing UI.
 */
const DerotatorComponent = (() => {
  'use strict';

  function render() {
    return `
      <div class="derotator-dashboard">
        <div class="sensor-grid">
          <div class="sensor-card">
            <span class="sensor-label">Position</span>
            <span class="sensor-value" id="derot-pos">0.0°</span>
          </div>
          <div class="sensor-card">
            <span class="sensor-label">Rate</span>
            <span class="sensor-value" id="derot-rate">0.0 °/s</span>
          </div>
          <div class="sensor-card">
            <span class="sensor-label">Homed</span>
            <span class="sensor-value" id="derot-homed">No</span>
          </div>
          <div class="sensor-card">
            <span class="sensor-label">Moving</span>
            <span class="sensor-value" id="derot-moving">No</span>
          </div>
        </div>

        <h3 style="margin:16px 0 8px; font-size:0.9rem; color:var(--color-text-secondary);">Mode</h3>
        <div class="control-form">
          <div class="form-row" style="gap:12px; flex-wrap:wrap;">
            <label class="checkbox-label" style="gap:6px;">
              <input type="radio" name="derotator-mode" value="0" checked onchange="DerotatorComponent.setMode(0)"> Disabled
            </label>
            <label class="checkbox-label" style="gap:6px;">
              <input type="radio" name="derotator-mode" value="1" onchange="DerotatorComponent.setMode(1)"> Auto
            </label>
            <label class="checkbox-label" style="gap:6px;">
              <input type="radio" name="derotator-mode" value="2" onchange="DerotatorComponent.setMode(2)"> Fixed Angle
            </label>
            <label class="checkbox-label" style="gap:6px;">
              <input type="radio" name="derotator-mode" value="3" onchange="DerotatorComponent.setMode(3)"> Manual Rate
            </label>
          </div>
        </div>

        <h3 style="margin:16px 0 8px; font-size:0.9rem; color:var(--color-text-secondary);">Control</h3>
        <div class="control-form">
          <div class="form-row">
            <div class="form-group">
              <label for="derot-angle" class="form-label">Angle (°)</label>
              <input type="number" id="derot-angle" class="form-input" value="0" step="0.1" style="width:120px;">
            </div>
            <div class="form-group" style="align-self:flex-end;">
              <button class="btn btn-primary" onclick="DerotatorComponent.setAngle()">Set Angle</button>
            </div>
          </div>
          <div class="form-row">
            <div class="form-group">
              <label for="derot-rate-input" class="form-label">Rate (°/s)</label>
              <input type="number" id="derot-rate-input" class="form-input" value="0" step="0.1" style="width:120px;">
            </div>
            <div class="form-group" style="align-self:flex-end;">
              <button class="btn btn-primary" onclick="DerotatorComponent.setRate()">Set Rate</button>
            </div>
          </div>
          <div class="action-grid" style="grid-template-columns:1fr 1fr;">
            <button class="btn btn-secondary" onclick="DerotatorComponent.home()">🏠 Home</button>
            <button class="btn btn-secondary" onclick="DerotatorComponent.refreshStatus()">⟳ Refresh</button>
          </div>
        </div>

        <h3 style="margin:16px 0 8px; font-size:0.9rem; color:var(--color-text-secondary);">Field Rotation</h3>
        <div class="sensor-grid">
          <div class="sensor-card">
            <span class="sensor-label">Current Angle</span>
            <span class="sensor-value" id="fieldrot-angle">--°</span>
          </div>
          <div class="sensor-card">
            <span class="sensor-label">Rate</span>
            <span class="sensor-value" id="fieldrot-rate">-- "/s</span>
          </div>
          <div class="sensor-card">
            <span class="sensor-label">Predicted (10min)</span>
            <span class="sensor-value" id="fieldrot-pred">--°</span>
          </div>
        </div>
      </div>`;
  }

  function setMode(mode) { Api.post('/api/derotator/mode', { mode }); }
  function setAngle() {
    const angle = parseFloat(document.getElementById('derot-angle').value);
    Api.post('/api/derotator/angle', { angle_deg: angle });
  }
  function setRate() {
    const rate = parseFloat(document.getElementById('derot-rate-input').value);
    Api.post('/api/derotator/rate', { rate_deg_s: rate });
  }
  function home() { Api.post('/api/derotator/home', {}); }

  function refreshStatus() {
    Api.get('/api/derotator/status').then(data => {
      document.getElementById('derot-pos').textContent = (data.current_position_deg || 0).toFixed(1) + '°';
      document.getElementById('derot-rate').textContent = (data.current_rate_deg_s || 0).toFixed(1) + ' °/s';
      document.getElementById('derot-homed').textContent = data.homed ? 'Yes' : 'No';
      document.getElementById('derot-moving').textContent = data.moving ? 'Yes' : 'No';
      const radios = document.getElementsByName('derotator-mode');
      if (radios.length > data.mode) radios[data.mode].checked = true;
    }).catch(() => {});

    Api.get('/api/derotator/field-rotation').then(data => {
      document.getElementById('fieldrot-angle').textContent = (data.current_angle_deg || 0).toFixed(1) + '°';
      document.getElementById('fieldrot-rate').textContent = (data.current_rate_arcsec_s || 0).toFixed(1) + ' "/s';
      document.getElementById('fieldrot-pred').textContent = (data.predicted_angle_10min || 0).toFixed(1) + '°';
    }).catch(() => {});
  }

  return { render, setMode, setAngle, setRate, home, refreshStatus };
})();
