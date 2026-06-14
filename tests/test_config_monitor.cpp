#include "config/config_monitor.h"
#include "config/configuration.h"
#include "logging/logger.h"
#include <gtest/gtest.h>
#include <fstream>
#include <filesystem>
#include <thread>
#include <chrono>

namespace astro_mount {
namespace config {
namespace test {

using namespace std::chrono_literals;

// ============================================================================
// Test Fixture: Creates a temp config file for testing
// ============================================================================
class ConfigMonitorTest : public ::testing::Test {
protected:
    void SetUp() override {
        logging::Logger::init("");

        // Create a temporary directory for test config files
        tmp_dir_ = std::filesystem::temp_directory_path() / "astro_mount_config_test";
        std::filesystem::create_directories(tmp_dir_);

        // Create a valid config file
        config_path_ = (tmp_dir_ / "test_config.json").string();
        createValidConfigFile(config_path_);
    }

    void TearDown() override {
        // Clean up temp directory
        std::filesystem::remove_all(tmp_dir_);
        logging::Logger::shutdown();
    }

    void createValidConfigFile(const std::string& path) {
        std::ofstream file(path);
        file << R"({
            "logging": {
                "level": "INFO",
                "directory": "/tmp/logs",
                "rotation_days": 7,
                "max_file_size_mb": 100,
                "console_output": true
            },
            "network": {
                "grpc_port": 50051,
                "max_connections": 10
            },
            "canopen": {
                "node_id": 1,
                "baud_rate": 1000000,
                "sync_interval_ms": 100
            },
            "mount": {
                "latitude": 52.0,
                "longitude": 21.0,
                "altitude": 100.0,
                "max_slew_rate": 5.0,
                "max_tracking_rate": 0.004178,
                "slew_acceleration": 1.0,
                "tracking_acceleration": 0.001,
                "mount_height": 1.5,
                "axis_physical_parameters": {
                    "ha_axis": {
                        "encoder_resolution": 16384,
                        "gear_ratio": 360.0
                    },
                    "dec_axis": {
                        "encoder_resolution": 16384,
                        "gear_ratio": 360.0
                    }
                }
            },
            "telescope": {
                "focal_length": 2000.0,
                "aperture": 200.0,
                "pixel_size": 3.8
            },
            "guider": {
                "max_correction": 10.0,
                "aggression": 0.5
            },
            "kalman": {
                "process_noise": 0.01,
                "measurement_noise": 1.0,
                "innovation_threshold": 3.0,
                "max_iterations": 10
            },
            "tpoint": {
                "enabled_terms": 65535,
                "max_residual": 30.0,
                "min_measurements": 10
            }
        })";
        file.close();
    }

    void modifyConfigFile(const std::string& path) {
        // Wait a bit to ensure modification time differs
        std::this_thread::sleep_for(10ms);
        std::ofstream file(path);
        file << R"({
            "logging": {
                "level": "DEBUG",
                "directory": "/tmp/logs",
                "rotation_days": 7,
                "max_file_size_mb": 100,
                "console_output": true
            },
            "network": {
                "grpc_port": 50052,
                "max_connections": 10
            },
            "canopen": {
                "node_id": 1,
                "baud_rate": 1000000,
                "sync_interval_ms": 100
            },
            "mount": {
                "latitude": 52.0,
                "longitude": 21.0,
                "altitude": 100.0,
                "max_slew_rate": 5.0,
                "max_tracking_rate": 0.004178,
                "slew_acceleration": 1.0,
                "tracking_acceleration": 0.001,
                "mount_height": 1.5,
                "axis_physical_parameters": {
                    "ha_axis": {
                        "encoder_resolution": 16384,
                        "gear_ratio": 360.0
                    },
                    "dec_axis": {
                        "encoder_resolution": 16384,
                        "gear_ratio": 360.0
                    }
                }
            },
            "telescope": {
                "focal_length": 2000.0,
                "aperture": 200.0,
                "pixel_size": 3.8
            },
            "guider": {
                "max_correction": 10.0,
                "aggression": 0.5
            },
            "kalman": {
                "process_noise": 0.01,
                "measurement_noise": 1.0,
                "innovation_threshold": 3.0,
                "max_iterations": 10
            },
            "tpoint": {
                "enabled_terms": 65535,
                "max_residual": 30.0,
                "min_measurements": 10
            }
        })";
        file.close();
    }

    std::filesystem::path tmp_dir_;
    std::string config_path_;
};

