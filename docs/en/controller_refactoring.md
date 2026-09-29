# Mount Controller Refactoring Analysis
## Reducing Complexity and Increasing Stability

**Date:** 2026-09-28
**Main file:** [`src/controllers/mount_controller.cpp`](src/controllers/mount_controller.cpp) (8581 lines)

This document identifies the places where refactoring yields the greatest
return in **lower complexity** and **higher stability**. The analysis is based
on measurements (line counts, duplicated-pattern counts, class field counts) and
on the bugs previously fixed in this file (sections 6/6a of
[`tracking_controller_indi_analysis.md`](docs/en/tracking_controller_indi_analysis.md)).

## Deployment Status (after refactoring)

Completed phases (each verified by compiling `astro_mount_core`):

- **P1 — unit helpers:** `haGear()`/`decGear()`; removed the 23 repetitions of
  the `gear_ratio > 0.0 ? … : 360.0` fallback.
- **P6 — fields instead of `static`:** 12 local `static` variables (log
  counters, gamepad debounce) moved to `Impl` fields; also removed the
  "first press was swallowed" effect in the gamepad button debounce.
- **P2 — `computeSoftLimits()`:** pure, testable `computeSoftLimits()` function
  + `SoftLimitEvaluation` struct; `evaluateSoftLimits()` is now a thin wrapper
  (servo→telescope conversion + field writes).
- **P4 — tracking-loop split:** extracted `computeAltAzRates()`,
  `computeCasualRates()` (rate computation), `applyEquatorialCorrections()`
  (nutation/TPoint/refraction) and `trackingModeFactor()` (mode factor). The
  tracking loop shrank by ~430 lines.
- **P5 — `runSlewMonitor()`:** a shared slew monitor (watchdog, `targetReached`,
  verification, retry, timeout, finalization) extracted from `slewToEquatorial`
  and `slewToHorizontal` — ~240 duplicated lines removed.
- **P7 — `computeFlipTargets()`:** shared multi-turn-safe meridian-flip target
  computation used by both the automatic flip and the manual
  `executeMeridianFlip`; this also fixed the duplicate of bug #9 in the manual
  path.

The file shrank from 8581 to 8309 lines despite adding 8 named functions — a
result of consolidating the duplicates. Fully splitting `Impl` into separate
files/components remains future structural hygiene work, but the largest
duplications and hidden states have been eliminated.

---

## 1. Complexity Metrics

| Metric | Value | Risk |
|---|---|---|
| [`mount_controller.cpp`](src/controllers/mount_controller.cpp) | 8581 lines | very high |
| `MountController::Impl` class (entirely in the `.cpp`) | ~8150 lines | god class |
| Tracking loop (lambda inside `startTracking`) | ~1400 lines | one function does everything |
| Private `Impl` fields | 100+ | high state complexity |
| `gear_ratio > 0.0 ? … : 360.0` pattern | 23 occurrences | duplicated unit conversion |
| Soft-limit target validation (before motion) | 3 copies | risk of divergence |
| Slew loops (equatorial / horizontal) | 2 copies | ~400 duplicated lines |
| Astronomical corrections (equatorial vs alt-az/casual) | 2 copies | ~150 duplicated lines |
| Tracking-rate computation (ALT_AZ vs CASUAL) | 2 copies | ~100 duplicated lines |
| `static` locals inside methods | 12 occurrences | hidden state, thread-safety smell |

---

## 2. Main Complexity Hotspots

### 2.1 "God class" — `MountController::Impl`

The public interface [`MountController`](include/controllers/mount_controller.h)
is already a pimpl (the delegating methods start at line 8166), but all logic
lives in a single `Impl` class of ~8150 lines. The class is simultaneously
responsible for:

- the state machine (`SLEWING` / `TRACKING` / `MERIDIAN_FLIP` / …),
- the tracking loop (position integration, corrections, motor I/O),
- the meridian flip,
- soft limits and the deceleration zone,
- coordinate and unit conversions,
- TPoint/bootstrap calibration,
- gamepad, LX200, ephemeris, guider, HAL status, state/config.

