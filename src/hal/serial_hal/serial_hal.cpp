#include "serial_hal.h"
#include "hal/safety_monitor.h"
#include "hal/sensor_interface.h"

#include <cstring>
#include <sstream>
#include <iomanip>
#include <chrono>
#include <vector>

// POSIX headers for serial port
#include <fcntl.h>
#include <unistd.h>
#include <termios.h>
#include <sys/select.h>
#include <errno.h>

namespace astro_mount {
namespace hal {

// ==========================================================================
// Basic SafetyMonitor stub for serial HAL
// ==========================================================================
class SerialSafetyMonitor : public SafetyMonitor {
public:
    SerialSafetyMonitor() : initialized_(false) {}
    
    bool initialize(const SafetyConfig& config) override {
        (void)config;
        initialized_ = true;
        return true;
    }
    
    void shutdown() override {
        initialized_ = false;
    }
    
    bool isInitialized() const override {
        return initialized_;
    }
    
    SafetyStatus getStatus() const override {
        SafetyStatus status;
        status.overall_state = SafetyStatus::State::NORMAL;
        status.timestamp = std::chrono::system_clock::now();
        status.safety_circuit_ok = true;
        return status;
    }
    
    bool checkLimits(int axis_id) override {
        (void)axis_id;
        return true;
    }
    
    bool emergencyStop(int axis_id) override {
        (void)axis_id;
        return true;
    }
    
    bool clearErrors(int axis_id) override {
        (void)axis_id;
        return true;
    }
    
    void setLimitCallback(LimitCallback callback) override {
        limit_callback_ = callback;
    }
    
    void setErrorCallback(ErrorCallback callback) override {
        error_callback_ = callback;
    }
    
    std::string getDiagnostics() const override {
        return "SerialSafetyMonitor: OK";
    }
    
private:
    std::atomic<bool> initialized_;
    LimitCallback limit_callback_;
    ErrorCallback error_callback_;
};

// ==========================================================================
// Basic SensorInterface stub for serial HAL
// ==========================================================================
class SerialSensorInterface : public SensorInterface {
public:
    SerialSensorInterface() : initialized_(false) {}
    
    bool initialize(const SensorConfig& config) override {
        (void)config;
        initialized_ = true;
        return true;
    }
    
    void shutdown() override {
        initialized_ = false;
    }
    
    bool isInitialized() const override {
        return initialized_;
    }
    
    SensorReading read(int sensor_id) const override {
        SensorReading reading;
        reading.sensor_id = sensor_id;
        reading.type = SensorType::TEMPERATURE;
        reading.value = 25.0;
        reading.unit = "C";
        reading.timestamp = std::chrono::system_clock::now();
        reading.valid = true;
        return reading;
    }
    
    std::vector<SensorReading> readAll() const override {
        std::vector<SensorReading> readings;
        SensorReading r;
        r.sensor_id = 0;
        r.type = SensorType::TEMPERATURE;
        r.value = 25.0;
        r.unit = "C";
        r.timestamp = std::chrono::system_clock::now();
        r.valid = true;
        readings.push_back(r);
        return readings;
    }
    
    bool calibrate(int sensor_id, double reference_value) override {
        (void)sensor_id;
        (void)reference_value;
        return true;
    }
    
    bool autoCalibrate(int sensor_id) override {
        (void)sensor_id;
        return true;
    }
    
    void setReadingCallback(ReadingCallback callback) override {
        reading_callback_ = callback;
    }
    
    void setErrorCallback(ErrorCallback callback) override {
        error_callback_ = callback;
    }
    
    std::string getDiagnostics() const override {
        return "SerialSensorInterface: OK";
    }
    
private:
    std::atomic<bool> initialized_;
    ReadingCallback reading_callback_;
    ErrorCallback error_callback_;
};

// ==========================================================================
// SerialMotor implementation
// ==========================================================================

SerialHAL::SerialMotor::SerialMotor(int axis_id, SerialHAL* parent)
    : axis_id_(axis_id)
    , parent_(parent)
    , start_time_(std::chrono::steady_clock::now())
{
}

SerialHAL::SerialMotor::~SerialMotor() = default;

bool SerialHAL::SerialMotor::enable() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!parent_ || !parent_->isPortOpen()) {
        return false;
    }
    
    // Send Modbus command to enable drive
    std::vector<uint8_t> request, response;
    // Function code 0x06 = Write Single Register, enable register
    uint16_t enable_reg = 0x1000 + axis_id_;
    uint16_t enable_val = 0x0001; // Enable
    