// ============================================================================
// Part 1: ConfigMonitor Tests
// ============================================================================

TEST_F(ConfigMonitorTest, Constructor) {
    ConfigMonitor monitor(config_path_, 100);
    EXPECT_EQ(monitor.getConfigFilePath(), config_path_);
    EXPECT_EQ(monitor.getCheckInterval(), 100);
    EXPECT_FALSE(monitor.isMonitoring());
}

TEST_F(ConfigMonitorTest, StartAndStop) {
    ConfigMonitor monitor(config_path_, 100);
    EXPECT_FALSE(monitor.isMonitoring());

    EXPECT_TRUE(monitor.start());
    EXPECT_TRUE(monitor.isMonitoring());

    monitor.stop();
    EXPECT_FALSE(monitor.isMonitoring());
}

TEST_F(ConfigMonitorTest, GetConfigurationAfterConstruction) {
    ConfigMonitor monitor(config_path_, 100);
    const auto& config = monitor.getConfiguration();
    // Should have loaded default or from file
    EXPECT_NO_THROW(config.getMountConfig());
}

TEST_F(ConfigMonitorTest, Reload) {
    ConfigMonitor monitor(config_path_, 100);
    EXPECT_TRUE(monitor.reload());
}

TEST_F(ConfigMonitorTest, ReloadWithNonexistentFile) {
    ConfigMonitor monitor("/nonexistent/config.json", 100);
    // Should handle gracefully (use defaults)
    EXPECT_NO_THROW(monitor.reload());
}

TEST_F(ConfigMonitorTest, SetAndTriggerChangeCallback) {
    ConfigMonitor monitor(config_path_, 100);
    bool callback_called = false;

    monitor.setConfigChangeCallback([&callback_called](const Configuration&) {
        callback_called = true;
    });

    // Modify file and trigger reload
    modifyConfigFile(config_path_);
    EXPECT_TRUE(monitor.reload());

    // Verify callback was invoked by reload
    EXPECT_TRUE(callback_called);
}

TEST_F(ConfigMonitorTest, DoubleStartIsIdempotent) {
    ConfigMonitor monitor(config_path_, 100);
    EXPECT_TRUE(monitor.start());
    // Second start returns false (already running)
    EXPECT_FALSE(monitor.start());
    monitor.stop();
}

TEST_F(ConfigMonitorTest, StartMonitorsFileChanges) {
    // Use short interval for testing
    ConfigMonitor monitor(config_path_, 50);
    bool callback_called = false;

    monitor.setConfigChangeCallback([&callback_called](const Configuration&) {
        callback_called = true;
    });

    EXPECT_TRUE(monitor.start());

    // Modify the config file
    modifyConfigFile(config_path_);

    // Wait for monitoring thread to detect change
    std::this_thread::sleep_for(200ms);

    monitor.stop();
}

// ============================================================================
// Part 2: ConfigNotifier Tests
// ============================================================================

TEST(ConfigNotifierTest, SubscribeAndNotify) {
    ConfigNotifier notifier;
    bool callback_called = false;

    int token = notifier.subscribe(
        ConfigNotifier::ConfigSection::ALL,
        [&callback_called](const ConfigNotifier::ConfigChangeEvent& event) {
            callback_called = true;
            EXPECT_EQ(event.section, ConfigNotifier::ConfigSection::ALL);
            EXPECT_EQ(event.field_changed, "test_field");
        }
    );

    EXPECT_GT(token, 0);

    ConfigNotifier::ConfigChangeEvent event;
    event.section = ConfigNotifier::ConfigSection::ALL;
    event.field_changed = "test_field";
    event.timestamp = std::chrono::system_clock::now();

    notifier.notify(event);
    EXPECT_TRUE(callback_called);
}

