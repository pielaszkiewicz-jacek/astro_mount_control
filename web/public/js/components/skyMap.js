/**
 * Astronomical Mount Controller - Sky Map Component
 *
 * Renders a sky map of the objects stored in the object database.
 *
 * Two view modes are available:
 *   - "equatorial" (default): full-sky equirectangular RA/Dec chart,
 *   - "horizontal":   observer's local sky (altitude/azimuth) above the horizon
 *                     for the current sidereal time and mount location.
 *
 * Exposes a global `SkyMapComponent` object with an `init()` method, matching
 * the pattern used by the other UI components (mountStatus, database, ...).
 */

const SkyMapComponent = (() => {
  'use strict';

  // ─── Color / style configuration ──────────────────────────────────────
  const TYPE_COLORS = {
    STAR:                '#f5f5f5',
    GALAXY_SPIRAL:       '#4fc3f7',
    GALAXY_ELLIPTICAL:   '#4fc3f7',
    GALAXY_IRREGULAR:    '#4fc3f7',
    DIFFUSE_NEBULA:      '#66bb6a',
    EMISSION_NEBULA:     '#66bb6a',
    PLANETARY_NEBULA:    '#66bb6a',
    SUPERNOVA_REMNANT:   '#e57373',
    STAR_CLUSTER_OPEN:   '#ffb74d',
    STAR_CLUSTER_GLOBULAR:'#ffb74d',
    PLANET:              '#ff7043',
  };
  const DEFAULT_COLOR = '#b0b0b0';

  // ─── State ────────────────────────────────────────────────────────────
  let canvas = null;
  let ctx = null;
  let objects = [];
  let location = { latitude: 52.0, longitude: 21.0, altitude: 0 };
  let mountRaHours = null;
  let mountDecDeg = null;
  let hoveredObject = null;
  let dataTimer = null;
  let renderTimer = null;
  let initialized = false;

  // View mode: 'equatorial' (RA/Dec), 'polar', or 'horizontal' (local sky).
  let viewMode = 'equatorial';

  // Progressive magnitude loading: zooming in loads fainter objects.
  let loadedMaxMag = 0;
  let loadTimer = null;
  let displayObjects = [];   // objects filtered for the current zoom tier

  // User-selectable filters: object types and magnitude range.
  const typeFilter = new Set(['star', 'galaxy', 'nebula', 'cluster', 'planet']);
  let magMin = -2;
  let magMax = 16;

  // ─── View transform (zoom & pan) ───────────────────────────────────────
  let zoom = 1;
  let panX = 0;
  let panY = 0;
  const pointers = new Map();          // active pointerId -> {x, y}
  let panStartX = 0, panStartY = 0;
  let panStartPanX = 0, panStartPanY = 0;
  let pinchStartDist = 0, pinchStartZoom = 1;
  let pinchStartMidX = 0, pinchStartMidY = 0;
  let pinchStartPanX = 0, pinchStartPanY = 0;

  // ─── Astronomy helpers ────────────────────────────────────────────────

  /** Julian date from the current wall-clock time. */
  function currentJulianDate() {
    return Date.now() / 86400000.0 + 2440587.5;
  }

  /** Greenwich mean sidereal time [hours]. */
  function gmstHours(jd) {
    const d = jd - 2451545.0;
    const g = 18.697374558 + 24.06570982441908 * d;
    return ((g % 24) + 24) % 24;
  }

  /** Local sidereal time [hours] for a given longitude [deg]. */
  function lstHours(jd, longitudeDeg) {
    return ((gmstHours(jd) + longitudeDeg / 15.0) % 24 + 24) % 24;
  }

  /**
   * Convert equatorial coordinates (RA hours, Dec degrees) to horizontal
   * coordinates (altitude degrees, azimuth degrees from North).
   */
  function raDecToAltAz(raHours, decDeg, latDeg, jd, lonDeg) {
    const lst = lstHours(jd, lonDeg);
    const haDeg = ((lst - raHours) * 15.0 + 360.0) % 360.0; // hour angle
    const ha = haDeg * Math.PI / 180.0;
    const dec = decDeg * Math.PI / 180.0;
    const lat = latDeg * Math.PI / 180.0;

    const sinAlt = Math.sin(dec) * Math.sin(lat) +
                   Math.cos(dec) * Math.cos(lat) * Math.cos(ha);
    const alt = Math.asin(Math.max(-1, Math.min(1, sinAlt)));

    const cosAltCosAz = Math.sin(dec) * Math.cos(lat) -
                        Math.cos(dec) * Math.sin(lat) * Math.cos(ha);
    const cosAltSinAz = -Math.cos(dec) * Math.sin(ha);
    const az = Math.atan2(cosAltSinAz, cosAltCosAz); // 0 = North, 90° = East

    return {
      alt: alt * 180.0 / Math.PI,
      az: ((az * 180.0 / Math.PI) + 360.0) % 360.0,
    };
  }

  // ─── Projection helpers ───────────────────────────────────────────────

  /** Azimuthal-equidistant projection for the horizontal view: zenith at centre. */
  function projectHorizontal(altDeg, azDeg, cx, cy, R) {
    const zd = Math.max(0, 90.0 - altDeg); // zenith distance
    const r = (zd / 90.0) * R;
    const a = azDeg * Math.PI / 180.0;
    return { x: cx + r * Math.sin(a), y: cy - r * Math.cos(a) };
  }

  /** Equirectangular RA/Dec projection: full sky fits the canvas rectangle. */
  function projectEquatorial(raHours, decDeg, cx, cy, halfW, halfH) {
    const raDeg = ((raHours * 15.0) % 360.0 + 360.0) % 360.0;
    const x = cx - halfW + (raDeg / 360.0) * (2.0 * halfW);
    const y = cy - (decDeg / 90.0) * halfH;
    return { x, y };
  }

  /** Raw equirectangular x for a (possibly unwrapped) RA [hours]. */
  function eqX(raHours, geo) {
    return geo.cx - geo.halfW + (raHours / 24.0) * (2.0 * geo.halfW);
  }

  /** Equirectangular y for declination [deg]. */
  function eqY(decDeg, geo) {
    return geo.cy - (decDeg / 90.0) * geo.halfH;
  }

  /** Polar (planisphere) projection centred on the north celestial pole. */
  function projectPolar(raHours, decDeg, cx, cy, R) {
    const r = ((90.0 - decDeg) / 90.0) * R; // Dec +90 → 0, Dec 0 → R
    const th = raHours * 15.0 * Math.PI / 180.0;
    return { x: cx + r * Math.sin(th), y: cy - r * Math.cos(th) };
  }

  /** Horizon point (altitude = 0) → equatorial coordinates for the polar view. */
  function horizonAzToRaDec(azDeg, latDeg, lst) {
    const lat = latDeg * Math.PI / 180.0;
    const az = azDeg * Math.PI / 180.0;
    const sinDec = Math.cos(lat) * Math.cos(az);
    const dec = Math.asin(Math.max(-1, Math.min(1, sinDec)));
    const cosDec = Math.max(Math.cos(dec), 1e-9);
    const sinHA = -Math.sin(az) / cosDec;
    const cosHA = -Math.sin(dec) * Math.sin(lat) / Math.max(cosDec * Math.cos(lat), 1e-9);
    const haDeg = Math.atan2(sinHA, cosHA) * 180.0 / Math.PI;
    const raHours = ((lst - haDeg / 15.0) % 24 + 24) % 24;
    return { raHours, decDeg: dec * 180.0 / Math.PI };
  }

  function viewGeometry(w, h) {
    const cx = w / 2;
    const cy = h / 2;
    const pad = 24;
    const R = Math.max(10, Math.min(w, h) / 2 - pad);
    const halfW = Math.max(10, w / 2 - pad);
    const halfH = Math.max(10, h / 2 - pad);
    return { cx, cy, R, halfW, halfH };
  }

  /** Project an equatorial position into the current view's world coordinates. */
  function projectSkyPoint(raHours, decDeg, geo, jd) {
    if (viewMode === 'horizontal') {
      const { alt, az } = raDecToAltAz(raHours, decDeg, location.latitude, jd, location.longitude);
      if (alt < -2) return null; // below horizon
      return projectHorizontal(alt, az, geo.cx, geo.cy, geo.R);
    }
    if (viewMode === 'polar') {
      return projectPolar(raHours, decDeg, geo.cx, geo.cy, geo.R);
    }
    return projectEquatorial(raHours, decDeg, geo.cx, geo.cy, geo.halfW, geo.halfH);
  }

  /** Map a screen-space point to the sky-map (world) coordinate frame. */
  function screenToWorld(sx, sy, cx, cy) {
    return {
      x: (sx - cx - panX) / zoom + cx,
      y: (sy - cy - panY) / zoom + cy,
    };
  }

  /** Map a sky-map (world) point to screen-space. */
  function worldToScreen(x, y, cx, cy) {
    return {
      x: cx + panX + zoom * (x - cx),
      y: cy + panY + zoom * (y - cy),
    };
  }

  function clampZoom(z) {
    return Math.min(12, Math.max(1, z));
  }

  // ─── Data loading ─────────────────────────────────────────────────────

  /** Magnitude limit for the current zoom level (fainter as you zoom in). */
  function magnitudeForZoom(z) {
    if (z <= 1) return 6;
    if (z <= 1.5) return 8;
    if (z <= 2.5) return 10;
    if (z <= 4) return 12;
    if (z <= 7) return 14;
    return 16;
  }

  /** Map a database object type to a coarse filter category. */
  function typeCategory(type) {
    const t = (type || '').toUpperCase();
    if (t === 'STAR') return 'star';
    if (t.startsWith('GALAXY')) return 'galaxy';
    if (t.includes('NEBULA') || t === 'SUPERNOVA_REMNANT') return 'nebula';
    if (t.includes('CLUSTER')) return 'cluster';
    if (t === 'PLANET') return 'planet';
    return 'other';
  }

  /** Filter the loaded objects by selected types and magnitude range. */
  function rebuildDisplayList() {
    displayObjects = objects.filter((o) => {
      const cat = typeCategory(o.object_type);
      if (cat !== 'other' && !typeFilter.has(cat)) return false;
      const mag = Number(o.v_magnitude);
      if (Number.isFinite(mag) && (mag < magMin || mag > magMax)) return false;
      return true;
    });
  }

  /**
   * Load ALL objects up to the given magnitude limit by paginating through the
   * database until every matching object has been fetched (bounded by a safety
   * cap so a pathological query can never fetch indefinitely).
   */
  async function fetchObjects(magLimit) {
    const PAGE_SIZE = 30000;
    const MAX_OBJECTS = 180000;
    try {
      let all = [];
      let page = 1;
      while (all.length < MAX_OBJECTS) {
        const result = await Api.listObjects({
          page: page,
          pageSize: PAGE_SIZE,
          maxMagnitude: magLimit,
        });
        const batch = Array.isArray(result && result.objects) ? result.objects : [];
        if (batch.length === 0) break;
        all = all.concat(batch);

        const total = Number(result && result.total_count);
        if (Number.isFinite(total) && all.length >= total) break;
        if (batch.length < PAGE_SIZE) break;
        page += 1;
      }
      objects = all;
      loadedMaxMag = magLimit;
    } catch (err) {
      console.warn('[SkyMap] Failed to load database objects:', err);
    }
    rebuildDisplayList();
    draw();
  }

  /** Debounced upgrade of the loaded magnitude tier for the current zoom. */
  function requestObjectsForZoom() {
    // Update the visible subset immediately, then load a fainter tier if needed.
    rebuildDisplayList();
    // Progressive loading is capped by the user-selected max magnitude.
    const target = Math.min(magnitudeForZoom(zoom), magMax);
    if (target <= loadedMaxMag) return;
    clearTimeout(loadTimer);
    loadTimer = setTimeout(() => fetchObjects(target), 250);
  }

  async function loadData() {
    try {
      const status = await Api.getStatus();
      if (status && status.location) {
        location = {
          latitude: Number(status.location.latitude) || 0,
          longitude: Number(status.location.longitude) || 0,
          altitude: Number(status.location.altitude) || 0,
        };
      }
      mountRaHours = (typeof status.current_ra === 'number') ? status.current_ra : null;
      mountDecDeg = (typeof status.current_dec === 'number') ? status.current_dec : null;
    } catch (err) {
      console.warn('[SkyMap] Failed to read mount status:', err);
    }

    await fetchObjects(magnitudeForZoom(zoom));
  }

  // ─── Rendering ────────────────────────────────────────────────────────

  function typeColor(type) {
    const key = (type || '').toUpperCase();
    return TYPE_COLORS[key] || DEFAULT_COLOR;
  }

  function uiScale() {
    const s = parseFloat(getComputedStyle(document.documentElement).getPropertyValue('--ui-scale')) || 1;
    return Number.isFinite(s) ? s : 1;
  }

  function fmtHms(hours) {
    const h = Math.floor(hours);
    const m = Math.floor((hours - h) * 60);
    const s = Math.floor((hours - h - m / 60) * 3600);
    return `${String(h).padStart(2, '0')}:${String(m).padStart(2, '0')}:${String(s).padStart(2, '0')}`;
  }

  function fmtRaLabel(raDeg) {
    const h = raDeg / 15.0;
    const hh = Math.floor(h);
    const mm = Math.floor((h - hh) * 60);
    return `${String(hh).padStart(2, '0')}h${String(mm).padStart(2, '0')}m`;
  }

  function setupCanvas() {
    if (!canvas) return null;
    const dpr = window.devicePixelRatio || 1;
    const rect = canvas.getBoundingClientRect();
    const w = Math.max(1, Math.floor(rect.width));
    const h = Math.max(1, Math.floor(rect.height));
    canvas.width = Math.floor(w * dpr);
    canvas.height = Math.floor(h * dpr);
    ctx = canvas.getContext('2d');
    ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
    return { w, h };
  }

  function draw() {
    if (!canvas) return;
    const size = setupCanvas();
    if (!size || !ctx) return;
    const { w, h } = size;

    const geo = viewGeometry(w, h);
    const jd = currentJulianDate();
    const lst = lstHours(jd, location.longitude);

    // Background (screen space)
    ctx.clearRect(0, 0, w, h);
    ctx.fillStyle = 'rgba(10, 12, 24, 0.6)';
    ctx.fillRect(0, 0, w, h);

    // ── Sky map (world space, with zoom & pan applied) ──
    ctx.save();
    ctx.translate(geo.cx + panX, geo.cy + panY);
    ctx.scale(zoom, zoom);
    ctx.translate(-geo.cx, -geo.cy);

    if (viewMode === 'horizontal') {
      drawHorizontalGrid(geo);
    } else if (viewMode === 'polar') {
      drawPolarGrid(geo, jd);
    } else {
      drawEquatorialGrid(geo);
    }
    drawConstellationBoundaries(geo, jd);
    drawConstellationLines(geo, jd);
    drawObjects(geo, jd);
    drawMountMarker(geo, jd);

    ctx.restore();

    // ── HUD (fixed screen space) ──
    const modeLabel = viewMode === 'horizontal' ? 'Horizon' : (viewMode === 'polar' ? 'Polar' : 'RA/Dec');
    ctx.fillStyle = 'rgba(160, 175, 200, 0.75)';
    ctx.font = `${Math.max(10, 11 * uiScale())}px -apple-system, 'Segoe UI', Roboto, sans-serif`;
    ctx.textAlign = 'left';
    ctx.fillText(
      `${modeLabel} · LST ${fmtHms(lst)} · ${location.latitude.toFixed(2)}°, ${location.longitude.toFixed(2)}° · zoom ${Math.round(zoom * 100)}%`,
      8, h - 8
    );

    // Hovered object label
    if (hoveredObject) {
      const p = worldToScreen(hoveredObject.pos.x, hoveredObject.pos.y, geo.cx, geo.cy);
      const name = hoveredObject.name || hoveredObject.catalog_name || '';
      ctx.font = `${Math.max(10, 11 * uiScale())}px -apple-system, 'Segoe UI', Roboto, sans-serif`;
      ctx.textAlign = 'left';
      const label = `${name} · ${Number(hoveredObject.v_magnitude).toFixed(1)} mag`;
      const tw = ctx.measureText(label).width;
      ctx.fillStyle = 'rgba(0,0,0,0.7)';
      ctx.fillRect(p.x + 8, p.y - 9, tw + 8, 16);
      ctx.fillStyle = '#fff';
      ctx.fillText(label, p.x + 12, p.y + 3);
    }
  }

  function drawEquatorialGrid(geo) {
    const { cx, cy, halfW, halfH } = geo;

    // Frame
    ctx.strokeStyle = 'rgba(120, 140, 180, 0.35)';
    ctx.lineWidth = 1;
    ctx.strokeRect(cx - halfW, cy - halfH, halfW * 2, halfH * 2);

    // Dec parallels every 30° (labels in degrees)
    for (let dec = -60; dec <= 60; dec += 30) {
      const y = cy - (dec / 90.0) * halfH;
      ctx.beginPath();
      ctx.moveTo(cx - halfW, y);
      ctx.lineTo(cx + halfW, y);
      ctx.stroke();
    }

    // RA meridians every 30° (2h) (labels in hours)
    for (let ra = 0; ra < 360; ra += 30) {
      const x = cx - halfW + (ra / 360.0) * (2.0 * halfW);
      ctx.beginPath();
      ctx.moveTo(x, cy - halfH);
      ctx.lineTo(x, cy + halfH);
      ctx.stroke();
    }

    ctx.fillStyle = 'rgba(200, 210, 235, 0.8)';
    ctx.font = `${Math.max(10, 11 * uiScale())}px -apple-system, 'Segoe UI', Roboto, sans-serif`;
    ctx.textAlign = 'center';
    ctx.textBaseline = 'middle';

    // RA labels along the top
    for (let ra = 0; ra < 360; ra += 30) {
      const x = cx - halfW + (ra / 360.0) * (2.0 * halfW);
      ctx.fillText(fmtRaLabel(ra), x, cy - halfH - 10);
    }
    // Dec labels along the left
    ctx.textAlign = 'right';
    for (let dec = -60; dec <= 60; dec += 30) {
      const y = cy - (dec / 90.0) * halfH;
      ctx.fillText(`${dec > 0 ? '+' : ''}${dec}°`, cx - halfW - 6, y);
    }
  }

  function drawHorizontalGrid(geo) {
    const { cx, cy, R } = geo;

    const skyGrad = ctx.createRadialGradient(cx, cy, 0, cx, cy, R);
    skyGrad.addColorStop(0, 'rgba(30, 40, 80, 0.55)');
    skyGrad.addColorStop(1, 'rgba(12, 14, 30, 0.75)');
    ctx.beginPath();
    ctx.arc(cx, cy, R, 0, Math.PI * 2);
    ctx.fillStyle = skyGrad;
    ctx.fill();

    // Altitude rings (30°, 60°) and horizon
    ctx.strokeStyle = 'rgba(120, 140, 180, 0.35)';
    ctx.lineWidth = 1;
    [30, 60, 90].forEach((zd) => {
      ctx.beginPath();
      ctx.arc(cx, cy, (zd / 90.0) * R, 0, Math.PI * 2);
      ctx.stroke();
    });

    // Cardinal directions
    ctx.fillStyle = 'rgba(210, 220, 240, 0.9)';
    ctx.font = `${Math.max(11, 12 * uiScale())}px -apple-system, 'Segoe UI', Roboto, sans-serif`;
    ctx.textAlign = 'center';
    ctx.textBaseline = 'middle';
    [['N', 0], ['E', 90], ['S', 180], ['W', 270]].forEach(([label, az]) => {
      const p = projectHorizontal(0, az, cx, cy, R + 14);
      ctx.fillText(label, p.x, p.y);
    });

    // Zenith marker
    ctx.fillStyle = 'rgba(210, 220, 240, 0.8)';
    ctx.beginPath();
    ctx.arc(cx, cy, 3, 0, Math.PI * 2);
    ctx.fill();
    ctx.fillStyle = 'rgba(180, 190, 210, 0.7)';
    ctx.fillText('Z', cx, cy + 12);
  }

  function drawPolarGrid(geo, jd) {
    const { cx, cy, R } = geo;
    const lst = lstHours(jd, location.longitude);

    // Dec circles (concentric): NCP at centre, celestial equator at radius R.
    ctx.strokeStyle = 'rgba(120, 140, 180, 0.35)';
    ctx.lineWidth = 1;
    [90, 60, 30, 0].forEach((dec) => {
      const r = ((90 - dec) / 90.0) * R;
      ctx.beginPath();
      ctx.arc(cx, cy, r, 0, Math.PI * 2);
      ctx.stroke();
    });
    // Below-equator reference circle (Dec = -30°), dashed.
    ctx.setLineDash([4, 4]);
    ctx.beginPath();
    ctx.arc(cx, cy, ((90 - (-30)) / 90.0) * R, 0, Math.PI * 2);
    ctx.stroke();
    ctx.setLineDash([]);

    // RA spokes every 2h.
    for (let h = 0; h < 24; h += 2) {
      const th = h * 15 * Math.PI / 180;
      ctx.beginPath();
      ctx.moveTo(cx, cy);
      ctx.lineTo(cx + R * Math.sin(th), cy - R * Math.cos(th));
      ctx.stroke();
    }

    // RA labels (hours) just outside the equator circle.
    ctx.fillStyle = 'rgba(200, 210, 235, 0.85)';
    ctx.font = `${Math.max(10, 11 * uiScale())}px -apple-system, 'Segoe UI', Roboto, sans-serif`;
    ctx.textAlign = 'center';
    ctx.textBaseline = 'middle';
    for (let h = 0; h < 24; h += 2) {
      const th = h * 15 * Math.PI / 180;
      const p = { x: cx + (R + 14) * Math.sin(th), y: cy - (R + 14) * Math.cos(th) };
      ctx.fillText(`${h}h`, p.x, p.y);
    }

    // North celestial pole marker.
    ctx.fillStyle = 'rgba(210, 220, 240, 0.8)';
    ctx.beginPath();
    ctx.arc(cx, cy, 3, 0, Math.PI * 2);
    ctx.fill();
    ctx.fillText('NCP', cx, cy - 12);

    // Apparent horizon (altitude = 0) drawn as a curve over the polar grid.
    ctx.beginPath();
    for (let az = 0; az <= 360; az += 2) {
      const hp = horizonAzToRaDec(az, location.latitude, lst);
      const p = projectPolar(hp.raHours, hp.decDeg, cx, cy, R);
      if (az === 0) ctx.moveTo(p.x, p.y); else ctx.lineTo(p.x, p.y);
    }
    ctx.closePath();
    ctx.fillStyle = 'rgba(120, 200, 255, 0.10)';
    ctx.fill();
    ctx.strokeStyle = 'rgba(120, 200, 255, 0.9)';
    ctx.lineWidth = 1.5;
    ctx.stroke();
  }

  /** Append a line segment between two equatorial points to the current path. */
  function drawSkyLine(geo, jd, ra1, dec1, ra2, dec2) {
    if (viewMode === 'horizontal') {
      const p1 = projectSkyPoint(ra1, dec1, geo, jd);
      const p2 = projectSkyPoint(ra2, dec2, geo, jd);
      if (!p1 || !p2) return;
      ctx.moveTo(p1.x, p1.y);
      ctx.lineTo(p2.x, p2.y);
    } else if (viewMode === 'polar') {
      // Unwrap RA so segments crossing 0h draw as a short arc, not a chord.
      let r2 = ra2;
      if (r2 - ra1 > 12) r2 -= 24;
      else if (ra1 - r2 > 12) r2 += 24;
      const p1 = projectPolar(ra1, dec1, geo.cx, geo.cy, geo.R);
      const p2 = projectPolar(r2, dec2, geo.cx, geo.cy, geo.R);
      ctx.moveTo(p1.x, p1.y);
      ctx.lineTo(p2.x, p2.y);
    } else {
      // Equirectangular: split segments crossing the RA 0/24h wrap.
      const a = ((ra1 % 24) + 24) % 24;
      const b = ((ra2 % 24) + 24) % 24;
      if (Math.abs(b - a) > 12) {
        const hi = Math.max(a, b), lo = Math.min(a, b);
        ctx.moveTo(eqX(hi, geo), eqY(dec1, geo));
        ctx.lineTo(eqX(24, geo), eqY(dec1, geo));
        ctx.moveTo(eqX(0, geo), eqY(dec2, geo));
        ctx.lineTo(eqX(lo, geo), eqY(dec2, geo));
      } else {
        ctx.moveTo(eqX(a, geo), eqY(dec1, geo));
        ctx.lineTo(eqX(b, geo), eqY(dec2, geo));
      }
    }
  }

  function drawConstellationBoundaries(geo, jd) {
    const bounds = (typeof window !== 'undefined' && window.CONSTELLATION_BOUNDARIES) || [];
    if (!bounds.length) return;

    ctx.strokeStyle = 'rgba(150, 165, 210, 0.22)';
    ctx.lineWidth = 1;
    ctx.beginPath();

    bounds.forEach((seg) => {
      drawSkyLine(geo, jd, seg[0], seg[1], seg[2], seg[3]);
    });

    ctx.stroke();
  }

  function drawConstellationLines(geo, jd) {
    // Keep the RA/Dec chart clean: constellation figures are only drawn on the
    // polar and observer views.
    if (viewMode === 'equatorial') return;

    const lines = (typeof window !== 'undefined' && window.CONSTELLATION_LINES) || [];
    if (!lines.length) return;

    // Connecting lines between constellation stars.
    ctx.strokeStyle = 'rgba(190, 205, 245, 0.35)';
    ctx.lineWidth = 1;
    ctx.beginPath();
    lines.forEach((entry) => {
      const pts = entry.pts || [];
      for (let i = 0; i + 1 < pts.length; i++) {
        drawSkyLine(geo, jd, pts[i][0], pts[i][1], pts[i + 1][0], pts[i + 1][1]);
      }
    });
    ctx.stroke();

    // Render the constellation stars as slightly larger, bright dots.
    const seen = new Set();
    lines.forEach((entry) => {
      (entry.pts || []).forEach((p) => {
        const key = `${p[0].toFixed(4)}_${p[1].toFixed(4)}`;
        if (seen.has(key)) return;
        seen.add(key);
        const pos = projectSkyPoint(p[0], p[1], geo, jd);
        if (!pos) return;
        const mag = p[2];
        const r = 1.6 + Math.max(0, (3.5 - mag) * 0.55);
        ctx.beginPath();
        ctx.arc(pos.x, pos.y, Math.min(4.5, r), 0, Math.PI * 2);
        ctx.fillStyle = '#e6edff';
        ctx.fill();
      });
    });
  }

  function drawObjects(geo, jd) {
    displayObjects.forEach((obj) => {
      const ra = Number(obj.ra_hours);
      const dec = Number(obj.dec_degrees);
      if (!Number.isFinite(ra) || !Number.isFinite(dec)) return;

      const p = projectSkyPoint(ra, dec, geo, jd);
      if (!p) return;

      // In the polar view, dim objects that are currently below the horizon.
      if (viewMode === 'polar') {
        const { alt } = raDecToAltAz(ra, dec, location.latitude, jd, location.longitude);
        ctx.globalAlpha = alt < -2 ? 0.35 : 1;
      }

      const mag = Number(obj.v_magnitude);
      // Progressive LOD: only draw objects bright enough for the current zoom.
      if (Number.isFinite(mag) && mag > magnitudeForZoom(zoom)) return;
      const r = Number.isFinite(mag)
        ? Math.max(1.5, Math.min(6, (5.5 - mag) * 1.1))
        : 2.5;

      ctx.beginPath();
      ctx.arc(p.x, p.y, r, 0, Math.PI * 2);
      ctx.fillStyle = typeColor(obj.object_type);
      ctx.fill();
      if (r > 3.5) {
        ctx.strokeStyle = 'rgba(0,0,0,0.45)';
        ctx.lineWidth = 1;
        ctx.stroke();
      }
    });
    ctx.globalAlpha = 1;
  }

  function drawMountMarker(geo, jd) {
    if (!Number.isFinite(mountRaHours) || !Number.isFinite(mountDecDeg)) return;
    const p = projectSkyPoint(mountRaHours, mountDecDeg, geo, jd);
    if (!p) return;

    ctx.strokeStyle = '#ffd54f';
    ctx.lineWidth = 1.5;
    ctx.beginPath();
    ctx.arc(p.x, p.y, 8, 0, Math.PI * 2);
    ctx.stroke();
    ctx.beginPath();
    ctx.moveTo(p.x - 12, p.y); ctx.lineTo(p.x + 12, p.y);
    ctx.moveTo(p.x, p.y - 12); ctx.lineTo(p.x, p.y + 12);
    ctx.stroke();
  }

  function renderLegend() {
    const el = document.getElementById('skymap-legend');
    if (!el) return;
    const entries = [
      ['STAR', 'Star'],
      ['GALAXY_SPIRAL', 'Galaxy'],
      ['DIFFUSE_NEBULA', 'Nebula'],
      ['STAR_CLUSTER_GLOBULAR', 'Cluster'],
      ['PLANET', 'Planet'],
    ];
    el.innerHTML = entries.map(([type, label]) => {
      const color = TYPE_COLORS[type] || DEFAULT_COLOR;
      return `<span class="skymap-legend-item"><span class="skymap-legend-dot" style="background:${color}"></span>${label}</span>`;
    }).join('');
  }

  function findHovered(ev) {
    if (!canvas) return null;
    const rect = canvas.getBoundingClientRect();
    const mx = ev.clientX - rect.left;
    const my = ev.clientY - rect.top;
    const geo = viewGeometry(rect.width, rect.height);
    const wpt = screenToWorld(mx, my, geo.cx, geo.cy);
    const jd = currentJulianDate();

    let best = null, bestDist = 14 * 14;
    displayObjects.forEach((obj) => {
      const ra = Number(obj.ra_hours), dec = Number(obj.dec_degrees);
      if (!Number.isFinite(ra) || !Number.isFinite(dec)) return;
      const p = projectSkyPoint(ra, dec, geo, jd);
      if (!p) return;
      const d = (p.x - wpt.x) ** 2 + (p.y - wpt.y) ** 2;
      if (d < bestDist) { bestDist = d; best = Object.assign({}, obj, { pos: p }); }
    });
    return best;
  }

  // ─── Mode switching ───────────────────────────────────────────────────

  function updateModeButtons() {
    const eq = document.getElementById('skymap-mode-eq');
    const polar = document.getElementById('skymap-mode-polar');
    const obs = document.getElementById('skymap-mode-obs');
    if (eq) eq.classList.toggle('active', viewMode === 'equatorial');
    if (polar) polar.classList.toggle('active', viewMode === 'polar');
    if (obs) obs.classList.toggle('active', viewMode === 'horizontal');
  }

  function setViewMode(mode) {
    viewMode = mode;
    zoom = 1; panX = 0; panY = 0; // reset view when switching projections
    requestObjectsForZoom();
    updateModeButtons();
    draw();
  }

  // ─── Events ───────────────────────────────────────────────────────────

  function bindFilters() {
    // Object-type checkboxes.
    document.querySelectorAll('[data-skymap-type]').forEach((cb) => {
      cb.checked = typeFilter.has(cb.dataset.skymapType);
      cb.addEventListener('change', () => {
        if (cb.checked) typeFilter.add(cb.dataset.skymapType);
        else typeFilter.delete(cb.dataset.skymapType);
        rebuildDisplayList();
        draw();
      });
    });

    // Magnitude range inputs.
    const magMinInput = document.getElementById('skymap-mag-min');
    const magMaxInput = document.getElementById('skymap-mag-max');
    const applyMagRange = () => {
      let lo = parseFloat(magMinInput && magMinInput.value);
      let hi = parseFloat(magMaxInput && magMaxInput.value);
      if (!Number.isFinite(lo)) lo = -99;
      if (!Number.isFinite(hi)) hi = 99;
      if (lo > hi) { const t = lo; lo = hi; hi = t; }
      magMin = lo; magMax = hi;
      if (magMinInput) magMinInput.value = lo;
      if (magMaxInput) magMaxInput.value = hi;
      requestObjectsForZoom();
    };
    if (magMinInput) magMinInput.addEventListener('change', applyMagRange);
    if (magMaxInput) magMaxInput.addEventListener('change', applyMagRange);
  }

  function bindEvents() {
    if (!canvas) return;
    canvas.addEventListener('mousemove', (ev) => {
      hoveredObject = findHovered(ev);
      draw();
    });
    canvas.addEventListener('mouseleave', () => {
      hoveredObject = null;
      draw();
    });

    // Mouse-wheel zoom, anchored at the cursor position.
    canvas.addEventListener('wheel', (ev) => {
      ev.preventDefault();
      const rect = canvas.getBoundingClientRect();
      const mx = ev.clientX - rect.left;
      const my = ev.clientY - rect.top;
      const cx = rect.width / 2, cy = rect.height / 2;
      const factor = ev.deltaY < 0 ? 1.2 : 1 / 1.2;
      const newZoom = clampZoom(zoom * factor);
      const zr = newZoom / zoom;
      panX = (mx - cx) - (mx - cx - panX) * zr;
      panY = (my - cy) - (my - cy - panY) * zr;
      zoom = newZoom;
      requestObjectsForZoom();
      draw();
    }, { passive: false });

    // Drag to pan (mouse/touch), two-finger pinch to zoom.
    canvas.addEventListener('pointerdown', (ev) => {
      canvas.setPointerCapture(ev.pointerId);
      pointers.set(ev.pointerId, { x: ev.clientX, y: ev.clientY });
      if (pointers.size === 1) {
        panStartX = ev.clientX; panStartY = ev.clientY;
        panStartPanX = panX; panStartPanY = panY;
      } else if (pointers.size === 2) {
        const pts = [...pointers.values()];
        pinchStartDist = Math.hypot(pts[0].x - pts[1].x, pts[0].y - pts[1].y);
        pinchStartZoom = zoom;
        pinchStartMidX = (pts[0].x + pts[1].x) / 2;
        pinchStartMidY = (pts[0].y + pts[1].y) / 2;
        pinchStartPanX = panX; pinchStartPanY = panY;
      }
    });

    canvas.addEventListener('pointermove', (ev) => {
      if (!pointers.has(ev.pointerId)) return;
      pointers.set(ev.pointerId, { x: ev.clientX, y: ev.clientY });
      const rect = canvas.getBoundingClientRect();
      const cx = rect.width / 2, cy = rect.height / 2;

      if (pointers.size === 1) {
        panX = panStartPanX + (ev.clientX - panStartX);
        panY = panStartPanY + (ev.clientY - panStartY);
      } else if (pointers.size === 2) {
        const pts = [...pointers.values()];
        const midX = (pts[0].x + pts[1].x) / 2;
        const midY = (pts[0].y + pts[1].y) / 2;
        const dist = Math.hypot(pts[0].x - pts[1].x, pts[0].y - pts[1].y);
        if (pinchStartDist > 0) {
          const newZoom = clampZoom(pinchStartZoom * (dist / pinchStartDist));
          const zr = newZoom / pinchStartZoom;
          panX = (midX - cx) - (pinchStartMidX - cx - pinchStartPanX) * zr;
          panY = (midY - cy) - (pinchStartMidY - cy - pinchStartPanY) * zr;
          zoom = newZoom;
          requestObjectsForZoom();
        }
      }

      hoveredObject = findHovered(ev);
      draw();
    });

    const endPointer = (ev) => {
      if (!pointers.has(ev.pointerId)) return;
      pointers.delete(ev.pointerId);
      if (pointers.size === 1) {
        const p = [...pointers.values()][0];
        panStartX = p.x; panStartY = p.y;
        panStartPanX = panX; panStartPanY = panY;
      }
    };
    canvas.addEventListener('pointerup', endPointer);
    canvas.addEventListener('pointercancel', endPointer);
    canvas.addEventListener('pointerleave', endPointer);

    // Double-click resets zoom & pan.
    canvas.addEventListener('dblclick', () => {
      zoom = 1; panX = 0; panY = 0;
      requestObjectsForZoom();
      hoveredObject = null;
      draw();
    });

    // View-mode buttons
    const eqBtn = document.getElementById('skymap-mode-eq');
    const polarBtn = document.getElementById('skymap-mode-polar');
    const obsBtn = document.getElementById('skymap-mode-obs');
    if (eqBtn) eqBtn.addEventListener('click', () => setViewMode('equatorial'));
    if (polarBtn) polarBtn.addEventListener('click', () => setViewMode('polar'));
    if (obsBtn) obsBtn.addEventListener('click', () => setViewMode('horizontal'));

    const refreshBtn = document.getElementById('skymap-refresh');
    if (refreshBtn) refreshBtn.addEventListener('click', loadData);
    window.addEventListener('resize', draw);

    // Redraw when the Sky Map tab becomes visible — a hidden panel reports a
    // zero-size canvas, so we must re-render after the tab is activated.
    const panel = document.getElementById('panel-skymap');
    if (panel && 'MutationObserver' in window) {
      const observer = new MutationObserver(() => {
        if (panel.classList.contains('active')) {
          setTimeout(draw, 50);
        }
      });
      observer.observe(panel, { attributes: true, attributeFilter: ['class'] });
    }
  }

  // ─── Public API ───────────────────────────────────────────────────────

  function init() {
    if (initialized) return;
    initialized = true;
    canvas = document.getElementById('skymap-canvas');
    if (!canvas) return;
    renderLegend();
    updateModeButtons();
    bindEvents();
    bindFilters();
    loadData();
    // Refresh DB objects periodically and re-render so the sky follows the
    // advancing sidereal time (objects drift ~1° every 4 minutes).
    dataTimer = setInterval(loadData, 60000);
    renderTimer = setInterval(draw, 30000);
  }

  return { init };
})();
