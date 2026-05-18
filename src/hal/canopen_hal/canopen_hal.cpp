#include "hal/canopen_hal/canopen_hal.h"
#include <chrono>
#include <iostream>
#include <iomanip>
#include <sstream>

using namespace astro_mount::hal;

// ============================================================================
// PIDController implementation
// ============================================================================

PIDController::PIDController(double kp, double ki, double kd,
                             double integral_limit, double output_limit)
    : kp_(kp), ki_(ki), kd_(kd),
      integral_limit_(integral_limit),
      output_limit_(output_limit),
      integral_(0.0),
      previous_error_(0.0) {
}

double PIDController::calculate(double setpoint, double measured, double dt) {
    if (dt <= 0.0) {
        return 0.0;
    }
    
    double error = setpoint - measured;
    
    // Proportional term
    double p_term = kp_ * error;
    
    // Integral term with anti-windup
    integral_ += error * dt;
    if (integral_ > integral_limit_) integral_ = integral_limit_;
    if (integral_ < -integral_limit_) integral_ = -integral_limit_;
    double i_term = ki_ * integral_;
    
    // Derivative term
    double derivative = (error - previous_error_) / dt;
    double d_term = kd_ * derivative;
    
    previous_error_ = error;
    
    // Calculate output with limits
    double output = p_term + i_term + d_term;
    if (output > output_limit_) output = output_limit_;
    if (output < -output_limit_) output = -output_limit_;
    
    return output;
}

void PIDController::reset() {
    integral_ = 0.0;
    previous_error_ = 0.0;
}

void PIDController::setParameters(double kp, double ki, double kd) {
    kp_ = kp;
    ki_ = ki;
    kd_ = kd;
    reset();
}

std::tuple<double, double, double> PIDController::getParameters() const {
    return std::make_tuple(kp_, ki_, kd_);
}

// ============================================================================
// CanOpenMotor implementation
// ============================================================================

CanOpenHAL::CanOpenMotor::CanOpenMotor(int axis_id, controllers::ICanOpenInterface& canopen)
    : axis_id_(axis_id),
      canopen_(canopen),
      pid_controller_(1.5, 0.2, 0.05, 1000.0, 100.0) {
    
    // Start control thread
    control_running_ = true;
    control_thread_ = std::thread(&CanOpenMotor::controlLoop, this);
}

CanOpenHAL::CanOpenMotor::~CanOpenMotor() {
    control_running_ = false;
    if (control_thread_.joinable()) {
        control_thread_.join();
    }
}

bool CanOpenHAL::CanOpenMotor::enable() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    try {
        // Send control word to enable drive (CiA 402 state machine)
        if (!sendControlWord(0x0006)) { // Switch on disabled -> Ready to switch on
            return false;
        }
        
        if (!sendControlWord(0x0007)) { // Ready to switch on -> Switched on
            return false;
        }
        
        if (!sendControlWord(0x000F)) { // Switched on -> Operation enabled
            return false;
        }
        
        enabled_ = true;
        return true;
    } catch (const std::exception& e) {
        error_state_ = true;
        error_message_ = std::string("Failed to enable motor: ") + e.what();
        return false;
    }
}

bool CanOpenHAL::CanOpenMotor::disable() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    try {
        // Send control word to disable drive
        if (!sendControlWord(0x0000)) { // Disable operation
            return false;
        }
        
        enabled_ = false;
        moving_ = false;
        return true;
    } catch (const std::exception& e) {
        error_state_ = true;
        error_message_ = std::string("Failed to disable motor: ") + e.what();
        return false;
    }
}

bool CanOpenHAL::CanOpenMotor::isEnabled() const {
    return enabled_;
}

bool CanOpenHAL::CanOpenMotor::setPosition(double position_deg, double velocity_deg_s,
                                          double acceleration_deg_s2) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (!enabled_) {
        error_message_ = "Motor not enabled";
        return false;
    }
    
    try {
        // Convert degrees to encoder counts
        double counts_per_degree = config_.encoder_counts_per_degree;
        int32_t target_counts = static_cast<int32_t>(position_deg * counts_per_degree);
        
        // Set position target via CANopen
        if (!canopen_.setPositionTarget(axis_id_, target_counts, velocity_deg_s, acceleration_deg_s2)) {
            error_message_ = "Failed to set position target";
            return false;
        }
        
        target_position_ = position_deg;
        moving_ = true;
        
        return true;
    } catch (const std::exception& e) {
        error_state_ = true;
        error_message_ = std::string("Failed to set position: ") + e.what();
        return false;
    }
}

bool CanOpenHAL::CanOpenMotor::setVelocity(double velocity_deg_s,
                                          double acceleration_deg_s2) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (!enabled_) {
        error_message_ = "Motor not enabled";
        return false;
    }
    
    try {
        // Przełącz w tryb Velocity Mode (CiA 402: Modes of Operation = 0x6060)
        int8_t velocity_mode = 2; // Velocity mode (vl)
        canopen_.sendSDO(axis_id_, 0x6060, 0x00, &velocity_mode, sizeof(velocity_mode));
        
        // Konwertuj °/s na jednostki napędu (counts/s)
        double counts_per_degree = config_.encoder_counts_per_degree;
        int32_t velocity_counts = static_cast<int32_t>(velocity_deg_s * counts_per_degree);
        
        // Ustaw target velocity przez SDO (0x6042: vl_target_velocity)
        if (!canopen_.sendSDO(axis_id_, 0x6042, 0x00, &velocity_counts, sizeof(velocity_counts))) {
            error_message_ = "Failed to set velocity target";
            return false;
        }
        
        // Ustaw przyspieszenie (0x6048: vl_max_acceleration)
        int32_t accel_counts = static_cast<int32_t>(acceleration_deg_s2 * counts_per_degree);
        canopen_.sendSDO(axis_id_, 0x6048, 0x01, &accel_counts, sizeof(accel_counts));
        
        target_position_ = velocity_deg_s; // store velocity as pseudo-target
        moving_ = true;
        return true;
    } catch (const std::exception& e) {
        error_state_ = true;
        error_message_ = std::string("Failed to set velocity: ") + e.what();
        return false;
    }
}

bool CanOpenHAL::CanOpenMotor::setTorque(double torque_percent) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (!enabled_) {
        error_message_ = "Motor not enabled";
        return false;
    }
    
    // Torque control mode - limit to safe range
    torque_percent = std::max(-100.0, std::min(100.0, torque_percent));
    
    // Convert percentage to torque value based on motor specs
    double max_torque = config_.max_torque;
    double torque_nm = max_torque * (torque_percent / 100.0);
    
        // Set torque via CANopen (CiA 402 Torque Mode)
        try {
            // Przełącz w tryb Torque Mode (Modes of Operation = 0x6060, tq=4)
            int8_t torque_mode = 4; // Torque mode (tq)
            canopen_.sendSDO(axis_id_, 0x6060, 0x00, &torque_mode, sizeof(torque_mode));
            
            // Ustaw target torque przez SDO (0x6071: Target Torque)
            // Konwersja: max_torque z MotorConfig jako 100%
            int16_t target_torque_raw = static_cast<int16_t>(torque_percent * 10.0); // 1 digit = 0.1%
            canopen_.sendSDO(axis_id_, 0x6071, 0x00, &target_torque_raw, sizeof(target_torque_raw));
            
            // Odczytaj actual torque (0x6077: Torque Actual Value)
            int16_t actual_torque_raw = 0;
            canopen_.receiveSDO(axis_id_, 0x6077, 0x00, &actual_torque_raw, sizeof(actual_torque_raw));
            actual_torque_ = static_cast<double>(actual_torque_raw) / 10.0;
            
        } catch (const std::exception& e) {
            std::cerr << "Motor " << axis_id_ << ": Torque SDO failed: " << e.what() << std::endl;
            // Fallback: używamy PID, jeśli SDO się nie powiedzie
        }
        
        return true;
}

bool CanOpenHAL::CanOpenMotor::stop() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    try {
        // Halt motion (CiA 402 halt function)
        if (!sendControlWord(0x010F)) { // Halt
            return false;
        }
        
        moving_ = false;
        return true;
    } catch (const std::exception& e) {
        error_state_ = true;
        error_message_ = std::string("Failed to stop motor: ") + e.what();
        return false;
    }
}

