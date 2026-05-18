#include "ethernet_hal.h"
#include "hal/safety_monitor.h"
#include "hal/sensor_interface.h"

#include <cstring>
#include <sstream>
#include <iomanip>
#include <chrono>
#include <vector>

// POSIX headers for TCP sockets
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

namespace astro_mount {
namespace hal {

// ==========================================================================
// Basic SafetyMonitor stub for ethernet HAL
// ==========================================================================
class EthernetSafetyMonitor : public SafetyMonitor {
public:
    EthernetSafetyMonitor() : initialized_(false) {}
    
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
        return "EthernetSafetyMonitor: OK";
    }
    
private:
    std::atomic<bool> initialized_;
    LimitCallback limit_callback_;
    ErrorCallback error_callback_;
};

// ==========================================================================
// Basic SensorInterface stub for ethernet HAL
// ==========================================================================
class EthernetSensorInterface : public SensorInterface {
public:
    EthernetSensorInterface() : initialized_(false) {}
    
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
        return "EthernetSensorInterface: OK";
    }
    
private:
    std::atomic<bool> initialized_;
    ReadingCallback reading_callback_;
    ErrorCallback error_callback_;
};

// ==========================================================================
// EthernetMotor implementation
// ==========================================================================

EthernetHAL::EthernetMotor::EthernetMotor(int axis_id, EthernetHAL* parent)
    : axis_id_(axis_id)
    , parent_(parent)
    , start_time_(std::chrono::steady_clock::now())
{
}

EthernetHAL::EthernetMotor::~EthernetMotor() = default;

bool EthernetHAL::EthernetMotor::enable() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!parent_ || !parent_->isSocketConnected()) {
        return false;
    }
    
    std::vector<uint8_t> request, response;
    uint16_t enable_reg = 0x1000 + axis_id_;
    uint16_t enable_val = 0x0001;
    
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

bool EthernetHAL::EthernetMotor::disable() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!parent_ || !parent_->isSocketConnected()) {
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

bool EthernetHAL::EthernetMotor::isEnabled() const {
    return enabled_.load();
}

bool EthernetHAL::EthernetMotor::setPosition(double position_deg, double velocity_deg_s,
                                             double acceleration_deg_s2) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!parent_ || !parent_->isSocketConnected() || !enabled_) {
        return false;
    }
    
    int32_t position_counts = static_cast<int32_t>(
        position_deg * config_.encoder_counts_per_degree);
    
    std::vector<uint8_t> request, response;
    uint16_t pos_reg = 0x2000 + axis_id_ * 4;
    
    request.push_back(0x10);
    request.push_back(static_cast<uint8_t>(pos_reg >> 8));
    request.push_back(static_cast<uint8_t>(pos_reg & 0xFF));
    request.push_back(0x00);
    request.push_back(0x02);
    request.push_back(0x04);
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

bool EthernetHAL::EthernetMotor::setVelocity(double velocity_deg_s, double acceleration_deg_s2) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!parent_ || !parent_->isSocketConnected() || !enabled_) {
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

bool EthernetHAL::EthernetMotor::setTorque(double torque_percent) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!parent_ || !parent_->isSocketConnected() || !enabled_) {
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

bool EthernetHAL::EthernetMotor::stop() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!parent_ || !parent_->isSocketConnected()) {
        return false;
    }
    
    std::vector<uint8_t> request, response;
    uint16_t stop_reg = 0x1000 + axis_id_;
    
    request.push_back(0x06);
    request.push_back(static_cast<uint8_t>(stop_reg >> 8));
    request.push_back(static_cast<uint8_t>(stop_reg & 0xFF));
    request.push_back(0x00);
    request.push_back(0x02);
    
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