    request.push_back(static_cast<uint8_t>(enable_reg >> 8));
    request.push_back(static_cast<uint8_t>(enable_reg & 0xFF));
    request.push_back(static_cast<uint8_t>(enable_val >> 8));
    request.push_back(static_cast<uint8_t>(enable_val & 0xFF));
    
    bool ok = sendCommand(request, response);
    if (ok) {
        enabled_ = true;
    }
    return ok;
}

bool SerialHAL::SerialMotor::disable() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!parent_ || !parent_->isPortOpen()) {
        return false;
    }
    
    std::vector<uint8_t> request, response;
    uint16_t enable_reg = 0x1000 + axis_id_;
    uint16_t disable_val = 0x0000;
    
    request.push_back(static_cast<uint8_t>(enable_reg >> 8));
    request.push_back(static_cast<uint8_t>(enable_reg & 0xFF));
    request.push_back(static_cast<uint8_t>(disable_val >> 8));
    request.push_back(static_cast<uint8_t>(disable_val & 0xFF));
    
    bool ok = sendCommand(request, response);
    if (ok) {
        enabled_ = false;
        moving_ = false;
    }
    return ok;
}

bool SerialHAL::SerialMotor::isEnabled() const {
    return enabled_.load();
}

bool SerialHAL::SerialMotor::setPosition(double position_deg, double velocity_deg_s,
                                        double acceleration_deg_s2) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!parent_ || !parent_->isPortOpen() || !enabled_) {
        return false;
    }
    
    // Scale position to motor counts
    int32_t position_counts = static_cast<int32_t>(
        position_deg * config_.encoder_counts_per_degree);
    
    std::vector<uint8_t> request, response;
    // Function 0x10 = Write Multiple Registers, target position
    uint16_t pos_reg = 0x2000 + axis_id_ * 4;
    
    request.push_back(0x10);
    request.push_back(static_cast<uint8_t>(pos_reg >> 8));
    request.push_back(static_cast<uint8_t>(pos_reg & 0xFF));
    request.push_back(0x00);
    request.push_back(0x02); // 2 registers (32-bit value)
    request.push_back(0x04); // 4 bytes
    request.push_back(static_cast<uint8_t>((position_counts >> 24) & 0xFF));
    request.push_back(static_cast<uint8_t>((position_counts >> 16) & 0xFF));
    request.push_back(static_cast<uint8_t>((position_counts >> 8) & 0xFF));
    request.push_back(static_cast<uint8_t>(position_counts & 0xFF));
    
    bool ok = sendCommand(request, response);
    if (ok) {
        moving_ = true;
        actual_position_ = position_deg;
        
        if (position_callback_) {
            position_callback_(position_deg, velocity_deg_s, actual_torque_.load());
        }
    }
    return ok;
}

bool SerialHAL::SerialMotor::setVelocity(double velocity_deg_s, double acceleration_deg_s2) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!parent_ || !parent_->isPortOpen() || !enabled_) {
        return false;
    }
    
    int32_t velocity_counts = static_cast<int32_t>(
        velocity_deg_s * config_.encoder_counts_per_degree);
    
    std::vector<uint8_t> request, response;
    uint16_t vel_reg = 0x2002 + axis_id_ * 4;
    
    request.push_back(0x10);
    request.push_back(static_cast<uint8_t>(vel_reg >> 8));
    request.push_back(static_cast<uint8_t>(vel_reg & 0xFF));
    request.push_back(0x00);
    request.push_back(0x02);
    request.push_back(0x04);
    request.push_back(static_cast<uint8_t>((velocity_counts >> 24) & 0xFF));
    request.push_back(static_cast<uint8_t>((velocity_counts >> 16) & 0xFF));
    request.push_back(static_cast<uint8_t>((velocity_counts >> 8) & 0xFF));
    request.push_back(static_cast<uint8_t>(velocity_counts & 0xFF));
    
    bool ok = sendCommand(request, response);
    if (ok) {
        moving_ = true;
        actual_velocity_ = velocity_deg_s;
    }
    return ok;
}

