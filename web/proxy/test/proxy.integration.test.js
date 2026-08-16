/**
 * Web Proxy — Integration Test (Phase 1)
 *
 * Verifies the Phase 1 web-proxy integration fix (P1/P3/P4) from
 * docs/pl/kompleksowa_analiza_projektu_2026-08-14.md:
 *
 *   1. Extended-service routes (weather, power, sequencer, focuser) are wired
 *      to their own dedicated gRPC clients. With no backend running they must
 *      return an EXPLICIT HTTP 503 error — never silent simulated data.
 *   2. Deferred-service routes (camera, pulley, pec, guider) have no hosted
 *      backend yet (Phase 2) and must return HTTP 503 "not implemented" —
 *      never simulated data.
 *   3. Control: the mount route still reports an explicit 503 when the mount
 *      backend is unreachable.
 *
 * The tests run without any live gRPC backend (the app is imported without
 * binding a port — see the `require.main` guard in server.js), so all service
 * calls fail with "client not initialised" / "connection refused", which the
 * routes must surface as 503.
 *
 * Run with:  NODE_ENV=test node --test test/
 */
'use strict';

const test = require('node:test');
const assert = require('node:assert/strict');
const request = require('supertest');

const app = require('../server');

// ─── Extended services (real backends, dedicated clients) → explicit 503 ─────

const EXTENDED_POST = [
  ['/api/power/output', { output_id: 0, enabled: true }],
  ['/api/sequencer/start', {}],
  ['/api/sequencer/stop', {}],
  ['/api/sequencer/load', {}],
  ['/api/focuser/move', { position: 1000 }],
  ['/api/focuser/halt', {}],
  ['/api/focuser/autofocus', {}],
  ['/api/guider/start', {}],
  ['/api/guider/stop', {}],
  ['/api/guider/calibrate', {}],
  ['/api/pec/enable', { enabled: true }],
  ['/api/pec/train/stop', {}],
  ['/api/pec/save', {}],
  // R3: camera & pulley are now hosted in-process (simulated backends). With no
  // live backend the dedicated client is unreachable → explicit 503.
  ['/api/camera/expose', {}],
  ['/api/camera/abort', {}],
  ['/api/pulley/deploy', {}],
  ['/api/pulley/retract', {}],
  ['/api/pulley/home', {}],
  // R1: notifications hosted in-process → explicit 503 when unreachable.
  ['/api/notifications/configure', {}],
  ['/api/notifications/test', {}],
];

const EXTENDED_GET = [
  ['/api/weather/status'],
  ['/api/weather/history'],
  ['/api/power/status'],
  ['/api/power/history'],
  ['/api/sequencer/status'],
  ['/api/focuser/status'],
  ['/api/dome/status'],
  ['/api/derotator/status'],
  ['/api/derotator/field-rotation'],
  ['/api/guider/status'],
  ['/api/pec/status'],
  // R3 / R1: camera, pulley, notifications — hosted in-process, 503 when down.
  ['/api/camera/info'],
  ['/api/pulley/status'],
  ['/api/notifications/status'],
];

test('extended-service routes return explicit HTTP 503 when backend is unreachable', async (t) => {
  for (const [url, body = {}] of EXTENDED_POST) {
    await t.test(`POST ${url} -> 503`, async () => {
      const res = await request(app).post(url).send(body);
      assert.equal(res.status, 503, `expected 503 for POST ${url}, got ${res.status}`);
      assert.ok(res.body && typeof res.body.error === 'string',
        `response for POST ${url} must carry an error message`);
    });
  }
  for (const [url] of EXTENDED_GET) {
    await t.test(`GET ${url} -> 503`, async () => {
      const res = await request(app).get(url);
      assert.equal(res.status, 503, `expected 503 for GET ${url}, got ${res.status}`);
      assert.ok(res.body && typeof res.body.error === 'string',
        `response for GET ${url} must carry an error message (no simulated data)`);
    });
  }
});

// ─── R3: camera & pulley are now hosted in-process — no more deferred 503s ────

test('camera/pulley are no longer deferred — routes fail only with an explicit 503 when unreachable', async (t) => {
  // These are covered by the EXTENDED tests above; this test guards against a
  // regression back to a "not implemented" stub that would hide a reachable
  // backend behind a hard-coded error.
  const ROUTES = [
    ['GET', '/api/camera/info'],
    ['POST', '/api/camera/expose'],
    ['POST', '/api/camera/abort'],
    ['GET', '/api/pulley/status'],
    ['POST', '/api/pulley/deploy'],
    ['POST', '/api/pulley/home'],
  ];
  for (const [method, url] of ROUTES) {
    await t.test(`${method} ${url} -> 503 (not "not implemented")`, async () => {
      const res = method === 'GET'
        ? await request(app).get(url)
        : await request(app).post(url).send({});
      assert.equal(res.status, 503, `expected 503 for ${method} ${url}, got ${res.status}`);
      assert.doesNotMatch(res.body.error || '', /not implemented/i,
        `error for ${method} ${url} must NOT claim the route is not implemented`);
    });
  }
});

// ─── Control: the mount route still reports an explicit 503 ──────────────────

test('mount route reports explicit 503 instead of simulated data (control)', async () => {
  const res = await request(app).get('/api/status');
  assert.equal(res.status, 503);
  assert.equal(res.body.error, 'Mount controller unreachable');
});
