#include <gtest/gtest.h>
#include "hal/canopen_hal/canopen_hal.h"
#include "hal/safety_monitor.h"
#include "hal/sensor_interface.h"
#include "controllers/canopen_factory.h"
#include "controllers/icanopen_interface.h"

namespace astro_mount {
namespace hal {
namespace test {

using namespace std::chrono_literals;

// ============================================================================
// Part 1: PIDController tests (pure computation, no hardware dependencies)
// ============================================================================

TEST(PIDControllerTest, DefaultConstruction) {
    PIDController pid;
    auto [kp, ki, kd] = pid.getParameters();
    EXPECT_DOUBLE_EQ(kp, 1.5);
    EXPECT_DOUBLE_EQ(ki, 0.2);
    EXPECT_DOUBLE_EQ(kd, 0.05);
}

TEST(PIDControllerTest, CustomConstruction) {
    PIDController pid(2.0, 0.5, 0.1, 500.0, 50.0);
    auto [kp, ki, kd] = pid.getParameters();
    EXPECT_DOUBLE_EQ(kp, 2.0);
    EXPECT_DOUBLE_EQ(ki, 0.5);
    EXPECT_DOUBLE_EQ(kd, 0.1);
}

TEST(PIDControllerTest, CalculateProportionalTerm) {
    PIDController pid(2.0, 0.0, 0.0);  // P-only
    double output = pid.calculate(100.0, 90.0, 0.1);
    // Error = 10, P = 2 * 10 = 20
    EXPECT_NEAR(output, 20.0, 1e-6);
}

TEST(PIDControllerTest, CalculateZeroError) {
    PIDController pid(2.0, 0.5, 0.1);
    double output = pid.calculate(100.0, 100.0, 0.1);
    // Error = 0, but integral accumulates from previous calls
    EXPECT_DOUBLE_EQ(output, 0.0);
}

TEST(PIDControllerTest, ResetClearsIntegral) {
    PIDController pid(2.0, 0.5, 0.1);
    pid.calculate(100.0, 90.0, 0.1);  // Accumulate integral
    pid.reset();
    double output = pid.calculate(100.0, 100.0, 0.1);
    // After reset, integral should be 0, so output = 0
    EXPECT_DOUBLE_EQ(output, 0.0);
}

TEST(PIDControllerTest, IntegralAccumulation) {
    PIDController pid(0.0, 1.0, 0.0);  // I-only
    double out1 = pid.calculate(100.0, 90.0, 0.1);  // error=10, I accumulates 10*0.1=1
    double out2 = pid.calculate(100.0, 90.0, 0.1);  // error=10, I accumulates 1+10*0.1=2
    EXPECT_NEAR(out1, 1.0, 1e-6);
    EXPECT_NEAR(out2, 2.0, 1e-6);
}

TEST(PIDControllerTest, DerivativeTerm) {
    PIDController pid(0.0, 0.0, 1.0);  // D-only
    double out1 = pid.calculate(100.0, 90.0, 0.1);  // first call: previous_error=0 => d_error=0
    double out2 = pid.calculate(100.0, 95.0, 0.1);  // error changes: d_error=(5-10)/0.1 = -50
    // D term = 1.0 * (-50) = -50
    EXPECT_NEAR(out2, -50.0, 1e-6);
}

TEST(PIDControllerTest, FullPIDOutput) {
    PIDController pid(2.0, 0.5, 0.1);
    double output = pid.calculate(100.0, 90.0, 0.1);
    // error = 10, P = 2*10 = 20
    // I: integral_accumulates 10*0.1 = 1.0, I_term = 0.5*1.0 = 0.5
    // D: previous_error_ starts at 0, derivative = (10-0)/0.1 = 100, D_term = 0.1*100 = 10
    // output = 20 + 0.5 + 10 = 30.5
    EXPECT_NEAR(output, 30.5, 1e-6);
}

TEST(PIDControllerTest, OutputLimiting) {
    PIDController pid(100.0, 0.0, 0.0, 1000.0, 50.0);  // output_limit=50
    double output = pid.calculate(100.0, 0.0, 0.1);  // error=100, P=10000, clamped to 50
    EXPECT_NEAR(output, 50.0, 1e-6);
}

TEST(PIDControllerTest, IntegralAntiWindup) {
    // ki=100 amplifies clamped integral (10) to 1000, then output_limit (100) clamps final output
    PIDController pid(0.0, 100.0, 0.0, 10.0, 100.0);  // integral_limit=10, output_limit=100
    double out1 = pid.calculate(100.0, 0.0, 1.0);  // I accumulates 100*1=100, clamped to 10, i_term=1000, output clamped to 100
    EXPECT_NEAR(out1, 100.0, 1e-6);  // clamped by output_limit
    // Second call should not wind up further
    double out2 = pid.calculate(100.0, 0.0, 1.0);
    EXPECT_NEAR(out2, 100.0, 1e-6);
}

TEST(PIDControllerTest, SetParameters) {
    PIDController pid;
    pid.setParameters(3.0, 0.1, 0.01);
    auto [kp, ki, kd] = pid.getParameters();
    EXPECT_DOUBLE_EQ(kp, 3.0);
    EXPECT_DOUBLE_EQ(ki, 0.1);
    EXPECT_DOUBLE_EQ(kd, 0.01);
}

TEST(PIDControllerTest, GetParameters) {
    PIDController pid(5.0, 1.0, 0.5);
    auto [kp, ki, kd] = pid.getParameters();
    EXPECT_DOUBLE_EQ(kp, 5.0);
    EXPECT_DOUBLE_EQ(ki, 1.0);
    EXPECT_DOUBLE_EQ(kd, 0.5);
}

// ============================================================================
// Part 2: CanOpenHAL tests (mock ICanOpenInterface via CanOpenFactory)
// ============================================================================

class CanOpenHALTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Create a mock CANopen interface
        canopen_ = controllers::CanOpenFactory::create("mock");
        ASSERT_NE(canopen_, nullptr);

