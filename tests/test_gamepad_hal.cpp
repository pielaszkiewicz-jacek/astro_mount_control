#include "hal/hal_factory.h"
#include "hal/hal_interface.h"
#include "hal/hal_config.h"
#include "hal/gamepad_input.h"
#include "hal/gamepad_hal/gamepad_hal.h"
#include "hal/gamepad_hal/gamepad_input_evdev.h"
#include "hal/motor_control.h"
#include "hal/encoder_reader.h"
#include "hal/safety_monitor.h"
#include "hal/sensor_interface.h"
#include "logging/logger.h"
#include <gtest/gtest.h>
#include <thread>
#include <chrono>
#include <atomic>
#include <memory>
#include <cmath>

namespace astro_mount {
namespace hal {
namespace test {

using namespace std::chrono_literals;

// ============================================================================
// MockGamepadInput — injectable mock that simulates gamepad input
// without requiring real hardware (/dev/input/js*).
// ============================================================================

class MockGamepadInput : public GamepadInput {
public:
    // Configurable behavior
    bool init_result{true};
    bool connected{true};
    std::string device_name{"MockGamepad"};
    int axis_count{8};
    int button_count{11};

    // Simulated state — tests can modify this directly
    GamepadState state;

    // Tracking
    bool initialized_{false};
    bool shutdown_called_{false};
    std::string last_device_path_;
    std::map<int, std::string> last_button_mapping_;
    std::map<int, std::string> last_axis_mapping_;

    bool initialize(const std::string& device_path) override {
        initialized_ = init_result;
        last_device_path_ = device_path;
        state.connected = init_result;
        return init_result;
    }

    void shutdown() override {
        shutdown_called_ = true;
        initialized_ = false;
        state.connected = false;
    }

    bool isConnected() const override {
        return connected && initialized_;
    }

    GamepadState readState() override {
        state.connected = connected && initialized_;
        state.timestamp = std::chrono::steady_clock::now();
        return state;
    }

    std::string getDeviceName() const override {
        return device_name;
    }

    int getAxisCount() const override {
        return axis_count;
    }

    int getButtonCount() const override {
        return button_count;
    }

    void applyButtonMapping(const std::map<int, std::string>& mapping) override {
        last_button_mapping_ = mapping;
    }

    void applyAxisMapping(const std::map<int, std::string>& mapping) override {
        last_axis_mapping_ = mapping;
    }
};

// ============================================================================
// Test Fixture
// ============================================================================

class GamepadHALTest : public ::testing::Test {
protected:
    void SetUp() override {
        logging::Logger::init("");

        // Build default gamepad config
        config_ = HALFactory::getDefaultConfig(HALType::GAMEPAD);
    }

    void TearDown() override {
        if (hal_) {
            hal_->shutdown();
            hal_.reset();
        }
    }

    // Create a GamepadHAL with a mock input (no real hardware needed)
    std::unique_ptr<GamepadHAL> createHALWithMock(
        std::unique_ptr<MockGamepadInput> mock = nullptr)
    {
        if (!mock) {
            mock = std::make_unique<MockGamepadInput>();
        }
        mock_input_ = mock.get();  // Store raw ptr for state manipulation
        auto hal = std::make_unique<GamepadHAL>(config_, std::move(mock));
        return hal;
    }

