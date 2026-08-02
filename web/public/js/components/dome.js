/**
 * Dome Control Panel Component
 *
 * Unified card-based style matching the existing UI.
 */
const DomeComponent = (() => {
  'use strict';

  function render() {
    return `
      <div class="dome-dashboard">
        <div class="sensor-grid">
          <div class="sensor-card">
            <span class="sensor-label">State</span>
            <span class="sensor-value" id="dome-state">Unknown</span>
          </div>
          <div class="sensor-card">
            <span class="sensor-label">Azimuth</span>
            <span class="sensor-value" id="dome-azimuth">--°</span>
          </div>
          <div class="sensor-card">
            <span class="sensor-label">Shutter</span>
            <span class="sensor-value" id="dome-shutter">--</span>
          </div>
          <div class="sensor-card">
            <span class="sensor-label">Parked</span>
            <span class="sensor-value" id="dome-parked">--</span>
          </div>
        </div>

        <div class="action-grid" style="margin-top:12px;">
          <button class="btn btn-primary" onclick="DomeComponent.open()">🔓 Open Shutter</button>
          <button class="btn btn-danger" onclick="DomeComponent.close()">🔒 Close Shutter</button>
          <button class="btn btn-secondary" onclick="DomeComponent.park()">🏠 Park</button>
        </div>

        <h3 style="margin:16px 0 8px; font-size:0.9rem; color:var(--color-text-secondary);">Rotation</h3>
        <div class="control-form">
          <div class="form-row">
            <div class="form-group">
              <label for="dome-target-az" class="form-label">Target Azimuth (°)</label>
              <input type="number" id="dome-target-az" class="form-input" value="180" min="0" max="360" style="width:120px;">
            </div>
            <div class="form-group" style="align-self:flex-end;">
              <button class="btn btn-primary" onclick="DomeComponent.rotate()">Rotate To</button>
            </div>
          </div>
        </div>

        <h3 style="margin:16px 0 8px; font-size:0.9rem; color:var(--color-text-secondary);">Mount Sync</h3>
        <div class="control-form">
          <label class="checkbox-label" style="gap:8px;">
            <input type="checkbox" id="dome-sync-enabled" onchange="DomeComponent.toggleSync()">
            Auto-sync with Mount
          </label>
          <div class="form-row" style="margin-top:8px;">
            <div class="form-group">
              <label for="dome-sync-offset" class="form-label">Sync Offset (°)</label>
              <input type="number" id="dome-sync-offset" class="form-input" value="180" style="width:100px;">
            </div>
          </div>
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

  function refreshStatus() {
    Api.get('/api/dome/status').then(data => {
      const stateNames = ['Closed', 'Opening', 'Open', 'Closing', 'Rotating', 'Parked', 'Error', 'Unknown'];
      document.getElementById('dome-state').textContent = stateNames[data.state] || 'Unknown';
      document.getElementById('dome-azimuth').textContent = (data.azimuth_deg || 0).toFixed(1) + '°';
      document.getElementById('dome-shutter').textContent = data.shutter_open ? '✅ Open' : 'Closed';
      document.getElementById('dome-parked').textContent = data.parked ? 'Yes' : 'No';
    }).catch(() => {});
  }

  return { render, open, close, park, rotate, toggleSync, refreshStatus };
})();