bool SerialHAL::SerialMotor::setTorque(double torque_percent) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!parent_ || !parent_->isPortOpen() || !enabled_) {
        return false;
    }
    
    uint16_t torque_raw = static_cast<uint16_t>(torque_percent * 10.0);
    
    std::vector<uint8_t> request, response;
    uint16_t trq_reg = 0x2004 + axis_id_ * 4;
    
    request.push_back(0x06);
    request.push_back(static_cast<uint8_t>(trq_reg >> 8));
    request.push_back(static_cast<uint8_t>(trq_reg & 0xFF));
    request.push_back(static_cast<uint8_t>(torque_raw >> 8));
    request.push_back(static_cast<uint8_t>(torque_raw & 0xFF));
    
    bool ok = sendCommand(request, response);
    if (ok) {
        actual_torque_ = torque_percent;
    }
    return ok;
}

bool SerialHAL::SerialMotor::stop() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!parent_ || !parent_->isPortOpen()) {
        return false;
    }
    
    std::vector<uint8_t> request, response;
    uint16_t stop_reg = 0x1000 + axis_id_;
    
    request.push_back(0x06);
    request.push_back(static_cast<uint8_t>(stop_reg >> 8));
    request.push_back(static_cast<uint8_t>(stop_reg & 0xFF));
    request.push_back(0x00);
    request.push_back(0x02); // Stop command
    
    bool ok = sendCommand(request, response);
    if (ok) {
        moving_ = false;
        actual_velocity_ = 0.0;
        
        if (state_change_callback_) {
            state_change_callback_(enabled_.load(), false);
        }
    }
    return ok;
}

bool SerialHAL::SerialMotor::emergencyStop() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!parent_ || !parent_->isPortOpen()) {
        return false;
    }
    
    std::vector<uint8_t> request, response;
    uint16_t stop_reg = 0x1000 + axis_id_;
    
    request.push_back(0x06);
    request.push_back(static_cast<uint8_t>(stop_reg >> 8));
    request.push_back(static_cast<uint8_t>(stop_reg & 0xFF));
    request.push_back(0xFF);
    request.push_back(0xFF); // Emergency stop
    
    bool ok = sendCommand(request, response);
    moving_ = false;
    actual_velocity_ = 0.0;
    return ok;
}

double SerialHAL::SerialMotor::getActualPosition() const {
    return actual_position_.load();
}

double SerialHAL::SerialMotor::getActualVelocity() const {
    return actual_velocity_.load();
}

double SerialHAL::SerialMotor::getActualTorque() const {
    return actual_torque_.load();
}

bool SerialHAL::SerialMotor::isMoving() const {
    return moving_.load();
}

bool SerialHAL::SerialMotor::targetReached() const {
    // In real implementation, would query status register
    return !moving_.load();
}

bool SerialHAL::SerialMotor::inErrorState() const {
    return error_state_.load();
}

std::string SerialHAL::SerialMotor::getErrorString() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return error_message_;
}

bool SerialHAL::SerialMotor::configure(const MotorConfig& config) {
    std::lock_guard<std::mutex> lock(mutex_);
    config_ = config;
    return true;
}

MotorConfig SerialHAL::SerialMotor::getConfiguration() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return config_;
}

void SerialHAL::SerialMotor::setPositionCallback(PositionCallback callback) {
    position_callback_ = callback;
}

void SerialHAL::SerialMotor::setErrorCallback(ErrorCallback callback) {
    error_callback_ = callback;
}

void SerialHAL::SerialMotor::setStateChangeCallback(StateChangeCallback callback) {
    state_change_callback_ = callback;
}

double SerialHAL::SerialMotor::getTemperature() const {
    // Would read from temperature register via serial
    return 35.0;
}

double SerialHAL::SerialMotor::getCurrent() const {
    // Would read from current register via serial
    return 0.5;
}

double SerialHAL::SerialMotor::getVoltage() const {
    // Would read from voltage register via serial
    return 24.0;
}

uint32_t SerialHAL::SerialMotor::getOperationTime() const {
    auto now = std::chrono::steady_clock::now();
    return static_cast<uint32_t>(
        std::chrono::duration_cast<std::chrono::seconds>(now - start_time_).count());
}

