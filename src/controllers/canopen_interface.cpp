// CANopen/CiA 402 concrete interface — NiMotion STMP42SXI over SocketCAN.

#ifndef __linux__
#error "CANopen interface requires Linux (SocketCAN)."
#endif

#include "controllers/canopen_interface.h"

#include <chrono>
#include <cstring>
#include <thread>

namespace astro_mount {
namespace controllers {

namespace {

// CiA 402 control-word values.
constexpr uint16_t kCwDisableVoltage = 0x0000;
constexpr uint16_t kCwShutdown       = 0x0006;
constexpr uint16_t kCwSwitchOn       = 0x0007;
constexpr uint16_t kCwEnableOp       = 0x000F;
constexpr uint16_t kCwNewSetpoint    = 0x001F; // absolute, bit4=1
constexpr uint16_t kCwRelEnableOp    = 0x004F; // relative (bit6=1), enable op
constexpr uint16_t kCwRelNewSetpoint = 0x005F; // relative + new setpoint (bit4=1)
constexpr uint16_t kCwFaultReset     = 0x0080;

// Status-word bits.
constexpr uint16_t kSwFault          = 0x0008;
constexpr uint16_t kSwTargetReached  = 0x0400;

// CiA 402 operation modes (6060h).
constexpr uint8_t kModeProfilePosition = 0x01;
constexpr uint8_t kModeProfileVelocity = 0x03;
constexpr uint8_t kModeHoming          = 0x06;

// Object dictionary.
constexpr uint16_t kOdControlWord      = 0x6040;
constexpr uint16_t kOdStatusWord       = 0x6041;
constexpr uint16_t kOdModesOfOperation = 0x6060;
constexpr uint16_t kOdModesDisplay     = 0x6061;
constexpr uint16_t kOdPosDemand        = 0x6062;
constexpr uint16_t kOdPosActualUser    = 0x6064;
// Velocity actual: use 6069h (Velocity sensor value, UserUnit/s) — 606Ch
// (Velocity actual value) is in rpm on NiMotion STM42/STMP42SXI and would be
// mis-scaled by the counts-per-degree division below.
constexpr uint16_t kOdVelActual        = 0x6069;
constexpr uint16_t kOdTargetPosition   = 0x607A;
constexpr uint16_t kOdProfileVelocity  = 0x6081;
constexpr uint16_t kOdProfileAccel     = 0x6083;
constexpr uint16_t kOdProfileDecel     = 0x6084;
constexpr uint16_t kOdTargetVelocity   = 0x60FF;
constexpr uint16_t kOdMotionProfileType = 0x6086; // 0 = linear, 3 = S-curve
constexpr uint16_t kOdVelMinAmount      = 0x6046; // sub 1 = min velocity (rpm)
constexpr uint16_t kOdCtrlModeSelec    = 0x2002;
constexpr uint16_t kOdNodeId           = 0x200C;
constexpr uint16_t kOdStoreParams      = 0x1010;

constexpr uint32_t kSaveAllSignature   = 0x65766173; // ASCII "save"

constexpr uint8_t kNodeIdSub   = 0x02;
constexpr uint8_t kBaudRateSub = 0x03;
constexpr uint8_t kCtrlModeSub = 0x01;

uint32_t accelToRate(uint32_t accel, const std::string& mode, uint32_t velocity) {
    if (mode == "time") {
        // "time": 6083h is the ramp time in ms from 0 to profile velocity.
        // Convert to acceleration rate [user units/s^2].
        if (accel == 0) return 0;
        const double t_ms = static_cast<double>(accel);
        const double v = static_cast<double>(velocity);
        return static_cast<uint32_t>(v * 1000.0 / t_ms);
    }
    return accel; // "rate": already an acceleration rate
}

} // namespace

CanOpenInterface::CanOpenInterface() {
    std::memset(&ctx_, 0, sizeof(ctx_));
    ctx_.sock_fd = -1;
    std::memset(mode_cache_, 0xFF, sizeof(mode_cache_));
}

CanOpenInterface::~CanOpenInterface() {
    shutdown();
}

bool CanOpenInterface::initialize(const Config& config) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (initialized_) return true;
    config_ = config;
    if (!canopen_init(&ctx_, config.interface_name.c_str(),
                      config.sdo_timeout_ms * 1000)) {
        return false;
    }
    initialized_ = true;
    return true;
}

void CanOpenInterface::shutdown() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (initialized_) {
        canopen_shutdown(&ctx_);
        initialized_ = false;
    }
}

