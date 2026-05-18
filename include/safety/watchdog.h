#ifndef WATCHDOG_H
#define WATCHDOG_H

#include "controllers/mount_controller.h"
#include "logging/logger.h"
#include <memory>
#include <thread>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <chrono>
#include <functional>

namespace astro_mount {
namespace safety {

/**
 * @brief Watchdog configuration parameters
 */
struct WatchdogConfig {
    // Timing parameters
    int check_interval_ms = 1000;           // Interval between checks
    int timeout_threshold_ms = 5000;        // Timeout threshold for no response
    int max_consecutive_errors = 3;         // Max consecutive errors before action
    
    // Safety thresholds
    double max_position_error_arcsec = 10.0; // Max allowed position error
    double max_tracking_error_arcsec = 5.0;  // Max allowed tracking error
    double max_velocity_error_arcsec = 1.0;  // Max allowed velocity error
    double max_acceleration_error = 2.0;     // Max allowed acceleration error
    
    // Temperature thresholds
    double max_motor_temperature_c = 80.0;   // Max motor temperature
    double max_controller_temperature_c = 70.0; // Max controller temperature
    
    // Power thresholds
    double min_voltage = 10.0;              // Minimum allowed voltage
    double max_current = 5.0;               // Maximum allowed current per axis
    
    // Action configuration
    bool enable_automatic_recovery = true;  // Enable automatic recovery attempts
    int max_recovery_attempts = 3;          // Max recovery attempts before permanent stop
    int recovery_delay_ms = 2000;           // Delay between recovery attempts
    
    // Logging configuration
    bool log_all_checks = false;            // Log every watchdog check (verbose)
    logging::LogLevel log_level = logging::LogLevel::WARN;  // Log level for watchdog events
};

/**
 * @brief Watchdog status and statistics
 */
struct WatchdogStatus {
    bool active = false;                    // Is watchdog active
    bool triggered = false;                 // Has watchdog been triggered
    std::chrono::system_clock::time_point last_check_time;
    std::chrono::system_clock::time_point last_response_time;
    
    // Counters
    int total_checks = 0;
    int error_count = 0;
    int consecutive_errors = 0;
    int recovery_attempts = 0;
    
    // Current metrics
    double position_error_arcsec = 0.0;
    double tracking_error_arcsec = 0.0;
    double velocity_error_arcsec = 0.0;
    double motor_temperature_c = 0.0;
    double controller_temperature_c = 0.0;
    double voltage = 0.0;
    double current_axis1 = 0.0;
    double current_axis2 = 0.0;
    
    // Last triggered condition
    std::string last_trigger_condition;
    std::chrono::system_clock::time_point last_trigger_time;
    
    // Action taken
    std::string last_action_taken;
    bool emergency_stop_triggered = false;
};

/**
 * @brief Watchdog callback types
 */
enum class WatchdogEvent {
    MONITORING_STARTED,
    MONITORING_STOPPED,
    CHECK_PASSED,
    CHECK_FAILED,
    ERROR_THRESHOLD_EXCEEDED,
    TIMEOUT_DETECTED,
    POSITION_ERROR_EXCEEDED,
    TRACKING_ERROR_EXCEEDED,
    TEMPERATURE_EXCEEDED,
    POWER_FAULT_DETECTED,
    EMERGENCY_STOP_TRIGGERED,
    RECOVERY_ATTEMPT_STARTED,
    RECOVERY_ATTEMPT_FAILED,
    RECOVERY_SUCCESSFUL,
    SYSTEM_RESUMED,
    PERMANENT_STOP
};

using WatchdogCallback = std::function<void(WatchdogEvent, const std::string&, const WatchdogStatus&)>;

/**
 * @brief System watchdog for safety monitoring
 * 
 * Monitors mount controller state and triggers safety actions
 * when abnormal conditions are detected.
 */
class Watchdog {
public:
    /**
     * @brief Construct a new Watchdog object
     * @param controller Reference to mount controller
     * @param config Watchdog configuration
     */
    explicit Watchdog(controllers::MountController& controller,
                     const WatchdogConfig& config = WatchdogConfig{});
    
    /**
     * @brief Destroy the Watchdog object
     */
    ~Watchdog();
    
    // Delete copy and move constructors
    Watchdog(const Watchdog&) = delete;
    Watchdog& operator=(const Watchdog&) = delete;
    Watchdog(Watchdog&&) = delete;
    Watchdog& operator=(Watchdog&&) = delete;
    
    /**
     * @brief Start watchdog monitoring
     * @return True if started successfully
     */
    bool start();
    
    /**
     * @brief Stop watchdog monitoring
     */
    void stop();
    
    /**
     * @brief Check if watchdog is active
     * @return True if active
     */
    bool isActive() const { return active_; }
    
    /**
     * @brief Check if watchdog has been triggered
     * @return True if triggered (emergency condition)
     */
    bool isTriggered() const { return triggered_; }
    
    /**
     * @brief Get current watchdog status
     * @return Watchdog status
     */
    WatchdogStatus getStatus() const;
    