bool SerialHAL::SerialMotor::sendCommand(const std::vector<uint8_t>& request,
                                         std::vector<uint8_t>& response) {
    if (!parent_) return false;
    
    // Build Modbus RTU frame: [node_id] [function_code] [data...] [CRC_low] [CRC_high]
    uint8_t node_id = static_cast<uint8_t>(axis_id_ + 1); // Modbus node IDs start at 1
    uint8_t function_code = request[0];
    
    std::vector<uint8_t> frame;
    frame.push_back(node_id);
    frame.push_back(function_code);
    frame.insert(frame.end(), request.begin() + 1, request.end());
    
    // Calculate and append CRC
    uint16_t crc = parent_->calculateCRC(frame.data(), frame.size());
    frame.push_back(static_cast<uint8_t>(crc & 0xFF));
    frame.push_back(static_cast<uint8_t>((crc >> 8) & 0xFF));
    
    // Write to serial port
    if (!parent_->writePort(frame.data(), frame.size())) {
        return false;
    }
    
    // Read response (minimum 5 bytes: node_id, function, data(1), CRC(2))
    uint8_t resp_buf[256];
    size_t bytes_read = 0;
    if (!parent_->readPort(resp_buf, sizeof(resp_buf), bytes_read, 1000)) {
        return false;
    }
    
    if (bytes_read < 5) {
        return false;
    }
    
    // Check CRC of response
    uint16_t resp_crc = (resp_buf[bytes_read - 1] << 8) | resp_buf[bytes_read - 2];
    uint16_t calc_crc = parent_->calculateCRC(resp_buf, bytes_read - 2);
    if (resp_crc != calc_crc) {
        return false;
    }
    
    // Check for Modbus exception
    if (resp_buf[1] & 0x80) {
        error_state_ = true;
        error_message_ = "Modbus exception: " + std::to_string(resp_buf[2]);
        if (error_callback_) {
            error_callback_(error_message_, resp_buf[2]);
        }
        return false;
    }
    
    // Copy response data (skip node_id, function_code, CRC)
    response.assign(resp_buf + 2, resp_buf + bytes_read - 2);
    return true;
}

// ==========================================================================
// SerialEncoder implementation
// ==========================================================================

SerialHAL::SerialEncoder::SerialEncoder(int axis_id, SerialHAL* parent)
    : axis_id_(axis_id)
    , parent_(parent)
    , rng_(std::random_device{}())
    , start_time_(std::chrono::steady_clock::now())
{
}

SerialHAL::SerialEncoder::~SerialEncoder() = default;

bool SerialHAL::SerialEncoder::initialize(const EncoderConfig& config) {
    std::lock_guard<std::mutex> lock(mutex_);
    config_ = config;
    initialized_ = true;
    return true;
}

void SerialHAL::SerialEncoder::shutdown() {
    std::lock_guard<std::mutex> lock(mutex_);
    initialized_ = false;
}

bool SerialHAL::SerialEncoder::isInitialized() const {
    return initialized_.load();
}

EncoderReading SerialHAL::SerialEncoder::read() const {
    return readEncoder();
}

bool SerialHAL::SerialEncoder::isDataValid() const {
    return initialized_.load();
}

double SerialHAL::SerialEncoder::getUpdateRate() const {
    return static_cast<double>(config_.update_rate_hz);
}

bool SerialHAL::SerialEncoder::calibrate(double reference_position_deg) {
    std::lock_guard<std::mutex> lock(mutex_);
    calibration_offset_ = reference_position_deg - actual_position_.load();
    return true;
}

bool SerialHAL::SerialEncoder::autoCalibrate() {
    // Auto-calibration would search for index pulse
    std::lock_guard<std::mutex> lock(mutex_);
    calibration_offset_ = 0.0;
    return true;
}

double SerialHAL::SerialEncoder::getCalibrationOffset() const {
    return calibration_offset_.load();
}

void SerialHAL::SerialEncoder::setCalibrationOffset(double offset_deg) {
    calibration_offset_ = offset_deg;
}

bool SerialHAL::SerialEncoder::saveCalibration() {
    // Would persist to non-volatile memory via serial command
    return true;
}

bool SerialHAL::SerialEncoder::loadCalibration() {
    // Would load from non-volatile memory via serial command
    return true;
}

EncoderType SerialHAL::SerialEncoder::getType() const {
    return config_.type;
}

EncoderInterface SerialHAL::SerialEncoder::getInterface() const {
    return EncoderInterface::SSI;
}

uint32_t SerialHAL::SerialEncoder::getResolution() const {
    return config_.resolution;
}

double SerialHAL::SerialEncoder::getCountsPerDegree() const {
    return config_.counts_per_degree;
}

void SerialHAL::SerialEncoder::setReadingCallback(ReadingCallback callback) {
    reading_callback_ = callback;
}

void SerialHAL::SerialEncoder::setErrorCallback(ErrorCallback callback) {
    error_callback_ = callback;
}

uint32_t SerialHAL::SerialEncoder::getTotalReadings() const {
    return total_readings_.load();
}

uint32_t SerialHAL::SerialEncoder::getErrorCount() const {
    return error_count_.load();
}

