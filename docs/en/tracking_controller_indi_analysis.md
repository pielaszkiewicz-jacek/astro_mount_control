# Mathematical and Numerical Tracking Analysis
## Mount controller ↔ INDI driver

This document describes the full position-calculation chain from the physical
drive (MF7025v2) through the mount controller to the INDI driver, together with
the mathematical invariants, the numerical bugs that were found, and the
remaining risks.

---

## 1. Position Flow Architecture

```
drive (0x92 multi-turn, servo degrees)
   │  getActualPosition() / getActualVelocity()
   ▼
MF7025v2 HAL (invert_direction → transparent)
   │
   ▼
MountController::refreshPositionsFromHAL()
   │  raw_servo_axis* = pos + home_offset
   │  axis*_position_   = pos + home_offset  (when !tracking_active_)
   ▼
MountController::getStatus()
   │  raw_tel_axis* = raw_servo / gear_ratio
   │  fold Dec, normalize [0°, 360°)
   │  current_ra = LST − HA, current_dec = fold(Dec)
   ▼
gRPC GetState → ControllerState (current_ra, current_dec, telescope_axis1/2)
   │
   ▼
INDI: IndiPropertyMapper::toIndiRaDec()
   │  prefers current_ra/current_dec
   │  fallback: telescope_axis1/2 + its own LST
   ▼
INDI: setEquatorialCoords() → NewRaDec(JNow) + EQUATORIAL_J2000
```

Command path:

```
INDI: Goto / SetTrackEnabled
   │  toGrpcCoordinates() → Coordinates (JNow, no precession)
   ▼
gRPC SlewToCoordinates / TrackObject
   ▼
MountController::slewToEquatorial() / startTracking()
   │  HA = LST − RA (shortest path)
   │  Dec = resolveDecTarget() (pier side / 360°)
   │  axis*_target = telescope_degrees × gear − home_offset
   ▼
HAL setPosition() / setVelocity() → drive
```

---

## 2. Coordinate Systems and Transformations

### 2.1 Equatorial mount (EQUATORIAL)

Units:

- **Telescope degrees** — on-sky axis angles (HA, Dec).
- **Servo degrees** — motor shaft angles = telescope degrees × `gear_ratio`.
  For `gear_ratio = 360`: `1° telescope = 360° servo`.

Fundamental relations:

```
HA [h]  = LST [h] − RA [h]
RA [h]  = LST [h] − HA [h]
Dec [°] = Dec-axis angle (astronomically −90°…+90°)
```

### 2.2 Normalization and the Dec "fold"

In [`getStatus()`](src/controllers/mount_controller.cpp:3718) the Dec readback is
normalized:

```
if (Dec >  90°) Dec = 180° − Dec
if (Dec < −90°) Dec = −180° − Dec
Dec = fmod(Dec, 360);  if (Dec < 0) Dec += 360
```

Property: `fold(Dec)` maps the physical Dec into the astronomical range
`[−90°, 90°]`. Dec = 120° (past the pole) is equivalent to Dec = 60° on the
other side of the pier — the same sky position.

### 2.3 Time frames: JNow vs J2000

- The controller computes `HA = LST − RA` in the **apparent (JNow)** frame.
- [`IndiPropertyMapper::toGrpcCoordinates()`](indi/IndiPropertyMapper.cpp:22)
  passes coordinates through without precession (`apply_precession=false`,
  `epoch=0.0`).
- INDI reports `NewRaDec` in JNow, and the `EQUATORIAL_J2000` property is
  precessed (`jnowToJ2000`) only for display.

As a result there is no double JNow↔J2000 precession (~0.4° error).

---

## 3. Readback Path

### 3.1 MF7025v2 HAL

The motor position comes from the absolute multi-turn readback **0x92** (every
`absolute_position_poll_ms = 100 ms`). Zero is the **real position**, not an
"unknown". Velocity comes from **0x9C** (1 dps resolution).

`invert_direction` is transparent: it negates both the command and the readback,
so the two cancel logically (`getActualPosition()` returns the logical position).

### 3.2 Controller

[`refreshPositionsFromHAL()`](src/controllers/mount_controller.cpp:3890):

```cpp
raw_servo_axis* = pos + home_offset        // ALWAYS (authoritative)
axis*_position_  = pos + home_offset        // only when !tracking_active_
```

Important properties:

1. `raw_servo_axis*` has a **single writer** — the HAL. The tracking loop does
   not overwrite it on real hardware (fix).
2. During tracking, `axis*_position_` belongs to the tracking loop.

