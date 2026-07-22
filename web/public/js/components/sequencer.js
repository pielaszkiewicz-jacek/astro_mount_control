/**
 * Observation Sequencer Panel Component
 */
const SequencerComponent = (() => {
  'use strict';

  function render() {
    return `
      <div id="sequencer-panel" class="panel">
        <h2>📋 Sequencer</h2>
        <div class="sequencer-controls">
          <button onclick="SequencerComponent.start()">▶ Start</button>
          <button onclick="SequencerComponent.stop()">⏹ Stop</button>
          <button onclick="SequencerComponent.pause()">⏸ Pause</button>
          <button onclick="SequencerComponent.resume()">▶ Resume</button>
        </div>
        <div class="sequencer-status">
          <p>State: <span id="seq-state">IDLE</span></p>
          <p>Target: <span id="seq-target">--</span> / <span id="seq-targets">--</span></p>
          <p>Exposure: <span id="seq-exposure">--</span> / <span id="seq-exposures">--</span></p>
          <div class="progress-bar"><div id="seq-progress" style="width:0%"></div></div>
        </div>
        <div class="sequencer-targets">
          <h3>Target List</h3>
          <div id="seq-target-list" class="target-list"></div>
          <div class="add-target">
            <label>Name: <input type="text" id="seq-new-name"></label>
            <label>RA: <input type="number" id="seq-new-ra" step="0.001"></label>
            <label>Dec: <input type="number" id="seq-new-dec" step="0.001"></label>
            <button onclick="SequencerComponent.addTarget()">Add Target</button>
          </div>
          <button onclick="SequencerComponent.loadPlan()">Load Plan</button>
        </div>
        <div class="sequencer-log">
          <h3>Session Log</h3>
          <div id="seq-log" class="log-list"></div>
        </div>
      </div>`;
  }

  function start() { Api.post('/api/sequencer/start', {}); }
  function stop() { Api.post('/api/sequencer/stop', {}); }
  function pause() { Api.post('/api/sequencer/pause', {}); }
  function resume() { Api.post('/api/sequencer/resume', {}); }

  function addTarget() {
    const name = document.getElementById('seq-new-name').value;
    const ra = parseFloat(document.getElementById('seq-new-ra').value);
    const dec = parseFloat(document.getElementById('seq-new-dec').value);
    if (name && !isNaN(ra) && !isNaN(dec)) {
      const list = document.getElementById('seq-target-list');
      list.innerHTML += `<div class="target-entry">${name} (RA=${ra}, Dec=${dec})</div>`;
    }
  }

  function loadPlan() {
    Api.post('/api/sequencer/load', {}).then(r => {
      if (r.success) alert('Plan loaded');
    });
  }

  return { render, start, stop, pause, resume, addTarget, loadPlan };
})();