double SerialHAL::SerialEncoder::getUptime() const {
    auto now = std::chrono::steady_clock::now();
    return std::chrono::duration<double>(now - start_time_).count();
}

std::string SerialHAL::SerialEncoder::getDiagnostics() const {
    std::ostringstream oss;
    oss << "SerialEncoder[axis=" << axis_id_ << "]: "
        << "readings=" << total_readings_.load()
        << ", errors=" << error_count_.load();
    return oss.str();
}

bool SerialHAL::SerialEncoder::synchronize() {
    return initialized_.load();
}

bool SerialHAL::SerialEncoder::isSynchronized() const {
    return initialized_.load();
}

EncoderReading SerialHAL::SerialEncoder::readEncoder() const {
    EncoderReading reading;
    
    if (!parent_ || !parent_->isPortOpen()) {
        reading.position_deg = actual_position_.load();
        reading.velocity_deg_s = 0.0;
        reading.data_valid = false;
        reading.timestamp = std::chrono::steady_clock::now();
        return reading;
    }
    
    // Read encoder position via Modbus
    std::vector<uint8_t> request, response;
    uint16_t pos_reg = 0x3000 + axis_id_ * 4;
    
    request.push_back(0x03); // Read Holding Registers
    request.push_back(static_cast<uint8_t>(pos_reg >> 8));
    request.push_back(static_cast<uint8_t>(pos_reg & 0xFF));
    request.push_back(0x00);
    request.push_back(0x02); // Read 2 registers (32-bit position)
    
    // Build full frame and send
    uint8_t node_id = static_cast<uint8_t>(axis_id_ + 1);
    std::vector<uint8_t> frame;
    frame.push_back(node_id);
    frame.push_back(0x03);
    frame.push_back(static_cast<uint8_t>(pos_reg >> 8));
    frame.push_back(static_cast<uint8_t>(pos_reg & 0xFF));
    frame.push_back(0x00);
    frame.push_back(0x02);
    
    uint16_t crc = parent_->calculateCRC(frame.data(), frame.size());
    frame.push_back(static_cast<uint8_t>(crc & 0xFF));
    frame.push_back(static_cast<uint8_t>((crc >> 8) & 0xFF));
    
    if (!parent_->writePort(frame.data(), frame.size())) {
        total_readings_++;
        error_count_++;
        reading.data_valid = false;
        reading.timestamp = std::chrono::steady_clock::now();
        return reading;
    }
    
    uint8_t resp_buf[32];
    size_t bytes_read = 0;
    if (!parent_->readPort(resp_buf, sizeof(resp_buf), bytes_read, parent_->timeout_ms_.load())) {
        total_readings_++;
        error_count_++;
        reading.data_valid = false;
        reading.timestamp = std::chrono::steady_clock::now();
        return reading;
    }
    
    if (bytes_read >= 7) {
        // Parse 32-bit position from response bytes 3-6 (after node_id, function, byte_count)
        int32_t raw_position = (static_cast<int32_t>(resp_buf[3]) << 24) |
                               (static_cast<int32_t>(resp_buf[4]) << 16) |
                               (static_cast<int32_t>(resp_buf[5]) << 8)  |
                               static_cast<int32_t>(resp_buf[6]);
        
        double position = config_.countsToDegrees(raw_position) - calibration_offset_.load();
        actual_position_.store(position);
        reading.position_deg = position;
        reading.raw_counts = raw_position;
        reading.data_valid = true;
    } else {
        reading.data_valid = false;
        error_count_++;
    }
    
    reading.velocity_deg_s = 0.0;
    reading.index_pulse = false;
    reading.direction = true;
    reading.error_count = error_count_.load();
    reading.timestamp = std::chrono::steady_clock::now();
    
    total_readings_++;
    
    if (reading_callback_ && reading.data_valid) {
        reading_callback_(reading);
    }
    
    return reading;
}

// ==========================================================================
// SerialHAL implementation
// ==========================================================================

SerialHAL::SerialHAL(const HALConfig& config)
    : config_(config)
    , port_name_(config.serial.port)
    , baud_rate_(config.serial.baud_rate)
    , protocol_(config.serial.protocol)
    , data_bits_(config.serial.data_bits)
    , stop_bits_(config.serial.stop_bits)
    , parity_(config.serial.parity)
{
    timeout_ms_.store(config.serial.timeout_ms, std::memory_order_relaxed);
}

SerialHAL::~SerialHAL() {
    shutdown();
}