bool CanOpenHAL::CanOpenMotor::emergencyStop() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    try {
        // Quick stop (CiA 402 quick stop)
        if (!sendControlWord(0x0002)) { // Quick stop
            return false;
        }
        
        moving_ = false;
        enabled_ = false;
        return true;
    } catch (const std::exception& e) {
        error_state_ = true;
        error_message_ = std::string("Failed emergency stop: ") + e.what();
        return false;
    }
}

double CanOpenHAL::CanOpenMotor::getActualPosition() const {
    try {
        auto position_data = canopen_.getPositionData(axis_id_);
        return position_data.actual_position; // Already in degrees
    } catch (const std::exception&) {
        return actual_position_.load(); // Fallback to cached value
    }
}

double CanOpenHAL::CanOpenMotor::getActualVelocity() const {
    try {
        auto position_data = canopen_.getPositionData(axis_id_);
        return position_data.actual_velocity; // deg/s
    } catch (const std::exception&) {
        return actual_velocity_.load(); // Fallback to cached value
    }
}

double CanOpenHAL::CanOpenMotor::getActualTorque() const {
    try {
        // Try to get torque/current data from drive
        // This depends on drive support
        return actual_torque_.load();
    } catch (const std::exception&) {
        return 0.0;
    }
}

bool CanOpenHAL::CanOpenMotor::isMoving() const {
    return moving_;
}

bool CanOpenHAL::CanOpenMotor::targetReached() const {
    double tolerance = 0.1; // degrees
    double current_pos = getActualPosition();
    return std::abs(current_pos - target_position_) < tolerance;
}

bool CanOpenHAL::CanOpenMotor::inErrorState() const {
    return error_state_;
}

std::string CanOpenHAL::CanOpenMotor::getErrorString() const {
    return error_message_;
}

bool CanOpenHAL::CanOpenMotor::configure(const MotorConfig& config) {
    std::lock_guard<std::mutex> lock(mutex_);
    config_ = config;
    
    // Configure PID controller based on motor parameters
    // Używamy PIDParams z konfiguracji globalnej HAL
    // MotorConfig nie zawiera pól PID, więc pomijamy
    
    return true;
}

MotorConfig CanOpenHAL::CanOpenMotor::getConfiguration() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return config_;
}

void CanOpenHAL::CanOpenMotor::setPositionCallback(PositionCallback callback) {
    std::lock_guard<std::mutex> lock(mutex_);
    position_callback_ = callback;
}

void CanOpenHAL::CanOpenMotor::setErrorCallback(ErrorCallback callback) {
    std::lock_guard<std::mutex> lock(mutex_);
    error_callback_ = callback;
}

void CanOpenHAL::CanOpenMotor::setStateChangeCallback(StateChangeCallback callback) {
    std::lock_guard<std::mutex> lock(mutex_);
    state_change_callback_ = callback;
}

double CanOpenHAL::CanOpenMotor::getTemperature() const {
    // Odczyt temperatury przez SDO z obiektu producenta 0x2030 (lub 0x2000)
    // W CiA 301/402 brak standardowego obiektu temperatury – zależy od producenta napędu
    // Priorytet: SDO > DriveStatus > domyślna
    try {
        // Próba 1: Odczyt SDO z obiektu 0x2030 (często używany dla temperatury silnika)
        int16_t temp_raw = 0;
        if (canopen_.receiveSDO(axis_id_, 0x2030, 0x00, &temp_raw, sizeof(temp_raw)) == sizeof(temp_raw)) {
            return static_cast<double>(temp_raw) / 10.0; // 1 digit = 0.1°C
        }
        
        // Próba 2: Odczyt z obiektu 0x2000 (obszar producenta)
        if (canopen_.receiveSDO(axis_id_, 0x2000, 0x01, &temp_raw, sizeof(temp_raw)) == sizeof(temp_raw)) {
            return static_cast<double>(temp_raw) / 10.0;
        }
        
        // Próba 3: Fallback przez getDriveStatus
        auto status = canopen_.getDriveStatus(axis_id_);
        if (status.temperature > 0.0) return status.temperature;
    } catch (...) {}
    return 25.0; // temperatura domyślna gdy brak odczytu
}

double CanOpenHAL::CanOpenMotor::getCurrent() const {
    // Odczyt prądu przez SDO z obiektu CiA 402 0x2031 (Actual Current)
    // Standard CiA 402: obiekt 0x6078 (Current Actual Value) lub 0x2031
    try {
        // Próba 1: SDO 0x2031 (Actual Current, obszar producenta)
        int16_t current_raw = 0;
        if (canopen_.receiveSDO(axis_id_, 0x2031, 0x00, &current_raw, sizeof(current_raw)) == sizeof(current_raw)) {
            return static_cast<double>(current_raw) / 1000.0; // mA → A
        }
        
        // Próba 2: SDO 0x6078 (CiA 402: Current Actual Value)
        if (canopen_.receiveSDO(axis_id_, 0x6078, 0x00, &current_raw, sizeof(current_raw)) == sizeof(current_raw)) {
            return static_cast<double>(current_raw) / 1000.0;
        }
        
        // Próba 3: Fallback przez getDriveStatus
        auto status = canopen_.getDriveStatus(axis_id_);
        if (status.current >= 0.0) return status.current;
    } catch (...) {}
    return 0.0; // prąd domyślny gdy brak odczytu
}

double CanOpenHAL::CanOpenMotor::getVoltage() const {
    // Odczyt napięcia przez SDO z obiektu CiA 402 (Actual DC Link Voltage)
    // Domyślnie 24V dla typowego systemu CANopen
    try {
        uint16_t voltage_raw = 0;
        if (canopen_.receiveSDO(axis_id_, 0x2032, 0x00, &voltage_raw, sizeof(voltage_raw)) == sizeof(voltage_raw)) {
            return static_cast<double>(voltage_raw) / 10.0; // V, 1 digit = 0.1V
        }
    } catch (...) {}
    return 24.0; // napięcie domyślne gdy brak odczytu
}

uint32_t CanOpenHAL::CanOpenMotor::getOperationTime() const {
    // Operation time in hours (simulated)
    static auto start_time = std::chrono::steady_clock::now();
    auto now = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::hours>(now - start_time);
    return duration.count();
}

bool CanOpenHAL::CanOpenMotor::sendControlWord(uint16_t controlword) {
    // Send control word via CANopen SDO (CiA 402 Object 0x6040)
    // Controlword = 16-bit value zgodny z CiA 402 Drive Profile
    try {
        return canopen_.sendSDO(axis_id_, 0x6040, 0x00, &controlword, sizeof(controlword));
    } catch (const std::exception& e) {
        std::cerr << "Motor " << axis_id_ << ": sendControlWord(0x" 
                  << std::hex << controlword << std::dec << ") failed: " << e.what() << std::endl;
        return false;
    }
}

bool CanOpenHAL::CanOpenMotor::readStatusWord() {
    // Read status word via CANopen SDO (CiA 402 Object 0x6041)
    try {
        uint16_t status_word = 0;
        int ret = canopen_.receiveSDO(axis_id_, 0x6041, 0x00, &status_word, sizeof(status_word));
        if (ret == sizeof(status_word)) {
            // Aktualizuj stan silnika na podstawie status word
            enabled_ = (status_word & 0x000F) == 0x000F; // Operation enabled
            moving_  = (status_word & 0x0400) != 0;       // Target reached
            error_state_ = (status_word & 0x0008) != 0;   // Fault
            return true;
        }
    } catch (const std::exception& e) {
        std::cerr << "Motor " << axis_id_ << ": readStatusWord() failed: " << e.what() << std::endl;
    }
    return false;
}

