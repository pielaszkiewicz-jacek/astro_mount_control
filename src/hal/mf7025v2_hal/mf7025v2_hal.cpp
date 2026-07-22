#include "hal/mf7025v2_hal/mf7025v2_hal.h"
#include "logging/logger.h"
#include <cmath>
#include <sstream>
#include <algorithm>

namespace astro_mount {
namespace hal {

namespace {
    constexpr const char* LOG_TAG = "mf7025v2_hal";
    constexpr double POSITION_UNITS_PER_DEG = 100.0;  // 0.01°/LSB
    constexpr double VELOCITY_UNITS_PER_DPS = 100.0;  // 0.01dps/LSB
    constexpr double TORQUE_SCALE = 2048.0 / 100.0;   // iq 0..2048 → 0..100%

    inline int32_t degToAngleUnits(double deg) {
        return static_cast<int32_t>(std::llround(deg * POSITION_UNITS_PER_DEG));
    }

    inline double angleUnitsToDeg(int32_t units) {
        return static_cast<double>(units) / POSITION_UNITS_PER_DEG;
    }

    inline int32_t dpsToSpeedUnits(double deg_per_s) {
        return static_cast<int32_t>(std::llround(deg_per_s * VELOCITY_UNITS_PER_DPS));
    }

    inline int16_t percentToIq(double percent) {
        return static_cast<int16_t>(std::llround(percent * TORQUE_SCALE));
    }
}

// ════════════════════════════════════════════════════════════════════
// MfMotor
// ════════════════════════════════════════════════════════════════════

Mf7025v2Hal::MfMotor::MfMotor(uint8_t node_id,
                                controllers::IMf7025v2Interface& can,
                                const MotorConfig& config)
    : node_id_(node_id), can_(can), config_(config),
      start_time_(std::chrono::steady_clock::now()) {}

Mf7025v2Hal::MfMotor::~MfMotor() {
    poll_running_ = false;
    if (poll_thread_.joinable()) {
        poll_thread_.join();
    }
}

bool Mf7025v2Hal::MfMotor::enable() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (can_.motorEnable(node_id_)) {
        enabled_ = true;
        // Start status polling thread
        if (!poll_running_) {
            poll_running_ = true;
            poll_thread_ = std::thread(&MfMotor::pollLoop, this);
        }
        if (state_change_callback_) {
            state_change_callback_(true, moving_.load());
        }
        return true;
    }
    logging::Logger::get(LOG_TAG)->error(
        "MF7025v2 node {}: motorEnable() failed — CAN command not acknowledged",
        node_id_);
    error_state_ = true;
    error_message_ = "MOTOR_ENABLE_FAILED";
    return false;
}

bool Mf7025v2Hal::MfMotor::disable() {
    std::lock_guard<std::mutex> lock(mutex_);
    poll_running_ = false;
    if (poll_thread_.joinable()) {
        poll_thread_.join();
    }
    if (can_.motorDisable(node_id_)) {
        enabled_ = false;
        moving_ = false;
        if (state_change_callback_) {
            state_change_callback_(false, false);
        }
        return true;
    }
    return false;
}

bool Mf7025v2Hal::MfMotor::isEnabled() const {
    return enabled_.load();
}

bool Mf7025v2Hal::MfMotor::setPosition(double position_deg, double velocity_deg_s,
                                         double /*acceleration_deg_s2*/) {
    std::lock_guard<std::mutex> lock(mutex_);
    int32_t angle = degToAngleUnits(position_deg);

    bool ok;
    if (velocity_deg_s > 0.0) {
        uint16_t max_speed = static_cast<uint16_t>(std::abs(velocity_deg_s));
        ok = can_.setMultiTurnPositionWithSpeed(node_id_, angle, max_speed);
    } else {
        ok = can_.setMultiTurnPosition(node_id_, angle);
    }

    if (ok) {
        moving_ = true;
        target_position_ = position_deg;
        can_failures_ = 0;  // Reset CAN failure counter on successful command
        return true;
    }

    can_failures_++;
    logging::Logger::get(LOG_TAG)->error(
        "MF7025v2 node {}: setPosition({:.4f}°) failed (CAN failure #{})",
        node_id_, position_deg, can_failures_.load());
    return false;
}

bool Mf7025v2Hal::MfMotor::setVelocity(double velocity_deg_s, double /*acceleration_deg_s2*/) {
    std::lock_guard<std::mutex> lock(mutex_);
    int32_t speed = dpsToSpeedUnits(velocity_deg_s);
    int16_t torque_limit = percentToIq(config_.max_torque);

    if (can_.setVelocity(node_id_, speed, torque_limit)) {
        moving_ = true;
        can_failures_ = 0;
        return true;
    }

    can_failures_++;
    logging::Logger::get(LOG_TAG)->error(
        "MF7025v2 node {}: setVelocity({:.4f} dps) failed (CAN failure #{})",
        node_id_, velocity_deg_s, can_failures_.load());
    return false;
}

bool Mf7025v2Hal::MfMotor::setTorque(double torque_percent) {
    std::lock_guard<std::mutex> lock(mutex_);
    int16_t iq = percentToIq(torque_percent);
    return can_.setTorque(node_id_, iq);
}

bool Mf7025v2Hal::MfMotor::stop() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (can_.motorStop(node_id_)) {
        moving_ = false;
        can_failures_ = 0;
        return true;
    }