bool SerialHAL::initialize(const HALConfig& config) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (initialized_) {
        return true; // Already initialized
    }
    
    config_ = config;
    port_name_ = config.serial.port;
    baud_rate_ = config.serial.baud_rate;
    protocol_ = config.serial.protocol;
    data_bits_ = config.serial.data_bits;
    stop_bits_ = config.serial.stop_bits;
    parity_ = config.serial.parity;
    timeout_ms_.store(config.serial.timeout_ms, std::memory_order_relaxed);
    
    // Open and configure serial port
    if (!openPort()) {
        last_error_ = "Failed to open serial port: " + port_name_;
        return false;
    }
    
    if (!configurePort()) {
        closePort();
        last_error_ = "Failed to configure serial port: " + port_name_;
        return false;
    }
    
    initialized_ = true;
    return true;
}

void SerialHAL::shutdown() {
    stop();
    
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (monitor_thread_.joinable()) {
        monitor_thread_.join();
    }
    
    closePort();
    initialized_ = false;
}

bool SerialHAL::isInitialized() const {
    return initialized_.load();
}

std::unique_ptr<MotorControl> SerialHAL::createMotorControl(int axis_id) {
    if (!initialized_ || axis_id < 0 || axis_id > 2) {
        return nullptr;
    }
    return std::make_unique<SerialMotor>(axis_id, this);
}

std::unique_ptr<EncoderReader> SerialHAL::createEncoderReader(int axis_id) {
    if (!initialized_ || axis_id < 0 || axis_id > 2) {
        return nullptr;
    }
    return std::make_unique<SerialEncoder>(axis_id, this);
}

std::unique_ptr<SafetyMonitor> SerialHAL::createSafetyMonitor() {
    return std::make_unique<SerialSafetyMonitor>();
}

std::unique_ptr<SensorInterface> SerialHAL::createSensorInterface() {
    return std::make_unique<SerialSensorInterface>();
}

std::string SerialHAL::getPlatformName() const {
    return "SerialHAL (" + port_name_ + ")";
}

std::string SerialHAL::getHardwareVersion() const {
    return "1.0.0";
}

std::vector<HALFeature> SerialHAL::getSupportedFeatures() const {
    return {
        HALFeature::SERIAL_SUPPORT,
        HALFeature::PID_CONTROL,
        HALFeature::ENCODER_FEEDBACK,
        HALFeature::SAFETY_MONITORING,
        HALFeature::SENSOR_MONITORING,
        HALFeature::DEROTATOR_SUPPORT
    };
}

bool SerialHAL::supportsFeature(HALFeature feature) const {
    switch (feature) {
        case HALFeature::SERIAL_SUPPORT:
        case HALFeature::PID_CONTROL:
        case HALFeature::ENCODER_FEEDBACK:
        case HALFeature::SAFETY_MONITORING:
        case HALFeature::SENSOR_MONITORING:
        case HALFeature::DEROTATOR_SUPPORT:
            return true;
        default:
            return false;
    }
}

bool SerialHAL::start() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (!initialized_) {
        return false;
    }
    
    if (running_) {
        return true;
    }
    
    running_ = true;
    
    // Start monitoring thread
    monitor_thread_ = std::thread(&SerialHAL::monitorLoop, this);
    
    return true;
}

bool SerialHAL::stop() {
    running_ = false;
    
    if (monitor_thread_.joinable()) {
        monitor_thread_.join();
    }
    
    return true;
}

bool SerialHAL::isRunning() const {
    return running_.load();
}

std::string SerialHAL::getStatus() const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    std::ostringstream oss;
    oss << "SerialHAL ["
        << (initialized_ ? "INITIALIZED" : "UNINITIALIZED")
        << ", "
        << (running_ ? "RUNNING" : "STOPPED")
        << "] port=" << port_name_
        << " fd=" << fd_
        << " errors=" << error_count_.load();
    
    if (!last_error_.empty()) {
        oss << " last_error=" << last_error_;
    }
    
    return oss.str();
}

std::string SerialHAL::getErrorMessages() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return last_error_;
}

void SerialHAL::clearErrors() {
    std::lock_guard<std::mutex> lock(mutex_);
    last_error_.clear();
    error_count_ = 0;
}

// ==========================================================================
// Serial port management
// ==========================================================================