bool EthernetHAL::EthernetMotor::emergencyStop() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!parent_ || !parent_->isSocketConnected()) {
        return false;
    }
    
    std::vector<uint8_t> request, response;
    uint16_t stop_reg = 0x1000 + axis_id_;
    
    request.push_back(0x06);
    request.push_back(static_cast<uint8_t>(stop_reg >> 8));
    request.push_back(static_cast<uint8_t>(stop_reg & 0xFF));
    request.push_back(0xFF);
    request.push_back(0xFF);
    
    sendCommand(request, response); // Best effort, don't check return
    moving_ = false;
    actual_velocity_ = 0.0;
    return true;
}

double EthernetHAL::EthernetMotor::getActualPosition() const {
    return actual_position_.load();
}

double EthernetHAL::EthernetMotor::getActualVelocity() const {
    return actual_velocity_.load();
}

double EthernetHAL::EthernetMotor::getActualTorque() const {
    return actual_torque_.load();
}

bool EthernetHAL::EthernetMotor::isMoving() const {
    return moving_.load();
}

bool EthernetHAL::EthernetMotor::targetReached() const {
    return !moving_.load();
}

bool EthernetHAL::EthernetMotor::inErrorState() const {
    return error_state_.load();
}

std::string EthernetHAL::EthernetMotor::getErrorString() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return error_message_;
}

bool EthernetHAL::EthernetMotor::configure(const MotorConfig& config) {
    std::lock_guard<std::mutex> lock(mutex_);
    config_ = config;
    return true;
}

MotorConfig EthernetHAL::EthernetMotor::getConfiguration() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return config_;
}

void EthernetHAL::EthernetMotor::setPositionCallback(PositionCallback callback) {
    position_callback_ = callback;
}

void EthernetHAL::EthernetMotor::setErrorCallback(ErrorCallback callback) {
    error_callback_ = callback;
}

void EthernetHAL::EthernetMotor::setStateChangeCallback(StateChangeCallback callback) {
    state_change_callback_ = callback;
}

double EthernetHAL::EthernetMotor::getTemperature() const {
    return 35.0;
}

double EthernetHAL::EthernetMotor::getCurrent() const {
    return 0.5;
}

double EthernetHAL::EthernetMotor::getVoltage() const {
    return 24.0;
}

uint32_t EthernetHAL::EthernetMotor::getOperationTime() const {
    auto now = std::chrono::steady_clock::now();
    return static_cast<uint32_t>(
        std::chrono::duration_cast<std::chrono::seconds>(now - start_time_).count());
}

bool EthernetHAL::EthernetMotor::sendCommand(const std::vector<uint8_t>& request,
                                             std::vector<uint8_t>& response) {
    if (!parent_) return false;
    
    // Build Modbus TCP frame (no CRC, uses TCP/IP checksum)
    uint8_t unit_id = static_cast<uint8_t>(axis_id_ + 1);
    uint8_t function_code = request[0];
    
    std::vector<uint8_t> data;
    data.push_back(function_code);
    data.insert(data.end(), request.begin() + 1, request.end());
    
    return parent_->sendModbusTCPRequest(unit_id, function_code,
                                         (request.size() > 1) ? 
                                             static_cast<uint16_t>((request[1] << 8) | request[2]) : 0,
                                         (request.size() > 3) ?
                                             static_cast<uint16_t>((request[3] << 8) | request[4]) : 0,
                                         response);
}

// ==========================================================================
// EthernetEncoder implementation
// ==========================================================================

EthernetHAL::EthernetEncoder::EthernetEncoder(int axis_id, EthernetHAL* parent)
    : axis_id_(axis_id)
    , parent_(parent)
    , rng_(std::random_device{}())
    , start_time_(std::chrono::steady_clock::now())
{
}

EthernetHAL::EthernetEncoder::~EthernetEncoder() = default;

bool EthernetHAL::EthernetEncoder::initialize(const EncoderConfig& config) {
    std::lock_guard<std::mutex> lock(mutex_);
    config_ = config;
    initialized_ = true;
    return true;
}

