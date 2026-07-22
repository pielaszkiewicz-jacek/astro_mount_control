#pragma once
#include "hal/hal_interface.h"
#include "hal/motor_control.h"
#include "hal/encoder_reader.h"
#include "hal/safety_monitor.h"
#include "hal/sensor_interface.h"
#include "hal/hal_config.h"
#include "controllers/imf7025v2_interface.h"
#include <memory>
#include <mutex>
#include <thread>
#include <atomic>

namespace astro_mount {
namespace hal {

/**
 * @brief HAL implementation for LingKong MF7025v2 BLDC servo drives.
 *
 * Uses the proprietary LingKong CAN protocol (V2.36) instead of CANopen/CiA 402.
 * CAN ID = 0x140 + node_id, standard frames, DLC=8, request-response.
 *
 * Supported commands:
 *   - Multi-turn position: 0xA3 (basic), 0xA4 (with speed limit)
 *   - Speed control: 0xA2 with torque limit
 *   - Torque control: 0xA1
 *   - Status: 0x9A (temp/voltage/current/errors), 0x9C (iq/speed/encoder)
 *   - Encoder: 0x92 (multi-turn angle), 0x90 (encoder data)
 *   - Config: 0xC0/C1 (control parameters)
 */
class Mf7025v2Hal : public HALInterface {
private:
    // ─── MotorControl implementation for MF7025v2 ────────────────
    class MfMotor : public MotorControl {
    public:
        MfMotor(uint8_t node_id, controllers::IMf7025v2Interface& can,
                const MotorConfig& config);
        ~MfMotor() override;

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
        uint8_t node_id_;
        controllers::IMf7025v2Interface& can_;
        MotorConfig config_;
        std::atomic<bool> enabled_{false};
        std::atomic<bool> moving_{false};
        std::atomic<double> actual_position_{0.0};
        std::atomic<double> actual_velocity_{0.0};
        std::atomic<double> actual_torque_{0.0};
        std::atomic<double> target_position_{0.0};  // Last commanded target
        std::atomic<bool> error_state_{false};
        std::string error_message_;
        PositionCallback position_callback_;
        ErrorCallback error_callback_;
        StateChangeCallback state_change_callback_;
        mutable std::mutex mutex_;
        std::thread poll_thread_;
        std::atomic<bool> poll_running_{false};
        std::chrono::steady_clock::time_point start_time_;
        int poll_interval_ms_{50}; // 20Hz status polling
        std::atomic<int> can_failures_{0};  // Consecutive CAN failures for dead-node detection
        static constexpr int CAN_FAILURE_THRESHOLD = 5;  // Trigger emergencyStop after N failures

        void pollLoop();
        void updateStatus();
        std::string decodeErrorFlags(uint8_t error_state) const;
    };

    // ─── EncoderReader implementation for MF7025v2 ──────────────
    class MfEncoder : public EncoderReader {
    public:
        MfEncoder(uint8_t node_id, controllers::IMf7025v2Interface& can,
                  const EncoderConfig& config);
        ~MfEncoder() override;

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
        uint8_t node_id_;
        controllers::IMf7025v2Interface& can_;
        EncoderConfig config_;
        std::atomic<bool> initialized_{false};
        std::atomic<double> calibration_offset_{0.0};
        ReadingCallback reading_callback_;
        ErrorCallback error_callback_;
        mutable std::mutex mutex_;
        std::chrono::steady_clock::time_point start_time_;
        mutable std::atomic<uint32_t> total_readings_{0};
        mutable std::atomic<uint32_t> error_count_{0};
    };

    // ─── SafetyMonitor implementation for MF7025v2 ──────────────
    class MfSafetyMonitor : public SafetyMonitor {
    public:
        MfSafetyMonitor(controllers::IMf7025v2Interface& can,
                        std::vector<uint8_t> monitored_nodes);
        ~MfSafetyMonitor() override;

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
        controllers::IMf7025v2Interface& can_;
        SafetyConfig config_;
        std::atomic<bool> initialized_{false};
        std::thread monitor_thread_;
        std::atomic<bool> monitor_running_{false};
        mutable std::mutex mutex_;
        LimitCallback limit_callback_;
        ErrorCallback error_callback_;
        std::vector<uint8_t> monitored_nodes_;

        void monitorLoop();
    };

    // ─── SensorInterface implementation for MF7025v2 ────────────
    class MfSensorInterface : public SensorInterface {
    public:
        MfSensorInterface(controllers::IMf7025v2Interface& can);
        ~MfSensorInterface() override;

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
        controllers::IMf7025v2Interface& can_;
        SensorConfig config_;
        std::atomic<bool> initialized_{false};
        ReadingCallback reading_callback_;
        ErrorCallback error_callback_;
    };

public:
    /// Create from an externally-provided CAN interface
    Mf7025v2Hal(std::unique_ptr<controllers::IMf7025v2Interface> can_interface);
    ~Mf7025v2Hal() override;

    /// Factory method: creates the CAN interface internally from HALConfig
    static std::unique_ptr<Mf7025v2Hal> create(const HALConfig& config);

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
    std::unique_ptr<controllers::IMf7025v2Interface> can_interface_;
    HALConfig config_;
    std::atomic<bool> initialized_{false};
    std::atomic<bool> running_{false};
    mutable std::mutex mutex_;

    // Cached error messages for getErrorMessages()
    std::string last_errors_;
};

} // namespace hal
} // namespace astro_mount
