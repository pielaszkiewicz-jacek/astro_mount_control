#include <gtest/gtest.h>
#include "hal/canopen_hal/canopen_hal.h"
#include "hal/sensor_interface.h"
#include "controllers/icanopen_interface.h"

#include <chrono>
#include <cmath>
#include <thread>

using namespace astro_mount;
using namespace astro_mount::hal;
using astro_mount::controllers::ICanOpenInterface;

namespace {

class MockCanOpen : public controllers::ICanOpenInterface {
public:
    bool initialize(const Config& cfg) override {
        config = cfg;
        initialized = true;
        return true;
    }
    void shutdown() override { initialized = false; }
    bool isInitialized() const override { return initialized; }

    bool enableDrive(uint8_t axis) override { last_enable_axis = axis; return true; }
    bool disableDrive(uint8_t axis) override { last_disable_axis = axis; return true; }

    bool setPositionTarget(uint8_t axis, int32_t position, uint32_t velocity,
                           uint32_t acceleration) override {
        last_axis = axis;
        last_position = position;
        last_velocity = velocity;
        last_acceleration = acceleration;
        return true;
    }
    bool setVelocityTarget(uint8_t axis, int32_t velocity, uint32_t acceleration) override {
        last_axis = axis;
        last_velocity = static_cast<uint32_t>(velocity);
        last_acceleration = acceleration;
        return true;
    }
    bool stopAxis(uint8_t axis) override { last_stop_axis = axis; return true; }
    bool emergencyStop(uint8_t axis) override { last_stop_axis = axis; return true; }

    bool getDriveStatus(uint8_t axis, controllers::CanOpenStatus& status) override {
        (void)axis;
        status.enabled = true;
        status.moving = false;
        status.target_reached = true;
        status.fault = false;
        status.actual_position = 1000;   // 1000 counts
        status.actual_velocity = 0;
        status.actual_current = 0;
        status.status_word = 0x0637;
        status.mode_display = 1;
        status.position_valid = true;
        return true;
    }
    bool getPositionData(uint8_t axis, controllers::CanOpenPositionData& data) override {
        (void)axis;
        data.actual_position = 1000;
        data.demand_position = 1000;
        data.actual_velocity = 0;
        return true;
    }
    bool clearErrors(uint8_t axis) override { (void)axis; return true; }
    bool sendSDO(uint8_t axis, uint16_t index, uint8_t subindex,
                 uint32_t value, uint8_t size) override {
        (void)axis; (void)size;
        last_sdo_index = index;
        last_sdo_sub = subindex;
        last_sdo_value = value;
        return true;
    }
    bool readSDO(uint8_t, uint16_t, uint8_t, uint32_t& value, uint8_t& size) override {
        value = 0; size = 0; return true;
    }
    bool sendNMT(uint8_t, uint8_t) override { return true; }
    bool setNodeId(uint8_t, uint8_t) override { return true; }
    bool getNodeId(uint8_t, uint8_t& id) override { id = 1; return true; }
    bool setBaudRate(uint8_t, uint8_t) override { return true; }
    bool saveParameters(uint8_t) override { return true; }
    bool setHeartbeatPeriod(uint8_t, uint16_t) override { return true; }
    bool setHomingParameters(uint8_t, uint8_t, uint32_t, uint32_t, uint32_t) override { return true; }
    bool startHoming(uint8_t axis) override { last_homing_axis = axis; return true; }
    bool configurePdo(uint8_t, bool) override { return true; }

    Config config;
    bool initialized{false};
    int last_axis{-1};
    int last_enable_axis{-1};
    int last_disable_axis{-1};
    int last_stop_axis{-1};
    int32_t last_position{0};
    uint32_t last_velocity{0};
    uint32_t last_acceleration{0};
    int last_homing_axis{-1};
    uint16_t last_sdo_index{0};
    uint8_t last_sdo_sub{0};
    uint32_t last_sdo_value{0};
};

HALConfig makeConfig() {
    HALConfig cfg;
    cfg.type = HALType::CANOPEN;
    cfg.canopen.library = "canopensocket";
    cfg.canopen.interface_name = "can0";
    cfg.axes.clear();
    for (int i = 0; i < 2; ++i) {
        HALConfig::AxisConfig axis;
        axis.id = i;
        axis.can_node_id = static_cast<uint8_t>(i + 1);
        axis.motor_config.encoder_counts_per_degree = 4000.0 / 360.0;
        axis.encoder_config.counts_per_degree = 4000.0 / 360.0;
        axis.encoder_config.resolution = 4000;
        cfg.axes.push_back(axis);
    }
    return cfg;
}

} // namespace

