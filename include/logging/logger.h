#ifndef LOGGER_H
#define LOGGER_H

#include <memory>
#include <string>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/syslog_sink.h>
#include <map>

namespace astro_mount {
namespace logging {

/**
 * @brief Logging levels for mount controller system
 */
enum class LogLevel {
    TRACE = spdlog::level::trace,
    DEBUG = spdlog::level::debug,
    INFO = spdlog::level::info,
    WARN = spdlog::level::warn,
    ERROR = spdlog::level::err,
    CRITICAL = spdlog::level::critical,
    OFF = spdlog::level::off
};

/**
 * @brief Log event structure for structured logging
 */
struct LogEvent {
    std::string event_type;
    std::string component;
    std::map<std::string, std::string> context;
    std::chrono::system_clock::time_point timestamp;
    
    LogEvent(const std::string& type, const std::string& comp = "")
        : event_type(type), component(comp), 
          timestamp(std::chrono::system_clock::now()) {}
    
    void addContext(const std::string& key, const std::string& value) {
        context[key] = value;
    }
};

/**
 * @brief Central logging system for mount controller
 * 
 * Provides structured logging, audit trails, and performance monitoring
 */
class Logger {
public:
    /**
     * @brief Initialize logging system from configuration
     * @param config_path Path to logging configuration
     * @return True if initialization successful
     */
    static bool init(const std::string& config_path = "");
    
    /**
     * @brief Initialize with programmatic configuration
     * @param log_dir Log directory
     * @param max_size_mb Maximum log file size in MB
     * @param max_files Maximum number of rotated files
     * @param console_enabled Enable console output
     * @param syslog_enabled Enable syslog output
     * @param level Minimum log level
     * @return True if initialization successful
     */
    static bool initProgrammatic(const std::string& log_dir = "/var/log/astro-mount",
                                size_t max_size_mb = 100,
                                size_t max_files = 10,
                                bool console_enabled = true,
                                bool syslog_enabled = false,
                                LogLevel level = LogLevel::INFO);
    
    /**
     * @brief Get logger instance by name
     * @param name Logger name (component name)
     * @return Shared pointer to logger instance
     */
    static std::shared_ptr<spdlog::logger> get(const std::string& name);
    
    /**
     * @brief Shutdown logging system
     */
    static void shutdown();
    
    /**
     * @brief Set global log level
     * @param level Log level to set
     */
    static void setLevel(LogLevel level);
    
    /**
     * @brief Get current global log level
     * @return Current log level
     */
    static LogLevel getLevel();
    
    /**
     * @brief Parse log level from string (case-insensitive)
     * @param level_str Level string (TRACE, DEBUG, INFO, WARN, ERROR, CRITICAL, OFF)
     * @return Parsed log level, defaults to INFO for unknown strings
     */
    static LogLevel levelFromString(const std::string& level_str);
    
    /**
     * @brief Set the console sink log level (for dynamic toggling)
     * @param level Log level for console output (use OFF to disable)
     */
    static void setConsoleLevel(LogLevel level);
    
    /**
     * @brief Flush all loggers
     */
    static void flush();
    
    /**
     * @brief Structured logging for mount operations
     * @param event Log event with context
     * @param message Log message
     * @param level Log level
     */
    static void logEvent(const LogEvent& event, const std::string& message, LogLevel level = LogLevel::INFO);
    
    /**
     * @brief Audit logging for security/operations
     * @param event Audit event type
     * @param user User performing action (if applicable)
     * @param details Additional details
     */
    static void audit(const std::string& event, const std::string& user = "", 
                     const std::map<std::string, std::string>& details = {});
    
    /**
     * @brief Performance logging
     * @param operation Operation name
     * @param duration_ms Duration in milliseconds
     * @param success Whether operation succeeded
     * @param additional Additional context
     */
    static void performance(const std::string& operation, double duration_ms, 
                           bool success = true,
                           const std::map<std::string, std::string>& additional = {});
    
    /**
     * @brief Error logging with exception context
     * @param component Component where error occurred
     * @param error_message Error message
     * @param error_code Error code (if applicable)
     * @param context Additional context
     */
    static void error(const std::string& component, const std::string& error_message,
                     int error_code = 0, 
                     const std::map<std::string, std::string>& context = {});
    
    /**
     * @brief Mount operation specific logging
     * @param operation Operation name (slew, track, park, etc.)
     * @param status Operation status
     * @param ra Right ascension (if applicable)
     * @param dec Declination (if applicable)
     * @param additional Additional parameters
     */
    static void mountOperation(const std::string& operation, const std::string& status,
                              double ra = 0.0, double dec = 0.0,
                              const std::map<std::string, std::string>& additional = {});
    
    /**
     * @brief Calibration logging
     * @param calibration_type Type of calibration
     * @param measurement_count Number of measurements
     * @param residual_max Maximum residual
     * @param success Calibration success status
     */
    static void calibration(const std::string& calibration_type, int measurement_count,
                           double residual_max, bool success,
                           const std::map<std::string, std::string>& additional = {});
    
    /**
     * @brief System health logging
     * @param component Component name
     * @param metric_name Metric name
     * @param value Metric value
     * @param unit Metric unit
     * @param threshold Threshold value (if applicable)
     */
    static void health(const std::string& component, const std::string& metric_name,
                      double value, const std::string& unit = "",
                      double threshold = 0.0);
    
    /**
     * @brief Configuration change logging
     * @param config_path Configuration path
     * @param old_value Old value
     * @param new_value New value
     * @param user User making change (if applicable)
     */
    static void configChange(const std::string& config_path, const std::string& old_value,
                            const std::string& new_value, const std::string& user = "");
    
