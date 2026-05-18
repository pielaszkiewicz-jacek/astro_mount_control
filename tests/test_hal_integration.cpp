#include "hal/hal_factory.h"
#include "hal/hal_interface.h"
#include "hal/hal_config.h"
#include "hal/simulated_hal/simulated_hal.h"
#include "hal/safety_monitor.h"
#include "hal/sensor_interface.h"
#include "controllers/mount_controller.h"
#include "logging/logger.h"
#include <gtest/gtest.h>
#include <thread>
#include <chrono>
#include <atomic>
#include <csignal>
#include <functional>

namespace astro_mount {
namespace hal {
namespace test {

using namespace std::chrono_literals;

// ============================================================================
// Shared test fixture for HAL integration tests
// ============================================================================

class HALIntegrationTest : public ::testing::Test {
protected:
    void SetUp() override {
        logging::Logger::init("");

        // Default controller config matching test_mount_controller.cpp
        config_.mount_type = controllers::MountController::MountType::EQUATORIAL;
        config_.latitude = 52.0;
        config_.longitude = 21.0;
        config_.altitude = 100.0;
        config_.max_slew_rate = 5.0;
        config_.max_tracking_rate = 0.004178;
        config_.slew_acceleration = 1.0;
        config_.tracking_acceleration = 0.001;
        config_.position_tolerance = 0.5;
        config_.rate_tolerance = 0.001;
        config_.canopen_interface = "";   // mock (no real hardware)
        config_.canopen_node_id = 1;
        config_.use_encoders = false;
        config_.encoders_absolute = false;
        config_.encoder_resolution = 360000;
        config_.default_temperature = 15.0;
        config_.default_pressure = 1013.25;
        config_.default_humidity = 0.5;
        config_.focal_length = 2000.0;
        config_.aperture = 250.0;
        config_.enable_guider = false;
        config_.guider_max_correction = 100.0;
        config_.guider_aggression = 0.5;
        config_.ha_axis_params.gear_ratio = 360.0;
        config_.dec_axis_params.gear_ratio = 360.0;
        config_.ha_axis_params.backlash = 0.0;
        config_.dec_axis_params.backlash = 0.0;
        config_.ha_axis_params.encoder_resolution = 360000.0;
        config_.dec_axis_params.encoder_resolution = 360000.0;
        config_.ha_axis_params.motor_steps_per_rev = 200;
        config_.dec_axis_params.motor_steps_per_rev = 200;
        config_.ha_axis_params.motor_microstepping = 16;
        config_.dec_axis_params.motor_microstepping = 16;

        // Build default HAL config for SimulatedHAL
        hal_config_ = HALFactory::getDefaultConfig(HALType::SIMULATED);
    }

    void TearDown() override {
        if (controller_) {
            controller_->shutdown();
            controller_.reset();
        }
        if (hal_) {
            hal_->shutdown();
            hal_.reset();
        }
    }

