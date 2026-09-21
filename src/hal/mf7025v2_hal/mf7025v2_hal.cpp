// MF7025v2 HAL implementation — LingKong BLDC servo via proprietary CAN V2.36
#include "hal/mf7025v2_hal/mf7025v2_hal.h"
#include "hal/safety_monitor.h"
#include "hal/sensor_interface.h"
#include "logging/logger.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <sstream>
#include <thread>

using namespace astro_mount::hal;
using namespace astro_mount::controllers;

// Helper: obtain the mf7025v2 logger once per function to avoid
// re-declaration conflicts in functions that log multiple times.
#define MF7025V2_LOGGER() logging::Logger::get("mf7025v2")

// ═══════════════════════════════════════════════════════════════════════════
// Mf7025v2Motor
// ═══════════════════════════════════════════════════════════════════════════

Mf7025v2Hal::Mf7025v2Motor::Mf7025v2Motor(int axis_id, uint8_t can_node_id, Mf7025v2Hal* parent)
    : axis_id_(axis_id), can_node_id_(can_node_id), parent_(parent),
      start_time_(std::chrono::steady_clock::now()) {}

Mf7025v2Hal::Mf7025v2Motor::~Mf7025v2Motor() {
    if (enabled_) disable();
}

bool Mf7025v2Hal::Mf7025v2Motor::enable() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (error_state_) return false;

    auto* can = parent_->getCanInterface();
    if (!can || !can->isOpen()) {
        auto logger = logging::Logger::get("mf7025v2");
        logger->error("Motor {} enable: CAN interface not available", axis_id_);
        return false;
    }

    if (!can->motorRun(can_node_id_)) {
        auto logger = logging::Logger::get("mf7025v2");
        logger->error("Motor {} enable (0x88) failed on node {}", axis_id_, can_node_id_);
        can_failures_++;
        if (can_failures_ >= CAN_FAILURE_THRESHOLD) {
            error_state_ = true;
            error_message_ = "CAN dead-node: " + std::to_string(can_failures_) + " consecutive failures";
            emergencyStop();
        }
        return false;
    }
    can_failures_ = 0;
    enabled_ = true;

    if (state_change_callback_) state_change_callback_(true, false);

    auto logger = logging::Logger::get("mf7025v2");
    logger->info("Motor {} (node {}) enabled", axis_id_, can_node_id_);
    return true;
}

bool Mf7025v2Hal::Mf7025v2Motor::disable() {
    std::lock_guard<std::mutex> lock(mutex_);
    auto* can = parent_->getCanInterface();
    if (can && can->isOpen()) {
        can->motorOff(can_node_id_);
    }
    enabled_ = false;
    moving_ = false;
    actual_velocity_ = 0.0;
    if (state_change_callback_) state_change_callback_(false, false);
    return true;
}

bool Mf7025v2Hal::Mf7025v2Motor::isEnabled() const {
    return enabled_;
}

bool Mf7025v2Hal::Mf7025v2Motor::setPosition(double position_deg, double velocity_deg_s,
                                              double acceleration_deg_s2) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!enabled_ || error_state_) return false;

    (void)acceleration_deg_s2; // handled by drive's speed ramp config
    
    // Apply low-level direction inversion
    double pos = invert_direction_ ? -position_deg : position_deg;

    auto* can = parent_->getCanInterface();
    if (!can || !can->isOpen()) return false;

    // Convert to protocol units: 0.01°/LSB
    int32_t angle_001deg = static_cast<int32_t>(pos * 100.0);

    // The CAN bus is shared with the HAL monitor thread (0x9C status polls).
    // Under load the drive may not echo the position command within the
    // interface timeout even though it is still able to execute it. Retrying
    // here prevents an intermittent "one axis never moved" failure where one
    // drive receives its target and the other does not.
    bool ok = false;
    constexpr int MAX_ATTEMPTS = 3;
    for (int attempt = 0; attempt < MAX_ATTEMPTS && !ok; ++attempt) {
        if (velocity_deg_s > 0.0) {
            uint16_t max_speed_dps = static_cast<uint16_t>(std::min(velocity_deg_s, 65535.0));
            ok = can->positionControl2(can_node_id_, max_speed_dps, angle_001deg);
        } else {
            ok = can->positionControl1(can_node_id_, angle_001deg);
        }
        if (!ok && attempt + 1 < MAX_ATTEMPTS) {
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
    }

    if (!ok) {
        auto logger = logging::Logger::get("mf7025v2");
        logger->error("Motor {} setPosition({:.2f}°) failed on node {} after {} attempts",
                      axis_id_, position_deg, can_node_id_, MAX_ATTEMPTS);
        can_failures_++;
        if (can_failures_ >= CAN_FAILURE_THRESHOLD) {
            error_state_ = true;
            error_message_ = "CAN dead-node detected";
        }
        return false;
    }
    can_failures_ = 0;
    target_position_ = pos;
    actual_velocity_ = 0.0;  // Reset velocity so updateStatus() does not keep
                             // integrating position using a stale velocity from
                             // a previous velocity-control or position move.
                             // When the motor physically moves, can_speed from
                             // readStatus2 (0x9C) will be non-zero and correctly
                             // drive the position integration in updateStatus().
    moving_ = true;

    // Apply speed-dependent PID gains for this position move (RAM writes).
    applySpeedBasedPid(velocity_deg_s);
    return true;
}

