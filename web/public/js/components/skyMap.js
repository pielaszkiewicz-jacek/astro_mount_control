/**
 * Astronomical Mount Controller - Sky Map Component (d3-celestial)
 *
 * Interactive sky map using d3-celestial. The object source is selectable
 * between the controller's local database and d3-celestial's built-in data.
 * Features: click-to-inspect popup with slew/track actions, type filtering,
 * object search, a telescope position indicator (shown only when calibrated),
 * one-click centring on the telescope, and a maximise (fullscreen) toggle.
 *
 * View modes map onto d3-celestial projections:
 *   - "equatorial" (default): full-sky equirectangular RA/Dec chart,
 *   - "polar":     orthographic view centred on the north celestial pole,
 *   - "horizontal": orthographic view centred on the local zenith.
 */

const SkyMapComponent = (() => {
  'use strict';

  const CONTAINER_ID = 'skymap-container';
  const DATAPATH = 'vendor/d3-celestial/data/';
  const MAX_MAG = 16;
  const PAGE_SIZE = 30000;
  const MAX_OBJECTS = 180000;
  const MOUNT_POLL_MS = 2000;

  // Colors for database objects by type.
  const TYPE_COLORS = {
    UNKNOWN_TYPE:         '#b0b0b0',
    STAR:                 '#f5f5f5',
    DOUBLE_STAR:          '#e6eeff',
    VARIABLE_STAR:        '#ffab91',
    STAR_CLUSTER_OPEN:    '#ffb74d',
    STAR_CLUSTER_GLOBULAR:'#ffd54f',
    PLANETARY_NEBULA:     '#4dd0e1',
    DIFFUSE_NEBULA:       '#66bb6a',
    DARK_NEBULA:          '#78909c',
    EMISSION_NEBULA:      '#8bc34a',
    REFLECTION_NEBULA:    '#80cbc4',
    GALAXY_SPIRAL:        '#4fc3f7',
    GALAXY_ELLIPTICAL:    '#82b1ff',
    GALAXY_IRREGULAR:     '#40c4ff',
    GALAXY_LENTICULAR:    '#b39ddb',
    QUASAR:               '#e040fb',
    SUPERNOVA_REMNANT:    '#e57373',
    PLANET:               '#ff7043',
    DWARF_PLANET:         '#ff8a65',
    MOON:                 '#eeeeee',
    ASTEROID:             '#a1887f',
    COMET:                '#4db6ac',
    SATELLITE:            '#90a4ae',
    EXOPLANET:            '#ff8a65',
    ARTIFICIAL_SATELLITE: '#90a4ae',
    SPACE_DEBRIS:         '#78909c',
  };
  const DEFAULT_COLOR = '#b0b0b0';

  // Friendly labels for database object types.
  const TYPE_LABELS = {
    UNKNOWN_TYPE: 'Unknown',
    STAR: 'Star',
    DOUBLE_STAR: 'Double Star',
    VARIABLE_STAR: 'Variable Star',
    STAR_CLUSTER_OPEN: 'Open Cluster',
    STAR_CLUSTER_GLOBULAR: 'Globular Cluster',
    PLANETARY_NEBULA: 'Planetary Nebula',
    DIFFUSE_NEBULA: 'Diffuse Nebula',
    DARK_NEBULA: 'Dark Nebula',
    EMISSION_NEBULA: 'Emission Nebula',
    REFLECTION_NEBULA: 'Reflection Nebula',
    GALAXY_SPIRAL: 'Spiral Galaxy',
    GALAXY_ELLIPTICAL: 'Elliptical Galaxy',
    GALAXY_IRREGULAR: 'Irregular Galaxy',
    GALAXY_LENTICULAR: 'Lenticular Galaxy',
    QUASAR: 'Quasar',
    SUPERNOVA_REMNANT: 'Supernova Remnant',
    PLANET: 'Planet',
    DWARF_PLANET: 'Dwarf Planet',
    MOON: 'Moon',
    ASTEROID: 'Asteroid',
    COMET: 'Comet',
    SATELLITE: 'Satellite',
    EXOPLANET: 'Exoplanet',
    ARTIFICIAL_SATELLITE: 'Artificial Satellite',
    SPACE_DEBRIS: 'Space Debris',
  };

  // Friendly labels for d3-celestial DSO type codes.
  const DSO_TYPE_LABELS = {
    gg: 'Galaxy Cluster', g: 'Galaxy', s: 'Spiral Galaxy', s0: 'Lenticular Galaxy',
    sd: 'Dwarf Galaxy', e: 'Elliptical Galaxy', i: 'Irregular Galaxy',
    oc: 'Open Cluster', gc: 'Globular Cluster', en: 'Emission Nebula',
    bn: 'Bright Nebula', sfr: 'Star Forming Region', rn: 'Reflection Nebula',
    pn: 'Planetary Nebula', snr: 'Supernova Remnant', dn: 'Dark Nebula', pos: 'Marker',
  };

  // Type filter categories (checkboxes shown in the Types dropdown).
  const TYPE_CATEGORIES = [
    { key: 'star', label: 'Stars', types: ['STAR'] },
    { key: 'double_star', label: 'Double Stars', types: ['DOUBLE_STAR'] },
    { key: 'variable_star', label: 'Variable Stars', types: ['VARIABLE_STAR'] },
    { key: 'open_cluster', label: 'Open Clusters', types: ['STAR_CLUSTER_OPEN'] },
    { key: 'globular_cluster', label: 'Globular Clusters', types: ['STAR_CLUSTER_GLOBULAR'] },
    { key: 'nebula', label: 'Nebulae & Remnants', types: ['PLANETARY_NEBULA', 'DIFFUSE_NEBULA', 'DARK_NEBULA', 'EMISSION_NEBULA', 'REFLECTION_NEBULA', 'SUPERNOVA_REMNANT'] },
    { key: 'galaxy', label: 'Galaxies & Quasars', types: ['GALAXY_SPIRAL', 'GALAXY_ELLIPTICAL', 'GALAXY_IRREGULAR', 'GALAXY_LENTICULAR', 'QUASAR'] },
    { key: 'solar_system', label: 'Solar System', types: ['PLANET', 'DWARF_PLANET', 'MOON', 'ASTEROID', 'COMET', 'EXOPLANET'] },
    { key: 'other', label: 'Other', types: ['SATELLITE', 'ARTIFICIAL_SATELLITE', 'SPACE_DEBRIS', 'UNKNOWN_TYPE'] },
  ];

  // ─── State ────────────────────────────────────────────────────────────
  let containerEl = null;
  let panelEl = null;
  let popupEl = null;
  let sourceEl = null;
  let searchInputEl = null;
  let searchBtnEl = null;
  let typesToggleEl = null;
  let typesMenuEl = null;
  let centerMountBtnEl = null;
  let maximizeBtnEl = null;
  let refreshBtnEl = null;
  let zoomInBtnEl = null;
  let zoomOutBtnEl = null;
  let zoomResetBtnEl = null;
  let legendEl = null;

  let starNames = {};
  let objects = [];
  let displayObjects = [];
  let initialized = false;
  let mapCreated = false;
  let pointerDownPos = null;
  let selectedObject = null;
  let mountTimer = null;

  let viewMode = 'equatorial';
  let source = 'database';
  let location = { latitude: 52.0, longitude: 21.0 };
  let mountRaHours = null;
  let mountDecDeg = null;
  let mountCalibrated = false;

  const visibleCategories = new Set(TYPE_CATEGORIES.map((c) => c.key));

  // ─── Astronomy helpers ────────────────────────────────────────────────
  function currentJulianDate() { return Date.now() / 86400000.0 + 2440587.5; }
  function gmstHours(jd) {
    const d = jd - 2451545.0;
    const g = 18.697374558 + 24.06570982441908 * d;
    return ((g % 24) + 24) % 24;
  }
  function lstHours(jd, longitudeDeg) {
    return ((gmstHours(jd) + longitudeDeg / 15.0) % 24 + 24) % 24;
  }

  // ─── Data loading ─────────────────────────────────────────────────────
  async function loadStarNames() {
    try {
      const res = await fetch(DATAPATH + 'starnames.json');
      starNames = await res.json();
    } catch (err) {
      console.warn('[SkyMap] Failed to load star names:', err);
    }
  }

  async function loadObjects() {
    try {
      let all = [];
      let page = 1;
      while (all.length < MAX_OBJECTS) {
        const result = await Api.listObjects({ page, pageSize: PAGE_SIZE, maxMagnitude: MAX_MAG });
        const batch = Array.isArray(result && result.objects) ? result.objects : [];
        if (batch.length === 0) break;
        all = all.concat(batch);
        const total = Number(result && result.total_count);
        if (Number.isFinite(total) && all.length >= total) break;
        if (batch.length < PAGE_SIZE) break;
        page += 1;
      }
      objects = all;
      displayObjects = all;
    } catch (err) {
      console.warn('[SkyMap] Failed to load database objects:', err);
    }
    redrawMap();
  }

  async function loadMountState() {
    try {
      const status = await Api.getStatus();
      if (status && status.location) {
        location = {
          latitude: Number(status.location.latitude) || 0,
          longitude: Number(status.location.longitude) || 0,
        };
      }
      mountRaHours = (typeof status.current_ra === 'number') ? status.current_ra : null;
      mountDecDeg = (typeof status.current_dec === 'number') ? status.current_dec : null;
      mountCalibrated = !!(status.tpointCalibrated || (status.bootstrap && status.bootstrap.calibrated));
    } catch (err) {
      console.warn('[SkyMap] Failed to read mount status:', err);
    }
    updateCenterButton();
    redrawMap();
  }

  function startMountPolling() {
    loadMountState();
    mountTimer = setInterval(loadMountState, MOUNT_POLL_MS);
  }

  // ─── d3-celestial configuration ───────────────────────────────────────
  function projectionForMode() {
    return viewMode === 'equatorial' ? 'equirectangular' : 'orthographic';
  }
  function centerForMode() {
    if (viewMode === 'polar') return [0, 90, 0];
    if (viewMode === 'horizontal') {
      const lst = lstHours(currentJulianDate(), location.longitude);
      return [lst * 15.0, location.latitude, 0];
    }
    return [0, 0, 0];
  }

  function buildConfig() {
    const libraryObjects = source === 'library';
    return {
      container: CONTAINER_ID,
      width: 0,
      datapath: DATAPATH,
      projection: projectionForMode(),
      projectionRatio: null,
      transform: 'equatorial',
      center: centerForMode(),
      interactive: true,
      controls: false,
      disableAnimations: true,
      zoomlevel: 1,
      zoomextend: 12,
      adaptable: true,
      orientationfixed: true,
      form: false,
      location: false,
      settimezone: false,
      lang: '',
      culture: 'iau',
      background: { fill: '#0a0c18', stroke: '#182138', opacity: 1, width: 1 },
      stars: {
        show: libraryObjects, limit: 7, colors: true,
        style: { fill: '#ffffff', opacity: 1 }, size: 7, exponent: -0.28,
        propername: true, propernameType: 'name', propernameLimit: 1.5,
        designation: false, data: 'stars.6.json',
      },
      dsos: {
        show: libraryObjects, limit: 8, colors: true,
        names: true, namesType: 'desig', nameLimit: 6,
        size: null, exponent: 1.4, data: 'dsos.bright.json',
      },
      constellations: {
        show: true, names: true, namesType: 'desig',
        nameStyle: {
          fill: '#9aa7c9', align: 'center', baseline: 'middle', opacity: 0.9,
          font: ['13px DejaVu Sans, sans-serif', '12px DejaVu Sans, sans-serif', '11px DejaVu Sans, sans-serif'],
        },
        lines: true,
        lineStyle: { stroke: '#8aa0cc', width: 1.6, opacity: 0.85 },
        bounds: true,
        boundStyle: { stroke: '#5a6e9e', width: 1.0, opacity: 0.7, dash: [4, 4] },
      },
      mw: { show: true, style: { fill: '#8aa0cc', opacity: 0.10 } },
      lines: {
        graticule: {
          show: true, stroke: '#26304a', width: 0.6, opacity: 0.9,
          lon: { pos: [], fill: '#6a7896', font: '10px DejaVu Sans, sans-serif' },
          lat: { pos: [], fill: '#6a7896', font: '10px DejaVu Sans, sans-serif' },
        },
        equatorial: { show: true, stroke: '#26304a', width: 1.1, opacity: 0.55 },
        ecliptic: { show: true, stroke: '#3c5a3c', width: 1.1, opacity: 0.55 },
        galactic: { show: false },
        supergalactic: { show: false },
      },
      planets: { show: false },
      horizon: { show: false },
      daylight: { show: false },
    };
  }

  // ─── Type filter helpers ──────────────────────────────────────────────
  function typeCategoryOf(type) {
    const t = (type || '').toUpperCase();
    for (const cat of TYPE_CATEGORIES) {
      if (cat.types.indexOf(t) !== -1) return cat.key;
    }
    return 'other';
  }
  function isTypeVisible(type) {
    return visibleCategories.has(typeCategoryOf(type));
  }

  // ─── Custom database-object overlay + mount marker ────────────────────
  function registerLayers() {
    Celestial.add({ type: 'raw', callback: function () {}, redraw: drawCustomLayers });
  }
  function toCoord(raHours, decDeg) {
    return [((raHours * 15) % 360 + 360) % 360, decDeg];
  }
  function typeColor(type) {
    return TYPE_COLORS[(type || '').toUpperCase()] || DEFAULT_COLOR;
  }
  function typeLabel(type) {
    return TYPE_LABELS[(type || '').toUpperCase()] || (type || 'Object');
  }
  function dsoTypeLabel(code) {
    return DSO_TYPE_LABELS[code] || (code || 'Deep-Sky Object');
  }
  function currentZoom() {
    if (typeof Celestial !== 'undefined' && typeof Celestial.zoomBy === 'function') {
      const z = Celestial.zoomBy();
      if (Number.isFinite(z) && z > 0) return z;
    }
    return 1;
  }
  function magnitudeForZoom(z) {
    if (z <= 1) return 6;
    if (z <= 1.5) return 8;
    if (z <= 2.5) return 10;
    if (z <= 4) return 12;
    if (z <= 7) return 14;
    return 16;
  }

  function drawCustomLayers() {
    if (typeof Celestial === 'undefined' || !Celestial.context || !Celestial.map) return;
    const ctx = Celestial.context;
    const proj = Celestial.map.projection();
    const clip = (typeof Celestial.clip === 'function') ? Celestial.clip : function () { return 1; };
    const zoom = currentZoom();
    const magLimit = magnitudeForZoom(zoom);
    const adapt = Math.sqrt(Math.max(1, zoom));

    if (source === 'database') {
      for (const o of displayObjects) {
        if (!isTypeVisible(o.object_type)) continue;
        const ra = Number(o.ra_hours);
        const dec = Number(o.dec_degrees);
        if (!Number.isFinite(ra) || !Number.isFinite(dec)) continue;

        const mag = Number(o.v_magnitude);
        const hasMag = Number.isFinite(mag) && mag !== 0;
        if (hasMag && mag > magLimit) continue;

        const coord = toCoord(ra, dec);
        if (!clip(coord)) continue;
        const pt = proj(coord);
        if (!pt) continue;

        const r = hasMag
          ? Math.max(1.2, Math.min(8, (6.5 - mag) * 1.2 * adapt))
          : 2.5;

        ctx.beginPath();
        ctx.arc(pt[0], pt[1], r, 0, 2 * Math.PI);
        ctx.fillStyle = typeColor(o.object_type);
        ctx.fill();
        if (r > 3.5) {
          ctx.strokeStyle = 'rgba(0,0,0,0.45)';
          ctx.lineWidth = 0.8;
          ctx.stroke();
        }
      }
    }

    // Telescope position marker (only when calibrated).
    if (mountCalibrated && Number.isFinite(mountRaHours) && Number.isFinite(mountDecDeg)) {
      const coord = toCoord(mountRaHours, mountDecDeg);
      if (clip(coord)) {
        const pt = proj(coord);
        if (pt) {
          ctx.strokeStyle = '#ffd54f';
          ctx.lineWidth = 1.6;
          ctx.beginPath();
          ctx.arc(pt[0], pt[1], 9, 0, 2 * Math.PI);
          ctx.stroke();
          ctx.beginPath();
          ctx.moveTo(pt[0] - 13, pt[1]); ctx.lineTo(pt[0] + 13, pt[1]);
          ctx.moveTo(pt[0], pt[1] - 13); ctx.lineTo(pt[0], pt[1] + 13);
          ctx.stroke();
        }
      }
    }
  }

  function redrawMap() {
    if (mapCreated && typeof Celestial !== 'undefined' && typeof Celestial.redraw === 'function') {
      Celestial.redraw();
    }
  }

  // ─── Hit-testing ──────────────────────────────────────────────────────
  function containerPoint(ev) {
    const rect = containerEl.getBoundingClientRect();
    return { x: ev.clientX - rect.left, y: ev.clientY - rect.top };
  }
  function findObjectAt(ev) {
    if (!mapCreated || typeof Celestial === 'undefined' || !Celestial.map) return null;
    const proj = Celestial.map.projection();
    const clip = (typeof Celestial.clip === 'function') ? Celestial.clip : function () { return 1; };
    const sp = containerPoint(ev);

    let best = null;
    let bestDist = 12 * 12;

    if (source === 'database') {
      for (const o of displayObjects) {
        if (!isTypeVisible(o.object_type)) continue;
        const ra = Number(o.ra_hours);
        const dec = Number(o.dec_degrees);
        if (!Number.isFinite(ra) || !Number.isFinite(dec)) continue;
        const coord = toCoord(ra, dec);
        if (!clip(coord)) continue;
        const pt = proj(coord);
        if (!pt) continue;
        const dx = pt[0] - sp.x;
        const dy = pt[1] - sp.y;
        const dist = dx * dx + dy * dy;
        if (dist < bestDist) { bestDist = dist; best = { kind: 'db', o: o }; }
      }
    } else if (Celestial.container) {
      Celestial.container.selectAll('.star').each(function (d) {
        const coord = d.geometry.coordinates;
        if (!clip(coord)) return;
        const pt = proj(coord);
        if (!pt) return;
        const dx = pt[0] - sp.x;
        const dy = pt[1] - sp.y;
        const dist = dx * dx + dy * dy;
        if (dist < bestDist) { bestDist = dist; best = { kind: 'star', d: d }; }
      });
      Celestial.container.selectAll('.dso').each(function (d) {
        const coord = d.geometry.coordinates;
        if (!clip(coord)) return;
        const pt = proj(coord);
        if (!pt) return;
        const dx = pt[0] - sp.x;
        const dy = pt[1] - sp.y;
        const dist = dx * dx + dy * dy;
        if (dist < bestDist) { bestDist = dist; best = { kind: 'dso', d: d }; }
      });
    }
    return best;
  }

  // ─── Popup ────────────────────────────────────────────────────────────
  function t(key, fallback) {
    if (typeof I18n !== 'undefined' && typeof I18n.t === 'function') {
      try { return I18n.t(key); } catch (e) { /* fall through */ }
    }
    return fallback;
  }
  function formatRa(ra) {
    if (!Number.isFinite(ra)) return '—';
    if (typeof Utils !== 'undefined' && typeof Utils.formatRA === 'function') return Utils.formatRA(ra);
    return Number(ra).toFixed(2) + 'h';
  }
  function formatDec(dec) {
    if (!Number.isFinite(dec)) return '—';
    if (typeof Utils !== 'undefined' && typeof Utils.formatDec === 'function') return Utils.formatDec(dec);
    return Number(dec).toFixed(2) + '°';
  }
  function starDisplayName(id) {
    const s = starNames[String(id)];
    if (!s) return 'HIP ' + id;
    const c = s.c || '';
    if (s.name) return s.name;
    if (s.bayer) return c ? `${s.bayer} ${c}` : s.bayer;
    if (s.flam) return c ? `${s.flam} ${c}` : s.flam;
    if (s.hip) return s.hip;
    if (s.desig) return c ? `${s.desig} ${c}` : s.desig;
    return 'HIP ' + id;
  }
  function starAllNames(id) {
    const names = [];
    const s = starNames[String(id)];
    const c = s ? (s.c || '') : '';
    if (s) {
      if (s.name) names.push(s.name);
      if (s.bayer) names.push(c ? `${s.bayer} ${c}` : s.bayer);
      if (s.flam) names.push(c ? `${s.flam} ${c}` : s.flam);
      if (s.hip) names.push(s.hip);
      if (s.hd) names.push(s.hd);
      if (s.var) names.push(c ? `${s.var} ${c}` : s.var);
      if (s.desig) names.push(c ? `${s.desig} ${c}` : s.desig);
    }
    const hip = 'HIP ' + id;
    if (names.indexOf(hip) === -1) names.push(hip);
    return names;
  }
  function objectLabel(o) {
    return o.name || o.catalog_name || o.id || 'Object';
  }
  function addInfoRow(dl, label, value) {
    const dt = document.createElement('dt');
    dt.textContent = label;
    const dd = document.createElement('dd');
    dd.textContent = value;
    dl.append(dt, dd);
  }

  function buildDetails(hit) {
    if (hit.kind === 'db') {
      const o = hit.o;
      const ra = Number(o.ra_hours);
      const dec = Number(o.dec_degrees);
      const mag = Number(o.v_magnitude);
      const names = [];
      if (o.name) names.push(o.name);
      if (o.alternate_names) {
        String(o.alternate_names).split(',').forEach((n) => {
          const nm = n.trim();
          if (nm && names.indexOf(nm) === -1) names.push(nm);
        });
      }
      return {
        title: objectLabel(o),
        raHours: ra,
        decDegrees: dec,
        names: names,
        rows: [
          [t('skymap.popup_type', 'Type'), typeLabel(o.object_type)],
          [t('skymap.popup_magnitude', 'Magnitude'), (Number.isFinite(mag) && mag !== 0) ? mag.toFixed(2) : '—'],
          [t('skymap.popup_ra', 'RA'), formatRa(ra)],
          [t('skymap.popup_dec', 'Dec'), formatDec(dec)],
          [t('skymap.popup_catalog', 'Catalog'), o.catalog_name || '—'],
        ],
      };
    }

    const d = hit.d;
    const coord = d.geometry.coordinates;
    const raHours = coord[0] / 15.0;
    const decDeg = coord[1];

    if (hit.kind === 'star') {
      const mag = Number(d.properties.mag);
      return {
        title: starDisplayName(d.id),
        raHours: raHours,
        decDegrees: decDeg,
        names: starAllNames(d.id),
        rows: [
          [t('skymap.popup_type', 'Type'), 'Star'],
          [t('skymap.popup_magnitude', 'Magnitude'), Number.isFinite(mag) ? mag.toFixed(2) : '—'],
          [t('skymap.popup_ra', 'RA'), formatRa(raHours)],
          [t('skymap.popup_dec', 'Dec'), formatDec(decDeg)],
        ],
      };
    }

    const mag = Number(d.properties.mag);
    const dsoNames = [];
    if (d.properties.desig) dsoNames.push(d.properties.desig);
    if (d.id && dsoNames.indexOf(d.id) === -1) dsoNames.push(d.id);
    return {
      title: d.properties.desig || d.id || 'Deep-Sky Object',
      raHours: raHours,
      decDegrees: decDeg,
      names: dsoNames,
      rows: [
        [t('skymap.popup_type', 'Type'), dsoTypeLabel(d.properties.type)],
        [t('skymap.popup_magnitude', 'Magnitude'), Number.isFinite(mag) ? mag.toFixed(2) : '—'],
        [t('skymap.popup_ra', 'RA'), formatRa(raHours)],
        [t('skymap.popup_dec', 'Dec'), formatDec(decDeg)],
      ],
    };
  }

  function showPopup(details) {
    if (!popupEl) return;
    selectedObject = { name: details.title, raHours: details.raHours, decDegrees: details.decDegrees };
    const titleEl = document.getElementById('skymap-popup-title');
    const bodyEl = document.getElementById('skymap-popup-body');
    if (titleEl) titleEl.textContent = details.title;
    if (bodyEl) {
      bodyEl.textContent = '';
      if (Array.isArray(details.names) && details.names.length) {
        addInfoRow(bodyEl, t('skymap.popup_names', 'Names'), details.names.join(', '));
      }
      for (const [label, value] of details.rows) addInfoRow(bodyEl, label, value);
    }
    const slewBtn = document.getElementById('skymap-popup-slew');
    const trackBtn = document.getElementById('skymap-popup-track');
    if (slewBtn) slewBtn.textContent = t('db.slew_btn', 'Slew');
    if (trackBtn) trackBtn.textContent = t('slew.track_btn', 'Slew & Track');
    popupEl.hidden = false;
  }
  function hidePopup() {
    if (popupEl) popupEl.hidden = true;
    selectedObject = null;
  }
  async function runAction(action) {
    const sel = selectedObject;
    if (!sel) return;
    const ra = sel.raHours;
    const dec = sel.decDegrees;
    if (!Number.isFinite(ra) || !Number.isFinite(dec)) {
      App.showToast('Object has no coordinates', 'error');
      hidePopup();
      return;
    }
    const name = sel.name;
    hidePopup();
    try {
      if (action === 'track') {
        await Api.trackObject(ra, dec);
        App.showToast(`${t('slew.track_btn', 'Slew & Track')} — ${name}`, 'success');
      } else {
        await Api.slewToCoordinates(ra, dec);
        App.showToast(`Slew — ${name}`, 'info');
      }
    } catch (err) {
      App.showToast(`${action === 'track' ? 'Track' : 'Slew'} failed: ${err.message}`, 'error');
    }
  }

  // ─── Mode & source switching ──────────────────────────────────────────
  function updateModeButtons() {
    const eq = document.getElementById('skymap-mode-eq');
    const polar = document.getElementById('skymap-mode-polar');
    const obs = document.getElementById('skymap-mode-obs');
    if (eq) eq.classList.toggle('active', viewMode === 'equatorial');
    if (polar) polar.classList.toggle('active', viewMode === 'polar');
    if (obs) obs.classList.toggle('active', viewMode === 'horizontal');
  }
  function updateSourceControl() {
    if (sourceEl) sourceEl.value = source;
  }
  async function setViewMode(mode) {
    if (viewMode === mode) return;
    viewMode = mode;
    updateModeButtons();
    hidePopup();
    if (mode === 'horizontal') await loadMountState();
    if (mapCreated) {
      Celestial.rotate({ center: centerForMode() });
      Celestial.reproject({ projection: projectionForMode() });
    }
    applyFullscreenLayout();
  }
  async function setSource(src) {
    if (source === src) return;
    source = src;
    updateSourceControl();
    hidePopup();
    if (mapCreated) {
      Celestial.apply({ stars: { show: src === 'library' }, dsos: { show: src === 'library' } });
    }
    if (src === 'database' && objects.length === 0) await loadObjects();
    redrawMap();
  }

  // ─── Types menu ───────────────────────────────────────────────────────
  function buildTypesMenu() {
    if (!typesMenuEl) return;
    typesMenuEl.textContent = '';
    TYPE_CATEGORIES.forEach((cat) => {
      const label = document.createElement('label');
      label.className = 'skymap-types-item';
      const cb = document.createElement('input');
      cb.type = 'checkbox';
      cb.checked = visibleCategories.has(cat.key);
      cb.dataset.category = cat.key;
      const span = document.createElement('span');
      span.textContent = cat.label;
      label.append(cb, span);
      typesMenuEl.appendChild(label);
    });
  }
  function toggleTypesMenu() {
    if (!typesMenuEl) return;
    typesMenuEl.hidden = !typesMenuEl.hidden;
  }
  function onTypesChange(cb) {
    if (cb.checked) visibleCategories.add(cb.dataset.category);
    else visibleCategories.delete(cb.dataset.category);
    redrawMap();
  }

  // ─── Search ───────────────────────────────────────────────────────────
  function searchLibraryObject(q) {
    if (typeof Celestial === 'undefined' || !Celestial.container) return null;
    let found = null;
    Celestial.container.selectAll('.star').each(function (d) {
      if (found) return;
      const name = starDisplayName(d.id).toLowerCase();
      if (name.indexOf(q) !== -1 || String(d.id) === q) {
        found = { kind: 'star', d: d };
      }
    });
    if (!found) {
      Celestial.container.selectAll('.dso').each(function (d) {
        if (found) return;
        const desig = (d.properties.desig || d.id || '').toLowerCase();
        if (desig.indexOf(q) !== -1) found = { kind: 'dso', d: d };
      });
    }
    return found;
  }

  async function handleSearch() {
    const q = (searchInputEl ? searchInputEl.value : '').trim();
    if (!q) return;
    let hit = null;
    if (source === 'database') {
      try {
        const res = await Api.searchObjects({ query: q, limit: 5 });
        const objs = (res && res.objects) || [];
        if (objs.length) hit = { kind: 'db', o: objs[0] };
      } catch (err) {
        console.warn('[SkyMap] Search failed:', err);
      }
    } else {
      hit = searchLibraryObject(q.toLowerCase());
    }
    if (!hit) {
      App.showToast(t('skymap.no_result', 'Object not found'), 'warning');
      return;
    }
    const details = buildDetails(hit);
    if (mapCreated && Number.isFinite(details.raHours) && Number.isFinite(details.decDegrees)) {
      Celestial.rotate({ center: [((details.raHours * 15) % 360 + 360) % 360, details.decDegrees, 0] });
    }
    showPopup(details);
  }

  // ─── Maximise / centre on mount ───────────────────────────────────────
  function toggleMaximize() {
    if (!panelEl) return;
    if (document.fullscreenElement) {
      if (document.exitFullscreen) document.exitFullscreen();
    } else if (panelEl.requestFullscreen) {
      panelEl.requestFullscreen();
    }
  }

  function applyFullscreenLayout() {
    if (!panelEl || !containerEl) return;
    const fs = document.fullscreenElement === panelEl;
    if (!fs || viewMode === 'equatorial') {
      containerEl.style.maxWidth = '';
      return;
    }
    // Square projections (orthographic: polar / observer) must fit the
    // available height, otherwise the map gets clipped to half its size.
    const stage = containerEl.parentElement;
    const rect = stage ? stage.getBoundingClientRect() : null;
    const h = rect ? rect.height : 0;
    containerEl.style.maxWidth = (h > 0 ? Math.floor(h) : Math.min(window.innerWidth, window.innerHeight)) + 'px';
  }

  async function refreshMap() {
    await loadMountState();
    if (source === 'database') await loadObjects();
    redrawMap();
  }
  function centerOnMount() {
    if (!mountCalibrated || !Number.isFinite(mountRaHours) || !Number.isFinite(mountDecDeg)) return;
    if (mapCreated) {
      Celestial.rotate({ center: [((mountRaHours * 15) % 360 + 360) % 360, mountDecDeg, 0] });
    }
  }
  function updateCenterButton() {
    if (centerMountBtnEl) centerMountBtnEl.disabled = !mountCalibrated;
  }

  // ─── Zoom & legend ────────────────────────────────────────────────────
  function zoomIn() {
    if (!mapCreated || typeof Celestial === 'undefined') return;
    Celestial.zoomBy(1.25);
    redrawMap();
  }
  function zoomOut() {
    if (!mapCreated || typeof Celestial === 'undefined') return;
    Celestial.zoomBy(0.8);
    redrawMap();
  }
  function zoomReset() {
    if (!mapCreated || typeof Celestial === 'undefined') return;
    const z = currentZoom();
    if (Number.isFinite(z) && z > 0) Celestial.zoomBy(1 / z);
    redrawMap();
  }

  function categoryColor(cat) {
    for (const ty of cat.types) {
      if (TYPE_COLORS[ty]) return TYPE_COLORS[ty];
    }
    return DEFAULT_COLOR;
  }

  function renderLegend() {
    if (!legendEl) return;
    legendEl.textContent = '';

    const title = document.createElement('span');
    title.className = 'skymap-legend-title';
    title.textContent = t('skymap.legend_title', 'Legend:');
    legendEl.appendChild(title);

    TYPE_CATEGORIES.forEach((cat) => {
      const item = document.createElement('span');
      item.className = 'skymap-legend-item';
      const dot = document.createElement('span');
      dot.className = 'skymap-legend-dot';
      dot.style.backgroundColor = categoryColor(cat);
      const label = document.createElement('span');
      label.textContent = cat.label;
      item.append(dot, label);
      legendEl.appendChild(item);
    });

    const mountItem = document.createElement('span');
    mountItem.className = 'skymap-legend-item';
    const mountDot = document.createElement('span');
    mountDot.className = 'skymap-legend-dot';
    mountDot.style.backgroundColor = '#ffd54f';
    const mountLabel = document.createElement('span');
    mountLabel.textContent = t('skymap.legend_telescope', 'Telescope');
    mountItem.append(mountDot, mountLabel);
    legendEl.appendChild(mountItem);
  }

  // ─── Events ───────────────────────────────────────────────────────────
  function onClick(ev) {
    const moved = pointerDownPos
      ? Math.hypot(ev.clientX - pointerDownPos.x, ev.clientY - pointerDownPos.y)
      : 0;
    if (moved > 6) return;
    const hit = findObjectAt(ev);
    if (hit) showPopup(buildDetails(hit));
    else hidePopup();
  }

  function bindEvents() {
    if (!containerEl) return;

    containerEl.addEventListener('pointerdown', (ev) => {
      pointerDownPos = { x: ev.clientX, y: ev.clientY };
    });
    containerEl.addEventListener('click', onClick);

    if (popupEl) {
      popupEl.addEventListener('click', (ev) => {
        if (ev.target === popupEl) hidePopup();
      });
      const closeBtn = document.getElementById('skymap-popup-close');
      if (closeBtn) closeBtn.addEventListener('click', hidePopup);
      const slewBtn = document.getElementById('skymap-popup-slew');
      const trackBtn = document.getElementById('skymap-popup-track');
      if (slewBtn) slewBtn.addEventListener('click', () => runAction('slew'));
      if (trackBtn) trackBtn.addEventListener('click', () => runAction('track'));
    }

    if (sourceEl) sourceEl.addEventListener('change', () => setSource(sourceEl.value));

    if (searchBtnEl) searchBtnEl.addEventListener('click', handleSearch);
    if (searchInputEl) searchInputEl.addEventListener('keydown', (ev) => { if (ev.key === 'Enter') handleSearch(); });

    if (typesToggleEl) typesToggleEl.addEventListener('click', toggleTypesMenu);
    if (typesMenuEl) {
      typesMenuEl.addEventListener('change', (ev) => {
        if (ev.target && ev.target.dataset && ev.target.dataset.category) onTypesChange(ev.target);
      });
    }
    document.addEventListener('click', (ev) => {
      if (typesMenuEl && !typesMenuEl.hidden && typesToggleEl &&
          !typesMenuEl.contains(ev.target) && !typesToggleEl.contains(ev.target)) {
        typesMenuEl.hidden = true;
      }
    });

    if (centerMountBtnEl) centerMountBtnEl.addEventListener('click', centerOnMount);
    if (maximizeBtnEl) maximizeBtnEl.addEventListener('click', toggleMaximize);
    if (refreshBtnEl) refreshBtnEl.addEventListener('click', refreshMap);
    if (zoomInBtnEl) zoomInBtnEl.addEventListener('click', zoomIn);
    if (zoomOutBtnEl) zoomOutBtnEl.addEventListener('click', zoomOut);
    if (zoomResetBtnEl) zoomResetBtnEl.addEventListener('click', zoomReset);
    document.addEventListener('fullscreenchange', () => {
      if (maximizeBtnEl) maximizeBtnEl.classList.toggle('active', !!document.fullscreenElement);
      applyFullscreenLayout();
      // Recompute the d3-celestial canvas once the fullscreen layout settles.
      if (mapCreated && typeof Celestial !== 'undefined' && typeof Celestial.resize === 'function') {
        setTimeout(() => { applyFullscreenLayout(); Celestial.resize(); }, 50);
      }
    });

    const eqBtn = document.getElementById('skymap-mode-eq');
    const polarBtn = document.getElementById('skymap-mode-polar');
    const obsBtn = document.getElementById('skymap-mode-obs');
    if (eqBtn) eqBtn.addEventListener('click', () => setViewMode('equatorial'));
    if (polarBtn) polarBtn.addEventListener('click', () => setViewMode('polar'));
    if (obsBtn) obsBtn.addEventListener('click', () => setViewMode('horizontal'));
  }

  // ─── Lazy map creation ────────────────────────────────────────────────
  function ensureMap() {
    if (mapCreated || !containerEl) return;
    if (containerEl.clientWidth < 10) return;
    if (typeof Celestial === 'undefined' || typeof Celestial.display !== 'function') {
      console.warn('[SkyMap] d3-celestial library is not available');
      return;
    }

    registerLayers();
    Celestial.display(buildConfig());
    mapCreated = true;

    loadStarNames();
    startMountPolling();
    if (source === 'database') loadObjects();
  }

  function bindVisibilityObserver() {
    const panel = document.getElementById('panel-skymap');
    if (!panel || !('MutationObserver' in window)) {
      ensureMap();
      return;
    }
    const observer = new MutationObserver(() => {
      if (panel.classList.contains('active')) setTimeout(ensureMap, 50);
    });
    observer.observe(panel, { attributes: true, attributeFilter: ['class'] });
    ensureMap();
  }

  // ─── Public API ───────────────────────────────────────────────────────
  function init() {
    if (initialized) return;
    initialized = true;
    containerEl = document.getElementById(CONTAINER_ID);
    if (!containerEl) return;
    panelEl = document.getElementById('skymap-panel');
    refreshBtnEl = document.getElementById('skymap-refresh');
    popupEl = document.getElementById('skymap-popup');
    sourceEl = document.getElementById('skymap-source');
    searchInputEl = document.getElementById('skymap-search-input');
    searchBtnEl = document.getElementById('skymap-search-btn');
    typesToggleEl = document.getElementById('skymap-types-toggle');
    typesMenuEl = document.getElementById('skymap-types-menu');
    centerMountBtnEl = document.getElementById('skymap-center-mount');
    maximizeBtnEl = document.getElementById('skymap-maximize');
    zoomInBtnEl = document.getElementById('skymap-zoom-in');
    zoomOutBtnEl = document.getElementById('skymap-zoom-out');
    zoomResetBtnEl = document.getElementById('skymap-zoom-reset');
    legendEl = document.getElementById('skymap-legend');
    if (searchInputEl) searchInputEl.placeholder = t('skymap.search_placeholder', 'Search object...');

    buildTypesMenu();
    updateModeButtons();
    updateSourceControl();
    updateCenterButton();
    renderLegend();
    bindEvents();
    bindVisibilityObserver();
  }

  return { init };
})();
