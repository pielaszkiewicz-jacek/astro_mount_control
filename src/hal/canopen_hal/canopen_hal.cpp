// CANopen/CiA 402 HAL — NiMotion STMP42SXI over SocketCAN.

#ifndef __linux__
#error "CanOpenHAL requires Linux (SocketCAN)."
#endif

#include "hal/canopen_hal/canopen_hal.h"
#include "hal/sensor_interface.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <sstream>

namespace astro_mount {
namespace hal {

using controllers::CanOpenStatus;

namespace {

uint32_t degPerSecToCounts(double deg_s, double counts_per_degree) {
    const double v = std::abs(deg_s) * counts_per_degree;
    if (v < 1.0) return 1;
    return static_cast<uint32_t>(v);
}

} // namespace

// ── CanOpenMotor ───────────────────────────────────────────────────────────

CanOpenHAL::CanOpenMotor::CanOpenMotor(int axis_id, uint8_t can_node_id, CanOpenHAL* parent)
    : axis_id_(axis_id), can_node_id_(can_node_id), parent_(parent),
      start_time_(std::chrono::steady_clock::now()) {}

CanOpenHAL::CanOpenMotor::~CanOpenMotor() = default;

bool CanOpenHAL::CanOpenMotor::enable() {
    auto* iface = parent_ ? parent_->getCanInterface() : nullptr;
    if (!iface) return false;
    const bool ok = iface->enableDrive(static_cast<uint8_t>(axis_id_));
    enabled_ = ok;
    return ok;
}

bool CanOpenHAL::CanOpenMotor::disable() {
    auto* iface = parent_ ? parent_->getCanInterface() : nullptr;
    if (!iface) return false;
    const bool ok = iface->disableDrive(static_cast<uint8_t>(axis_id_));
    if (ok) enabled_ = false;
    return ok;
}

bool CanOpenHAL::CanOpenMotor::isEnabled() const { return enabled_; }

bool CanOpenHAL::CanOpenMotor::setPosition(double position_deg, double velocity_deg_s,
                                           double acceleration_deg_s2) {
    applySpeedBasedPid(velocity_deg_s);
    auto* iface = parent_ ? parent_->getCanInterface() : nullptr;
    if (!iface) return false;
    const double cpd = config_.encoder_counts_per_degree;
    const int32_t target = static_cast<int32_t>(position_deg * cpd);
    const uint32_t vel = degPerSecToCounts(velocity_deg_s, cpd);
    const uint32_t accel = degPerSecToCounts(acceleration_deg_s2, cpd);
    const bool ok = iface->setPositionTarget(static_cast<uint8_t>(axis_id_),
                                              target, vel, accel);
    if (ok) moving_ = true;
    return ok;
}

bool CanOpenHAL::CanOpenMotor::setPositionRelative(double position_deg, double velocity_deg_s,
                                                   double acceleration_deg_s2) {
    applySpeedBasedPid(velocity_deg_s);
    auto* iface = parent_ ? parent_->getCanInterface() : nullptr;
    if (!iface) return false;
    const double cpd = config_.encoder_counts_per_degree;
    const int32_t delta = static_cast<int32_t>(position_deg * cpd);
    const uint32_t vel = degPerSecToCounts(velocity_deg_s, cpd);
    const uint32_t accel = degPerSecToCounts(acceleration_deg_s2, cpd);
    const bool ok = iface->setPositionTargetRelative(static_cast<uint8_t>(axis_id_),
                                                      delta, vel, accel);
    if (ok) moving_ = true;
    return ok;
}

bool CanOpenHAL::CanOpenMotor::setVelocity(double velocity_deg_s, double acceleration_deg_s2) {
    applySpeedBasedPid(velocity_deg_s);
    auto* iface = parent_ ? parent_->getCanInterface() : nullptr;
    if (!iface) return false;
    const double cpd = config_.encoder_counts_per_degree;
    const int32_t vel = static_cast<int32_t>(velocity_deg_s * cpd);
    const uint32_t accel = degPerSecToCounts(acceleration_deg_s2, cpd);
    const bool ok = iface->setVelocityTarget(static_cast<uint8_t>(axis_id_),
                                             vel, accel);
    if (ok) moving_ = true;
    return ok;
}

bool CanOpenHAL::CanOpenMotor::setTorque(double /*torque_percent*/) {
    // STMP42SXI exposes CiA 402 torque mode (6071h, mode 0x0A); not implemented
    // in this HAL subset yet.
    return false;
}

bool CanOpenHAL::CanOpenMotor::stop() {
    auto* iface = parent_ ? parent_->getCanInterface() : nullptr;
    if (!iface) return false;
    const bool ok = iface->stopAxis(static_cast<uint8_t>(axis_id_));
    if (ok) moving_ = false;
    return ok;
}

bool CanOpenHAL::CanOpenMotor::emergencyStop() {
    auto* iface = parent_ ? parent_->getCanInterface() : nullptr;
    if (!iface) return false;
    const bool ok = iface->emergencyStop(static_cast<uint8_t>(axis_id_));
    if (ok) { moving_ = false; enabled_ = false; }
    return ok;
}

double CanOpenHAL::CanOpenMotor::getActualPosition() const { return actual_position_; }
double CanOpenHAL::CanOpenMotor::getActualVelocity() const { return actual_velocity_; }
double CanOpenHAL::CanOpenMotor::getActualTorque() const { return actual_torque_; }
bool CanOpenHAL::CanOpenMotor::isMoving() const { return moving_; }
bool CanOpenHAL::CanOpenMotor::targetReached() const { return target_reached_; }
bool CanOpenHAL::CanOpenMotor::inErrorState() const { return error_state_; }

std::string CanOpenHAL::CanOpenMotor::getErrorString() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return error_message_;
}

bool CanOpenHAL::CanOpenMotor::clearErrors() {
    auto* iface = parent_ ? parent_->getCanInterface() : nullptr;
    if (!iface) return false;
    const bool ok = iface->clearErrors(static_cast<uint8_t>(axis_id_));
    if (ok) error_state_ = false;
    return ok;
}

bool CanOpenHAL::CanOpenMotor::zeroPosition() {
    // STMP42SXI: set the origin through the communication-given VDI level
    // (2031h:01h). The target VDI must be configured as "set origin" in 2017h.
    auto* iface = parent_ ? parent_->getCanInterface() : nullptr;
    if (!iface) return false;
    return iface->sendSDO(static_cast<uint8_t>(axis_id_), 0x2031, 0x01, 0x02, 2);
}

bool CanOpenHAL::CanOpenMotor::home() {
    auto* iface = parent_ ? parent_->getCanInterface() : nullptr;
    if (!iface) return false;
    return iface->startHoming(static_cast<uint8_t>(axis_id_));
}

bool CanOpenHAL::CanOpenMotor::writePidLoopRam(int loop, double kp, double ki, double kd) {
    (void)ki;
    (void)kd;
    auto* iface = parent_ ? parent_->getCanInterface() : nullptr;
    if (!iface) return false;

    // STMP42SXI gain set:
    //   loop 1 (current)  -> 2001h:1Ch CurrentLoopCutoffFreq (Hz)
    //   loop 2 (speed)    -> 2008h:01h SpeedLoopGain          (scale 0.1Hz)
    //   loop 3 (position) -> 2008h:03h PositionLoopGain       (scale 1)
    uint16_t index = 0;
    uint16_t value = 0;
    switch (loop) {
        case 1:
            index = 0x2001;
            value = static_cast<uint16_t>(kp);
            return iface->sendSDO(static_cast<uint8_t>(axis_id_), index, 0x1C, value, 2);
        case 2:
            index = 0x2008;
            value = static_cast<uint16_t>(kp * 10.0);
            return iface->sendSDO(static_cast<uint8_t>(axis_id_), index, 0x01, value, 2);
        case 3:
            index = 0x2008;
            value = static_cast<uint16_t>(kp);
            return iface->sendSDO(static_cast<uint8_t>(axis_id_), index, 0x03, value, 2);
        default:
            return false;
    }
}

bool CanOpenHAL::CanOpenMotor::applySpeedPidSchedule(
        const std::vector<hal::SpeedPidEntry>& schedule,
        bool enabled, double update_interval_ms,
        bool /*send_speed_pid*/, bool /*send_current_pid*/, bool /*send_position_pid*/) {
    std::lock_guard<std::mutex> lock(mutex_);
    speed_pid_schedule_ = schedule;
    speed_pid_enabled_ = enabled && !schedule.empty();
    speed_pid_update_interval_ms_ = update_interval_ms > 0.0 ? update_interval_ms : 50.0;
    // Reset the last-sent cache so the next command applies the gains for the
    // current speed band (the drive may have been re-powered meanwhile).
    last_sent_speed_pid_ = LastSentSpeedPid{};
    last_speed_pid_update_ = std::chrono::steady_clock::time_point{};
    return true;
}

void CanOpenHAL::CanOpenMotor::applySpeedBasedPid(double speed_deg_s) {
    if (!speed_pid_enabled_ || speed_pid_schedule_.empty()) return;

    // Convert servo degrees/second → motor shaft RPM. 1 RPM = 360°/60s = 6°/s.
    const double rpm = std::abs(speed_deg_s) / 6.0;

    // Throttle re-evaluations to the configured minimum interval to avoid
    // spamming the CAN bus with SDO writes at high command rates.
    auto now = std::chrono::steady_clock::now();
    if (last_speed_pid_update_.time_since_epoch().count() != 0) {
        auto elapsed_ms =
            std::chrono::duration<double, std::milli>(now - last_speed_pid_update_).count();
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

    // Skip the write if the speed-loop gains already match what was last sent.
    if (selected->speed_kp == last_sent_speed_pid_.speed_kp &&
        selected->speed_ki == last_sent_speed_pid_.speed_ki) {
        return;
    }

    auto* iface = parent_ ? parent_->getCanInterface() : nullptr;
    if (!iface) return;

    // Speed loop gains on STM42/STMP42SXI:
    //   2008h:01h = SpeedLoopGain (scale 0.1 Hz)
    //   2008h:02h = SpeedLoopIntegral
    const uint16_t speed_kp_reg = static_cast<uint16_t>(std::lround(selected->speed_kp * 10.0));
    const uint16_t speed_ki_reg = static_cast<uint16_t>(std::lround(selected->speed_ki));

    if (!iface->sendSDO(static_cast<uint8_t>(axis_id_), 0x2008, 0x01, speed_kp_reg, 2)) {
        return;
    }
    if (!iface->sendSDO(static_cast<uint8_t>(axis_id_), 0x2008, 0x02, speed_ki_reg, 2)) {
        return;
    }

    last_sent_speed_pid_.speed_kp = selected->speed_kp;
    last_sent_speed_pid_.speed_ki = selected->speed_ki;
    last_speed_pid_update_ = now;
}

bool CanOpenHAL::CanOpenMotor::configure(const MotorConfig& config) {
    std::lock_guard<std::mutex> lock(mutex_);
    config_ = config;
    return true;
}

MotorConfig CanOpenHAL::CanOpenMotor::getConfiguration() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return config_;
}

void CanOpenHAL::CanOpenMotor::setPositionCallback(PositionCallback callback) {
    std::lock_guard<std::mutex> lock(mutex_);
    position_callback_ = std::move(callback);
}

void CanOpenHAL::CanOpenMotor::setErrorCallback(ErrorCallback callback) {
    std::lock_guard<std::mutex> lock(mutex_);
    error_callback_ = std::move(callback);
}

void CanOpenHAL::CanOpenMotor::setStateChangeCallback(StateChangeCallback callback) {
    std::lock_guard<std::mutex> lock(mutex_);
    state_change_callback_ = std::move(callback);
}

double CanOpenHAL::CanOpenMotor::getTemperature() const { return temperature_; }
double CanOpenHAL::CanOpenMotor::getCurrent() const { return current_; }
double CanOpenHAL::CanOpenMotor::getVoltage() const { return voltage_; }

uint32_t CanOpenHAL::CanOpenMotor::getOperationTime() const {
    return static_cast<uint32_t>(
        std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::steady_clock::now() - start_time_).count());
}