TEST(CanOpenHAL, InitializeCreatesComponents) {
    auto mock = std::make_unique<MockCanOpen>();
    auto* mock_ptr = mock.get();
    CanOpenHAL hal(std::move(mock));

    ASSERT_TRUE(hal.initialize(makeConfig()));
    EXPECT_TRUE(hal.isInitialized());
    EXPECT_TRUE(mock_ptr->initialized);

    auto motor = hal.createMotorControl(0);
    ASSERT_NE(motor, nullptr);
    auto encoder = hal.createEncoderReader(0);
    ASSERT_NE(encoder, nullptr);
    auto safety = hal.createSafetyMonitor();
    ASSERT_NE(safety, nullptr);
    EXPECT_EQ(hal.createSensorInterface(), nullptr);
}

TEST(CanOpenHAL, EnableCallsInterface) {
    auto mock = std::make_unique<MockCanOpen>();
    auto* mock_ptr = mock.get();
    CanOpenHAL hal(std::move(mock));
    ASSERT_TRUE(hal.initialize(makeConfig()));

    auto motor = hal.createMotorControl(0);
    ASSERT_NE(motor, nullptr);
    EXPECT_TRUE(motor->enable());
    EXPECT_TRUE(motor->isEnabled());
    EXPECT_EQ(mock_ptr->last_enable_axis, 0);
}

TEST(CanOpenHAL, SetPositionConvertsDegreesToCounts) {
    auto mock = std::make_unique<MockCanOpen>();
    auto* mock_ptr = mock.get();
    CanOpenHAL hal(std::move(mock));
    ASSERT_TRUE(hal.initialize(makeConfig()));

    auto motor = hal.createMotorControl(0);
    ASSERT_NE(motor, nullptr);
    // 90° at 4000 counts/turn = 1000 counts.
    EXPECT_TRUE(motor->setPosition(90.0, 1.0, 1.0));
    EXPECT_EQ(mock_ptr->last_axis, 0);
    EXPECT_EQ(mock_ptr->last_position, 1000);
    EXPECT_GT(mock_ptr->last_velocity, 0u);
    EXPECT_GT(mock_ptr->last_acceleration, 0u);
}

TEST(CanOpenHAL, MotorHomeCallsInterface) {
    auto mock = std::make_unique<MockCanOpen>();
    auto* mock_ptr = mock.get();
    CanOpenHAL hal(std::move(mock));
    ASSERT_TRUE(hal.initialize(makeConfig()));

    auto motor = hal.createMotorControl(0);
    ASSERT_NE(motor, nullptr);
    EXPECT_TRUE(motor->home());
    EXPECT_EQ(mock_ptr->last_homing_axis, 0);
}

TEST(CanOpenHAL, WritePidLoopRamMapsToObject2008) {
    auto mock = std::make_unique<MockCanOpen>();
    auto* mock_ptr = mock.get();
    CanOpenHAL hal(std::move(mock));
    ASSERT_TRUE(hal.initialize(makeConfig()));

    auto motor = hal.createMotorControl(0);
    ASSERT_NE(motor, nullptr);
    EXPECT_TRUE(motor->writePidLoopRam(3, 0.5, 0.0, 0.0));
    EXPECT_EQ(mock_ptr->last_sdo_index, 0x2008u);
    EXPECT_EQ(mock_ptr->last_sdo_sub, 0x01u);
    EXPECT_EQ(mock_ptr->last_sdo_value, 50000u); // 0.5 * 100000
}

TEST(CanOpenHAL, MonitorLoopUpdatesPosition) {
    auto mock = std::make_unique<MockCanOpen>();
    CanOpenHAL hal(std::move(mock));
    ASSERT_TRUE(hal.initialize(makeConfig()));
    ASSERT_TRUE(hal.start());
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    hal.stop();

    auto motor = hal.createMotorControl(0);
    ASSERT_NE(motor, nullptr);
    // 1000 counts / (4000/360) = 90°.
    EXPECT_NEAR(motor->getActualPosition(), 90.0, 0.5);
}
