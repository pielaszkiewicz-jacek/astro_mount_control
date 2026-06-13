# CANopen Protocol Compliance Audit

> **Date**: 2026-06-11
> **Last Updated**: 2026-06-11 (fixes applied)
> **Scope**: CiA 301 (CANopen Application Layer) + CiA 402 (Drive Profile)
> **Layers Audited**:
> 1. C library — `lib/canopen_wrapper/src/canopen.cpp` (L1)
> 2. C++ wrapper — `src/controllers/canopen_interface.cpp` (L2)
> 3. HAL layer — `src/hal/canopen_hal/canopen_hal.cpp` (L3)

---

## Summary

| Severity | Count | Fixed | Remaining |
|----------|-------|-------|-----------|
| 🔴 CRITICAL | 3 | 3 | 0 |
| 🟠 HIGH | 6 | 3 | 3 |
| 🟡 MEDIUM | 5 | 4 | 1 |
| 🟢 LOW | 4 | 2 | 2 |
| **Total** | **18** | **12** | **6** |

---

## 🔴 CRITICAL Issues

### ✅ C1. SDO abort codes not parsed — FIXED

**File**: [`lib/canopen_wrapper/src/canopen.cpp`](lib/canopen_wrapper/src/canopen.cpp:308)
**Spec**: CiA 301 §7.2.10.7 (SDO Abort Transfer Protocol)

When an SDO response has bit 0 of the first byte set to 1 (abort), bytes 4–7 contain a 32-bit abort code (e.g., `0x06090030` = value range exceeded, `0x05040000` = timeout). The current implementation only checks `(resp.data[0] & 0x01) == 0` and returns `false` — the abort code is silently discarded.

**Impact**: When drives reject an SDO request, the cause is completely invisible. All SDO failures appear identical, making hardware debugging impossible.

**Fix applied**: Added `log_sdo_abort()` function with full switch statement parsing all 14 standard SDO abort codes. Both `canopen_sdo_write_expedited()` and `canopen_sdo_read_expedited()` now parse abort codes from bytes 4–7 of the response and call `log_sdo_abort()`. Also added 14 SDO abort code constants to canopen.h.

**⚠️ Regression fixed (2026-06-11)**: The initial fix used `(resp.data[0] & 0x01) != 0` to detect aborts, which checks **bit 0** (the `s`/size indicator bit). Per CiA 301 §7.2.10.7, the SDO abort protocol is identified by SCS (bits 7-5) = 4 (binary 100), making byte 0 = `0x80`. Valid upload responses like `0x43` (0100 0011 = CANOPEN_SDO_CMD_READ_RESP_4) have bit 0 = 1, causing ALL successful SDO reads to be falsely flagged as aborts. The correct check is `(resp.data[0] & 0xE0) == 0x80`. Both functions were corrected and the `CANOPEN_SDO_CMD_ABORT` (0x80) constant was added to [`canopen.h`](lib/canopen_wrapper/include/canopen/canopen.h:128).

### ✅ C2. `createMotorControl()` / `createEncoderReader()` create duplicate instances — FIXED

**File**: [`src/hal/canopen_hal/canopen_hal.cpp`](src/hal/canopen_hal/canopen_hal.cpp:1223)

```cpp
// Stores in motors_[axis_id] — but never returns it
motors_[axis_id] = std::make_unique<CanOpenMotor>(...);
// Returns a BRAND NEW instance instead
return std::make_unique<CanOpenMotor>(axis_id, *canopen_interface_);
```

The factory methods create a **second** `CanOpenMotor`/`CanOpenEncoder` instance with its own control thread, PID controller, and configuration. The stored `motors_[axis_id]` runs a useless redundant control loop. The same pattern exists in `createEncoderReader()` (line 1243) and `createSafetyMonitor()` (line 1263).

**Impact**: Two motor control loops run for each axis — the stored one (useless) and the returned one (used). Configuration applied to the factory-created instance is lost.

**Fix applied**: Factory methods (`createMotorControl()`, `createEncoderReader()`, `createSafetyMonitor()`, `createSensorInterface()`) now create new instances each time and return them directly. Removed stored `motors_[]`, `encoders_[]`, and `safety_monitor_` array members from `canopen_hal.h`. Fixed safety monitor double-initialization crash by removing pre-initialization from `createSafetyMonitor()`.

