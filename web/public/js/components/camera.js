/**
 * Camera Control Panel Component
 *
 * Unified card-based style matching the existing UI.
 */
const CameraComponent = (() => {
  'use strict';

  function render() {
    return `
      <div class="camera-dashboard">
        <h3 style="margin:0 0 8px; font-size:0.9rem; color:var(--color-text-secondary);">Camera Info</h3>
        <div class="sensor-grid">
          <div class="sensor-card">
            <span class="sensor-label">Model</span>
            <span class="sensor-value" id="cam-model">--</span>
          </div>
          <div class="sensor-card">
            <span class="sensor-label">Sensor</span>
            <span class="sensor-value" id="cam-sensor">--</span>
          </div>
          <div class="sensor-card">
            <span class="sensor-label">Chip</span>
            <span class="sensor-value" id="cam-chip">--</span>
          </div>
          <div class="sensor-card">
            <span class="sensor-label">Cooler</span>
            <span class="sensor-value" id="cam-cooler">--</span>
          </div>
        </div>

        <h3 style="margin:16px 0 8px; font-size:0.9rem; color:var(--color-text-secondary);">Camera Device</h3>
        <div class="control-form">
          <div class="form-row" style="gap:8px; align-items:center;">
            <select id="cam-device" class="form-input form-select" style="flex:1;" onchange="CameraComponent.selectDevice()">
              <option value="">— Wybierz kamerę —</option>
            </select>
            <button type="button" class="btn btn-secondary" onclick="CameraComponent.refreshDevices()">Refresh</button>
          </div>
          <p id="cam-device-status" style="margin:6px 0 0; font-size:0.8rem; color:var(--color-text-muted);"></p>
        </div>

        <h3 style="margin:16px 0 8px; font-size:0.9rem; color:var(--color-text-secondary);">Exposure</h3>
        <div class="control-form">
          <div class="form-row">
            <div class="form-group">
              <label for="cam-exposure" class="form-label">Time (s)</label>
              <input type="number" id="cam-exposure" class="form-input" value="60" min="0.001" step="0.001" style="width:120px;">
            </div>
            <div class="form-group">
              <label for="cam-gain" class="form-label">Gain: <span id="cam-gain-label">0</span></label>
              <input type="range" id="cam-gain" class="speed-slider" min="0" max="100" value="0"
                     oninput="document.getElementById('cam-gain-label').textContent=this.value">
            </div>
            <div class="form-group">
              <label for="cam-binning" class="form-label">Binning</label>
              <select id="cam-binning" class="form-input form-select" style="width:80px;">
                <option value="1">1x1</option>
                <option value="2">2x2</option>
                <option value="3">3x3</option>
              </select>
            </div>
            <div class="form-group">
              <label for="cam-filter" class="form-label">Filter</label>
              <select id="cam-filter" class="form-input form-select" style="width:80px;">
                <option value="0">L</option>
                <option value="1">R</option>
                <option value="2">G</option>
                <option value="3">B</option>
                <option value="4">Ha</option>
              </select>
            </div>
          </div>
          <div class="action-grid" style="grid-template-columns:1fr 1fr;">
            <button class="btn btn-primary" onclick="CameraComponent.startExposure()">▶ Start Exposure</button>
            <button class="btn btn-danger" onclick="CameraComponent.abort()">⏹ Abort</button>
          </div>
          <div class="progress-bar" style="margin-top:8px;">
            <div id="cam-progress" style="width:0%; height:100%; background:var(--color-primary); border-radius:4px; transition:width 0.3s ease;"></div>
          </div>
          <div id="cam-progress-label" style="margin-top:4px; font-size:0.8rem; text-align:right; color:var(--color-text-muted);">0%</div>
        </div>

        <h3 style="margin:16px 0 8px; font-size:0.9rem; color:var(--color-text-secondary);">Cooler</h3>
        <div class="control-form">
          <div class="form-row">
            <div class="form-group">
              <label for="cam-cooler-target" class="form-label">Target Temperature (°C)</label>
              <input type="number" id="cam-cooler-target" class="form-input" value="-10" style="width:120px;">
            </div>
            <div class="form-group">
              <span class="form-label">Current</span>
              <span id="cam-cooler-current" style="font-weight:600;">--°C</span>
            </div>
            <div class="form-group">
              <span class="form-label">Cooler Power</span>
              <span id="cam-cooler-power" style="font-weight:600;">--%</span>
            </div>
            <div class="form-group" style="align-self:flex-end;">
              <button class="btn btn-secondary" onclick="CameraComponent.setCooler()">Set Cooler</button>
            </div>
          </div>
        </div>

        <div style="display:flex; align-items:center; justify-content:space-between; margin:16px 0 8px;">
          <h3 style="margin:0; font-size:0.9rem; color:var(--color-text-secondary);">Camera Properties</h3>
          <button type="button" class="btn btn-secondary" style="font-size:0.75rem;" onclick="CameraComponent.refreshProperties()">Refresh</button>
        </div>

        <details class="form-section" id="cam-config-section" style="margin-top:16px;">
          <summary>⚙️ Konfiguracja kamery</summary>
          <div class="form-section-body">
            <div id="cam-properties" class="control-form">
              <p style="margin:0; font-size:0.8rem; color:var(--color-text-muted);">Wybierz kamerę INDI, aby zobaczyć jej właściwości.</p>
            </div>
          </div>
        </details>

        <div id="cam-preview" class="chart-placeholder" style="margin-top:16px; padding:40px; text-align:center; color:var(--color-text-muted); border:1px dashed var(--color-border); border-radius:8px;">
          📷 Preview image will appear here
        </div>
      </div>`;
  }

  async function startExposure() {
    const time = parseFloat(document.getElementById('cam-exposure').value);
    const gain = parseInt(document.getElementById('cam-gain').value);
    const binning = parseInt(document.getElementById('cam-binning').value);
    const filter = parseInt(document.getElementById('cam-filter').value);

    App.showToast('Ekspozycja trwa... (' + time + ' s)', 'info', 3000);

    // Time-based progress indicator. The proxy blocks until the exposure
    // stream completes, so the UI animates the bar from the requested duration.
    const progressEl = document.getElementById('cam-progress');
    const progressLabel = document.getElementById('cam-progress-label');
    const startedAt = Date.now();
    const progressTimer = setInterval(() => {
      const pct = Math.min(100, ((Date.now() - startedAt) / (time * 1000)) * 100);
      if (progressEl) progressEl.style.width = pct + '%';
      if (progressLabel) progressLabel.textContent = Math.round(pct) + '%';
    }, 200);

    try {
      // The proxy blocks until the exposure stream completes, so give it a
      // generous timeout (exposure duration + 30 s buffer).
      const result = await Api.post('/api/camera/expose', {
        exposure_time_s: time,
        gain,
        binning,
        filter: { position: filter }
      }, { timeout: Math.max(60000, time * 1000 + 30000) });

      if (result.image && result.image.data_base64) {
        const filename = downloadImage(result.image);
        App.showToast('Obraz pobrany (' + (result.image.size_bytes || 0) + ' bajtów)', 'success');
        const preview = document.getElementById('cam-preview');
        if (preview) {
          if (result.image.thumbnail_base64) {
            preview.style.padding = '8px';
            preview.innerHTML = '';
            const img = document.createElement('img');
            img.src = 'data:image/' + (result.image.thumbnail_format || 'png') + ';base64,' + result.image.thumbnail_base64;
            img.alt = filename;
            img.style.maxWidth = '100%';
            img.style.height = 'auto';
            img.style.borderRadius = '6px';
            img.style.display = 'block';
            preview.appendChild(img);
            const caption = document.createElement('div');
            caption.style.marginTop = '8px';
            caption.style.fontSize = '0.8rem';
            caption.textContent = '📷 Zapisano: ' + filename + ' (' + (result.image.size_bytes || 0) + ' bajtów)';
            preview.appendChild(caption);
          } else {
            preview.style.padding = '12px';
            preview.textContent = '📷 Zapisano: ' + filename + ' (' + (result.image.size_bytes || 0) + ' bajtów)';
          }
        }
      } else {
        const msg = result.message || 'Ekspozycja zakończona (brak obrazu)';
        App.showToast(msg, result.success === false ? 'info' : 'success');
      }
    } catch (err) {
      App.showToast('Błąd ekspozycji: ' + err.message, 'error');
    } finally {
      clearInterval(progressTimer);
      if (progressEl) progressEl.style.width = '100%';
      if (progressLabel) progressLabel.textContent = '100%';
    }
  }

  function downloadImage(image) {
    const binary = atob(image.data_base64);
    const bytes = new Uint8Array(binary.length);
    for (let i = 0; i < binary.length; i++) bytes[i] = binary.charCodeAt(i);

    const ext = (image.format === 'fits') ? 'fits' : (image.format || 'fits');
    const filename = 'exposure.' + ext;
    const blob = new Blob([bytes], { type: 'application/octet-stream' });
    const url = URL.createObjectURL(blob);
    const a = document.createElement('a');
    a.href = url;
    a.download = filename;
    document.body.appendChild(a);
    a.click();
    a.remove();
    setTimeout(() => URL.revokeObjectURL(url), 1000);
    return filename;
  }

  function abort() { Api.post('/api/camera/abort', {}); }
  function setCooler() {
    const target = parseFloat(document.getElementById('cam-cooler-target').value);
    Api.post('/api/camera/cooler', { target_c: target, enabled: true })
      .then(refreshCooler);
  }

  function refreshCooler() {
    Api.get('/api/camera/cooler').then(data => {
      const cur = document.getElementById('cam-cooler-current');
      if (cur) cur.textContent = (data.current_c || 0).toFixed(1) + '°C';
      const pwr = document.getElementById('cam-cooler-power');
      if (pwr) pwr.textContent = (data.power_percent || 0).toFixed(0) + '%';
    }).catch(() => { /* cooler status is best-effort */ });
  }

  function refreshInfo() {
    Api.get('/api/camera/info').then(data => {
      document.getElementById('cam-model').textContent = data.name || '--';
      document.getElementById('cam-sensor').textContent = data.sensor_name || '--';
      document.getElementById('cam-chip').textContent = (data.width || '--') + '×' + (data.height || '--');
      document.getElementById('cam-cooler').textContent = data.has_cooler ? '✅ Yes' : 'No';
    }).catch(() => App.showServiceUnavailable('panel-camera', 'Camera service'));
    refreshCooler();
    refreshDevices();
    refreshProperties();
  }

  const HTML_AMP = '&' + 'amp;';
  const HTML_LT = '&' + 'lt;';
  const HTML_GT = '&' + 'gt;';
  const HTML_QUOT = '&' + 'quot;';
  const HTML_APOS = '&' + '#39;';

  function escapeHtml(str) {
    return String(str == null ? '' : str)
      .replace(/&/g, HTML_AMP)
      .replace(/</g, HTML_LT)
      .replace(/>/g, HTML_GT)
      .replace(/"/g, HTML_QUOT)
      .replace(/'/g, HTML_APOS);
  }

  function refreshProperties() {
    const container = document.getElementById('cam-properties');
    if (!container) return;

    Api.get('/api/camera/properties').then(data => {
      const props = (data.properties || []).filter(p => p.type);
      if (!props.length) {
        container.innerHTML = '<p style="margin:0; font-size:0.8rem; color:var(--color-text-muted);">Brak właściwości (wybierz kamerę INDI).</p>';
        return;
      }

      let html = '';
      props.forEach(p => {
        const disabled = p.writable ? '' : 'disabled';
        if (p.type === 'switch') {
          const selected = (p.elements || []).find(e => e.switch_state === 'ON');
          html += `<div class="form-row" style="gap:8px; align-items:center; margin-bottom:6px;">
            <label style="flex:1; font-size:0.8rem;">${escapeHtml(p.label)}${p.writable ? '' : ' (RO)'}</label>
            <select class="form-input form-select" style="width:160px;" ${disabled} data-prop="${escapeHtml(p.name)}" data-elem="${escapeHtml(selected ? selected.name : '')}" data-type="switch">
              ${(p.elements || []).map(e => `<option value="${escapeHtml(e.name)}" ${e.switch_state === 'ON' ? 'selected' : ''}>${escapeHtml(e.label || e.name)}</option>`).join('')}
            </select>
          </div>`;
        } else {
          (p.elements || []).forEach(e => {
            if (p.type === 'number') {
              html += `<div class="form-row" style="gap:8px; align-items:center; margin-bottom:6px;">
                <label style="flex:1; font-size:0.8rem;">${escapeHtml(p.label)}${p.elements.length > 1 ? ' / ' + escapeHtml(e.label) : ''}${p.writable ? '' : ' (RO)'}</label>
                <input type="number" class="form-input" style="width:160px;" ${disabled} data-prop="${escapeHtml(p.name)}" data-elem="${escapeHtml(e.name)}" data-type="number"
                       value="${e.number_value}" min="${e.number_min}" max="${e.number_max}" step="${e.number_step || 1}">
              </div>`;
            } else if (p.type === 'text') {
              html += `<div class="form-row" style="gap:8px; align-items:center; margin-bottom:6px;">
                <label style="flex:1; font-size:0.8rem;">${escapeHtml(p.label)}${p.writable ? '' : ' (RO)'}</label>
                <input type="text" class="form-input" style="flex:1;" ${disabled} data-prop="${escapeHtml(p.name)}" data-elem="${escapeHtml(e.name)}" data-type="text" value="${escapeHtml(e.text_value)}">
              </div>`;
            }
          });
        }
      });

      container.innerHTML = html;
      container.querySelectorAll('[data-prop]').forEach(el => {
        el.addEventListener('change', () => {
          if (el.disabled) return;
          setProperty(el.dataset.prop, el.dataset.elem, el.value);
        });
      });
    }).catch(() => {
      container.innerHTML = '<p style="margin:0; font-size:0.8rem; color:var(--color-text-muted);">Brak połączenia z serwerem INDI kamery.</p>';
    });
  }

  function setProperty(prop, elem, value) {
    Api.post('/api/camera/property', { property: prop, element: elem, value: String(value) })
      .then(() => App.showToast(`Ustawiono ${prop}.${elem}`, 'success', 1200))
      .catch(err => App.showToast('Błąd ustawiania właściwości: ' + err.message, 'error'));
  }

  function refreshDevices() {
    const select = document.getElementById('cam-device');
    const status = document.getElementById('cam-device-status');
    if (!select) return;

    Api.get('/api/camera/devices').then(data => {
      const current = select.value || data.active_device || '';
      select.innerHTML = '<option value="">— Wybierz kamerę —</option>';
      (data.devices || []).forEach(d => {
        const opt = document.createElement('option');
        opt.value = d.name;
        opt.textContent = d.label || d.name;
        select.appendChild(opt);
      });
      select.value = current || data.active_device || '';
      if (status) {
        status.textContent = data.active_device
          ? `Aktywna kamera: ${data.active_device}`
          : 'Brak aktywnej kamery (wybierz kamerę INDI)';
      }
      refreshProperties();
    }).catch(() => {
      if (status) status.textContent = 'Brak połączenia z serwerem INDI kamery';
    });
  }

  function selectDevice() {
    const select = document.getElementById('cam-device');
    const status = document.getElementById('cam-device-status');
    if (!select) return;

    Api.post('/api/camera/select', { device: select.value })
      .then(() => {
        if (status) status.textContent = select.value
          ? `Wybrano kamerę: ${select.value}`
          : 'Brak wybranej kamery';
        refreshInfo();
        // INDI properties arrive asynchronously after the device connects —
        // refresh a few times so the full list becomes visible.
        setTimeout(refreshProperties, 600);
        setTimeout(refreshProperties, 1600);
      })
      .catch(err => {
        if (status) status.textContent = 'Błąd wyboru kamery: ' + err.message;
      });
  }

  return { render, startExposure, abort, setCooler, refreshInfo, refreshDevices, selectDevice };
})();