    /**
     * @brief Get watchdog configuration
     * @return Watchdog configuration
     */
    WatchdogConfig getConfig() const;
    
    /**
     * @brief Update watchdog configuration
     * @param config New configuration
     */
    void updateConfig(const WatchdogConfig& config);
    
    /**
     * @brief Manually trigger watchdog (for testing)
     * @param condition Condition description
     */
    void manualTrigger(const std::string& condition);
    
    /**
     * @brief Reset watchdog (clear triggered state)
     * @return True if reset successful
     */
    bool reset();
    
    /**
     * @brief Register callback for watchdog events
     * @param callback Callback function
     */
    void registerCallback(WatchdogCallback callback);
    
    /**
     * @brief Perform immediate safety check
     * @return True if all safety checks pass
     */
    bool performSafetyCheck();
    
    /**
     * @brief Get watchdog statistics
     * @return Statistics string
     */
    std::string getStatistics() const;
    
    /**
     * @brief Get default watchdog configuration
     * @return Default configuration
     */
    static WatchdogConfig getDefaultConfig();
    
private:
    // Private implementation
    class Impl;
    std::unique_ptr<Impl> impl_;
    
    controllers::MountController& controller_;
    WatchdogConfig config_;
    std::atomic<bool> active_{false};
    std::atomic<bool> triggered_{false};
};

/**
 * @brief Safety manager coordinating multiple safety mechanisms
 */
class SafetyManager {
public:
    /**
     * @brief Safety subsystem types
     */
    enum class SafetySubsystem {
        WATCHDOG,
        EMERGENCY_STOP,
        POSITION_LIMITS,
        VELOCITY_LIMITS,
        TEMPERATURE_MONITOR,
        POWER_MONITOR,
        COMMUNICATION_MONITOR
    };
    
    /**
     * @brief Safety event with severity
     */
    struct SafetyEvent {
        SafetySubsystem subsystem;
        std::string event_type;
        std::string description;
        logging::LogLevel severity;
        std::chrono::system_clock::time_point timestamp;
        std::map<std::string, std::string> context;
    };
    
    /**
     * @brief Safety action types
     */
    // Undefine conflicting syslog macros before defining enum values
#ifdef LOG_WARNING
#undef LOG_WARNING
#endif
    enum class SafetyAction {
        NONE,
        LOG_WARNING,
        LOG_ERROR,
        REDUCE_SPEED,
        PAUSE_OPERATION,
        EMERGENCY_STOP,
        POWER_DOWN,
        SYSTEM_RESET
    };
    
    /**
     * @brief Construct a new SafetyManager object
     * @param controller Reference to mount controller
     */
    explicit SafetyManager(controllers::MountController& controller);
    
    /**
     * @brief Destroy the SafetyManager object
     */
    ~SafetyManager();
    
    // Delete copy and move constructors
    SafetyManager(const SafetyManager&) = delete;
    SafetyManager& operator=(const SafetyManager&) = delete;
    SafetyManager(SafetyManager&&) = delete;
    SafetyManager& operator=(SafetyManager&&) = delete;
    
    /**
     * @brief Initialize safety manager
     * @return True if initialization successful
     */
    bool initialize();
    
    /**
     * @brief Shutdown safety manager
     */
    void shutdown();
    
    /**
     * @brief Get watchdog instance
     * @return Reference to watchdog
     */
    Watchdog& getWatchdog();
    
    /**
     * @brief Trigger emergency stop
     * @param reason Reason for emergency stop
     * @param subsystem Triggering subsystem
     */
    void triggerEmergencyStop(const std::string& reason, SafetySubsystem subsystem);
    
    /**
     * @brief Check if emergency stop is active
     * @return True if emergency stop is active
     */
    bool isEmergencyStopActive() const;
    
    /**
     * @brief Clear emergency stop (requires manual override)
     * @param override_code Override authorization code
     * @return True if cleared successfully
     */
    bool clearEmergencyStop(const std::string& override_code = "");
    
    /**
     * @brief Register safety event
     * @param event Safety event
     */
    void registerSafetyEvent(const SafetyEvent& event);
    
    /**
     * @brief Get safety events log
     * @param max_events Maximum number of events to return (0 for all)
     * @return Vector of safety events
     */
    std::vector<SafetyEvent> getSafetyEvents(size_t max_events = 0) const;
    
    /**
     * @brief Get safety status summary
     * @return Status summary string
     */
    std::string getSafetyStatus() const;
    
    /**
     * @brief Perform comprehensive safety check
     * @return True if all safety systems are nominal
     */
    bool performComprehensiveSafetyCheck();
    
private:
    // Private implementation
    class Impl;
    std::unique_ptr<Impl> impl_;
    
    controllers::MountController& controller_;
    std::atomic<bool> initialized_{false};
    std::atomic<bool> emergency_stop_active_{false};
};

} // namespace safety
} // namespace astro_mount

#endif // WATCHDOG_H