bool CanOpenInterface::isInitialized() const {
    return initialized_;
}

uint8_t CanOpenInterface::nodeIdForAxis(uint8_t axis_id) const {
    if (axis_id < config_.axis_node_ids.size()) {
        return config_.axis_node_ids[axis_id];
    }
    return static_cast<uint8_t>(config_.node_id + axis_id);
}

bool CanOpenInterface::writeSDO2(uint8_t node_id, uint16_t index, uint8_t subindex,
                                 uint16_t value) {
    return canopen_sdo_write_expedited(&ctx_, node_id, index, subindex, value, 2);
}

bool CanOpenInterface::writeSDO4(uint8_t node_id, uint16_t index, uint8_t subindex,
                                 uint32_t value) {
    return canopen_sdo_write_expedited(&ctx_, node_id, index, subindex, value, 4);
}

bool CanOpenInterface::writeControlWord(uint8_t node_id, uint16_t value) {
    return writeSDO2(node_id, kOdControlWord, 0x00, value);
}

bool CanOpenInterface::readStatusWord(uint8_t node_id, uint16_t& value) {
    uint32_t v = 0;
    uint8_t size = 0;
    if (!canopen_sdo_read_expedited(&ctx_, node_id, kOdStatusWord, 0x00, &v, &size)) {
        return false;
    }
    value = static_cast<uint16_t>(v);
    return true;
}

bool CanOpenInterface::writeMode(uint8_t node_id, uint8_t mode) {
    return canopen_sdo_write_expedited(&ctx_, node_id, kOdModesOfOperation,
                                       0x00, mode, 1);
}

bool CanOpenInterface::switchMode(uint8_t node_id, uint8_t mode) {
    if (node_id < 128 && mode_cache_[node_id] == mode) return true;

    // STMP42SXI requires the drive to be deactivated before 6060h changes.
    writeControlWord(node_id, kCwDisableVoltage);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    if (!writeMode(node_id, mode)) return false;
    if (node_id < 128) mode_cache_[node_id] = mode;

    // Minimum velocity (6046h:01h): re-assert 0 rpm while the drive is still
    // disabled.  The factory default (10 rpm = 60°/s servo) would otherwise
    // clamp slow profile velocities (sidereal tracking ≈ 0.25 rpm) and make the
    // axis run ahead of the commanded rate after a switch to Profile Velocity.
    writeSDO4(node_id, kOdVelMinAmount, 0x01, 0);

    // Re-enable: shutdown -> switch on -> enable operation.
    writeControlWord(node_id, kCwShutdown);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    writeControlWord(node_id, kCwSwitchOn);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    writeControlWord(node_id, kCwEnableOp);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    return true;
}

bool CanOpenInterface::sendSDO(uint8_t axis_id, uint16_t index, uint8_t subindex,
                               uint32_t value, uint8_t size) {
    const uint8_t node = nodeIdForAxis(axis_id);
    return canopen_sdo_write_expedited(&ctx_, node, index, subindex, value, size);
}

bool CanOpenInterface::readSDO(uint8_t axis_id, uint16_t index, uint8_t subindex,
                               uint32_t& value, uint8_t& size) {
    const uint8_t node = nodeIdForAxis(axis_id);
    return canopen_sdo_read_expedited(&ctx_, node, index, subindex, &value, &size);
}