bool Mf7025v2Hal::Mf7025v2Motor::setVelocity(double velocity_deg_s, double acceleration_deg_s2) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!enabled_ || error_state_) return false;

    (void)acceleration_deg_s2;
    
    // Apply low-level direction inversion
    double vel = invert_direction_ ? -velocity_deg_s : velocity_deg_s;

    auto* can = parent_->getCanInterface();
    if (!can || !can->isOpen()) return false;

    // Speed control (0xA2): iqControl(int16) + speedControl(int32, 0.01dps/LSB)
    int16_t iq = static_cast<int16_t>(config_.max_torque * 2048.0 / 100.0);
    int32_t speed_001dps = static_cast<int32_t>(vel * 100.0);

    if (!can->speedControl(can_node_id_, iq, speed_001dps)) {
        auto logger = logging::Logger::get("mf7025v2");
        logger->warn("Motor {} speedControl(0xA2) failed for {:.2f}°/s on node {} — "
                     "falling back to position control",
                     axis_id_, vel, can_node_id_);

        // Fallback: use multi-turn position control (0xA4) to emulate velocity.
        // The LingKong 0xA2 speed-control command is unreliable on some
        // hardware/firmware revisions; position control (0xA4) is universally
        // supported and can approximate velocity by targeting a far-away position.
        if (std::abs(vel) <= SPEED_HYSTERESIS) {
            // Zero velocity → stop the motor
            can->motorStop(can_node_id_);
            actual_velocity_ = 0.0;
            moving_ = false;
            return true;
        }

        // Compute a position target far in the direction of movement.
        // 1 000 000° ≈ 2778 motor revolutions — enough headroom for
        // continuous movement until the user releases the button.
        // Use physical position directly: getActualPosition() returns
        // logical (sign-inverted) which would give wrong CAN target.
        double current_pos = actual_position_;
        double direction = (vel > 0) ? 1.0 : -1.0;
        int32_t angle_001deg = static_cast<int32_t>((current_pos + direction * 1000000.0) * 100.0);
        uint16_t max_speed_dps = static_cast<uint16_t>(std::min(std::abs(vel), 65535.0));

        if (!can->positionControl2(can_node_id_, max_speed_dps, angle_001deg)) {
            logger->warn("Motor {} positionControl2(0xA4) fallback failed for {:.2f}°/s on node {} — "
                         "this is expected during rapid direction changes; motor will catch up next cycle",
                         axis_id_, vel, can_node_id_);
            // Do NOT increment can_failures_ here — the position-control
            // fallback in setVelocity() can legitimately fail during rapid
            // direction changes (the LingKong driver may reject large
            // position-target jumps).  This is NOT a sign of a dead CAN
            // node.  The calling loop (gamepad or tracking) will retry on
            // the next cycle.
            return false;
        }
        // Position-control fallback succeeded — track the state as if
        // velocity control worked, so stopAxis() knows the motor is moving.
        can_failures_ = 0;
        actual_velocity_ = vel;
        moving_ = true;

        // Apply speed-dependent PID gains for this velocity command.
        applySpeedBasedPid(vel);
        return true;
    }
    can_failures_ = 0;
    actual_velocity_ = vel;
    moving_ = (std::abs(vel) > SPEED_HYSTERESIS);

    // Apply speed-dependent PID gains for this velocity command.
    applySpeedBasedPid(vel);
    return true;
}

bool Mf7025v2Hal::Mf7025v2Motor::setTorque(double torque_percent) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!enabled_ || error_state_) return false;

    auto* can = parent_->getCanInterface();
    if (!can || !can->isOpen()) return false;

    int16_t iq = static_cast<int16_t>(torque_percent * 2048.0 / 100.0);
    return can->torqueControl(can_node_id_, iq);
}

bool Mf7025v2Hal::Mf7025v2Motor::stop() {
    std::lock_guard<std::mutex> lock(mutex_);
    auto* can = parent_->getCanInterface();
    if (can && can->isOpen()) {
        can->motorStop(can_node_id_);
    }
    moving_ = false;
    actual_velocity_ = 0.0;
    return true;
}

bool Mf7025v2Hal::Mf7025v2Motor::emergencyStop() {
    stop();
    return disable();
}

double Mf7025v2Hal::Mf7025v2Motor::getActualPosition() const {
    // Transparent inversion: return logical position so higher layers
    // never need to know about HAL-level direction reversal.
    double phys = actual_position_;
    return invert_direction_ ? -phys : phys;
}