void CanOpenHAL::CanOpenMotor::updateStatus(const CanOpenStatus& st) {
    const double cpd = config_.encoder_counts_per_degree > 0.0
                           ? config_.encoder_counts_per_degree : 1.0;
    if (st.position_valid) {
        actual_position_ = static_cast<double>(st.actual_position) / cpd;
    }
    actual_velocity_ = static_cast<double>(st.actual_velocity) / cpd;
    actual_torque_ = static_cast<double>(st.actual_current) / 10.0;
    enabled_ = st.enabled;
    moving_ = st.moving;
    target_reached_ = st.target_reached;
    error_state_ = st.fault;

    if (error_state_) {
        std::lock_guard<std::mutex> lock(mutex_);
        error_message_ = "drive fault (status word 0x" +
                         [] (uint16_t sw) {
                             std::ostringstream os;
                             os << std::hex << sw;
                             return os.str();
                         }(st.status_word) + ")";
    }
}

// ── CanOpenEncoder ─────────────────────────────────────────────────────────

CanOpenHAL::CanOpenEncoder::CanOpenEncoder(int axis_id, uint8_t can_node_id, CanOpenHAL* parent)
    : axis_id_(axis_id), can_node_id_(can_node_id), parent_(parent),
      start_time_(std::chrono::steady_clock::now()) {}