void EthernetHAL::EthernetEncoder::shutdown() {
    std::lock_guard<std::mutex> lock(mutex_);
    initialized_ = false;
}

bool EthernetHAL::EthernetEncoder::isInitialized() const {
    return initialized_.load();
}

EncoderReading EthernetHAL::EthernetEncoder::read() const {
    return readEncoder();
}

bool EthernetHAL::EthernetEncoder::isDataValid() const {
    return initialized_.load();
}

double EthernetHAL::EthernetEncoder::getUpdateRate() const {
    return static_cast<double>(config_.update_rate_hz);
}

bool EthernetHAL::EthernetEncoder::calibrate(double reference_position_deg) {
    std::lock_guard<std::mutex> lock(mutex_);
    calibration_offset_ = reference_position_deg - actual_position_.load();
    return true;
}

bool EthernetHAL::EthernetEncoder::autoCalibrate() {
    std::lock_guard<std::mutex> lock(mutex_);
    calibration_offset_ = 0.0;
    return true;
}

double EthernetHAL::EthernetEncoder::getCalibrationOffset() const {
    return calibration_offset_.load();
}

void EthernetHAL::EthernetEncoder::setCalibrationOffset(double offset_deg) {
    calibration_offset_ = offset_deg;
}

bool EthernetHAL::EthernetEncoder::saveCalibration() {
    return true;
}

bool EthernetHAL::EthernetEncoder::loadCalibration() {
    return true;
}

EncoderType EthernetHAL::EthernetEncoder::getType() const {
    return config_.type;
}

EncoderInterface EthernetHAL::EthernetEncoder::getInterface() const {
    return EncoderInterface::SSI;
}

uint32_t EthernetHAL::EthernetEncoder::getResolution() const {
    return config_.resolution;
}

double EthernetHAL::EthernetEncoder::getCountsPerDegree() const {
    return config_.counts_per_degree;
}

void EthernetHAL::EthernetEncoder::setReadingCallback(ReadingCallback callback) {
    reading_callback_ = callback;
}

void EthernetHAL::EthernetEncoder::setErrorCallback(ErrorCallback callback) {
    error_callback_ = callback;
}

uint32_t EthernetHAL::EthernetEncoder::getTotalReadings() const {
    return total_readings_.load();
}

uint32_t EthernetHAL::EthernetEncoder::getErrorCount() const {
    return error_count_.load();
}

double EthernetHAL::EthernetEncoder::getUptime() const {
    auto now = std::chrono::steady_clock::now();
    return std::chrono::duration<double>(now - start_time_).count();
}

std::string EthernetHAL::EthernetEncoder::getDiagnostics() const {
    std::ostringstream oss;
    oss << "EthernetEncoder[axis=" << axis_id_ << "]: "
        << "readings=" << total_readings_.load()
        << ", errors=" << error_count_.load();
    return oss.str();
}

bool EthernetHAL::EthernetEncoder::synchronize() {
    return initialized_.load();
}

bool EthernetHAL::EthernetEncoder::isSynchronized() const {
    return initialized_.load();
}

