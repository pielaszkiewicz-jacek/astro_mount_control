/**
 * Dome Control Panel Component
 */
const DomeComponent = (() => {
  'use strict';

  function render() {
    return `
      <div id="dome-panel" class="panel">
        <h2>🏠 Dome</h2>
        <div class="dome-status">
          <p>State: <span id="dome-state">Unknown</span></p>
          <p>Azimuth: <span id="dome-azimuth">--°</span></p>
          <p>Shutter: <span id="dome-shutter">--</span></p>
        </div>
        <div class="dome-controls">
          <button onclick="DomeComponent.open()">Open Shutter</button>
          <button onclick="DomeComponent.close()">Close Shutter</button>
          <button onclick="DomeComponent.park()">Park</button>
        </div>
        <div class="dome-rotate">
          <h3>Rotate</h3>
          <label>Target Azimuth: <input type="number" id="dome-target-az" value="180" min="0" max="360">°</label>
          <button onclick="DomeComponent.rotate()">Rotate To</button>
        </div>
        <div class="dome-sync">
          <h3>Mount Sync</h3>
          <label><input type="checkbox" id="dome-sync-enabled" onchange="DomeComponent.toggleSync()"> Auto-sync with Mount</label>
          <label>Offset: <input type="number" id="dome-sync-offset" value="180">°</label>
        </div>
      </div>`;
  }

  function open() { Api.post('/api/dome/open', {}); }
  function close() { Api.post('/api/dome/close', {}); }
  function park() { Api.post('/api/dome/park', {}); }
  function rotate() {
    const az = parseFloat(document.getElementById('dome-target-az').value);
    Api.post('/api/dome/rotate', { azimuth_deg: az });
  }
  function toggleSync() {
    const enabled = document.getElementById('dome-sync-enabled').checked;
    Api.post('/api/dome/sync', { enabled });
  }

  return { render, open, close, park, rotate, toggleSync };
})();