bool CanOpenInterface::sendNMT(uint8_t axis_id, uint8_t command) {
    const uint8_t node = nodeIdForAxis(axis_id);
    return canopen_nmt_send(&ctx_, node, command);
}

bool CanOpenInterface::enableDrive(uint8_t axis_id) {
    const uint8_t node = nodeIdForAxis(axis_id);

    // STMP42SXI executes motion only in NMT Operational state.
    canopen_nmt_send(&ctx_, node, CANOPEN_NMT_START);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // 2002h:01h and 6060h must be written while the drive is DISABLED.
    // If the drive was left enabled (e.g. previous session), writing 2002h:01h
    // returns SDO abort 0x08000000 — disable first to avoid it.
    writeControlWord(node, kCwDisableVoltage);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // Ensure CiA402 mode + Profile Position mode (must be written while disabled).
    // 2002h:01h (CtrlModeSelec) is uint16 per object dictionary (10.2).
    if (!canopen_sdo_write_expedited(&ctx_, node, kOdCtrlModeSelec, kCtrlModeSub, 0, 2)) {
        return false;
    }
    writeMode(node, kModeProfilePosition);
    if (node < 128) mode_cache_[node] = kModeProfilePosition;

    // Minimum velocity (6046h:01h): factory default is 10 rpm (= 60°/s servo),
    // which clamps slow profile velocities. Set to 0 rpm so slow moves (e.g.
    // 0.15°/s) are executed at the commanded speed.
    if (!writeSDO4(node, kOdVelMinAmount, 0x01, 0)) {
        return false;
    }

    // Motion profile type (6086h): use linear/trapezoidal (0), NOT S-curve (3).
    // On NiMotion STM42/STMP42SXI drives, S-curve mode ignores the commanded
    // profile velocity (6081h) for slow moves and runs at the drive's minimum
    // speed (~20 rpm), which makes a 360° @ 1°/s step complete in a few seconds
    // instead of 360 s. Linear mode honours 6081h in user-unit/s (counts/s).
    // Written while the drive is disabled.
    if (!canopen_sdo_write_expedited(&ctx_, node, kOdMotionProfileType, 0x00, 0, 2)) {
        return false;
    }

    // CiA 402 enable sequence: shutdown -> switch on -> enable operation.
    if (!writeControlWord(node, kCwShutdown)) return false;
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    if (!writeControlWord(node, kCwSwitchOn)) return false;
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    if (!writeControlWord(node, kCwEnableOp)) return false;
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    uint16_t status = 0;
    if (!readStatusWord(node, status)) return false;
    // Operation enabled: bits 0..2 = 111, bit3 = 0, bit5 = 1, bit6 = 0.
    return ((status & 0x6F) == 0x27);
}

bool CanOpenInterface::disableDrive(uint8_t axis_id) {
    const uint8_t node = nodeIdForAxis(axis_id);
    return writeControlWord(node, kCwDisableVoltage);
}

bool CanOpenInterface::setPositionTarget(uint8_t axis_id, int32_t position,
                                         uint32_t velocity, uint32_t acceleration) {
    const uint8_t node = nodeIdForAxis(axis_id);
    const uint32_t accel = accelToRate(acceleration, config_.accel_mode, velocity);

    if (!switchMode(node, kModeProfilePosition)) return false;
    if (!writeSDO4(node, kOdTargetPosition, 0x00, static_cast<uint32_t>(position))) return false;
    if (!writeSDO4(node, kOdProfileVelocity, 0x00, velocity)) return false;
    if (!writeSDO4(node, kOdProfileAccel, 0x00, accel)) return false;
    if (!writeSDO4(node, kOdProfileDecel, 0x00, accel)) return false;

    // Trigger absolute move: clear bit4, then set it (rising edge).
    if (!writeControlWord(node, kCwEnableOp)) return false;
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    return writeControlWord(node, kCwNewSetpoint);
}

