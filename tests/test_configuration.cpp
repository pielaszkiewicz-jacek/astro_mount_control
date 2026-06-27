#include "config/configuration.h"
#include <gtest/gtest.h>
#include <fstream>

namespace astro_mount {
namespace config {
namespace test {

class ConfigurationTest : public ::testing::Test {
protected:
    void SetUp() override {
        config = std::make_unique<Configuration>();
    }
    
    void TearDown() override {
        config.reset();
    }
    
    std::unique_ptr<Configuration> config;
};

TEST_F(ConfigurationTest, InitialState) {
    // Default configuration should be valid
    auto errors = config->validate();
    for (const auto& e : errors) {
        ADD_FAILURE() << "Validation error: " << e;
    }
    EXPECT_TRUE(errors.empty());
}

TEST_F(ConfigurationTest, GetDefaultConfiguration) {
    auto default_config = Configuration::getDefault();
    auto errors = default_config.validate();
    EXPECT_TRUE(errors.empty());
}

TEST_F(ConfigurationTest, GetLoggingConfig) {
    auto logging = config->getLoggingConfig();
    EXPECT_EQ(logging.level, "INFO");
    EXPECT_EQ(logging.directory, "/var/log/astro-mount");
    EXPECT_EQ(logging.rotation_days, 7);
    EXPECT_EQ(logging.max_file_size_mb, 100);
    EXPECT_TRUE(logging.console_output);
}

TEST_F(ConfigurationTest, GetNetworkConfig) {
    auto network = config->getNetworkConfig();
    EXPECT_EQ(network.grpc_address, "0.0.0.0");
    EXPECT_EQ(network.grpc_port, 50051);
    EXPECT_EQ(network.max_connections, 10);
    EXPECT_FALSE(network.enable_ssl);
    EXPECT_TRUE(network.ssl_cert_path.empty());
    EXPECT_TRUE(network.ssl_key_path.empty());
}

TEST_F(ConfigurationTest, GetCanOpenConfig) {
    auto canopen = config->getCanOpenConfig();
    EXPECT_EQ(canopen.interface, "can0");
    EXPECT_EQ(canopen.node_id, 1);
    EXPECT_EQ(canopen.baud_rate, 1000000);
    EXPECT_TRUE(canopen.enable_sync);
    EXPECT_EQ(canopen.sync_interval_ms, 100);
}

TEST_F(ConfigurationTest, GetMountConfig) {
    auto mount = config->getMountConfig();
    EXPECT_EQ(mount.type, "equatorial");
    EXPECT_NEAR(mount.latitude, 52.0, 1e-6);
    EXPECT_NEAR(mount.longitude, 21.0, 1e-6);
    EXPECT_NEAR(mount.altitude, 100.0, 1e-6);
    EXPECT_NEAR(mount.ha_axis_params.gear_ratio, 360.0, 1e-6);
    EXPECT_NEAR(mount.dec_axis_params.gear_ratio, 360.0, 1e-6);
    EXPECT_NEAR(mount.max_slew_rate, 5.0, 1e-6);
    EXPECT_NEAR(mount.max_tracking_rate, 1.504, 1e-6);
    EXPECT_NEAR(mount.slew_acceleration, 1.0, 1e-6);
    EXPECT_NEAR(mount.tracking_acceleration, 0.001, 1e-6);
}

TEST_F(ConfigurationTest, GetTelescopeConfig) {
    auto telescope = config->getTelescopeConfig();
    EXPECT_NEAR(telescope.focal_length, 2000.0, 1e-6);
    EXPECT_NEAR(telescope.aperture, 200.0, 1e-6);
    EXPECT_NEAR(telescope.tube_length, 1800.0, 1e-6);
    EXPECT_EQ(telescope.camera_model, "ASI1600");
    EXPECT_NEAR(telescope.pixel_size, 3.8, 1e-6);
    EXPECT_EQ(telescope.sensor_width, 4656);
    EXPECT_EQ(telescope.sensor_height, 3520);
}

TEST_F(ConfigurationTest, GetGuiderConfig) {
    auto guider = config->getGuiderConfig();
    EXPECT_FALSE(guider.enabled);
    EXPECT_TRUE(guider.connection_string.empty());
    EXPECT_NEAR(guider.max_correction, 10.0, 1e-6);
    EXPECT_NEAR(guider.aggression, 0.5, 1e-6);
    EXPECT_EQ(guider.exposure_time_ms, 2000);
    EXPECT_EQ(guider.binning, 2);
}

TEST_F(ConfigurationTest, GetKalmanConfig) {
    auto kalman = config->getKalmanConfig();
    EXPECT_NEAR(kalman.process_noise, 0.01, 1e-6);
    EXPECT_NEAR(kalman.measurement_noise, 1.0, 1e-6);
    EXPECT_TRUE(kalman.adaptive_q);
    EXPECT_FALSE(kalman.adaptive_r);
    EXPECT_NEAR(kalman.innovation_threshold, 3.0, 1e-6);
    EXPECT_EQ(kalman.max_iterations, 10);
}

TEST_F(ConfigurationTest, GetTPointConfig) {
    auto tpoint = config->getTPointConfig();
    EXPECT_EQ(tpoint.enabled_terms, 0xFFFF);
    EXPECT_EQ(tpoint.min_measurements, 10);
    EXPECT_NEAR(tpoint.max_residual, 30.0, 1e-6);
    EXPECT_TRUE(tpoint.auto_calibrate);
}

TEST_F(ConfigurationTest, SetLoggingConfig) {
    Configuration::LoggingConfig new_logging;
    new_logging.level = "DEBUG";
    new_logging.directory = "/tmp/logs";
    new_logging.rotation_days = 3;
    new_logging.max_file_size_mb = 50;
    new_logging.console_output = false;
    
    config->setLoggingConfig(new_logging);
    auto retrieved = config->getLoggingConfig();
    
    EXPECT_EQ(retrieved.level, "DEBUG");
    EXPECT_EQ(retrieved.directory, "/tmp/logs");
    EXPECT_EQ(retrieved.rotation_days, 3);
    EXPECT_EQ(retrieved.max_file_size_mb, 50);
    EXPECT_FALSE(retrieved.console_output);
}

TEST_F(ConfigurationTest, SetNetworkConfig) {
    Configuration::NetworkConfig new_network;
    new_network.grpc_address = "127.0.0.1";
    new_network.grpc_port = 8080;
    new_network.max_connections = 5;
    new_network.enable_ssl = true;
    new_network.ssl_cert_path = "/path/to/cert.pem";
    new_network.ssl_key_path = "/path/to/key.pem";
    
    config->setNetworkConfig(new_network);
    auto retrieved = config->getNetworkConfig();
    
    EXPECT_EQ(retrieved.grpc_address, "127.0.0.1");
    EXPECT_EQ(retrieved.grpc_port, 8080);
    EXPECT_EQ(retrieved.max_connections, 5);
    EXPECT_TRUE(retrieved.enable_ssl);
    EXPECT_EQ(retrieved.ssl_cert_path, "/path/to/cert.pem");
    EXPECT_EQ(retrieved.ssl_key_path, "/path/to/key.pem");
}

TEST_F(ConfigurationTest, SaveAndLoadFile) {
    std::string filename = "test_config.json";
    
    // Modify some values
    auto logging = config->getLoggingConfig();
    logging.level = "DEBUG";
    config->setLoggingConfig(logging);
    
    // Save to file
    bool save_result = config->saveToFile(filename);
    EXPECT_TRUE(save_result);
    
    // Create new config and load from file
    auto loaded_config = std::make_unique<Configuration>();
    bool load_result = loaded_config->loadFromFile(filename);
    EXPECT_TRUE(load_result);
    
    // Verify loaded values
    auto loaded_logging = loaded_config->getLoggingConfig();
    EXPECT_EQ(loaded_logging.level, "DEBUG");
    
    // Clean up
    std::remove(filename.c_str());
}

TEST_F(ConfigurationTest, LoadFromString) {
    std::string json_str = R"({
        "logging": {
            "level": "ERROR",
            "directory": "/var/log/test"
        }
    })";
    
