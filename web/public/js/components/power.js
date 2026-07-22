/**
 * Power Management Panel Component
 */
const PowerComponent = (() => {
  'use strict';

  function render() {
    return `
      <div id="power-panel" class="panel">
        <h2>🔋 Power Management</h2>
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
          <div class="power-status">
            <p>Source: <span id="pwr-source">--</span></p>
            <p>Charging: <span id="pwr-charging">--</span></p>
          </div>
        </div>
        <div class="power-outputs">
          <h3>Outputs</h3>
          <div id="pwr-outputs"></div>
        </div>
        <div class="power-settings">
          <h3>Auto-Park</h3>
          <label>Low Voltage Threshold: <input type="number" id="pwr-low-v" value="11.5" step="0.1"> V</label>
          <button onclick="PowerComponent.refresh()">Refresh</button>
        </div>
      </div>`;
  }

  function refresh() {
    Api.get('/api/power/status').then(data => {
      document.getElementById('pwr-voltage').textContent = data.voltage_v + ' V';
      document.getElementById('pwr-current').textContent = data.current_a + ' A';
      document.getElementById('pwr-power').textContent = data.power_w + ' W';
      document.getElementById('pwr-battery').textContent = data.charge_percent + '%';
      document.getElementById('pwr-runtime').textContent = data.estimated_runtime_min + ' min';
      document.getElementById('pwr-temp').textContent = data.temperature_c + '°C';
      document.getElementById('pwr-source').textContent = data.on_battery ? 'Battery' : 'External';
      document.getElementById('pwr-charging').textContent = data.charging ? 'Yes' : 'No';
    }).catch(() => {});
  }

  return { render, refresh };
})();
