/**
 * PEC (Periodic Error Correction) Panel Component
 */
const PECComponent = (() => {
  'use strict';

  function render() {
    return `
      <div id="pec-panel" class="panel">
        <h2>⚙ PEC (Periodic Error Correction)</h2>
        <div class="pec-status">
          <p>Status: <span id="pec-enabled">Disabled</span></p>
          <p>Trained: <span id="pec-trained">No</span></p>
          <p>Peak Error: <span id="pec-peak">--"</span></p>
          <p>RMS Error: <span id="pec-rms">--"</span></p>
          <p>Harmonics: <span id="pec-harmonics">--</span></p>
          <label><input type="checkbox" id="pec-toggle" onchange="PECComponent.toggle()"> Enable PEC</label>
        </div>
        <div class="pec-training">
          <h3>Training</h3>
          <label>Worm Cycle: <input type="number" id="pec-worm-cycle" value="638"> s</label>
          <label>Harmonics: <input type="number" id="pec-harmonics-count" value="8" min="1" max="20"></label>
          <label>Duration: <input type="number" id="pec-duration" value="3" min="1" max="10"> cycles</label>
          <button onclick="PECComponent.startTraining()">▶ Start Training</button>
          <button onclick="PECComponent.stopTraining()">⏹ Stop Training</button>
          <div class="progress-bar"><div id="pec-progress" style="width:0%"></div></div>
        </div>
        <div class="pec-data">
          <h3>Data Management</h3>
          <button onclick="PECComponent.save()">💾 Save</button>
          <button onclick="PECComponent.load()">📂 Load</button>
        </div>
        <div id="pec-chart" class="chart-placeholder">Harmonics chart would appear here</div>
      </div>`;
  }

  function toggle() {
    const enabled = document.getElementById('pec-toggle').checked;
    Api.post('/api/pec/enable', { enabled });
  }

  function startTraining() {
    const worm = parseFloat(document.getElementById('pec-worm-cycle').value);
    const harm = parseInt(document.getElementById('pec-harmonics-count').value);
    const dur = parseInt(document.getElementById('pec-duration').value);
    Api.post('/api/pec/train/start', { worm_cycle_seconds: worm, num_harmonics: harm, duration_cycles: dur });
  }

  function stopTraining() { Api.post('/api/pec/train/stop', {}); }
  function save() { Api.post('/api/pec/save', {}); }
  function load() { Api.post('/api/pec/load', {}); }

  return { render, toggle, startTraining, stopTraining, save, load };
})();