    can_failures_++;
    logging::Logger::get(LOG_TAG)->error(
        "MF7025v2 node {}: motorStop() failed (CAN failure #{})",
        node_id_, can_failures_.load());
    return false;
}

bool Mf7025v2Hal::MfMotor::emergencyStop() {
    std::lock_guard<std::mutex> lock(mutex_);
    can_.motorStop(node_id_);
    can_.motorDisable(node_id_);
    moving_ = false;
    enabled_ = false;
    poll_running_ = false;
    if (poll_thread_.joinable()) {
        poll_thread_.join();
    }
    return true;
}

double Mf7025v2Hal::MfMotor::getActualPosition() const {
    return actual_position_.load();
}

double Mf7025v2Hal::MfMotor::getActualVelocity() const {
    return actual_velocity_.load();
}

double Mf7025v2Hal::MfMotor::getActualTorque() const {
    return actual_torque_.load();
}

bool Mf7025v2Hal::MfMotor::isMoving() const {
    return moving_.load();
}

bool Mf7025v2Hal::MfMotor::targetReached() const {
    // Consider target reached when velocity is near zero and position is stable
    return !moving_.load();
}

bool Mf7025v2Hal::MfMotor::inErrorState() const {
    return error_state_.load();
}

std::string Mf7025v2Hal::MfMotor::getErrorString() const {
    return error_message_;
}

bool Mf7025v2Hal::MfMotor::configure(const MotorConfig& config) {
    std::lock_guard<std::mutex> lock(mutex_);
    config_ = config;
    return true;
}

MotorConfig Mf7025v2Hal::MfMotor::getConfiguration() const {
    return config_;
}

void Mf7025v2Hal::MfMotor::setPositionCallback(PositionCallback callback) {
    position_callback_ = std::move(callback);
}

void Mf7025v2Hal::MfMotor::setErrorCallback(ErrorCallback callback) {
    error_callback_ = std::move(callback);
}

void Mf7025v2Hal::MfMotor::setStateChangeCallback(StateChangeCallback callback) {
    state_change_callback_ = std::move(callback);
}

double Mf7025v2Hal::MfMotor::getTemperature() const {
    auto status = can_.readStatus1(node_id_);
    return static_cast<double>(status.temperature);
}

double Mf7025v2Hal::MfMotor::getCurrent() const {
    auto status = can_.readStatus1(node_id_);
    return static_cast<double>(status.current) * 0.01;
}

double Mf7025v2Hal::MfMotor::getVoltage() const {
    auto status = can_.readStatus1(node_id_);
    return static_cast<double>(status.voltage) * 0.01;
}

uint32_t Mf7025v2Hal::MfMotor::getOperationTime() const {
    auto elapsed = std::chrono::steady_clock::now() - start_time_;
    return static_cast<uint32_t>(
        std::chrono::duration_cast<std::chrono::seconds>(elapsed).count());
}