double Mf7025v2Hal::Mf7025v2Motor::getActualVelocity() const {
    double phys = actual_velocity_;
    return invert_direction_ ? -phys : phys;
}

double Mf7025v2Hal::Mf7025v2Motor::getActualTorque() const {
    return actual_torque_;
}

bool Mf7025v2Hal::Mf7025v2Motor::isMoving() const {
    return moving_;
}

bool Mf7025v2Hal::Mf7025v2Motor::targetReached() const {
    if (!moving_) return true;
    return std::abs(actual_velocity_) < SPEED_HYSTERESIS;
}

bool Mf7025v2Hal::Mf7025v2Motor::inErrorState() const {
    return error_state_;
}

std::string Mf7025v2Hal::Mf7025v2Motor::getErrorString() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return error_message_;
}

bool Mf7025v2Hal::Mf7025v2Motor::clearErrors() {
    std::lock_guard<std::mutex> lock(mutex_);
    auto* can = parent_->getCanInterface();
    if (can && can->isOpen()) {
        can->clearErrors(can_node_id_);
    }
    error_state_ = false;
    error_message_.clear();
    can_failures_ = 0;
    return true;
}

bool Mf7025v2Hal::Mf7025v2Motor::zeroPosition() {
    std::lock_guard<std::mutex> lock(mutex_);
    auto* can = parent_->getCanInterface();
    if (!can || !can->isOpen()) return false;

    // 0x95: Set current position as zero point (RAM, volatile).
    // Motor stops after receiving this command.
    if (!can->setZeroRAM(can_node_id_)) {
        auto logger = logging::Logger::get("mf7025v2");
        logger->warn("Motor {} zeroPosition (0x95) failed on node {}", axis_id_, can_node_id_);
        return false;
    }

    // Reset our position tracking to match the new zero
    actual_position_ = 0.0;
    target_position_ = 0.0;

    auto logger = logging::Logger::get("mf7025v2");
    logger->info("Motor {} (node {}) position zeroed (0x95 SetZeroRAM)", axis_id_, can_node_id_);
    return true;
}

bool Mf7025v2Hal::Mf7025v2Motor::configure(const MotorConfig& config) {
    std::lock_guard<std::mutex> lock(mutex_);
    config_ = config;
    return true;
}

MotorConfig Mf7025v2Hal::Mf7025v2Motor::getConfiguration() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return config_;
}

bool Mf7025v2Hal::Mf7025v2Motor::writePidLoopRam(int loop, double kp, double ki, double kd) {
    // The MF7025v2 writes PID gains to RAM with the combined 0x31 command.
    // One frame overwrites all three loops (Kp/Ki only — no Kd), so the
    // per-loop selection is realised by updating only the selected loop in the
    // cached gains and resending the whole cached set.
    (void)kd;
    switch (loop) {
        case 1: pid_ram_cache_.current_kp  = kp; pid_ram_cache_.current_ki  = ki; break;
        case 2: pid_ram_cache_.speed_kp    = kp; pid_ram_cache_.speed_ki    = ki; break;
        case 3: pid_ram_cache_.position_kp = kp; pid_ram_cache_.position_ki = ki; break;
        default: return false;
    }

    auto* can = parent_->getCanInterface();
    if (!can || !can->isOpen()) {
        MF7025V2_LOGGER()->warn("Motor {} (node {}) PID loop {} RAM write skipped: CAN interface {}",
                   axis_id_, can_node_id_, loop,
                   (!can ? "missing" : "not open"));
        return false;
    }

    auto clamp8 = [](double v) -> uint8_t {
        if (v < 0.0) return 0;
        if (v > 255.0) return 255;
        return static_cast<uint8_t>(std::lround(v));
    };

    uint8_t cur_kp = clamp8(pid_ram_cache_.current_kp);
    uint8_t cur_ki = clamp8(pid_ram_cache_.current_ki);
    uint8_t spd_kp = clamp8(pid_ram_cache_.speed_kp);
    uint8_t spd_ki = clamp8(pid_ram_cache_.speed_ki);
    uint8_t pos_kp = clamp8(pid_ram_cache_.position_kp);
    uint8_t pos_ki = clamp8(pid_ram_cache_.position_ki);

    if (!can->writePidRam(can_node_id_, cur_kp, cur_ki, spd_kp, spd_ki, pos_kp, pos_ki)) {
        MF7025V2_LOGGER()->warn("Motor {} (node {}) PID loop {} RAM write (0x31) failed",
                   axis_id_, can_node_id_, loop);
        return false;
    }

    MF7025V2_LOGGER()->debug("Motor {} (node {}) PID loop {} RAM write (0x31): "
               "cur Kp={}/Ki={}, spd Kp={}/Ki={}, pos Kp={}/Ki={}",
               axis_id_, can_node_id_, loop,
               cur_kp, cur_ki, spd_kp, spd_ki, pos_kp, pos_ki);
    return true;
}