CanOpenHAL::CanOpenEncoder::~CanOpenEncoder() = default;

bool CanOpenHAL::CanOpenEncoder::initialize(const EncoderConfig& config) {
    config_ = config;
    initialized_ = true;
    return true;
}

void CanOpenHAL::CanOpenEncoder::shutdown() { initialized_ = false; }
bool CanOpenHAL::CanOpenEncoder::isInitialized() const { return initialized_; }

EncoderReading CanOpenHAL::CanOpenEncoder::read() const {
    EncoderReading reading;
    auto* iface = parent_ ? parent_->getCanInterface() : nullptr;
    if (!iface) {
        reading.data_valid = false;
        return reading;
    }
    controllers::CanOpenPositionData data;
    if (!iface->getPositionData(static_cast<uint8_t>(axis_id_), data)) {
        reading.data_valid = false;
        error_count_++;
        return reading;
    }
    const double cpd = config_.counts_per_degree > 0.0 ? config_.counts_per_degree : 1.0;
    reading.position_deg = static_cast<double>(data.actual_position) / cpd
                           + calibration_offset_;
    reading.velocity_deg_s = static_cast<double>(data.actual_velocity) / cpd;
    reading.raw_counts = data.actual_position;
    reading.data_valid = true;
    reading.timestamp = std::chrono::steady_clock::now();
    total_readings_++;
    return reading;
}