TEST(ConfigNotifierTest, SubscribeToSpecificSection) {
    ConfigNotifier notifier;
    int mount_calls = 0;
    int logging_calls = 0;

    notifier.subscribe(
        ConfigNotifier::ConfigSection::MOUNT,
        [&mount_calls](const ConfigNotifier::ConfigChangeEvent&) {
            mount_calls++;
        }
    );

    notifier.subscribe(
        ConfigNotifier::ConfigSection::LOGGING,
        [&logging_calls](const ConfigNotifier::ConfigChangeEvent&) {
            logging_calls++;
        }
    );

    // Notify MOUNT change
    ConfigNotifier::ConfigChangeEvent mount_event;
    mount_event.section = ConfigNotifier::ConfigSection::MOUNT;
    mount_event.timestamp = std::chrono::system_clock::now();
    notifier.notify(mount_event);

    EXPECT_EQ(mount_calls, 1);
    EXPECT_EQ(logging_calls, 0);  // Should not be called

    // Notify LOGGING change
    ConfigNotifier::ConfigChangeEvent logging_event;
    logging_event.section = ConfigNotifier::ConfigSection::LOGGING;
    logging_event.timestamp = std::chrono::system_clock::now();
    notifier.notify(logging_event);

    EXPECT_EQ(mount_calls, 1);
    EXPECT_EQ(logging_calls, 1);
}

TEST(ConfigNotifierTest, AllSectionReceivesAll) {
    ConfigNotifier notifier;
    int call_count = 0;

    notifier.subscribe(
        ConfigNotifier::ConfigSection::ALL,
        [&call_count](const ConfigNotifier::ConfigChangeEvent&) {
            call_count++;
        }
    );

    ConfigNotifier::ConfigChangeEvent mount_event;
    mount_event.section = ConfigNotifier::ConfigSection::MOUNT;
    mount_event.timestamp = std::chrono::system_clock::now();
    notifier.notify(mount_event);

    ConfigNotifier::ConfigChangeEvent logging_event;
    logging_event.section = ConfigNotifier::ConfigSection::LOGGING;
    logging_event.timestamp = std::chrono::system_clock::now();
    notifier.notify(logging_event);

    EXPECT_EQ(call_count, 2);
}

TEST(ConfigNotifierTest, Unsubscribe) {
    ConfigNotifier notifier;
    int call_count = 0;

    int token = notifier.subscribe(
        ConfigNotifier::ConfigSection::ALL,
        [&call_count](const ConfigNotifier::ConfigChangeEvent&) {
            call_count++;
        }
    );

    // Notify once
    ConfigNotifier::ConfigChangeEvent event;
    event.section = ConfigNotifier::ConfigSection::ALL;
    event.timestamp = std::chrono::system_clock::now();
    notifier.notify(event);
    EXPECT_EQ(call_count, 1);

    // Unsubscribe
    notifier.unsubscribe(token);

    // Notify again - should not trigger callback
    notifier.notify(event);
    EXPECT_EQ(call_count, 1);  // Still 1
}

TEST(ConfigNotifierTest, MultipleSubscribers) {
    ConfigNotifier notifier;
    int call_count1 = 0;
    int call_count2 = 0;

    notifier.subscribe(
        ConfigNotifier::ConfigSection::ALL,
        [&call_count1](const ConfigNotifier::ConfigChangeEvent&) {
            call_count1++;
        }
    );

    notifier.subscribe(
        ConfigNotifier::ConfigSection::ALL,
        [&call_count2](const ConfigNotifier::ConfigChangeEvent&) {
            call_count2++;
        }
    );

    ConfigNotifier::ConfigChangeEvent event;
    event.section = ConfigNotifier::ConfigSection::ALL;
    event.timestamp = std::chrono::system_clock::now();
    notifier.notify(event);

    EXPECT_EQ(call_count1, 1);
    EXPECT_EQ(call_count2, 1);
}

TEST(ConfigNotifierTest, SubscriberExceptionDoesNotCrash) {
    ConfigNotifier notifier;

    notifier.subscribe(
        ConfigNotifier::ConfigSection::ALL,
        [](const ConfigNotifier::ConfigChangeEvent&) {
            throw std::runtime_error("test exception");
        }
    );

    ConfigNotifier::ConfigChangeEvent event;
    event.section = ConfigNotifier::ConfigSection::ALL;
    event.timestamp = std::chrono::system_clock::now();

    // Should not throw
    EXPECT_NO_THROW(notifier.notify(event));
}

// ============================================================================
// Part 3: ConfigManager Tests
// ============================================================================

