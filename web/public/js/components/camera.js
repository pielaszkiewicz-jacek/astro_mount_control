/**
 * Camera Control Panel Component
 */
const CameraComponent = (() => {
  'use strict';

  function render() {
    return `
      <div id="camera-panel" class="panel">
        <h2>📷 Camera</h2>
        <div class="camera-info" id="cam-info">
          <p>Model: <span id="cam-model">--</span></p>
          <p>Sensor: <span id="cam-sensor">--</span></p>
          <p>Chip: <span id="cam-chip">--</span></p>
          <p>Cooler: <span id="cam-cooler">--</span></p>
        </div>
        <div class="camera-exposure">
          <h3>Exposure</h3>
          <label>Time: <input type="number" id="cam-exposure" value="60" min="0.001" step="0.001"> s</label>
          <label>Gain: <input type="range" id="cam-gain" min="0" max="100" value="0"></label>
          <label>Binning: <select id="cam-binning"><option value="1">1x1</option><option value="2">2x2</option><option value="3">3x3</option></select></label>
          <label>Filter:
            <select id="cam-filter">
              <option value="0">L</option>
              <option value="1">R</option>
              <option value="2">G</option>
              <option value="3">B</option>
              <option value="4">Ha</option>
            </select>
          </label>
          <button onclick="CameraComponent.startExposure()">Start</button>
          <button onclick="CameraComponent.abort()">Abort</button>
          <div id="cam-progress" class="progress-bar"></div>
        </div>
        <div class="camera-cooler">
          <h3>Cooler</h3>
          <label>Target: <input type="number" id="cam-cooler-target" value="-10"> °C</label>
          <label>Current: <span id="cam-cooler-current">--°C</span></label>
          <label>Power: <span id="cam-cooler-power">--%</span></label>
          <button onclick="CameraComponent.setCooler()">Set</button>
        </div>
        <div id="cam-preview" class="camera-preview">
          <p>Preview image would appear here</p>
        </div>
      </div>`;
  }

  function startExposure() {
    const time = parseFloat(document.getElementById('cam-exposure').value);
    const gain = parseInt(document.getElementById('cam-gain').value);
    const binning = parseInt(document.getElementById('cam-binning').value);
    Api.post('/api/camera/expose', { exposure_time_s: time, gain, binning });
  }

  function abort() { Api.post('/api/camera/abort', {}); }
  function setCooler() {
    const target = parseFloat(document.getElementById('cam-cooler-target').value);
    Api.post('/api/camera/cooler', { target_c: target });
  }

  return { render, startExposure, abort, setCooler };
})();