void Mf7025v2Hal::Mf7025v2Motor::setSpeedPidSchedule(
        const std::vector<hal::SpeedPidEntry>& schedule,
        bool enabled, double update_interval_ms,
        bool send_speed_pid, bool send_current_pid, bool send_position_pid) {
    std::lock_guard<std::mutex> lock(mutex_);
    speed_pid_schedule_ = schedule;
    speed_pid_enabled_ = enabled && !schedule.empty();
    speed_pid_update_interval_ms_ = update_interval_ms > 0.0 ? update_interval_ms : 50.0;
    // Seed the per-loop RAM cache from the configured schedule so calibration
    // writes do not start from arbitrary defaults when a schedule is present.
    if (!speed_pid_schedule_.empty()) {
        const auto& e = speed_pid_schedule_.front();
        pid_ram_cache_.current_kp  = e.current_kp;
        pid_ram_cache_.current_ki  = e.current_ki;
        pid_ram_cache_.speed_kp    = e.speed_kp;
        pid_ram_cache_.speed_ki    = e.speed_ki;
        pid_ram_cache_.position_kp = e.position_kp;
        pid_ram_cache_.position_ki = e.position_ki;
    }
    send_speed_pid_ = send_speed_pid;
    send_current_pid_ = send_current_pid;
    send_position_pid_ = send_position_pid;
    // Reset the last-sent cache so the next command applies the gains for
    // the current speed band (the drive may have been re-powered meanwhile).
    last_sent_pid_ = LastSentPid{};
    last_pid_update_ = std::chrono::steady_clock::time_point{};
    MF7025V2_LOGGER()->debug("Motor {} (node {}) speed-PID schedule: enabled={} entries={} "
               "send speed={} current={} position={}",
               axis_id_, can_node_id_, speed_pid_enabled_, speed_pid_schedule_.size(),
               send_speed_pid_, send_current_pid_, send_position_pid_);
}

void Mf7025v2Hal::Mf7025v2Motor::applySpeedBasedPid(double speed_deg_s) {
    if (!speed_pid_enabled_ || speed_pid_schedule_.empty()) return;

    // Convert servo degrees/second → motor shaft RPM. 1 RPM = 360°/60s = 6°/s.
    double rpm = std::abs(speed_deg_s) / 6.0;

    // Throttle re-evaluations to the configured minimum interval to avoid
    // spamming the CAN bus with parameter writes at high loop rates.
    auto now = std::chrono::steady_clock::now();
    if (last_pid_update_.time_since_epoch().count() != 0) {
        auto elapsed_ms = std::chrono::duration<double, std::milli>(now - last_pid_update_).count();
        if (elapsed_ms < speed_pid_update_interval_ms_) return;
    }

    // Find the schedule entry: the largest breakpoint <= current RPM.
    // Entries are expected to be sorted ascending by speed_rpm.
    const hal::SpeedPidEntry* selected = nullptr;
    for (const auto& e : speed_pid_schedule_) {
        if (e.speed_rpm <= rpm) {
            selected = &e;
        } else {
            break;  // table is sorted; no later entry can match either
        }
    }
    if (!selected) {
        // Speed below the first breakpoint — clamp to the first entry.
        selected = &speed_pid_schedule_.front();
    }

    // Skip the write if the gains already match what was last sent.
    // 0x31 overwrites all three loops in one frame, so every gain carried in
    // that frame participates in the change detection.
    bool changed = false;
    changed |= (selected->current_kp  != last_sent_pid_.current_kp ||
                selected->current_ki  != last_sent_pid_.current_ki);
    changed |= (selected->speed_kp    != last_sent_pid_.speed_kp ||
                selected->speed_ki    != last_sent_pid_.speed_ki);
    changed |= (selected->position_kp != last_sent_pid_.position_kp ||
                selected->position_ki != last_sent_pid_.position_ki);
    if (!changed) return;

    // Back off after a failed write: if the last PID write failed within the
    // backoff window, skip this attempt (and its warning).  The cache is not
    // updated on failure, so without this the drive is hammered every
    // speed_pid_update_interval_ms and the log is flooded with warnings.
    if (last_pid_failure_.time_since_epoch().count() != 0) {
        auto since_failure = std::chrono::duration<double, std::milli>(now - last_pid_failure_).count();
        if (since_failure < PID_RETRY_BACKOFF_MS) return;
    }

    auto* can = parent_->getCanInterface();
    if (!can || !can->isOpen()) return;

    // ── Combined PID write to RAM (0x31) ───────────────────────────────
    // The MF7025v2 protocol has no per-loop speed command: 0x31 overwrites the
    // current, speed and position loop gains together in a single frame.  Each
    // gain is a single byte (0..255):
    //   DATA[2..3] = current Kp/Ki
    //   DATA[4..5] = speed Kp/Ki
    //   DATA[6..7] = position Kp/Ki
    // Kd is not carried by 0x31 (the schedule kd values are informational).
    auto clamp8 = [](double v) -> uint8_t {
        if (v < 0.0) return 0;
        if (v > 255.0) return 255;
        return static_cast<uint8_t>(std::lround(v));
    };

    uint8_t cur_kp = clamp8(selected->current_kp);
    uint8_t cur_ki = clamp8(selected->current_ki);
    uint8_t spd_kp = clamp8(selected->speed_kp);
    uint8_t spd_ki = clamp8(selected->speed_ki);
    uint8_t pos_kp = clamp8(selected->position_kp);
    uint8_t pos_ki = clamp8(selected->position_ki);

    if (!can->writePidRam(can_node_id_, cur_kp, cur_ki, spd_kp, spd_ki, pos_kp, pos_ki)) {
        MF7025V2_LOGGER()->warn("Motor {} (node {}) combined PID write (0x31) failed @ {:.0f} RPM",
                   axis_id_, can_node_id_, rpm);
        last_pid_failure_ = now;
        return;
    }

    // Writes succeeded — clear the backoff timestamp so the next speed change
    // is applied immediately.
    last_pid_failure_ = std::chrono::steady_clock::time_point{};

    // Record the gains now resident in the drive so we don't resend them.
    last_sent_pid_.current_kp  = selected->current_kp;
    last_sent_pid_.current_ki  = selected->current_ki;
    last_sent_pid_.speed_kp    = selected->speed_kp;
    last_sent_pid_.speed_ki    = selected->speed_ki;
    last_sent_pid_.position_kp = selected->position_kp;
    last_sent_pid_.position_ki = selected->position_ki;
    last_pid_update_ = now;

    MF7025V2_LOGGER()->debug("Motor {} (node {}) speed-PID applied @ {:.0f} RPM: "
               "cur Kp={:.0f}/Ki={:.0f}, spd Kp={:.0f}/Ki={:.0f}, pos Kp={:.0f}/Ki={:.0f}",
               axis_id_, can_node_id_, rpm,
               selected->current_kp, selected->current_ki,
               selected->speed_kp, selected->speed_ki,
               selected->position_kp, selected->position_ki);
}

