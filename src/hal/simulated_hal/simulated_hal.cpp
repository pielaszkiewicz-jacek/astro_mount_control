#include "hal/simulated_hal/simulated_hal.h"
#include "hal/safety_monitor.h"
#include "hal/sensor_interface.h"
#include <chrono>
#include <iostream>
#include <thread>

using namespace astro_mount::hal;

// ============================================================================
// SimulatedMotor implementation
// ============================================================================

SimulatedHAL::SimulatedMotor::SimulatedMotor(int axis_id)
    : axis_id_(axis_id),
      rng_(std::random_device{}()),
      position_noise_(0.0, 0.001),  // 0.001 degree stddev
      velocity_noise_(0.0, 0.0001), // 0.0001 deg/s stddev
      start_time_(std::chrono::steady_clock::now()) {
    
    // Start simulation thread
    running_ = true;
    simulation_thread_ = std::thread(&SimulatedMotor::simulationThread, this);
}

SimulatedHAL::SimulatedMotor::~SimulatedMotor() {
    running_ = false;
    if (simulation_thread_.joinable()) {
        simulation_thread_.join();
    }
}

bool SimulatedHAL::SimulatedMotor::enable() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (error_state_) {
        return false;
    }
    
    enabled_ = true;
    
    if (state_change_callback_) {
        state_change_callback_(true, false);
    }
    
    return true;
}

bool SimulatedHAL::SimulatedMotor::disable() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    enabled_ = false;
    moving_ = false;
    actual_velocity_ = 0.0;
    
    if (state_change_callback_) {
        state_change_callback_(false, false);
    }
    
    return true;
}

bool SimulatedHAL::SimulatedMotor::isEnabled() const {
    return enabled_;
}

bool SimulatedHAL::SimulatedMotor::setPosition(double position_deg, double velocity_deg_s, 
                                              double acceleration_deg_s2) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (!enabled_ || error_state_) {
        return false;
    }
    
    target_position_ = position_deg;
    moving_ = true;
    
    // Simulate motion parameters
    // In a real implementation, these would be used for trajectory generation
    (void)velocity_deg_s;      // Unused in simulation
    (void)acceleration_deg_s2; // Unused in simulation
    
    if (state_change_callback_) {
        state_change_callback_(true, true);
    }
    
    return true;
}

bool SimulatedHAL::SimulatedMotor::setVelocity(double velocity_deg_s, double acceleration_deg_s2) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (!enabled_ || error_state_) {
        return false;
    }
    
    // In velocity mode, we continuously move
    actual_velocity_ = velocity_deg_s;
    moving_ = true;
    
    (void)acceleration_deg_s2; // Unused in simulation
    
    if (state_change_callback_) {
        state_change_callback_(true, true);
    }
    
    return true;
}

bool SimulatedHAL::SimulatedMotor::setTorque(double torque_percent) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (!enabled_ || error_state_) {
        return false;
    }
    
    // Simulate torque control
    actual_torque_ = torque_percent;
    
    // In torque mode, velocity depends on load
    // For simulation, assume linear relationship
    actual_velocity_ = torque_percent * 0.1; // 0.1 deg/s per % torque
    
    moving_ = true;
    
    if (state_change_callback_) {
        state_change_callback_(true, true);
    }
    
    return true;
}

bool SimulatedHAL::SimulatedMotor::stop() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    moving_ = false;
    actual_velocity_ = 0.0;
    actual_torque_ = 0.0;
    
    if (state_change_callback_) {
        state_change_callback_(enabled_, false);
    }
    
    return true;
}

bool SimulatedHAL::SimulatedMotor::emergencyStop() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    moving_ = false;
    enabled_ = false;
    actual_velocity_ = 0.0;
    actual_torque_ = 0.0;
    
    if (state_change_callback_) {
        state_change_callback_(false, false);
    }
    
    return true;
}

double SimulatedHAL::SimulatedMotor::getActualPosition() const {
    return actual_position_ + position_noise_(rng_);
}

