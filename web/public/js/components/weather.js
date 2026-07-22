/**
 * Weather Monitoring Panel Component
 */
const WeatherComponent = (() => {
  'use strict';

  function render() {
    return `
      <div id="weather-panel" class="panel">
        <h2>🌤️ Weather Monitor</h2>
        <div class="weather-current">
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
          </div>
          <div class="weather-safety" id="weather-safety">
            ✅ Safe to Observe
          </div>
        </div>
        <div class="weather-rules">
          <h3>Safety Rules</h3>
          <label>Auto-Park on Alert: <input type="checkbox" id="weather-autopark" checked></label>
          <label>Max Wind: <input type="number" id="weather-max-wind" value="15"> m/s</label>
          <label>Min Temp: <input type="number" id="weather-min-temp" value="-20"> °C</label>
          <label>Max Cloud Cover: <input type="number" id="weather-max-clouds" value="90"> %</label>
          <button onclick="WeatherComponent.refresh()">Refresh</button>
        </div>
      </div>`;
  }

  function refresh() {
    Api.get('/api/weather/status').then(data => {
      document.getElementById('weather-temp').textContent = data.temperature_c + '°C';
      document.getElementById('weather-humidity').textContent = data.humidity_percent + '%';
    }).catch(() => {});
  }

  return { render, refresh };
})();