bool CanOpenInterface::setPositionTargetRelative(uint8_t axis_id, int32_t position,
                                                 uint32_t velocity, uint32_t acceleration) {
    const uint8_t node = nodeIdForAxis(axis_id);
    const uint32_t accel = accelToRate(acceleration, config_.accel_mode, velocity);

    if (!switchMode(node, kModeProfilePosition)) return false;
    if (!writeSDO4(node, kOdTargetPosition, 0x00, static_cast<uint32_t>(position))) return false;
    if (!writeSDO4(node, kOdProfileVelocity, 0x00, velocity)) return false;
    if (!writeSDO4(node, kOdProfileAccel, 0x00, accel)) return false;
    if (!writeSDO4(node, kOdProfileDecel, 0x00, accel)) return false;

    // Trigger relative move (bit6=1): enable op, then new setpoint (rising edge).
    if (!writeControlWord(node, kCwRelEnableOp)) return false;
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    return writeControlWord(node, kCwRelNewSetpoint);
}

bool CanOpenInterface::setVelocityTarget(uint8_t axis_id, int32_t velocity,
                                         uint32_t acceleration) {
    const uint8_t node = nodeIdForAxis(axis_id);
    const uint32_t accel = accelToRate(acceleration, config_.accel_mode,
                                       static_cast<uint32_t>(velocity < 0 ? -velocity : velocity));

    // Switch to Profile Velocity mode (handles deactivate → 6060h → re-enable).
    if (!switchMode(node, kModeProfileVelocity)) return false;
    if (!writeSDO4(node, kOdProfileAccel, 0x00, accel)) return false;
    if (!writeSDO4(node, kOdProfileDecel, 0x00, accel)) return false;
    return writeSDO4(node, kOdTargetVelocity, 0x00, static_cast<uint32_t>(velocity));
}

bool CanOpenInterface::stopAxis(uint8_t axis_id) {
    const uint8_t node = nodeIdForAxis(axis_id);
    // Shutdown: decelerate and keep the drive ready to switch on.
    return writeControlWord(node, kCwShutdown);
}

bool CanOpenInterface::emergencyStop(uint8_t axis_id) {
    const uint8_t node = nodeIdForAxis(axis_id);
    // Quick stop: bit2 = 0.
    return writeControlWord(node, kCwDisableVoltage);
}

bool CanOpenInterface::getDriveStatus(uint8_t axis_id, CanOpenStatus& status) {
    const uint8_t node = nodeIdForAxis(axis_id);
    uint16_t sw = 0;
    int32_t cached_pos = 0;
    bool have_cached = false;

    if (node < 128) {
        std::lock_guard<std::mutex> plock(pdo_mutex_);
        if (pdo_cache_[node].valid) {
            sw = pdo_cache_[node].status_word;
            cached_pos = pdo_cache_[node].position;
            have_cached = true;
        }
    }

    // PDO fast path (see docs/pl/propozycja_podzialu_watku_sledzenia.md §15.3/
    // §16.4): when TPDO1 telemetry is configured, mode (0x6061) and velocity
    // (0x606C) are deliberately NOT read through blocking SDO — an unresponsive
    // node cannot stall the caller for up to ~1.6 s.  The whole status then
    // comes from the TPDO1 cache.
    const bool fast = pdoFastPathEnabled();
    if (!have_cached) {
        // No fresh PDO telemetry yet: never block on SDO in fast mode, but keep
        // the legacy synchronous status read when the fast path is disabled.
        if (fast || !readStatusWord(node, sw)) {
            return false;
        }
    }

    status.status_word = sw;
    status.enabled = ((sw & 0x6F) == 0x27);
    status.fault = (sw & kSwFault) != 0;
    status.target_reached = (sw & kSwTargetReached) != 0;

    if (fast) {
        // Cache-only: position from TPDO1; mode/velocity intentionally omitted
        // (they are not carried by TPDO1 and must not trigger an SDO round-trip).
        status.actual_position = cached_pos;
        status.position_valid = true;
        status.moving = !status.target_reached && status.enabled;
        return true;
    }

    uint32_t v = 0;
    uint8_t size = 0;
    if (canopen_sdo_read_expedited(&ctx_, node, kOdModesDisplay, 0x00, &v, &size)) {
        status.mode_display = static_cast<uint8_t>(v);
    }
    if (have_cached) {
        status.actual_position = cached_pos;
        status.position_valid = true;
    } else if (canopen_sdo_read_expedited(&ctx_, node, kOdPosActualUser, 0x00, &v, &size)) {
        status.actual_position = static_cast<int32_t>(v);
        status.position_valid = true;
    }
    if (canopen_sdo_read_expedited(&ctx_, node, kOdVelActual, 0x00, &v, &size)) {
        status.actual_velocity = static_cast<int32_t>(v);
    }
    status.moving = !status.target_reached && status.enabled;
    return true;
}