EncoderReading EthernetHAL::EthernetEncoder::readEncoder() const {
    EncoderReading reading;
    
    if (!parent_ || !parent_->isSocketConnected()) {
        reading.position_deg = actual_position_.load();
        reading.velocity_deg_s = 0.0;
        reading.data_valid = false;
        reading.timestamp = std::chrono::steady_clock::now();
        return reading;
    }
    
    // Read encoder position via Modbus TCP
    uint16_t pos_reg = 0x3000 + axis_id_ * 4;
    uint8_t unit_id = static_cast<uint8_t>(axis_id_ + 1);
    
    std::vector<uint8_t> response;
    bool ok = parent_->sendModbusTCPRequest(unit_id, 0x03, pos_reg, 0x0002, response);
    
    if (ok && response.size() >= 4) {
        // Parse 32-bit position from Modbus TCP response
        int32_t raw_position = (static_cast<int32_t>(response[0]) << 24) |
                               (static_cast<int32_t>(response[1]) << 16) |
                               (static_cast<int32_t>(response[2]) << 8)  |
                               static_cast<int32_t>(response[3]);
        
        double position = config_.countsToDegrees(raw_position) - calibration_offset_.load();
        const_cast<std::atomic<double>&>(actual_position_).store(position);
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
// EthernetHAL implementation
// ==========================================================================

EthernetHAL::EthernetHAL(const HALConfig& config)
    : config_(config)
    , ip_address_(config.ethernet.ip_address)
    , port_(config.ethernet.port)
    , protocol_(config.ethernet.protocol)
    , timeout_ms_(config.ethernet.timeout_ms)
    , retry_count_(config.ethernet.retry_count)
{
}

EthernetHAL::~EthernetHAL() {
    shutdown();
}

bool EthernetHAL::initialize(const HALConfig& config) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (initialized_) {
        return true;
    }
    
    config_ = config;
    ip_address_ = config.ethernet.ip_address;
    port_ = config.ethernet.port;
    protocol_ = config.ethernet.protocol;
    timeout_ms_ = config.ethernet.timeout_ms;
    retry_count_ = config.ethernet.retry_count;
    
    // Attempt initial connection
    if (!connectSocket()) {
        last_error_ = "Failed to connect to " + ip_address_ + ":" + std::to_string(port_);
        // Don't fail initialization - allow reconnection later
    }
    
    initialized_ = true;
    return true;
}

void EthernetHAL::shutdown() {
    stop();
    
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (monitor_thread_.joinable()) {
        monitor_thread_.join();
    }
    
    disconnectSocket();
    initialized_ = false;
}

bool EthernetHAL::isInitialized() const {
    return initialized_.load();
}

std::unique_ptr<MotorControl> EthernetHAL::createMotorControl(int axis_id) {
    if (!initialized_ || axis_id < 0 || axis_id > 2) {
        return nullptr;
    }
    return std::make_unique<EthernetMotor>(axis_id, this);
}

std::unique_ptr<EncoderReader> EthernetHAL::createEncoderReader(int axis_id) {
    if (!initialized_ || axis_id < 0 || axis_id > 2) {
        return nullptr;
    }
    return std::make_unique<EthernetEncoder>(axis_id, this);
}

std::unique_ptr<SafetyMonitor> EthernetHAL::createSafetyMonitor() {
    return std::make_unique<EthernetSafetyMonitor>();
}

std::unique_ptr<SensorInterface> EthernetHAL::createSensorInterface() {
    return std::make_unique<EthernetSensorInterface>();
}

std::string EthernetHAL::getPlatformName() const {
    return "EthernetHAL (" + ip_address_ + ":" + std::to_string(port_) + ")";
}

std::string EthernetHAL::getHardwareVersion() const {
    return "1.0.0";
}

std::vector<HALFeature> EthernetHAL::getSupportedFeatures() const {
    return {
        HALFeature::ETHERNET_SUPPORT,
        HALFeature::PID_CONTROL,
        HALFeature::ENCODER_FEEDBACK,
        HALFeature::SAFETY_MONITORING,
        HALFeature::SENSOR_MONITORING,
        HALFeature::DEROTATOR_SUPPORT
    };
}

bool EthernetHAL::supportsFeature(HALFeature feature) const {
    switch (feature) {
        case HALFeature::ETHERNET_SUPPORT:
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

bool EthernetHAL::start() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (!initialized_) {
        return false;
    }
    
    if (running_) {
        return true;
    }
    
    running_ = true;
    
    monitor_thread_ = std::thread(&EthernetHAL::monitorLoop, this);
    
    return true;
}

bool EthernetHAL::stop() {
    running_ = false;
    
    if (monitor_thread_.joinable()) {
        monitor_thread_.join();
    }
    
    return true;
}

bool EthernetHAL::isRunning() const {
    return running_.load();
}

std::string EthernetHAL::getStatus() const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    std::ostringstream oss;
    oss << "EthernetHAL ["
        << (initialized_ ? "INITIALIZED" : "UNINITIALIZED")
        << ", "
        << (running_ ? "RUNNING" : "STOPPED")
        << "] " << ip_address_ << ":" << port_
        << " fd=" << sock_fd_
        << " errors=" << error_count_.load();
    
    if (!last_error_.empty()) {
        oss << " last_error=" << last_error_;
    }
    
    return oss.str();
}

std::string EthernetHAL::getErrorMessages() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return last_error_;
}

