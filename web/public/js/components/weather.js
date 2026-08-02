/**
 * Weather Monitoring Panel Component
 *
 * Unified card-based style matching the existing UI.
 */
const WeatherComponent = (() => {
  'use strict';

  function render() {
    return `
      <div class="weather-dashboard">
        <div class="sensor-grid">
          <div class="sensor-card">
            <span class="sensor-label">Temperature</span>
            <span class="sensor-value" id="weather-temp">--°C</span>
          </div>
          <div class="sensor-card">
            <span class="sensor-label">Humidity</span>
            <span class="sensor-value" id="weather-humidity">--%</span>
          </div>
          <div class="sensor-card">
            <span class="sensor-label">Pressure</span>
            <span class="sensor-value" id="weather-pressure">-- hPa</span>
          </div>
          <div class="sensor-card">
            <span class="sensor-label">Wind</span>
            <span class="sensor-value" id="weather-wind">-- m/s</span>
          </div>
          <div class="sensor-card">
            <span class="sensor-label">Rain</span>
            <span class="sensor-value" id="weather-rain">-- mm/h</span>
          </div>
          <div class="sensor-card">
            <span class="sensor-label">Cloud Cover</span>
            <span class="sensor-value" id="weather-clouds">--%</span>
          </div>
          <div class="sensor-card">
            <span class="sensor-label">Dew Point</span>
            <span class="sensor-value" id="weather-dew">--°C</span>
          </div>
          <div class="sensor-card">
            <span class="sensor-label">Sky Brightness</span>
            <span class="sensor-value" id="weather-sky">-- mpsas</span>
          </div>
        </div>

        <div id="weather-safety" class="weather-safety" style="margin-top:12px; padding:10px 16px; border-radius:8px; font-weight:600; text-align:center; background:var(--color-success-bg); color:var(--color-success);">
          ✅ Safe to Observe
        </div>

        <h3 style="margin:16px 0 8px; font-size:0.9rem; color:var(--color-text-secondary);">Safety Rules</h3>
        <div class="control-form">
          <label class="checkbox-label" style="gap:8px;">
            <input type="checkbox" id="weather-autopark" checked>
            Auto-Park on Alert
          </label>
          <div class="form-row">
            <div class="form-group">
              <label for="weather-max-wind" class="form-label">Max Wind (m/s)</label>
              <input type="number" id="weather-max-wind" class="form-input" value="15" style="width:100px;">
            </div>
            <div class="form-group">
              <label for="weather-min-temp" class="form-label">Min Temp (°C)</label>
              <input type="number" id="weather-min-temp" class="form-input" value="-20" style="width:100px;">
            </div>
            <div class="form-group">
              <label for="weather-max-clouds" class="form-label">Max Cloud Cover (%)</label>
              <input type="number" id="weather-max-clouds" class="form-input" value="90" style="width:100px;">
            </div>
          </div>
          <button class="btn btn-secondary" onclick="WeatherComponent.refresh()">⟳ Refresh</button>
        </div>
      </div>`;
  }

  function refresh() {
    Api.get('/api/weather/status').then(data => {
      document.getElementById('weather-temp').textContent = (data.temperature_c || '--') + '°C';
      document.getElementById('weather-humidity').textContent = (data.humidity_percent || '--') + '%';
      document.getElementById('weather-pressure').textContent = (data.pressure_hpa || '--') + ' hPa';
      document.getElementById('weather-wind').textContent = (data.wind_speed_ms || '--') + ' m/s';
      document.getElementById('weather-rain').textContent = (data.rain_rate_mmh || '--') + ' mm/h';
      document.getElementById('weather-clouds').textContent = (data.cloud_cover_percent || '--') + '%';
      document.getElementById('weather-dew').textContent = (data.dew_point_c || '--') + '°C';
      document.getElementById('weather-sky').textContent = (data.sky_brightness_mpsas || '--') + ' mpsas';

      const safety = document.getElementById('weather-safety');
      if (safety) {
        if (data.safe_to_observe) {
          safety.textContent = '✅ Safe to Observe';
          safety.style.background = 'var(--color-success-bg)';
          safety.style.color = 'var(--color-success)';
        } else {
          safety.textContent = '⚠️ Unsafe Conditions — ' + (data.safety_message || 'Take action');
          safety.style.background = 'var(--color-danger-bg, rgba(244,67,54,0.1))';
          safety.style.color = 'var(--color-danger)';
        }
      }
    }).catch(() => {});
  }

  return { render, refresh };
})();