[`getStatus()`](src/controllers/mount_controller.cpp:3706) computes:

```cpp
raw_tel_axis* = raw_servo / gear
current_ra    = LST − HA          (from raw_tel_axis1)
current_dec   = fold(raw_tel_axis2)
```

For the MF7025V2/SIMULATED HAL, zero is not replaced by the park position
([`halPositionIsAuthoritative()`](src/controllers/mount_controller.cpp:7255)).

### 3.3 INDI

[`IndiPropertyMapper::toIndiRaDec()`](indi/IndiPropertyMapper.cpp:114) prefers
`current_ra/current_dec` (corrected by the controller); the `telescope_axis1/2`
fallback is used only when both values are exactly zero.

---

## 4. Command Path (slew / track)

### 4.1 `slewToEquatorial()` / `startTracking()`

```
ha_hours  = LST − ra                    // [−12, +12]
current_ha_hours = axis1_position_ / (gear × 15)
ha_delta  = wrap(ha_hours − current_ha_hours, ±12)
ha_hours  = current_ha_hours + ha_delta  // shortest path
axis1_target = ha_hours × 15 × gear − home_offset_axis1

dec_resolved = resolveDecTarget(dec, axis2_position_/gear)
axis2_target = dec_resolved × gear − home_offset_axis2
```

[`resolveDecTarget()`](src/controllers/mount_controller.cpp:7269) selects the Dec
equivalent nearest to the current physical position from:

```
{d, 180−d, d−360, d+360, 180−d−360, 180−d+360}
```

### 4.2 Tracking Loop

- **Position mode** (default, `equatorial_tracking_velocity_mode=false`):
  every ~50 iterations it sends `setPosition(new_HA_target, constant_Dec_target)`.
  HA target = `LST − RA_target` + ~2 s lead. Dec target = the resolved target
  from `startTracking()` (constant — the Dec of the object does not change during
  sidereal tracking).
- **Velocity mode** (experimental): `setVelocity(sidereal_rate × gear)` with
  drift correction.

Sidereal rate (telescope): `0.004178074 °/s`; in servo: `× gear`.

---

## 5. Mathematical Invariants (round-trip)

**Key property:** a reported position sent back as a target must produce zero
motion.

### 5.1 HA/RA axis

Report: `ra = LST − HA`.
Command: `HA_target = wrap(LST − ra − current_HA, ±12) + current_HA`.

Because `current_HA = axis1_position_/(gear×15)` is the same value from which the
report was computed (when not tracking), `LST − ra = HA`, so
`HA_target = HA`. ✓ (mod 24 h, handled by the wrap).

### 5.2 Dec axis

Report: `d = fold(p)`.
Command: `resolveDecTarget(d, p)`.

Theorem: `resolveDecTarget(fold(p), p) = p` for `p ∈ [−180°, 180°]`.

Proof (by cases):

| Range of p     | d = fold(p)  | candidate equal to p   |
|----------------|--------------|------------------------|
| [−90, 90]      | p            | `d` (= p)              |
| (90, 180]      | 180 − p      | `180 − d` (= p)        |
| [−180, −90)    | −180 − p     | `180 − d − 360` (= p)  |

In every case a candidate with distance **0** exists, so it is chosen as the
minimum. ✓

Conclusion: both axes close the loop — "Use current" + "Slew and Track" is a
no-op.

---

## 6. Detected and Fixed Numerical Problems