double SimulatedHAL::SimulatedMotor::getActualVelocity() const {
    return actual_velocity_ + velocity_noise_(rng_);
}

double SimulatedHAL::SimulatedMotor::getActualTorque() const {
    return actual_torque_;
}

bool SimulatedHAL::SimulatedMotor::isMoving() const {
    return moving_;
}

bool SimulatedHAL::SimulatedMotor::targetReached() const {
    const double tolerance = 0.001; // 0.001 degree tolerance
    
    if (!moving_) {
        return true;
    }
    
    return std::abs(target_position_ - actual_position_) < tolerance;
}

bool SimulatedHAL::SimulatedMotor::inErrorState() const {
    return error_state_;
}

std::string SimulatedHAL::SimulatedMotor::getErrorString() const {
    return error_message_;
}

bool SimulatedHAL::SimulatedMotor::configure(const MotorConfig& config) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    config_ = config;
    
    return true;
}

MotorConfig SimulatedHAL::SimulatedMotor::getConfiguration() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return config_;
}

void SimulatedHAL::SimulatedMotor::setPositionCallback(PositionCallback callback) {
    std::lock_guard<std::mutex> lock(mutex_);
    position_callback_ = callback;
}

void SimulatedHAL::SimulatedMotor::setErrorCallback(ErrorCallback callback) {
    std::lock_guard<std::mutex> lock(mutex_);
    error_callback_ = callback;
}

void SimulatedHAL::SimulatedMotor::setStateChangeCallback(StateChangeCallback callback) {
    std::lock_guard<std::mutex> lock(mutex_);
    state_change_callback_ = callback;
}

double SimulatedHAL::SimulatedMotor::getTemperature() const {
    // Simulate temperature based on operation time
    auto now = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::minutes>(now - start_time_);
    
    // Base temperature 25°C + 0.1°C per minute of operation
    return 25.0 + duration.count() * 0.1;
}

double SimulatedHAL::SimulatedMotor::getCurrent() const {
    // Simulate current based on torque
    return std::abs(actual_torque_) * 0.5; // 0.5A per % torque
}

double SimulatedHAL::SimulatedMotor::getVoltage() const {
    // Simulate constant voltage
    return 24.0; // 24V
}

uint32_t SimulatedHAL::SimulatedMotor::getOperationTime() const {
    auto now = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::seconds>(now - start_time_);
    return static_cast<uint32_t>(duration.count());
}

void SimulatedHAL::SimulatedMotor::simulationThread() {
    const double update_rate = 100.0; // Hz
    const auto update_interval = std::chrono::milliseconds(1000 / static_cast<int>(update_rate));
    
    while (running_) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            
            if (enabled_ && moving_) {
                // Update position based on velocity
                double dt = 1.0 / update_rate;
                actual_position_.store(actual_position_.load(std::memory_order_relaxed) + actual_velocity_ * dt, std::memory_order_relaxed);
                
                // If in position mode and close to target, stop
                if (std::abs(actual_velocity_) < 0.0001) { // Not in velocity mode
                    if (std::abs(target_position_ - actual_position_) < 0.001) {
                        moving_ = false;
                        actual_velocity_ = 0.0;
                        
                        if (state_change_callback_) {
                            state_change_callback_(true, false);
                        }
                    } else {
                        // Move toward target with simulated PID
                        double error = target_position_ - actual_position_;
                        double max_velocity = config_.max_velocity;
                        double velocity = std::clamp(error * 0.1, -max_velocity, max_velocity);
                        actual_velocity_ = velocity;
                    }
                }
                
                // Call position callback
                if (position_callback_) {
                    position_callback_(actual_position_, actual_velocity_, actual_torque_);
                }
            }
        }
        
        std::this_thread::sleep_for(update_interval);
    }
}

// ============================================================================
// SimulatedEncoder implementation
// ============================================================================

SimulatedHAL::SimulatedEncoder::SimulatedEncoder(int axis_id)
    : axis_id_(axis_id),
      rng_(std::random_device{}()),
      position_noise_(0.0, 0.0001), // 0.0001 degree stddev
      start_time_(std::chrono::steady_clock::now()) {}