bool CanOpenHAL::CanOpenEncoder::isDataValid() const { return true; }
double CanOpenHAL::CanOpenEncoder::getUpdateRate() const { return 100.0; }

bool CanOpenHAL::CanOpenEncoder::calibrate(double reference_position_deg) {
    // STMP42SXI home offset is applied in the drive (607Ch). Here we track a
    // software offset so read() can report the calibrated position.
    calibration_offset_ = reference_position_deg;
    return true;
}

bool CanOpenHAL::CanOpenEncoder::autoCalibrate() { return false; }
double CanOpenHAL::CanOpenEncoder::getCalibrationOffset() const { return calibration_offset_; }
void CanOpenHAL::CanOpenEncoder::setCalibrationOffset(double offset_deg) { calibration_offset_ = offset_deg; }
bool CanOpenHAL::CanOpenEncoder::saveCalibration() { return true; }
bool CanOpenHAL::CanOpenEncoder::loadCalibration() { return true; }

EncoderType CanOpenHAL::CanOpenEncoder::getType() const { return EncoderType::ABSOLUTE; }
EncoderInterface CanOpenHAL::CanOpenEncoder::getInterface() const {
    return EncoderInterface::SSI;
}
uint32_t CanOpenHAL::CanOpenEncoder::getResolution() const { return config_.resolution; }
double CanOpenHAL::CanOpenEncoder::getCountsPerDegree() const { return config_.counts_per_degree; }