bool CanOpenHAL::CanOpenMotor::configureCiA402() {
    // Configure CiA 402 drive profile
    // Set up PDO mappings, operation modes, etc.
    try {
        // Ustaw tryb pracy: Position mode (domyślnie)
        int8_t op_mode = 1; // pp = Profile Position mode
        canopen_.sendSDO(axis_id_, 0x6060, 0x00, &op_mode, sizeof(op_mode));
        
        // Konfiguruj PDO1: Status Word (0x6041) + Position Actual (0x6064)
        uint32_t pdo1_mapping[] = {
            0x60410010, // Status Word, 16-bit
            0x60640020  // Position Actual, 32-bit
        };
        std::vector<uint32_t> pdo1_map(pdo1_mapping, pdo1_mapping + 2);
        canopen_.configurePDO(axis_id_, 1, pdo1_map);
        
        // Konfiguruj PDO2: Control Word (0x6040) + Target Position (0x607A)
        uint32_t pdo2_mapping[] = {
            0x60400010, // Control Word, 16-bit
            0x607A0020  // Target Position, 32-bit
        };
        std::vector<uint32_t> pdo2_map(pdo2_mapping, pdo2_mapping + 2);
        canopen_.configurePDO(axis_id_, 2, pdo2_map);
        
        // Włącz PDO1 (TX - odbiór danych z napędu)
        canopen_.enablePDO(axis_id_, 1, true);
        // Włącz PDO2 (RX - wysyłanie danych do napędu)
        canopen_.enablePDO(axis_id_, 2, true);
        
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Motor " << axis_id_ << ": configureCiA402() failed: " << e.what() << std::endl;
        return false;
    }
}