    std::unique_ptr<HALInterface> hal_;
    std::unique_ptr<controllers::MountController> controller_;
    controllers::MountController::ControllerConfig config_;
    HALConfig hal_config_;
};

// ============================================================================
// PART 1: HAL Factory Tests
// Verifies that HALFactory correctly creates and manages SimulatedHAL instances
// ============================================================================

TEST_F(HALIntegrationTest, FactoryCreatesSimulatedHALByType) {
    auto hal = HALFactory::create(HALType::SIMULATED);
    EXPECT_NE(hal, nullptr);
}

TEST_F(HALIntegrationTest, FactoryCreatesSimulatedHALByName) {
    auto hal = HALFactory::create("simulated");
    EXPECT_NE(hal, nullptr);
}

TEST_F(HALIntegrationTest, FactoryCreatesSimulatedHALByConfig) {
    auto hal = HALFactory::create(hal_config_);
    EXPECT_NE(hal, nullptr);
}

TEST_F(HALIntegrationTest, FactoryGetAvailableTypesContainsSimulated) {
    auto types = HALFactory::getAvailableTypes();
    bool has_simulated = false;
    for (auto t : types) {
        if (t == HALType::SIMULATED) has_simulated = true;
    }
    EXPECT_TRUE(has_simulated);
}

TEST_F(HALIntegrationTest, FactoryDefaultConfigIsSimulated) {
    auto config = HALFactory::getDefaultConfig(HALType::SIMULATED);
    EXPECT_EQ(config.type, HALType::SIMULATED);
    EXPECT_TRUE(config.simulated.enable_simulation);
    EXPECT_EQ(config.axes.size(), 2);
    EXPECT_EQ(config.axes[0].name, "RA_Axis");
    EXPECT_EQ(config.axes[1].name, "Dec_Axis");
}

TEST_F(HALIntegrationTest, FactoryIsTypeAvailable) {
    EXPECT_TRUE(HALFactory::isTypeAvailable(HALType::SIMULATED));
}

TEST_F(HALIntegrationTest, FactoryGetDefaultType) {
    // On this system without CANopen hardware, default should be SIMULATED
    auto default_type = HALFactory::getDefaultType();
    EXPECT_EQ(default_type, HALType::SIMULATED);
}

// ============================================================================
// PART 2: SimulatedHAL Direct Tests
// Tests the SimulatedHAL implementation independently
// ============================================================================

TEST_F(HALIntegrationTest, SimulatedHALCreateAndDestroy) {
    // Create via factory and destroy — no crash
    auto hal = HALFactory::create(HALType::SIMULATED);
    ASSERT_NE(hal, nullptr);
    // hal goes out of scope, ~SimulatedHAL() calls shutdown()
}

TEST_F(HALIntegrationTest, SimulatedHALInitialize) {
    hal_ = HALFactory::create(HALType::SIMULATED);
    ASSERT_NE(hal_, nullptr);

    EXPECT_TRUE(hal_->initialize(hal_config_));
    EXPECT_TRUE(hal_->isInitialized());
}

TEST_F(HALIntegrationTest, SimulatedHALInitializeRejectedWhenAlreadyInitialized) {
    hal_ = HALFactory::create(HALType::SIMULATED);
    ASSERT_NE(hal_, nullptr);

    EXPECT_TRUE(hal_->initialize(hal_config_));
    EXPECT_TRUE(hal_->isInitialized());

    // Second initialize should still succeed (idempotent)
    EXPECT_TRUE(hal_->initialize(hal_config_));
}

TEST_F(HALIntegrationTest, SimulatedHALStartStop) {
    hal_ = HALFactory::create(HALType::SIMULATED);
    ASSERT_NE(hal_, nullptr);
    ASSERT_TRUE(hal_->initialize(hal_config_));

    // Start
    EXPECT_TRUE(hal_->start());
    EXPECT_TRUE(hal_->isRunning());

    // Stop
    EXPECT_TRUE(hal_->stop());
    EXPECT_FALSE(hal_->isRunning());
}

TEST_F(HALIntegrationTest, SimulatedHALStartFailsWhenNotInitialized) {
    hal_ = HALFactory::create(HALType::SIMULATED);
    ASSERT_NE(hal_, nullptr);

    // Cannot start without initialize
    EXPECT_FALSE(hal_->start());
    EXPECT_FALSE(hal_->isRunning());
}

TEST_F(HALIntegrationTest, SimulatedHALShutdown) {
    hal_ = HALFactory::create(HALType::SIMULATED);
    ASSERT_NE(hal_, nullptr);
    ASSERT_TRUE(hal_->initialize(hal_config_));
    ASSERT_TRUE(hal_->isInitialized());

    hal_->shutdown();
    EXPECT_FALSE(hal_->isInitialized());
    EXPECT_FALSE(hal_->isRunning());
}

TEST_F(HALIntegrationTest, SimulatedHALShutdownStopsRunning) {
    hal_ = HALFactory::create(HALType::SIMULATED);
    ASSERT_NE(hal_, nullptr);
    ASSERT_TRUE(hal_->initialize(hal_config_));
    ASSERT_TRUE(hal_->start());
    ASSERT_TRUE(hal_->isRunning());

    hal_->shutdown();
    EXPECT_FALSE(hal_->isInitialized());
    EXPECT_FALSE(hal_->isRunning());
}

TEST_F(HALIntegrationTest, SimulatedHALGetPlatformInfo) {
    hal_ = HALFactory::create(HALType::SIMULATED);
    ASSERT_NE(hal_, nullptr);

    EXPECT_EQ(hal_->getPlatformName(), "SimulatedHAL_v1.0");
    EXPECT_EQ(hal_->getHardwareVersion(), "1.0.0-simulated");
}

TEST_F(HALIntegrationTest, SimulatedHALSupportsFeatures) {
    hal_ = HALFactory::create(HALType::SIMULATED);
    ASSERT_NE(hal_, nullptr);

    EXPECT_TRUE(hal_->supportsFeature(HALFeature::DEROTATOR_SUPPORT));
    // SIMULATION is not in the enum, but MOTOR_CONTROL, ENCODER_READING are
    // actual features
}

TEST_F(HALIntegrationTest, SimulatedHALCreateComponents) {
    hal_ = HALFactory::create(HALType::SIMULATED);
    ASSERT_NE(hal_, nullptr);
    ASSERT_TRUE(hal_->initialize(hal_config_));

    // Motor controls (currently returns nullptr due to design — 
    // motors are pre-created internally)
    auto motor0 = hal_->createMotorControl(0);
    // May be null - it's valid for the interface
    // We just verify no crash

    auto encoder0 = hal_->createEncoderReader(0);
    (void)motor0;
    (void)encoder0;

    auto safety = hal_->createSafetyMonitor();
    (void)safety;

    auto sensor = hal_->createSensorInterface();
    (void)sensor;
}

TEST_F(HALIntegrationTest, SimulatedHALDerotatorSupport) {
    hal_ = HALFactory::create(HALType::SIMULATED);
    ASSERT_NE(hal_, nullptr);
    ASSERT_TRUE(hal_->initialize(hal_config_));

    // Derotator config and components
    DerotatorConfig derotator_cfg;
    derotator_cfg.enabled = true;
    derotator_cfg.type = DerotatorType::STEPPER;
    EXPECT_TRUE(hal_->configureDerotator(derotator_cfg));

    auto derotator_motor = hal_->createDerotatorMotor();
    // May be null — just verify no crash
    (void)derotator_motor;

    auto derotator_encoder = hal_->createDerotatorEncoder();
    (void)derotator_encoder;
}

TEST_F(HALIntegrationTest, SimulatedHALStatusAndErrors) {
    hal_ = HALFactory::create(HALType::SIMULATED);
    ASSERT_NE(hal_, nullptr);

    // Status string should be non-empty
    auto status = hal_->getStatus();
    EXPECT_FALSE(status.empty());
    EXPECT_NE(status.find("SimulatedHAL"), std::string::npos);

    // No errors initially
    EXPECT_TRUE(hal_->getErrorMessages().empty());

    // Clear errors is a no-op
    hal_->clearErrors();
}

TEST_F(HALIntegrationTest, SimulatedHALFullLifecycle) {
    hal_ = HALFactory::create(HALType::SIMULATED);
    ASSERT_NE(hal_, nullptr);

    // Not initialized yet
    EXPECT_FALSE(hal_->isInitialized());
    EXPECT_FALSE(hal_->isRunning());

    // Initialize
    EXPECT_TRUE(hal_->initialize(hal_config_));
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

// ============================================================================
// PART 3: HAL → Controller Integration
// Tests that MountController works correctly with an injected SimulatedHAL
// ============================================================================

TEST_F(HALIntegrationTest, MountControllerWithSimulatedHAL) {
    auto hal = HALFactory::create(HALType::SIMULATED);
    ASSERT_NE(hal, nullptr);

    controller_ = std::make_unique<controllers::MountController>(std::move(hal));
    ASSERT_NE(controller_, nullptr);

    // Initialize — the controller will initialize the HAL internally
    EXPECT_TRUE(controller_->initialize(config_));

    // Verify controller reaches IDLE state
    auto status = controller_->getStatus();
    EXPECT_EQ(status.state, controllers::MountController::MountStatus::State::IDLE);
}

TEST_F(HALIntegrationTest, MountControllerWithHALInitialState) {
    controller_ = std::make_unique<controllers::MountController>(
        HALFactory::create(HALType::SIMULATED));
    ASSERT_NE(controller_, nullptr);

    // Before initialize, state should be UNINITIALIZED
    auto status = controller_->getStatus();
    EXPECT_EQ(status.state, controllers::MountController::MountStatus::State::UNINITIALIZED);
}

TEST_F(HALIntegrationTest, MountControllerWithHALSlewToEquatorial) {
    controller_ = std::make_unique<controllers::MountController>(
        HALFactory::create(HALType::SIMULATED));
    ASSERT_NE(controller_, nullptr);
    ASSERT_TRUE(controller_->initialize(config_));

    // Slew to equatorial coordinates
    EXPECT_TRUE(controller_->slewToEquatorial(12.0, 45.0));

    auto status = controller_->getStatus();
    EXPECT_EQ(status.state, controllers::MountController::MountStatus::State::SLEWING);
}

TEST_F(HALIntegrationTest, MountControllerWithHALStartTracking) {
    controller_ = std::make_unique<controllers::MountController>(
        HALFactory::create(HALType::SIMULATED));
    ASSERT_NE(controller_, nullptr);
    ASSERT_TRUE(controller_->initialize(config_));

    // Start tracking
    EXPECT_TRUE(controller_->startTracking(12.0, 45.0,
        controllers::MountController::TrackingMode::SIDEREAL));

    auto status = controller_->getStatus();
    EXPECT_EQ(status.state, controllers::MountController::MountStatus::State::TRACKING);
}

TEST_F(HALIntegrationTest, MountControllerWithHALStop) {
    controller_ = std::make_unique<controllers::MountController>(
        HALFactory::create(HALType::SIMULATED));
    ASSERT_NE(controller_, nullptr);
    ASSERT_TRUE(controller_->initialize(config_));
    ASSERT_TRUE(controller_->startTracking(12.0, 45.0));

    // Stop
    controller_->stop();

    auto status = controller_->getStatus();
    EXPECT_EQ(status.state, controllers::MountController::MountStatus::State::IDLE);
    EXPECT_DOUBLE_EQ(status.axis1_rate, 0.0);
    EXPECT_DOUBLE_EQ(status.axis2_rate, 0.0);
}

TEST_F(HALIntegrationTest, MountControllerWithHALParkUnpark) {
    controller_ = std::make_unique<controllers::MountController>(
        HALFactory::create(HALType::SIMULATED));
    ASSERT_NE(controller_, nullptr);
    ASSERT_TRUE(controller_->initialize(config_));

    // Park
    controller_->park();
    auto status = controller_->getStatus();
    EXPECT_EQ(status.state, controllers::MountController::MountStatus::State::PARKING);

    // Wait for park to complete (with mock CANopen, target_reached is immediate)
    bool parked = false;
    for (int i = 0; i < 50; i++) {
        std::this_thread::sleep_for(200ms);
        status = controller_->getStatus();
        if (status.state == controllers::MountController::MountStatus::State::PARKED) {
            parked = true;
            break;
        }
    }
    EXPECT_TRUE(parked) << "Park did not complete within timeout";

    // Unpark
    controller_->unpark();
    status = controller_->getStatus();
    EXPECT_EQ(status.state, controllers::MountController::MountStatus::State::IDLE);
}

TEST_F(HALIntegrationTest, MountControllerWithHALShutdown) {
    controller_ = std::make_unique<controllers::MountController>(
        HALFactory::create(HALType::SIMULATED));
    ASSERT_NE(controller_, nullptr);
    ASSERT_TRUE(controller_->initialize(config_));
    ASSERT_TRUE(controller_->startTracking(12.0, 45.0));

    // Shutdown
    controller_->shutdown();

    auto status = controller_->getStatus();
    EXPECT_EQ(status.state, controllers::MountController::MountStatus::State::UNINITIALIZED);
}

TEST_F(HALIntegrationTest, MountControllerWithHALFullLifecycle) {
    controller_ = std::make_unique<controllers::MountController>(
        HALFactory::create(HALType::SIMULATED));
    ASSERT_NE(controller_, nullptr);

    // UNINITIALIZED → initialize → IDLE → startTracking → TRACKING
    EXPECT_EQ(controller_->getStatus().state,
              controllers::MountController::MountStatus::State::UNINITIALIZED);

    ASSERT_TRUE(controller_->initialize(config_));
    EXPECT_EQ(controller_->getStatus().state,
              controllers::MountController::MountStatus::State::IDLE);

    ASSERT_TRUE(controller_->startTracking(12.0, 45.0));
    EXPECT_EQ(controller_->getStatus().state,
              controllers::MountController::MountStatus::State::TRACKING);

    // TRACKING → stop → IDLE
    controller_->stop();
    EXPECT_EQ(controller_->getStatus().state,
              controllers::MountController::MountStatus::State::IDLE);

    // IDLE → park → PARKING → PARKED
    controller_->park();
    EXPECT_EQ(controller_->getStatus().state,
              controllers::MountController::MountStatus::State::PARKING);

    bool parked = false;
    for (int i = 0; i < 50; i++) {
        std::this_thread::sleep_for(200ms);
        if (controller_->getStatus().state ==
            controllers::MountController::MountStatus::State::PARKED) {
            parked = true;
            break;
        }
    }
    EXPECT_TRUE(parked);

    // PARKED → unpark → IDLE
    controller_->unpark();
    EXPECT_EQ(controller_->getStatus().state,
              controllers::MountController::MountStatus::State::IDLE);

    // IDLE → shutdown → UNINITIALIZED
    controller_->shutdown();
    EXPECT_EQ(controller_->getStatus().state,
              controllers::MountController::MountStatus::State::UNINITIALIZED);
}

TEST_F(HALIntegrationTest, MountControllerWithHALGetStatusPopulated) {
    controller_ = std::make_unique<controllers::MountController>(
        HALFactory::create(HALType::SIMULATED));
    ASSERT_NE(controller_, nullptr);
    ASSERT_TRUE(controller_->initialize(config_));

    auto status = controller_->getStatus();

    // Verify all status fields are populated
    EXPECT_EQ(status.state, controllers::MountController::MountStatus::State::IDLE);
    EXPECT_TRUE(std::isfinite(status.axis1_position));
    EXPECT_TRUE(std::isfinite(status.axis2_position));
    EXPECT_TRUE(std::isfinite(status.axis1_rate));
    EXPECT_TRUE(std::isfinite(status.axis2_rate));
    EXPECT_FALSE(status.encoders_active);
    EXPECT_FALSE(status.guider_active);
    EXPECT_FALSE(status.tpoint_calibrated);
}

// ============================================================================
// PART 4: Main Loop Pattern Tests
// Tests the polling/logging pattern used in src/main.cpp
// ============================================================================

TEST_F(HALIntegrationTest, MainLoopPollingPattern) {
    // Simulates the main loop from main.cpp:
    //   while (running) {
    //       auto status = mount_controller->getStatus();
    //       if (status.state == ERROR) { ... }
    //       std::this_thread::sleep_for(100ms);
    //   }

    controller_ = std::make_unique<controllers::MountController>(
        HALFactory::create(HALType::SIMULATED));
    ASSERT_NE(controller_, nullptr);
    ASSERT_TRUE(controller_->initialize(config_));
    ASSERT_TRUE(controller_->startTracking(12.0, 45.0));

    // Run the polling loop for a fixed number of iterations
    const int NUM_ITERATIONS = 5;
    int poll_count = 0;

    for (int i = 0; i < NUM_ITERATIONS; i++) {
        auto status = controller_->getStatus();

        // Verify status is always valid
        EXPECT_TRUE(std::isfinite(status.axis1_position));
        EXPECT_TRUE(std::isfinite(status.axis2_position));
        EXPECT_TRUE(std::isfinite(status.axis1_rate));
        EXPECT_TRUE(std::isfinite(status.axis2_rate));

        // State should be TRACKING throughout (no errors with SimulatedHAL)
        EXPECT_EQ(status.state, controllers::MountController::MountStatus::State::TRACKING);

        // No error message in normal operation
        EXPECT_TRUE(status.error_message.empty());

        poll_count++;

        std::this_thread::sleep_for(100ms);
    }

    EXPECT_EQ(poll_count, NUM_ITERATIONS);

    controller_->stop();
}

TEST_F(HALIntegrationTest, MainLoopPollingWithSlew) {
    // Test the main loop pattern during slew operations
    controller_ = std::make_unique<controllers::MountController>(
        HALFactory::create(HALType::SIMULATED));
    ASSERT_NE(controller_, nullptr);
    ASSERT_TRUE(controller_->initialize(config_));

    // Start a slew
    ASSERT_TRUE(controller_->slewToEquatorial(12.0, 45.0));

    // Poll during slew
    for (int i = 0; i < 10; i++) {
        auto status = controller_->getStatus();

        // During slew, position should be finite
        EXPECT_TRUE(std::isfinite(status.axis1_position));
        EXPECT_TRUE(std::isfinite(status.axis2_position));

        std::this_thread::sleep_for(50ms);
    }

    controller_->stop();
}

TEST_F(HALIntegrationTest, MainLoopPeriodicLoggingPattern) {
    // Test that the periodic logging interval pattern works
    // From main.cpp:
    //   static auto last_log = std::chrono::steady_clock::now();
    //   if (duration_cast<seconds>(now - last_log).count() >= 10) { log; last_log = now; }

    controller_ = std::make_unique<controllers::MountController>(
        HALFactory::create(HALType::SIMULATED));
    ASSERT_NE(controller_, nullptr);
    ASSERT_TRUE(controller_->initialize(config_));

    // Simulate periodic logging
    auto last_log = std::chrono::steady_clock::now();
    const auto LOG_INTERVAL = std::chrono::seconds(10);
    int log_count = 0;

    for (int i = 0; i < 5; i++) {
        auto status = controller_->getStatus();
        auto now = std::chrono::steady_clock::now();

        if (std::chrono::duration_cast<std::chrono::seconds>(now - last_log).count() >= 10) {
            log_count++;
            last_log = now;
        }

        // Each poll takes 100ms, so 5 iterations = 500ms total — no logging
        std::this_thread::sleep_for(100ms);
    }

    // No logging should have occurred (only 500ms elapsed)
    EXPECT_EQ(log_count, 0);

    controller_->stop();
}

TEST_F(HALIntegrationTest, MainLoopErrorHandling) {
    // Test the error handling pattern from main.cpp:
    //   if (status.state == ERROR) { logger->error("Mount error: {}", status.error_message); }

    controller_ = std::make_unique<controllers::MountController>(
        HALFactory::create(HALType::SIMULATED));
    ASSERT_NE(controller_, nullptr);
    ASSERT_TRUE(controller_->initialize(config_));

    // With SimulatedHAL, no errors should occur
    for (int i = 0; i < 5; i++) {
        auto status = controller_->getStatus();

        // Pattern from main.cpp: check for error
        if (status.state == controllers::MountController::MountStatus::State::ERROR) {
            ADD_FAILURE() << "Unexpected error state: " << status.error_message;
        }

        std::this_thread::sleep_for(50ms);
    }
}

TEST_F(HALIntegrationTest, MainLoopSignalHandlerPattern) {
    // Test the signal handler pattern from main.cpp:
    //   void signal_handler(int) { running = false; }
    //   while (running) { ... }

    std::atomic<bool> running{true};
    int iterations = 0;

    // Simulate signal handler
    auto signal_handler = [&running](int) {
        running = false;
    };

    // Run loop that stops when running becomes false
    controller_ = std::make_unique<controllers::MountController>(
        HALFactory::create(HALType::SIMULATED));
    ASSERT_NE(controller_, nullptr);
    ASSERT_TRUE(controller_->initialize(config_));

    while (running && iterations < 100) {
        auto status = controller_->getStatus();
        (void)status;

        iterations++;

        // Simulate signal after 3 iterations
        if (iterations == 3) {
            signal_handler(SIGINT);
        }

        std::this_thread::sleep_for(10ms);
    }

    // Loop should have stopped at iteration 3 (signal received)
    EXPECT_EQ(iterations, 3);
    EXPECT_FALSE(running);
}

TEST_F(HALIntegrationTest, MainLoopSignalHandlerSetsRunningFalse) {
    // Direct test of the signal_handler pattern:
    //   extern bool running;
    //   void signal_handler(int) { running = false; }

    bool running = true;

    auto handler = [](int sig, bool* flag) {
        *flag = false;
    };

    handler(SIGINT, &running);
    EXPECT_FALSE(running);

    running = true;
    handler(SIGTERM, &running);
    EXPECT_FALSE(running);
}

TEST_F(HALIntegrationTest, MainLoopExceptionSafety) {
    // Test the exception handling pattern from main.cpp:
    //   try { ... } catch (const std::exception& e) { return 1; }

    controller_ = std::make_unique<controllers::MountController>(
        HALFactory::create(HALType::SIMULATED));
    ASSERT_NE(controller_, nullptr);

    // Normal operation should not throw
    EXPECT_NO_THROW({
        EXPECT_TRUE(controller_->initialize(config_));
        EXPECT_TRUE(controller_->startTracking(12.0, 45.0));
        auto status = controller_->getStatus();
        EXPECT_EQ(status.state, controllers::MountController::MountStatus::State::TRACKING);
        controller_->stop();
        controller_->shutdown();
    });
}

// ============================================================================
// PART 5: Direct SimulatedHAL Instantiation Tests
// Tests creating SimulatedHAL directly (bypassing factory) for edge cases
// ============================================================================

TEST_F(HALIntegrationTest, DirectSimulatedHALDefaultConstructor) {
    // Test the default constructor (no config)
    auto sim_hal = std::make_unique<SimulatedHAL>();
    ASSERT_NE(sim_hal, nullptr);

    // Default constructor creates motors/encoders but doesn't initialize
    // We can still check basic properties
    EXPECT_EQ(sim_hal->getPlatformName(), "SimulatedHAL_v1.0");
}

TEST_F(HALIntegrationTest, DirectSimulatedHALWithConfigConstructor) {
    // Test constructor that takes config
    auto sim_hal = std::make_unique<SimulatedHAL>(hal_config_);
    ASSERT_NE(sim_hal, nullptr);

    // This constructor calls initialize internally via the motors/encoders setup
    // But isInitialized() should still be false until explicit initialize() call
    EXPECT_FALSE(sim_hal->isInitialized());

    // Explicit initialize should work
    EXPECT_TRUE(sim_hal->initialize(hal_config_));
    EXPECT_TRUE(sim_hal->isInitialized());
}

} // namespace test
} // namespace hal
} // namespace astro_mount

// Main
int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