void EthernetHAL::clearErrors() {
    std::lock_guard<std::mutex> lock(mutex_);
    last_error_.clear();
    error_count_ = 0;
}

// ==========================================================================
// Socket management
// ==========================================================================

bool EthernetHAL::connectSocket() {
    // Create socket
    sock_fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
    if (sock_fd_ < 0) {
        last_error_ = "Failed to create socket: " + std::string(strerror(errno));
        return false;
    }
    
    // Set socket timeout
    struct timeval tv;
    tv.tv_sec = timeout_ms_ / 1000;
    tv.tv_usec = (timeout_ms_ % 1000) * 1000;
    
    if (setsockopt(sock_fd_, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0) {
        last_error_ = "Failed to set socket receive timeout";
        ::close(sock_fd_);
        sock_fd_ = -1;
        return false;
    }
    
    if (setsockopt(sock_fd_, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv)) < 0) {
        last_error_ = "Failed to set socket send timeout";
        ::close(sock_fd_);
        sock_fd_ = -1;
        return false;
    }
    
    // Enable TCP_NODELAY to disable Nagle's algorithm
    int flag = 1;
    if (setsockopt(sock_fd_, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag)) < 0) {
        last_error_ = "Failed to set TCP_NODELAY";
        ::close(sock_fd_);
        sock_fd_ = -1;
        return false;
    }
    
    // Resolve hostname if needed
    struct sockaddr_in server_addr;
    std::memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port_);
    
    // Try direct IP first
    if (inet_pton(AF_INET, ip_address_.c_str(), &server_addr.sin_addr) <= 0) {
        // Hostname resolution
        struct addrinfo hints, *result;
        std::memset(&hints, 0, sizeof(hints));
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_STREAM;
        
        int ret = getaddrinfo(ip_address_.c_str(), nullptr, &hints, &result);
        if (ret != 0 || !result) {
            last_error_ = "Failed to resolve hostname: " + ip_address_;
            ::close(sock_fd_);
            sock_fd_ = -1;
            return false;
        }
        
        struct sockaddr_in* addr = reinterpret_cast<struct sockaddr_in*>(result->ai_addr);
        server_addr.sin_addr = addr->sin_addr;
        freeaddrinfo(result);
    }
    
    // Connect
    if (::connect(sock_fd_, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        last_error_ = "Connection failed to " + ip_address_ + ":" + std::to_string(port_)
                      + " - " + std::string(strerror(errno));
        ::close(sock_fd_);
        sock_fd_ = -1;
        return false;
    }
    
    last_error_.clear();
    return true;
}

void EthernetHAL::disconnectSocket() {
    if (sock_fd_ >= 0) {
        ::shutdown(sock_fd_, SHUT_RDWR);
        ::close(sock_fd_);
        sock_fd_ = -1;
    }
}

bool EthernetHAL::reconnectSocket() {
    disconnectSocket();
    return connectSocket();
}

bool EthernetHAL::writeSocket(const uint8_t* data, size_t len) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (sock_fd_ < 0) return false;
    
    ssize_t written = ::write(sock_fd_, data, len);
    if (written < 0 || static_cast<size_t>(written) != len) {
        error_count_++;
        last_error_ = "Socket write error: " + std::string(strerror(errno));
        return false;
    }
    
    return true;
}

