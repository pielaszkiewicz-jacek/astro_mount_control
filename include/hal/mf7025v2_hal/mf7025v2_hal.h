#pragma once
// MF7025v2 HAL — LingKong BLDC servo via proprietary CAN V2.36 (SocketCAN)
// Linux only.

#include "hal/hal_interface.h"
#include "hal/motor_control.h"
#include "hal/encoder_reader.h"
#include "hal/hal_config.h"
#include "controllers/imf7025v2_interface.h"

#include <memory>
#include <mutex>
#include <thread>
#include <atomic>
#include <array>

namespace astro_mount {
namespace hal {

class Mf7025v2Hal : public HALInterface {
private:
    // ── Internal motor implementation ──────────────────────────────────────
    struct Mf7025v2Motor : public MotorControl {
        Mf7025v2Motor(int axis_id, uint8_t can_node_id, Mf7025v2Hal* parent);
        ~Mf7025v2Motor() override;

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

        bool configure(const MotorConfig& config) override;
        MotorConfig getConfiguration() const override;

        void setPositionCallback(PositionCallback callback) override;
        void setErrorCallback(ErrorCallback callback) override;
        void setStateChangeCallback(StateChangeCallback callback) override;

        double getTemperature() const override;
        double getCurrent() const override;
        double getVoltage() const override;
        uint32_t getOperationTime() const override;

        // Called by Mf7025v2Hal monitor thread
        void updateStatus(const controllers::Mf7025v2Status& st);
        // Called by monitor thread to sync absolute position from drive (0x92)
        void updateAbsolutePosition(double abs_pos_deg);

    private:
        int axis_id_;
        uint8_t can_node_id_;
        Mf7025v2Hal* parent_;
        MotorConfig config_;

        std::atomic<bool> enabled_{false};
        std::atomic<bool> moving_{false};
        std::atomic<double> target_position_{0.0};
        std::atomic<double> actual_position_{0.0};
        std::atomic<double> actual_velocity_{0.0};
        std::atomic<double> actual_torque_{0.0};
        std::atomic<double> temperature_{25.0};
        std::atomic<double> current_{0.0};
        std::atomic<double> voltage_{24.0};
        std::atomic<bool> error_state_{false};
        std::string error_message_;
        mutable std::mutex mutex_;

        PositionCallback position_callback_;
        ErrorCallback error_callback_;
        StateChangeCallback state_change_callback_;

        std::chrono::steady_clock::time_point start_time_;
        uint32_t can_failures_{0};
        static constexpr uint32_t CAN_FAILURE_THRESHOLD = 5;
        static constexpr double SPEED_HYSTERESIS = 0.01; // dps
    };

    // ── Internal encoder implementation ────────────────────────────────────
    struct Mf7025v2Encoder : public EncoderReader {
        Mf7025v2Encoder(int axis_id, uint8_t can_node_id, Mf7025v2Hal* parent);
        ~Mf7025v2Encoder() override;

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

        // Called by parent to push status data
        void updateFromStatus(uint16_t encoder_raw, bool valid);
        // Called by parent to set encoder position directly from 0x94 single-turn angle
        void updatePositionDeg(double pos_deg, bool valid);

    private:
        int axis_id_;
        uint8_t can_node_id_;
        Mf7025v2Hal* parent_;
        EncoderConfig config_;
        std::atomic<bool> initialized_{false};

        mutable std::mutex mutex_;
        mutable std::atomic<double> actual_position_{0.0};
        mutable std::atomic<bool> data_valid_{false};
        mutable std::atomic<uint32_t> total_readings_{0};
        mutable std::atomic<uint32_t> error_count_{0};
        std::atomic<double> calibration_offset_{0.0};
        std::chrono::steady_clock::time_point start_time_;

        ReadingCallback reading_callback_;
        ErrorCallback error_callback_;
    };

public:
    explicit Mf7025v2Hal(std::unique_ptr<controllers::IMf7025v2Interface> can_iface);
    ~Mf7025v2Hal() override;

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

    // Access to CAN interface for motor/encoder inner classes
    controllers::IMf7025v2Interface* getCanInterface() { return can_iface_.get(); }

private:
    HALConfig config_;
    std::atomic<bool> initialized_{false};
    std::atomic<bool> running_{false};
    mutable std::mutex mutex_;

    std::unique_ptr<controllers::IMf7025v2Interface> can_iface_;
    std::string last_error_;

    // Pre-created motor/encoder instances (one per axis, up to 2)
    std::array<std::unique_ptr<Mf7025v2Motor>, 2> motors_;
    std::array<std::unique_ptr<Mf7025v2Encoder>, 2> encoders_;

    // Status polling thread
    std::thread monitor_thread_;
    void monitorLoop();
};

// ── Thin proxy wrappers for createMotorControl / createEncoderReader ──────
// (same pattern as SimulatedHAL::MotorProxy)

class Mf7025v2MotorProxy : public MotorControl {
public:
    explicit Mf7025v2MotorProxy(MotorControl* target) : target_(target) {}
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

class Mf7025v2EncoderProxy : public EncoderReader {
public:
    explicit Mf7025v2EncoderProxy(EncoderReader* target) : target_(target) {}
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