void Mf7025v2Hal::Mf7025v2Motor::setPositionCallback(PositionCallback callback) {
    std::lock_guard<std::mutex> lock(mutex_);
    position_callback_ = callback;
}

void Mf7025v2Hal::Mf7025v2Motor::setErrorCallback(ErrorCallback callback) {
    std::lock_guard<std::mutex> lock(mutex_);
    error_callback_ = callback;
}

void Mf7025v2Hal::Mf7025v2Motor::setStateChangeCallback(StateChangeCallback callback) {
    std::lock_guard<std::mutex> lock(mutex_);
    state_change_callback_ = callback;
}

double Mf7025v2Hal::Mf7025v2Motor::getTemperature() const {
    return temperature_;
}

double Mf7025v2Hal::Mf7025v2Motor::getCurrent() const {
    return current_;
}

double Mf7025v2Hal::Mf7025v2Motor::getVoltage() const {
    return voltage_;
}

uint32_t Mf7025v2Hal::Mf7025v2Motor::getOperationTime() const {
    auto now = std::chrono::steady_clock::now();
    return static_cast<uint32_t>(
        std::chrono::duration_cast<std::chrono::seconds>(now - start_time_).count());
}

void Mf7025v2Hal::Mf7025v2Motor::updateStatus(const Mf7025v2Status& st) {
    std::lock_guard<std::mutex> lock(mutex_);

    temperature_ = st.temperature;
    current_ = st.current * 0.01;
    voltage_ = st.voltage * 0.01;

    // ── Velocity ──────────────────────────────────────────────────────
    // Read directly from CAN 0x9C (ReadStatus2).  Resolution is 1 dps,
    // so speeds below 1 dps (tracking, sidereal) report as 0.
    double can_speed = static_cast<double>(st.speed);
    actual_velocity_ = can_speed;
    moving_ = std::abs(can_speed) > SPEED_HYSTERESIS;

    // ── Position ──────────────────────────────────────────────────────
    // Integrate from the current velocity while moving, using the ACTUAL
    // elapsed time since the previous status update.  The old hardcoded
    // 0.1 s factor assumed a 100 ms poll, but status_poll_ms defaults to
    // 50 ms — the position therefore advanced at 2× the real rate and,
    // after a few manual moves, the reported position drifted far from the
    // true one (visible as INDI showing a position far from the current
    // pointing).  When stopped, keep the last value set by
    // updateAbsolutePosition() (0x92 multi-turn read) so getActualPosition()
    // reports the true measured position, not the commanded target — this
    // is required for the slew-monitor verification that re-issues the move
    // when an axis did not reach its target.
    auto now = std::chrono::steady_clock::now();
    if (moving_ && last_status_update_.time_since_epoch().count() != 0) {
        const double dt = std::chrono::duration<double>(now - last_status_update_).count();
        // Clamp to a sane window so a stalled monitor thread cannot inject a
        // huge position step on the next wake-up.
        if (dt > 0.0 && dt < 0.5) {
            actual_position_ = actual_position_.load() + actual_velocity_.load() * dt;
        }
    }
    last_status_update_ = now;

    double iq_a = st.iq * (33.0 / 4096.0);
    actual_torque_ = (config_.max_torque > 0) ? (iq_a / (config_.max_torque * 2048.0 / 100.0 / (33.0/4096.0))) * 100.0 : 0.0;

    bool was_error = error_state_.load();
}