SimulatedHAL::SimulatedEncoder::~SimulatedEncoder() {
    // Signal thread to stop BEFORE checking joinable to avoid races
    reading_running_ = false;
    if (reading_thread_.joinable()) {
        reading_thread_.join();
    }
}

bool SimulatedHAL::SimulatedEncoder::initialize(const EncoderConfig& config) {
    // If a reading thread is already running, stop it first
    if (reading_thread_.joinable()) {
        reading_running_ = false;
        reading_thread_.join();
    }
    
    std::lock_guard<std::mutex> lock(mutex_);
    
    config_ = config;
    initialized_ = true;
    
    // Start reading thread
    reading_running_ = true;
    reading_thread_ = std::thread(&SimulatedEncoder::readingThread, this);
    
    return true;
}

void SimulatedHAL::SimulatedEncoder::shutdown() {
    // Signal thread to stop WITHOUT holding the mutex to avoid deadlock
    // (readingThread() also acquires this mutex)
    reading_running_ = false;
    
    if (reading_thread_.joinable()) {
        reading_thread_.join();
    }
    
    // Now acquire mutex for cleanup
    {
        std::lock_guard<std::mutex> lock(mutex_);
        initialized_ = false;
    }
}

bool SimulatedHAL::SimulatedEncoder::isInitialized() const {
    return initialized_;
}

EncoderReading SimulatedHAL::SimulatedEncoder::read() const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    EncoderReading reading;
    
    // Get position from simulated motor (would need access to motor instance)
    // For now, simulate based on time
    auto now = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration<double>(now - start_time_);
    
    // Simulate encoder readings
    double base_position = duration.count() * 0.01; // 0.01 deg/s drift
    
    reading.position_deg = base_position + position_noise_(rng_);
    reading.velocity_deg_s = 0.01; // Constant drift
    reading.raw_counts = static_cast<int32_t>(reading.position_deg * config_.counts_per_degree);
    reading.index_pulse = (total_readings_ % static_cast<uint32_t>(config_.counts_per_degree)) == 0;
    reading.direction = reading.velocity_deg_s >= 0;
    reading.timestamp = std::chrono::steady_clock::now();
    reading.error_count = error_count_;
    
    const_cast<std::atomic<uint32_t>&>(total_readings_)++;
    
    return reading;
}

bool SimulatedHAL::SimulatedEncoder::isDataValid() const {
    return initialized_ && (error_count_ < 10);
}

double SimulatedHAL::SimulatedEncoder::getUpdateRate() const {
    return config_.update_rate_hz;
}

bool SimulatedHAL::SimulatedEncoder::calibrate(double reference_position_deg) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    calibration_offset_ = reference_position_deg - (actual_position_ + position_noise_(rng_));
    
    return true;
}

bool SimulatedHAL::SimulatedEncoder::autoCalibrate() {
    // Simulate auto-calibration
    calibration_offset_ = -position_noise_(rng_);
    return true;
}

double SimulatedHAL::SimulatedEncoder::getCalibrationOffset() const {
    return calibration_offset_;
}

void SimulatedHAL::SimulatedEncoder::setCalibrationOffset(double offset_deg) {
    calibration_offset_ = offset_deg;
}

bool SimulatedHAL::SimulatedEncoder::saveCalibration() {
    // In simulation, just return success
    return true;
}

bool SimulatedHAL::SimulatedEncoder::loadCalibration() {
    // In simulation, just return success
    return true;
}

EncoderType SimulatedHAL::SimulatedEncoder::getType() const {
    return config_.type;
}

EncoderInterface SimulatedHAL::SimulatedEncoder::getInterface() const {
    return config_.interface;
}

uint32_t SimulatedHAL::SimulatedEncoder::getResolution() const {
    return config_.resolution;
}

double SimulatedHAL::SimulatedEncoder::getCountsPerDegree() const {
    return config_.counts_per_degree;
}