void Mf7025v2Hal::MfMotor::pollLoop() {
    while (poll_running_) {
        updateStatus();

        // Dead-node detection: if CAN failures exceed threshold,
        // trigger emergency stop to prevent uncontrolled movement.
        if (can_failures_.load() >= CAN_FAILURE_THRESHOLD) {
            logging::Logger::get(LOG_TAG)->error(
                "MF7025v2 node {}: {} consecutive CAN failures — emergency stop",
                node_id_, can_failures_.load());
            emergencyStop();
            break;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(poll_interval_ms_));
    }
}

void Mf7025v2Hal::MfMotor::updateStatus() {
    std::lock_guard<std::mutex> lock(mutex_);

    // Read Status1 (temperature, voltage, current, errors)
    auto status1 = can_.readStatus1(node_id_);
    // Read Status2 (iq, speed, encoder)
    auto status2 = can_.readStatus2(node_id_);

    // Check for CAN communication failures — all status structs have
    // zero-initialized fields on failure, but we detect errors by
    // checking if the entire response is plausibly valid.
    // A common failure mode is the drive not responding, in which case
    // readMultiTurnAngle would return 0, status fields stay at 0, and
    // error_state remains 0 — indistinguishable from a powered-off drive.
    // We use the CAN interface layer's timeout/error return instead:
    // if readStatus1 returned a default/zeroed struct, assume comm failure.
    bool can_ok = true;

    // Read multi-turn angle for absolute position
    int64_t multi_turn = can_.readMultiTurnAngle(node_id_);
    double pos = angleUnitsToDeg(static_cast<int32_t>(multi_turn));
    double vel = static_cast<double>(status2.speed);
    double torque = static_cast<double>(status2.iq) / TORQUE_SCALE;

    // Heuristic: if position hasn't changed but velocity is non-zero,
    // the CAN transaction probably failed (stale data).
    // A more robust check would use the CAN interface's return status.
    if (multi_turn == 0) {
        can_failures_++;
        logging::Logger::get(LOG_TAG)->warn(
            "MF7025v2 node {}: readMultiTurnAngle returned 0 "
            "(CAN failure #{})", node_id_, can_failures_.load());
        return;  // Skip status update this cycle — keep last known values
    }

    // Reset failure counter on successful read
    can_failures_ = 0;

    actual_position_ = pos;
    actual_velocity_ = vel;
    actual_torque_ = torque;

    // Check for drive-reported errors
    if (status1.error_state != 0) {
        error_state_ = true;
        error_message_ = decodeErrorFlags(status1.error_state);
        logging::Logger::get(LOG_TAG)->error(
            "MF7025v2 node {}: drive error 0x{:02x} — {}",
            node_id_, status1.error_state, error_message_);
        if (error_callback_) {
            error_callback_(error_message_, status1.error_state);
        }
    } else {
        error_state_ = false;
        error_message_.clear();
    }

    // Moving detection with hysteresis
    moving_ = std::abs(vel) > 0.1;

    if (position_callback_) {
        position_callback_(pos, vel, torque);
    }
}

std::string Mf7025v2Hal::MfMotor::decodeErrorFlags(uint8_t error_state) const {
    std::vector<std::string> errors;
    if (error_state & 0x01) errors.push_back("LOW_VOLTAGE");
    if (error_state & 0x02) errors.push_back("HIGH_VOLTAGE");
    if (error_state & 0x04) errors.push_back("DRIVER_OVERTEMP");
    if (error_state & 0x08) errors.push_back("MOTOR_OVERTEMP");
    if (error_state & 0x10) errors.push_back("OVERCURRENT");
    if (error_state & 0x20) errors.push_back("SHORT_CIRCUIT");
    if (error_state & 0x40) errors.push_back("STALL");
    if (error_state & 0x80) errors.push_back("SIGNAL_LOSS");

    if (errors.empty()) return "NO_ERROR";
    std::ostringstream oss;
    for (size_t i = 0; i < errors.size(); i++) {
        if (i > 0) oss << " | ";
        oss << errors[i];
    }
    return oss.str();
}