void Mf7025v2Hal::Mf7025v2Motor::updateAbsolutePosition(double abs_pos_deg) {
    std::lock_guard<std::mutex> lock(mutex_);
    // Do NOT overwrite target_position_ here — it must keep the commanded
    // target so the slew monitor can compare the measured position against it.
    actual_position_ = abs_pos_deg;
}

// ═══════════════════════════════════════════════════════════════════════════
// Mf7025v2Encoder
// ═══════════════════════════════════════════════════════════════════════════

Mf7025v2Hal::Mf7025v2Encoder::Mf7025v2Encoder(int axis_id, uint8_t can_node_id, Mf7025v2Hal* parent)
    : axis_id_(axis_id), can_node_id_(can_node_id), parent_(parent),
      start_time_(std::chrono::steady_clock::now()) {}

Mf7025v2Hal::Mf7025v2Encoder::~Mf7025v2Encoder() {
    shutdown();
}

bool Mf7025v2Hal::Mf7025v2Encoder::initialize(const EncoderConfig& config) {
    std::lock_guard<std::mutex> lock(mutex_);
    config_ = config;
    initialized_ = true;
    return true;
}

void Mf7025v2Hal::Mf7025v2Encoder::shutdown() {
    initialized_ = false;
}

bool Mf7025v2Hal::Mf7025v2Encoder::isInitialized() const {
    return initialized_;
}

EncoderReading Mf7025v2Hal::Mf7025v2Encoder::read() const {
    EncoderReading r;
    r.position_deg = actual_position_;
    r.data_valid = data_valid_;
    r.velocity_deg_s = 0.0;
    r.timestamp = std::chrono::steady_clock::now();
    total_readings_++;
    if (!data_valid_) error_count_++;
    return r;
}

bool Mf7025v2Hal::Mf7025v2Encoder::isDataValid() const { return data_valid_; }
double Mf7025v2Hal::Mf7025v2Encoder::getUpdateRate() const { return 10.0; } // 100ms poll

bool Mf7025v2Hal::Mf7025v2Encoder::calibrate(double reference_position_deg) {
    calibration_offset_ = reference_position_deg - actual_position_;
    return true;
}

bool Mf7025v2Hal::Mf7025v2Encoder::autoCalibrate() { return false; }
double Mf7025v2Hal::Mf7025v2Encoder::getCalibrationOffset() const { return calibration_offset_; }
void Mf7025v2Hal::Mf7025v2Encoder::setCalibrationOffset(double offset_deg) { calibration_offset_ = offset_deg; }
bool Mf7025v2Hal::Mf7025v2Encoder::saveCalibration() { return false; }
bool Mf7025v2Hal::Mf7025v2Encoder::loadCalibration() { return false; }

EncoderType Mf7025v2Hal::Mf7025v2Encoder::getType() const { return config_.type; }
EncoderInterface Mf7025v2Hal::Mf7025v2Encoder::getInterface() const { return config_.interface; }
uint32_t Mf7025v2Hal::Mf7025v2Encoder::getResolution() const { return config_.resolution; }
double Mf7025v2Hal::Mf7025v2Encoder::getCountsPerDegree() const { return config_.counts_per_degree; }

void Mf7025v2Hal::Mf7025v2Encoder::setReadingCallback(ReadingCallback callback) {
    reading_callback_ = callback;
}
void Mf7025v2Hal::Mf7025v2Encoder::setErrorCallback(ErrorCallback callback) {
    error_callback_ = callback;
}

uint32_t Mf7025v2Hal::Mf7025v2Encoder::getTotalReadings() const { return total_readings_; }
uint32_t Mf7025v2Hal::Mf7025v2Encoder::getErrorCount() const { return error_count_; }

double Mf7025v2Hal::Mf7025v2Encoder::getUptime() const {
    return std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::steady_clock::now() - start_time_).count();
}

std::string Mf7025v2Hal::Mf7025v2Encoder::getDiagnostics() const {
    return "MF7025v2 Encoder axis=" + std::to_string(axis_id_) +
           " node=" + std::to_string(can_node_id_);
}

bool Mf7025v2Hal::Mf7025v2Encoder::synchronize() { return true; }
bool Mf7025v2Hal::Mf7025v2Encoder::isSynchronized() const { return true; }