**Stability impact:** every change touches one file with hundreds of fields; it
is hard to keep paths consistent (e.g. the multi-turn flip fix had to respect
both the `home_offset` and `raw_servo` conventions at once — see bug #9).

### 2.2 Monolithic tracking loop

The lambda run in `work_thread_` (from ~line 1906) sequentially contains:

1. HAL safety read,
2. environmental sensor read,
3. soft limits (`evaluateSoftLimits`),
4. position integration + Kalman,
5. ALT_AZ/CASUAL normalization,
6. astronomical corrections (nutation/TPoint/refraction) — for 3 mount types,
7. rate computation (ALT_AZ / CASUAL),
8. meridian-flip detection and execution,
9. snapshots + motor I/O (position mode / velocity mode / drift correction).

That is ~1400 lines in a single lambda with several nesting levels.
**Stability impact:** bugs such as "uncontrolled rotation" (case #9) live in one
nested branch and are hard to isolate with a test.

### 2.3 Duplicated blocks

Measured duplication (line numbers after the latest changes):

1. **Servo↔telescope conversion** — 23× the pattern
   `config_.mount_config.<axis>_params.gear_ratio > 0.0 ? gear_ratio : 360.0`
   (e.g. [`mount_controller.cpp:728`](src/controllers/mount_controller.cpp:728),
   [`mount_controller.cpp:1690`](src/controllers/mount_controller.cpp:1690),
   [`mount_controller.cpp:2221`](src/controllers/mount_controller.cpp:2221)).
   Repeating the same fallback 23 times gives 23 places to mix up `ha_gear` and
   `dec_gear` (an `axis2 / ha_gear` mistake happened historically).

2. **Soft-limit target validation before motion** — 3 copies:
   - equatorial slew loop (≈[`mount_controller.cpp:727`](src/controllers/mount_controller.cpp:727)),
   - horizontal slew loop (≈[`mount_controller.cpp:1197`](src/controllers/mount_controller.cpp:1197)),
   - `startTracking` (≈[`mount_controller.cpp:1688`](src/controllers/mount_controller.cpp:1688)).

3. **Slew loops** — `slewToEquatorial` and `slewToHorizontal` duplicate the
   watchdog, `targetReached` verification, retry, timeout and state finalization.

4. **Astronomical corrections** — the EQUATORIAL path (nutation/TPoint/refraction)
   and the ALT_AZ/CASUAL path repeat the same sequence in a different frame.

5. **Rate computation** — the ALT_AZ block and the CASUAL block repeat `omega`,
   `mode_factor`, the `cos(lat)`/`cos(alt)` guards, and rad→deg→servo conversion.

### 2.4 `static` locals

12 occurrences (e.g. [`mount_controller.cpp:3052`](src/controllers/mount_controller.cpp:3052),
[`mount_controller.cpp:3184`](src/controllers/mount_controller.cpp:3184),
[`mount_controller.cpp:3278`](src/controllers/mount_controller.cpp:3278),
[`mount_controller.cpp:4126`](src/controllers/mount_controller.cpp:4126)).
They serve as log counters and gamepad-button debounce. Problem: the state lives
globally (shared between instances), is not reset between sessions, and is
invisible in the class — making testing and diagnostics harder.

### 2.5 Magic numbers and unit conventions

Repeated many times: `15.0` (hours→degrees), `360.0` (full turn), `0.004178`
(sidereal rate), `1e-9` (tolerances), `89.5°` (zenith guard), `0.1°` (pole
guard). Without unit types (`ServoDegrees`, `TelescopeDegrees`, `Hours`) the
compiler cannot catch the mistakes that historically caused bugs A1/A3/C2 (from
the earlier tracking analysis).

---

## 3. Refactoring Proposals (by priority)

### P1 — Unit types and conversion helpers (low risk, high return)

Introduce lightweight types/helpers:

```cpp
struct ServoDegrees  { double value; };
struct TelescopeDegrees { double value; };
struct Hours { double value; };

double haGear()   const { return gear(ha_axis_params.gear_ratio); }
double decGear()  const { return gear(dec_axis_params.gear_ratio); }
TelescopeDegrees servoToTel(double servo, double gear);
ServoDegrees     telToServo(double tel, double gear, double home_offset);
```

This removes the 23 fallback repetitions and the whole class of "servo instead
of telescope" mistakes. It is a **behavior-preserving** refactor (pure
mechanics), easy to test.

### P2 — `SoftLimitEvaluator` (extract `evaluateSoftLimits`)

`evaluateSoftLimits` (≈[`mount_controller.cpp:7257`](src/controllers/mount_controller.cpp:7257))
mixes: unit conversion, HA/Dec folding, distance computation, warning/decel
zones, message building and rate-factor computation. Extract:

```cpp
struct SoftLimitResult {
    double distance_axis1, distance_axis2;
    bool warning, deceleration;
    double rate_factor;
    std::string message;
};
class SoftLimitEvaluator {
    SoftLimitResult evaluate(ServoDegrees a1, ServoDegrees a2) const;
};
```

Additionally, one shared `validateTargetLimits()` method replaces the 3 copies of
pre-motion validation (section 2.3, item 2).

**Effect:** one limits implementation instead of three; easy unit tests for the
boundaries (fold hysteresis, decel zone).

### P3 — `MeridianFlipController` (extract flip logic)

The flip logic (detection, feasibility check, targets, execution, completion) is
embedded in the tracking loop (≈[`mount_controller.cpp:2837`](src/controllers/mount_controller.cpp:2837)–3060).
Extract it into a separate class with explicit inputs/outputs:

```cpp
class MeridianFlipController {
    FlipDecision evaluate(double ha, ServoDegrees dec, ...);
    FlipTargets computeTargets(ServoDegrees a1, ServoDegrees a2, ...);
    bool completeIfReached(...);
};
```

This place produced bugs #9 and #10 — extracting it allows unit tests with
multi-turn positions (e.g. `axis2=5311889°`).

### P4 — `TrackingEngine` (split the tracking loop)

Split the ~1400-line lambda into named steps (the "I/O Block 1/2/3" comments
already mark the boundaries):

- `trackingIterationOnce(dt)` — orchestrator,
- `applyAstronomicalCorrections(...)` — one function for all mount types
  (parameterized by frame),
- `computeRates(...)` — shared for ALT_AZ/CASUAL with a conversion parameter,
- `sendMotorTargets(...)` — position/velocity mode + drift correction.

Additionally, unify the EQUATORIAL and ALT_AZ/CASUAL astronomical corrections:
both perform "convert to equatorial → nutation → TPoint → refraction → convert
back", differing only in the frame transform. This can be written as one
algorithm with a conversion functor.

### P5 — Shared `SlewExecutor` (unify the slew loops)

`slewToEquatorial` and `slewToHorizontal` share ~80% of their logic (thread join,
`refreshPositionsFromHAL`, watchdog, `targetReached` + retry, timeout, final
state). Extract a method template:

```cpp
bool executeSlew(ServoDegrees target1, ServoDegrees target2,
                 const SlewProfile& profile);
```

The concrete methods keep only the coordinate→servo-target conversion.

### P6 — Replace `static` locals with fields

Move the 12 `static` counters/debouncers into `Impl` fields (e.g.
`flip_log_counter_`, `pos_mode_logged_`, `refresh_log_counter_`,
`button_debounce_*`). This gives:
- state reset between sessions,
- safety with multiple instances,
- testability.

### P7 — Split `Impl` into components (target state)

After P1–P6, the natural split is:

- `TrackingEngine` (loop, corrections, rates),
- `MeridianFlipController`,
- `SoftLimitEvaluator`,
- `SlewExecutor`,
- `CoordinateTools` (units, fold, LST),
- `CalibrationService` (TPoint/bootstrap) — currently in
  [`src/models/tpoint_model.cpp`](src/models/tpoint_model.cpp:1263) and in `Impl`.

`Impl` becomes a thin coordinator (state + delegation), and each component is
testable in isolation.

---

## 4. Mapping Refactoring to Stability

| Refactor | Which historical bug it would prevent/mitigate |
|---|---|
| P1 unit types | A1/A3/C2 (servo in trig functions), `ha_gear`/`dec_gear` mix-ups |
| P2 SoftLimitEvaluator | "Hard limit exceeded" / false decel at multi-turn positions |
| P3 MeridianFlipController | #9 (multi-turn flip targets), #10 (flip near the pole) |
| P4 TrackingEngine | E1 (correction accumulation), uncontrolled rotation |
| P5 SlewExecutor | #7 (HA target a full turn off), slew timeouts |
| P6 fields instead of static | hidden state, session diagnostics |

Key principle: **testability = stability**. Today the tracking-loop and flip
logic is unreachable for unit tests without a full HAL. Extracting P2–P4 enables
regression tests using the numeric data from logs (e.g. the `axis2=5311889°`
case from the bug report).

---

## 5. Deployment Plan (low risk, incremental)

Each phase ends with a build (`cmake --build build --target astro_mount_core`)
and, where possible, test runs. Order from safest:

1. **Phase 0 — P1**: unit helpers + replace the 23 fallbacks. Pure mechanics,
   zero behavior change. Immediate noise reduction.
2. **Phase 1 — P2**: `SoftLimitEvaluator` + unify target validation (3 copies → 1).
3. **Phase 2 — P6**: move `static` locals to fields.
4. **Phase 3 — P3**: `MeridianFlipController` + multi-turn regression tests.
5. **Phase 4 — P4**: split the tracking loop into steps, unify corrections and
   rates.
6. **Phase 5 — P5**: shared `SlewExecutor`.
7. **Phase 6 — P7**: final split of `Impl` into components.

---

## 6. Risks and Mitigation

| Risk | Mitigation |
|---|---|
| Refactor changes numerical behavior | Phase 0 is purely mechanical; round-trip tests (`test_mount_coordinates`) + log-comparison tests |
| Extracting the loop breaks access to 100 fields | Components receive explicit inputs/outputs (structs), not a pointer to `Impl` |
| Flip/soft-limit regression | P2/P3 unit tests with data from real logs (multi-turn) |
| Large file hard to review | Each phase is a separate, small commit |

Recommendation: start with **Phase 0 (P1)** — lowest risk, immediate removal of
23 repetitions, and groundwork for the rest.
