/**
 * ST4 Guider Status Panel Component
 */
const GuiderComponent = (() => {
  'use strict';

  function render() {
    return `
      <div id="guider-panel" class="panel">
        <h2>🎯 Guider (ST4)</h2>
        <div class="guider-controls">
          <button onclick="GuiderComponent.start()">▶ Start Guiding</button>
          <button onclick="GuiderComponent.stop()">⏹ Stop Guiding</button>
          <button onclick="GuiderComponent.calibrate()">⚙ Calibrate</button>
        </div>
        <div class="guider-status">
          <p>Status: <span id="guider-status">Stopped</span></p>
          <p>Interface: <span id="guider-interface">--</span></p>
          <p>Pulses Sent: <span id="guider-pulses">0</span></p>
          <p>RMS RA: <span id="guider-rms-ra">--"</span></p>
          <p>RMS Dec: <span id="guider-rms-dec">--"</span></p>
        </div>
        <div class="guider-config">
          <h3>Interface Configuration</h3>
          <label>Type:
            <select id="guider-type">
              <option value="simulated">Simulated</option>
              <option value="gpio_sysfs">GPIO sysfs</option>
              <option value="gpiolib">GPIO libgpiod</option>
              <option value="ftdi">FTDI FT232H</option>
              <option value="mcp2221">MCP2221</option>
              <option value="arduino">Arduino Serial</option>
            </select>
          </label>
          <label>Device: <input type="text" id="guider-device" value="/dev/ttyACM0"></label>
          <label>Aggression: <input type="range" id="guider-aggression" min="0" max="1" step="0.1" value="0.8"></label>
          <label>Min Pulse: <input type="number" id="guider-min-pulse" value="10"> ms</label>
          <label>Max Pulse: <input type="number" id="guider-max-pulse" value="3000"> ms</label>
          <button onclick="GuiderComponent.applyConfig()">Apply</button>
        </div>
        <div class="guider-graph" id="guider-graph">
          <p>Correction graph would appear here</p>
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

  return { render, start, stop, calibrate, applyConfig };
})();
