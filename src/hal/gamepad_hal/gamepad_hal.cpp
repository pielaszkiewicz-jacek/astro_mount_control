#include "gamepad_hal.h"
#include "gamepad_input_evdev.h"
#include <cmath>
#include <algorithm>
#include <sstream>
#include "logging/logger.h"

namespace astro_mount {
namespace hal {

// ============================================================================
// GamepadHAL
// ============================================================================

GamepadHAL::GamepadHAL(const HALConfig& config,
                       std::unique_ptr<GamepadInput> gamepad_input)
    : config_(config)
{
    if (gamepad_input) {
        gamepad_input_ = std::move(gamepad_input);
        owns_gamepad_ = false;
    } else {
        gamepad_input_ = createDefaultGamepadInput();
        owns_gamepad_ = true;
    }

    // Apply deadzone from config if provided
    if (config_.gamepad.deadzone > 0.0 && config_.gamepad.deadzone < 1.0) {
        manual_cfg_.deadzone = config_.gamepad.deadzone;
    }
    if (config_.gamepad.sensitivity > 0.0) {
        manual_cfg_.sensitivity = config_.gamepad.sensitivity;
    }
    if (config_.gamepad.max_velocity_deg_s > 0.0) {
        manual_cfg_.max_velocity_deg_s = config_.gamepad.max_velocity_deg_s;
    }
    if (config_.gamepad.invert_axis1) {
        manual_cfg_.invert_axis1 = true;
    }
    if (config_.gamepad.invert_axis2) {
        manual_cfg_.invert_axis2 = true;
    }
}

GamepadHAL::~GamepadHAL() {
    shutdown();
}

bool GamepadHAL::initialize(const HALConfig& config) {
    config_ = config;

    // Initialize gamepad input
    std::string device_path = config_.gamepad.device_path;
    if (!gamepad_input_->initialize(device_path)) {
        error_messages_ = "Failed to initialize gamepad input";
        logging::Logger::get("gamepad")->error("[GamepadHAL] {}", error_messages_);
        return false;
    }

    // Apply button and axis mappings from config (overrides defaults)
    if (!config_.gamepad.button_mapping.empty()) {
        gamepad_input_->applyButtonMapping(config_.gamepad.button_mapping);
    }
    if (!config_.gamepad.axis_mapping.empty()) {
        gamepad_input_->applyAxisMapping(config_.gamepad.axis_mapping);
    }

    initialized_ = true;
    logging::Logger::get("gamepad")->info("[GamepadHAL] Initialized with device: {}",
              gamepad_input_->getDeviceName());
    return true;
}

void GamepadHAL::shutdown() {
    stop();

    // Clear raw pointers BEFORE joining the update thread to prevent
    // use-after-free if the update loop is still running.
    {
        std::lock_guard<std::mutex> lock(motor_mutex_);
        motors_.clear();
        encoders_.clear();
        safety_monitor_ = nullptr;
        sensor_interface_ = nullptr;
    }

    if (gamepad_input_) {
        gamepad_input_->shutdown();
    }

    initialized_ = false;
}

bool GamepadHAL::isInitialized() const {
    return initialized_;
}

std::unique_ptr<MotorControl> GamepadHAL::createMotorControl(int axis_id) {
    if (axis_id < 0) return nullptr;
    auto motor = std::make_unique<GamepadMotorControl>(axis_id, *this);
    auto* raw = motor.get();
    {
        std::lock_guard<std::mutex> lock(motor_mutex_);
        // Ensure vector is large enough
        while ((int)motors_.size() <= axis_id) {
            motors_.push_back(nullptr);
        }
        motors_[axis_id] = raw;
    }
    return motor;
}

std::unique_ptr<EncoderReader> GamepadHAL::createEncoderReader(int axis_id) {
    if (axis_id < 0) return nullptr;
    auto* motor = getMotor(axis_id);
    auto encoder = std::make_unique<GamepadEncoderReader>(motor);
    auto* raw = encoder.get();
    {
        std::lock_guard<std::mutex> lock(motor_mutex_);
        while ((int)encoders_.size() <= axis_id) {
            encoders_.push_back(nullptr);
        }
        encoders_[axis_id] = raw;
    }
    return encoder;
}

std::unique_ptr<SafetyMonitor> GamepadHAL::createSafetyMonitor() {
    auto monitor = std::make_unique<GamepadSafetyMonitor>();
    safety_monitor_ = monitor.get();
    return monitor;
}

std::unique_ptr<SensorInterface> GamepadHAL::createSensorInterface() {
    auto si = std::make_unique<GamepadSensorInterface>();
    sensor_interface_ = si.get();
    return si;
}

std::string GamepadHAL::getPlatformName() const {
    return "GamepadHAL";
}

std::string GamepadHAL::getHardwareVersion() const {
    if (gamepad_input_ && gamepad_input_->isConnected()) {
        return gamepad_input_->getDeviceName();
    }
    return "No gamepad connected";
}

std::vector<HALFeature> GamepadHAL::getSupportedFeatures() const {
    return {
        HALFeature::MANUAL_CONTROL
    };
}

bool GamepadHAL::supportsFeature(HALFeature feature) const {
    return feature == HALFeature::MANUAL_CONTROL;
}

bool GamepadHAL::start() {
    if (!initialized_) return false;
    if (running_) return true;

    running_ = true;
    update_thread_ = std::thread(&GamepadHAL::updateLoop, this);

    logging::Logger::get("gamepad")->info("[GamepadHAL] Started update loop");
    return true;
}

bool GamepadHAL::stop() {
    if (running_) {
        running_ = false;
        if (update_thread_.joinable()) {
            update_thread_.join();
        }
    }
    return true;
}

bool GamepadHAL::isRunning() const {
    return running_;
}

std::string GamepadHAL::getStatus() const {
    std::lock_guard<std::mutex> lock(state_mutex_);
    std::ostringstream oss;
    oss << "GamepadHAL: "
        << (initialized_ ? "initialized" : "not initialized")
        << ", " << (running_ ? "running" : "stopped")
        << ", gamepad: " << (gamepad_input_ && gamepad_input_->isConnected() ? "connected" : "disconnected")
        << ", speed preset: " << getCurrentSpeedPreset() << " deg/s";
    return oss.str();
}

std::string GamepadHAL::getErrorMessages() const {
    std::lock_guard<std::mutex> lock(state_mutex_);
    return error_messages_;
}

void GamepadHAL::clearErrors() {
    std::lock_guard<std::mutex> lock(state_mutex_);
    error_messages_.clear();
}

void GamepadHAL::setManualControlConfig(const ManualControlConfig& cfg) {
    manual_cfg_ = cfg;
}

GamepadHAL::ManualControlConfig GamepadHAL::getManualControlConfig() const {
    return manual_cfg_;
}

double GamepadHAL::getCurrentSpeedPreset() const {
    int idx = speed_preset_idx_.load();
    if (idx < 0) idx = 0;
    if (idx >= (int)manual_cfg_.speed_presets_deg_s.size())
        idx = (int)manual_cfg_.speed_presets_deg_s.size() - 1;
    return manual_cfg_.speed_presets_deg_s[idx];
}

void GamepadHAL::cycleSpeedUp() {
    int idx = speed_preset_idx_.load();
    idx = std::min(idx + 1, (int)manual_cfg_.speed_presets_deg_s.size() - 1);
    speed_preset_idx_ = idx;
    logging::Logger::get("gamepad")->info("[GamepadHAL] Speed preset: {} deg/s", getCurrentSpeedPreset());
}

void GamepadHAL::cycleSpeedDown() {
    int idx = speed_preset_idx_.load();
    idx = std::max(idx - 1, 0);
    speed_preset_idx_ = idx;
    logging::Logger::get("gamepad")->info("[GamepadHAL] Speed preset: {} deg/s", getCurrentSpeedPreset());
}

GamepadMotorControl* GamepadHAL::getMotor(int axis_id) {
    std::lock_guard<std::mutex> lock(motor_mutex_);
    if (axis_id >= 0 && axis_id < (int)motors_.size()) {
        return motors_[axis_id];
    }
    return nullptr;
}

std::unique_ptr<GamepadInput> GamepadHAL::createDefaultGamepadInput() {
    return std::make_unique<EvdevGamepadInput>();
}

void GamepadHAL::updateLoop() {
    auto last_time = std::chrono::steady_clock::now();
    const auto update_interval = std::chrono::milliseconds(
        static_cast<int>(1000.0 / manual_cfg_.update_rate_hz));

    while (running_) {
        auto now = std::chrono::steady_clock::now();
        double dt = std::chrono::duration<double>(now - last_time).count();
        last_time = now;

        // Read gamepad state
        GamepadState gamepad_state;
        bool gamepad_ok = false;
        if (gamepad_input_) {
            gamepad_state = gamepad_input_->readState();
            gamepad_ok = gamepad_state.connected;
        }

        if (gamepad_ok) {
            // Handle button events
            if (gamepad_state.button_speed_up) {
                cycleSpeedUp();
            }
            if (gamepad_state.button_speed_down) {
                cycleSpeedDown();
            }

            // Compute axis velocities from gamepad sticks
            double axis1_raw = gamepad_state.axis_lx;
            double axis2_raw = gamepad_state.axis_ly;

            // Apply inversion
            if (manual_cfg_.invert_axis1) axis1_raw = -axis1_raw;
            if (manual_cfg_.invert_axis2) axis2_raw = -axis2_raw;

            // Apply deadzone
            double deadzone = manual_cfg_.deadzone;
            auto applyDeadzone = [deadzone](double val) -> double {
                if (std::abs(val) < deadzone) return 0.0;
                double sign = (val > 0.0) ? 1.0 : -1.0;
                return sign * (std::abs(val) - deadzone) / (1.0 - deadzone);
            };
            axis1_raw = applyDeadzone(axis1_raw);
            axis2_raw = applyDeadzone(axis2_raw);

            // Apply sensitivity curve
            auto applySensitivity = [](double val, double sens) -> double {
                if (sens != 1.0) {
                    return std::copysign(std::pow(std::abs(val), sens), val);
                }
                return val;
            };
            axis1_raw = applySensitivity(axis1_raw, manual_cfg_.sensitivity);
            axis2_raw = applySensitivity(axis2_raw, manual_cfg_.sensitivity);

            // Scale by current speed preset
            double max_speed = getCurrentSpeedPreset();
            double vel1 = axis1_raw * max_speed;
            double vel2 = axis2_raw * max_speed;

            // Update motors with gamepad-derived velocity
            // If the motor has a commanded velocity (from setVelocity()),
            // gamepad velocity OVERRIDES it when stick is deflected.
            {
                std::lock_guard<std::mutex> lock(motor_mutex_);
                for (size_t i = 0; i < motors_.size(); i++) {
                    if (!motors_[i]) continue;
                    double vel = (i == 0) ? vel1 : (i == 1) ? vel2 : 0.0;
                    motors_[i]->updateVelocity(vel);
                    motors_[i]->integratePosition(dt);
                }
            }
        }

        std::this_thread::sleep_for(update_interval);
    }
}

// ============================================================================
// GamepadMotorControl
// ============================================================================

GamepadMotorControl::GamepadMotorControl(int axis_id, GamepadHAL& parent)
    : axis_id_(axis_id), parent_(parent) {}

GamepadMotorControl::~GamepadMotorControl() = default;

bool GamepadMotorControl::enable() {
    enabled_ = true;
    emergency_stop_ = false;
    stop_requested_ = false;
    if (state_cb_) {
        state_cb_(true, isMoving());
    }
    return true;
}

bool GamepadMotorControl::disable() {
    enabled_ = false;
    if (state_cb_) {
        state_cb_(false, false);
    }
    return true;
}

bool GamepadMotorControl::isEnabled() const {
    return enabled_;
}

bool GamepadMotorControl::setPosition(double position_deg, double velocity_deg_s, double acceleration_deg_s2) {
    if (!enabled_ || emergency_stop_) return false;
    std::lock_guard<std::mutex> lock(motor_state_mutex_);
    // In gamepad mode, position control is not directly supported
    // Instead, set the target position and velocity so the motor
    // will auto-drive toward it when gamepad is in deadzone
    target_velocity_ = velocity_deg_s;
    // Position target will be handled outside (by MountController)
    actual_position_ = position_deg;
    return true;
}

bool GamepadMotorControl::setVelocity(double velocity_deg_s, double acceleration_deg_s2) {
    if (!enabled_ || emergency_stop_) return false;
    std::lock_guard<std::mutex> lock(motor_state_mutex_);
    commanded_velocity_ = velocity_deg_s;
    target_velocity_ = velocity_deg_s;
    return true;
}

bool GamepadMotorControl::setTorque(double torque_percent) {
    // Not supported in gamepad mode
    return false;
}

bool GamepadMotorControl::stop() {
    stop_requested_ = true;
    std::lock_guard<std::mutex> lock(motor_state_mutex_);
    commanded_velocity_ = 0.0;
    integrated_velocity_ = 0.0;
    actual_velocity_ = 0.0;
    return true;
}

bool GamepadMotorControl::emergencyStop() {
    emergency_stop_ = true;
    std::lock_guard<std::mutex> lock(motor_state_mutex_);
    commanded_velocity_ = 0.0;
    integrated_velocity_ = 0.0;
    actual_velocity_ = 0.0;
    return true;
}

double GamepadMotorControl::getActualPosition() const {
    std::lock_guard<std::mutex> lock(motor_state_mutex_);
    return actual_position_;
}

double GamepadMotorControl::getActualVelocity() const {
    std::lock_guard<std::mutex> lock(motor_state_mutex_);
    return actual_velocity_;
}

double GamepadMotorControl::getActualTorque() const {
    std::lock_guard<std::mutex> lock(motor_state_mutex_);
    return actual_torque_;
}

bool GamepadMotorControl::isMoving() const {
    if (emergency_stop_) return false;
    std::lock_guard<std::mutex> lock(motor_state_mutex_);
    return std::abs(actual_velocity_) > 0.001;
}

bool GamepadMotorControl::targetReached() const {
    // In gamepad mode, target is never "reached" in the traditional sense
    return !isMoving();
}

bool GamepadMotorControl::inErrorState() const {
    return in_error_;
}

std::string GamepadMotorControl::getErrorString() const {
    return error_string_;
}

bool GamepadMotorControl::configure(const MotorConfig& config) {
    motor_config_ = config;
    return true;
}

MotorConfig GamepadMotorControl::getConfiguration() const {
    return motor_config_;
}

void GamepadMotorControl::setPositionCallback(PositionCallback callback) {
    position_cb_ = callback;
}

void GamepadMotorControl::setErrorCallback(ErrorCallback callback) {
    error_cb_ = callback;
}

void GamepadMotorControl::setStateChangeCallback(StateChangeCallback callback) {
    state_cb_ = callback;
}

double GamepadMotorControl::getTemperature() const {
    return temperature_;
}

double GamepadMotorControl::getCurrent() const {
    return current_;
}

double GamepadMotorControl::getVoltage() const {
    return voltage_;
}

uint32_t GamepadMotorControl::getOperationTime() const {
    return operation_time_;
}

void GamepadMotorControl::updateVelocity(double velocity_deg_s) {
    std::lock_guard<std::mutex> lock(motor_state_mutex_);
    integrated_velocity_ = velocity_deg_s;

    // If gamepad is active (non-zero), it overrides commanded velocity
    if (std::abs(velocity_deg_s) > 0.001) {
        actual_velocity_ = velocity_deg_s;
    } else {
        // When gamepad is idle, fall back to commanded velocity
        actual_velocity_ = commanded_velocity_;
    }
}

void GamepadMotorControl::integratePosition(double dt) {
    if (dt <= 0.0 || dt > 1.0) return;  // Sanity check

    std::lock_guard<std::mutex> lock(motor_state_mutex_);

    // Integrate velocity to position
    actual_position_ += actual_velocity_ * dt;

    // Normalize position with wrap-around (avoids drift in long sessions)
    actual_position_ = std::fmod(actual_position_, 360.0);

    // Update operation time
    operation_time_ += static_cast<uint32_t>(dt);

    // Call position callback if set
    if (position_cb_) {
        position_cb_(actual_position_, actual_velocity_, actual_torque_);
    }
}

double GamepadMotorControl::getCommandedVelocity() const {
    std::lock_guard<std::mutex> lock(motor_state_mutex_);
    return commanded_velocity_;
}

void GamepadMotorControl::setCommandedVelocity(double vel) {
    std::lock_guard<std::mutex> lock(motor_state_mutex_);
    commanded_velocity_ = vel;
}

// ============================================================================
// GamepadEncoderReader
// ============================================================================

GamepadEncoderReader::GamepadEncoderReader(GamepadMotorControl* motor)
    : motor_(motor) {}

GamepadEncoderReader::~GamepadEncoderReader() = default;

bool GamepadEncoderReader::initialize(const EncoderConfig& config) {
    config_ = config;
    initialized_ = true;
    return true;
}

void GamepadEncoderReader::shutdown() {
    initialized_ = false;
}

bool GamepadEncoderReader::isInitialized() const {
    return initialized_;
}

EncoderReading GamepadEncoderReader::read() const {
    EncoderReading reading;
    reading.timestamp = std::chrono::steady_clock::now();

    if (motor_) {
        reading.position_deg = motor_->getActualPosition() - calibration_offset_;
        reading.velocity_deg_s = motor_->getActualVelocity();
        reading.data_valid = motor_->isEnabled() && !motor_->inErrorState();
        reading.raw_counts = static_cast<int32_t>(reading.position_deg * config_.counts_per_degree);
        reading.direction = reading.velocity_deg_s >= 0.0;
    } else {
        reading.data_valid = false;
    }

    total_readings_++;
    if (reading_cb_) {
        reading_cb_(reading);
    }

    return reading;
}

bool GamepadEncoderReader::isDataValid() const {
    return motor_ && motor_->isEnabled() && !motor_->inErrorState();
}

double GamepadEncoderReader::getUpdateRate() const {
    return config_.update_rate_hz;
}

bool GamepadEncoderReader::calibrate(double reference_position_deg) {
    if (!motor_) return false;
    calibration_offset_ = motor_->getActualPosition() - reference_position_deg;
    return true;
}

bool GamepadEncoderReader::autoCalibrate() {
    calibration_offset_ = 0.0;
    return true;
}

double GamepadEncoderReader::getCalibrationOffset() const {
    return calibration_offset_;
}

void GamepadEncoderReader::setCalibrationOffset(double offset_deg) {
    calibration_offset_ = offset_deg;
}

bool GamepadEncoderReader::saveCalibration() {
    // No persistent storage in this implementation
    return true;
}

bool GamepadEncoderReader::loadCalibration() {
    return true;
}

EncoderType GamepadEncoderReader::getType() const {
    return EncoderType::VIRTUAL;
}

EncoderInterface GamepadEncoderReader::getInterface() const {
    return EncoderInterface::QUADRATURE;
}

uint32_t GamepadEncoderReader::getResolution() const {
    return config_.resolution;
}

double GamepadEncoderReader::getCountsPerDegree() const {
    return config_.counts_per_degree;
}

void GamepadEncoderReader::setReadingCallback(ReadingCallback callback) {
    reading_cb_ = callback;
}

void GamepadEncoderReader::setErrorCallback(ErrorCallback callback) {
    error_cb_ = callback;
}

uint32_t GamepadEncoderReader::getTotalReadings() const {
    return total_readings_;
}

uint32_t GamepadEncoderReader::getErrorCount() const {
    return 0;
}

double GamepadEncoderReader::getUptime() const {
    return 0.0;
}

std::string GamepadEncoderReader::getDiagnostics() const {
    return "GamepadEncoderReader (virtual encoder)";
}

bool GamepadEncoderReader::synchronize() {
    return true;
}

bool GamepadEncoderReader::isSynchronized() const {
    return true;
}

// ============================================================================
// GamepadSafetyMonitor
// ============================================================================

GamepadSafetyMonitor::GamepadSafetyMonitor() {
    status_.overall_state = SafetyStatus::State::NORMAL;
    status_.timestamp = std::chrono::system_clock::now();
}

GamepadSafetyMonitor::~GamepadSafetyMonitor() = default;

bool GamepadSafetyMonitor::initialize(const SafetyConfig& config) {
    safety_config_ = config;
    initialized_ = true;
    return true;
}

void GamepadSafetyMonitor::shutdown() {
    initialized_ = false;
}

bool GamepadSafetyMonitor::isInitialized() const {
    return initialized_;
}

SafetyStatus GamepadSafetyMonitor::getStatus() const {
    return status_;
}

bool GamepadSafetyMonitor::checkLimits(int axis_id) {
    // No real limits to check in gamepad mode
    return true;
}

bool GamepadSafetyMonitor::emergencyStop(int axis_id) {
    status_.overall_state = SafetyStatus::State::EMERGENCY_STOP;
    status_.emergency_stop_active = true;
    return true;
}

bool GamepadSafetyMonitor::clearErrors(int axis_id) {
    status_.overall_state = SafetyStatus::State::NORMAL;
    status_.emergency_stop_active = false;
    return true;
}

void GamepadSafetyMonitor::setLimitCallback(LimitCallback callback) {
    limit_cb_ = callback;
}

void GamepadSafetyMonitor::setErrorCallback(ErrorCallback callback) {
    error_cb_ = callback;
}

std::string GamepadSafetyMonitor::getDiagnostics() const {
    return "GamepadSafetyMonitor (no real hardware limits)";
}

// ============================================================================
// GamepadSensorInterface
// ============================================================================

GamepadSensorInterface::GamepadSensorInterface() {
    env_sensor_.id = 0;
    env_sensor_.name = "Gamepad_Environment";
    env_sensor_.type = SensorType::TEMPERATURE;
    env_sensor_.interface = SensorInterfaceType::ANALOG;
    env_sensor_.update_rate_hz = 1;
}

GamepadSensorInterface::~GamepadSensorInterface() = default;

bool GamepadSensorInterface::initialize(const SensorConfig& config) {
    config_ = config;
    initialized_ = true;
    return true;
}

void GamepadSensorInterface::shutdown() {
    initialized_ = false;
}

bool GamepadSensorInterface::isInitialized() const {
    return initialized_;
}

SensorReading GamepadSensorInterface::read(int sensor_id) const {
    SensorReading reading;
    reading.sensor_id = sensor_id;
    reading.type = SensorType::TEMPERATURE;
    reading.value = 20.0;  // Default room temperature
    reading.unit = "°C";
    reading.timestamp = std::chrono::system_clock::now();
    reading.valid = true;
    return reading;
}

std::vector<SensorReading> GamepadSensorInterface::readAll() const {
    return {read(0)};
}

bool GamepadSensorInterface::calibrate(int sensor_id, double reference_value) {
    return true;
}

bool GamepadSensorInterface::autoCalibrate(int sensor_id) {
    return true;
}

void GamepadSensorInterface::setReadingCallback(ReadingCallback callback) {
    // No-op for gamepad mode
}

void GamepadSensorInterface::setErrorCallback(ErrorCallback callback) {
    // No-op for gamepad mode
}

std::string GamepadSensorInterface::getDiagnostics() const {
    return "GamepadSensorInterface (no real sensors)";
}

} // namespace hal
} // namespace astro_mount