void Mf7025v2Hal::Mf7025v2Encoder::updateFromStatus(uint16_t encoder_raw, bool valid) {
    if (config_.counts_per_degree > 0) {
        actual_position_ = static_cast<double>(encoder_raw) / config_.counts_per_degree
                          + calibration_offset_;
    }
    data_valid_ = valid;
    if (reading_callback_) {
        EncoderReading r = read();
        reading_callback_(r);
    }
}

void Mf7025v2Hal::Mf7025v2Encoder::updatePositionDeg(double pos_deg, bool valid) {
    actual_position_ = pos_deg + calibration_offset_;
    data_valid_ = valid;
    if (reading_callback_) {
        EncoderReading r = read();
        reading_callback_(r);
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// Mf7025v2Hal
// ═══════════════════════════════════════════════════════════════════════════

Mf7025v2Hal::Mf7025v2Hal(std::unique_ptr<IMf7025v2Interface> can_iface)
    : can_iface_(std::move(can_iface)) {}

Mf7025v2Hal::~Mf7025v2Hal() {
    shutdown();
}

bool Mf7025v2Hal::initialize(const HALConfig& config) {
    std::lock_guard<std::mutex> lock(mutex_);
    config_ = config;

    if (!can_iface_) {
        last_error_ = "No CAN interface provided";
        return false;
    }

    MF7025V2_LOGGER()->info( "Opening CAN interface {} at {} bps",
               config_.mf7025v2.can_interface, config_.mf7025v2.bitrate);

    if (!can_iface_->open(config_.mf7025v2.can_interface, config_.mf7025v2.bitrate)) {
        last_error_ = "Failed to open CAN interface " + config_.mf7025v2.can_interface;
        MF7025V2_LOGGER()->error( "{}", last_error_);
        return false;
    }

    can_iface_->setCanTrace(config_.mf7025v2.can_trace);
    can_iface_->setCanTraceReadState(config_.mf7025v2.can_trace_read_state);

    // Create motor and encoder instances for each configured axis (max 2)
    for (size_t i = 0; i < config_.axes.size() && i < 2; ++i) {
        auto& axis = config_.axes[i];
        motors_[i] = std::make_unique<Mf7025v2Motor>(axis.id, axis.can_node_id, this);
        motors_[i]->configure(axis.motor_config);
        motors_[i]->invert_direction_ = axis.invert_direction;

        // Install the speed-dependent PID gain schedule (RAM writes only).
        // Both axes share the single schedule from the mf7025v2 config section.
        // send_speed_pid / send_current_pid / send_position_pid select which
        // per-loop gains from the schedule are written to the drive.
        motors_[i]->setSpeedPidSchedule(
            config_.mf7025v2.speed_pid_schedule,
            config_.mf7025v2.speed_pid_adaptation_enabled,
            config_.mf7025v2.speed_pid_adaptation_update_ms,
            config_.mf7025v2.send_speed_pid,
            config_.mf7025v2.send_current_pid,
            config_.mf7025v2.send_position_pid);

        encoders_[i] = std::make_unique<Mf7025v2Encoder>(axis.id, axis.can_node_id, this);
        encoders_[i]->initialize(axis.encoder_config);
    }

    initialized_ = true;
    MF7025V2_LOGGER()->info( "MF7025v2 HAL initialized with {} axes", config_.axes.size());
    return true;
}

void Mf7025v2Hal::shutdown() {
    stop();

    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& m : motors_) {
        if (m) m->disable();
    }
    if (can_iface_) can_iface_->close();
    initialized_ = false;
}

bool Mf7025v2Hal::isInitialized() const {
    return initialized_;
}

std::unique_ptr<MotorControl> Mf7025v2Hal::createMotorControl(int axis_id) {
    if (axis_id < 0 || axis_id >= 2 || !motors_[axis_id])
        return nullptr;
    return std::make_unique<Mf7025v2MotorProxy>(motors_[axis_id].get());
}

std::unique_ptr<EncoderReader> Mf7025v2Hal::createEncoderReader(int axis_id) {
    if (axis_id < 0 || axis_id >= 2 || !encoders_[axis_id])
        return nullptr;
    return std::make_unique<Mf7025v2EncoderProxy>(encoders_[axis_id].get());
}

std::unique_ptr<SafetyMonitor> Mf7025v2Hal::createSafetyMonitor() {
    return nullptr; // Not yet implemented
}

std::unique_ptr<SensorInterface> Mf7025v2Hal::createSensorInterface() {
    return nullptr; // Not yet implemented
}

std::string Mf7025v2Hal::getPlatformName() const {
    return "MF7025v2 (LingKong BLDC)";
}

std::string Mf7025v2Hal::getHardwareVersion() const {
    return "CAN V2.36";
}

std::vector<HALFeature> Mf7025v2Hal::getSupportedFeatures() const {
    return { HALFeature::FIELD_BUS_SUPPORT, HALFeature::PID_CONTROL,
             HALFeature::TRAJECTORY_CONTROL, HALFeature::ENCODER_FEEDBACK,
             HALFeature::REAL_TIME_CONTROL };
}

bool Mf7025v2Hal::supportsFeature(HALFeature feature) const {
    auto feats = getSupportedFeatures();
    return std::find(feats.begin(), feats.end(), feature) != feats.end();
}

bool Mf7025v2Hal::start() {
    if (!initialized_) return false;
    if (running_) return true;  // already started
    running_ = true;
    monitor_thread_ = std::thread(&Mf7025v2Hal::monitorLoop, this);
    MF7025V2_LOGGER()->info( "Monitor thread started");
    return true;
}

bool Mf7025v2Hal::stop() {
    running_ = false;
    if (monitor_thread_.joinable()) {
        monitor_thread_.join();
    }
    return true;
}

bool Mf7025v2Hal::isRunning() const {
    return running_;
}

std::string Mf7025v2Hal::getStatus() const {
    if (!initialized_) return "Not initialized";
    if (!can_iface_ || !can_iface_->isOpen()) return "CAN disconnected";
    return running_ ? "Running" : "Stopped";
}

std::string Mf7025v2Hal::getErrorMessages() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return last_error_;
}