| # | Problem | Cause | Fix |
|---|---------|-------|-----|
| 1 | Position jumps between 2+ values | `raw_servo_axis*` written simultaneously by the tracking loop and `refreshPositionsFromHAL()` | Tracking loop writes `raw_servo` only for simulation ([`mount_controller.cpp`](src/controllers/mount_controller.cpp:2020)) |
| 2 | Reported Dec=90° instead of physical 0° | Initialization from the park position (32400° servo) and fallback in `getStatus()` | [`halPositionIsAuthoritative()`](src/controllers/mount_controller.cpp:7255) — honor the physical zero |
| 3 | Dec axis spun / target 60–180° off | Dec fold on readback, no closure in the command | [`resolveDecTarget()`](src/controllers/mount_controller.cpp:7269) + constant Dec target in the tracking loop |
| 4 | Position drifted 2× during motion | Integrating `velocity × 0.1 s` with a 50 ms poll | Integrate with the real `dt` ([`mf7025v2_hal.cpp`](src/hal/mf7025v2_hal/mf7025v2_hal.cpp:550)) |
| 5 | Tracking the Goto target instead of the current position | `SetTrackEnabled` used `m_targetRA/Dec` | Track the current position ([`astro_mount_driver.cpp`](indi/astro_mount_driver.cpp:1069)) |
| 6 | Double axis inversion | `invert_direction` (HAL) × `invert_axis` (controller) | Removed from configuration (`false`) |
| 7 | HA target a full revolution (24 h) off | No shortest path | ±12 h wrap in `slewToEquatorial`/`startTracking` |
| 8 | Invalid Dec (>90°) from the client | No input validation | Reject + log ([`mount_controller.cpp`](src/controllers/mount_controller.cpp:551)) |
| 9 | Continuous rotation / drive-away after "Use current" + "Slew and Track" (Dec target `−5247089°` ≈ 40 revolutions) | Meridian-flip target computed from the **raw multi-turn position**: `180°·gear − axis2_target_` (and the HA target normalized to `[−180°, 180°]` instead of relative to the current position) | Flip targets computed as the shortest path from the current physical position, preserving the multi-turn window ([`mount_controller.cpp:2911`](src/controllers/mount_controller.cpp:2911)) |
| 10 | Flip triggered near the pole (Dec 89.75° → complement 90.25° outside the ±90° limit) | No feasibility check of the flip against the Dec limits | Flip skipped when the complemented Dec (`180°−Dec`) falls outside `soft_limit_axis2_min/max` ([`mount_controller.cpp:2867`](src/controllers/mount_controller.cpp:2867)) |
| 11 | Position jump at the start of a new tracking session | Delta-correction state `last_*_correction_*` (nutation/TPoint/refraction) not reset between sessions | Zero the 6 variables at tracking start ([`mount_controller.cpp:1885`](src/controllers/mount_controller.cpp:1885)) |

---

## 6a. Mapping of Observed Symptoms to Causes

All symptoms reported during testing map to the table above. Direct mapping:

| Observed symptom | Cause (table #) | Status |
|---|---|---|
| "Two different positions alternating" | #1 `raw_servo` write conflict | fixed |
| "Position = last calibration object" | #5 Goto target instead of current position + frame drift (#1–#3) | fixed |
| "Drive-away after double Slew and Track" | #2 park 90° + #3 Dec fold + #7 no HA shortest path | fixed |
| "Position limit error (axis2=134°)" | #3 Dec fold + #8 no Dec validation | fixed |
| "Uncontrolled rotation" | #3 Dec axis spun (fold without closure in the tracking loop) | fixed |
| "Mount↔INDI coordinate frame drift" | #1/#2/#3 — broken round-trip invariant | fixed (section 5) |
| "Axis inversion in configuration" | #6 double inversion (`invert_direction` × `invert_axis`) | removed from configuration |
| "Continuous rotation driving away from the current position" | #9 flip target from the raw multi-turn position + #10 flip triggered near the pole | fixed |
| "Dec axis travels dozens of revolutions after a flip" | #9 Dec complement on the raw multi-turn value | fixed |
| "Flip triggered for an object near the pole" | #10 no flip feasibility check | fixed |
| "Small position jump after enabling tracking" | #11 stale delta-correction state from the previous session | fixed |

---

## 7. Remaining Risks and Edge Cases

**Deployment status:** items 1 (polar singularity), 4 (home offset) and 6
(double precision) are handled in code; items 2 (fold-boundary hysteresis),
3 (single LST source) and 5 (Kalman synchronization) remain **open**
recommendations from section 8. In addition, problems #9 (multi-turn
normalization of meridian-flip targets), #10 (skipping a flip when the Dec
complement falls outside the limits) and #11 (zeroing the delta-correction state
at tracking start) are handled — all described in sections 6 and 6a.

1. **Polar singularity (|Dec| ≈ 90°).** RA is undefined at the pole; the code has
   the `NUTATION_POLE_GUARD_DEG = 0.1°` guard, but the general precision near the
   pole is inherently limited.

2. **Dec fold boundary at 90°.** At exactly 90° the fold is the identity, but
   numerically near the boundary small readback fluctuations can flip Dec between
   89.999° and 90.001° (a convention jump, not a physical position jump).

3. **LST consistency.** The controller uses `calculateLST(jd, config.longitude)`;
   INDI uses its own `computeLst()` in the fallback (longitude from
   `updateLocation`). With divergent location configuration the fallback RA will
   be shifted. The main path (`current_ra/dec`) does not depend on INDI's LST.