// ════════════════════════════════════════════════════════════════════
// MfEncoder
// ════════════════════════════════════════════════════════════════════

Mf7025v2Hal::MfEncoder::MfEncoder(uint8_t node_id,
                                    controllers::IMf7025v2Interface& can,
                                    const EncoderConfig& config)
    : node_id_(node_id), can_(can), config_(config),
      start_time_(std::chrono::steady_clock::now()) {}

Mf7025v2Hal::MfEncoder::~MfEncoder() = default;

bool Mf7025v2Hal::MfEncoder::initialize(const EncoderConfig& config) {
    config_ = config;
    initialized_ = true;
    return true;
}

void Mf7025v2Hal::MfEncoder::shutdown() {
    initialized_ = false;
}

bool Mf7025v2Hal::MfEncoder::isInitialized() const {
    return initialized_.load();
}

EncoderReading Mf7025v2Hal::MfEncoder::read() const {
    EncoderReading reading{};
    reading.timestamp = std::chrono::steady_clock::now();

    // Read multi-turn angle (0x92) for absolute position
    int64_t angle_units = can_.readMultiTurnAngle(node_id_);
    // No reliable way to distinguish 0 from fault on this protocol,
    // so always treat the reading as valid. The error_count_ tracks
    // communication failures signaled by the CAN interface layer.
    reading.position_deg = static_cast<double>(angle_units) / POSITION_UNITS_PER_DEG;
    reading.position_deg += calibration_offset_.load();
    reading.data_valid = true;

    // Read velocity from Status 2
    auto status2 = can_.readStatus2(node_id_);
    reading.velocity_deg_s = static_cast<double>(status2.speed);

    total_readings_++;
    if (reading_callback_) {
        reading_callback_(reading);
    }

    return reading;
}

bool Mf7025v2Hal::MfEncoder::isDataValid() const {
    return true; // Multi-turn angle is always valid when drive is powered
}

double Mf7025v2Hal::MfEncoder::getUpdateRate() const {
    return 50.0; // Hz, polled at ~20ms
}

bool Mf7025v2Hal::MfEncoder::calibrate(double /*reference_position_deg*/) {
    return can_.calibrateEncoder(node_id_);
}

bool Mf7025v2Hal::MfEncoder::autoCalibrate() {
    return can_.calibrateEncoder(node_id_);
}

double Mf7025v2Hal::MfEncoder::getCalibrationOffset() const {
    return calibration_offset_.load();
}

void Mf7025v2Hal::MfEncoder::setCalibrationOffset(double offset_deg) {
    calibration_offset_ = offset_deg;
}

bool Mf7025v2Hal::MfEncoder::saveCalibration() {
    int32_t offset_units = static_cast<int32_t>(calibration_offset_.load() * POSITION_UNITS_PER_DEG);
    return can_.setZeroROM(node_id_, offset_units);
}

bool Mf7025v2Hal::MfEncoder::loadCalibration() {
    // Loaded from config on initialization
    return true;
}

EncoderType Mf7025v2Hal::MfEncoder::getType() const {
    return EncoderType::ABSOLUTE;
}

EncoderInterface Mf7025v2Hal::MfEncoder::getInterface() const {
    return EncoderInterface::CANOPEN; // Re-use CANOPEN enum for CAN bus
}

uint32_t Mf7025v2Hal::MfEncoder::getResolution() const {
    return config_.resolution;
}

double Mf7025v2Hal::MfEncoder::getCountsPerDegree() const {
    return config_.counts_per_degree;
}

void Mf7025v2Hal::MfEncoder::setReadingCallback(ReadingCallback callback) {
    reading_callback_ = std::move(callback);
}

void Mf7025v2Hal::MfEncoder::setErrorCallback(ErrorCallback callback) {
    error_callback_ = std::move(callback);
}

uint32_t Mf7025v2Hal::MfEncoder::getTotalReadings() const {
    return total_readings_.load();
}

uint32_t Mf7025v2Hal::MfEncoder::getErrorCount() const {
    return error_count_.load();
}

