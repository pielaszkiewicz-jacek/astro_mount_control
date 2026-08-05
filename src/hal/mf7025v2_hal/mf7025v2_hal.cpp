// MF7025v2 HAL implementation — LingKong BLDC servo via proprietary CAN V2.36
#include "hal/mf7025v2_hal/mf7025v2_hal.h"
#include "hal/safety_monitor.h"
#include "hal/sensor_interface.h"
#include "logging/logger.h"
#include <algorithm>
#include <cmath>
#include <sstream>

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

    auto* can = parent_->getCanInterface();
    if (!can || !can->isOpen()) return false;

    // Convert to protocol units: 0.01°/LSB
    int32_t angle_001deg = static_cast<int32_t>(position_deg * 100.0);

    bool ok;
    if (velocity_deg_s > 0.0) {
        uint16_t max_speed_dps = static_cast<uint16_t>(std::min(velocity_deg_s, 65535.0));
        ok = can->positionControl2(can_node_id_, max_speed_dps, angle_001deg);
    } else {
        ok = can->positionControl1(can_node_id_, angle_001deg);
    }

    if (!ok) {
        auto logger = logging::Logger::get("mf7025v2");
        logger->error("Motor {} setPosition({:.2f}°) failed on node {}",
                      axis_id_, position_deg, can_node_id_);
        can_failures_++;
        if (can_failures_ >= CAN_FAILURE_THRESHOLD) {
            error_state_ = true;
            error_message_ = "CAN dead-node detected";
        }
        return false;
    }
    can_failures_ = 0;
    target_position_ = position_deg;
    actual_position_ = position_deg;  // track commanded target for relative step moves
    actual_velocity_ = 0.0;  // Reset velocity so updateStatus() does not keep
                             // integrating position using a stale velocity from
                             // a previous velocity-control or position move.
                             // When the motor physically moves, can_speed from
                             // readStatus2 (0x9C) will be non-zero and correctly
                             // drive the position integration in updateStatus().
    moving_ = true;
    return true;
}

bool Mf7025v2Hal::Mf7025v2Motor::setVelocity(double velocity_deg_s, double acceleration_deg_s2) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!enabled_ || error_state_) return false;

    (void)acceleration_deg_s2;

    auto* can = parent_->getCanInterface();
    if (!can || !can->isOpen()) return false;

    // Speed control (0xA2): iqControl(int16) + speedControl(int32, 0.01dps/LSB)
    int16_t iq = static_cast<int16_t>(config_.max_torque * 2048.0 / 100.0);
    int32_t speed_001dps = static_cast<int32_t>(velocity_deg_s * 100.0);

    if (!can->speedControl(can_node_id_, iq, speed_001dps)) {
        auto logger = logging::Logger::get("mf7025v2");
        logger->warn("Motor {} speedControl(0xA2) failed for {:.2f}°/s on node {} — "
                     "falling back to position control",
                     axis_id_, velocity_deg_s, can_node_id_);

        // Fallback: use multi-turn position control (0xA4) to emulate velocity.
        // The LingKong 0xA2 speed-control command is unreliable on some
        // hardware/firmware revisions; position control (0xA4) is universally
        // supported and can approximate velocity by targeting a far-away position.
        if (std::abs(velocity_deg_s) <= SPEED_HYSTERESIS) {
            // Zero velocity → stop the motor
            can->motorStop(can_node_id_);
            actual_velocity_ = 0.0;
            moving_ = false;
            return true;
        }

        // Compute a position target far in the direction of movement.
        // 1 000 000° ≈ 2778 motor revolutions — enough headroom for
        // continuous movement until the user releases the button.
        double current_pos = getActualPosition();
        double direction = (velocity_deg_s > 0) ? 1.0 : -1.0;
        int32_t angle_001deg = static_cast<int32_t>((current_pos + direction * 1000000.0) * 100.0);
        uint16_t max_speed_dps = static_cast<uint16_t>(std::min(std::abs(velocity_deg_s), 65535.0));

        if (!can->positionControl2(can_node_id_, max_speed_dps, angle_001deg)) {
            logger->error("Motor {} positionControl2(0xA4) fallback also failed on node {}",
                          axis_id_, can_node_id_);
            can_failures_++;
            if (can_failures_ >= CAN_FAILURE_THRESHOLD) {
                error_state_ = true;
                error_message_ = "CAN dead-node detected";
            }
            return false;
        }
        // Position-control fallback succeeded — track the state as if
        // velocity control worked, so stopAxis() knows the motor is moving.
        can_failures_ = 0;
        actual_velocity_ = velocity_deg_s;
        moving_ = true;
        return true;
    }
    can_failures_ = 0;
    actual_velocity_ = velocity_deg_s;
    moving_ = (std::abs(velocity_deg_s) > SPEED_HYSTERESIS);
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
    return actual_position_;
}

double Mf7025v2Hal::Mf7025v2Motor::getActualVelocity() const {
    return actual_velocity_;
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
    // Always integrate from the current velocity so position tracks
    // continuously in both position and velocity control modes.
    // In position mode, when the motor stops (can_moving → false and
    // current_vel ≈ 0), we snap to the commanded target for accuracy.
    if (!moving_) {
        actual_position_ = target_position_.load();
    } else {
        actual_position_ = actual_position_.load() + actual_velocity_.load() * 0.1;
    }

    double iq_a = st.iq * (33.0 / 4096.0);
    actual_torque_ = (config_.max_torque > 0) ? (iq_a / (config_.max_torque * 2048.0 / 100.0 / (33.0/4096.0))) * 100.0 : 0.0;

    bool was_error = error_state_.load();
}

void Mf7025v2Hal::Mf7025v2Motor::updateAbsolutePosition(double abs_pos_deg) {
    std::lock_guard<std::mutex> lock(mutex_);
    actual_position_ = abs_pos_deg;
    target_position_ = abs_pos_deg;
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

                // Fallback: if 0x92 failed or returned garbage, use the
                // encoder position from 0x94 (single-turn angle) as the
                // motor position.  This prevents the position from being
                // stuck at 0 on uncalibrated axes.
                if (!abs_ok && encoders_[i]) {
                    double enc_pos = encoders_[i]->read().position_deg;
                    motors_[i]->updateAbsolutePosition(enc_pos);
                }
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
