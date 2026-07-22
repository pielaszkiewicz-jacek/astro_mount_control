/**
 * Focuser Control Panel Component
 */
const FocuserComponent = (() => {
  'use strict';

  function render() {
    return `
      <div id="focuser-panel" class="panel">
        <h2>🔭 Focuser</h2>
        <div class="focuser-control">
          <div class="focuser-status">
            <p>Position: <span id="foc-position">0</span> / <span id="foc-max">100000</span></p>
            <p>Temperature: <span id="foc-temp">--°C</span></p>
            <p>HFD: <span id="foc-hfd">--</span></p>
          </div>
          <div class="focuser-move">
            <label>Target: <input type="number" id="foc-target" value="50000" min="0"></label>
            <label>Speed: <input type="range" id="foc-speed" min="1" max="100" value="50"></label>
            <button onclick="FocuserComponent.move()">Move</button>
            <button onclick="FocuserComponent.halt()">Halt</button>
          </div>
          <div class="focuser-autofocus">
            <h3>Auto-Focus</h3>
            <label>Start: <input type="number" id="foc-af-start" value="40000"></label>
            <label>End: <input type="number" id="foc-af-end" value="60000"></label>
            <label>Step: <input type="number" id="foc-af-step" value="500"></label>
            <button onclick="FocuserComponent.autoFocus()">Run Auto-Focus</button>
            <div id="foc-af-progress"></div>
          </div>
          <div id="foc-curve" class="chart-placeholder">Focus Curve Chart</div>
        </div>
      </div>`;
  }

  function move() {
    const pos = parseInt(document.getElementById('foc-target').value);
    Api.post('/api/focuser/move', { position: pos });
  }

  function halt() {
    Api.post('/api/focuser/halt', {});
  }

  function autoFocus() {
    const start = parseInt(document.getElementById('foc-af-start').value);
    const end = parseInt(document.getElementById('foc-af-end').value);
    const step = parseInt(document.getElementById('foc-af-step').value);
    Api.post('/api/focuser/autofocus', { start_position: start, end_position: end, step_size: step });
  }

  return { render, move, halt, autoFocus };
})();