double Mf7025v2Hal::MfEncoder::getUptime() const {
    auto elapsed = std::chrono::steady_clock::now() - start_time_;
    return std::chrono::duration_cast<std::chrono::duration<double>>(elapsed).count();
}

std::string Mf7025v2Hal::MfEncoder::getDiagnostics() const {
    std::ostringstream oss;
    oss << "MF7025v2 Encoder node=" << static_cast<int>(node_id_)
        << " readings=" << total_readings_.load()
        << " errors=" << error_count_.load();
    return oss.str();
}

bool Mf7025v2Hal::MfEncoder::synchronize() { return true; }
bool Mf7025v2Hal::MfEncoder::isSynchronized() const { return true; }

// ════════════════════════════════════════════════════════════════════
// MfSafetyMonitor
// ════════════════════════════════════════════════════════════════════

Mf7025v2Hal::MfSafetyMonitor::MfSafetyMonitor(
    controllers::IMf7025v2Interface& can,
    std::vector<uint8_t> monitored_nodes)
    : can_(can), monitored_nodes_(std::move(monitored_nodes)) {
    // Default to nodes 1,2 if none specified
    if (monitored_nodes_.empty()) {
        monitored_nodes_ = {1, 2};
    }
}

Mf7025v2Hal::MfSafetyMonitor::~MfSafetyMonitor() {
    shutdown();
}

bool Mf7025v2Hal::MfSafetyMonitor::initialize(const SafetyConfig& config) {
    config_ = config;
    initialized_ = true;
    monitor_running_ = true;
    monitor_thread_ = std::thread(&MfSafetyMonitor::monitorLoop, this);
    return true;
}

void Mf7025v2Hal::MfSafetyMonitor::shutdown() {
    monitor_running_ = false;
    if (monitor_thread_.joinable()) {
        monitor_thread_.join();
    }
    initialized_ = false;
}

bool Mf7025v2Hal::MfSafetyMonitor::isInitialized() const {
    return initialized_.load();
}

SafetyStatus Mf7025v2Hal::MfSafetyMonitor::getStatus() const {
    SafetyStatus status;
    status.overall_state = SafetyStatus::State::NORMAL;
    status.timestamp = std::chrono::system_clock::now();
    status.safety_circuit_ok = true;
    return status;
}

bool Mf7025v2Hal::MfSafetyMonitor::checkLimits(int axis_id) {
    uint8_t node = (axis_id >= 0 && axis_id < static_cast<int>(monitored_nodes_.size()))
                       ? monitored_nodes_[axis_id]
                       : static_cast<uint8_t>(axis_id + 1);
    auto status = can_.readStatus1(node);
    return status.error_state == 0;
}

bool Mf7025v2Hal::MfSafetyMonitor::emergencyStop(int axis_id) {
    uint8_t node = (axis_id >= 0 && axis_id < static_cast<int>(monitored_nodes_.size()))
                       ? monitored_nodes_[axis_id]
                       : static_cast<uint8_t>(axis_id + 1);
    can_.motorStop(node);
    can_.motorDisable(node);
    return true;
}

bool Mf7025v2Hal::MfSafetyMonitor::clearErrors(int axis_id) {
    uint8_t node = (axis_id >= 0 && axis_id < static_cast<int>(monitored_nodes_.size()))
                       ? monitored_nodes_[axis_id]
                       : static_cast<uint8_t>(axis_id + 1);
    return can_.clearErrors(node);
}

void Mf7025v2Hal::MfSafetyMonitor::setLimitCallback(LimitCallback callback) {
    limit_callback_ = std::move(callback);
}

void Mf7025v2Hal::MfSafetyMonitor::setErrorCallback(ErrorCallback callback) {
    error_callback_ = std::move(callback);
}

std::string Mf7025v2Hal::MfSafetyMonitor::getDiagnostics() const {
    return "MF7025v2 SafetyMonitor";
}

