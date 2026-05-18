#ifndef CONFIG_MONITOR_H
#define CONFIG_MONITOR_H

#include "config/configuration.h"
#include <string>
#include <functional>
#include <memory>
#include <thread>
#include <atomic>
#include <mutex>

namespace astro_mount {
namespace config {

/**
 * @brief Configuration monitor for hot-reload functionality
 * 
 * Monitors configuration file for changes and automatically reloads
 * configuration when file is modified.
 */
class ConfigMonitor {
public:
    /**
     * @brief Callback type for configuration change events
     */
    using ConfigChangeCallback = std::function<void(const Configuration&)>;

    /**
     * @brief Construct a new ConfigMonitor object
     * @param config_file Path to configuration file
     * @param check_interval_ms Interval to check for changes (in milliseconds)
     */
    explicit ConfigMonitor(const std::string& config_file, 
                          int check_interval_ms = 1000);

    /**
     * @brief Destroy the ConfigMonitor object
     */
    ~ConfigMonitor();

    // Delete copy and move constructors
    ConfigMonitor(const ConfigMonitor&) = delete;
    ConfigMonitor& operator=(const ConfigMonitor&) = delete;
    ConfigMonitor(ConfigMonitor&&) = delete;
    ConfigMonitor& operator=(ConfigMonitor&&) = delete;

    /**
     * @brief Start monitoring configuration file
     * @return True if monitoring started successfully
     */
    bool start();

    /**
     * @brief Stop monitoring configuration file
     */
    void stop();

    /**
     * @brief Check if monitoring is active
     * @return True if monitoring is active
     */
    bool isMonitoring() const { return monitoring_; }

    /**
     * @brief Get the current configuration
     * @return Reference to current configuration
     */
    const Configuration& getConfiguration() const;

    /**
     * @brief Manually trigger configuration reload
     * @return True if reload successful
     */
    bool reload();

    /**
     * @brief Set callback for configuration change events
     * @param callback Callback function
     */
    void setConfigChangeCallback(ConfigChangeCallback callback);

    /**
     * @brief Get path to monitored configuration file
     * @return Configuration file path
     */
    std::string getConfigFilePath() const { return config_file_; }

    /**
     * @brief Get monitoring interval
     * @return Monitoring interval in milliseconds
     */
    int getCheckInterval() const { return check_interval_ms_; }

private:
    // Private implementation
    class Impl;
    std::unique_ptr<Impl> impl_;

    std::string config_file_;
    int check_interval_ms_;
    std::atomic<bool> monitoring_{false};
    ConfigChangeCallback change_callback_;
};

/**
 * @brief Configuration reload notification system
 * 
 * Provides a way for components to register for specific
 * configuration changes (e.g., only logging, only mount params)
 */
class ConfigNotifier {
public:
    /**
     * @brief Configuration section enum
     */
    enum class ConfigSection {
        ALL,
        LOGGING,
        NETWORK,
        CANOPEN,
        MOUNT,
        TELESCOPE,
        GUIDER,
        KALMAN,
        TPOINT
    };

    /**
     * @brief Configuration change event data
     */
    struct ConfigChangeEvent {
        ConfigSection section;
        Configuration new_config;
        Configuration old_config;
        std::string field_changed;
        std::chrono::system_clock::time_point timestamp;
    };

    /**
     * @brief Callback type for configuration section changes
     */
    using SectionChangeCallback = std::function<void(const ConfigChangeEvent&)>;

    /**
     * @brief Construct a new ConfigNotifier object
     */
    ConfigNotifier();

    /**
     * @brief Destroy the ConfigNotifier object
     */
    ~ConfigNotifier();

    /**
     * @brief Register a callback for configuration section changes
     * @param section Configuration section to monitor
     * @param callback Callback function
     * @return Token for unsubscribing
     */
    int subscribe(ConfigSection section, SectionChangeCallback callback);

    /**
     * @brief Unsubscribe from configuration changes
     * @param token Subscription token
     */
    void unsubscribe(int token);

    /**
     * @brief Notify subscribers of configuration change
     * @param event Configuration change event
     */
    void notify(const ConfigChangeEvent& event);

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

/**
 * @brief Configuration manager with hot-reload support
 * 
 * Combines ConfigMonitor and ConfigNotifier for complete
 * configuration management with hot-reload capability.
 */
class ConfigManager {
public:
    /**
     * @brief Construct a new ConfigManager object
     * @param config_file Path to configuration file
     * @param check_interval_ms Interval to check for changes (in milliseconds)
     */
    ConfigManager(const std::string& config_file = "config/default.json",
                  int check_interval_ms = 1000);

    /**
     * @brief Destroy the ConfigManager object
     */
    ~ConfigManager();

    // Delete copy and move constructors
    ConfigManager(const ConfigManager&) = delete;
    ConfigManager& operator=(const ConfigManager&) = delete;
    ConfigManager(ConfigManager&&) = delete;
    ConfigManager& operator=(ConfigManager&&) = delete;

    /**
     * @brief Initialize configuration manager
     * @return True if initialization successful
     */
    bool initialize();

    /**
     * @brief Shutdown configuration manager
     */
    void shutdown();

    /**
     * @brief Get the current configuration
     * @return Reference to current configuration
     */
    const Configuration& getConfiguration() const;

    /**
     * @brief Manually reload configuration
     * @return True if reload successful
     */
    bool reloadConfiguration();

    /**
     * @brief Update configuration and save to file
     * @param config New configuration
     * @return True if update successful
     */
    bool updateConfiguration(const Configuration& config);

    /**
     * @brief Register callback for configuration changes
     * @param section Configuration section to monitor
     * @param callback Callback function
     * @return Subscription token
     */
    int registerConfigChangeCallback(
        ConfigNotifier::ConfigSection section,
        ConfigNotifier::SectionChangeCallback callback);

    /**
     * @brief Unregister callback
     * @param token Subscription token
     */
    void unregisterConfigChangeCallback(int token);

    /**
     * @brief Check if configuration manager is active
     * @return True if active
     */
    bool isActive() const;

    /**
     * @brief Get configuration file path
     * @return Configuration file path
     */
    std::string getConfigFilePath() const;

    /**
     * @brief Validate configuration before applying
     * @param config Configuration to validate
     * @return Vector of validation errors (empty if valid)
     */
    static std::vector<std::string> validateConfiguration(const Configuration& config);

private:
    // Private implementation
    class Impl;
    std::unique_ptr<Impl> impl_;

    std::string config_file_;
    int check_interval_ms_;
    std::atomic<bool> active_{false};
};

} // namespace config
} // namespace astro_mount

#endif // CONFIG_MONITOR_H