void Mf7025v2Hal::clearErrors() {
    std::lock_guard<std::mutex> lock(mutex_);
    last_error_.clear();
    for (auto& m : motors_) {
        if (m) m->clearErrors();
    }
}

// ── Monitor thread ────────────────────────────────────────────────────────

void Mf7025v2Hal::monitorLoop() {
    using namespace std::chrono;

    auto interval_status = milliseconds(config_.mf7025v2.status_poll_ms);
    auto interval_absolute = milliseconds(config_.mf7025v2.absolute_position_poll_ms);
    auto last_absolute = steady_clock::now();

    MF7025V2_LOGGER()->info("Monitor loop started (status={}ms, absolute={}ms)",
               interval_status.count(), interval_absolute.count());

    while (running_) {
        auto loop_start = steady_clock::now();

        if (!can_iface_ || !can_iface_->isOpen()) {
            std::this_thread::sleep_for(milliseconds(500));
            continue;
        }

        // ── Status + single-turn angle for each axis ─────────────────────
        for (size_t i = 0; i < 2; ++i) {
            if (!motors_[i]) continue;

            Mf7025v2Status st;
            uint8_t node_id = static_cast<uint8_t>(i + 1); // HA=1, Dec=2

            // Read status 2 (temperature, iq, speed)
            // Encoder position is read separately via 0x94 (single-turn angle)
            // because the 0x9C encoder field is only 16-bit while the
            // real hardware encoder is 18-bit.
            if (can_iface_->readStatus2(node_id, st)) {
                motors_[i]->updateStatus(st);
            } else {
                MF7025V2_LOGGER()->warn("Status read failed for node {}", node_id);
            }

            // Read single-turn angle (0x94) for encoder position
            {
                uint32_t single_turn_angle = 0;
                if (can_iface_->readSingleTurnAngle(node_id, single_turn_angle)) {
                    if (encoders_[i]) {
                        double pos_deg = static_cast<double>(single_turn_angle) * 0.01;
                        encoders_[i]->updatePositionDeg(pos_deg, true);
                    }
                }
            }
        }

        // ── Absolute multi-turn position on independent timer ────────────
        auto now = steady_clock::now();
        if (now - last_absolute >= interval_absolute) {
            for (size_t i = 0; i < 2; ++i) {
                if (!motors_[i]) continue;
                uint8_t node_id = static_cast<uint8_t>(i + 1);

                int64_t angle_001deg = 0;
                bool abs_ok = false;
                if (can_iface_->readMultiTurnAngle(node_id, angle_001deg)) {
                    double abs_pos_deg = static_cast<double>(angle_001deg) * 0.01;
                    if (std::abs(abs_pos_deg) < 10000000.0) {
                        motors_[i]->updateAbsolutePosition(abs_pos_deg);
                        // Sync encoder to motor absolute position so the
                        // UI displays signed values (0x94 single-turn
                        // angle is always unsigned 0..360°*gear_ratio).
                        if (encoders_[i]) {
                            encoders_[i]->updatePositionDeg(abs_pos_deg, true);
                        }
                        abs_ok = true;
                    } else {
                        MF7025V2_LOGGER()->warn(
                            "axis{} 0x92 multi-turn angle out of range ({:.4f}°) — "
                            "motor may not be calibrated, falling back to single-turn (0x94)",
                            i, abs_pos_deg);
                    }
                }

                // Fallback: if 0x92 failed or returned garbage, do NOT
                // overwrite the motor's multi-turn position with the 0x94
                // single-turn encoder value — that would corrupt the absolute
                // reference used by the slew-monitor verification.
                (void)abs_ok;
            }
            last_absolute = now;
        }

        // Maintain the configured status poll interval
        auto elapsed = steady_clock::now() - loop_start;
        if (elapsed < interval_status) {
            std::this_thread::sleep_for(interval_status - elapsed);
        }
    }

    MF7025V2_LOGGER()->info("Monitor loop stopped");
}