bool CanOpenInterface::getPositionData(uint8_t axis_id, CanOpenPositionData& data) {
    const uint8_t node = nodeIdForAxis(axis_id);
    uint32_t v = 0;
    uint8_t size = 0;
    if (!canopen_sdo_read_expedited(&ctx_, node, kOdPosActualUser, 0x00, &v, &size)) {
        return false;
    }
    data.actual_position = static_cast<int32_t>(v);
    if (canopen_sdo_read_expedited(&ctx_, node, kOdPosDemand, 0x00, &v, &size)) {
        data.demand_position = static_cast<int32_t>(v);
    }
    if (canopen_sdo_read_expedited(&ctx_, node, kOdVelActual, 0x00, &v, &size)) {
        data.actual_velocity = static_cast<int32_t>(v);
    }
    return true;
}

bool CanOpenInterface::clearErrors(uint8_t axis_id) {
    const uint8_t node = nodeIdForAxis(axis_id);
    // Fault reset = rising edge of control-word bit7.
    if (!writeControlWord(node, kCwFaultReset)) return false;
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    return writeControlWord(node, kCwEnableOp);
}

bool CanOpenInterface::setNodeId(uint8_t axis_id, uint8_t new_node_id) {
    const uint8_t node = nodeIdForAxis(axis_id);
    if (new_node_id < 1 || new_node_id > 127) return false;
    if (!writeSDO2(node, kOdNodeId, kNodeIdSub, new_node_id)) return false;
    return saveParameters(axis_id);
}

bool CanOpenInterface::getNodeId(uint8_t axis_id, uint8_t& node_id) {
    const uint8_t node = nodeIdForAxis(axis_id);
    uint32_t v = 0;
    uint8_t size = 0;
    if (!canopen_sdo_read_expedited(&ctx_, node, kOdNodeId, kNodeIdSub, &v, &size)) {
        return false;
    }
    node_id = static_cast<uint8_t>(v);
    return true;
}

bool CanOpenInterface::setBaudRate(uint8_t axis_id, uint8_t baud_code) {
    const uint8_t node = nodeIdForAxis(axis_id);
    if (baud_code > 8) return false;
    return writeSDO2(node, kOdNodeId, kBaudRateSub, baud_code);
}

bool CanOpenInterface::saveParameters(uint8_t axis_id) {
    const uint8_t node = nodeIdForAxis(axis_id);
    return writeSDO4(node, kOdStoreParams, 0x01, kSaveAllSignature);
}

bool CanOpenInterface::setHeartbeatPeriod(uint8_t axis_id, uint16_t ms) {
    const uint8_t node = nodeIdForAxis(axis_id);
    return canopen_nmt_set_heartbeat(&ctx_, node, ms);
}

bool CanOpenInterface::setHomingParameters(uint8_t axis_id, uint8_t method,
                                           uint32_t speed_switch, uint32_t speed_zero,
                                           uint32_t acceleration) {
    const uint8_t node = nodeIdForAxis(axis_id);
    if (!canopen_sdo_write_expedited(&ctx_, node, 0x6098, 0x00, method, 1)) return false;
    if (!writeSDO4(node, 0x6099, 0x01, speed_switch)) return false;
    if (!writeSDO4(node, 0x6099, 0x02, speed_zero)) return false;
    if (!writeSDO4(node, 0x609A, 0x00, acceleration)) return false;
    return true;
}