void CanOpenHAL::CanOpenEncoder::setReadingCallback(ReadingCallback callback) {
    reading_callback_ = std::move(callback);
}
void CanOpenHAL::CanOpenEncoder::setErrorCallback(ErrorCallback callback) {
    error_callback_ = std::move(callback);
}

uint32_t CanOpenHAL::CanOpenEncoder::getTotalReadings() const { return total_readings_; }
uint32_t CanOpenHAL::CanOpenEncoder::getErrorCount() const { return error_count_; }
double CanOpenHAL::CanOpenEncoder::getUptime() const {
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - start_time_).count();
}
std::string CanOpenHAL::CanOpenEncoder::getDiagnostics() const {
    return "CANopen absolute encoder (6064h)";
}
bool CanOpenHAL::CanOpenEncoder::synchronize() { return true; }
bool CanOpenHAL::CanOpenEncoder::isSynchronized() const { return true; }

// ── CanOpenSafetyMonitor ───────────────────────────────────────────────────

CanOpenHAL::CanOpenSafetyMonitor::CanOpenSafetyMonitor(CanOpenHAL* parent)
    : parent_(parent) {}

CanOpenHAL::CanOpenSafetyMonitor::~CanOpenSafetyMonitor() = default;

bool CanOpenHAL::CanOpenSafetyMonitor::initialize(const SafetyConfig& config) {
    config_ = config;
    initialized_ = true;
    return true;
}

void CanOpenHAL::CanOpenSafetyMonitor::shutdown() { initialized_ = false; }
bool CanOpenHAL::CanOpenSafetyMonitor::isInitialized() const { return initialized_; }

SafetyStatus CanOpenHAL::CanOpenSafetyMonitor::getStatus() const {
    SafetyStatus status;
    status.overall_state = SafetyStatus::State::NORMAL;
    if (!parent_) return status;
    for (int axis = 0; axis < 2; ++axis) {
        const auto st = parent_->lastStatus(axis);
        status.axes_status[axis].communication_ok = (st.status_word != 0);
        status.axes_status[axis].temperature_ok = true;
        status.axes_status[axis].current_ok = true;
        status.axes_status[axis].voltage_ok = true;
        if (st.fault) {
            status.axes_status[axis].limits_ok = false;
            status.axes_status[axis].error_message = "drive fault";
            status.overall_state = SafetyStatus::State::ERROR;
        }
    }
    return status;
}

bool CanOpenHAL::CanOpenSafetyMonitor::checkLimits(int /*axis_id*/) { return true; }

bool CanOpenHAL::CanOpenSafetyMonitor::emergencyStop(int axis_id) {
    if (!parent_ || !parent_->getCanInterface()) return false;
    return parent_->getCanInterface()->emergencyStop(static_cast<uint8_t>(axis_id));
}

bool CanOpenHAL::CanOpenSafetyMonitor::clearErrors(int axis_id) {
    if (!parent_ || !parent_->getCanInterface()) return false;
    return parent_->getCanInterface()->clearErrors(static_cast<uint8_t>(axis_id));
}

