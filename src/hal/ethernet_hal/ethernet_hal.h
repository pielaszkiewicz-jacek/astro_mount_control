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

class EthernetHAL : public HALInterface {
private:
    struct EthernetMotor : public MotorControl {
        EthernetMotor(int axis_id, EthernetHAL* parent);
        ~EthernetMotor() override;
        
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
        EthernetHAL* parent_;
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
        
        // Send Modbus TCP command via socket
        bool sendCommand(const std::vector<uint8_t>& request, std::vector<uint8_t>& response);
    };
    
    struct EthernetEncoder : public EncoderReader {
        EthernetEncoder(int axis_id, EthernetHAL* parent);
        ~EthernetEncoder() override;
        
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
        EthernetHAL* parent_;
        EncoderConfig config_;
        std::atomic<bool> initialized_{false};
        std::atomic<double> calibration_offset_{0.0};
        mutable std::atomic<double> actual_position_{0.0};
        mutable std::mt19937 rng_;
        std::chrono::steady_clock::time_point start_time_;
        mutable std::atomic<uint32_t> total_readings_{0};
        mutable std::atomic<uint32_t> error_count_{0};
        
        ReadingCallback reading_callback_;
        ErrorCallback error_callback_;
        
        mutable std::mutex mutex_;
        
        // Read encoder position via TCP
        EncoderReading readEncoder() const;
    };

public:
    explicit EthernetHAL(const HALConfig& config);
    ~EthernetHAL() override;
    
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
    
    // Socket access for inner classes
    bool isSocketConnected() const { return sock_fd_ >= 0; }
    bool writeSocket(const uint8_t* data, size_t len);
    bool readSocket(uint8_t* data, size_t len, size_t& bytes_read, int timeout_ms);

private:
    HALConfig config_;
    std::atomic<bool> initialized_{false};
    std::atomic<bool> running_{false};
    mutable std::mutex mutex_;
    
    // TCP socket
    int sock_fd_{-1};
    std::string ip_address_;
    uint16_t port_;
    std::string protocol_;
    std::atomic<uint32_t> timeout_ms_{1000};
    uint32_t retry_count_;
    
    // Error tracking
    std::string last_error_;
    std::atomic<uint32_t> error_count_{0};
    
    // Socket management
    bool connectSocket();
    void disconnectSocket();
    bool reconnectSocket();
    
    // Modbus TCP helpers
    uint16_t transaction_id_{0};
    bool sendModbusTCPRequest(uint8_t unit_id, uint8_t function_code,
                              uint16_t register_addr, uint16_t value,
                              std::vector<uint8_t>& response);
    
    // Monitoring thread
    std::thread monitor_thread_;
    void monitorLoop();
    
    // Derotator support
    std::unique_ptr<MotorControl> createDerotatorMotor() override {
        int derotator_axis = 2;
        return std::make_unique<EthernetMotor>(derotator_axis, this);
    }
    
    std::unique_ptr<EncoderReader> createDerotatorEncoder() override {
        int derotator_axis = 2;
        return std::make_unique<EthernetEncoder>(derotator_axis, this);
    }
    
    bool configureDerotator(const struct DerotatorConfig& config) override {
        (void)config;
        return initialized_.load();
    }
};

} // namespace hal
} // namespace astro_mount
