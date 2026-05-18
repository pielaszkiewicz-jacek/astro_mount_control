#include <gtest/gtest.h>
#include "hal/serial_hal/serial_hal.h"
#include "hal/safety_monitor.h"
#include "hal/sensor_interface.h"

namespace astro_mount {
namespace hal {
namespace test {

// ============================================================================
// SerialHAL construction and basic info (no serial port needed)
// ============================================================================

TEST(SerialHALTest, ConstructorDoesNotOpenPort) {
    HALConfig config;
    config.serial.port = "/dev/ttyUSB0";
    config.serial.baud_rate = 115200;

    SerialHAL hal(config);
    EXPECT_FALSE(hal.isInitialized());
    EXPECT_FALSE(hal.isRunning());
}

TEST(SerialHALTest, DestructorDoesNotCrash) {
    HALConfig config;
    config.serial.port = "/dev/ttyUSB0";

    SerialHAL* hal = new SerialHAL(config);
    delete hal;
}

TEST(SerialHALTest, GetPlatformName) {
    HALConfig config;
    config.serial.port = "/dev/ttyUSB0";
    config.serial.baud_rate = 115200;

    SerialHAL hal(config);
    std::string name = hal.getPlatformName();
    EXPECT_NE(name.find("SerialHAL"), std::string::npos);
    EXPECT_NE(name.find("/dev/ttyUSB0"), std::string::npos);
}

TEST(SerialHALTest, GetHardwareVersion) {
    HALConfig config;
    SerialHAL hal(config);
    EXPECT_EQ(hal.getHardwareVersion(), "1.0.0");
}

TEST(SerialHALTest, GetSupportedFeatures) {
    HALConfig config;
    SerialHAL hal(config);
    auto features = hal.getSupportedFeatures();
    EXPECT_GT(features.size(), 0);

    EXPECT_TRUE(hal.supportsFeature(HALFeature::SERIAL_SUPPORT));
    EXPECT_TRUE(hal.supportsFeature(HALFeature::PID_CONTROL));
    EXPECT_TRUE(hal.supportsFeature(HALFeature::ENCODER_FEEDBACK));
    EXPECT_TRUE(hal.supportsFeature(HALFeature::SAFETY_MONITORING));
    EXPECT_TRUE(hal.supportsFeature(HALFeature::SENSOR_MONITORING));
    EXPECT_TRUE(hal.supportsFeature(HALFeature::DEROTATOR_SUPPORT));
    EXPECT_FALSE(hal.supportsFeature(HALFeature::CANOPEN_SUPPORT));
    EXPECT_FALSE(hal.supportsFeature(HALFeature::ETHERNET_SUPPORT));
    EXPECT_FALSE(hal.supportsFeature(HALFeature::MANUAL_CONTROL));
}

TEST(SerialHALTest, GetStatusBeforeInit) {
    HALConfig config;
    SerialHAL hal(config);
    std::string status = hal.getStatus();
    EXPECT_NE(status.find("UNINITIALIZED"), std::string::npos);
}

TEST(SerialHALTest, ErrorMessagesInitiallyEmpty) {
    HALConfig config;
    SerialHAL hal(config);
    EXPECT_TRUE(hal.getErrorMessages().empty());
}

TEST(SerialHALTest, ClearErrorsDoesNotCrash) {
    HALConfig config;
    SerialHAL hal(config);
    hal.clearErrors();
    EXPECT_TRUE(hal.getErrorMessages().empty());
}

TEST(SerialHALTest, InitializeFailsWithoutPort) {
    HALConfig config;
    config.serial.port = "/dev/nonexistent";
    config.serial.baud_rate = 115200;

    SerialHAL hal(config);
    bool result = hal.initialize(config);
    EXPECT_FALSE(result);
    EXPECT_FALSE(hal.isInitialized());
    EXPECT_FALSE(hal.getErrorMessages().empty());
}

// ============================================================================
// Component creation without initialization (returns nullptr for motors/encoders)
// ============================================================================

TEST(SerialHALTest, CreateMotorControlReturnsNullWhenNotInitialized) {
    HALConfig config;
    SerialHAL hal(config);
    EXPECT_EQ(hal.createMotorControl(0), nullptr);
    EXPECT_EQ(hal.createMotorControl(1), nullptr);
    EXPECT_EQ(hal.createMotorControl(2), nullptr);
}

TEST(SerialHALTest, CreateMotorControlInvalidAxis) {
    HALConfig config;
    SerialHAL hal(config);
    EXPECT_EQ(hal.createMotorControl(-1), nullptr);
    EXPECT_EQ(hal.createMotorControl(3), nullptr);
}

TEST(SerialHALTest, CreateEncoderReaderReturnsNullWhenNotInitialized) {
    HALConfig config;
    SerialHAL hal(config);
    EXPECT_EQ(hal.createEncoderReader(0), nullptr);
    EXPECT_EQ(hal.createEncoderReader(1), nullptr);
    EXPECT_EQ(hal.createEncoderReader(2), nullptr);
}

TEST(SerialHALTest, CreateEncoderReaderInvalidAxis) {
    HALConfig config;
    SerialHAL hal(config);
    EXPECT_EQ(hal.createEncoderReader(-1), nullptr);
    EXPECT_EQ(hal.createEncoderReader(3), nullptr);
}

// ============================================================================
// SafetyMonitor stub (SerialSafetyMonitor) - fully testable without hardware
// ============================================================================

TEST(SerialHALTest, CreateSafetyMonitorAlwaysReturnsValid) {
    HALConfig config;
    SerialHAL hal(config);
    auto monitor = hal.createSafetyMonitor();
    EXPECT_NE(monitor, nullptr);
}

TEST(SerialHALTest, SafetyMonitorInitialize) {
    HALConfig config;
    SerialHAL hal(config);
    auto monitor = hal.createSafetyMonitor();

    SafetyConfig safety_config;
    EXPECT_TRUE(monitor->initialize(safety_config));
    EXPECT_TRUE(monitor->isInitialized());
}

TEST(SerialHALTest, SafetyMonitorShutdown) {
    HALConfig config;
    SerialHAL hal(config);
    auto monitor = hal.createSafetyMonitor();

    SafetyConfig safety_config;
    monitor->initialize(safety_config);
    EXPECT_TRUE(monitor->isInitialized());

    monitor->shutdown();
    EXPECT_FALSE(monitor->isInitialized());
}

TEST(SerialHALTest, SafetyMonitorGetStatus) {
    HALConfig config;
    SerialHAL hal(config);
    auto monitor = hal.createSafetyMonitor();

    SafetyConfig safety_config;
    monitor->initialize(safety_config);

    SafetyStatus status = monitor->getStatus();
    EXPECT_EQ(status.overall_state, SafetyStatus::State::NORMAL);
    EXPECT_TRUE(status.safety_circuit_ok);
}

TEST(SerialHALTest, SafetyMonitorCheckLimits) {
    HALConfig config;
    SerialHAL hal(config);
    auto monitor = hal.createSafetyMonitor();

    EXPECT_TRUE(monitor->checkLimits(0));
    EXPECT_TRUE(monitor->checkLimits(1));
}

TEST(SerialHALTest, SafetyMonitorEmergencyStop) {
    HALConfig config;
    SerialHAL hal(config);
    auto monitor = hal.createSafetyMonitor();

    EXPECT_TRUE(monitor->emergencyStop(0));
    EXPECT_TRUE(monitor->emergencyStop(1));
}

TEST(SerialHALTest, SafetyMonitorClearErrors) {
    HALConfig config;
    SerialHAL hal(config);
    auto monitor = hal.createSafetyMonitor();

    EXPECT_TRUE(monitor->clearErrors(0));
}

TEST(SerialHALTest, SafetyMonitorSetCallbacks) {
    HALConfig config;
    SerialHAL hal(config);
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

TEST(SerialHALTest, SafetyMonitorGetDiagnostics) {
    HALConfig config;
    SerialHAL hal(config);
    auto monitor = hal.createSafetyMonitor();

    std::string diag = monitor->getDiagnostics();
    EXPECT_FALSE(diag.empty());
}

TEST(SerialHALTest, SafetyMonitorCanInitializeMultipleTimes) {
    HALConfig config;
    SerialHAL hal(config);
    auto monitor = hal.createSafetyMonitor();
    SafetyConfig safety_config;

    EXPECT_TRUE(monitor->initialize(safety_config));
    EXPECT_TRUE(monitor->initialize(safety_config));
    EXPECT_TRUE(monitor->isInitialized());
}

// ============================================================================
// SensorInterface stub (SerialSensorInterface) - fully testable without hardware
// ============================================================================

TEST(SerialHALTest, CreateSensorInterfaceAlwaysReturnsValid) {
    HALConfig config;
    SerialHAL hal(config);
    auto sensor = hal.createSensorInterface();
    EXPECT_NE(sensor, nullptr);
}

TEST(SerialHALTest, SensorInterfaceInitialize) {
    HALConfig config;
    SerialHAL hal(config);
    auto sensor = hal.createSensorInterface();

    SensorConfig sensor_config;
    EXPECT_TRUE(sensor->initialize(sensor_config));
    EXPECT_TRUE(sensor->isInitialized());
}

TEST(SerialHALTest, SensorInterfaceShutdown) {
    HALConfig config;
    SerialHAL hal(config);
    auto sensor = hal.createSensorInterface();

    SensorConfig sensor_config;
    sensor->initialize(sensor_config);
    EXPECT_TRUE(sensor->isInitialized());

    sensor->shutdown();
    EXPECT_FALSE(sensor->isInitialized());
}

TEST(SerialHALTest, SensorInterfaceRead) {
    HALConfig config;
    SerialHAL hal(config);
    auto sensor = hal.createSensorInterface();

    SensorConfig sensor_config;
    sensor->initialize(sensor_config);

    SensorReading reading = sensor->read(0);
    EXPECT_TRUE(reading.valid);
    EXPECT_EQ(reading.sensor_id, 0);
    EXPECT_EQ(reading.type, SensorType::TEMPERATURE);
    EXPECT_DOUBLE_EQ(reading.value, 25.0);
}

TEST(SerialHALTest, SensorInterfaceReadAll) {
    HALConfig config;
    SerialHAL hal(config);
    auto sensor = hal.createSensorInterface();

    SensorConfig sensor_config;
    sensor->initialize(sensor_config);

    auto readings = sensor->readAll();
    EXPECT_EQ(readings.size(), 1);
    EXPECT_TRUE(readings[0].valid);
}

TEST(SerialHALTest, SensorInterfaceCalibrate) {
    HALConfig config;
    SerialHAL hal(config);
    auto sensor = hal.createSensorInterface();

    EXPECT_TRUE(sensor->calibrate(0, 25.0));
    EXPECT_TRUE(sensor->autoCalibrate(0));
}

TEST(SerialHALTest, SensorInterfaceSetCallbacks) {
    HALConfig config;
    SerialHAL hal(config);
    auto sensor = hal.createSensorInterface();

    sensor->setReadingCallback([](const SensorReading&) {});
    sensor->setErrorCallback([](int, const std::string&) {});
    SUCCEED();
}

TEST(SerialHALTest, SensorInterfaceGetDiagnostics) {
    HALConfig config;
    SerialHAL hal(config);
    auto sensor = hal.createSensorInterface();

    std::string diag = sensor->getDiagnostics();
    EXPECT_FALSE(diag.empty());
}

// ============================================================================
// Start/Stop without initialize (expected to fail gracefully)
// ============================================================================

TEST(SerialHALTest, StartFailsWhenNotInitialized) {
    HALConfig config;
    SerialHAL hal(config);
    EXPECT_FALSE(hal.start());
}

TEST(SerialHALTest, StopIsIdempotent) {
    HALConfig config;
    SerialHAL hal(config);
    EXPECT_TRUE(hal.stop());
    EXPECT_TRUE(hal.stop());
}

} // namespace test
} // namespace hal
} // namespace astro_mount