### ✅ C3. Emergency objects silently discarded — FIXED

**File**: [`lib/canopen_wrapper/src/canopen.cpp`](lib/canopen_wrapper/src/canopen.cpp:124)
**Spec**: CiA 301 §7.2.7 (Emergency Object)

Emergency objects use COB-IDs `0x081`–`0x0FF` (0x080 + node_id). The reader thread filters for:
- Heartbeat: `0x700`–`0x77F`
- TPDO1: `0x180`–`0x1FF`
- TPDO2: `0x280`–`0x2FF`

Emergency frames (`0x080`–`0x0FF`) fall through ALL filters and are dropped with a `pthread_mutex_unlock` with zero processing. A drive's emergency condition (over-voltage, over-current, following error) goes completely undetected.

**Impact**: Critical drive fault conditions are invisible. No emergency error callback mechanism exists.

**Fix applied**: Extended the context struct with `canopen_emergency_t last_emergency[MAX_CANOPEN_NODES]` and `emcy_cb`/`emcy_userdata`. Reader thread now detects emergency frames (COB-ID `0x080`-`0x0FF`, checked via `(cob_id & ~0x7F) == CANOPEN_COBID_EMERGENCY_BASE`), stores emergency data in `last_emergency[node]`, and notifies via `emcy_cb` outside the sock_mutex lock. Added `canopen_set_emergency_callback()` and `canopen_get_emergency()` API functions. Added 9 Emergency error code constants to canopen.h.

---

## 🟠 HIGH Issues

### ❌ H1. Simulated heartbeat instead of real heartbeat reception — NOT FIXED

**File**: [`src/hal/canopen_hal/canopen_hal.cpp`](src/hal/canopen_hal/canopen_hal.cpp:1594)
**Spec**: CiA 301 §7.2.6.1 (Heartbeat Protocol)

The NMT monitoring thread does NOT listen for real heartbeat CAN frames (COB-ID `0x700`+node). Instead, it calls `canopen_interface_->getDriveStatus(i)` which performs a synchronous SDO read (0x6041) at every heartbeat interval. This adds significant CAN bus load because:
- Real heartbeat: 1 CAN frame per node per period (0 data bytes)
- This implementation: 2+ CAN frames per node per period (SDO request + response, 8 data bytes each)

The C library's reader thread (`canopen.cpp:106-113`) correctly receives heartbeat messages and calls `nmt_cb`, but the HAL layer completely ignores this mechanism and implements its own SDO-based polling.

**Impact**: 4×–8× more CAN bus traffic than necessary for NMT monitoring. SDO timeouts cause false positive "missed heartbeat" detections in mock mode.

### ❌ H2. "PDO receive thread" is actually SDO polling — NOT FIXED

**File**: [`src/hal/canopen_hal/canopen_hal.cpp`](src/hal/canopen_hal/canopen_hal.cpp:777)
**Spec**: CiA 301 §7.2.5 (Process Data Object)

`pdoReceiveThread()` calls `canopen_.getEncoderData()` which performs an SDO read of OD `0x6381`. This is synchronous polling via SDO, not event-driven PDO reception. Real PDO reception would:
1. Configure the drive's TPDO mapping (OD `0x1A00`) to include encoder position
2. Set the TPDO transmission type (OD `0x1800` sub 2) to event-driven or SYNC-driven
3. Handle incoming TPDO frames in the reader thread callback

**Impact**: "PDO" naming is misleading. Functionally works but adds unnecessary SDO overhead and doesn't follow the CANopen PDO model.

### ✅ H3. `CanOpenHAL::shutdown()` doesn't shutdown CANopen interface — FIXED

**File**: [`src/hal/canopen_hal/canopen_hal.cpp`](src/hal/canopen_hal/canopen_hal.cpp:1211)

```cpp
if (canopen_interface_) {
    // Proper shutdown would be here
}
```

The shutdown method explicitly leaves the CANopen interface alive. `canopen_interface_->shutdown()` is never called.

**Impact**: Memory leak of CANopen context and socket. Reader thread continues running. Socket stays open.

