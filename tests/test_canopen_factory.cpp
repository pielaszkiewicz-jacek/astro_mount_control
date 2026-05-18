#include "controllers/canopen_factory.h"
#include "controllers/icanopen_interface.h"
#include "logging/logger.h"
#include <gtest/gtest.h>
#include <memory>

namespace astro_mount {
namespace controllers {
namespace test {

// ============================================================================
// Test Fixture
// ============================================================================
class CanOpenFactoryTest : public ::testing::Test {
protected:
    void SetUp() override {
        logging::Logger::init("");
    }

    void TearDown() override {
        logging::Logger::shutdown();
    }
};

// ============================================================================
// Part 1: Factory creation methods
// ============================================================================

TEST_F(CanOpenFactoryTest, CreateWithLibraryName) {
    auto instance = CanOpenFactory::create("mock");
    EXPECT_NE(instance, nullptr);
    EXPECT_TRUE(instance->supportsSimulation());
}

TEST_F(CanOpenFactoryTest, CreateWithEmptyLibraryName) {
    // Empty library name should use default ("mock" without HAVE_CANOPEN)
    auto instance = CanOpenFactory::create("");
    EXPECT_NE(instance, nullptr);
}

TEST_F(CanOpenFactoryTest, CreateWithConfigObject) {
    ICanOpenInterface::Config config;
    config.library = "mock";
    config.interface_name = "vcan0";
    config.bitrate = 250000;
    config.node_id = 1;

    auto instance = CanOpenFactory::create(config);
    EXPECT_NE(instance, nullptr);
}

TEST_F(CanOpenFactoryTest, CreateWithUnsupportedLibrary) {
    auto instance = CanOpenFactory::create("nonexistent_library");
    EXPECT_EQ(instance, nullptr);
}

// ============================================================================
// Part 2: Supported libraries
// ============================================================================

TEST_F(CanOpenFactoryTest, GetSupportedLibrariesContainsMock) {
    auto libraries = CanOpenFactory::getSupportedLibraries();
    EXPECT_FALSE(libraries.empty());

    bool has_mock = false;
    for (const auto& lib : libraries) {
        if (lib == "mock") {
            has_mock = true;
            break;
        }
    }
    EXPECT_TRUE(has_mock);
}

TEST_F(CanOpenFactoryTest, IsLibrarySupported) {
    EXPECT_TRUE(CanOpenFactory::isLibrarySupported("mock"));
    EXPECT_FALSE(CanOpenFactory::isLibrarySupported("nonexistent"));
}

// ============================================================================
// Part 3: Default library
// ============================================================================

TEST_F(CanOpenFactoryTest, GetDefaultLibrary) {
    std::string default_lib = CanOpenFactory::getDefaultLibrary();
    EXPECT_FALSE(default_lib.empty());
    // Without HAVE_CANOPEN, should be "mock"
    EXPECT_TRUE(default_lib == "mock" || default_lib == "canopensocket");
}

// ============================================================================
// Part 4: Created instance behavior (TestCanOpenService)
// ============================================================================

TEST_F(CanOpenFactoryTest, CreatedInstanceImplementsICanOpenInterface) {
    auto instance = CanOpenFactory::create("mock");
    ASSERT_NE(instance, nullptr);

    // Type identification
    EXPECT_EQ(instance->getImplementationType(), "test-service");
    EXPECT_TRUE(instance->supportsSimulation());
}

TEST_F(CanOpenFactoryTest, CreatedInstanceInitializeAndConnect) {
    auto instance = CanOpenFactory::create("mock");
    ASSERT_NE(instance, nullptr);

    ICanOpenInterface::Config config;
    config.library = "mock";
    config.interface_name = "vcan0";

    EXPECT_TRUE(instance->initialize(config));
    EXPECT_FALSE(instance->isConnected());

    EXPECT_TRUE(instance->connect());
    EXPECT_TRUE(instance->isConnected());
}

TEST_F(CanOpenFactoryTest, CreatedInstanceDisconnectResetsState) {
    auto instance = CanOpenFactory::create("mock");
    ASSERT_NE(instance, nullptr);

    ICanOpenInterface::Config config;
    config.library = "mock";
    instance->initialize(config);
    instance->connect();

    EXPECT_TRUE(instance->isConnected());

    instance->disconnect();
    EXPECT_FALSE(instance->isConnected());
}

TEST_F(CanOpenFactoryTest, CreatedInstanceEnableAndDisableDrive) {
    auto instance = CanOpenFactory::create("mock");
    ASSERT_NE(instance, nullptr);

    ICanOpenInterface::Config config;
    instance->initialize(config);
    instance->connect();

    // Enable axis 0
    EXPECT_TRUE(instance->enableDrive(0));
    auto status = instance->getDriveStatus(0);
    EXPECT_TRUE(status.enabled);

    // Disable axis 0
    instance->disableDrive(0);
    status = instance->getDriveStatus(0);
    EXPECT_FALSE(status.enabled);
}

TEST_F(CanOpenFactoryTest, CreatedInstanceSetPositionTarget) {
    auto instance = CanOpenFactory::create("mock");
    ASSERT_NE(instance, nullptr);

    ICanOpenInterface::Config config;
    instance->initialize(config);
    instance->connect();
    instance->enableDrive(0);

    EXPECT_TRUE(instance->setPositionTarget(0, 45.0, 2.0, 1.0));
    auto position = instance->getPositionData(0);
    EXPECT_DOUBLE_EQ(position.actual_position, 45.0);
}

TEST_F(CanOpenFactoryTest, CreatedInstanceSetVelocityTarget) {
    auto instance = CanOpenFactory::create("mock");
    ASSERT_NE(instance, nullptr);

    ICanOpenInterface::Config config;
    instance->initialize(config);
    instance->connect();
    instance->enableDrive(0);

    EXPECT_TRUE(instance->setVelocityTarget(0, 1.5, 0.5));
    auto position = instance->getPositionData(0);
    EXPECT_DOUBLE_EQ(position.actual_velocity, 1.5);
}

TEST_F(CanOpenFactoryTest, CreatedInstanceStopAndEmergencyStop) {
    auto instance = CanOpenFactory::create("mock");
    ASSERT_NE(instance, nullptr);

    ICanOpenInterface::Config config;
    instance->initialize(config);
    instance->connect();
    instance->enableDrive(0);
    instance->setVelocityTarget(0, 1.0, 0.5);

    // Stop axis
    instance->stopAxis(0);
    auto position = instance->getPositionData(0);
    EXPECT_DOUBLE_EQ(position.actual_velocity, 0.0);

    // Emergency stop
    instance->setVelocityTarget(0, 1.0, 0.5);
    instance->emergencyStop(0);
    auto status = instance->getDriveStatus(0);
    EXPECT_FALSE(status.enabled);
}

TEST_F(CanOpenFactoryTest, CreatedInstanceClearErrors) {
    auto instance = CanOpenFactory::create("mock");
    ASSERT_NE(instance, nullptr);

    ICanOpenInterface::Config config;
    instance->initialize(config);

    EXPECT_TRUE(instance->clearErrors(0));
}

TEST_F(CanOpenFactoryTest, CreatedInstanceConfigureDrive) {
    auto instance = CanOpenFactory::create("mock");
    ASSERT_NE(instance, nullptr);

    ICanOpenInterface::Config config;
    instance->initialize(config);

    EXPECT_TRUE(instance->configureDrive(0, "mode=position,max_current=2.0"));
}

TEST_F(CanOpenFactoryTest, CreatedInstanceGetEncoderData) {
    auto instance = CanOpenFactory::create("mock");
    ASSERT_NE(instance, nullptr);

    ICanOpenInterface::Config config;
    instance->initialize(config);
    instance->connect();
    instance->enableDrive(0);
    instance->setPositionTarget(0, 90.0, 2.0, 1.0);

    auto encoder = instance->getEncoderData(0);
    // Raw position should reflect actual position * 1000
    EXPECT_GT(encoder.raw_position, 0);
}

TEST_F(CanOpenFactoryTest, CreatedInstanceSDOAndPDOOperations) {
    auto instance = CanOpenFactory::create("mock");
    ASSERT_NE(instance, nullptr);

    ICanOpenInterface::Config config;
    instance->initialize(config);

    // SDO
    EXPECT_TRUE(instance->sendSDO(0, 0x6040, 0, nullptr, 0));
    EXPECT_EQ(instance->receiveSDO(0, 0x6041, 0, nullptr, 0), 0);

    // PDO
    EXPECT_TRUE(instance->configurePDO(0, 1, {0x60400010, 0x60600008}));
    EXPECT_NO_THROW(instance->enablePDO(0, 1, true));
}

TEST_F(CanOpenFactoryTest, CreatedInstanceNMTAndSync) {
    auto instance = CanOpenFactory::create("mock");
    ASSERT_NE(instance, nullptr);

    ICanOpenInterface::Config config;
    instance->initialize(config);

    EXPECT_TRUE(instance->sendNMT(1, 0x01));  // Start node
    EXPECT_NO_THROW(instance->sendSync());
}

TEST_F(CanOpenFactoryTest, CreatedInstanceSaveAndLoadConfiguration) {
    auto instance = CanOpenFactory::create("mock");
    ASSERT_NE(instance, nullptr);

    ICanOpenInterface::Config config;
    instance->initialize(config);

    EXPECT_TRUE(instance->saveConfiguration("/tmp/test_canopen_config.json"));
    EXPECT_TRUE(instance->loadConfiguration("/tmp/test_canopen_config.json"));
}

TEST_F(CanOpenFactoryTest, CreatedInstanceGetStatistics) {
    auto instance = CanOpenFactory::create("mock");
    ASSERT_NE(instance, nullptr);

    ICanOpenInterface::Config config;
    instance->initialize(config);
    instance->connect();

    std::string stats = instance->getStatistics();
    EXPECT_FALSE(stats.empty());
    EXPECT_NE(stats.find("Connected"), std::string::npos);
    EXPECT_NE(stats.find("Test CANopen Service"), std::string::npos);
}

TEST_F(CanOpenFactoryTest, CreatedInstanceShutdown) {
    auto instance = CanOpenFactory::create("mock");
    ASSERT_NE(instance, nullptr);

    ICanOpenInterface::Config config;
    instance->initialize(config);
    instance->connect();
    instance->enableDrive(0);

    EXPECT_NO_THROW(instance->shutdown());
    EXPECT_FALSE(instance->isConnected());
}

TEST_F(CanOpenFactoryTest, CreatedInstanceInvalidAxisHandling) {
    auto instance = CanOpenFactory::create("mock");
    ASSERT_NE(instance, nullptr);

    ICanOpenInterface::Config config;
    instance->initialize(config);

    // Operations on invalid axis should not crash
    EXPECT_FALSE(instance->enableDrive(5));        // Out of range
    EXPECT_NO_THROW(instance->disableDrive(5));     // Out of range
    EXPECT_FALSE(instance->setPositionTarget(5, 0, 0, 0));  // Out of range

    // Invalid axis getDriveStatus should still work (returns default)
    auto status = instance->getDriveStatus(5);
    EXPECT_FALSE(status.operational);
}

TEST_F(CanOpenFactoryTest, CreatedInstanceSetStatusCallbacks) {
    auto instance = CanOpenFactory::create("mock");
    ASSERT_NE(instance, nullptr);

    ICanOpenInterface::Config config;
    instance->initialize(config);

    EXPECT_NO_THROW(instance->setStatusCallback(nullptr));
    EXPECT_NO_THROW(instance->setPositionCallback(nullptr));
    EXPECT_NO_THROW(instance->setEncoderCallback(nullptr));
    EXPECT_NO_THROW(instance->setErrorCallback(nullptr));
}

// ============================================================================
// Part 5: Trajectory generation
// ============================================================================

TEST_F(CanOpenFactoryTest, GenerateTrajectory) {
    auto instance = CanOpenFactory::create("mock");
    ASSERT_NE(instance, nullptr);

    ICanOpenInterface::TrajectoryParams params;
    params.type = ICanOpenInterface::TrajectoryType::TRAPEZOIDAL;
    params.max_velocity = 5.0;
    params.max_acceleration = 2.0;
    params.max_jerk = 0.5;
    params.start_position = 0.0;
    params.target_position = 90.0;
    params.update_rate = 10.0;  // Hz

    auto trajectory = instance->generateTrajectory(params);
    EXPECT_FALSE(trajectory.empty());

    // Last point should be at target
    const auto& last = trajectory.back();
    EXPECT_NEAR(last.position, 90.0, 1.0);
    EXPECT_GE(last.time, 0);
}

TEST_F(CanOpenFactoryTest, ExecuteTrajectory) {
    auto instance = CanOpenFactory::create("mock");
    ASSERT_NE(instance, nullptr);

    ICanOpenInterface::Config config;
    instance->initialize(config);
    instance->connect();
    instance->enableDrive(0);

    // Generate trajectory
    ICanOpenInterface::TrajectoryParams params;
    params.max_velocity = 5.0;
    params.max_acceleration = 2.0;
    params.start_position = 0.0;
    params.target_position = 90.0;
    params.update_rate = 10.0;

    auto trajectory = instance->generateTrajectory(params);

    // Execute trajectory
    EXPECT_TRUE(instance->executeTrajectory(0, trajectory));

    // Position should now match the last trajectory point
    auto position = instance->getPositionData(0);
    EXPECT_NEAR(position.actual_position, 90.0, 1.0);
}

TEST_F(CanOpenFactoryTest, ExecuteTrajectoryOnDisabledAxis) {
    auto instance = CanOpenFactory::create("mock");
    ASSERT_NE(instance, nullptr);

    ICanOpenInterface::Config config;
    instance->initialize(config);

    ICanOpenInterface::TrajectoryParams params;
    params.max_velocity = 5.0;
    params.start_position = 0.0;
    params.target_position = 90.0;
    params.update_rate = 10.0;

    auto trajectory = instance->generateTrajectory(params);

    // Axis not enabled - should return false
    EXPECT_FALSE(instance->executeTrajectory(0, trajectory));
}

TEST_F(CanOpenFactoryTest, ExecuteTrajectoryOnInvalidAxis) {
    auto instance = CanOpenFactory::create("mock");
    ASSERT_NE(instance, nullptr);

    ICanOpenInterface::TrajectoryParams params;
    params.max_velocity = 5.0;
    params.start_position = 0.0;
    params.target_position = 90.0;
    params.update_rate = 10.0;

    auto trajectory = instance->generateTrajectory(params);

    EXPECT_FALSE(instance->executeTrajectory(5, trajectory));
}

} // namespace test
} // namespace controllers
} // namespace astro_mount