void SimulatedHAL::SimulatedEncoder::readingThread() {
    const double update_rate = config_.update_rate_hz;
    const auto update_interval = std::chrono::milliseconds(1000 / static_cast<int>(update_rate));
    
    while (reading_running_) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            
            // Update simulated position
            actual_position_.store(actual_position_.load(std::memory_order_relaxed) + 0.01 / update_rate, std::memory_order_relaxed);
            
            // Call reading callback
            if (reading_callback_) {
                EncoderReading reading;
                reading.position_deg = actual_position_ + position_noise_(rng_);
                reading.velocity_deg_s = 0.01;
                reading.raw_counts = static_cast<int32_t>(reading.position_deg * config_.counts_per_degree);
                reading.timestamp = std::chrono::steady_clock::now();
                
                reading_callback_(reading);
            }
        }
        
        std::this_thread::sleep_for(update_interval);
    }
}

// ============================================================================
// SimulatedHAL implementation
// ============================================================================

SimulatedHAL::SimulatedHAL() {
    // Initialize motors and encoders
    for (int i = 0; i < 3; ++i) {
        motors_[i] = std::make_unique<SimulatedMotor>(i);
        encoders_[i] = std::make_unique<SimulatedEncoder>(i);
    }
}

SimulatedHAL::SimulatedHAL(const HALConfig& config) : config_(config) {
    // Initialize motors and encoders (but don't start encoder threads yet -
    // that happens during explicit initialize() call)
    for (int i = 0; i < 3; ++i) {
        motors_[i] = std::make_unique<SimulatedMotor>(i);
        encoders_[i] = std::make_unique<SimulatedEncoder>(i);
        
        // Configure with HAL config if available, otherwise use defaults
        if (i < static_cast<int>(config_.axes.size())) {
            MotorConfig motor_config;
            motor_config.max_velocity = config_.axes[i].motor_config.max_velocity;
            motor_config.max_acceleration = config_.axes[i].motor_config.max_acceleration;
            
            motors_[i]->configure(motor_config);
        }
    }
}

SimulatedHAL::~SimulatedHAL() {
    shutdown();
}

bool SimulatedHAL::initialize(const HALConfig& config) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    config_ = config;
    
    // Configure motors and encoders
    for (int i = 0; i < 3; ++i) {
        if (i < config_.axes.size()) {
            MotorConfig motor_config;
            motor_config.max_velocity = config_.axes[i].motor_config.max_velocity;
            motor_config.max_acceleration = config_.axes[i].motor_config.max_acceleration;
            
            motors_[i]->configure(motor_config);
            
            EncoderConfig encoder_config;
            encoder_config.type = config_.axes[i].encoder_config.type;
            encoder_config.counts_per_degree = config_.axes[i].encoder_config.counts_per_degree;
            encoder_config.update_rate_hz = 100.0;
            
            encoders_[i]->initialize(encoder_config);
        }
    }
    
    initialized_ = true;
    return true;
}

void SimulatedHAL::shutdown() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    for (auto& motor : motors_) {
        if (motor) {
            motor->disable();
        }
    }
    
    for (auto& encoder : encoders_) {
        if (encoder) {
            encoder->shutdown();
        }
    }
    
    initialized_ = false;
    running_ = false;
}

bool SimulatedHAL::isInitialized() const {
    return initialized_;
}

std::unique_ptr<MotorControl> SimulatedHAL::createMotorControl(int axis_id) {
    if (axis_id < 0 || axis_id >= motors_.size()) {
        return nullptr;
    }
    
    // Return a copy of the motor control interface
    // In reality, we would return the existing instance
    // For now, just return nullptr as the motors are already created
    return nullptr;
}

std::unique_ptr<EncoderReader> SimulatedHAL::createEncoderReader(int axis_id) {
    if (axis_id < 0 || axis_id >= encoders_.size()) {
        return nullptr;
    }
    
    // Return a copy of the encoder interface
    return nullptr;
}