void CanOpenHAL::CanOpenSafetyMonitor::setLimitCallback(LimitCallback callback) {
    limit_callback_ = std::move(callback);
}
void CanOpenHAL::CanOpenSafetyMonitor::setErrorCallback(ErrorCallback callback) {
    error_callback_ = std::move(callback);
}
std::string CanOpenHAL::CanOpenSafetyMonitor::getDiagnostics() const {
    return "CANopen safety monitor (heartbeat/EMCY)";
}

// ── CanOpenHAL ─────────────────────────────────────────────────────────────

CanOpenHAL::CanOpenHAL(std::unique_ptr<controllers::ICanOpenInterface> can_iface)
    : can_iface_(std::move(can_iface)) {}

CanOpenHAL::~CanOpenHAL() {
    shutdown();
}

bool CanOpenHAL::initialize(const HALConfig& config) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (initialized_) return true;
    config_ = config;
    if (!can_iface_) return false;

    controllers::ICanOpenInterface::Config iface_config;
    iface_config.library = config.canopen.library;
    iface_config.interface_name = config.canopen.interface_name;
    iface_config.bitrate = config.canopen.bitrate;
    iface_config.node_id = config.canopen.node_id;
    iface_config.sdo_timeout_ms = config.canopen.sdo_timeout_ms;
    iface_config.pdo_update_rate = config.canopen.pdo_update_rate;
    iface_config.accel_mode = config.canopen.accel_mode;
    iface_config.pdo_config_enabled = config.canopen.pdo_config_enabled;
    iface_config.position_rewind_enabled = config.canopen.position_rewind_enabled;
    iface_config.position_rewind_interval_seconds = config.canopen.position_rewind_interval_seconds;
    iface_config.position_rewind_threshold_percent = config.canopen.position_rewind_threshold_percent;

    // Build axis_id -> node_id mapping.
    iface_config.axis_node_ids.clear();
    for (const auto& axis : config.axes) {
        iface_config.axis_node_ids.push_back(axis.can_node_id);
    }

    if (!can_iface_->initialize(iface_config)) return false;

    for (const auto& axis : config.axes) {
        const int axis_id = axis.id;
        if (axis_id < 0 || axis_id >= 2) continue;
        motors_[axis_id] = std::make_unique<CanOpenMotor>(axis_id, axis.can_node_id, this);
        motors_[axis_id]->configure(axis.motor_config);
        // Install the speed-dependent speed-loop PID gain schedule (volatile
        // RAM writes applied on the next setPosition()/setVelocity() command).
        motors_[axis_id]->applySpeedPidSchedule(
            config.canopen.speed_pid_schedule,
            config.canopen.speed_pid_adaptation_enabled,
            config.canopen.speed_pid_adaptation_update_ms,
            true, false, false);
        encoders_[axis_id] = std::make_unique<CanOpenEncoder>(axis_id, axis.can_node_id, this);
        encoders_[axis_id]->initialize(axis.encoder_config);

        // Enable heartbeat producer so the NMT monitor can detect the node.
        if (config.canopen.enable_nmt) {
            can_iface_->setHeartbeatPeriod(static_cast<uint8_t>(axis_id),
                                           static_cast<uint16_t>(config.canopen.heartbeat_period_ms));
        }
        // Configure PDO mapping (TPDO1: 6041h+6064h, RPDO2: 6040h+607Ah).
        if (config.canopen.pdo_config_enabled) {
            can_iface_->configurePdo(static_cast<uint8_t>(axis_id), true);
        }
    }

    initialized_ = true;
    return true;
}

void CanOpenHAL::shutdown() {
    stop();
    std::lock_guard<std::mutex> lock(mutex_);
    if (initialized_) {
        if (can_iface_) can_iface_->shutdown();
        initialized_ = false;
    }
}

bool CanOpenHAL::isInitialized() const { return initialized_; }

std::unique_ptr<MotorControl> CanOpenHAL::createMotorControl(int axis_id) {
    if (axis_id < 0 || axis_id >= 2 || !motors_[axis_id]) return nullptr;
    return std::make_unique<CanOpenMotorProxy>(motors_[axis_id].get());
}