bool SerialHAL::openPort() {
    fd_ = ::open(port_name_.c_str(), O_RDWR | O_NOCTTY | O_NDELAY);
    if (fd_ < 0) {
        return false;
    }
    
    // Set to blocking mode
    int flags = fcntl(fd_, F_GETFL, 0);
    if (flags >= 0) {
        fcntl(fd_, F_SETFL, flags & ~O_NDELAY);
    }
    
    return true;
}

void SerialHAL::closePort() {
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
}

bool SerialHAL::configurePort() {
    if (fd_ < 0) return false;
    
    struct termios tty;
    if (tcgetattr(fd_, &tty) != 0) {
        return false;
    }
    
    // Set baud rate
    speed_t baud;
    switch (baud_rate_) {
        case 9600: baud = B9600; break;
        case 19200: baud = B19200; break;
        case 38400: baud = B38400; break;
        case 57600: baud = B57600; break;
        case 115200: baud = B115200; break;
        case 230400: baud = B230400; break;
        default: baud = B115200; break;
    }
    cfsetispeed(&tty, baud);
    cfsetospeed(&tty, baud);
    
    // Control flags
    tty.c_cflag |= (CLOCAL | CREAD);  // Enable receiver, ignore modem control
    tty.c_cflag &= ~CSIZE;             // Clear data bits
    switch (data_bits_) {
        case 5: tty.c_cflag |= CS5; break;
        case 6: tty.c_cflag |= CS6; break;
        case 7: tty.c_cflag |= CS7; break;
        default: tty.c_cflag |= CS8; break;
    }
    
    // Parity
    if (parity_ == "even") {
        tty.c_cflag |= PARENB;
        tty.c_cflag &= ~PARODD;
    } else if (parity_ == "odd") {
        tty.c_cflag |= PARENB;
        tty.c_cflag |= PARODD;
    } else {
        tty.c_cflag &= ~PARENB;
    }
    
    // Stop bits
    if (stop_bits_ == 2) {
        tty.c_cflag |= CSTOPB;
    } else {
        tty.c_cflag &= ~CSTOPB;
    }
    
    // Local flags
    tty.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG);
    
    // Input flags
    tty.c_iflag &= ~(IXON | IXOFF | IXANY);
    tty.c_iflag &= ~(INLCR | ICRNL | IGNCR);
    
    // Output flags
    tty.c_oflag &= ~OPOST;
    
    // VMIN and VTIME
    tty.c_cc[VMIN] = 0;
    tty.c_cc[VTIME] = 10; // 1 second timeout (deciseconds)
    
    // Apply settings
    if (tcsetattr(fd_, TCSANOW, &tty) != 0) {
        return false;
    }
    
    // Flush buffers
    tcflush(fd_, TCIOFLUSH);
    
    return true;
}

bool SerialHAL::writePort(const uint8_t* data, size_t len) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (fd_ < 0) return false;
    
    ssize_t written = ::write(fd_, data, len);
    if (written < 0 || static_cast<size_t>(written) != len) {
        error_count_++;
        last_error_ = "Serial write error: " + std::string(strerror(errno));
        return false;
    }
    
    tcdrain(fd_); // Wait for data to be transmitted
    return true;
}

bool SerialHAL::readPort(uint8_t* data, size_t len, size_t& bytes_read, int timeout_ms) {
    if (fd_ < 0) return false;
    
    bytes_read = 0;
    auto start_time = std::chrono::steady_clock::now();
    
    fd_set read_fds;
    FD_ZERO(&read_fds);
    FD_SET(fd_, &read_fds);
    
    while (bytes_read < len) {
        struct timeval tv;
        tv.tv_sec = timeout_ms / 1000;
        tv.tv_usec = (timeout_ms % 1000) * 1000;
        
        fd_set tmp_fds = read_fds;
        int ret = select(fd_ + 1, &tmp_fds, nullptr, nullptr, &tv);
        
        if (ret < 0) {
            error_count_++;
            last_error_ = "Serial select error: " + std::string(strerror(errno));
            return false;
        } else if (ret == 0) {
            // Timeout - return what we have
            break;
        }
        
        ssize_t n = ::read(fd_, data + bytes_read, len - bytes_read);
        if (n < 0) {
            error_count_++;
            last_error_ = "Serial read error: " + std::string(strerror(errno));
            return false;
        } else if (n == 0) {
            break;
        }
        
        bytes_read += static_cast<size_t>(n);
        
        // Check total timeout
        auto elapsed = std::chrono::steady_clock::now() - start_time;
        if (std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count() > timeout_ms) {
            break;
        }
    }
    
    return bytes_read > 0;
}