    bool result = config->loadFromString(json_str);
    EXPECT_TRUE(result);
    
    auto logging = config->getLoggingConfig();
    EXPECT_EQ(logging.level, "ERROR");
    EXPECT_EQ(logging.directory, "/var/log/test");
}

TEST_F(ConfigurationTest, ToString) {
    std::string json_str = config->toString();
    EXPECT_FALSE(json_str.empty());
    EXPECT_NE(json_str.find("logging"), std::string::npos);
    EXPECT_NE(json_str.find("network"), std::string::npos);
}

TEST_F(ConfigurationTest, ValidateValidConfig) {
    auto errors = config->validate();
    EXPECT_TRUE(errors.empty());
}

TEST_F(ConfigurationTest, ValidateInvalidPort) {
    auto network = config->getNetworkConfig();
    network.grpc_port = 70000;  // Invalid port
    config->setNetworkConfig(network);
    
    auto errors = config->validate();
    EXPECT_FALSE(errors.empty());
    EXPECT_NE(std::string::npos, errors[0].find("grpc_port"));
}

TEST_F(ConfigurationTest, ValidateInvalidLatitude) {
    auto mount = config->getMountConfig();
    mount.latitude = 100.0;  // Invalid latitude
    config->setMountConfig(mount);
    
    auto errors = config->validate();
    EXPECT_FALSE(errors.empty());
    EXPECT_NE(std::string::npos, errors[0].find("latitude"));
}

TEST_F(ConfigurationTest, GetValueByPath) {
    std::string level = config->getValue("logging.level");
    EXPECT_EQ(level, "INFO");
    
    std::string port = config->getValue("network.grpc_port");
    EXPECT_EQ(port, "50051");
}

TEST_F(ConfigurationTest, SetValueByPath) {
    bool result = config->setValue("logging.level", "DEBUG");
    EXPECT_TRUE(result);
    
    std::string level = config->getValue("logging.level");
    EXPECT_EQ(level, "DEBUG");
    
    // Test setting nested path
    result = config->setValue("new.section.value", "123");
    EXPECT_TRUE(result);
    
    std::string value = config->getValue("new.section.value");
    EXPECT_EQ(value, "123");
}

TEST_F(ConfigurationTest, IsModified) {
    EXPECT_FALSE(config->isModified());
    
    auto logging = config->getLoggingConfig();
    logging.level = "DEBUG";
    config->setLoggingConfig(logging);
    
    EXPECT_TRUE(config->isModified());
    
    config->resetModified();
    EXPECT_FALSE(config->isModified());
}

TEST_F(ConfigurationTest, MergeConfigurations) {
    Configuration other;
    auto other_logging = other.getLoggingConfig();
    other_logging.level = "TRACE";
    other.setLoggingConfig(other_logging);
    
    config->merge(other);
    
    auto logging = config->getLoggingConfig();
    EXPECT_EQ(logging.level, "TRACE");
}

// ============================================
// CASUAL MOUNT ORIENTATION CONFIG TESTS
// ============================================

TEST_F(ConfigurationTest, DefaultOrientationQuaternionIsIdentity) {
    auto mount = config->getMountConfig();
    
    // Default orientation quaternion should be identity: [0, 0, 0, 1]
    // (scalar-last convention with qw=1, qx=qy=qz=0)
    ASSERT_EQ(mount.orientation_quaternion.size(), 4);
    EXPECT_NEAR(mount.orientation_quaternion[0], 0.0, 1e-10);
    EXPECT_NEAR(mount.orientation_quaternion[1], 0.0, 1e-10);
    EXPECT_NEAR(mount.orientation_quaternion[2], 0.0, 1e-10);
    EXPECT_NEAR(mount.orientation_quaternion[3], 1.0, 1e-10);
}

TEST_F(ConfigurationTest, GetMountOrientationQuaternion) {
    auto q = config->getMountOrientationQuaternion();
    
    // Default should be identity: [0, 0, 0, 1]
    // (scalar-last convention with qw=1, qx=qy=qz=0)
    ASSERT_EQ(q.size(), 4);
    EXPECT_NEAR(q[0], 0.0, 1e-10);
    EXPECT_NEAR(q[1], 0.0, 1e-10);
    EXPECT_NEAR(q[2], 0.0, 1e-10);
    EXPECT_NEAR(q[3], 1.0, 1e-10);
}

TEST_F(ConfigurationTest, SetMountOrientationQuaternion) {
    auto mount = config->getMountConfig();
    mount.orientation_quaternion = {{0.0, 0.0, 0.0, 1.0}};
    config->setMountConfig(mount);
    
    auto retrieved = config->getMountConfig();
    ASSERT_EQ(retrieved.orientation_quaternion.size(), 4);
    EXPECT_NEAR(retrieved.orientation_quaternion[0], 0.0, 1e-10);
    EXPECT_NEAR(retrieved.orientation_quaternion[1], 0.0, 1e-10);
    EXPECT_NEAR(retrieved.orientation_quaternion[2], 0.0, 1e-10);
    EXPECT_NEAR(retrieved.orientation_quaternion[3], 1.0, 1e-10);
}

TEST_F(ConfigurationTest, OrientationQuaternionRoundTrip) {
    // Set a non-trivial quaternion and verify it persists through save/load
    auto mount = config->getMountConfig();
    mount.orientation_quaternion = {{0.5, 0.5, 0.5, 0.5}};
    config->setMountConfig(mount);
    
    // Verify via getMountOrientationQuaternion
    auto q = config->getMountOrientationQuaternion();
    EXPECT_NEAR(q[0], 0.5, 1e-10);
    EXPECT_NEAR(q[1], 0.5, 1e-10);
    EXPECT_NEAR(q[2], 0.5, 1e-10);
    EXPECT_NEAR(q[3], 0.5, 1e-10);
}

TEST_F(ConfigurationTest, OrientationQuaternionSerialization) {
    // Set a quaternion and verify it appears in the JSON string
    auto mount = config->getMountConfig();
    mount.orientation_quaternion = {{0.707, 0.0, 0.0, 0.707}};
    config->setMountConfig(mount);
    
    std::string json_str = config->toString();
    EXPECT_NE(json_str.find("orientation_quaternion"), std::string::npos);
    EXPECT_NE(json_str.find("0.707"), std::string::npos);
}

} // namespace test
} // namespace config
} // namespace astro_mount

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}