        // Initialize and connect the mock
        controllers::ICanOpenInterface::Config config;
        config.library = "mock";
        config.interface_name = "vcan0";
        EXPECT_TRUE(canopen_->initialize(config));
        EXPECT_TRUE(canopen_->connect());

        // Set up default HAL config
        config_.canopen.library = "mock";
        config_.canopen.node_id = 1;
        config_.canopen.bitrate = 250000;

        // Configure axes
        config_.axes.resize(2);
        config_.axes[0].id = 0;
        config_.axes[0].name = "HA";
        config_.axes[1].id = 1;
        config_.axes[1].name = "Dec";

        // Safety config
        config_.safety.monitoring_rate = 10;
    }

    void TearDown() override {
        if (hal_) {
            hal_->shutdown();
        }
    }

    // Creates a CanOpenHAL and stores it in the fixture (owned by hal_).
    // The fixture's TearDown will shut it down. Tests must not create multiple HALs
    // without resetting hal_ first.
    CanOpenHAL* createHAL() {
        hal_ = std::make_unique<CanOpenHAL>(std::move(canopen_));
        return hal_.get();
    }

    std::unique_ptr<controllers::ICanOpenInterface> canopen_;
    std::unique_ptr<CanOpenHAL> hal_;
    HALConfig config_;
};

TEST_F(CanOpenHALTest, Constructor) {
    auto hal = createHAL();
    EXPECT_FALSE(hal->isInitialized());
    EXPECT_FALSE(hal->isRunning());
}

TEST_F(CanOpenHALTest, DestructorDoesNotCrash) {
    CanOpenHAL* hal = new CanOpenHAL(controllers::CanOpenFactory::create("mock"));
    delete hal;
}

TEST_F(CanOpenHALTest, InitializeSuccess) {
    auto hal = createHAL();
    EXPECT_TRUE(hal->initialize(config_));
    EXPECT_TRUE(hal->isInitialized());
}

TEST_F(CanOpenHALTest, GetPlatformName) {
    auto hal = createHAL();
    std::string name = hal->getPlatformName();
    EXPECT_NE(name.find("CANopen"), std::string::npos);
}

TEST_F(CanOpenHALTest, GetHardwareVersion) {
    auto hal = createHAL();
    EXPECT_EQ(hal->getHardwareVersion(), "1.0");
}

TEST_F(CanOpenHALTest, GetSupportedFeatures) {
    auto hal = createHAL();
    auto features = hal->getSupportedFeatures();
    EXPECT_GT(features.size(), 0);

    EXPECT_TRUE(hal->supportsFeature(HALFeature::CANOPEN_SUPPORT));
    EXPECT_TRUE(hal->supportsFeature(HALFeature::PID_CONTROL));
    EXPECT_TRUE(hal->supportsFeature(HALFeature::ENCODER_FEEDBACK));
    EXPECT_TRUE(hal->supportsFeature(HALFeature::SAFETY_MONITORING));
    EXPECT_TRUE(hal->supportsFeature(HALFeature::SENSOR_MONITORING));
    EXPECT_TRUE(hal->supportsFeature(HALFeature::DEROTATOR_SUPPORT));
    EXPECT_FALSE(hal->supportsFeature(HALFeature::SERIAL_SUPPORT));
    EXPECT_FALSE(hal->supportsFeature(HALFeature::ETHERNET_SUPPORT));
    EXPECT_FALSE(hal->supportsFeature(HALFeature::MANUAL_CONTROL));
}

TEST_F(CanOpenHALTest, GetStatusBeforeInit) {
    auto hal = createHAL();
    std::string status = hal->getStatus();
    EXPECT_NE(status.find("Not Initialized"), std::string::npos);
}

TEST_F(CanOpenHALTest, ErrorMessagesInitiallyEmpty) {
    auto hal = createHAL();
    EXPECT_TRUE(hal->getErrorMessages().empty());
}

TEST_F(CanOpenHALTest, ClearErrorsDoesNotCrash) {
    auto hal = createHAL();
    hal->clearErrors();
    EXPECT_TRUE(hal->getErrorMessages().empty());
}

// ============================================================================
// CanOpenHAL start/stop lifecycle tests
// ============================================================================

TEST_F(CanOpenHALTest, StartFailsWhenNotInitialized) {
    auto hal = createHAL();
    EXPECT_FALSE(hal->start());
    EXPECT_FALSE(hal->isRunning());
}

TEST_F(CanOpenHALTest, StartAfterInitSucceeds) {
    auto hal = createHAL();
    EXPECT_TRUE(hal->initialize(config_));
    EXPECT_TRUE(hal->start());
    EXPECT_TRUE(hal->isRunning());
}

TEST_F(CanOpenHALTest, Stop) {
    auto hal = createHAL();
    EXPECT_TRUE(hal->initialize(config_));
    EXPECT_TRUE(hal->start());
    EXPECT_TRUE(hal->isRunning());

    EXPECT_TRUE(hal->stop());
    EXPECT_FALSE(hal->isRunning());
}

TEST_F(CanOpenHALTest, Shutdown) {
    auto hal = createHAL();
    EXPECT_TRUE(hal->initialize(config_));
    EXPECT_TRUE(hal->start());

    hal->shutdown();
    EXPECT_FALSE(hal->isInitialized());
    EXPECT_FALSE(hal->isRunning());
}

TEST_F(CanOpenHALTest, ShutdownStopsRunning) {
    auto hal = createHAL();
    EXPECT_TRUE(hal->initialize(config_));
    EXPECT_TRUE(hal->start());
    EXPECT_TRUE(hal->isRunning());

    hal->shutdown();
    EXPECT_FALSE(hal->isRunning());
}

TEST_F(CanOpenHALTest, DoubleInitializeIsSafe) {
    auto hal = createHAL();
    EXPECT_TRUE(hal->initialize(config_));
    // Second init should be safe (idempotent or returns false)
    bool second_init = hal->initialize(config_);
    EXPECT_TRUE(hal->isInitialized());
}

// ============================================================================
// Component creation tests
// ============================================================================

TEST_F(CanOpenHALTest, CreateMotorControl) {
    auto hal = createHAL();
    EXPECT_TRUE(hal->initialize(config_));

    auto motor = hal->createMotorControl(0);
    EXPECT_NE(motor, nullptr);
}

TEST_F(CanOpenHALTest, CreateMotorControlReturnsNonNullEvenWhenNotInitialized) {
    auto hal = createHAL();
    // CanOpenHAL allows creating motor controls before initialize()
    EXPECT_NE(hal->createMotorControl(0), nullptr);
}

TEST_F(CanOpenHALTest, CreateMotorControlInvalidAxis) {
    auto hal = createHAL();
    EXPECT_TRUE(hal->initialize(config_));
    EXPECT_EQ(hal->createMotorControl(-1), nullptr);
    EXPECT_EQ(hal->createMotorControl(10), nullptr);
}

TEST_F(CanOpenHALTest, CreateEncoderReader) {
    auto hal = createHAL();
    EXPECT_TRUE(hal->initialize(config_));

    auto encoder = hal->createEncoderReader(0);
    EXPECT_NE(encoder, nullptr);
}

TEST_F(CanOpenHALTest, CreateEncoderReaderReturnsNonNullEvenWhenNotInitialized) {
    auto hal = createHAL();
    // CanOpenHAL allows creating encoder readers before initialize()
    EXPECT_NE(hal->createEncoderReader(0), nullptr);
}

TEST_F(CanOpenHALTest, CreateEncoderReaderInvalidAxis) {
    auto hal = createHAL();
    EXPECT_TRUE(hal->initialize(config_));
    EXPECT_EQ(hal->createEncoderReader(-1), nullptr);
    EXPECT_EQ(hal->createEncoderReader(10), nullptr);
}

TEST_F(CanOpenHALTest, CreateSafetyMonitor) {
    auto hal = createHAL();
    EXPECT_TRUE(hal->initialize(config_));

    auto monitor = hal->createSafetyMonitor();
    EXPECT_NE(monitor, nullptr);
}

TEST_F(CanOpenHALTest, CreateSafetyMonitorInitialize) {
    auto hal = createHAL();
    EXPECT_TRUE(hal->initialize(config_));

    auto monitor = hal->createSafetyMonitor();
    ASSERT_NE(monitor, nullptr);

    SafetyConfig safety_config;
    EXPECT_TRUE(monitor->initialize(safety_config));
    EXPECT_TRUE(monitor->isInitialized());
}

TEST_F(CanOpenHALTest, CreateSafetyMonitorCheckLimits) {
    auto hal = createHAL();
    EXPECT_TRUE(hal->initialize(config_));

    auto monitor = hal->createSafetyMonitor();
    ASSERT_NE(monitor, nullptr);

    SafetyConfig safety_config;
    monitor->initialize(safety_config);

    EXPECT_TRUE(monitor->checkLimits(0));
    EXPECT_TRUE(monitor->checkLimits(1));
}

TEST_F(CanOpenHALTest, CreateSafetyMonitorEmergencyStop) {
    auto hal = createHAL();
    EXPECT_TRUE(hal->initialize(config_));

    auto monitor = hal->createSafetyMonitor();
    ASSERT_NE(monitor, nullptr);

    SafetyConfig safety_config;
    monitor->initialize(safety_config);

    EXPECT_TRUE(monitor->emergencyStop(0));
    EXPECT_TRUE(monitor->emergencyStop(1));
}

TEST_F(CanOpenHALTest, CreateSensorInterface) {
    auto hal = createHAL();
    EXPECT_TRUE(hal->initialize(config_));

    auto sensor = hal->createSensorInterface();
    EXPECT_NE(sensor, nullptr);
}

TEST_F(CanOpenHALTest, CreateSensorInterfaceInitialize) {
    auto hal = createHAL();
    EXPECT_TRUE(hal->initialize(config_));

    auto sensor = hal->createSensorInterface();
    ASSERT_NE(sensor, nullptr);

    SensorConfig sensor_config;
    EXPECT_TRUE(sensor->initialize(sensor_config));
    EXPECT_TRUE(sensor->isInitialized());
}

TEST_F(CanOpenHALTest, CreateSensorInterfaceRead) {
    auto hal = createHAL();
    EXPECT_TRUE(hal->initialize(config_));

    auto sensor = hal->createSensorInterface();
    ASSERT_NE(sensor, nullptr);

    SensorConfig sensor_config;
    sensor->initialize(sensor_config);

    auto reading = sensor->read(0);
    EXPECT_GE(reading.value, 0.0);
}

// ============================================================================
// CanOpenHAL derotator tests
// ============================================================================

TEST_F(CanOpenHALTest, CreateDerotatorMotor) {
    auto hal = createHAL();
    EXPECT_TRUE(hal->initialize(config_));

    auto motor = hal->createDerotatorMotor();
    EXPECT_NE(motor, nullptr);
}

TEST_F(CanOpenHALTest, CreateDerotatorEncoder) {
    auto hal = createHAL();
    EXPECT_TRUE(hal->initialize(config_));

    auto encoder = hal->createDerotatorEncoder();
    EXPECT_NE(encoder, nullptr);
}

// ============================================================================
// CanOpenHAL CANopen-specific methods
// ============================================================================

TEST_F(CanOpenHALTest, SendNMT) {
    auto hal = createHAL();
    EXPECT_TRUE(hal->initialize(config_));

    // Start remote node (NMT command 0x01)
    EXPECT_TRUE(hal->sendNMT(1, 0x01));
}

TEST_F(CanOpenHALTest, CheckConnection) {
    auto hal = createHAL();
    EXPECT_TRUE(hal->initialize(config_));

    EXPECT_TRUE(hal->checkConnection());
}

} // namespace test
} // namespace hal
} // namespace astro_mount