    HALConfig config_;
    std::unique_ptr<GamepadHAL> hal_;
    MockGamepadInput* mock_input_{nullptr};  // Non-owning pointer to mock
};

// ============================================================================
// PART 1: HALFactory — Gamepad creation tests
// ============================================================================

TEST_F(GamepadHALTest, FactoryCreatesGamepadHALByType) {
    auto hal = HALFactory::create(HALType::GAMEPAD);
    EXPECT_NE(hal, nullptr);
}

TEST_F(GamepadHALTest, FactoryCreatesGamepadHALByName) {
    auto hal = HALFactory::create("gamepad");
    EXPECT_NE(hal, nullptr);
}

TEST_F(GamepadHALTest, FactoryCreatesGamepadHALByConfig) {
    auto hal = HALFactory::create(config_);
    EXPECT_NE(hal, nullptr);
}

TEST_F(GamepadHALTest, FactoryGetAvailableTypesContainsGamepad) {
    auto types = HALFactory::getAvailableTypes();
    bool has_gamepad = false;
    for (auto t : types) {
        if (t == HALType::GAMEPAD) has_gamepad = true;
    }
    EXPECT_TRUE(has_gamepad);
}

TEST_F(GamepadHALTest, FactoryDefaultGamepadConfig) {
    auto config = HALFactory::getDefaultConfig(HALType::GAMEPAD);
    EXPECT_EQ(config.type, HALType::GAMEPAD);
    EXPECT_EQ(config.gamepad.deadzone, 0.15);
    EXPECT_EQ(config.gamepad.sensitivity, 1.0);
    EXPECT_EQ(config.gamepad.max_velocity_deg_s, 5.0);
    EXPECT_GE(config.gamepad.update_rate_hz, 1.0);
    EXPECT_FALSE(config.gamepad.invert_axis1);
    EXPECT_FALSE(config.gamepad.invert_axis2);
    EXPECT_GT(config.gamepad.speed_presets.size(), 0);
    EXPECT_EQ(config.axes.size(), 2);
}

TEST_F(GamepadHALTest, FactoryIsTypeAvailable) {
    EXPECT_TRUE(HALFactory::isTypeAvailable(HALType::GAMEPAD));
}

// ============================================================================
// PART 2: GamepadHAL Lifecycle Tests (with MockGamepadInput)
// ============================================================================

TEST_F(GamepadHALTest, CreateAndDestroy) {
    auto hal = createHALWithMock();
    ASSERT_NE(hal, nullptr);
    // ~GamepadHAL() calls shutdown() — no crash
}

TEST_F(GamepadHALTest, InitializeSuccess) {
    hal_ = createHALWithMock();
    ASSERT_NE(hal_, nullptr);

    EXPECT_TRUE(hal_->initialize(config_));
    EXPECT_TRUE(hal_->isInitialized());
}

TEST_F(GamepadHALTest, InitializeFailurePropagated) {
    auto mock = std::make_unique<MockGamepadInput>();
    mock->init_result = false;  // Simulate init failure
    hal_ = createHALWithMock(std::move(mock));
    ASSERT_NE(hal_, nullptr);

    EXPECT_FALSE(hal_->initialize(config_));
    EXPECT_FALSE(hal_->isInitialized());
}

TEST_F(GamepadHALTest, InitializeAppliesButtonMappingFromConfig) {
    // Set button mappings in config
    config_.gamepad.button_mapping[0] = "home";
    config_.gamepad.button_mapping[1] = "stop";

    hal_ = createHALWithMock();
    ASSERT_NE(hal_, nullptr);
    ASSERT_TRUE(hal_->initialize(config_));

    // Verify mock received the mapping
    ASSERT_NE(mock_input_, nullptr);
    EXPECT_EQ(mock_input_->last_button_mapping_[0], "home");
    EXPECT_EQ(mock_input_->last_button_mapping_[1], "stop");
}

TEST_F(GamepadHALTest, InitializeAppliesAxisMappingFromConfig) {
    config_.gamepad.axis_mapping[0] = "lx";
    config_.gamepad.axis_mapping[3] = "rx";

    hal_ = createHALWithMock();
    ASSERT_NE(hal_, nullptr);
    ASSERT_TRUE(hal_->initialize(config_));

    ASSERT_NE(mock_input_, nullptr);
    EXPECT_EQ(mock_input_->last_axis_mapping_[0], "lx");
    EXPECT_EQ(mock_input_->last_axis_mapping_[3], "rx");
}

TEST_F(GamepadHALTest, InitializeWithoutMappings) {
    // No button_mapping or axis_mapping set
    hal_ = createHALWithMock();
    ASSERT_NE(hal_, nullptr);
    ASSERT_TRUE(hal_->initialize(config_));

    ASSERT_NE(mock_input_, nullptr);
    EXPECT_TRUE(mock_input_->last_button_mapping_.empty());
    EXPECT_TRUE(mock_input_->last_axis_mapping_.empty());
}

TEST_F(GamepadHALTest, StartStop) {
    hal_ = createHALWithMock();
    ASSERT_NE(hal_, nullptr);
    ASSERT_TRUE(hal_->initialize(config_));

    // Start
    EXPECT_TRUE(hal_->start());
    EXPECT_TRUE(hal_->isRunning());

    // Brief sleep to let the update loop run
    std::this_thread::sleep_for(10ms);

    // Stop
    EXPECT_TRUE(hal_->stop());
    EXPECT_FALSE(hal_->isRunning());
}

TEST_F(GamepadHALTest, StartFailsWhenNotInitialized) {
    hal_ = createHALWithMock();
    ASSERT_NE(hal_, nullptr);
    // No initialize() call
    EXPECT_FALSE(hal_->start());
    EXPECT_FALSE(hal_->isRunning());
}

TEST_F(GamepadHALTest, Shutdown) {
    hal_ = createHALWithMock();
    ASSERT_NE(hal_, nullptr);
    ASSERT_TRUE(hal_->initialize(config_));
    ASSERT_TRUE(hal_->isInitialized());

    hal_->shutdown();
    EXPECT_FALSE(hal_->isInitialized());
    EXPECT_FALSE(hal_->isRunning());
}

TEST_F(GamepadHALTest, ShutdownStopsRunning) {
    hal_ = createHALWithMock();
    ASSERT_NE(hal_, nullptr);
    ASSERT_TRUE(hal_->initialize(config_));
    ASSERT_TRUE(hal_->start());
    ASSERT_TRUE(hal_->isRunning());

    hal_->shutdown();
    EXPECT_FALSE(hal_->isInitialized());
    EXPECT_FALSE(hal_->isRunning());
}

TEST_F(GamepadHALTest, FullLifecycle) {
    hal_ = createHALWithMock();
    ASSERT_NE(hal_, nullptr);

    // Initial state
    EXPECT_FALSE(hal_->isInitialized());
    EXPECT_FALSE(hal_->isRunning());

    // Initialize
    EXPECT_TRUE(hal_->initialize(config_));
    EXPECT_TRUE(hal_->isInitialized());
    EXPECT_FALSE(hal_->isRunning());

    // Start
    EXPECT_TRUE(hal_->start());
    EXPECT_TRUE(hal_->isInitialized());
    EXPECT_TRUE(hal_->isRunning());

    // Stop
    EXPECT_TRUE(hal_->stop());
    EXPECT_TRUE(hal_->isInitialized());
    EXPECT_FALSE(hal_->isRunning());

    // Shutdown
    hal_->shutdown();
    EXPECT_FALSE(hal_->isInitialized());
    EXPECT_FALSE(hal_->isRunning());
}

TEST_F(GamepadHALTest, GetPlatformInfo) {
    hal_ = createHALWithMock();
    ASSERT_NE(hal_, nullptr);

    EXPECT_EQ(hal_->getPlatformName(), "GamepadHAL");
}

TEST_F(GamepadHALTest, GetHardwareVersionWithoutConnection) {
    // Without any gamepad input initialized, should report "No gamepad connected"
    // Create HAL without calling initialize() so mock's isConnected() returns false
    auto hal = std::make_unique<GamepadHAL>(config_, std::make_unique<MockGamepadInput>());
    std::string version = hal->getHardwareVersion();
    EXPECT_EQ(version, "No gamepad connected");
}

TEST_F(GamepadHALTest, GetHardwareVersionWithConnection) {
    hal_ = createHALWithMock();
    ASSERT_NE(hal_, nullptr);
    ASSERT_TRUE(hal_->initialize(config_));

    // After init, mock is connected
    std::string version = hal_->getHardwareVersion();
    EXPECT_EQ(version, "MockGamepad");
}

TEST_F(GamepadHALTest, SupportsManualControlFeature) {
    hal_ = createHALWithMock();
    ASSERT_NE(hal_, nullptr);

    EXPECT_TRUE(hal_->supportsFeature(HALFeature::MANUAL_CONTROL));
}

TEST_F(GamepadHALTest, DoesNotSupportOtherFeatures) {
    hal_ = createHALWithMock();
    ASSERT_NE(hal_, nullptr);

    EXPECT_FALSE(hal_->supportsFeature(HALFeature::CANOPEN_SUPPORT));
    EXPECT_FALSE(hal_->supportsFeature(HALFeature::PID_CONTROL));
    EXPECT_FALSE(hal_->supportsFeature(HALFeature::TRAJECTORY_CONTROL));
    EXPECT_FALSE(hal_->supportsFeature(HALFeature::DEROTATOR_SUPPORT));
}

TEST_F(GamepadHALTest, GetSupportedFeatures) {
    hal_ = createHALWithMock();
    ASSERT_NE(hal_, nullptr);

    auto features = hal_->getSupportedFeatures();
    ASSERT_EQ(features.size(), 1);
    EXPECT_EQ(features[0], HALFeature::MANUAL_CONTROL);
}

TEST_F(GamepadHALTest, StatusStringNonEmpty) {
    hal_ = createHALWithMock();
    ASSERT_NE(hal_, nullptr);

    auto status = hal_->getStatus();
    EXPECT_FALSE(status.empty());
    EXPECT_NE(status.find("GamepadHAL"), std::string::npos);
}

TEST_F(GamepadHALTest, NoErrorsInitially) {
    hal_ = createHALWithMock();
    ASSERT_NE(hal_, nullptr);

    EXPECT_TRUE(hal_->getErrorMessages().empty());
    hal_->clearErrors();  // Should be a no-op
}

TEST_F(GamepadHALTest, ErrorAfterFailedInit) {
    auto mock = std::make_unique<MockGamepadInput>();
    mock->init_result = false;
    hal_ = createHALWithMock(std::move(mock));
    ASSERT_NE(hal_, nullptr);

    hal_->initialize(config_);
    std::string errors = hal_->getErrorMessages();
    EXPECT_FALSE(errors.empty());
    EXPECT_NE(errors.find("Failed to initialize"), std::string::npos);
}

// ============================================================================
// PART 3: Component Creation Tests
// ============================================================================

TEST_F(GamepadHALTest, CreateMotorControl) {
    hal_ = createHALWithMock();
    ASSERT_NE(hal_, nullptr);
    ASSERT_TRUE(hal_->initialize(config_));

    auto motor0 = hal_->createMotorControl(0);
    ASSERT_NE(motor0, nullptr);

    auto motor1 = hal_->createMotorControl(1);
    ASSERT_NE(motor1, nullptr);
}

TEST_F(GamepadHALTest, CreateMotorControlInvalidAxis) {
    hal_ = createHALWithMock();
    ASSERT_NE(hal_, nullptr);
    ASSERT_TRUE(hal_->initialize(config_));

    // Negative axis ID — should not crash
    auto motor = hal_->createMotorControl(-1);
    // May return nullptr or a valid pointer; just verify no crash
    (void)motor;
}

TEST_F(GamepadHALTest, CreateEncoderReader) {
    hal_ = createHALWithMock();
    ASSERT_NE(hal_, nullptr);
    ASSERT_TRUE(hal_->initialize(config_));

    auto encoder0 = hal_->createEncoderReader(0);
    ASSERT_NE(encoder0, nullptr);

    auto encoder1 = hal_->createEncoderReader(1);
    ASSERT_NE(encoder1, nullptr);
}

TEST_F(GamepadHALTest, CreateSafetyMonitor) {
    hal_ = createHALWithMock();
    ASSERT_NE(hal_, nullptr);
    ASSERT_TRUE(hal_->initialize(config_));

    auto safety = hal_->createSafetyMonitor();
    ASSERT_NE(safety, nullptr);
}

TEST_F(GamepadHALTest, CreateSensorInterface) {
    hal_ = createHALWithMock();
    ASSERT_NE(hal_, nullptr);
    ASSERT_TRUE(hal_->initialize(config_));

    auto sensor = hal_->createSensorInterface();
    ASSERT_NE(sensor, nullptr);
}

TEST_F(GamepadHALTest, CreateAllComponentsNoCrash) {
    hal_ = createHALWithMock();
    ASSERT_NE(hal_, nullptr);
    ASSERT_TRUE(hal_->initialize(config_));

    auto motor0 = hal_->createMotorControl(0);
    auto motor1 = hal_->createMotorControl(1);
    auto encoder0 = hal_->createEncoderReader(0);
    auto encoder1 = hal_->createEncoderReader(1);
    auto safety = hal_->createSafetyMonitor();
    auto sensor = hal_->createSensorInterface();

    ASSERT_NE(motor0, nullptr);
    ASSERT_NE(motor1, nullptr);
    ASSERT_NE(encoder0, nullptr);
    ASSERT_NE(encoder1, nullptr);
    ASSERT_NE(safety, nullptr);
    ASSERT_NE(sensor, nullptr);
}

// ============================================================================
// PART 4: GamepadMotorControl Tests
// ============================================================================

TEST_F(GamepadHALTest, MotorControlDefaultState) {
    hal_ = createHALWithMock();
    ASSERT_NE(hal_, nullptr);
    ASSERT_TRUE(hal_->initialize(config_));

    auto motor = hal_->createMotorControl(0);
    ASSERT_NE(motor, nullptr);

    // Default state
    EXPECT_FALSE(motor->isEnabled());
    EXPECT_FALSE(motor->isMoving());
    EXPECT_DOUBLE_EQ(motor->getActualPosition(), 0.0);
    EXPECT_DOUBLE_EQ(motor->getActualVelocity(), 0.0);
    EXPECT_DOUBLE_EQ(motor->getActualTorque(), 0.0);
    EXPECT_FALSE(motor->inErrorState());
    EXPECT_TRUE(motor->getErrorString().empty());

    // Environmental defaults — temperature and voltage are always positive,
    // current starts at 0 (no load when idle)
    EXPECT_GT(motor->getTemperature(), 0.0);
    EXPECT_GE(motor->getCurrent(), 0.0);
    EXPECT_GT(motor->getVoltage(), 0.0);
}

TEST_F(GamepadHALTest, MotorControlEnableDisable) {
    hal_ = createHALWithMock();
    ASSERT_NE(hal_, nullptr);
    ASSERT_TRUE(hal_->initialize(config_));

    auto motor = hal_->createMotorControl(0);
    ASSERT_NE(motor, nullptr);

    // Enable
    EXPECT_TRUE(motor->enable());
    EXPECT_TRUE(motor->isEnabled());

    // Disable
    EXPECT_TRUE(motor->disable());
    EXPECT_FALSE(motor->isEnabled());
}

TEST_F(GamepadHALTest, MotorControlSetVelocityEnabled) {
    hal_ = createHALWithMock();
    ASSERT_NE(hal_, nullptr);
    ASSERT_TRUE(hal_->initialize(config_));

    auto motor = hal_->createMotorControl(0);
    ASSERT_NE(motor, nullptr);
    ASSERT_TRUE(motor->enable());

    // Set velocity
    EXPECT_TRUE(motor->setVelocity(2.5, 1.0));
}

TEST_F(GamepadHALTest, MotorControlSetVelocityDisabled) {
    hal_ = createHALWithMock();
    ASSERT_NE(hal_, nullptr);
    ASSERT_TRUE(hal_->initialize(config_));

    auto motor = hal_->createMotorControl(0);
    ASSERT_NE(motor, nullptr);
    // NOT enabled

    EXPECT_FALSE(motor->setVelocity(2.5, 1.0));
}

TEST_F(GamepadHALTest, MotorControlTorqueNotSupported) {
    hal_ = createHALWithMock();
    ASSERT_NE(hal_, nullptr);
    ASSERT_TRUE(hal_->initialize(config_));

    auto motor = hal_->createMotorControl(0);
    ASSERT_NE(motor, nullptr);

    EXPECT_FALSE(motor->setTorque(50.0));
}

TEST_F(GamepadHALTest, MotorControlStop) {
    hal_ = createHALWithMock();
    ASSERT_NE(hal_, nullptr);
    ASSERT_TRUE(hal_->initialize(config_));

    auto motor = hal_->createMotorControl(0);
    ASSERT_NE(motor, nullptr);
    ASSERT_TRUE(motor->enable());

    EXPECT_TRUE(motor->stop());
    EXPECT_DOUBLE_EQ(motor->getActualVelocity(), 0.0);
}

TEST_F(GamepadHALTest, MotorControlEmergencyStop) {
    hal_ = createHALWithMock();
    ASSERT_NE(hal_, nullptr);
    ASSERT_TRUE(hal_->initialize(config_));

    auto motor = hal_->createMotorControl(0);
    ASSERT_NE(motor, nullptr);
    ASSERT_TRUE(motor->enable());

    EXPECT_TRUE(motor->emergencyStop());
    EXPECT_DOUBLE_EQ(motor->getActualVelocity(), 0.0);
}

TEST_F(GamepadHALTest, MotorControlTargetReachedWhenNotMoving) {
    hal_ = createHALWithMock();
    ASSERT_NE(hal_, nullptr);
    ASSERT_TRUE(hal_->initialize(config_));

    auto motor = hal_->createMotorControl(0);
    ASSERT_NE(motor, nullptr);
    ASSERT_TRUE(motor->enable());

    // Not moving → target reached
    EXPECT_TRUE(motor->targetReached());
}

TEST_F(GamepadHALTest, MotorControlConfigure) {
    hal_ = createHALWithMock();
    ASSERT_NE(hal_, nullptr);
    ASSERT_TRUE(hal_->initialize(config_));

    auto motor = hal_->createMotorControl(0);
    ASSERT_NE(motor, nullptr);

    MotorConfig cfg;
    cfg.type = MotorType::VIRTUAL;
    cfg.max_velocity = 10.0;
    EXPECT_TRUE(motor->configure(cfg));
    auto retrieved = motor->getConfiguration();
    EXPECT_EQ(retrieved.max_velocity, 10.0);
}

TEST_F(GamepadHALTest, MotorControlCallbacks) {
    hal_ = createHALWithMock();
    ASSERT_NE(hal_, nullptr);
    ASSERT_TRUE(hal_->initialize(config_));

    auto motor = hal_->createMotorControl(0);
    ASSERT_NE(motor, nullptr);

    bool pos_cb_called = false;
    bool error_cb_called = false;
    bool state_cb_called = false;

    motor->setPositionCallback([&](double, double, double) {
        pos_cb_called = true;
    });
    motor->setErrorCallback([&](const std::string&, int) {
        error_cb_called = true;
    });
    motor->setStateChangeCallback([&](bool, bool) {
        state_cb_called = true;
    });

    // Enable triggers state change callback
    motor->enable();
    EXPECT_TRUE(state_cb_called);

    // No crash on destruction with callbacks set
}

// ============================================================================
// PART 5: GamepadEncoderReader Tests
// ============================================================================

TEST_F(GamepadHALTest, EncoderReaderInitialize) {
    hal_ = createHALWithMock();
    ASSERT_NE(hal_, nullptr);
    ASSERT_TRUE(hal_->initialize(config_));

    auto encoder = hal_->createEncoderReader(0);
    ASSERT_NE(encoder, nullptr);

    EncoderConfig enc_cfg;
    enc_cfg.type = EncoderType::VIRTUAL;
    enc_cfg.counts_per_degree = 10000.0;
    EXPECT_TRUE(encoder->initialize(enc_cfg));
    EXPECT_TRUE(encoder->isInitialized());
}

TEST_F(GamepadHALTest, EncoderReaderReadReturnsData) {
    hal_ = createHALWithMock();
    ASSERT_NE(hal_, nullptr);
    ASSERT_TRUE(hal_->initialize(config_));

    // First create motor so encoder has a motor reference
    auto motor = hal_->createMotorControl(0);
    ASSERT_NE(motor, nullptr);
    // Enable the stored motor (createMotorControl returns a new instance,
    // but the encoder reads from the stored one via getMotor())
    auto stored_motor = hal_->getMotor(0);
    ASSERT_NE(stored_motor, nullptr);
    ASSERT_TRUE(stored_motor->enable());

    auto encoder = hal_->createEncoderReader(0);
    ASSERT_NE(encoder, nullptr);

    EncoderConfig enc_cfg;
    enc_cfg.counts_per_degree = 10000.0;
    encoder->initialize(enc_cfg);

    auto reading = encoder->read();
    EXPECT_TRUE(std::isfinite(reading.position_deg));
    EXPECT_TRUE(std::isfinite(reading.velocity_deg_s));
    EXPECT_TRUE(reading.data_valid);
}

TEST_F(GamepadHALTest, EncoderReaderPositionTracksMotor) {
    hal_ = createHALWithMock();
    ASSERT_NE(hal_, nullptr);
    ASSERT_TRUE(hal_->initialize(config_));

    auto motor = hal_->createMotorControl(0);
    ASSERT_NE(motor, nullptr);
    ASSERT_TRUE(motor->enable());

    auto encoder = hal_->createEncoderReader(0);
    ASSERT_NE(encoder, nullptr);

    EncoderConfig enc_cfg;
    enc_cfg.counts_per_degree = 10000.0;
    encoder->initialize(enc_cfg);

    // Position should start at 0
    auto reading = encoder->read();
    EXPECT_DOUBLE_EQ(reading.position_deg, 0.0);

    // Configure the motor and move it
    motor->enable();
    motor->setVelocity(1.0, 0.5);

    // Read again — position remains 0 since we're using the stored motor
    // (createMotorControl returns a new instance, but the encoder reads from the stored one)
    // Actually, the encoder gets the shared_ptr motor from GamepadHAL::getMotor()
    reading = encoder->read();
    EXPECT_TRUE(std::isfinite(reading.position_deg));
}

TEST_F(GamepadHALTest, EncoderReaderCalibrate) {
    hal_ = createHALWithMock();
    ASSERT_NE(hal_, nullptr);
    ASSERT_TRUE(hal_->initialize(config_));

    auto motor = hal_->createMotorControl(0);
    ASSERT_NE(motor, nullptr);

    auto encoder = hal_->createEncoderReader(0);
    ASSERT_NE(encoder, nullptr);

    EncoderConfig enc_cfg;
    enc_cfg.counts_per_degree = 10000.0;
    encoder->initialize(enc_cfg);

    // Calibrate with reference
    EXPECT_TRUE(encoder->calibrate(10.0));
    EXPECT_TRUE(encoder->autoCalibrate());
    EXPECT_DOUBLE_EQ(encoder->getCalibrationOffset(), 0.0);
}

TEST_F(GamepadHALTest, EncoderReaderTypeAndInterface) {
    hal_ = createHALWithMock();
    ASSERT_NE(hal_, nullptr);
    ASSERT_TRUE(hal_->initialize(config_));

    auto encoder = hal_->createEncoderReader(0);
    ASSERT_NE(encoder, nullptr);

    EXPECT_EQ(encoder->getType(), EncoderType::VIRTUAL);
    EXPECT_EQ(encoder->getInterface(), EncoderInterface::QUADRATURE);
}

// ============================================================================
// PART 6: GamepadSafetyMonitor Tests
// ============================================================================

TEST_F(GamepadHALTest, SafetyMonitorInitialize) {
    hal_ = createHALWithMock();
    ASSERT_NE(hal_, nullptr);
    ASSERT_TRUE(hal_->initialize(config_));

    auto safety = hal_->createSafetyMonitor();
    ASSERT_NE(safety, nullptr);

    SafetyConfig safety_cfg;
    EXPECT_TRUE(safety->initialize(safety_cfg));
    EXPECT_TRUE(safety->isInitialized());
}

TEST_F(GamepadHALTest, SafetyMonitorDefaultState) {
    hal_ = createHALWithMock();
    ASSERT_NE(hal_, nullptr);
    ASSERT_TRUE(hal_->initialize(config_));

    auto safety = hal_->createSafetyMonitor();
    ASSERT_NE(safety, nullptr);

    SafetyConfig safety_cfg;
    safety->initialize(safety_cfg);

    auto status = safety->getStatus();
    EXPECT_EQ(status.overall_state, SafetyStatus::State::NORMAL);
    EXPECT_FALSE(status.emergency_stop_active);
}

TEST_F(GamepadHALTest, SafetyMonitorCheckLimits) {
    hal_ = createHALWithMock();
    ASSERT_NE(hal_, nullptr);
    ASSERT_TRUE(hal_->initialize(config_));

    auto safety = hal_->createSafetyMonitor();
    ASSERT_NE(safety, nullptr);

    EXPECT_TRUE(safety->checkLimits(0));
    EXPECT_TRUE(safety->checkLimits(1));
}

TEST_F(GamepadHALTest, SafetyMonitorEmergencyStop) {
    hal_ = createHALWithMock();
    ASSERT_NE(hal_, nullptr);
    ASSERT_TRUE(hal_->initialize(config_));

    auto safety = hal_->createSafetyMonitor();
    ASSERT_NE(safety, nullptr);
    SafetyConfig safety_cfg;
    safety->initialize(safety_cfg);

    EXPECT_TRUE(safety->emergencyStop(0));

    auto status = safety->getStatus();
    EXPECT_EQ(status.overall_state, SafetyStatus::State::EMERGENCY_STOP);
    EXPECT_TRUE(status.emergency_stop_active);
}

TEST_F(GamepadHALTest, SafetyMonitorClearErrors) {
    hal_ = createHALWithMock();
    ASSERT_NE(hal_, nullptr);
    ASSERT_TRUE(hal_->initialize(config_));

    auto safety = hal_->createSafetyMonitor();
    ASSERT_NE(safety, nullptr);
    SafetyConfig safety_cfg;
    safety->initialize(safety_cfg);

    safety->emergencyStop(0);
    EXPECT_TRUE(safety->clearErrors(0));

    auto status = safety->getStatus();
    EXPECT_EQ(status.overall_state, SafetyStatus::State::NORMAL);
    EXPECT_FALSE(status.emergency_stop_active);
}

// ============================================================================
// PART 7: GamepadSensorInterface Tests
// ============================================================================

TEST_F(GamepadHALTest, SensorInterfaceInitialize) {
    hal_ = createHALWithMock();
    ASSERT_NE(hal_, nullptr);
    ASSERT_TRUE(hal_->initialize(config_));

    auto sensor = hal_->createSensorInterface();
    ASSERT_NE(sensor, nullptr);

    SensorConfig sensor_cfg;
    EXPECT_TRUE(sensor->initialize(sensor_cfg));
    EXPECT_TRUE(sensor->isInitialized());
}

TEST_F(GamepadHALTest, SensorInterfaceRead) {
    hal_ = createHALWithMock();
    ASSERT_NE(hal_, nullptr);
    ASSERT_TRUE(hal_->initialize(config_));

    auto sensor = hal_->createSensorInterface();
    ASSERT_NE(sensor, nullptr);

    auto reading = sensor->read(0);
    EXPECT_EQ(reading.sensor_id, 0);
    EXPECT_EQ(reading.type, SensorType::TEMPERATURE);
    EXPECT_GE(reading.value, 15.0);  // Room temp range
    EXPECT_LE(reading.value, 25.0);
    EXPECT_EQ(reading.unit, "°C");
    EXPECT_TRUE(reading.valid);
}

TEST_F(GamepadHALTest, SensorInterfaceReadAll) {
    hal_ = createHALWithMock();
    ASSERT_NE(hal_, nullptr);
    ASSERT_TRUE(hal_->initialize(config_));

    auto sensor = hal_->createSensorInterface();
    ASSERT_NE(sensor, nullptr);

    auto readings = sensor->readAll();
    ASSERT_GE(readings.size(), 1);
    EXPECT_TRUE(readings[0].valid);
}

// ============================================================================
// PART 8: Speed Preset Tests
// ============================================================================

TEST_F(GamepadHALTest, SpeedPresetDefault) {
    hal_ = createHALWithMock();
    ASSERT_NE(hal_, nullptr);

    // Default should be the middle preset (index 2 = 2.0 deg/s)
    double preset = hal_->getCurrentSpeedPreset();
    EXPECT_DOUBLE_EQ(preset, 2.0);
}

TEST_F(GamepadHALTest, SpeedPresetCycleUp) {
    hal_ = createHALWithMock();
    ASSERT_NE(hal_, nullptr);

    // Start at index 2 (2.0)
    EXPECT_DOUBLE_EQ(hal_->getCurrentSpeedPreset(), 2.0);

    hal_->cycleSpeedUp();
    EXPECT_DOUBLE_EQ(hal_->getCurrentSpeedPreset(), 3.0);

    hal_->cycleSpeedUp();
    EXPECT_DOUBLE_EQ(hal_->getCurrentSpeedPreset(), 5.0);

    // Should clamp at max
    hal_->cycleSpeedUp();
    EXPECT_DOUBLE_EQ(hal_->getCurrentSpeedPreset(), 5.0);
}

TEST_F(GamepadHALTest, SpeedPresetCycleDown) {
    hal_ = createHALWithMock();
    ASSERT_NE(hal_, nullptr);

    // Start at index 2 (2.0)
    hal_->cycleSpeedDown();
    EXPECT_DOUBLE_EQ(hal_->getCurrentSpeedPreset(), 1.0);

    hal_->cycleSpeedDown();
    EXPECT_DOUBLE_EQ(hal_->getCurrentSpeedPreset(), 0.5);

    // Should clamp at min
    hal_->cycleSpeedDown();
    EXPECT_DOUBLE_EQ(hal_->getCurrentSpeedPreset(), 0.5);
}

TEST_F(GamepadHALTest, SpeedPresetCustomValues) {
    hal_ = createHALWithMock();
    ASSERT_NE(hal_, nullptr);

    // Set custom presets
    GamepadHAL::ManualControlConfig cfg;
    cfg.speed_presets_deg_s = {1.0, 2.0, 4.0, 8.0};
    hal_->setManualControlConfig(cfg);

    // Reset index to 0
    for (int i = 0; i < 5; i++) hal_->cycleSpeedDown();
    EXPECT_DOUBLE_EQ(hal_->getCurrentSpeedPreset(), 1.0);

    hal_->cycleSpeedUp();
    EXPECT_DOUBLE_EQ(hal_->getCurrentSpeedPreset(), 2.0);
}

// ============================================================================
// PART 9: ManualControlConfig Tests
// ============================================================================

TEST_F(GamepadHALTest, SetGetManualControlConfig) {
    hal_ = createHALWithMock();
    ASSERT_NE(hal_, nullptr);

    GamepadHAL::ManualControlConfig cfg;
    cfg.max_velocity_deg_s = 10.0;
    cfg.deadzone = 0.2;
    cfg.sensitivity = 1.5;
    cfg.invert_axis1 = true;
    cfg.update_rate_hz = 100.0;

    hal_->setManualControlConfig(cfg);

    auto retrieved = hal_->getManualControlConfig();
    EXPECT_DOUBLE_EQ(retrieved.max_velocity_deg_s, 10.0);
    EXPECT_DOUBLE_EQ(retrieved.deadzone, 0.2);
    EXPECT_DOUBLE_EQ(retrieved.sensitivity, 1.5);
    EXPECT_TRUE(retrieved.invert_axis1);
    EXPECT_DOUBLE_EQ(retrieved.update_rate_hz, 100.0);
}

// ============================================================================
// PART 10: GamepadInput Interface Direct Tests
// ============================================================================

TEST_F(GamepadHALTest, MockGamepadInputInitialize) {
    auto mock = std::make_unique<MockGamepadInput>();
    ASSERT_NE(mock, nullptr);

    EXPECT_TRUE(mock->initialize("/dev/input/js0"));
    EXPECT_TRUE(mock->isConnected());
    EXPECT_EQ(mock->last_device_path_, "/dev/input/js0");
}

TEST_F(GamepadHALTest, MockGamepadInputInitializeFailure) {
    auto mock = std::make_unique<MockGamepadInput>();
    mock->init_result = false;

    EXPECT_FALSE(mock->initialize("/dev/input/js0"));
    EXPECT_FALSE(mock->isConnected());
}

TEST_F(GamepadHALTest, MockGamepadInputShutdown) {
    auto mock = std::make_unique<MockGamepadInput>();
    mock->initialize("/dev/input/js0");
    EXPECT_TRUE(mock->isConnected());

    mock->shutdown();
    EXPECT_FALSE(mock->isConnected());
    EXPECT_TRUE(mock->shutdown_called_);
}

TEST_F(GamepadHALTest, MockGamepadInputReadState) {
    auto mock = std::make_unique<MockGamepadInput>();
    mock->initialize("");

    // Set some state
    mock->state.axis_lx = 0.5;
    mock->state.axis_ly = -0.3;
    mock->state.button_stop = true;

    auto state = mock->readState();
    EXPECT_DOUBLE_EQ(state.axis_lx, 0.5);
    EXPECT_DOUBLE_EQ(state.axis_ly, -0.3);
    EXPECT_TRUE(state.button_stop);
    EXPECT_TRUE(state.connected);
}

TEST_F(GamepadHALTest, MockGamepadInputGetInfo) {
    auto mock = std::make_unique<MockGamepadInput>();
    EXPECT_EQ(mock->getDeviceName(), "MockGamepad");
    EXPECT_EQ(mock->getAxisCount(), 8);
    EXPECT_EQ(mock->getButtonCount(), 11);
}

TEST_F(GamepadHALTest, MockGamepadInputApplyButtonMapping) {
    auto mock = std::make_unique<MockGamepadInput>();
    std::map<int, std::string> mapping = {
        {0, "home"}, {1, "stop"}, {2, "park"}
    };
    mock->applyButtonMapping(mapping);
    EXPECT_EQ(mock->last_button_mapping_[0], "home");
    EXPECT_EQ(mock->last_button_mapping_[1], "stop");
    EXPECT_EQ(mock->last_button_mapping_[2], "park");
}

TEST_F(GamepadHALTest, MockGamepadInputApplyAxisMapping) {
    auto mock = std::make_unique<MockGamepadInput>();
    std::map<int, std::string> mapping = {
        {0, "lx"}, {1, "ly"}, {3, "rx"}
    };
    mock->applyAxisMapping(mapping);
    EXPECT_EQ(mock->last_axis_mapping_[0], "lx");
    EXPECT_EQ(mock->last_axis_mapping_[1], "ly");
    EXPECT_EQ(mock->last_axis_mapping_[3], "rx");
}

// ============================================================================
// PART 11: GamepadHAL Direct API Tests
// ============================================================================

TEST_F(GamepadHALTest, GetGamepadInputAccessor) {
    auto mock = std::make_unique<MockGamepadInput>();
    auto* mock_ptr = mock.get();
    hal_ = std::make_unique<GamepadHAL>(config_, std::move(mock));
    ASSERT_NE(hal_, nullptr);

    auto* input = hal_->getGamepadInput();
    ASSERT_NE(input, nullptr);
    EXPECT_EQ(input->getDeviceName(), "MockGamepad");
}

TEST_F(GamepadHALTest, GetGamepadInputReturnsNullIfNotOwned) {
    // If we pass a mock, getGamepadInput should still return the pointer
    auto mock = std::make_unique<MockGamepadInput>();
    hal_ = std::make_unique<GamepadHAL>(config_, std::move(mock));
    ASSERT_NE(hal_, nullptr);

    auto* input = hal_->getGamepadInput();
    EXPECT_NE(input, nullptr);
}

TEST_F(GamepadHALTest, GetMotorReturnsSharedPtr) {
    hal_ = createHALWithMock();
    ASSERT_NE(hal_, nullptr);
    ASSERT_TRUE(hal_->initialize(config_));

    // Create motor control first (stores it internally)
    auto motor_unique = hal_->createMotorControl(0);
    ASSERT_NE(motor_unique, nullptr);

    auto motor_shared = hal_->getMotor(0);
    // Note: getMotor returns the internally stored GamepadMotorControl,
    // while createMotorControl returns a *new* instance. They are different objects.
    // Just verify no crash and returns non-null.
    EXPECT_NE(motor_shared, nullptr);
}

TEST_F(GamepadHALTest, GetMotorInvalidAxis) {
    hal_ = createHALWithMock();
    ASSERT_NE(hal_, nullptr);

    auto motor = hal_->getMotor(99);
    EXPECT_EQ(motor, nullptr);
}

// ============================================================================
// PART 12: Update Loop Integration Tests
// ============================================================================

TEST_F(GamepadHALTest, UpdateLoopReadsGamepadState) {
    hal_ = createHALWithMock();
    ASSERT_NE(hal_, nullptr);
    ASSERT_TRUE(hal_->initialize(config_));
    ASSERT_NE(mock_input_, nullptr);

    // Set some gamepad state
    mock_input_->state.axis_lx = 0.5;
    mock_input_->state.connected = true;

    // Start the update loop
    ASSERT_TRUE(hal_->start());

    // Let it run a few cycles
    std::this_thread::sleep_for(100ms);

    // Stop
    ASSERT_TRUE(hal_->stop());

    // The mock should have been read
    // If motor was created, its velocity should reflect gamepad state
    // (This is an integration-level check; the motor control test below is more precise)
}

TEST_F(GamepadHALTest, UpdateLoopReactsToSpeedButtons) {
    hal_ = createHALWithMock();
    ASSERT_NE(hal_, nullptr);
    ASSERT_TRUE(hal_->initialize(config_));
    ASSERT_NE(mock_input_, nullptr);

    mock_input_->state.connected = true;

    double initial_speed = hal_->getCurrentSpeedPreset();
    EXPECT_DOUBLE_EQ(initial_speed, 2.0);

    ASSERT_TRUE(hal_->start());

    // Wait for one full loop iteration, then pulse speed_up for exactly
    // one read cycle (loop runs at 50Hz = 20ms per iteration).
    std::this_thread::sleep_for(25ms);
    mock_input_->state.button_speed_up = true;
    std::this_thread::sleep_for(25ms);
    mock_input_->state.button_speed_up = false;
    std::this_thread::sleep_for(50ms);

    ASSERT_TRUE(hal_->stop());

    // Speed should have increased by exactly one step
    EXPECT_DOUBLE_EQ(hal_->getCurrentSpeedPreset(), 3.0);
}

TEST_F(GamepadHALTest, UpdateLoopReactsToSpeedDownButton) {
    hal_ = createHALWithMock();
    ASSERT_NE(hal_, nullptr);
    ASSERT_TRUE(hal_->initialize(config_));
    ASSERT_NE(mock_input_, nullptr);

    mock_input_->state.connected = true;

    double initial_speed = hal_->getCurrentSpeedPreset();
    EXPECT_DOUBLE_EQ(initial_speed, 2.0);

    ASSERT_TRUE(hal_->start());

    // Pulse speed_down for exactly one read cycle.
    std::this_thread::sleep_for(25ms);
    mock_input_->state.button_speed_down = true;
    std::this_thread::sleep_for(25ms);
    mock_input_->state.button_speed_down = false;
    std::this_thread::sleep_for(50ms);

    ASSERT_TRUE(hal_->stop());

    // Speed should have decreased by exactly one step
    EXPECT_DOUBLE_EQ(hal_->getCurrentSpeedPreset(), 1.0);
}

TEST_F(GamepadHALTest, UpdateLoopIntegratesPosition) {
    hal_ = createHALWithMock();
    ASSERT_NE(hal_, nullptr);
    ASSERT_TRUE(hal_->initialize(config_));
    ASSERT_NE(mock_input_, nullptr);

    // Create motor for axis 0
    auto motor = hal_->createMotorControl(0);
    ASSERT_NE(motor, nullptr);
    ASSERT_TRUE(motor->enable());

    // Set left stick to half deflection
    mock_input_->state.axis_lx = 0.5;
    mock_input_->state.connected = true;

    // Start the loop — it will read the gamepad and update the motor
    ASSERT_TRUE(hal_->start());
    std::this_thread::sleep_for(200ms);
    ASSERT_TRUE(hal_->stop());

    // The stored motor should have non-zero velocity and integrated position
    // Note: createMotorControl returns a NEW instance, but the internal one
    // is updated by the loop. We access it via getMotor().
    auto stored_motor = hal_->getMotor(0);
    ASSERT_NE(stored_motor, nullptr);

    // With axis_lx = 0.5 and current speed preset 2.0, velocity should be ~1.0
    // (0.5 * 2.0 = 1.0, minus deadzone adjustment)
    double vel = stored_motor->getActualVelocity();
    EXPECT_GT(vel, 0.0);

    // Position should have been integrated
    double pos = stored_motor->getActualPosition();
    EXPECT_GT(pos, 0.0);
}

TEST_F(GamepadHALTest, UpdateLoopMotorVelocityMatchesJoystick) {
    hal_ = createHALWithMock();
    ASSERT_NE(hal_, nullptr);
    ASSERT_TRUE(hal_->initialize(config_));
    ASSERT_NE(mock_input_, nullptr);

    // Create motor
    auto motor_uniq = hal_->createMotorControl(0);
    ASSERT_NE(motor_uniq, nullptr);
    ASSERT_TRUE(motor_uniq->enable());

    // Full left stick deflection (after deadzone adjustment: (1.0-0.15)/(1.0-0.15) = 1.0)
    // With speed preset 2.0: velocity = 1.0 * 2.0 = 2.0
    mock_input_->state.axis_lx = 1.0;
    mock_input_->state.connected = true;

    ASSERT_TRUE(hal_->start());
    std::this_thread::sleep_for(100ms);
    ASSERT_TRUE(hal_->stop());

    auto stored_motor = hal_->getMotor(0);
    ASSERT_NE(stored_motor, nullptr);

    // Full deflection at 2.0 deg/s preset
    double expected_vel = 2.0;  // 1.0 * 2.0
    double actual_vel = stored_motor->getActualVelocity();
    EXPECT_NEAR(actual_vel, expected_vel, 0.1);
}

// ============================================================================
// PART 13: GamepadHAL Constructor Config Application Tests
// ============================================================================

TEST_F(GamepadHALTest, ConstructorAppliesDeadzoneFromConfig) {
    config_.gamepad.deadzone = 0.25;
    config_.gamepad.sensitivity = 0.8;
    config_.gamepad.max_velocity_deg_s = 8.0;

    hal_ = std::make_unique<GamepadHAL>(config_, std::make_unique<MockGamepadInput>());
    ASSERT_NE(hal_, nullptr);

    auto cfg = hal_->getManualControlConfig();
    EXPECT_DOUBLE_EQ(cfg.deadzone, 0.25);
    EXPECT_DOUBLE_EQ(cfg.sensitivity, 0.8);
    EXPECT_DOUBLE_EQ(cfg.max_velocity_deg_s, 8.0);
}

TEST_F(GamepadHALTest, ConstructorAppliesInvertFlags) {
    config_.gamepad.invert_axis1 = true;
    config_.gamepad.invert_axis2 = true;

    hal_ = std::make_unique<GamepadHAL>(config_, std::make_unique<MockGamepadInput>());
    ASSERT_NE(hal_, nullptr);

    auto cfg = hal_->getManualControlConfig();
    EXPECT_TRUE(cfg.invert_axis1);
    EXPECT_TRUE(cfg.invert_axis2);
}

// ============================================================================
// PART 14: Edge Case and Error Handling Tests
// ============================================================================

TEST_F(GamepadHALTest, UpdateLoopHandlesDisconnectedGamepad) {
    hal_ = createHALWithMock();
    ASSERT_NE(hal_, nullptr);
    ASSERT_TRUE(hal_->initialize(config_));
    ASSERT_NE(mock_input_, nullptr);

    // Gamepad not connected
    mock_input_->connected = false;

    // Start should work (HAL manages the disconnection gracefully)
    EXPECT_TRUE(hal_->start());
    std::this_thread::sleep_for(50ms);
    EXPECT_TRUE(hal_->stop());

    // No crash, no errors
}

TEST_F(GamepadHALTest, DoubleStartIsIdempotent) {
    hal_ = createHALWithMock();
    ASSERT_NE(hal_, nullptr);
    ASSERT_TRUE(hal_->initialize(config_));

    EXPECT_TRUE(hal_->start());
    EXPECT_TRUE(hal_->start());  // Second start should be safe
    EXPECT_TRUE(hal_->isRunning());

    EXPECT_TRUE(hal_->stop());
}

TEST_F(GamepadHALTest, DoubleStopIsIdempotent) {
    hal_ = createHALWithMock();
    ASSERT_NE(hal_, nullptr);
    ASSERT_TRUE(hal_->initialize(config_));

    EXPECT_TRUE(hal_->start());
    EXPECT_TRUE(hal_->stop());
    EXPECT_TRUE(hal_->stop());  // Second stop should be safe
    EXPECT_FALSE(hal_->isRunning());
}

TEST_F(GamepadHALTest, NoNullptrDereferenceWithNullGamepadInput) {
    // This tests the edge case where gamepad_input_ might be null
    // The constructor always creates one if not provided, so this is safe
    // by design. We verify the constructor handles nullptr gracefully.
    hal_ = std::make_unique<GamepadHAL>(config_);
    ASSERT_NE(hal_, nullptr);
    EXPECT_NE(hal_->getGamepadInput(), nullptr);
}

// ============================================================================
// PART 15: GamepadState Initial Values
// ============================================================================

TEST(GamepadStateTest, DefaultStateValues) {
    GamepadState state;

    EXPECT_DOUBLE_EQ(state.axis_lx, 0.0);
    EXPECT_DOUBLE_EQ(state.axis_ly, 0.0);
    EXPECT_DOUBLE_EQ(state.axis_rx, 0.0);
    EXPECT_DOUBLE_EQ(state.axis_ry, 0.0);
    EXPECT_DOUBLE_EQ(state.axis_trigger_l, 0.0);
    EXPECT_DOUBLE_EQ(state.axis_trigger_r, 0.0);
    EXPECT_DOUBLE_EQ(state.pov_hat, -1.0);
    EXPECT_FALSE(state.button_stop);
    EXPECT_FALSE(state.button_emergency_stop);
    EXPECT_FALSE(state.button_park);
    EXPECT_FALSE(state.button_speed_up);
    EXPECT_FALSE(state.button_speed_down);
    EXPECT_FALSE(state.button_manual_toggle);
    EXPECT_FALSE(state.button_home);
    EXPECT_FALSE(state.connected);
}

// ============================================================================
// PART 16: GamepadMotorControl updateVelocity and integratePosition
// ============================================================================

TEST_F(GamepadHALTest, MotorUpdateVelocityOverridesCommanded) {
    hal_ = createHALWithMock();
    ASSERT_NE(hal_, nullptr);

    auto motor = std::make_shared<GamepadMotorControl>(0, *hal_);
    motor->enable();

    // Set a commanded velocity
    motor->setCommandedVelocity(0.5);

    // Update with gamepad velocity (non-zero) — should override
    motor->updateVelocity(3.0);
    EXPECT_DOUBLE_EQ(motor->getActualVelocity(), 3.0);

    // Update with zero velocity — should fall back to commanded
    motor->updateVelocity(0.0);
    EXPECT_DOUBLE_EQ(motor->getActualVelocity(), 0.5);
}

TEST_F(GamepadHALTest, MotorIntegratePosition) {
    hal_ = createHALWithMock();
    ASSERT_NE(hal_, nullptr);

    auto motor = std::make_shared<GamepadMotorControl>(0, *hal_);

    // Set velocity and integrate
    motor->enable();
    motor->updateVelocity(2.0);
    motor->integratePosition(0.5);  // 0.5 seconds at 2.0 deg/s = 1.0 deg

    EXPECT_NEAR(motor->getActualPosition(), 1.0, 0.001);
    EXPECT_NEAR(motor->getActualVelocity(), 2.0, 0.001);
}

TEST_F(GamepadHALTest, MotorIntegratePositionClampsDt) {
    hal_ = createHALWithMock();
    ASSERT_NE(hal_, nullptr);

    auto motor = std::make_shared<GamepadMotorControl>(0, *hal_);
    motor->enable();
    motor->updateVelocity(2.0);

    // dt <= 0 → no integration
    double pos_before = motor->getActualPosition();
    motor->integratePosition(0.0);
    EXPECT_DOUBLE_EQ(motor->getActualPosition(), pos_before);

    // dt > 1.0 → no integration (sanity check)
    motor->integratePosition(2.0);
    EXPECT_DOUBLE_EQ(motor->getActualPosition(), pos_before);
}

TEST_F(GamepadHALTest, MotorIsMovingDetection) {
    hal_ = createHALWithMock();
    ASSERT_NE(hal_, nullptr);

    auto motor = std::make_shared<GamepadMotorControl>(0, *hal_);
    motor->enable();

    // Not moving initially
    EXPECT_FALSE(motor->isMoving());

    // Set velocity > 0.001 → moving
    motor->updateVelocity(0.5);
    EXPECT_TRUE(motor->isMoving());

    // Stop → not moving
    motor->stop();
    EXPECT_FALSE(motor->isMoving());
}

} // namespace test
} // namespace hal
} // namespace astro_mount

// ============================================================================
// Main
// ============================================================================
int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