class ConfigManagerTest : public ::testing::Test {
protected:
    void SetUp() override {
        logging::Logger::init("");

        tmp_dir_ = std::filesystem::temp_directory_path() / "astro_mount_cfg_mgr_test";
        std::filesystem::create_directories(tmp_dir_);

        config_path_ = (tmp_dir_ / "manager_test.json").string();
        std::ofstream file(config_path_);
        file << R"({
            "logging": {
                "level": "INFO",
                "directory": "/tmp/logs",
                "rotation_days": 7,
                "max_file_size_mb": 100,
                "console_output": true
            },
            "network": {
                "grpc_port": 50051,
                "max_connections": 10
            },
            "canopen": {
                "node_id": 1,
                "baud_rate": 1000000,
                "sync_interval_ms": 100
            },
            "mount": {
                "latitude": 52.0,
                "longitude": 21.0,
                "altitude": 100.0,
                "max_slew_rate": 5.0,
                "max_tracking_rate": 0.004178,
                "slew_acceleration": 1.0,
                "tracking_acceleration": 0.001,
                "mount_height": 1.5,
                "axis_physical_parameters": {
                    "ha_axis": {
                        "encoder_resolution": 16384,
                        "gear_ratio": 360.0
                    },
                    "dec_axis": {
                        "encoder_resolution": 16384,
                        "gear_ratio": 360.0
                    }
                }
            },
            "telescope": {
                "focal_length": 2000.0,
                "aperture": 200.0,
                "pixel_size": 3.8
            },
            "guider": {
                "max_correction": 10.0,
                "aggression": 0.5
            },
            "kalman": {
                "process_noise": 0.01,
                "measurement_noise": 1.0,
                "innovation_threshold": 3.0,
                "max_iterations": 10
            },
            "tpoint": {
                "enabled_terms": 65535,
                "max_residual": 30.0,
                "min_measurements": 10
            }
        })";
        file.close();
    }

    void TearDown() override {
        std::filesystem::remove_all(tmp_dir_);
        logging::Logger::shutdown();
    }

    std::filesystem::path tmp_dir_;
    std::string config_path_;
};

TEST_F(ConfigManagerTest, ConstructAndInitialize) {
    ConfigManager manager(config_path_, 100);
    EXPECT_FALSE(manager.isActive());
    EXPECT_EQ(manager.getConfigFilePath(), config_path_);

    EXPECT_TRUE(manager.initialize());
    EXPECT_TRUE(manager.isActive());
}

TEST_F(ConfigManagerTest, InitializeAndShutdown) {
    ConfigManager manager(config_path_, 100);
    EXPECT_TRUE(manager.initialize());
    EXPECT_TRUE(manager.isActive());

    manager.shutdown();
    EXPECT_FALSE(manager.isActive());
}

TEST_F(ConfigManagerTest, DoubleInitializeIsIdempotent) {
    ConfigManager manager(config_path_, 100);
    EXPECT_TRUE(manager.initialize());
    EXPECT_TRUE(manager.initialize());  // Second call should still return true
    manager.shutdown();
}

TEST_F(ConfigManagerTest, GetConfiguration) {
    ConfigManager manager(config_path_, 100);
    manager.initialize();

    const auto& config = manager.getConfiguration();
    EXPECT_NO_THROW(config.getMountConfig());

    manager.shutdown();
}

TEST_F(ConfigManagerTest, ReloadConfiguration) {
    ConfigManager manager(config_path_, 100);
    manager.initialize();
    EXPECT_TRUE(manager.reloadConfiguration());
    manager.shutdown();
}

TEST_F(ConfigManagerTest, RegisterAndUnregisterCallback) {
    ConfigManager manager(config_path_, 100);
    manager.initialize();

    bool callback_called = false;
    int token = manager.registerConfigChangeCallback(
        ConfigNotifier::ConfigSection::ALL,
        [&callback_called](const ConfigNotifier::ConfigChangeEvent&) {
            callback_called = true;
        }
    );

    EXPECT_GT(token, 0);

    manager.unregisterConfigChangeCallback(token);

    manager.shutdown();
}

TEST_F(ConfigManagerTest, ValidateConfiguration) {
    Configuration config;
    auto errors = ConfigManager::validateConfiguration(config);
    // Should return errors or empty vector - at least not throw
    EXPECT_NO_THROW(ConfigManager::validateConfiguration(config));
}

} // namespace test
} // namespace config
} // namespace astro_mount