    /**
     * @brief Emergency/Safety event logging
     * @param safety_event Safety event type
     * @param severity Event severity (WARN, ERROR, CRITICAL)
     * @param reason Reason for event
     * @param actions_taken Actions taken
     */
    static void safetyEvent(const std::string& safety_event, LogLevel severity,
                           const std::string& reason,
                           const std::vector<std::string>& actions_taken = {});
    
    // Convenience methods for common loggers
    static std::shared_ptr<spdlog::logger> mount();
    static std::shared_ptr<spdlog::logger> api();
    static std::shared_ptr<spdlog::logger> canopen();
    static std::shared_ptr<spdlog::logger> tpoint();
    static std::shared_ptr<spdlog::logger> kalman();
    static std::shared_ptr<spdlog::logger> config();
    static std::shared_ptr<spdlog::logger> safety();
    
private:
    static bool initialized_;
    static std::string log_dir_;
    static size_t max_size_mb_;
    static size_t max_files_;
    static bool console_enabled_;
    static bool syslog_enabled_;
    static LogLevel level_;
    
    static std::shared_ptr<spdlog::sinks::basic_file_sink_mt> file_sink_;
    static std::shared_ptr<spdlog::sinks::stdout_color_sink_mt> console_sink_;
    static std::shared_ptr<spdlog::sinks::syslog_sink_mt> syslog_sink_;
    
    static std::map<std::string, std::shared_ptr<spdlog::logger>> loggers_;
    
    static void createDefaultLoggers();
    static spdlog::level::level_enum toSpdlogLevel(LogLevel level);
    static LogLevel fromSpdlogLevel(spdlog::level::level_enum level);
    
    static std::string formatContext(const std::map<std::string, std::string>& context);
    static std::string formatTimestamp(const std::chrono::system_clock::time_point& tp);
};

// Undefine conflicting syslog macros
#ifdef LOG_DEBUG
#undef LOG_DEBUG
#endif
#ifdef LOG_INFO
#undef LOG_INFO
#endif
#ifdef LOG_WARN
#undef LOG_WARN
#endif
#ifdef LOG_ERROR
#undef LOG_ERROR
#endif
#ifdef LOG_CRITICAL
#undef LOG_CRITICAL
#endif

// Convenience macros for easier logging
#define LOG_TRACE(logger, ...) SPDLOG_LOGGER_TRACE(logger, __VA_ARGS__)
#define LOG_DEBUG(logger, ...) SPDLOG_LOGGER_DEBUG(logger, __VA_ARGS__)
#define LOG_INFO(logger, ...) SPDLOG_LOGGER_INFO(logger, __VA_ARGS__)
#define LOG_WARN(logger, ...) SPDLOG_LOGGER_WARN(logger, __VA_ARGS__)
#define LOG_ERROR(logger, ...) SPDLOG_LOGGER_ERROR(logger, __VA_ARGS__)
#define LOG_CRITICAL(logger, ...) SPDLOG_LOGGER_CRITICAL(logger, __VA_ARGS__)

// Component-specific convenience macros
#define MOUNT_LOG_TRACE(...) LOG_TRACE(astro_mount::logging::Logger::mount(), __VA_ARGS__)
#define MOUNT_LOG_DEBUG(...) LOG_DEBUG(astro_mount::logging::Logger::mount(), __VA_ARGS__)
#define MOUNT_LOG_INFO(...) LOG_INFO(astro_mount::logging::Logger::mount(), __VA_ARGS__)
#define MOUNT_LOG_WARN(...) LOG_WARN(astro_mount::logging::Logger::mount(), __VA_ARGS__)
#define MOUNT_LOG_ERROR(...) LOG_ERROR(astro_mount::logging::Logger::mount(), __VA_ARGS__)
#define MOUNT_LOG_CRITICAL(...) LOG_CRITICAL(astro_mount::logging::Logger::mount(), __VA_ARGS__)

#define API_LOG_TRACE(...) LOG_TRACE(astro_mount::logging::Logger::api(), __VA_ARGS__)
#define API_LOG_DEBUG(...) LOG_DEBUG(astro_mount::logging::Logger::api(), __VA_ARGS__)
#define API_LOG_INFO(...) LOG_INFO(astro_mount::logging::Logger::api(), __VA_ARGS__)
#define API_LOG_WARN(...) LOG_WARN(astro_mount::logging::Logger::api(), __VA_ARGS__)
#define API_LOG_ERROR(...) LOG_ERROR(astro_mount::logging::Logger::api(), __VA_ARGS__)
#define API_LOG_CRITICAL(...) LOG_CRITICAL(astro_mount::logging::Logger::api(), __VA_ARGS__)

#define CANOPEN_LOG_TRACE(...) LOG_TRACE(astro_mount::logging::Logger::canopen(), __VA_ARGS__)
#define CANOPEN_LOG_DEBUG(...) LOG_DEBUG(astro_mount::logging::Logger::canopen(), __VA_ARGS__)
#define CANOPEN_LOG_INFO(...) LOG_INFO(astro_mount::logging::Logger::canopen(), __VA_ARGS__)
#define CANOPEN_LOG_WARN(...) LOG_WARN(astro_mount::logging::Logger::canopen(), __VA_ARGS__)
#define CANOPEN_LOG_ERROR(...) LOG_ERROR(astro_mount::logging::Logger::canopen(), __VA_ARGS__)
#define CANOPEN_LOG_CRITICAL(...) LOG_CRITICAL(astro_mount::logging::Logger::canopen(), __VA_ARGS__)

} // namespace logging
} // namespace astro_mount

#endif // LOGGER_H