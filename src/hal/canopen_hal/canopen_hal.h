#pragma once
#include "hal/hal_interface.h"
#include "hal/motor_control.h"
#include "hal/encoder_reader.h"
#include "hal/safety_monitor.h"
#include "hal/sensor_interface.h"
#include "hal/hal_config.h"
#include "controllers/icanopen_interface.h"
#include <array>
#include <memory>
#include <mutex>
#include <thread>
#include <atomic>
#include <queue>
#include <functional>

namespace astro_mount {
namespace hal {

// Prosty kontroler PID dla precyzyjnego sterowania
class PIDController {
public:
    PIDController(double kp = 1.5, double ki = 0.2, double kd = 0.05,
                  double integral_limit = 1000.0, double output_limit = 100.0);
    
    double calculate(double setpoint, double measured, double dt);
    void reset();
    
    void setParameters(double kp, double ki, double kd);
    std::tuple<double, double, double> getParameters() const;
    
private:
    double kp_, ki_, kd_;
    double integral_;
    double previous_error_;
    double integral_limit_;
    double output_limit_;
};

class CanOpenHAL : public HALInterface {
private:
    struct CanOpenMotor : public MotorControl {
        CanOpenMotor(int axis_id, controllers::ICanOpenInterface& canopen);
        ~CanOpenMotor() override;
        
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
        
        // CANopen-specific methods
        bool sendControlWord(uint16_t controlword);
        bool readStatusWord();
        bool configureCiA402();
        
        // Set the CANopen node ID for NMT commands (called by createMotorControl)
        void setCanNodeId(uint8_t id) { can_node_id_ = id; }
        
    private:
        int axis_id_;
        uint8_t can_node_id_;  // CANopen node ID (1-127), set by createMotorControl()
        controllers::ICanOpenInterface& canopen_;
        MotorConfig config_;
        std::atomic<bool> enabled_{false};
        std::atomic<bool> moving_{false};
        std::atomic<double> target_position_{0.0};
        std::atomic<double> actual_position_{0.0};
        std::atomic<double> actual_velocity_{0.0};
        std::atomic<double> actual_torque_{0.0};
        std::atomic<uint16_t> last_statusword_{0};
        std::atomic<bool> error_state_{false};
        std::string error_message_;
        
        PositionCallback position_callback_;
        ErrorCallback error_callback_;
        StateChangeCallback state_change_callback_;
        
        mutable std::mutex mutex_;
        
        // PID controller for position/velocity control
        PIDController pid_controller_;
        std::chrono::steady_clock::time_point start_time_;
        std::thread control_thread_;
        std::atomic<bool> control_running_{false};
        
        void controlLoop();
    };
    
    struct CanOpenEncoder : public EncoderReader {
        CanOpenEncoder(int axis_id, controllers::ICanOpenInterface& canopen);
        ~CanOpenEncoder() override;
        
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
        
        // CANopen-specific methods
        bool configurePDO(uint32_t cob_id, uint32_t mapping);
        bool enablePDO(bool enable);
        
    private:
        int axis_id_;
        controllers::ICanOpenInterface& canopen_;
        EncoderConfig config_;
        std::atomic<bool> initialized_{false};
        std::atomic<double> calibration_offset_{0.0};
        std::chrono::steady_clock::time_point start_time_;
        mutable std::atomic<uint32_t> total_readings_{0};
        mutable std::atomic<uint32_t> error_count_{0};
        
        ReadingCallback reading_callback_;
        ErrorCallback error_callback_;
        
        mutable std::mutex mutex_;
        std::thread pdo_thread_;
        std::atomic<bool> pdo_running_{false};
        
        // Cache ostatniego odczytu PDO dla read()
        mutable EncoderReading latest_reading_;
        mutable std::chrono::steady_clock::time_point last_pdo_time_;
        
        void pdoReceiveThread();
    };
    
    // Safety monitor for CANopen hardware
    class CanOpenSafetyMonitor : public SafetyMonitor {
    public:
        CanOpenSafetyMonitor(controllers::ICanOpenInterface& canopen);
        ~CanOpenSafetyMonitor() override;
        
        bool initialize(const SafetyConfig& config) override;
        void shutdown() override;
        bool isInitialized() const override;
        
        SafetyStatus getStatus() const override;
        bool checkLimits(int axis_id) override;
        bool emergencyStop(int axis_id) override;
        bool clearErrors(int axis_id) override;
        
        void setLimitCallback(LimitCallback callback) override;
        void setErrorCallback(ErrorCallback callback) override;
        
        std::string getDiagnostics() const override;
        
    private:
        controllers::ICanOpenInterface& canopen_;
        SafetyConfig config_;
        std::atomic<bool> initialized_{false};
        std::thread monitoring_thread_;
        std::atomic<bool> monitoring_running_{false};
        mutable std::mutex mutex_;
        
        LimitCallback limit_callback_;
        ErrorCallback error_callback_;
        
        void monitoringLoop();
    };
    
    // Sensor interface for CANopen hardware
    class CanOpenSensorInterface : public SensorInterface {
    public:
        CanOpenSensorInterface(controllers::ICanOpenInterface& canopen);
        ~CanOpenSensorInterface() override;
        
        bool initialize(const SensorConfig& config) override;
        void shutdown() override;
        bool isInitialized() const override;
        
        SensorReading read(int sensor_id) const override;
        std::vector<SensorReading> readAll() const override;
        
        bool calibrate(int sensor_id, double reference_value) override;
        bool autoCalibrate(int sensor_id) override;
        
        void setReadingCallback(ReadingCallback callback) override;
        void setErrorCallback(ErrorCallback callback) override;
        
        std::string getDiagnostics() const override;
        
    private:
        controllers::ICanOpenInterface& canopen_;
        SensorConfig config_;
        std::atomic<bool> initialized_{false};
        ReadingCallback reading_callback_;
        ErrorCallback error_callback_;
    };

public:
    CanOpenHAL(std::unique_ptr<controllers::ICanOpenInterface> canopen_interface);
    ~CanOpenHAL() override;
    
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
    
    // Derotator support
    std::unique_ptr<MotorControl> createDerotatorMotor() override;
    std::unique_ptr<EncoderReader> createDerotatorEncoder() override;
    bool configureDerotator(const struct DerotatorConfig& config) override;
    
    // CANopen-specific methods
    bool sendNMT(uint8_t node_id, uint8_t command);
    bool configureNetwork();
    bool checkConnection();
    
private:

    std::unique_ptr<controllers::ICanOpenInterface> canopen_interface_;
    HALConfig config_;
    std::atomic<bool> initialized_{false};
    std::atomic<bool> running_{false};
    mutable std::mutex mutex_;
    
    // CANopen network management
    std::thread nmt_thread_;
    std::atomic<bool> nmt_running_{false};
    
    void nmtMonitoringThread();
    
    // Component factories
    std::array<std::unique_ptr<CanOpenMotor>, 3> motors_;
    std::array<std::unique_ptr<CanOpenEncoder>, 3> encoders_;
    std::unique_ptr<CanOpenSafetyMonitor> safety_monitor_;
    std::unique_ptr<CanOpenSensorInterface> sensor_interface_;
};

} // namespace hal
} // namespace astro_mount