bool CanOpenInterface::startHoming(uint8_t axis_id) {
    const uint8_t node = nodeIdForAxis(axis_id);

    canopen_nmt_send(&ctx_, node, CANOPEN_NMT_START);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // Homing mode (6060h = 6) must be selected while the drive is disabled.
    writeMode(node, kModeHoming);
    if (node < 128) mode_cache_[node] = kModeHoming;

    if (!writeControlWord(node, kCwShutdown)) return false;
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    if (!writeControlWord(node, kCwSwitchOn)) return false;
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    if (!writeControlWord(node, kCwEnableOp)) return false;
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Start homing: rising edge of control-word bit4.
    if (!writeControlWord(node, kCwEnableOp)) return false;
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    return writeControlWord(node, kCwNewSetpoint);
}

void CanOpenInterface::pdoCallback(uint32_t cob_id, const uint8_t* data,
                                   uint8_t dlc, void* userdata) {
    auto* self = static_cast<CanOpenInterface*>(userdata);
    if (!self) return;
    // TPDO1: 0x180 + node, payload = 6041h (2B) + 6064h (4B).
    if (cob_id < 0x180 || cob_id > 0x1FF || dlc < 6) return;
    const uint8_t node = static_cast<uint8_t>(cob_id - 0x180);
    if (node >= 128) return;

    const uint16_t sw = static_cast<uint16_t>(data[0] | (data[1] << 8));
    const int32_t pos =
        static_cast<int32_t>(data[2] | (data[3] << 8) |
                             (static_cast<uint32_t>(data[4]) << 16) |
                             (static_cast<uint32_t>(data[5]) << 24));
    {
        std::lock_guard<std::mutex> lock(self->pdo_mutex_);
        self->pdo_cache_[node].status_word = sw;
        self->pdo_cache_[node].position = pos;
        self->pdo_cache_[node].valid = true;
    }
    // Publish freshness outside the lock (release pairs with acquire readers).
    self->pdo_seq_[node].fetch_add(1, std::memory_order_release);
}

