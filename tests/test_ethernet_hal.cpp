#include <gtest/gtest.h>
#include "hal/ethernet_hal/ethernet_hal.h"
#include "hal/safety_monitor.h"
#include "hal/sensor_interface.h"

namespace astro_mount {
namespace hal {
namespace test {

// ============================================================================
// EthernetHAL construction and basic info (no network needed)
// ============================================================================

TEST(EthernetHALTest, ConstructorDoesNotConnect) {
    HALConfig config;
    config.ethernet.ip_address = "192.168.1.100";
    config.ethernet.port = 502;

    EthernetHAL hal(config);
    EXPECT_FALSE(hal.isInitialized());
    EXPECT_FALSE(hal.isRunning());
}

TEST(EthernetHALTest, DestructorDoesNotCrash) {
    HALConfig config;
    config.ethernet.ip_address = "192.168.1.100";
    config.ethernet.port = 502;

    EthernetHAL* hal = new EthernetHAL(config);
    delete hal;
}

TEST(EthernetHALTest, GetPlatformName) {
    HALConfig config;
    config.ethernet.ip_address = "192.168.1.100";
    config.ethernet.port = 502;

    EthernetHAL hal(config);
    std::string name = hal.getPlatformName();
    EXPECT_NE(name.find("EthernetHAL"), std::string::npos);
    EXPECT_NE(name.find("192.168.1.100"), std::string::npos);
}

TEST(EthernetHALTest, GetHardwareVersion) {
    HALConfig config;
    EthernetHAL hal(config);
    EXPECT_EQ(hal.getHardwareVersion(), "1.0.0");
}

TEST(EthernetHALTest, GetSupportedFeatures) {
    HALConfig config;
    EthernetHAL hal(config);
    auto features = hal.getSupportedFeatures();
    EXPECT_GT(features.size(), 0);

    EXPECT_TRUE(hal.supportsFeature(HALFeature::ETHERNET_SUPPORT));
    EXPECT_TRUE(hal.supportsFeature(HALFeature::PID_CONTROL));
    EXPECT_TRUE(hal.supportsFeature(HALFeature::ENCODER_FEEDBACK));
    EXPECT_TRUE(hal.supportsFeature(HALFeature::SAFETY_MONITORING));
    EXPECT_TRUE(hal.supportsFeature(HALFeature::SENSOR_MONITORING));
    EXPECT_TRUE(hal.supportsFeature(HALFeature::DEROTATOR_SUPPORT));
    EXPECT_FALSE(hal.supportsFeature(HALFeature::CANOPEN_SUPPORT));
    EXPECT_FALSE(hal.supportsFeature(HALFeature::SERIAL_SUPPORT));
    EXPECT_FALSE(hal.supportsFeature(HALFeature::MANUAL_CONTROL));
}

TEST(EthernetHALTest, GetStatusBeforeInit) {
    HALConfig config;
    EthernetHAL hal(config);
    std::string status = hal.getStatus();
    EXPECT_NE(status.find("UNINITIALIZED"), std::string::npos);
}

TEST(EthernetHALTest, ErrorMessagesInitiallyEmpty) {
    HALConfig config;
    EthernetHAL hal(config);
    EXPECT_TRUE(hal.getErrorMessages().empty());
}

TEST(EthernetHALTest, ClearErrorsDoesNotCrash) {
    HALConfig config;
    EthernetHAL hal(config);
    hal.clearErrors();
    EXPECT_TRUE(hal.getErrorMessages().empty());
}

TEST(EthernetHALTest, InitializeSucceedsEvenWithoutConnection) {
    HALConfig config;
    config.ethernet.ip_address = "192.168.1.100";
    config.ethernet.port = 502;

    EthernetHAL hal(config);
    // initialize() doesn't fail if connectSocket() fails - it just logs the error
    bool result = hal.initialize(config);
    EXPECT_TRUE(result);
    EXPECT_TRUE(hal.isInitialized());
}

// ============================================================================
// Component creation without initialization
// ============================================================================

TEST(EthernetHALTest, CreateMotorControlReturnsNullWhenNotInitialized) {
    HALConfig config;
    EthernetHAL hal(config);
    EXPECT_EQ(hal.createMotorControl(0), nullptr);
    EXPECT_EQ(hal.createMotorControl(1), nullptr);
    EXPECT_EQ(hal.createMotorControl(2), nullptr);
}

TEST(EthernetHALTest, CreateMotorControlInvalidAxis) {
    HALConfig config;
    EthernetHAL hal(config);
    EXPECT_EQ(hal.createMotorControl(-1), nullptr);
    EXPECT_EQ(hal.createMotorControl(3), nullptr);
}

TEST(EthernetHALTest, CreateEncoderReaderReturnsNullWhenNotInitialized) {
    HALConfig config;
    EthernetHAL hal(config);
    EXPECT_EQ(hal.createEncoderReader(0), nullptr);
    EXPECT_EQ(hal.createEncoderReader(1), nullptr);
    EXPECT_EQ(hal.createEncoderReader(2), nullptr);
}

TEST(EthernetHALTest, CreateEncoderReaderInvalidAxis) {
    HALConfig config;
    EthernetHAL hal(config);
    EXPECT_EQ(hal.createEncoderReader(-1), nullptr);
    EXPECT_EQ(hal.createEncoderReader(3), nullptr);
}

// ============================================================================
// SafetyMonitor stub (EthernetSafetyMonitor) - fully testable without hardware
// ============================================================================

TEST(EthernetHALTest, CreateSafetyMonitorAlwaysReturnsValid) {
    HALConfig config;
    EthernetHAL hal(config);
    auto monitor = hal.createSafetyMonitor();
    EXPECT_NE(monitor, nullptr);
}

TEST(EthernetHALTest, SafetyMonitorInitialize) {
    HALConfig config;
    EthernetHAL hal(config);
    auto monitor = hal.createSafetyMonitor();

    SafetyConfig safety_config;
    EXPECT_TRUE(monitor->initialize(safety_config));
    EXPECT_TRUE(monitor->isInitialized());
}

TEST(EthernetHALTest, SafetyMonitorShutdown) {
    HALConfig config;
    EthernetHAL hal(config);
    auto monitor = hal.createSafetyMonitor();

    SafetyConfig safety_config;
    monitor->initialize(safety_config);
    EXPECT_TRUE(monitor->isInitialized());

    monitor->shutdown();
    EXPECT_FALSE(monitor->isInitialized());
}

TEST(EthernetHALTest, SafetyMonitorGetStatus) {
    HALConfig config;
    EthernetHAL hal(config);
    auto monitor = hal.createSafetyMonitor();

    SafetyConfig safety_config;
    monitor->initialize(safety_config);

    SafetyStatus status = monitor->getStatus();
    EXPECT_EQ(status.overall_state, SafetyStatus::State::NORMAL);
    EXPECT_TRUE(status.safety_circuit_ok);
}

TEST(EthernetHALTest, SafetyMonitorCheckLimits) {
    HALConfig config;
    EthernetHAL hal(config);
    auto monitor = hal.createSafetyMonitor();

    EXPECT_TRUE(monitor->checkLimits(0));
    EXPECT_TRUE(monitor->checkLimits(1));
}

TEST(EthernetHALTest, SafetyMonitorEmergencyStop) {
    HALConfig config;
    EthernetHAL hal(config);
    auto monitor = hal.createSafetyMonitor();

    EXPECT_TRUE(monitor->emergencyStop(0));
    EXPECT_TRUE(monitor->emergencyStop(1));
}

TEST(EthernetHALTest, SafetyMonitorClearErrors) {
    HALConfig config;
    EthernetHAL hal(config);
    auto monitor = hal.createSafetyMonitor();

    EXPECT_TRUE(monitor->clearErrors(0));
}

TEST(EthernetHALTest, SafetyMonitorSetCallbacks) {
    HALConfig config;
    EthernetHAL hal(config);
    auto monitor = hal.createSafetyMonitor();

    bool limit_called = false;
    monitor->setLimitCallback([&limit_called](int, const std::string&, double) {
        limit_called = true;
    });

    bool error_called = false;
    monitor->setErrorCallback([&error_called](int, const std::string&) {
        error_called = true;
    });

    SUCCEED();
}

TEST(EthernetHALTest, SafetyMonitorGetDiagnostics) {
    HALConfig config;
    EthernetHAL hal(config);
    auto monitor = hal.createSafetyMonitor();

    std::string diag = monitor->getDiagnostics();
    EXPECT_FALSE(diag.empty());
}

TEST(EthernetHALTest, SafetyMonitorInitializeMultipleTimes) {
    HALConfig config;
    EthernetHAL hal(config);
    auto monitor = hal.createSafetyMonitor();
    SafetyConfig safety_config;

    EXPECT_TRUE(monitor->initialize(safety_config));
    EXPECT_TRUE(monitor->initialize(safety_config));
    EXPECT_TRUE(monitor->isInitialized());
}

// ============================================================================
// SensorInterface stub (EthernetSensorInterface) - fully testable without hardware
// ============================================================================

TEST(EthernetHALTest, CreateSensorInterfaceAlwaysReturnsValid) {
    HALConfig config;
    EthernetHAL hal(config);
    auto sensor = hal.createSensorInterface();
    EXPECT_NE(sensor, nullptr);
}

TEST(EthernetHALTest, SensorInterfaceInitialize) {
    HALConfig config;
    EthernetHAL hal(config);
    auto sensor = hal.createSensorInterface();

    SensorConfig sensor_config;
    EXPECT_TRUE(sensor->initialize(sensor_config));
    EXPECT_TRUE(sensor->isInitialized());
}

TEST(EthernetHALTest, SensorInterfaceShutdown) {
    HALConfig config;
    EthernetHAL hal(config);
    auto sensor = hal.createSensorInterface();

    SensorConfig sensor_config;
    sensor->initialize(sensor_config);
    EXPECT_TRUE(sensor->isInitialized());

    sensor->shutdown();
    EXPECT_FALSE(sensor->isInitialized());
}

TEST(EthernetHALTest, SensorInterfaceRead) {
    HALConfig config;
    EthernetHAL hal(config);
    auto sensor = hal.createSensorInterface();

    SensorConfig sensor_config;
    sensor->initialize(sensor_config);

    SensorReading reading = sensor->read(0);
    EXPECT_TRUE(reading.valid);
    EXPECT_EQ(reading.sensor_id, 0);
    EXPECT_EQ(reading.type, SensorType::TEMPERATURE);
    EXPECT_DOUBLE_EQ(reading.value, 25.0);
}

TEST(EthernetHALTest, SensorInterfaceReadAll) {
    HALConfig config;
    EthernetHAL hal(config);
    auto sensor = hal.createSensorInterface();

    SensorConfig sensor_config;
    sensor->initialize(sensor_config);

    auto readings = sensor->readAll();
    EXPECT_EQ(readings.size(), 1);
    EXPECT_TRUE(readings[0].valid);
}

TEST(EthernetHALTest, SensorInterfaceCalibrate) {
    HALConfig config;
    EthernetHAL hal(config);
    auto sensor = hal.createSensorInterface();

    EXPECT_TRUE(sensor->calibrate(0, 25.0));
    EXPECT_TRUE(sensor->autoCalibrate(0));
}

TEST(EthernetHALTest, SensorInterfaceSetCallbacks) {
    HALConfig config;
    EthernetHAL hal(config);
    auto sensor = hal.createSensorInterface();

    sensor->setReadingCallback([](const SensorReading&) {});
    sensor->setErrorCallback([](int, const std::string&) {});
    SUCCEED();
}

TEST(EthernetHALTest, SensorInterfaceGetDiagnostics) {
    HALConfig config;
    EthernetHAL hal(config);
    auto sensor = hal.createSensorInterface();

    std::string diag = sensor->getDiagnostics();
    EXPECT_FALSE(diag.empty());
}

// ============================================================================
// Start/Stop without initialization
// ============================================================================

TEST(EthernetHALTest, StartFailsWhenNotInitialized) {
    HALConfig config;
    EthernetHAL hal(config);
    EXPECT_FALSE(hal.start());
}

TEST(EthernetHALTest, StopIsIdempotent) {
    HALConfig config;
    EthernetHAL hal(config);
    EXPECT_TRUE(hal.stop());
    EXPECT_TRUE(hal.stop());
}

} // namespace test
} // namespace hal
} // namespace astro_mount