std::unique_ptr<EncoderReader> CanOpenHAL::createEncoderReader(int axis_id) {
    if (axis_id < 0 || axis_id >= 2 || !encoders_[axis_id]) return nullptr;
    return std::make_unique<CanOpenEncoderProxy>(encoders_[axis_id].get());
}

std::unique_ptr<SafetyMonitor> CanOpenHAL::createSafetyMonitor() {
    return std::make_unique<CanOpenSafetyMonitor>(this);
}

std::unique_ptr<SensorInterface> CanOpenHAL::createSensorInterface() {
    // STMP42SXI does not expose environment sensors.
    return nullptr;
}

std::string CanOpenHAL::getPlatformName() const { return "CANopen/CiA 402 (STMP42SXI)"; }
std::string CanOpenHAL::getHardwareVersion() const { return "STMP42SXI"; }

std::vector<HALFeature> CanOpenHAL::getSupportedFeatures() const {
    return { HALFeature::FIELD_BUS_SUPPORT, HALFeature::TRAJECTORY_CONTROL,
             HALFeature::ENCODER_FEEDBACK, HALFeature::SAFETY_MONITORING,
             HALFeature::REAL_TIME_CONTROL };
}

bool CanOpenHAL::supportsFeature(HALFeature feature) const {
    const auto features = getSupportedFeatures();
    return std::find(features.begin(), features.end(), feature) != features.end();
}

bool CanOpenHAL::start() {
    if (!initialized_) return false;
    if (running_) return true;
    running_ = true;
    monitor_thread_ = std::thread(&CanOpenHAL::monitorLoop, this);
    return true;
}

bool CanOpenHAL::stop() {
    if (!running_) return true;
    running_ = false;
    if (monitor_thread_.joinable()) monitor_thread_.join();
    return true;
}

bool CanOpenHAL::isRunning() const { return running_; }

std::string CanOpenHAL::getStatus() const {
    std::lock_guard<std::mutex> lock(status_mutex_);
    std::ostringstream os;
    for (int axis = 0; axis < 2; ++axis) {
        os << "axis" << axis
           << " enabled=" << (last_status_[axis].enabled ? 1 : 0)
           << " moving=" << (last_status_[axis].moving ? 1 : 0)
           << " reached=" << (last_status_[axis].target_reached ? 1 : 0)
           << " fault=" << (last_status_[axis].fault ? 1 : 0) << "; ";
    }
    return os.str();
}

std::string CanOpenHAL::getErrorMessages() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return last_error_;
}

void CanOpenHAL::clearErrors() {
    if (can_iface_) {
        for (int axis = 0; axis < 2; ++axis) {
            can_iface_->clearErrors(static_cast<uint8_t>(axis));
        }
    }
}

controllers::CanOpenStatus CanOpenHAL::lastStatus(int axis_id) const {
    std::lock_guard<std::mutex> lock(status_mutex_);
    if (axis_id < 0 || axis_id >= 2) return {};
    return last_status_[axis_id];
}

void CanOpenHAL::monitorLoop() {
    while (running_) {
        if (can_iface_) can_iface_->pollEvents();

        for (int axis = 0; axis < 2; ++axis) {
            if (!motors_[axis]) continue;
            controllers::CanOpenStatus st;
            if (can_iface_ && can_iface_->getDriveStatus(static_cast<uint8_t>(axis), st)) {
                motors_[axis]->updateStatus(st);
                {
                    std::lock_guard<std::mutex> lock(status_mutex_);
                    last_status_[axis] = st;
                }
            } else {
                std::lock_guard<std::mutex> lock(mutex_);
                last_error_ = "CANopen status read failed for axis " + std::to_string(axis);
            }
        }
        const uint32_t interval = std::max<uint32_t>(10, config_.canopen.pdo_update_rate);
        std::this_thread::sleep_for(std::chrono::milliseconds(interval));
    }
}

} // namespace hal
} // namespace astro_mount
