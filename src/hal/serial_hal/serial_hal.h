#pragma once
#include "hal/hal_interface.h"
#include "hal/motor_control.h"
#include "hal/encoder_reader.h"
#include "hal/hal_config.h"

#include <memory>
#include <mutex>
#include <thread>
#include <atomic>
#include <string>
#include <array>
#include <random>

namespace astro_mount {
namespace hal {

class SerialHAL : public HALInterface {
private:
    struct SerialMotor : public MotorControl {
        SerialMotor(int axis_id, SerialHAL* parent);
        ~SerialMotor() override;
        
        bool enable() override;
        bool disable() override;
        bool isEnabled() const override;
        
        bool setPosition(double position_deg, double velocity_deg_s,
                        double acceleration_deg_s2) override;
        bool setVelocity(double velocity_deg_s, double acceleration_deg_s2) override;
        bool setTorque(double torque_percent) override;
        bool stop() override;
        bool emergencyStop() override;
        
        double getActualPosition() const override;
        double getActualVelocity() const override;
        double getActualTorque() const override;
        bool isMoving() const override;
        bool targetReached() const override;
        bool inErrorState() const override;
        std::string getErrorString() const override;
        
        bool configure(const MotorConfig& config) override;
        MotorConfig getConfiguration() const override;
        
        void setPositionCallback(PositionCallback callback) override;
        void setErrorCallback(ErrorCallback callback) override;
        void setStateChangeCallback(StateChangeCallback callback) override;
        
        double getTemperature() const override;
        double getCurrent() const override;
        double getVoltage() const override;
        uint32_t getOperationTime() const override;
        
    private:
        int axis_id_;
        SerialHAL* parent_;
        MotorConfig config_;
        std::atomic<bool> enabled_{false};
        std::atomic<bool> moving_{false};
        std::atomic<double> actual_position_{0.0};
        std::atomic<double> actual_velocity_{0.0};
        std::atomic<double> actual_torque_{0.0};
        std::atomic<bool> error_state_{false};
        std::string error_message_;
        
        PositionCallback position_callback_;
        ErrorCallback error_callback_;
        StateChangeCallback state_change_callback_;
        
        mutable std::mutex mutex_;
        std::chrono::steady_clock::time_point start_time_;
        
        // Send Modbus RTU command via serial port
        bool sendCommand(const std::vector<uint8_t>& request, std::vector<uint8_t>& response);
    };
    
    struct SerialEncoder : public EncoderReader {
        SerialEncoder(int axis_id, SerialHAL* parent);
        ~SerialEncoder() override;
        
        bool initialize(const EncoderConfig& config) override;
        void shutdown() override;
        bool isInitialized() const override;
        
        EncoderReading read() const override;
        bool isDataValid() const override;
        double getUpdateRate() const override;
        
        bool calibrate(double reference_position_deg) override;
        bool autoCalibrate() override;
        double getCalibrationOffset() const override;
        void setCalibrationOffset(double offset_deg) override;
        bool saveCalibration() override;
        bool loadCalibration() override;
        
        EncoderType getType() const override;
        EncoderInterface getInterface() const override;
        uint32_t getResolution() const override;
        double getCountsPerDegree() const override;
        
        void setReadingCallback(ReadingCallback callback) override;
        void setErrorCallback(ErrorCallback callback) override;
        
        uint32_t getTotalReadings() const override;
        uint32_t getErrorCount() const override;
        double getUptime() const override;
        std::string getDiagnostics() const override;
        
        bool synchronize() override;
        bool isSynchronized() const override;
        
    private:
        int axis_id_;
        SerialHAL* parent_;
        EncoderConfig config_;
        std::atomic<bool> initialized_{false};
        std::atomic<double> calibration_offset_{0.0};
        std::atomic<double> actual_position_{0.0};
        mutable std::mt19937 rng_;
        std::chrono::steady_clock::time_point start_time_;
        mutable std::atomic<uint32_t> total_readings_{0};
        
        ReadingCallback reading_callback_;
        ErrorCallback error_callback_;
        
        mutable std::mutex mutex_;
        mutable std::atomic<uint32_t> error_count_{0};
        
        // Read encoder position via serial
        EncoderReading readEncoder() const;
    };

public:
    explicit SerialHAL(const HALConfig& config);
    ~SerialHAL() override;
    
    // HALInterface implementation
    bool initialize(const HALConfig& config) override;
    void shutdown() override;
    bool isInitialized() const override;
    
    std::unique_ptr<MotorControl> createMotorControl(int axis_id) override;
    std::unique_ptr<EncoderReader> createEncoderReader(int axis_id) override;
    std::unique_ptr<SafetyMonitor> createSafetyMonitor() override;
    std::unique_ptr<SensorInterface> createSensorInterface() override;
    
    std::string getPlatformName() const override;
    std::string getHardwareVersion() const override;
    std::vector<HALFeature> getSupportedFeatures() const override;
    bool supportsFeature(HALFeature feature) const override;
    
    bool start() override;
    bool stop() override;
    bool isRunning() const override;
    
    std::string getStatus() const override;
    std::string getErrorMessages() const override;
    void clearErrors() override;
    
    // Serial port access for inner classes
    bool isPortOpen() const { return fd_ >= 0; }
    bool writePort(const uint8_t* data, size_t len);
    bool readPort(uint8_t* data, size_t len, size_t& bytes_read, int timeout_ms);
    
private:
    HALConfig config_;
    std::atomic<bool> initialized_{false};
    std::atomic<bool> running_{false};
    mutable std::mutex mutex_;
    
    // Serial port
    int fd_{-1};
    std::string port_name_;
    uint32_t baud_rate_;
    std::string protocol_;
    uint8_t data_bits_;
    uint8_t stop_bits_;
    std::string parity_;
    uint32_t timeout_ms_;
    
    // Error tracking
    std::string last_error_;
    std::atomic<uint32_t> error_count_{0};
    
    // Serial port management
    bool openPort();
    void closePort();
    bool configurePort();
    
    // Modbus RTU helpers
    uint16_t calculateCRC(const uint8_t* data, size_t len) const;
    bool sendModbusRequest(uint8_t node_id, uint8_t function_code,
                           uint16_t register_addr, uint16_t value,
                           std::vector<uint8_t>& response);
    
    // Monitoring thread
    std::thread monitor_thread_;
    void monitorLoop();
    
    // Derotator support (optional, inlined for clarity)
    std::unique_ptr<MotorControl> createDerotatorMotor() override {
        int derotator_axis = 2;
        return std::make_unique<SerialMotor>(derotator_axis, this);
    }
    
    std::unique_ptr<EncoderReader> createDerotatorEncoder() override {
        int derotator_axis = 2;
        return std::make_unique<SerialEncoder>(derotator_axis, this);
    }
    
    bool configureDerotator(const struct DerotatorConfig& config) override {
        (void)config;
        return initialized_.load();
    }
};

} // namespace hal
} // namespace astro_mount