bool CanOpenInterface::configurePdo(uint8_t axis_id, bool enable) {
    const uint8_t node = nodeIdForAxis(axis_id);

    if (!enable) {
        // Disable TPDO1 and RPDO2 (bit31 = 1).
        writeSDO4(node, 0x1800, 0x01, 0x80000000u | (0x180 + node));
        writeSDO4(node, 0x1401, 0x01, 0x80000000u | (0x300 + node));
        if (node < 128) {
            pdo_configured_[node].store(false);
        }
        return true;
    }

    // The STMP42SXI/STM42 drives reject PDO mapping writes (SDO abort
    // 0x06010002 on 0x1A00/0x1601) when the objects are not writable in their
    // current state.  Every mapping write MUST succeed for the fast path to be
    // usable — otherwise RPDO2 frames are silently ignored by the drive and
    // position commands never reach it.  Track the result and only mark the
    // node as PDO-configured when the full mapping was accepted; otherwise the
    // command paths fall back to SDO.
    bool ok = true;

    // ── TPDO1: 6041h + 6064h ──────────────────────────────────────────────
    // Mapping sequence: set sub-0 = 0, write entries, then set sub-0 = count.
    ok = canopen_sdo_write_expedited(&ctx_, node, 0x1A00, 0x00, 0, 1) && ok;
    ok = writeSDO4(node, 0x1A00, 0x01, 0x60410010) && ok;
    ok = writeSDO4(node, 0x1A00, 0x02, 0x60640020) && ok;
    ok = canopen_sdo_write_expedited(&ctx_, node, 0x1A00, 0x00, 2, 1) && ok;

    ok = writeSDO4(node, 0x1800, 0x01, 0x180 + node) && ok;  // COB-ID
    ok = canopen_sdo_write_expedited(&ctx_, node, 0x1800, 0x02, 255, 1) && ok; // async
    ok = writeSDO2(node, 0x1800, 0x05, 10) && ok;            // event timer 10 ms

    // ── RPDO2: 6040h + 607Ah ──────────────────────────────────────────────
    ok = canopen_sdo_write_expedited(&ctx_, node, 0x1601, 0x00, 0, 1) && ok;
    ok = writeSDO4(node, 0x1601, 0x01, 0x60400010) && ok;
    ok = writeSDO4(node, 0x1601, 0x02, 0x607A0020) && ok;
    ok = canopen_sdo_write_expedited(&ctx_, node, 0x1601, 0x00, 2, 1) && ok;

    ok = writeSDO4(node, 0x1401, 0x01, 0x300 + node) && ok;
    ok = canopen_sdo_write_expedited(&ctx_, node, 0x1401, 0x02, 255, 1) && ok;

    canopen_set_pdo_callback(&ctx_, &CanOpenInterface::pdoCallback, this);

    if (node < 128) {
        pdo_velocity_mapping_[node].store(false);
        pdo_configured_[node].store(ok);
        if (ok) {
            last_profile_velocity_[node].store(0);
            last_profile_accel_[node].store(0);
        }
    }
    // Optional: map RPDO2 for velocity mode (6040h + 60FFh).
    if (ok && config_.pdo_velocity_mapping) {
        ok = configurePdoVelocityMapping(axis_id, true) && ok;
        if (node < 128) {
            pdo_configured_[node].store(ok);
        }
    }
    return ok;
}

bool CanOpenInterface::pollEvents() {
    return canopen_poll_events(&ctx_, 16) > 0;
}

bool CanOpenInterface::pdoFastPathEnabled() const {
    if (!config_.pdo_fast_path) return false;
    for (int a = 0; a < 2; ++a) {
        const uint8_t node = nodeIdForAxis(static_cast<uint8_t>(a));
        if (node < 128 && !pdo_configured_[node].load(std::memory_order_acquire)) {
            return false;
        }
    }
    return true;
}

bool CanOpenInterface::getCachedDriveStatus(uint8_t axis_id, CanOpenStatus& status) {
    const uint8_t node = nodeIdForAxis(axis_id);
    if (node >= 128) return false;

    uint16_t sw = 0;
    int32_t pos = 0;
    bool valid = false;
    {
        std::lock_guard<std::mutex> lock(pdo_mutex_);
        valid = pdo_cache_[node].valid;
        sw = pdo_cache_[node].status_word;
        pos = pdo_cache_[node].position;
    }
    if (!valid) return false;

    status = CanOpenStatus{};
    status.status_word = sw;
    status.enabled = ((sw & 0x6F) == 0x27);
    status.fault = (sw & kSwFault) != 0;
    status.target_reached = (sw & kSwTargetReached) != 0;
    status.actual_position = pos;
    status.position_valid = true;
    status.moving = !status.target_reached && status.enabled;
    return true;
}

namespace {
inline void packControlAndTarget(uint8_t* out, uint16_t cw, int32_t target) {
    out[0] = static_cast<uint8_t>(cw & 0xFF);
    out[1] = static_cast<uint8_t>((cw >> 8) & 0xFF);
    const uint32_t v = static_cast<uint32_t>(target);
    out[2] = static_cast<uint8_t>(v & 0xFF);
    out[3] = static_cast<uint8_t>((v >> 8) & 0xFF);
    out[4] = static_cast<uint8_t>((v >> 16) & 0xFF);
    out[5] = static_cast<uint8_t>((v >> 24) & 0xFF);
}
} // namespace