**Fix applied**: Added `canopen_interface_->shutdown()` and `canopen_interface_->disconnect()` to `CanOpenHAL::shutdown()`.

### ✅ H4. Inconsistent velocity modes between layers — FIXED

**File**: [`src/controllers/canopen_interface.cpp`](src/controllers/canopen_interface.cpp:358) vs [`src/hal/canopen_hal/canopen_hal.cpp`](src/hal/canopen_hal/canopen_hal.cpp:198)
**Spec**: CiA 402 §7.2.3 (Profile Velocity Mode), §7.2.6 (Velocity Mode)

| Layer | Mode | OD Index | Constant |
|-------|------|----------|----------|
| C++ wrapper (`canopen_interface.cpp:359`) | Profile Velocity (3) | `0x60FF` | `CIA402_OPMODE_PROFILE_VEL` |
| HAL layer (`canopen_hal.cpp:198`) | Velocity (2) | `0x6042` | literal `2` |

The C++ wrapper uses mode 3 (Profile Velocity) with OD `0x60FF`, while the HAL layer uses mode 2 (Velocity) with OD `0x6042`. If both layers issue velocity commands to the same drive, the mode gets switched back and forth, and velocity targets from the other mode become stale.

**Impact**: Velocity commands from different layers conflict. The drive's mode and target velocity OD may not match the caller's expectation.

**Fix applied**: Changed `CanOpenMotor::setVelocity()` to use Profile Velocity mode (3, `pv`) with OD `0x60FF` and profile acceleration (`0x6083`) / deceleration (`0x6084`), matching the C++ wrapper layer. Removed Velocity mode (2, `vl`) with OD `0x6042` and `0x6048`.

### ✅ H5. `stopAxis()` ineffective in Position mode — FIXED

**File**: [`src/controllers/canopen_interface.cpp`](src/controllers/canopen_interface.cpp:430)
**Spec**: CiA 402 §6.4 (State Machine)

`stopAxis()` writes velocity=0 to OD `0x60FF` (Target Velocity for Profile Velocity mode). This only works when the drive is in **Profile Velocity mode** (mode 3). If the drive was previously put into **Profile Position mode** (mode 1) by `setPositionTarget()`, writing zero to `0x60FF` has no effect on the ongoing position move — the drive continues moving to the target position.

Per CiA 402, stopping a position-mode move requires either:
- Setting the **Halt bit** (control word bit 8 = `0x0100`)
- Or switching to a different mode and then setting velocity to zero

**Impact**: `stopAxis()` fails silently when called after `setPositionTarget()` — the axis continues moving.

**Fix applied**: `stopAxis()` now reads the current mode of operation (OD `0x6061`). If in Profile Position mode (1), it sets the Halt bit (`0x0100` | `0x000F` = `0x010F`) in the control word, then clears it (`0x000F`) to keep the drive in Operation Enabled. If in Profile Velocity mode (3), it writes velocity=0 to `0x60FF` as before.

### ❌ H6. NMT monitoring creates excessive bus load — NOT FIXED

**File**: [`src/hal/canopen_hal/canopen_hal.cpp`](src/hal/canopen_hal/canopen_hal.cpp:1594)
**Spec**: CiA 301 §7.2.6.1

The monitoring thread calls `getDriveStatus()` → `canopen_402_get_status_word()` → SDO read (0x6041) which generates 2 CAN frames per node per check. With 3 nodes and a default heartbeat period of 100ms, this generates **60 CAN frames/second** for NMT monitoring alone. Real heartbeat monitoring generates **0 CAN frames** for the consumer (the producer sends 1 frame per node per period).

---

## 🟡 MEDIUM Issues

### ✅ M1. SDO expedited read incomplete validation — FIXED

**File**: [`lib/canopen_wrapper/src/canopen.cpp`](lib/canopen_wrapper/src/canopen.cpp:356)
**Spec**: CiA 301 §7.2.10.3

The read function only checks the SCS nibble (bits 0–3) of the response. It doesn't check:
- **e-bit** (bit 1): Must be 1 for expedited transfer. If 0, the response uses segmented transfer.
- **s-bit** (bit 0): Data size indicator. Must be 0 if data size ≠ 4.

If a drive responds with a segmented transfer (data > 4 bytes), the SCS check might coincidentally match but the data in bytes 4–7 would be incorrect.