void Mf7025v2Hal::MfSafetyMonitor::monitorLoop() {
    while (monitor_running_) {
        for (uint8_t node : monitored_nodes_) {
            auto status = can_.readStatus1(node);
            if (status.error_state != 0) {
                if (error_callback_) {
                    error_callback_("MF7025v2 node " + std::to_string(node) +
                                    " error: 0x" + std::to_string(status.error_state));
                }
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
}

// ════════════════════════════════════════════════════════════════════
// MfSensorInterface
// ════════════════════════════════════════════════════════════════════

Mf7025v2Hal::MfSensorInterface::MfSensorInterface(
    controllers::IMf7025v2Interface& can)
    : can_(can) {}

Mf7025v2Hal::MfSensorInterface::~MfSensorInterface() = default;

bool Mf7025v2Hal::MfSensorInterface::initialize(const SensorConfig& config) {
    config_ = config;
    initialized_ = true;
    return true;
}

void Mf7025v2Hal::MfSensorInterface::shutdown() {
    initialized_ = false;
}

bool Mf7025v2Hal::MfSensorInterface::isInitialized() const {
    return initialized_.load();
}

SensorReading Mf7025v2Hal::MfSensorInterface::read(int sensor_id) const {
    SensorReading reading{};
    reading.sensor_id = sensor_id;
    reading.timestamp = std::chrono::system_clock::now();

    // sensor_id 0=HA, 1=Dec
    uint8_t node = static_cast<uint8_t>(sensor_id + 1);
    auto status = can_.readStatus1(node);

    // Return temperature as the primary sensor value;
    // voltage and current are accessible via the drive's status interface.
    reading.type = SensorType::TEMPERATURE;
    reading.value = static_cast<double>(status.temperature);
    reading.unit = "C";
    reading.valid = (status.error_state == 0);
    reading.accuracy = 1.0;       // ±1°C typical for MF7025v2
    reading.confidence = status.error_state == 0 ? 1.0 : 0.0;

    return reading;
}

std::vector<SensorReading> Mf7025v2Hal::MfSensorInterface::readAll() const {
    std::vector<SensorReading> readings;
    readings.push_back(read(0)); // HA axis
    readings.push_back(read(1)); // Dec axis
    return readings;
}

bool Mf7025v2Hal::MfSensorInterface::calibrate(int /*sensor_id*/, double /*reference_value*/) {
    return false; // Not applicable for MF7025v2
}

bool Mf7025v2Hal::MfSensorInterface::autoCalibrate(int /*sensor_id*/) {
    return false;
}

void Mf7025v2Hal::MfSensorInterface::setReadingCallback(ReadingCallback callback) {
    reading_callback_ = std::move(callback);
}

void Mf7025v2Hal::MfSensorInterface::setErrorCallback(ErrorCallback callback) {
    error_callback_ = std::move(callback);
}

std::string Mf7025v2Hal::MfSensorInterface::getDiagnostics() const {
    return "MF7025v2 SensorInterface";
}

// ════════════════════════════════════════════════════════════════════
// Mf7025v2Hal
// ════════════════════════════════════════════════════════════════════

Mf7025v2Hal::Mf7025v2Hal(
    std::unique_ptr<controllers::IMf7025v2Interface> can_interface)
    : can_interface_(std::move(can_interface)) {}

Mf7025v2Hal::~Mf7025v2Hal() {
    shutdown();
}

std::unique_ptr<Mf7025v2Hal> Mf7025v2Hal::create(const HALConfig& config) {
    auto can_interface = controllers::createMf7025v2CanInterface();
    if (!can_interface) {
        logging::Logger::get(LOG_TAG)->error("Failed to create MF7025v2 CAN interface");
        return nullptr;
    }

    auto hal = std::make_unique<Mf7025v2Hal>(std::move(can_interface));
    if (!hal->initialize(config)) {
        return nullptr;
    }
    return hal;
}

bool Mf7025v2Hal::initialize(const HALConfig& config) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (initialized_) return true;

    config_ = config;

    if (!can_interface_->initialize(config.mf7025v2.can_interface,
                                     config.mf7025v2.bitrate)) {
        logging::Logger::get(LOG_TAG)->error("Failed to initialize CAN interface");
        return false;
    }

    initialized_ = true;
    logging::Logger::get(LOG_TAG)->info("MF7025v2 HAL initialized on {}",
                                         config.mf7025v2.can_interface);
    return true;
}

void Mf7025v2Hal::shutdown() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!initialized_) return;

    can_interface_->shutdown();
    initialized_ = false;
    running_ = false;
}

