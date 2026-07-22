/**
 * Derotator Control Panel Component
 */
const DerotatorComponent = (() => {
  'use strict';

  function render() {
    return `
      <div id="derotator-panel" class="panel">
        <h2>🔄 Derotator</h2>
        <div class="derotator-mode">
          <h3>Mode</h3>
          <label><input type="radio" name="derotator-mode" value="0" checked onchange="DerotatorComponent.setMode(0)"> Disabled</label>
          <label><input type="radio" name="derotator-mode" value="1" onchange="DerotatorComponent.setMode(1)"> Auto</label>
          <label><input type="radio" name="derotator-mode" value="2" onchange="DerotatorComponent.setMode(2)"> Fixed Angle</label>
          <label><input type="radio" name="derotator-mode" value="3" onchange="DerotatorComponent.setMode(3)"> Manual Rate</label>
        </div>
        <div class="derotator-status">
          <p>Position: <span id="derot-pos">0.0°</span></p>
          <p>Rate: <span id="derot-rate">0.0 "/s</span></p>
          <p>Homed: <span id="derot-homed">No</span></p>
        </div>
        <div class="derotator-control">
          <label>Angle: <input type="number" id="derot-angle" value="0" step="0.1">°</label>
          <button onclick="DerotatorComponent.setAngle()">Set Angle</button>
          <label>Rate: <input type="number" id="derot-rate-input" value="0" step="0.1"> °/s</label>
          <button onclick="DerotatorComponent.setRate()">Set Rate</button>
          <button onclick="DerotatorComponent.home()">Home</button>
        </div>
        <div class="field-rotation">
          <h3>Field Rotation</h3>
          <p>Current Angle: <span id="fieldrot-angle">--°</span></p>
          <p>Rate: <span id="fieldrot-rate">-- "/s</span></p>
          <p>Predicted (10min): <span id="fieldrot-pred">--°</span></p>
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

  return { render, setMode, setAngle, setRate, home };
})();
