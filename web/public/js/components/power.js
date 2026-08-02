/**
 * Power Management Panel Component
 *
 * Unified card-based style matching the existing UI.
 */
const PowerComponent = (() => {
  'use strict';

  function render() {
    return `
      <div class="power-dashboard">
        <div class="sensor-grid">
          <div class="sensor-card">
            <span class="sensor-label">Voltage</span>
            <span class="sensor-value" id="pwr-voltage">-- V</span>
          </div>
          <div class="sensor-card">
            <span class="sensor-label">Current</span>
            <span class="sensor-value" id="pwr-current">-- A</span>
          </div>
          <div class="sensor-card">
            <span class="sensor-label">Power</span>
            <span class="sensor-value" id="pwr-power">-- W</span>
          </div>
          <div class="sensor-card">
            <span class="sensor-label">Battery</span>
            <span class="sensor-value" id="pwr-battery">--%</span>
          </div>
          <div class="sensor-card">
            <span class="sensor-label">Runtime</span>
            <span class="sensor-value" id="pwr-runtime">-- min</span>
          </div>
          <div class="sensor-card">
            <span class="sensor-label">Temperature</span>
            <span class="sensor-value" id="pwr-temp">--°C</span>
          </div>
        </div>

        <div class="control-form" style="margin-top:12px;">
          <div class="form-row" style="gap:16px;">
            <div class="form-group">
              <span class="form-label">Source</span>
              <span id="pwr-source" style="font-weight:600;">--</span>
            </div>
            <div class="form-group">
              <span class="form-label">Charging</span>
              <span id="pwr-charging" style="font-weight:600;">--</span>
            </div>
          </div>
        </div>

        <h3 style="margin:16px 0 8px; font-size:0.9rem; color:var(--color-text-secondary);">Power Outputs</h3>
        <div id="pwr-outputs" class="control-form"></div>

        <h3 style="margin:16px 0 8px; font-size:0.9rem; color:var(--color-text-secondary);">Auto-Park Settings</h3>
        <div class="control-form">
          <div class="form-row">
            <div class="form-group">
              <label for="pwr-low-v" class="form-label">Low Voltage Threshold (V)</label>
              <input type="number" id="pwr-low-v" class="form-input" value="11.5" step="0.1" style="width:120px;">
            </div>
            <div class="form-group" style="align-self:flex-end;">
              <button class="btn btn-secondary" onclick="PowerComponent.refresh()">⟳ Refresh</button>
            </div>
          </div>
        </div>
      </div>`;
  }

  function refresh() {
    Api.get('/api/power/status').then(data => {
      document.getElementById('pwr-voltage').textContent = (data.voltage_v || 0).toFixed(1) + ' V';
      document.getElementById('pwr-current').textContent = (data.current_a || 0).toFixed(2) + ' A';
      document.getElementById('pwr-power').textContent = (data.power_w || 0).toFixed(1) + ' W';
      document.getElementById('pwr-battery').textContent = (data.charge_percent || 0) + '%';
      document.getElementById('pwr-runtime').textContent = (data.estimated_runtime_min || 0) + ' min';
      document.getElementById('pwr-temp').textContent = (data.temperature_c || 0).toFixed(1) + '°C';
      document.getElementById('pwr-source').textContent = data.on_battery ? '🔋 Battery' : '🔌 External';
      document.getElementById('pwr-charging').textContent = data.charging ? 'Yes' : 'No';

      const outputsContainer = document.getElementById('pwr-outputs');
      if (outputsContainer && data.outputs) {
        outputsContainer.innerHTML = data.outputs.map(o =>
          `<label class="checkbox-label" style="gap:8px; margin:4px 0;">
            <input type="checkbox" ${o.enabled ? 'checked' : ''} onchange="Api.post('/api/power/output', { output_id: ${o.id}, enabled: this.checked })">
            ${o.name} (${(o.voltage_v || 0).toFixed(1)}V / ${(o.current_a || 0).toFixed(2)}A)
            ${o.overload ? '<span class="status-badge error">OVERLOAD</span>' : ''}
          </label>`
        ).join('');
      }
    }).catch(() => {});
  }

  return { render, refresh };
})();