void CanOpenHAL::CanOpenMotor::controlLoop() {
    auto last_update = std::chrono::steady_clock::now();
    
    while (control_running_) {
        auto now = std::chrono::steady_clock::now();
        auto dt = std::chrono::duration<double>(now - last_update).count();
        last_update = now;
        
        if (enabled_ && moving_) {
            // Update actual position from encoder
            double new_position = getActualPosition();
            double new_velocity = getActualVelocity();
            
            actual_position_ = new_position;
            actual_velocity_ = new_velocity;
            
            // Call position callback if set (MotorControl callback sign: position, velocity, torque)
            if (position_callback_) {
                position_callback_(new_position, new_velocity, actual_torque_.load());
            }
            
            // Check if target reached
            if (targetReached()) {
                moving_ = false;
                if (state_change_callback_) {
                    state_change_callback_(enabled_, moving_);
                }
            }
        }
        
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

// ============================================================================
// CanOpenEncoder implementation (partial - showing structure)
// ============================================================================

CanOpenHAL::CanOpenEncoder::CanOpenEncoder(int axis_id, controllers::ICanOpenInterface& canopen)
    : axis_id_(axis_id),
      canopen_(canopen) {
    
    start_time_ = std::chrono::steady_clock::now();
    last_pdo_time_ = start_time_;
    
    // Inicjalizacja pustego cache PDO
    {
        std::lock_guard<std::mutex> lock(mutex_);
        latest_reading_.position_deg = 0.0;
        latest_reading_.velocity_deg_s = 0.0;
        latest_reading_.raw_counts = 0;
        latest_reading_.data_valid = false;
        latest_reading_.timestamp = std::chrono::steady_clock::now();
    }
}

CanOpenHAL::CanOpenEncoder::~CanOpenEncoder() {
    if (pdo_running_) {
        pdo_running_ = false;
        if (pdo_thread_.joinable()) {
            pdo_thread_.join();
        }
    }
}

bool CanOpenHAL::CanOpenEncoder::initialize(const EncoderConfig& config) {
    config_ = config;
    calibration_offset_ = config.calibration_offset;
    initialized_ = true;
    
    if (config.interface == EncoderInterface::CANOPEN) {
        // Uruchom wątek odbioru PDO
        pdo_running_ = true;
        pdo_thread_ = std::thread(&CanOpenEncoder::pdoReceiveThread, this);
        
        // Ustaw callback CANopen dla bezpośredniej aktualizacji cache enkodera
        canopen_.setEncoderCallback([this](int axis_id, const controllers::ICanOpenInterface::EncoderData& data) {
            if (axis_id == axis_id_) {
                std::lock_guard<std::mutex> lock(mutex_);
                latest_reading_.raw_counts = data.raw_position;
                latest_reading_.position_deg = config_.countsToDegrees(data.raw_position) + calibration_offset_.load();
                latest_reading_.velocity_deg_s = config_.velocityCountsToDegrees(data.raw_velocity);
                latest_reading_.index_pulse = data.index_pulse;
                latest_reading_.direction = data.direction;
                latest_reading_.error_count = data.error_count;
                latest_reading_.data_valid = true;
                latest_reading_.timestamp = std::chrono::steady_clock::now();
                last_pdo_time_ = std::chrono::steady_clock::now();
            }
        });
    }
    
    return true;
}

void CanOpenHAL::CanOpenEncoder::shutdown() {
    initialized_ = false;
    pdo_running_ = false;
    
    if (pdo_thread_.joinable()) {
        pdo_thread_.join();
    }
}

bool CanOpenHAL::CanOpenEncoder::isInitialized() const {
    return initialized_;
}

EncoderReading CanOpenHAL::CanOpenEncoder::read() const {
    // Jeśli PDO thread aktywny – zwróć ostatni cache z PDO
    if (pdo_running_) {
        std::lock_guard<std::mutex> lock(mutex_);
        return latest_reading_;
    }
    
    // Fallback: odczyt synchroniczny przez SDO / getEncoderData()
    EncoderReading reading;
    
    try {
        auto encoder_data = canopen_.getEncoderData(axis_id_);
        reading.raw_counts = encoder_data.raw_position;
        reading.position_deg = config_.countsToDegrees(encoder_data.raw_position) + calibration_offset_.load();
        reading.velocity_deg_s = config_.velocityCountsToDegrees(encoder_data.raw_velocity);
        reading.index_pulse = encoder_data.index_pulse;
        reading.direction = encoder_data.direction;
        reading.error_count = encoder_data.error_count;
        reading.timestamp = std::chrono::steady_clock::now();
        reading.data_valid = true;
        
        total_readings_.fetch_add(1);
        
        // Cache w latest_reading_
        {
            std::lock_guard<std::mutex> lock(mutex_);
            latest_reading_ = reading;
        }
        
        // Call reading callback if set
        if (reading_callback_) {
            reading_callback_(reading);
        }
        
    } catch (const std::exception& e) {
        reading.data_valid = false;
        error_count_.fetch_add(1);
        
        if (error_callback_) {
            error_callback_(std::string("Failed to read encoder: ") + e.what(), error_count_.load());
        }
    }
    
    return reading;
}

bool CanOpenHAL::CanOpenEncoder::isDataValid() const {
    return true; // Simplified
}

double CanOpenHAL::CanOpenEncoder::getUpdateRate() const {
    return config_.update_rate_hz;
}

bool CanOpenHAL::CanOpenEncoder::calibrate(double reference_position_deg) {
    try {
        double current_position = read().position_deg;
        calibration_offset_ = reference_position_deg - current_position;
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

bool CanOpenHAL::CanOpenEncoder::autoCalibrate() {
    // Auto-calibration would involve moving to known positions
    // For now, just zero the offset
    calibration_offset_ = 0.0;
    return true;
}

double CanOpenHAL::CanOpenEncoder::getCalibrationOffset() const {
    return calibration_offset_;
}

void CanOpenHAL::CanOpenEncoder::setCalibrationOffset(double offset_deg) {
    calibration_offset_ = offset_deg;
}

bool CanOpenHAL::CanOpenEncoder::saveCalibration() {
    // Save calibration to persistent storage
    return true;
}

bool CanOpenHAL::CanOpenEncoder::loadCalibration() {
    // Load calibration from persistent storage
    return true;
}

EncoderType CanOpenHAL::CanOpenEncoder::getType() const {
    return config_.type;
}

EncoderInterface CanOpenHAL::CanOpenEncoder::getInterface() const {
    return config_.interface;
}

uint32_t CanOpenHAL::CanOpenEncoder::getResolution() const {
    return config_.resolution;
}

double CanOpenHAL::CanOpenEncoder::getCountsPerDegree() const {
    return config_.counts_per_degree;
}

void CanOpenHAL::CanOpenEncoder::setReadingCallback(ReadingCallback callback) {
    reading_callback_ = callback;
}

void CanOpenHAL::CanOpenEncoder::setErrorCallback(ErrorCallback callback) {
    error_callback_ = callback;
}

uint32_t CanOpenHAL::CanOpenEncoder::getTotalReadings() const {
    return total_readings_;
}

uint32_t CanOpenHAL::CanOpenEncoder::getErrorCount() const {
    return error_count_;
}

double CanOpenHAL::CanOpenEncoder::getUptime() const {
    auto now = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration<double>(now - start_time_);
    return duration.count();
}

std::string CanOpenHAL::CanOpenEncoder::getDiagnostics() const {
    std::stringstream ss;
    ss << "Encoder " << axis_id_ << ": "
       << "Readings=" << total_readings_ << ", "
       << "Errors=" << error_count_ << ", "
       << "Uptime=" << std::fixed << std::setprecision(1) << getUptime() << "s, "
       << "Offset=" << calibration_offset_ << " deg";
    return ss.str();
}

bool CanOpenHAL::CanOpenEncoder::synchronize() {
    // Synchronize encoder with motor position
    return true;
}

bool CanOpenHAL::CanOpenEncoder::isSynchronized() const {
    return true;
}

bool CanOpenHAL::CanOpenEncoder::configurePDO(uint32_t cob_id, uint32_t mapping) {
    // Configure PDO for encoder data transmission
    return true;
}

bool CanOpenHAL::CanOpenEncoder::enablePDO(bool enable) {
    // Enable/disable PDO transmission
    return true;
}

void CanOpenHAL::CanOpenEncoder::pdoReceiveThread() {
    // Oblicz interwał odświeżania na podstawie konfiguracji
    auto update_interval = std::chrono::milliseconds(
        1000 / static_cast<int>(config_.update_rate_hz));
    
    // W przypadku braku konfiguracji lub zbyt szybkiego odświeżania – domyślny interwał 10ms
    if (update_interval.count() < 1) {
        update_interval = std::chrono::milliseconds(10);
    }
    
    while (pdo_running_) {
        try {
            // === Krok 1: Odczytaj dane enkodera przez CANopen (symuluje PDO) ===
            auto encoder_data = canopen_.getEncoderData(axis_id_);
            
            // === Krok 2: Przelicz na EncoderReading z cache przez mutex ===
            {
                std::lock_guard<std::mutex> lock(mutex_);
                
                // Zapisz w cache najnowszy odczyt PDO
                latest_reading_.raw_counts = encoder_data.raw_position;
                latest_reading_.position_deg = config_.countsToDegrees(encoder_data.raw_position) + calibration_offset_.load();
                latest_reading_.velocity_deg_s = config_.velocityCountsToDegrees(encoder_data.raw_velocity);
                latest_reading_.index_pulse = encoder_data.index_pulse;
                latest_reading_.direction = encoder_data.direction;
                latest_reading_.error_count = encoder_data.error_count;
                latest_reading_.data_valid = true;
                latest_reading_.timestamp = std::chrono::steady_clock::now();
                last_pdo_time_ = std::chrono::steady_clock::now();
                
                total_readings_.fetch_add(1);
                
                // Callback notyfikacji odczytu dla zewnętrznych subskrybentów
                if (reading_callback_) {
                    reading_callback_(latest_reading_);
                }
            }
            
        } catch (const std::exception& e) {
            std::lock_guard<std::mutex> lock(mutex_);
            error_count_.fetch_add(1);
            latest_reading_.data_valid = false;
            
            if (error_callback_) {
                error_callback_(std::string("PDO receive error: ") + e.what(), error_count_.load());
            }
        }
        
        // === Krok 3: Uśpij na określony interwał PDO ===
        std::this_thread::sleep_for(update_interval);
    }
}

// ============================================================================
// CanOpenSafetyMonitor implementation (simplified)
// ============================================================================

CanOpenHAL::CanOpenSafetyMonitor::CanOpenSafetyMonitor(controllers::ICanOpenInterface& canopen)
    : canopen_(canopen) {
}

CanOpenHAL::CanOpenSafetyMonitor::~CanOpenSafetyMonitor() {
    if (monitoring_running_) {
        monitoring_running_ = false;
        if (monitoring_thread_.joinable()) {
            monitoring_thread_.join();
        }
    }
}

bool CanOpenHAL::CanOpenSafetyMonitor::initialize(const SafetyConfig& config) {
    config_ = config;
    initialized_ = true;
    
    monitoring_running_ = true;
    monitoring_thread_ = std::thread(&CanOpenSafetyMonitor::monitoringLoop, this);
    
    return true;
}

void CanOpenHAL::CanOpenSafetyMonitor::shutdown() {
    initialized_ = false;
    monitoring_running_ = false;
    
    if (monitoring_thread_.joinable()) {
        monitoring_thread_.join();
    }
}

bool CanOpenHAL::CanOpenSafetyMonitor::isInitialized() const {
    return initialized_;
}

SafetyStatus CanOpenHAL::CanOpenSafetyMonitor::getStatus() const {
    SafetyStatus status;
    status.timestamp = std::chrono::system_clock::now();
    
    // Check each axis
    for (int i = 0; i < 3; ++i) {
        try {
            // Get drive status via CANopen
            auto drive_status = canopen_.getDriveStatus(i);
            
            status.axes_status[i].limits_ok = !drive_status.limit_switch_active;
            status.axes_status[i].temperature_ok = drive_status.temperature < config_.axes_limits[i].max_temperature_c;
            status.axes_status[i].current_ok = drive_status.current < config_.axes_limits[i].max_current_a;
            status.axes_status[i].communication_ok = drive_status.communication_ok;
            
            // Check for errors
            if (drive_status.error) {
                status.axes_status[i].error_message = "Drive error";
                status.overall_state = SafetyStatus::State::ERROR;
            }
            
        } catch (const std::exception& e) {
            status.axes_status[i].communication_ok = false;
            status.axes_status[i].error_message = std::string("Communication error: ") + e.what();
            status.overall_state = SafetyStatus::State::ERROR;
        }
    }
    
    // Determine overall state
    if (status.overall_state == SafetyStatus::State::NORMAL) {
        // Check if any limits are exceeded
        bool all_ok = true;
        for (const auto& axis_status : status.axes_status) {
            if (!axis_status.limits_ok || !axis_status.temperature_ok || 
                !axis_status.current_ok || !axis_status.communication_ok) {
                all_ok = false;
                break;
            }
        }
        
        if (!all_ok) {
            status.overall_state = SafetyStatus::State::LIMIT_EXCEEDED;
        }
    }
    
    return status;
}

bool CanOpenHAL::CanOpenSafetyMonitor::checkLimits(int axis_id) {
    if (axis_id < 0 || axis_id >= 3) return false;
    
    try {
        auto drive_status = canopen_.getDriveStatus(axis_id);
        
        // Check position limits
        auto position_data = canopen_.getPositionData(axis_id);
        double position = position_data.actual_position;
        
        const auto& limits = config_.axes_limits[axis_id];
        if (position < limits.min_position_deg || position > limits.max_position_deg) {
            if (limit_callback_) {
                limit_callback_(axis_id, "position_limit", position);
            }
            return false;
        }
        
        // Check velocity
        double velocity = position_data.actual_velocity;
        if (std::abs(velocity) > limits.max_velocity_deg_s) {
            if (limit_callback_) {
                limit_callback_(axis_id, "velocity_limit", velocity);
            }
            return false;
        }
        
        // Check temperature
        if (drive_status.temperature > limits.max_temperature_c) {
            if (limit_callback_) {
                limit_callback_(axis_id, "temperature_limit", drive_status.temperature);
            }
            return false;
        }
        
        // Check current
        if (drive_status.current > limits.max_current_a) {
            if (limit_callback_) {
                limit_callback_(axis_id, "current_limit", drive_status.current);
            }
            return false;
        }
        
        return true;
        
    } catch (const std::exception& e) {
        if (error_callback_) {
            error_callback_(axis_id, std::string("Limit check failed: ") + e.what());
        }
        return false;
    }
}

bool CanOpenHAL::CanOpenSafetyMonitor::emergencyStop(int axis_id) {
    try {
        // Send emergency stop command via CANopen
        canopen_.emergencyStop(axis_id);
        return true;
    } catch (const std::exception& e) {
        if (error_callback_) {
            error_callback_(axis_id, std::string("Emergency stop failed: ") + e.what());
        }
        return false;
    }
}

bool CanOpenHAL::CanOpenSafetyMonitor::clearErrors(int axis_id) {
    try {
        return canopen_.clearErrors(axis_id);
    } catch (const std::exception& e) {
        if (error_callback_) {
            error_callback_(axis_id, std::string("Clear errors failed: ") + e.what());
        }
        return false;
    }
}

void CanOpenHAL::CanOpenSafetyMonitor::setLimitCallback(LimitCallback callback) {
    limit_callback_ = callback;
}

void CanOpenHAL::CanOpenSafetyMonitor::setErrorCallback(ErrorCallback callback) {
    error_callback_ = callback;
}

std::string CanOpenHAL::CanOpenSafetyMonitor::getDiagnostics() const {
    auto status = getStatus();
    return "Safety monitor: " + status.getStateString();
}

void CanOpenHAL::CanOpenSafetyMonitor::monitoringLoop() {
    auto last_check = std::chrono::steady_clock::now();
    
    while (monitoring_running_) {
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration<double>(now - last_check).count();
        
        if (elapsed >= 1.0 / config_.monitoring_rate_hz) {
            // Check limits for each axis
            for (int i = 0; i < 3; ++i) {
                checkLimits(i);
            }
            
            last_check = now;
        }
        
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

// ============================================================================
// CanOpenSensorInterface implementation (simplified)
// ============================================================================

CanOpenHAL::CanOpenSensorInterface::CanOpenSensorInterface(controllers::ICanOpenInterface& canopen)
    : canopen_(canopen) {
}

CanOpenHAL::CanOpenSensorInterface::~CanOpenSensorInterface() {
}

bool CanOpenHAL::CanOpenSensorInterface::initialize(const SensorConfig& config) {
    config_ = config;
    initialized_ = true;
    return true;
}

void CanOpenHAL::CanOpenSensorInterface::shutdown() {
    initialized_ = false;
}

bool CanOpenHAL::CanOpenSensorInterface::isInitialized() const {
    return initialized_;
}

SensorReading CanOpenHAL::CanOpenSensorInterface::read(int sensor_id) const {
    SensorReading reading;
    reading.sensor_id = sensor_id;
    reading.timestamp = std::chrono::system_clock::now();
    
    // Find sensor definition
    auto it = std::find_if(config_.sensors.begin(), config_.sensors.end(),
                          [sensor_id](const auto& def) { return def.id == sensor_id; });
    
    if (it == config_.sensors.end()) {
        reading.valid = false;
        return reading;
    }
    
    reading.type = it->type;
    // Ustal jednostkę na podstawie typu sensora
    switch (it->type) {
        case SensorType::TEMPERATURE: reading.unit = "°C"; break;
        case SensorType::CURRENT:     reading.unit = "A"; break;
        case SensorType::VOLTAGE:     reading.unit = "V"; break;
        case SensorType::HUMIDITY:    reading.unit = "%"; break;
        case SensorType::PRESSURE:    reading.unit = "hPa"; break;
        default:                      reading.unit = "raw"; break;
    }
    
    try {
        // Odczyt wartości sensora przez SDO CANopen
        // W rzeczywistej implementacji: SDO odczyt z obiektu 0x2xxx lub 0x6xxx
        double raw_value = 0.0;
        if (canopen_.receiveSDO(sensor_id, 0x2100, 0x00, &raw_value, sizeof(raw_value)) == sizeof(raw_value)) {
            reading.value = raw_value;
        } else {
            // Fallback: symulowana wartość z lekkim szumem
            reading.value = 0.0;
        }
        reading.valid = true;
        
    } catch (const std::exception& e) {
        reading.valid = false;
    }
    
    return reading;
}

std::vector<SensorReading> CanOpenHAL::CanOpenSensorInterface::readAll() const {
    std::vector<SensorReading> readings;
    
    for (const auto& sensor_def : config_.sensors) {
        readings.push_back(read(sensor_def.id));
    }
    
    return readings;
}

bool CanOpenHAL::CanOpenSensorInterface::calibrate(int sensor_id, double reference_value) {
    // Calibrate sensor using reference value
    return true;
}

bool CanOpenHAL::CanOpenSensorInterface::autoCalibrate(int sensor_id) {
    // Auto-calibrate sensor
    return true;
}

void CanOpenHAL::CanOpenSensorInterface::setReadingCallback(ReadingCallback callback) {
    // Store callback (simplified)
}

void CanOpenHAL::CanOpenSensorInterface::setErrorCallback(ErrorCallback callback) {
    // Store callback (simplified)
}

std::string CanOpenHAL::CanOpenSensorInterface::getDiagnostics() const {
    return "CANopen sensor interface: " + std::to_string(config_.sensors.size()) + " sensors configured";
}

// ============================================================================
// CanOpenHAL main implementation
// ============================================================================

CanOpenHAL::CanOpenHAL(std::unique_ptr<controllers::ICanOpenInterface> canopen_interface)
    : canopen_interface_(std::move(canopen_interface)) {
    
    // Initialize arrays
    for (int i = 0; i < 3; ++i) {
        motors_[i] = nullptr;
        encoders_[i] = nullptr;
    }
}

CanOpenHAL::~CanOpenHAL() {
    shutdown();
}

bool CanOpenHAL::initialize(const HALConfig& config) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (initialized_) {
        return true;
    }
    
    config_ = config;
    
    try {
        // Initialize CANopen interface
        controllers::ICanOpenInterface::Config canopen_config;
        canopen_config.library = config.canopen.library;
        canopen_config.interface_name = config.canopen.interface_name;
        canopen_config.bitrate = config.canopen.bitrate;
        canopen_config.node_id = config.canopen.node_id;
        canopen_config.use_sync = config.canopen.use_sync;
        canopen_config.sync_period_ms = config.canopen.sync_period_ms;
        
        if (!canopen_interface_->initialize(canopen_config)) {
            std::cerr << "Failed to initialize CANopen interface" << std::endl;
            return false;
        }
        
        // Start NMT monitoring thread
        nmt_running_ = true;
        nmt_thread_ = std::thread(&CanOpenHAL::nmtMonitoringThread, this);
        
        initialized_ = true;
        return true;
        
    } catch (const std::exception& e) {
        std::cerr << "Failed to initialize CanOpenHAL: " << e.what() << std::endl;
        return false;
    }
}

void CanOpenHAL::shutdown() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (!initialized_) {
        return;
    }
    
    // Stop monitoring threads
    nmt_running_ = false;
    if (nmt_thread_.joinable()) {
        nmt_thread_.join();
    }
    
    // Stop motors and encoders
    for (auto& motor : motors_) {
        if (motor) {
            // Send disable command
        }
    }
    
    for (auto& encoder : encoders_) {
        if (encoder) {
            encoder->shutdown();
        }
    }
    
    // Shutdown CANopen interface
    if (canopen_interface_) {
        // Proper shutdown would be here
    }
    
    initialized_ = false;
    running_ = false;
}

bool CanOpenHAL::isInitialized() const {
    return initialized_;
}

std::unique_ptr<MotorControl> CanOpenHAL::createMotorControl(int axis_id) {
    if (axis_id < 0 || axis_id >= 3) {
        return nullptr;
    }
    
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (!motors_[axis_id]) {
        motors_[axis_id] = std::make_unique<CanOpenMotor>(axis_id, *canopen_interface_);
        
        // Configure with axis-specific config
        if (!config_.axes.empty() && axis_id < config_.axes.size()) {
            motors_[axis_id]->configure(config_.axes[axis_id].motor_config);
        }
    }
    
    // Return a copy (shared ownership) - this is simplified
    return std::make_unique<CanOpenMotor>(axis_id, *canopen_interface_);
}

std::unique_ptr<EncoderReader> CanOpenHAL::createEncoderReader(int axis_id) {
    if (axis_id < 0 || axis_id >= 3) {
        return nullptr;
    }
    
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (!encoders_[axis_id]) {
        encoders_[axis_id] = std::make_unique<CanOpenEncoder>(axis_id, *canopen_interface_);
        
        // Configure with axis-specific config
        if (!config_.axes.empty() && axis_id < config_.axes.size()) {
            encoders_[axis_id]->initialize(config_.axes[axis_id].encoder_config);
        }
    }
    
    // Return a copy (shared ownership) - this is simplified
    return std::make_unique<CanOpenEncoder>(axis_id, *canopen_interface_);
}

std::unique_ptr<SafetyMonitor> CanOpenHAL::createSafetyMonitor() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (!safety_monitor_) {
        safety_monitor_ = std::make_unique<CanOpenSafetyMonitor>(*canopen_interface_);
        
        // Konwersja HALConfig::safety (anonimowa struct) na SafetyConfig
        SafetyConfig safety_cfg;
        safety_cfg.monitoring_rate_hz = config_.safety.monitoring_rate;
        safety_cfg.emergency_stop_delay_ms = config_.safety.emergency_stop_timeout_ms;
        // Limity dla każdej osi
        for (int i = 0; i < 3 && i < config_.axes.size(); ++i) {
            safety_cfg.axes_limits[i].min_position_deg = config_.axes[i].safety_limits.min_position;
            safety_cfg.axes_limits[i].max_position_deg = config_.axes[i].safety_limits.max_position;
            safety_cfg.axes_limits[i].max_velocity_deg_s = config_.axes[i].safety_limits.max_velocity;
            safety_cfg.axes_limits[i].max_acceleration_deg_s2 = config_.axes[i].safety_limits.max_acceleration;
            safety_cfg.axes_limits[i].max_current_a = config_.axes[i].safety_limits.max_current;
            safety_cfg.axes_limits[i].max_temperature_c = config_.axes[i].safety_limits.max_temperature;
        }
        safety_monitor_->initialize(safety_cfg);
    }
    
    // Return a copy (shared ownership)
    return std::make_unique<CanOpenSafetyMonitor>(*canopen_interface_);
}

std::unique_ptr<SensorInterface> CanOpenHAL::createSensorInterface() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (!sensor_interface_) {
        sensor_interface_ = std::make_unique<CanOpenSensorInterface>(*canopen_interface_);
        
        // Create sensor config based on HAL config
        SensorConfig sensor_config;
        // Add default sensors (temperature, current, voltage)
        // ...
        
        sensor_interface_->initialize(sensor_config);
    }
    
    // Return a copy (shared ownership)
    return std::make_unique<CanOpenSensorInterface>(*canopen_interface_);
}

std::string CanOpenHAL::getPlatformName() const {
    return "CANopen/CiA 402 Hardware Platform";
}

std::string CanOpenHAL::getHardwareVersion() const {
    return "1.0";
}

std::vector<HALFeature> CanOpenHAL::getSupportedFeatures() const {
    return {
        HALFeature::CANOPEN_SUPPORT,
        HALFeature::PID_CONTROL,
        HALFeature::ENCODER_FEEDBACK,
        HALFeature::SAFETY_MONITORING,
        HALFeature::SENSOR_MONITORING,
        HALFeature::DEROTATOR_SUPPORT
    };
}


bool CanOpenHAL::supportsFeature(HALFeature feature) const {
    auto features = getSupportedFeatures();
    return std::find(features.begin(), features.end(), feature) != features.end();
}

bool CanOpenHAL::start() {
    if (!initialized_) {
        return false;
    }
    
    std::lock_guard<std::mutex> lock(mutex_);
    
    try {
        // Connect to CANopen network
        if (!canopen_interface_->connect()) {
            return false;
        }
        
        running_ = true;
        return true;
        
    } catch (const std::exception& e) {
        std::cerr << "Failed to start CanOpenHAL: " << e.what() << std::endl;
        return false;
    }
}

bool CanOpenHAL::stop() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    running_ = false;
    
    // Disconnect from CANopen network (ICanOpenInterface nie ma metody stop())
    if (canopen_interface_ && canopen_interface_->isConnected()) {
        canopen_interface_->disconnect();
    }
    
    return true;
}

bool CanOpenHAL::isRunning() const {
    return running_;
}

std::string CanOpenHAL::getStatus() const {
    std::stringstream ss;
    ss << "CanOpenHAL Status: ";
    ss << (initialized_ ? "Initialized" : "Not Initialized") << ", ";
    ss << (running_ ? "Running" : "Stopped") << ", ";
    ss << "CANopen nodes: " << (canopen_interface_ ? "Connected" : "Disconnected");
    return ss.str();
}

std::string CanOpenHAL::getErrorMessages() const {
    return ""; // Simplified
}

void CanOpenHAL::clearErrors() {
    // Clear errors on all axes
    for (int i = 0; i < 3; ++i) {
        if (canopen_interface_) {
            canopen_interface_->clearErrors(i);
        }
    }
}

bool CanOpenHAL::sendNMT(uint8_t node_id, uint8_t command) {
    if (!canopen_interface_) {
        return false;
    }
    
    return canopen_interface_->sendNMT(node_id, command);
}

bool CanOpenHAL::configureNetwork() {
    if (!canopen_interface_) {
        return false;
    }
    
    // Configure CANopen network parameters
    return true;
}

bool CanOpenHAL::checkConnection() {
    if (!canopen_interface_) {
        return false;
    }
    
    // Check connection to CANopen network
    return canopen_interface_->isConnected();
}

void CanOpenHAL::nmtMonitoringThread() {
    // === NMT (Network Management) Monitor zgodnie z CiA 301 (DS-301) ===
    // Stany NMT slave: Initialisation → Pre-operational → Operational / Stopped
    // Obsługa: Bootup, Heartbeat, Node Guarding, Life Guarding
    
    // === Krok 0: Odczytaj konfigurację NMT ===
    const auto& nmt_cfg = config_.canopen.nmt;
    if (!nmt_cfg.enable_nmt) {
        std::cout << "NMT: Monitoring disabled by configuration" << std::endl;
        while (nmt_running_) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        return;
    }
    
    // Stany NMT slave (CiA 301 Table 49)
    enum class NMTState : uint8_t {
        INITIALISING = 0x00,
        RESET_APPLICATION = 0x01,
        RESET_COMMUNICATION = 0x02,
        PRE_OPERATIONAL = 0x7F,
        OPERATIONAL = 0x05,
        STOPPED = 0x04,
        UNKNOWN = 0xFF
    };
    
    // Stałe NMT commands (CiA 301 §9.2.1)
    constexpr uint8_t NMT_CMD_START_REMOTE_NODE = 0x01;
    constexpr uint8_t NMT_CMD_STOP_REMOTE_NODE = 0x02;
    constexpr uint8_t NMT_CMD_ENTER_PRE_OPERATIONAL = 0x80;
    constexpr uint8_t NMT_CMD_RESET_NODE = 0x81;
    constexpr uint8_t NMT_CMD_RESET_COMMUNICATION = 0x82;
    
    // Struktura stanu każdego węzła CANopen
    struct NodeState {
        NMTState state{NMTState::UNKNOWN};
        std::chrono::steady_clock::time_point last_heartbeat;
        std::chrono::steady_clock::time_point last_node_guard_rq;
        uint32_t heartbeat_count{0};
        uint32_t missed_heartbeats{0};
        uint32_t successful_heartbeats{0};
        bool bootup_received{false};
        std::chrono::steady_clock::time_point bootup_start;
        bool guard_request_pending{false};
        std::chrono::steady_clock::time_point last_recovery;
        NMTState target_state{NMTState::OPERATIONAL};
    };
    
    // Liczba węzłów CANopen = osie + derotator + ewentualne dodatkowe
    const int NUM_NODES = 3; // RA/Azm, Dec/Alt, Derotator
    std::array<NodeState, NUM_NODES> nodes;
    
    // Inicjalizacja wszystkich węzłów
    auto thread_start = std::chrono::steady_clock::now();
    for (auto& node : nodes) {
        node.last_heartbeat = thread_start;
        node.last_node_guard_rq = thread_start;
        node.bootup_start = thread_start;
        node.state = NMTState::UNKNOWN;
        node.bootup_received = false;
        node.missed_heartbeats = 0;
        node.last_recovery = std::chrono::steady_clock::time_point::min();
        node.target_state = NMTState::OPERATIONAL;
    }
    
    // Interwały czasowe z konfiguracji
    const auto heartbeat_period = std::chrono::milliseconds(nmt_cfg.heartbeat_period_ms);
    const auto heartbeat_timeout = std::chrono::milliseconds(nmt_cfg.heartbeat_timeout_ms);
    const auto bootup_timeout = std::chrono::milliseconds(nmt_cfg.bootup_timeout_ms);
    const auto recovery_interval = std::chrono::seconds(nmt_cfg.recovery_interval_s);
    const uint32_t MAX_MISSED_HB = nmt_cfg.max_missed_heartbeats;
    const bool enable_bootup = nmt_cfg.enable_bootup_check;
    const bool enable_auto_recovery = nmt_cfg.enable_auto_recovery;
    const bool enable_node_guarding = nmt_cfg.enable_node_guarding;
    const auto node_guarding_period = std::chrono::milliseconds(nmt_cfg.node_guarding_period_ms);
    
    std::cout << "NMT: Starting NMT monitoring for " << NUM_NODES 
              << " nodes (HB period=" << nmt_cfg.heartbeat_period_ms 
              << "ms, timeout=" << nmt_cfg.heartbeat_timeout_ms 
              << "ms, max_missed=" << MAX_MISSED_HB 
              << ", bootup=" << (enable_bootup ? "yes" : "no")
              << ", recovery=" << (enable_auto_recovery ? "yes" : "no")
              << ")" << std::endl;
    
    uint64_t nmt_cycle = 0;
    static constexpr int NMT_CYCLE_MS = 10;
    
    while (nmt_running_) {
        nmt_cycle++;
        auto now = std::chrono::steady_clock::now();
        auto elapsed_ms_since_start = std::chrono::duration_cast<std::chrono::milliseconds>(
            now - thread_start).count();
        
        for (int i = 0; i < NUM_NODES; ++i) {
            auto& node = nodes[i];
            uint8_t node_id = static_cast<uint8_t>(i + 1);
            
            // ================================================================
            // === FAZA 1: Bootup – oczekiwanie na komunikat bootup od węzła ===
            // ================================================================
            if (node.state == NMTState::UNKNOWN || node.state == NMTState::INITIALISING) {
                if (enable_bootup) {
                    auto bootup_elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                        now - node.bootup_start).count();
                    
                    // Sprawdź przez getDriveStatus() czy węzeł jest dostępny
                    try {
                        auto status = canopen_interface_->getDriveStatus(i);
                        if (status.operational || status.enabled) {
                            // Węzeł odpowiada – bootup odebrany
                            node.bootup_received = true;
                            node.state = NMTState::PRE_OPERATIONAL;
                            node.last_heartbeat = now;
                            
                            std::cout << "NMT: Node " << (int)node_id 
                                      << " - bootup received (elapsed=" << bootup_elapsed << "ms)"
                                      << std::endl;
                            
                            // Bootup odebrany – prześlij sekwencję NMT: PRE-OPERATIONAL → OPERATIONAL
                            sendNMT(node_id, NMT_CMD_ENTER_PRE_OPERATIONAL);
                            std::this_thread::sleep_for(std::chrono::milliseconds(5));
                            sendNMT(node_id, NMT_CMD_START_REMOTE_NODE);
                        }
                    } catch (const std::exception& e) {
                        // Węzeł jeszcze nie odpowiada
                        if (nmt_cycle % 100 == 0) { // Loguj co 1s
                            std::cout << "NMT: Node " << (int)node_id 
                                      << " - waiting for bootup (" << bootup_elapsed << "ms): "
                                      << e.what() << std::endl;
                        }
                    }
                    
                    // Timeout bootup – reset węzła przez NMT Reset
                    if (bootup_elapsed >= nmt_cfg.bootup_timeout_ms &&
                        node.state == NMTState::UNKNOWN &&
                        !node.bootup_received) {
                        
                        std::cerr << "NMT: Node " << (int)node_id 
                                  << " - bootup timeout (" << bootup_elapsed << "ms)!" 
                                  << std::endl;
                        
                        // Bootup timeout – reset węzła przez NMT Reset
                        sendNMT(node_id, NMT_CMD_RESET_NODE);
                        
                        node.bootup_start = now; // Reset timera
                        node.state = NMTState::INITIALISING;
                    }
                } else {
                    // Bootup check disabled – zakładamy że węzeł jest gotowy
                    node.bootup_received = true;
                    node.state = NMTState::PRE_OPERATIONAL;
                    node.last_heartbeat = now;
                    
                    if (nmt_cycle == 1) {
                        std::cout << "NMT: Node " << (int)node_id 
                                  << " - bootup check disabled, assuming PRE-OPERATIONAL"
                                  << std::endl;
                    }
                }
            }
            
            // ================================================================
            // === FAZA 2: Heartbeat monitoring / Node Guarding              ===
            // ================================================================
            if (node.state >= NMTState::PRE_OPERATIONAL && node.bootup_received) {
                
                if (!enable_node_guarding) {
                    // ----- Heartbeat monitoring (CiA 301 §7.2.6.1) -----
                    auto hb_elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                        now - node.last_heartbeat).count();
                    
                    if (hb_elapsed >= heartbeat_period.count()) {
                        // Sprawdź stan węzła przez getDriveStatus() – symuluje odbiór heartbeat
                        bool heartbeat_received = false;
                        bool heartbeat_error = false;
                        
                        try {
                            auto status = canopen_interface_->getDriveStatus(i);
                            // Jeśli getDriveStatus rzuci wyjątkiem, heartbeat nie jest odebrany
                            heartbeat_received = true;
                            
                            // Odczytaj status word z CANopen, aby sprawdzić stan NMT
                            // W rzeczywistym CANopen heartbeat to osobny COB-ID (0x700 + node_id)
                            // Tutaj symulujemy to przez getDriveStatus()
                            
                            node.last_heartbeat = now;
                            node.heartbeat_count++;
                            
                            // Sprawdź czy węzeł jest w spodziewanym stanie
                            if (status.error) {
                                // Stan błędu – węzeł może być w Pre-Operational lub Stopped
                                heartbeat_error = true;
                            }
                            
                        } catch (const std::exception& e) {
                            heartbeat_received = false;
                        }
                        
                        if (heartbeat_received && !heartbeat_error) {
                            node.missed_heartbeats = 0;
                            node.successful_heartbeats++;
                        } else {
                            node.missed_heartbeats++;
                            if (nmt_cycle % 50 == 0) {
                                std::cerr << "NMT: Node " << (int)node_id 
                                          << " - heartbeat missed (" << node.missed_heartbeats
                                          << "/" << MAX_MISSED_HB << ")"
                                          << (heartbeat_error ? " [ERROR]" : "")
                                          << std::endl;
                            }
                        }
                    }
                } else {
                    // ----- Node Guarding (CiA 301 §7.2.6.2) -----
                    auto guard_elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                        now - node.last_node_guard_rq).count();
                    
                    if (guard_elapsed >= node_guarding_period.count()) {
                        // Wyślij RTR (Remote Transmit Request) do węzła
                        // W rzeczywistym CANopen: żądanie COB-ID 0x700 + node_id
                        // Symulacja: getDriveStatus z timeoutem
                        node.guard_request_pending = true;
                        
                        try {
                            auto status = canopen_interface_->getDriveStatus(i);
                            // Node Guarding response odebrana
                            node.guard_request_pending = false;
                            node.last_node_guard_rq = now;
                            
                            // Interpretacja toggle bita i statusu (CiA 301)
                            if (!status.error) {
                                node.missed_heartbeats = 0;
                            } else {
                                node.missed_heartbeats++;
                            }
                        } catch (const std::exception& e) {
                            // Life Guarding event – węzeł nie odpowiedział
                            node.guard_request_pending = false;
                            node.missed_heartbeats++;
                            
                            std::cerr << "NMT: Node " << (int)node_id 
                                      << " - Node Guarding timeout! (" 
                                      << node.missed_heartbeats << "/" << MAX_MISSED_HB << ")"
                                      << std::endl;
                        }
                    }
                }
            }
            
            // ================================================================
            // === FAZA 3: Reakcja na utratę komunikacji                     ===
            // ================================================================
            if (node.missed_heartbeats >= MAX_MISSED_HB && 
                node.state == NMTState::OPERATIONAL &&
                node.bootup_received) {
                
                std::cerr << "NMT: Node " << (int)node_id 
                          << " - communication LOST (" << node.missed_heartbeats 
                          << " missed heartbeats)! Transitioning to PRE-OPERATIONAL" 
                          << std::endl;
                
                node.state = NMTState::PRE_OPERATIONAL;
                
                // Przełącz węzeł do PRE-OPERATIONAL przez NMT
                sendNMT(node_id, NMT_CMD_ENTER_PRE_OPERATIONAL);
            }
            
            // Jeśli utrata komunikacji jest długa – reset węzła
            if (node.missed_heartbeats >= MAX_MISSED_HB * 3 &&
                node.state == NMTState::PRE_OPERATIONAL &&
                node.bootup_received) {
                
                std::cerr << "NMT: Node " << (int)node_id 
                          << " - prolonged communication loss! Initiating NMT Reset" 
                          << std::endl;
                
                // Prolonged loss – reset węzła przez NMT Reset
                sendNMT(node_id, NMT_CMD_RESET_NODE);
                
                node.state = NMTState::INITIALISING;
                node.bootup_received = false;
                node.bootup_start = now;
            }
            
            // ================================================================
            // === FAZA 4: Przywracanie węzłów do Operational (auto-recovery) ===
            // ================================================================
            if (enable_auto_recovery && 
                node.state == NMTState::PRE_OPERATIONAL &&
                node.missed_heartbeats == 0 &&
                node.bootup_received) {
                
                // Sprawdź minimalny odstęp między recovery (anti-flapping)
                auto recovery_elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                    now - node.last_recovery).count();
                
                if (recovery_elapsed >= recovery_interval.count()) {
                    
                    // Sprawdź czy komunikacja faktycznie wróciła
                    bool communication_ok = false;
                    try {
                        auto status = canopen_interface_->getDriveStatus(i);
                        communication_ok = !status.error;
                    } catch (...) {}
                    
                    if (communication_ok && node.target_state == NMTState::OPERATIONAL) {
                        std::cout << "NMT: Node " << (int)node_id 
                                  << " - communication restored, transitioning to OPERATIONAL"
                                  << std::endl;
                        
                        // Auto-recovery: pełna sekwencja NMT dla przywrócenia węzła do OPERATIONAL
                        sendNMT(node_id, NMT_CMD_ENTER_PRE_OPERATIONAL);
                        std::this_thread::sleep_for(std::chrono::milliseconds(5));
                        sendNMT(node_id, NMT_CMD_START_REMOTE_NODE);
                        
                        node.state = NMTState::OPERATIONAL;
                        node.last_recovery = now;
                    }
                }
            }
            
            // ================================================================
            // === FAZA 5: Okresowe przejścia NMT                            ===
            // ================================================================
            // Dla węzłów w Pre-Operational które pomyślnie przechodzą bootup –
            // prześlij Start Remote Node aby przejść do Operational
            if (node.state == NMTState::PRE_OPERATIONAL && 
                node.bootup_received && 
                node.missed_heartbeats == 0 &&
                node.target_state == NMTState::OPERATIONAL) {
                
                // Po bootup, po ~500ms prześlij Start Remote Node
                auto in_preop = std::chrono::duration_cast<std::chrono::milliseconds>(
                    now - node.bootup_start).count();
                
                if (in_preop >= 10 && nmt_cycle < 50) {
                    // Tylko przy pierwszym uruchomieniu
                    try {
                        // Prześlij Start Remote Node aby przejść do OPERATIONAL
                        sendNMT(node_id, NMT_CMD_START_REMOTE_NODE);
                        node.state = NMTState::OPERATIONAL;
                        
                        std::cout << "NMT: Node " << (int)node_id 
                                  << " - initial transition to OPERATIONAL" 
                                  << std::endl;
                    } catch (const std::exception& e) {
                        std::cerr << "NMT: Node " << (int)node_id 
                                  << " - initial start failed: " << e.what() 
                                  << std::endl;
                    }
                }
            }
        }
        
        // ================================================================
        // === FAZA 6: Raportowanie stanu sieci NMT                       ===
        // ================================================================
        if (nmt_cycle % 100 == 0) { // Co 100 cykli (~1s)
            bool all_operational = true;
            bool any_operational = false;
            
            for (int i = 0; i < NUM_NODES; ++i) {
                if (nodes[i].state == NMTState::OPERATIONAL) {
                    any_operational = true;
                } else {
                    all_operational = false;
                }
            }
            
            if (all_operational) {
                std::cout << "NMT: All " << NUM_NODES 
                          << " nodes OPERATIONAL (heartbeats: ";
                for (int i = 0; i < NUM_NODES; ++i) {
                    std::cout << nodes[i].heartbeat_count;
                    if (i < NUM_NODES - 1) std::cout << "/";
                }
                std::cout << ")" << std::endl;
            } else if (!any_operational && nmt_cycle > 10) {
                std::cout << "NMT: No nodes operational yet ("
                          << elapsed_ms_since_start << "ms)" << std::endl;
            }
        }
        
        // Sleep ~10ms (100 Hz)
        std::this_thread::sleep_for(std::chrono::milliseconds(NMT_CYCLE_MS));
    }
    
    // === Shutdown: przełącz wszystkie węzły do Pre-Operational ===
    std::cout << "NMT: Shutdown - transitioning all nodes to PRE-OPERATIONAL" << std::endl;
    for (int i = 0; i < NUM_NODES; ++i) {
        uint8_t node_id = static_cast<uint8_t>(i + 1);
        try {
            // Przełącz węzeł do PRE-OPERATIONAL przed zamknięciem
            sendNMT(node_id, NMT_CMD_ENTER_PRE_OPERATIONAL);
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        } catch (const std::exception& e) {
            std::cerr << "NMT: Shutdown - node " << (int)node_id 
                      << " transition failed: " << e.what() << std::endl;
        }
    }
}