std::unique_ptr<SafetyMonitor> SimulatedHAL::createSafetyMonitor() {
    // Safety monitor not implemented for simulated HAL
    return nullptr;
}

std::unique_ptr<SensorInterface> SimulatedHAL::createSensorInterface() {
    // Sensor interface not implemented for simulated HAL
    return nullptr;
}

std::string SimulatedHAL::getPlatformName() const {
    return "SimulatedHAL_v1.0";
}

std::string SimulatedHAL::getHardwareVersion() const {
    return "1.0.0-simulated";
}

std::vector<HALFeature> SimulatedHAL::getSupportedFeatures() const {
    return {
        HALFeature::PID_CONTROL,
        HALFeature::ENCODER_FEEDBACK,
        HALFeature::REAL_TIME_CONTROL,
        HALFeature::DEROTATOR_SUPPORT
    };
}


bool SimulatedHAL::supportsFeature(HALFeature feature) const {
    auto features = getSupportedFeatures();
    return std::find(features.begin(), features.end(), feature) != features.end();
}

bool SimulatedHAL::start() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (!initialized_) {
        return false;
    }
    
    running_ = true;
    
    // Enable all motors
    for (auto& motor : motors_) {
        if (motor) {
            motor->enable();
        }
    }
    
    return true;
}

bool SimulatedHAL::stop() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    // Disable all motors
    for (auto& motor : motors_) {
        if (motor) {
            motor->disable();
        }
    }
    
    running_ = false;
    return true;
}

bool SimulatedHAL::isRunning() const {
    return running_;
}

std::string SimulatedHAL::getStatus() const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    std::string status;
    status += "SimulatedHAL Status:\n";
    status += "  Initialized: " + std::string(initialized_ ? "Yes" : "No") + "\n";
    status += "  Running: " + std::string(running_ ? "Yes" : "No") + "\n";
    status += "  Motors: " + std::to_string(motors_.size()) + "\n";
    status += "  Encoders: " + std::to_string(encoders_.size()) + "\n";
    
    return status;
}

std::string SimulatedHAL::getErrorMessages() const {
    // No errors in simulation
    return "";
}

void SimulatedHAL::clearErrors() {
    // Nothing to clear in simulation
}

void SimulatedHAL::updateSimulation() {
    // This would update the overall simulation state
    // Currently handled by individual component threads
}

// ============================================================================
// SimulatedEncoder missing method implementations
// ============================================================================

void SimulatedHAL::SimulatedEncoder::setReadingCallback(ReadingCallback callback) {
    std::lock_guard<std::mutex> lock(mutex_);
    reading_callback_ = callback;
}

void SimulatedHAL::SimulatedEncoder::setErrorCallback(ErrorCallback callback) {
    std::lock_guard<std::mutex> lock(mutex_);
    error_callback_ = callback;
}

uint32_t SimulatedHAL::SimulatedEncoder::getTotalReadings() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return total_readings_;
}

uint32_t SimulatedHAL::SimulatedEncoder::getErrorCount() const {
    return error_count_;
}

double SimulatedHAL::SimulatedEncoder::getUptime() const {
    auto now = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration<double>(now - start_time_);
    return duration.count();
}

std::string SimulatedHAL::SimulatedEncoder::getDiagnostics() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::string diag;
    diag += "SimulatedEncoder[" + std::to_string(axis_id_) + "]:\n";
    diag += "  Initialized: " + std::string(initialized_ ? "Yes" : "No") + "\n";
    diag += "  Total readings: " + std::to_string(total_readings_) + "\n";
    diag += "  Error count: " + std::to_string(error_count_) + "\n";
    diag += "  Calibration offset: " + std::to_string(calibration_offset_) + " deg\n";
    return diag;
}

bool SimulatedHAL::SimulatedEncoder::synchronize() {
    // Simulation: always synchronized
    return true;
}

bool SimulatedHAL::SimulatedEncoder::isSynchronized() const {
    // Simulation: always synchronized
    return initialized_;
}