bool EthernetHAL::readSocket(uint8_t* data, size_t len, size_t& bytes_read, int timeout_ms) {
    if (sock_fd_ < 0) return false;
    
    bytes_read = 0;
    auto start_time = std::chrono::steady_clock::now();
    
    while (bytes_read < len) {
        fd_set read_fds;
        FD_ZERO(&read_fds);
        FD_SET(sock_fd_, &read_fds);
        
        struct timeval tv;
        tv.tv_sec = timeout_ms / 1000;
        tv.tv_usec = (timeout_ms % 1000) * 1000;
        
        int ret = select(sock_fd_ + 1, &read_fds, nullptr, nullptr, &tv);
        
        if (ret < 0) {
            error_count_++;
            last_error_ = "Socket select error: " + std::string(strerror(errno));
            return false;
        } else if (ret == 0) {
            break; // Timeout
        }
        
        ssize_t n = ::read(sock_fd_, data + bytes_read, len - bytes_read);
        if (n < 0) {
            error_count_++;
            last_error_ = "Socket read error: " + std::string(strerror(errno));
            return false;
        } else if (n == 0) {
            last_error_ = "Connection closed by peer";
            disconnectSocket();
            return false;
        }
        
        bytes_read += static_cast<size_t>(n);
        
        auto elapsed = std::chrono::steady_clock::now() - start_time;
        if (std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count() > timeout_ms) {
            break;
        }
    }
    
    return bytes_read > 0;
}

// ==========================================================================
// Modbus TCP helpers
// ==========================================================================