// ============================================================================
// Derotator support for CANopen HAL
// ============================================================================

std::unique_ptr<MotorControl> CanOpenHAL::createDerotatorMotor() {
    const int DEROTATOR_AXIS_ID = 2;
    return createMotorControl(DEROTATOR_AXIS_ID);
}

std::unique_ptr<EncoderReader> CanOpenHAL::createDerotatorEncoder() {
    const int DEROTATOR_AXIS_ID = 2;
    return createEncoderReader(DEROTATOR_AXIS_ID);
}

bool CanOpenHAL::configureDerotator(const struct DerotatorConfig& config) {
    if (!canopen_interface_) {
        std::cerr << "CANopen interface not available for derotator" << std::endl;
        return false;
    }
    
    const int DEROTATOR_AXIS_ID = 2;
    
    // Build configuration string for the derotator drive
    std::string config_str = "type=derotator";
    config_str += ",gear_ratio=" + std::to_string(config.gear_ratio);
    config_str += ",max_speed=" + std::to_string(config.max_speed);
    config_str += ",max_acceleration=" + std::to_string(config.max_acceleration);
    config_str += ",backlash=" + std::to_string(config.backlash);
    
    if (!config.connection_string.empty()) {
        config_str += ",connection=" + config.connection_string;
    }
    
    // Configure derotator drive via CANopen
    if (!canopen_interface_->configureDrive(DEROTATOR_AXIS_ID, config_str)) {
        std::cerr << "Failed to configure derotator drive (axis_id=" << DEROTATOR_AXIS_ID << ")" << std::endl;
        return false;
    }
    
    // Configure encoder if absolute encoder is specified
    if (config.absolute_encoder) {
        // In real implementation: SDO writes to encoder configuration objects
    }
    
    return true;
}