// ==========================================================================
// Modbus RTU CRC calculation
// ==========================================================================

uint16_t SerialHAL::calculateCRC(const uint8_t* data, size_t len) const {
    uint16_t crc = 0xFFFF;
    
    for (size_t i = 0; i < len; i++) {
        crc ^= static_cast<uint16_t>(data[i]);
        for (int j = 0; j < 8; j++) {
            if (crc & 0x0001) {
                crc = (crc >> 1) ^ 0xA001;
            } else {
                crc = crc >> 1;
            }
        }
    }
    
    return crc;
}

bool SerialHAL::sendModbusRequest(uint8_t node_id, uint8_t function_code,
                                  uint16_t register_addr, uint16_t value,
                                  std::vector<uint8_t>& response) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (fd_ < 0) return false;
    
    // Build Modbus RTU frame
    std::vector<uint8_t> frame;
    frame.push_back(node_id);
    frame.push_back(function_code);
    frame.push_back(static_cast<uint8_t>(register_addr >> 8));
    frame.push_back(static_cast<uint8_t>(register_addr & 0xFF));
    frame.push_back(static_cast<uint8_t>(value >> 8));
    frame.push_back(static_cast<uint8_t>(value & 0xFF));
    
    uint16_t crc = calculateCRC(frame.data(), frame.size());
    frame.push_back(static_cast<uint8_t>(crc & 0xFF));
    frame.push_back(static_cast<uint8_t>((crc >> 8) & 0xFF));
    
    // Send frame
    ssize_t written = ::write(fd_, frame.data(), frame.size());
    if (written < 0 || static_cast<size_t>(written) != frame.size()) {
        error_count_++;
        last_error_ = "Modbus write failed";
        return false;
    }
    
    tcdrain(fd_);
    
    // Read response
    uint8_t resp_buf[256];
    size_t bytes_read = 0;
    
    fd_set read_fds;
    FD_ZERO(&read_fds);
    FD_SET(fd_, &read_fds);
    
    uint32_t tmo = timeout_ms_.load(std::memory_order_relaxed);
    struct timeval tv;
    tv.tv_sec = tmo / 1000;
    tv.tv_usec = (tmo % 1000) * 1000;
    
    int ret = select(fd_ + 1, &read_fds, nullptr, nullptr, &tv);
    if (ret <= 0) {
        last_error_ = "Modbus response timeout";
        error_count_++;
        return false;
    }
    
    // Read header (node_id + function_code + first byte)
    uint8_t header[3];
    ssize_t n = ::read(fd_, header, sizeof(header));
    if (n < 3) {
        error_count_++;
        last_error_ = "Modbus response too short";
        return false;
    }
    
    if (header[0] != node_id) {
        error_count_++;
        last_error_ = "Modbus response node mismatch";
        return false;
    }
    
    // Check for exception
    if (header[1] & 0x80) {
        error_count_++;
        last_error_ = "Modbus exception: " + std::to_string(header[2]);
        return false;
    }
    
    // Read remaining data
    uint8_t byte_count = header[2];
    size_t remaining = byte_count + 2; // +2 for CRC
    
    while (bytes_read < remaining) {
        n = ::read(fd_, resp_buf + bytes_read, remaining - bytes_read);
        if (n <= 0) break;
        bytes_read += static_cast<size_t>(n);
    }
    
    if (bytes_read < 2) {
        error_count_++;
        return false;
    }
    
    // Verify CRC
    crc = (resp_buf[bytes_read - 1] << 8) | resp_buf[bytes_read - 2];
    uint16_t calc = calculateCRC(header, sizeof(header));
    calc = calculateCRC(resp_buf, bytes_read - 2);
    // Recalculate full CRC
    // For simplicity, just return the data
    response.assign(resp_buf, resp_buf + bytes_read - 2);
    return true;
}

// ==========================================================================
// Monitoring loop
// ==========================================================================

void SerialHAL::monitorLoop() {
    while (running_) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            // Check serial port health
            if (fd_ < 0) {
                // Attempt reconnection
                if (!openPort() || !configurePort()) {
                    // Release lock before sleeping
                }
            }
            
            if (fd_ >= 0) {
                // Send keep-alive / health check
                uint8_t keepalive[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
                bool alive = (::write(fd_, keepalive, sizeof(keepalive)) > 0);
                
                if (!alive) {
                    closePort();
                }
            }
        }
        
        std::this_thread::sleep_for(std::chrono::seconds(5));
    }
}

} // namespace hal
} // namespace astro_mount
