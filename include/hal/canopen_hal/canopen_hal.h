#pragma once
// CANopen/CiA 402 HAL — NiMotion STM42/STM42M over SocketCAN (Linux only).

#include "hal/hal_interface.h"
#include "hal/motor_control.h"
#include "hal/encoder_reader.h"
#include "hal/safety_monitor.h"
#include "hal/hal_config.h"
#include "controllers/icanopen_interface.h"

#include <array>
#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

namespace astro_mount {
namespace hal {

class CanOpenHAL : public HALInterface {
private:
    // ── Internal motor implementation ──────────────────────────────────────
    struct CanOpenMotor : public MotorControl {
        CanOpenMotor(int axis_id, uint8_t can_node_id, CanOpenHAL* parent);
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
        bool clearErrors() override;
        bool zeroPosition() override;
        bool home() override;
        bool writePidLoopRam(int loop, double kp, double ki, double kd) override;

        bool configure(const MotorConfig& config) override;
        MotorConfig getConfiguration() const override;

        void setPositionCallback(PositionCallback callback) override;
        void setErrorCallback(ErrorCallback callback) override;
        void setStateChangeCallback(StateChangeCallback callback) override;

        double getTemperature() const override;
        double getCurrent() const override;
        double getVoltage() const override;
        uint32_t getOperationTime() const override;

        // Called by CanOpenHAL monitor thread.
        void updateStatus(const controllers::CanOpenStatus& st);

    private:
        int axis_id_;
        uint8_t can_node_id_;
        CanOpenHAL* parent_;
        MotorConfig config_;
        std::atomic<bool> enabled_{false};
        std::atomic<bool> moving_{false};
        std::atomic<bool> target_reached_{false};
        std::atomic<bool> error_state_{false};
        std::atomic<double> actual_position_{0.0};
        std::atomic<double> actual_velocity_{0.0};
        std::atomic<double> actual_torque_{0.0};
        std::atomic<double> temperature_{25.0};
        std::atomic<double> current_{0.0};
        std::atomic<double> voltage_{24.0};
        std::string error_message_;
        mutable std::mutex mutex_;

        PositionCallback position_callback_;
        ErrorCallback error_callback_;
        StateChangeCallback state_change_callback_;
        std::chrono::steady_clock::time_point start_time_;
    };

    // ── Internal encoder implementation ────────────────────────────────────
    struct CanOpenEncoder : public EncoderReader {
        CanOpenEncoder(int axis_id, uint8_t can_node_id, CanOpenHAL* parent);
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

    private:
        int axis_id_;
        uint8_t can_node_id_;
        CanOpenHAL* parent_;
        EncoderConfig config_;
        std::atomic<bool> initialized_{false};
        std::atomic<double> calibration_offset_{0.0};
        mutable std::atomic<uint32_t> total_readings_{0};
        mutable std::atomic<uint32_t> error_count_{0};
        std::chrono::steady_clock::time_point start_time_;
        ReadingCallback reading_callback_;
        ErrorCallback error_callback_;
    };

    // ── Internal safety monitor ────────────────────────────────────────────
    struct CanOpenSafetyMonitor : public SafetyMonitor {
        CanOpenSafetyMonitor(CanOpenHAL* parent);
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
        CanOpenHAL* parent_;
        SafetyConfig config_;
        std::atomic<bool> initialized_{false};
        LimitCallback limit_callback_;
        ErrorCallback error_callback_;
    };

public:
    explicit CanOpenHAL(std::unique_ptr<controllers::ICanOpenInterface> can_iface);
    ~CanOpenHAL() override;

    // HALInterface
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

    controllers::ICanOpenInterface* getCanInterface() { return can_iface_.get(); }
    controllers::CanOpenStatus lastStatus(int axis_id) const;

private:
    HALConfig config_;
    std::atomic<bool> initialized_{false};
    std::atomic<bool> running_{false};
    mutable std::mutex mutex_;
    std::string last_error_;

    std::unique_ptr<controllers::ICanOpenInterface> can_iface_;
    std::array<std::unique_ptr<CanOpenMotor>, 2> motors_;
    std::array<std::unique_ptr<CanOpenEncoder>, 2> encoders_;
    std::array<controllers::CanOpenStatus, 2> last_status_;
    mutable std::mutex status_mutex_;