bool CanOpenInterface::setPositionTargetPdo(uint8_t axis_id, int32_t position,
                                            uint32_t velocity, uint32_t acceleration) {
    if (!pdoFastPathEnabled()) {
        return setPositionTarget(axis_id, position, velocity, acceleration);
    }
    const uint8_t node = nodeIdForAxis(axis_id);

    // If RPDO2 is currently mapped for velocity mode, switch back (slow path).
    if (pdo_velocity_mapping_[node].load(std::memory_order_acquire)) {
        if (!configurePdoVelocityMapping(axis_id, false)) return false;
    }
    if (!switchMode(node, kModeProfilePosition)) return false;

    // Profile velocity / acceleration change rarely; configure via SDO only when
    // they actually differ (slow control path), never per cycle.
    const uint32_t accel = accelToRate(acceleration, config_.accel_mode, velocity);
    if (last_profile_velocity_[node].load() != velocity ||
        last_profile_accel_[node].load() != accel) {
        if (!writeSDO4(node, kOdProfileVelocity, 0x00, velocity)) return false;
        if (!writeSDO4(node, kOdProfileAccel, 0x00, accel)) return false;
        if (!writeSDO4(node, kOdProfileDecel, 0x00, accel)) return false;
        last_profile_velocity_[node].store(velocity);
        last_profile_accel_[node].store(accel);
    }

    // RPDO2 payload = controlword (6040h) + target position (607Ah). Two frames:
    // bit4 = 0 then bit4 = 1 (rising "new setpoint" edge). Fire-and-forget.
    uint8_t f1[6];
    uint8_t f2[6];
    packControlAndTarget(f1, kCwEnableOp, position);
    packControlAndTarget(f2, kCwNewSetpoint, position);
    const uint32_t cob = 0x300u + node;
    if (!canopen_send_frame(&ctx_, cob, f1, 6)) return false;
    return canopen_send_frame(&ctx_, cob, f2, 6);
}

bool CanOpenInterface::setVelocityTargetPdo(uint8_t axis_id, int32_t velocity,
                                            uint32_t acceleration) {
    const uint8_t node = nodeIdForAxis(axis_id);
    if (!pdoFastPathEnabled() ||
        !pdo_velocity_mapping_[node].load(std::memory_order_acquire)) {
        return setVelocityTarget(axis_id, velocity, acceleration);
    }
    if (!switchMode(node, kModeProfileVelocity)) return false;

    const uint32_t accel =
        accelToRate(acceleration, config_.accel_mode,
                    static_cast<uint32_t>(velocity < 0 ? -velocity : velocity));
    if (last_profile_accel_[node].load() != accel) {
        if (!writeSDO4(node, kOdProfileAccel, 0x00, accel)) return false;
        if (!writeSDO4(node, kOdProfileDecel, 0x00, accel)) return false;
        last_profile_accel_[node].store(accel);
    }

    // RPDO2 payload = controlword (6040h) + target velocity (60FFh).
    uint8_t f[6];
    packControlAndTarget(f, kCwEnableOp, velocity);
    return canopen_send_frame(&ctx_, 0x300u + node, f, 6);
}

bool CanOpenInterface::configurePdoVelocityMapping(uint8_t axis_id, bool enable) {
    const uint8_t node = nodeIdForAxis(axis_id);

    // Remap RPDO2 (0x1601) to 6040h + 60FFh (controlword + target velocity).
    canopen_sdo_write_expedited(&ctx_, node, 0x1601, 0x00, 0, 1);
    writeSDO4(node, 0x1601, 0x01, 0x60400010);
    writeSDO4(node, 0x1601, 0x02, 0x60FF0020);
    canopen_sdo_write_expedited(&ctx_, node, 0x1601, 0x00, 2, 1);

    writeSDO4(node, 0x1401, 0x01, 0x300 + node);
    canopen_sdo_write_expedited(&ctx_, node, 0x1401, 0x02, 255, 1);

    if (node < 128) {
        pdo_velocity_mapping_[node].store(enable, std::memory_order_release);
    }
    return true;
}

} // namespace controllers
} // namespace astro_mount