**Fix applied**: Added e-bit (`(resp.data[0] & 0x02) == 0` → error, not expedited) and s-bit validation. The SCS nibble is now properly parsed for data size per CiA 301 §7.2.10.3: `(scs & 0x02) ? 0 : ((~scs & 0x01) ? 1 : 2)` for 1–3 byte transfers. Returns error if server responds with segmented transfer.

### ✅ M2. Heartbeat consumer is a no-op — FIXED

**File**: [`lib/canopen_wrapper/src/canopen.cpp`](lib/canopen_wrapper/src/canopen.cpp:253)
**Spec**: CiA 301 §7.2.6.1

```cpp
bool canopen_nmt_set_hb_consumer(canopen_ctx_t* ctx, uint8_t node_id,
                                 uint16_t heartbeat_ms) {
    (void)ctx; (void)node_id; (void)heartbeat_ms;
    return true;
}
```

This function should write to OD `0x1016` (Consumer Heartbeat Time) to configure the local device's heartbeat consumer. Without this, the local device's OD doesn't know which heartbeat messages to expect or what timeout to use.

**Fix applied**: Rewrote `canopen_nmt_set_hb_consumer()` to actually write OD `0x1016` sub 1 via `canopen_sdo_write_expedited()` with value `(node_id << 16) | heartbeat_ms`. Returns `false` if the SDO write fails.

### ✅ M3. Deadlock risk: callbacks under sock_mutex — FIXED

**File**: [`lib/canopen_wrapper/src/canopen.cpp`](lib/canopen_wrapper/src/canopen.cpp:109)
**Spec**: CiA 301 §7.2

The reader thread calls `pdo_cb` and `nmt_cb` while holding `sock_mutex`. If any of these callbacks attempt to perform an SDO operation (which tries to lock `sock_mutex`), the thread deadlocks against itself. This is a `pthread_mutex_t` (non-recursive) — the second lock attempt blocks forever.

**Fix applied**: All three callbacks (pdo_cb, nmt_cb, emcy_cb) now save the callback pointer and userdata while holding `sock_mutex`, then unlock the mutex BEFORE invoking the callback. This prevents deadlock if the callback calls back into any CANopen API function that needs `sock_mutex`.

### ❌ M4. `baud_rate` parameter silently ignored — NOT FIXED

**File**: [`lib/canopen_wrapper/src/canopen.cpp`](lib/canopen_wrapper/src/canopen.cpp:158)

`canopen_init()` accepts a `baud_rate` parameter but never uses it. SocketCAN requires the bitrate to be set via `ip link set can0 type can bitrate 1000000` before the socket is opened, but the API signature suggests this is handled internally. Passing a wrong bitrate gives no error.

### ✅ M5. `CanOpenMotor::enable()` doesn't send NMT Start — FIXED

**File**: [`src/hal/canopen_hal/canopen_hal.cpp`](src/hal/canopen_hal/canopen_hal.cpp:89)
**Spec**: CiA 301 §9.2.1, CiA 402 §6.4

The C library's `canopen_402_enable_drive()` sends NMT Start Remote Node before the CiA 402 enable sequence. The HAL's `CanOpenMotor::enable()` jumps straight to control word `0x0006` without ensuring the node is in NMT Operational state. Some drives reject control word SDOs when in NMT Pre-Operational state.

**Fix applied**: Added `canopen_.sendNMT(axis_id_, 0x01)` (NMT Start Remote Node) at the beginning of `CanOpenMotor::enable()`, before the CiA 402 control word sequence.

---

## 🟢 LOW Issues

### ✅ L1. Emergency object COB-ID constant is incomplete — FIXED

**File**: [`lib/canopen_wrapper/include/canopen/canopen.h`](lib/canopen_wrapper/include/canopen/canopen.h:109)

```c
#define CANOPEN_COBID_EMERGENCY          0x080
```

Emergency COB-ID is `0x080 + node_id` (e.g., node 1 → `0x081`). The constant defines only the base address and is never used in the code anyway.

**Fix applied**: Renamed to `CANOPEN_COBID_EMERGENCY_BASE`. Now used in the reader thread to detect emergency frames via `(cob_id & ~0x7F) == CANOPEN_COBID_EMERGENCY_BASE`.