    std::thread monitor_thread_;
    void monitorLoop();
};

// ── Proxy wrappers (same pattern as SimulatedHAL / Mf7025v2Hal) ───────────

class CanOpenMotorProxy : public MotorControl {
public:
    explicit CanOpenMotorProxy(MotorControl* target) : target_(target) {}
    bool enable() override { return target_->enable(); }
    bool disable() override { return target_->disable(); }
    bool isEnabled() const override { return target_->isEnabled(); }
    bool setPosition(double p, double v, double a) override { return target_->setPosition(p, v, a); }
    bool setVelocity(double v, double a) override { return target_->setVelocity(v, a); }
    bool setTorque(double t) override { return target_->setTorque(t); }
    bool stop() override { return target_->stop(); }
    bool emergencyStop() override { return target_->emergencyStop(); }
    double getActualPosition() const override { return target_->getActualPosition(); }
    double getActualVelocity() const override { return target_->getActualVelocity(); }
    double getActualTorque() const override { return target_->getActualTorque(); }
    bool isMoving() const override { return target_->isMoving(); }
    bool targetReached() const override { return target_->targetReached(); }
    bool inErrorState() const override { return target_->inErrorState(); }
    std::string getErrorString() const override { return target_->getErrorString(); }
    bool clearErrors() override { return target_->clearErrors(); }
    bool zeroPosition() override { return target_->zeroPosition(); }
    bool home() override { return target_->home(); }
    bool writePidLoopRam(int loop, double kp, double ki, double kd) override {
        return target_->writePidLoopRam(loop, kp, ki, kd);
    }
    bool configure(const MotorConfig& c) override { return target_->configure(c); }
    MotorConfig getConfiguration() const override { return target_->getConfiguration(); }
    void setPositionCallback(PositionCallback cb) override { target_->setPositionCallback(cb); }
    void setErrorCallback(ErrorCallback cb) override { target_->setErrorCallback(cb); }
    void setStateChangeCallback(StateChangeCallback cb) override { target_->setStateChangeCallback(cb); }
    double getTemperature() const override { return target_->getTemperature(); }
    double getCurrent() const override { return target_->getCurrent(); }
    double getVoltage() const override { return target_->getVoltage(); }
    uint32_t getOperationTime() const override { return target_->getOperationTime(); }
private:
    MotorControl* target_;
};

class CanOpenEncoderProxy : public EncoderReader {
public:
    explicit CanOpenEncoderProxy(EncoderReader* target) : target_(target) {}
    bool initialize(const EncoderConfig& c) override { return target_->initialize(c); }
    void shutdown() override { target_->shutdown(); }
    bool isInitialized() const override { return target_->isInitialized(); }
    EncoderReading read() const override { return target_->read(); }
    bool isDataValid() const override { return target_->isDataValid(); }
    double getUpdateRate() const override { return target_->getUpdateRate(); }
    bool calibrate(double r) override { return target_->calibrate(r); }
    bool autoCalibrate() override { return target_->autoCalibrate(); }
    double getCalibrationOffset() const override { return target_->getCalibrationOffset(); }
    void setCalibrationOffset(double o) override { target_->setCalibrationOffset(o); }
    bool saveCalibration() override { return target_->saveCalibration(); }
    bool loadCalibration() override { return target_->loadCalibration(); }
    EncoderType getType() const override { return target_->getType(); }
    EncoderInterface getInterface() const override { return target_->getInterface(); }
    uint32_t getResolution() const override { return target_->getResolution(); }
    double getCountsPerDegree() const override { return target_->getCountsPerDegree(); }
    void setReadingCallback(ReadingCallback cb) override { target_->setReadingCallback(cb); }
    void setErrorCallback(ErrorCallback cb) override { target_->setErrorCallback(cb); }
    uint32_t getTotalReadings() const override { return target_->getTotalReadings(); }
    uint32_t getErrorCount() const override { return target_->getErrorCount(); }
    double getUptime() const override { return target_->getUptime(); }
    std::string getDiagnostics() const override { return target_->getDiagnostics(); }
    bool synchronize() override { return target_->synchronize(); }
    bool isSynchronized() const override { return target_->isSynchronized(); }
private:
    EncoderReader* target_;
};

} // namespace hal
} // namespace astro_mount