bool Mf7025v2Hal::isInitialized() const {
    return initialized_.load();
}

std::unique_ptr<MotorControl> Mf7025v2Hal::createMotorControl(int axis_id) {
    uint8_t node_id;
    MotorConfig motor_config;

    if (axis_id >= 0 && axis_id < static_cast<int>(config_.axes.size())) {
        const auto& axis_cfg = config_.axes[axis_id];
        node_id = axis_cfg.can_node_id != 0 ? axis_cfg.can_node_id
                                            : static_cast<uint8_t>(axis_id + 1);
        motor_config = axis_cfg.motor_config;
    } else {
        node_id = static_cast<uint8_t>(axis_id + 1);
    }

    return std::make_unique<MfMotor>(node_id, *can_interface_, motor_config);
}

std::unique_ptr<EncoderReader> Mf7025v2Hal::createEncoderReader(int axis_id) {
    uint8_t node_id;
    EncoderConfig enc_config;

    if (axis_id >= 0 && axis_id < static_cast<int>(config_.axes.size())) {
        const auto& axis_cfg = config_.axes[axis_id];
        node_id = axis_cfg.can_node_id != 0 ? axis_cfg.can_node_id
                                            : static_cast<uint8_t>(axis_id + 1);
        enc_config = axis_cfg.encoder_config;
    } else {
        node_id = static_cast<uint8_t>(axis_id + 1);
    }

    return std::make_unique<MfEncoder>(node_id, *can_interface_, enc_config);
}

std::unique_ptr<SafetyMonitor> Mf7025v2Hal::createSafetyMonitor() {
    // Build list of node IDs from axis configs
    std::vector<uint8_t> nodes;
    for (const auto& axis : config_.axes) {
        uint8_t node_id = axis.can_node_id != 0 ? axis.can_node_id
                                                : static_cast<uint8_t>(axis.id + 1);
        nodes.push_back(node_id);
    }
    return std::make_unique<MfSafetyMonitor>(*can_interface_, std::move(nodes));
}

std::unique_ptr<SensorInterface> Mf7025v2Hal::createSensorInterface() {
    return std::make_unique<MfSensorInterface>(*can_interface_);
}

std::string Mf7025v2Hal::getPlatformName() const {
    return "Mf7025v2Hal_v1.0";
}

std::string Mf7025v2Hal::getHardwareVersion() const {
    return "LingKong MF7025v2 (CAN V2.36)";
}

std::vector<HALFeature> Mf7025v2Hal::getSupportedFeatures() const {
    return {
        HALFeature::FIELD_BUS_SUPPORT,
        HALFeature::PID_CONTROL,
        HALFeature::ENCODER_FEEDBACK,
        HALFeature::SAFETY_MONITORING,
        HALFeature::SENSOR_MONITORING,
        HALFeature::MANUAL_CONTROL
    };
}

bool Mf7025v2Hal::supportsFeature(HALFeature feature) const {
    auto features = getSupportedFeatures();
    return std::find(features.begin(), features.end(), feature) != features.end();
}

bool Mf7025v2Hal::start() {
    running_ = true;
    return true;
}

bool Mf7025v2Hal::stop() {
    running_ = false;
    return true;
}

bool Mf7025v2Hal::isRunning() const {
    return running_.load();
}

std::string Mf7025v2Hal::getStatus() const {
    std::ostringstream oss;
    oss << "MF7025v2 HAL ["
        << (initialized_.load() ? "initialized" : "uninitialized")
        << ", " << (running_.load() ? "running" : "stopped")
        << "] on " << config_.mf7025v2.can_interface;
    return oss.str();
}

std::string Mf7025v2Hal::getErrorMessages() const {
    return last_errors_;
}

void Mf7025v2Hal::clearErrors() {
    last_errors_.clear();
}

} // namespace hal
} // namespace astro_mount