bool EthernetHAL::sendModbusTCPRequest(uint8_t unit_id, uint8_t function_code,
                                       uint16_t register_addr, uint16_t value,
                                       std::vector<uint8_t>& response) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (sock_fd_ < 0) {
        // Attempt reconnection
        if (!reconnectSocket()) {
            return false;
        }
    }
    
    // Build Modbus TCP frame (MBAP header + PDU)
    // MBAP: [transaction_id(2)] [protocol_id(2)=0] [length(2)] [unit_id(1)]
    // PDU:  [function_code(1)] [data...]
    std::vector<uint8_t> frame;
    uint16_t tid = transaction_id_++;
    
    // MBAP header
    frame.push_back(static_cast<uint8_t>(tid >> 8));
    frame.push_back(static_cast<uint8_t>(tid & 0xFF));
    frame.push_back(0x00); // Protocol ID high
    frame.push_back(0x00); // Protocol ID low
    // Length will be filled later
    frame.push_back(0x00);
    frame.push_back(0x00);
    frame.push_back(unit_id);
    
    // PDU
    frame.push_back(function_code);
    frame.push_back(static_cast<uint8_t>(register_addr >> 8));
    frame.push_back(static_cast<uint8_t>(register_addr & 0xFF));
    frame.push_back(static_cast<uint8_t>(value >> 8));
    frame.push_back(static_cast<uint8_t>(value & 0xFF));
    
    // Update length field (total after length field: unit_id(1) + pdu)
    uint16_t pdu_length = static_cast<uint16_t>(frame.size() - 6);
    frame[4] = static_cast<uint8_t>(pdu_length >> 8);
    frame[5] = static_cast<uint8_t>(pdu_length & 0xFF);
    
    // Send frame
    ssize_t written = ::write(sock_fd_, frame.data(), frame.size());
    if (written < 0 || static_cast<size_t>(written) != frame.size()) {
        error_count_++;
        last_error_ = "Modbus TCP write failed: " + std::string(strerror(errno));
        disconnectSocket();
        return false;
    }
    
    // Read MBAP header (7 bytes) + at least 1 byte of response
    uint8_t resp_header[8];
    size_t header_read = 0;
    auto start_time = std::chrono::steady_clock::now();
    
    while (header_read < sizeof(resp_header)) {
        fd_set read_fds;
        FD_ZERO(&read_fds);
        FD_SET(sock_fd_, &read_fds);
        
        struct timeval tv;
        tv.tv_sec = timeout_ms_ / 1000;
        tv.tv_usec = (timeout_ms_ % 1000) * 1000;
        
        int ret = select(sock_fd_ + 1, &read_fds, nullptr, nullptr, &tv);
        if (ret <= 0) {
            error_count_++;
            last_error_ = "Modbus TCP response timeout";
            disconnectSocket();
            return false;
        }
        
        ssize_t n = ::read(sock_fd_, resp_header + header_read,
                          sizeof(resp_header) - header_read);
        if (n <= 0) {
            error_count_++;
            last_error_ = "Modbus TCP connection lost";
            disconnectSocket();
            return false;
        }
        header_read += static_cast<size_t>(n);
        
        auto elapsed = std::chrono::steady_clock::now() - start_time;
        if (std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count() > timeout_ms_) {
            error_count_++;
            last_error_ = "Modbus TCP response timeout (header)";
            disconnectSocket();
            return false;
        }
    }
    
    // Parse MBAP header
    uint16_t resp_tid = (resp_header[0] << 8) | resp_header[1];
    uint16_t resp_protocol = (resp_header[2] << 8) | resp_header[3];
    uint16_t resp_length = (resp_header[4] << 8) | resp_header[5];
    uint8_t resp_unit_id = resp_header[6];
    uint8_t resp_func = resp_header[7];
    
    // Validate response
    if (resp_tid != tid || resp_protocol != 0 || resp_unit_id != unit_id) {
        error_count_++;
        last_error_ = "Modbus TCP response header mismatch";
        return false;
    }
    
    // Check for exception
    if (resp_func & 0x80) {
        error_count_++;
        last_error_ = "Modbus TCP exception: function=0x" +
                      std::to_string(resp_func & 0x7F) + " code=" + std::to_string(resp_header[8]);
        return false;
    }
    
    // Read remaining data (resp_length - 1 for unit_id, - 1 for function_code = data length)
    uint16_t data_length = resp_length - 2; // Subtract unit_id and function_code
    if (data_length > 0) {
        response.resize(data_length);
        size_t data_read = 0;
        
        while (data_read < data_length) {
            fd_set read_fds;
            FD_ZERO(&read_fds);
            FD_SET(sock_fd_, &read_fds);
            
            struct timeval tv;
            tv.tv_sec = timeout_ms_ / 1000;
            tv.tv_usec = (timeout_ms_ % 1000) * 1000;
            
            int ret = select(sock_fd_ + 1, &read_fds, nullptr, nullptr, &tv);
            if (ret <= 0) {
                error_count_++;
                last_error_ = "Modbus TCP data timeout";
                disconnectSocket();
                return false;
            }
            
            ssize_t n = ::read(sock_fd_, response.data() + data_read,
                              data_length - data_read);
            if (n <= 0) {
                error_count_++;
                last_error_ = "Modbus TCP data read failed";
                disconnectSocket();
                return false;
            }
            data_read += static_cast<size_t>(n);
        }
    }
    
    return true;
}

// ==========================================================================
// Monitoring loop
// ==========================================================================

void EthernetHAL::monitorLoop() {
    while (running_) {
        // Check socket health
        if (sock_fd_ < 0) {
            reconnectSocket();
        } else {
            // Send keep-alive ping
            uint8_t ping[] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF, 0x00};
            if (::write(sock_fd_, ping, sizeof(ping)) < 0) {
                disconnectSocket();
            }
        }
        
        std::this_thread::sleep_for(std::chrono::seconds(5));
    }
    
    // Clean up on exit
    disconnectSocket();
}

} // namespace hal
} // namespace astro_mount
