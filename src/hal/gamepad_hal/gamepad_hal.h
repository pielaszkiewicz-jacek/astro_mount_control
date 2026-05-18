#pragma once
#include "hal/hal_interface.h"
#include "hal/hal_config.h"
#include "hal/gamepad_input.h"
#include "hal/motor_control.h"
#include "hal/encoder_reader.h"
#include "hal/safety_monitor.h"
#include "hal/sensor_interface.h"
#include <memory>
#include <thread>
#include <atomic>
#include <mutex>
#include <string>
#include <vector>
#include <map>

namespace astro_mount {
namespace hal {

// Forward declarations
class GamepadMotorControl;
class GamepadEncoderReader;
class GamepadSafetyMonitor;
class GamepadSensorInterface;

/**
 * @brief HAL implementation driven by gamepad / joystick input.
 *
 * GamepadHAL creates MotorControl instances that translate gamepad
 * joystick axes into velocity commands for the mount axes. This enables
 * manual, human-in-the-loop control of the mount using a physical
 * gamepad or joystick.
 *
 * Motor mapping:
 *   - Left stick X → axis 0 (RA/HA/Azimuth) velocity
 *   - Left stick Y → axis 1 (Dec/Altitude) velocity
 *
 * Encoder positions are synthesized from velocity integration, similar
 * to SimulatedHAL, so the rest of the system sees natural position
 * feedback while under manual control.
 */
class GamepadHAL : public HALInterface {
public:
    /**
     * @brief Construct GamepadHAL.
     * @param config        HAL configuration (must have type = GAMEPAD)
     * @param gamepad_input Optional external GamepadInput instance.
     *                      If null, an EvdevGamepadInput is created automatically.
     */
    explicit GamepadHAL(const HALConfig& config,
                        std::unique_ptr<GamepadInput> gamepad_input = nullptr);
    ~GamepadHAL() override;

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

    /// Access the underlying GamepadInput (for direct state queries).
    GamepadInput* getGamepadInput() const { return gamepad_input_.get(); }

    /// Configure manual control parameters.
    struct ManualControlConfig {
        double max_velocity_deg_s{5.0};         ///< Max slew speed at full joystick deflection
        double max_acceleration_deg_s2{2.0};    ///< Acceleration ramp rate
        double deadzone{0.15};                  ///< Joystick deadzone [0..1]
        double sensitivity{1.0};                ///< Sensitivity curve (1.0 = linear)
        bool invert_axis1{false};               ///< Invert left stick X
        bool invert_axis2{false};               ///< Invert left stick Y
        double update_rate_hz{50.0};            ///< Internal update rate

        // Speed presets (cycled by speed up/down buttons)
        std::vector<double> speed_presets_deg_s{0.5, 1.0, 2.0, 3.0, 5.0};
    };

    void setManualControlConfig(const ManualControlConfig& cfg);
    ManualControlConfig getManualControlConfig() const;

    /// Get current speed preset value in deg/s.
    double getCurrentSpeedPreset() const;

    /// Cycle speed preset up/down.
    void cycleSpeedUp();
    void cycleSpeedDown();

    /// Get mutable MotorControl for direct velocity injection.
    std::shared_ptr<GamepadMotorControl> getMotor(int axis_id);

private:
    HALConfig config_;
    ManualControlConfig manual_cfg_;
    std::atomic<bool> initialized_{false};
    std::atomic<bool> running_{false};

    // Gamepad input
    std::unique_ptr<GamepadInput> gamepad_input_;
    bool owns_gamepad_{false};

    // State
    mutable std::mutex state_mutex_;
    mutable std::mutex motor_mutex_;
    std::string error_messages_;

    // Speed preset index
    std::atomic<int> speed_preset_idx_{2};  // Default: middle preset

    // Component instances created via factory methods
    std::vector<std::shared_ptr<GamepadMotorControl>> motors_;
    std::vector<std::shared_ptr<GamepadEncoderReader>> encoders_;
    std::shared_ptr<GamepadSafetyMonitor> safety_monitor_;
    std::shared_ptr<GamepadSensorInterface> sensor_interface_;

    // Update thread
    std::thread update_thread_;
    void updateLoop();

    // Create default gamepad input
    std::unique_ptr<GamepadInput> createDefaultGamepadInput();
};

/**
 * @brief MotorControl driven by gamepad joystick input.
 *
 * The actual velocity is derived from the gamepad's left stick position
 * multiplied by the current speed preset. The motor integrates position
 * internally so encoder feedback works naturally.
 */
class GamepadMotorControl : public MotorControl {
public:
    GamepadMotorControl(int axis_id, GamepadHAL& parent);
    ~GamepadMotorControl() override;

    // MotorControl
    bool enable() override;
    bool disable() override;
    bool isEnabled() const override;

    bool setPosition(double position_deg, double velocity_deg_s, double acceleration_deg_s2) override;
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

    /// Called by GamepadHAL update loop with the latest computed velocity.
    void updateVelocity(double velocity_deg_s);

    /// Called by GamepadHAL update loop to integrate position.
    void integratePosition(double dt);

    /// Get/set the commanded (programmatic) velocity — overridden by gamepad.
    double getCommandedVelocity() const;
    void setCommandedVelocity(double vel);

private:
    int axis_id_;
    GamepadHAL& parent_;
    MotorConfig motor_config_;

    std::atomic<bool> enabled_{false};
    std::atomic<bool> emergency_stop_{false};
    std::atomic<bool> stop_requested_{false};

    mutable std::mutex motor_state_mutex_;
    double actual_position_{0.0};
    double actual_velocity_{0.0};
    double target_velocity_{0.0};
    double commanded_velocity_{0.0};  ///< Velocity set via setVelocity()
    double actual_torque_{0.0};
    double temperature_{25.0};
    double current_{0.0};
    double voltage_{12.0};
    uint32_t operation_time_{0};
    bool in_error_{false};
    std::string error_string_;

    PositionCallback position_cb_;
    ErrorCallback error_cb_;
    StateChangeCallback state_cb_;

    // Integration for position tracking
    double integrated_velocity_{0.0};  ///< Velocity from gamepad input
};

/**
 * @brief Encoder that returns the integrated position from GamepadMotorControl.
 */
class GamepadEncoderReader : public EncoderReader {
public:
    explicit GamepadEncoderReader(std::shared_ptr<GamepadMotorControl> motor);
    ~GamepadEncoderReader() override;

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
    std::shared_ptr<GamepadMotorControl> motor_;
    EncoderConfig config_;
    bool initialized_{false};
    double calibration_offset_{0.0};
    mutable uint32_t total_readings_{0};
    ReadingCallback reading_cb_;
    ErrorCallback error_cb_;
};

/**
 * @brief Minimal SafetyMonitor for gamepad-controlled mounts.
 */
class GamepadSafetyMonitor : public SafetyMonitor {
public:
    GamepadSafetyMonitor();
    ~GamepadSafetyMonitor() override;

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
    bool initialized_{false};
    SafetyConfig safety_config_;
    SafetyStatus status_;
    LimitCallback limit_cb_;
    ErrorCallback error_cb_;
};

/**
 * @brief Minimal SensorInterface (no real sensors in gamepad mode).
 */
class GamepadSensorInterface : public SensorInterface {
public:
    GamepadSensorInterface();
    ~GamepadSensorInterface() override;

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
    bool initialized_{false};
    SensorConfig config_;
    SensorConfig::SensorDefinition env_sensor_;
};

} // namespace hal
} // namespace astro_mount
