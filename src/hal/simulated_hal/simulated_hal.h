#pragma once
#include "hal/hal_interface.h"
#include "hal/motor_control.h"
#include "hal/encoder_reader.h"
#include "hal/hal_config.h"

#include <array>
#include <memory>
#include <mutex>
#include <thread>
#include <atomic>
#include <random>

namespace astro_mount {
namespace hal {

class SimulatedHAL : public HALInterface {
private:
    struct SimulatedMotor : public MotorControl {
        SimulatedMotor(int axis_id);
        ~SimulatedMotor() override;
        
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
        
        // Simulated motor thread
        void simulationThread();
        
    private:
        int axis_id_;
        MotorConfig config_;
        std::atomic<bool> enabled_{false};
        std::atomic<bool> moving_{false};
        std::atomic<double> target_position_{0.0};
        std::atomic<double> actual_position_{0.0};
        std::atomic<double> actual_velocity_{0.0};
        std::atomic<double> actual_torque_{0.0};
        std::atomic<bool> error_state_{false};
        std::string error_message_;
        
        PositionCallback position_callback_;
        ErrorCallback error_callback_;
        StateChangeCallback state_change_callback_;
        
        std::thread simulation_thread_;
        std::atomic<bool> running_{false};
        mutable std::mutex mutex_;
        mutable std::mt19937 rng_;
        mutable std::normal_distribution<double> position_noise_;
        mutable std::normal_distribution<double> velocity_noise_;
        std::chrono::steady_clock::time_point start_time_;
    };
    
    struct SimulatedEncoder : public EncoderReader {
        SimulatedEncoder(int axis_id);
        ~SimulatedEncoder() override;
        
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
        EncoderConfig config_;
        std::atomic<bool> initialized_{false};
        std::atomic<double> calibration_offset_{0.0};
        std::atomic<double> actual_position_{0.0};
        std::atomic<bool> reading_running_{false};
        mutable std::mt19937 rng_;
        mutable std::normal_distribution<double> position_noise_;
        std::chrono::steady_clock::time_point start_time_;
        mutable std::atomic<uint32_t> total_readings_{0};
        std::atomic<uint32_t> error_count_{0};
        
        ReadingCallback reading_callback_;
        ErrorCallback error_callback_;
        
        std::thread reading_thread_;
        mutable std::mutex mutex_;
        
        void readingThread();
    };

public:
    SimulatedHAL();
    explicit SimulatedHAL(const HALConfig& config);
    ~SimulatedHAL() override;
    
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
    
private:
    HALConfig config_;
    std::atomic<bool> initialized_{false};
    std::atomic<bool> running_{false};
    mutable std::mutex mutex_;
    
    // Simulated components
    std::array<std::unique_ptr<SimulatedMotor>, 3> motors_;
    std::array<std::unique_ptr<SimulatedEncoder>, 3> encoders_;
    
    // Derotator support
    std::unique_ptr<MotorControl> createDerotatorMotor() override {
        const int DEROTATOR_AXIS_ID = 2;
        if (DEROTATOR_AXIS_ID < motors_.size() && motors_[DEROTATOR_AXIS_ID]) {
            return std::make_unique<SimulatedMotor>(DEROTATOR_AXIS_ID);
        }
        return nullptr;
    }
    
    std::unique_ptr<EncoderReader> createDerotatorEncoder() override {
        const int DEROTATOR_AXIS_ID = 2;
        if (DEROTATOR_AXIS_ID < encoders_.size() && encoders_[DEROTATOR_AXIS_ID]) {
            return std::make_unique<SimulatedEncoder>(DEROTATOR_AXIS_ID);
        }
        return nullptr;
    }
    
    bool configureDerotator(const struct DerotatorConfig& config) override {
        (void)config;
        return true; // Simulated HAL accepts any config
    }
    
    // Helper methods
    void updateSimulation();

};

} // namespace hal
} // namespace astro_mount