### ✅ L2. NMT state constant naming — FIXED

**File**: [`lib/canopen_wrapper/include/canopen/canopen.h`](lib/canopen_wrapper/include/canopen/canopen.h:36)

```c
#define CANOPEN_NMT_STATE_OP_PREOP       0x7F
```

The constant name `OP_PREOP` is confusing — it should be `PRE_OPERATIONAL` to match the CiA 301 specification terminology.

**Fix applied**: Renamed to `CANOPEN_NMT_STATE_PRE_OPERATIONAL`.

### ❌ L3. No segmented SDO transfer support — NOT FIXED

**File**: [`lib/canopen_wrapper/src/canopen.cpp`](lib/canopen_wrapper/src/canopen.cpp:267)
**Spec**: CiA 301 §7.2.10.6

Only expedited transfers (1–4 bytes) are supported. Segmented transfers for larger data (e.g., PDO mapping configuration, firmware upload) are not implemented. The API silently returns `false` for `len > 4`.

### ❌ L4. No SYNC-driven PDO support — NOT FIXED

**File**: [`src/controllers/canopen_interface.cpp`](src/controllers/canopen_interface.cpp:715)
**Spec**: CiA 301 §7.2.3, §7.2.5

The SYNC message is sent but no PDO configuration uses SYNC-triggered transmission. TPDOs could be configured for synchronous transmission (transmission type 1–240) for deterministic sampling.

---

## Compliance by Specification Section (Updated)

| CiA 301 Section | Feature | Status | Issue |
|-----------------|---------|--------|-------|
| §7.2.3 | SYNC protocol | ⚠️ Partial | Sent but no PDO synchronization |
| §7.2.4 | NMT protocol | ✅ Good | All commands, state tracking |
| §7.2.5 | PDO protocol | ❌ Poor | No RPDO tx, no mapping config |
| §7.2.6.1 | Heartbeat monitoring | ⚠️ Simulated | SDO polling instead of real HB |
| §7.2.6.2 | Node/Life Guarding | ❌ Missing | No RTR support |
| §7.2.7 | Emergency object | ✅ Fixed | Now received, stored, callbacks |
| §7.2.10.1 | SDO protocol | ⚠️ Partial | Expedited only |
| §7.2.10.3 | SDO expedited read | ✅ Fixed | e-bit, s-bit validated |
| §7.2.10.5 | SDO toggle bit | ❌ Missing | Not implemented |
| §7.2.10.6 | Segmented SDO | ❌ Missing | Not implemented |
| §7.2.10.7 | SDO abort | ✅ Fixed | Abort codes parsed + logged |
| §9.2.1 | NMT commands | ✅ Good | All 5 commands supported |
| §9.2.2 | NMT state machine | ✅ Fixed | Heartbeat consumer writes OD |

| CiA 402 Section | Feature | Status | Issue |
|-----------------|---------|--------|-------|
| §6.1.2 | Control word | ✅ Good | All bits mapped |
| §6.1.3 | Status word | ✅ Good | All bits mapped |
| §6.4 | State machine | ✅ Fixed | NMT Start + Fault recovery + Halt bit |
| §7.2.3 | Profile Velocity Mode | ✅ Good | Mode 3 via 0x60FF (both layers unified) |
| §7.2.6 | Velocity Mode | ❌ Removed | Replaced by Profile Velocity Mode |
| §7.2.8 | Torque Mode | ✅ Good | Mode 4 via 0x6071 |
| §7.2.9 | Homing Mode | ❌ Missing | Not implemented |
| §7.3 | Interpolated Position | ❌ Missing | Not implemented |

---

## Recommended Fix Priority (Remaining)

1. **🟠 H1/H6** — Wire heartbeat from C library reader thread up through all layers (reduces bus load from 60+ frames/sec to 0)
2. **🟠 H2** — Either implement real PDO reception or rename "PDO" methods to clarify SDO polling
3. **🟡 M4** — Either implement baud rate setting via SocketCAN or remove the parameter from the API
4. **🟢 L3** — Add segmented SDO transfer support (for objects > 4 bytes)
5. **🟢 L4** — Add SYNC-driven PDO configuration