4. **Home offset.** `axis_target = telescope × gear − home_offset`, while the
   readback is `raw_servo = pos + home_offset`. The Home operation must be
   consistent — with a mismatched `home_offset` the Dec axis can "run away"
   (guarded by the closure from section 5.2).

5. **Kalman filter.** Updated only in the tracking loop; with real HAL in
   position mode `axis*_position_` is the internal source, while the physical
   position goes to `raw_servo`. The divergence between them grows over time
   (PID drift), but is negligible in the short term.

6. **Double precision and long tracking.** `axis1_position_` grows without
   normalization (comment in [`mount_controller.cpp`](src/controllers/mount_controller.cpp:2023));
   the 53-bit mantissa suffices for ~10 h of tracking at 1.5°/s servo without
   losing significant digits. The meridian flip now preserves the multi-turn
   window, so the accumulated position is never unwound by a flip.

---

## 8. Recommendations

1. **Single source of truth for position.** *(deployed)* [`getStatus()`](src/controllers/mount_controller.cpp:3767)
   and [`notifyStatusChanged()`](src/controllers/mount_controller.cpp:6935)
   report `axis1/2_position` exclusively from `raw_servo_axis*` (physical
   readback); `axis*_position_` remains purely the internal tracking-loop state.
2. **Synchronize `axis*_position_` with the HAL after tracking stops.**
   *(deployed)* [`startTracking()`](src/controllers/mount_controller.cpp:1517)
   and [`slewToEquatorial()`](src/controllers/mount_controller.cpp:574) call
   `refreshPositionsFromHAL()` after `joinWorkThread()`, so the next target is
   computed from the physical position.
3. **Single LST source.** *(deployed)* Shared header
   [`include/core/sidereal_time.h`](include/core/sidereal_time.h); the controller
   ([`calculateGMST`/`calculateLST`](src/core/astronomical_calculations.cpp:602))
   and INDI ([`computeLst()`](indi/IndiPropertyMapper.cpp:189)) delegate to one
   implementation.
4. **Round-trip tests.** *(deployed)* [`tests/test_mount_coordinates.cpp`](tests/test_mount_coordinates.cpp)
   checks `foldDec()` and `resolveDecTarget(foldDec(p), p) == p` (3 tests).
5. **Diagnostic logging.** *(deployed)* `slewToEquatorial: RA=... Dec=...`
   and `slewToEquatorial Dec: requested/current/resolved` allow quick
   identification of an incorrect target source.
6. **Meridian-flip target safety.** *(deployed)* Flip targets are computed as a
   shortest-path delta from the current physical position (preserving the
   multi-turn window), and the flip is skipped when the complemented Dec would
   exceed the configured Dec limits ([`mount_controller.cpp:2867`](src/controllers/mount_controller.cpp:2867),
   [`mount_controller.cpp:2911`](src/controllers/mount_controller.cpp:2911)).

### 8.1 Practical Meaning (historical)

Items #1 and #3 are closed. The description below explains what they meant in
practice.

**#1 Single source of truth for position**

The controller keeps two position representations:

- `raw_servo_axis*` — the physical drive position (single writer:
  [`refreshPositionsFromHAL()`](src/controllers/mount_controller.cpp:3890));
  this is what INDI sees (`current_ra/current_dec`).
- `axis*_position_` — the internal position, updated by the tracking loop
  (velocity integration) and by the HAL (when not tracking); this is used for
  the HA shortest-path selection and the nutation/TPOINT corrections.

In practice, during long tracking these two values slowly diverge (PID lag,
backlash). The INDI report is already correct (from `raw_servo`), but the HA
equivalent selection and model corrections use the internal position, which can
be a fraction of a degree off. Real effect: after many-hour sessions a re-target
may be slightly shifted; in short tests it is imperceptible.

**#3 Single LST source**

There are two LST implementations: the controller ([`calculateLST()`](src/controllers/mount_controller.cpp:3800)
with `config.longitude`) and INDI ([`computeLst()`](indi/IndiPropertyMapper.cpp:189)
with the location provided by KStars). INDI's LST is used in only two places: the
fallback in [`toIndiRaDec()`](indi/IndiPropertyMapper.cpp:114) (when
`current_ra/dec` are exactly zero) and in [`addSyncMeasurement()`](indi/astro_mount_driver.cpp:1493)
for bootstrap measurements.

If the KStars location differs from `config.longitude`, RA in these two paths is
shifted by Δlongitude/15 h. Normally (when `current_ra/dec` are non-zero) the
impact is zero.

Recommendation: both points are hygiene/resilience — worth closing during a
larger refactoring, but they do not block operation.
