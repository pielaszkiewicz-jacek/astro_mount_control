#include "logging/logger.h"
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <chrono>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace astro_mount {
namespace logging {

// Static member initialization
bool Logger::initialized_ = false;
std::string Logger::log_dir_ = "/var/log/astro-mount";
size_t Logger::max_size_mb_ = 100;
size_t Logger::max_files_ = 10;
bool Logger::console_enabled_ = true;
bool Logger::syslog_enabled_ = false;
LogLevel Logger::level_ = LogLevel::INFO;

std::shared_ptr<spdlog::sinks::basic_file_sink_mt> Logger::file_sink_ = nullptr;
std::shared_ptr<spdlog::sinks::stdout_color_sink_mt> Logger::console_sink_ = nullptr;
std::shared_ptr<spdlog::sinks::syslog_sink_mt> Logger::syslog_sink_ = nullptr;

std::map<std::string, std::shared_ptr<spdlog::logger>> Logger::loggers_;

bool Logger::init(const std::string& config_path) {
    if (initialized_) {
        return true;
    }
    
    try {
        if (!config_path.empty()) {
            std::ifstream config_file(config_path);
            if (config_file.is_open()) {
                json config = json::parse(config_file);
                
                log_dir_ = config.value("directory", "/var/log/astro-mount");
                max_size_mb_ = config.value("max_size_mb", 100);
                max_files_ = config.value("max_files", 10);
                console_enabled_ = config.value("console_output", true);
                syslog_enabled_ = config.value("syslog_enabled", false);
                
                std::string level_str = config.value("level", "INFO");
                if (level_str == "TRACE") level_ = LogLevel::TRACE;
                else if (level_str == "DEBUG") level_ = LogLevel::DEBUG;
                else if (level_str == "INFO") level_ = LogLevel::INFO;
                else if (level_str == "WARN") level_ = LogLevel::WARN;
                else if (level_str == "ERROR") level_ = LogLevel::ERROR;
                else if (level_str == "CRITICAL") level_ = LogLevel::CRITICAL;
                else if (level_str == "OFF") level_ = LogLevel::OFF;
            }
        }
        
        return initProgrammatic(log_dir_, max_size_mb_, max_files_, 
                               console_enabled_, syslog_enabled_, level_);
        
    } catch (const std::exception& e) {
        // Fall back to default initialization
        std::cerr << "Failed to initialize logger from config: " << e.what() 
                  << "\nFalling back to defaults." << std::endl;
        return initProgrammatic();
    }
}

bool Logger::initProgrammatic(const std::string& log_dir, size_t max_size_mb,
                             size_t max_files, bool console_enabled,
                             bool syslog_enabled, LogLevel level) {
    if (initialized_) {
        return true;
    }
    
    try {
        log_dir_ = log_dir;
        max_size_mb_ = max_size_mb;
        max_files_ = max_files;
        console_enabled_ = console_enabled;
        syslog_enabled_ = syslog_enabled;
        level_ = level;
        
        // Create log directory if it doesn't exist
        std::filesystem::create_directories(log_dir_);
        
        std::vector<spdlog::sink_ptr> sinks;
        
        // File sink (simple append; rotation handled externally by logrotate)
        std::string log_file = log_dir_ + "/astro-mount.log";
        file_sink_ = std::make_shared<spdlog::sinks::basic_file_sink_mt>(
            log_file, false);
        file_sink_->set_level(toSpdlogLevel(level_));
        sinks.push_back(file_sink_);
        
        // Console sink
        if (console_enabled_) {
            console_sink_ = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
            console_sink_->set_level(toSpdlogLevel(level_));
            sinks.push_back(console_sink_);
        }
        
        // Syslog sink (optional)
        if (syslog_enabled_) {
            syslog_sink_ = std::make_shared<spdlog::sinks::syslog_sink_mt>("astro-mount", 0, LOG_USER, true);
            syslog_sink_->set_level(toSpdlogLevel(level_));
            sinks.push_back(syslog_sink_);
        }
        
        // Create default loggers
        createDefaultLoggers();
        
        // Set global pattern
        spdlog::set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] [%n] %v");
        
        // Set global level
        spdlog::set_level(toSpdlogLevel(level_));
        
        // Register periodic flush
        spdlog::flush_every(std::chrono::seconds(5));
        
        initialized_ = true;
        
        // Log initialization
        get("logger")->info("Logging system initialized. Directory: {}, Level: {}", 
                           log_dir_, static_cast<int>(level_));
        
        return true;
        
    } catch (const std::exception& e) {
        std::cerr << "Failed to initialize logger: " << e.what() << std::endl;
        return false;
    }
}

std::shared_ptr<spdlog::logger> Logger::get(const std::string& name) {
    if (!initialized_) {
        // Try to initialize with defaults
        if (!initProgrammatic()) {
            // If initialization fails (e.g. permission denied on log dir),
            // check if this logger already exists in spdlog's global registry
            // before attempting to create it (spdlog::stdout_color_mt throws
            // logger_name_conflict for duplicate names).
            auto existing = spdlog::get(name);
            if (existing) {
                return existing;
            }
            // Create a simple console logger and register it in our map
            // so subsequent calls find it without re-creating
            try {
                auto logger = spdlog::stdout_color_mt(name);
                logger->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] [%n] %v");
                loggers_[name] = logger;
                return logger;
            } catch (const spdlog::spdlog_ex& e) {
                // spdlog registry conflict even after check — retrieve it
                return spdlog::get(name);
            }
        }
    }
    
    auto it = loggers_.find(name);
    if (it != loggers_.end()) {
        return it->second;
    }
    
    // Create new logger
    std::vector<spdlog::sink_ptr> sinks;
    if (file_sink_) sinks.push_back(file_sink_);
    if (console_enabled_ && console_sink_) sinks.push_back(console_sink_);
    if (syslog_enabled_ && syslog_sink_) sinks.push_back(syslog_sink_);
    
    auto logger = std::make_shared<spdlog::logger>(name, sinks.begin(), sinks.end());
    logger->set_level(toSpdlogLevel(level_));
    // Register with spdlog global registry so periodic flush_every() flushes this logger's sinks
    try {
        spdlog::register_logger(logger);
    } catch (const spdlog::spdlog_ex&) {
        // Logger name already registered; retrieve existing one
        auto existing = spdlog::get(name);
        if (existing) {
            logger = existing;
        }
    }
    loggers_[name] = logger;
    
    return logger;
}

void Logger::shutdown() {
    if (initialized_) {
        spdlog::shutdown();
        initialized_ = false;
        loggers_.clear();
        file_sink_.reset();
        console_sink_.reset();
        syslog_sink_.reset();
    }
}

void Logger::setLevel(LogLevel level) {
    level_ = level;
    spdlog::set_level(toSpdlogLevel(level));
    
    for (auto& [name, logger] : loggers_) {
        logger->set_level(toSpdlogLevel(level));
    }
    
    if (file_sink_) file_sink_->set_level(toSpdlogLevel(level));
    if (console_sink_) console_sink_->set_level(toSpdlogLevel(level));
    if (syslog_sink_) syslog_sink_->set_level(toSpdlogLevel(level));
}

LogLevel Logger::getLevel() {
    return level_;
}

void Logger::flush() {
    for (auto& [name, logger] : loggers_) {
        logger->flush();
    }
}

void Logger::logEvent(const LogEvent& event, const std::string& message, LogLevel level) {
    std::string formatted_msg = message;
    
    if (!event.component.empty()) {
        formatted_msg = "[" + event.component + "] " + formatted_msg;
    }
    
    if (!event.context.empty()) {
        formatted_msg += " " + formatContext(event.context);
    }
    
    formatted_msg += " [event=" + event.event_type + "]";
    formatted_msg += " [timestamp=" + formatTimestamp(event.timestamp) + "]";
    
    get("structured")->log(toSpdlogLevel(level), formatted_msg);
}

void Logger::audit(const std::string& event, const std::string& user, 
                   const std::map<std::string, std::string>& details) {
    std::string msg = "AUDIT: " + event;
    if (!user.empty()) {
        msg += " [user=" + user + "]";
    }
    
    if (!details.empty()) {
        msg += " " + formatContext(details);
    }
    
    get("audit")->info(msg);
}

void Logger::performance(const std::string& operation, double duration_ms, 
                        bool success, const std::map<std::string, std::string>& additional) {
    std::string msg = "PERFORMANCE: " + operation + 
                     " [duration_ms=" + std::to_string(duration_ms) + 
                     "] [success=" + (success ? "true" : "false") + "]";
    
    if (!additional.empty()) {
        msg += " " + formatContext(additional);
    }
    
    get("performance")->info(msg);
}

void Logger::error(const std::string& component, const std::string& error_message,
                  int error_code, const std::map<std::string, std::string>& context) {
    std::string msg = "ERROR: " + error_message + " [component=" + component + "]";
    
    if (error_code != 0) {
        msg += " [error_code=" + std::to_string(error_code) + "]";
    }
    
    if (!context.empty()) {
        msg += " " + formatContext(context);
    }
    
    get("error")->error(msg);
}

void Logger::mountOperation(const std::string& operation, const std::string& status,
                           double ra, double dec, const std::map<std::string, std::string>& additional) {
    std::string msg = "MOUNT_OP: " + operation + " [status=" + status + "]";
    
    if (ra != 0.0 || dec != 0.0) {
        msg += " [ra=" + std::to_string(ra) + "] [dec=" + std::to_string(dec) + "]";
    }
    
    if (!additional.empty()) {
        msg += " " + formatContext(additional);
    }
    
    get("mount")->info(msg);
}

void Logger::calibration(const std::string& calibration_type, int measurement_count,
                        double residual_max, bool success,
                        const std::map<std::string, std::string>& additional) {
    std::string msg = "CALIBRATION: " + calibration_type + 
                     " [measurements=" + std::to_string(measurement_count) + 
                     "] [max_residual=" + std::to_string(residual_max) + 
                     "] [success=" + (success ? "true" : "false") + "]";
    
    if (!additional.empty()) {
        msg += " " + formatContext(additional);
    }
    
    get("calibration")->info(msg);
}

void Logger::health(const std::string& component, const std::string& metric_name,
                   double value, const std::string& unit, double threshold) {
    std::string msg = "HEALTH: " + component + " [metric=" + metric_name + 
                     "] [value=" + std::to_string(value);
    
    if (!unit.empty()) {
        msg += unit;
    }
    msg += "]";
    
    if (threshold != 0.0) {
        msg += " [threshold=" + std::to_string(threshold) + 
               "] [status=" + (value <= threshold ? "OK" : "WARNING") + "]";
    }
    
    get("health")->info(msg);
}

void Logger::configChange(const std::string& config_path, const std::string& old_value,
                         const std::string& new_value, const std::string& user) {
    std::string msg = "CONFIG_CHANGE: " + config_path + 
                     " [old=" + old_value + "] [new=" + new_value + "]";
    
    if (!user.empty()) {
        msg += " [user=" + user + "]";
    }
    
    get("config")->info(msg);
}

void Logger::safetyEvent(const std::string& safety_event, LogLevel severity,
                        const std::string& reason, const std::vector<std::string>& actions_taken) {
    std::string msg = "SAFETY: " + safety_event + " [reason=" + reason + "]";
    
    if (!actions_taken.empty()) {
        msg += " [actions=";
        for (size_t i = 0; i < actions_taken.size(); ++i) {
            if (i > 0) msg += ",";
            msg += actions_taken[i];
        }
        msg += "]";
    }
    
    get("safety")->log(toSpdlogLevel(severity), msg);
}

// Convenience methods for common loggers
std::shared_ptr<spdlog::logger> Logger::mount() { return get("mount"); }
std::shared_ptr<spdlog::logger> Logger::api() { return get("api"); }
std::shared_ptr<spdlog::logger> Logger::canopen() { return get("canopen"); }
std::shared_ptr<spdlog::logger> Logger::tpoint() { return get("tpoint"); }
std::shared_ptr<spdlog::logger> Logger::kalman() { return get("kalman"); }
std::shared_ptr<spdlog::logger> Logger::config() { return get("config"); }
std::shared_ptr<spdlog::logger> Logger::safety() { return get("safety"); }

// Private methods
void Logger::createDefaultLoggers() {
    // Create default component loggers
    std::vector<std::string> default_loggers = {
        "mount", "api", "canopen", "tpoint", "kalman", "config", "safety",
        "structured", "audit", "performance", "error", "calibration", "health"
    };
    
    std::vector<spdlog::sink_ptr> sinks;
    if (file_sink_) sinks.push_back(file_sink_);
    if (console_enabled_ && console_sink_) sinks.push_back(console_sink_);
    if (syslog_enabled_ && syslog_sink_) sinks.push_back(syslog_sink_);
    
    for (const auto& name : default_loggers) {
        auto logger = std::make_shared<spdlog::logger>(name, sinks.begin(), sinks.end());
        logger->set_level(toSpdlogLevel(level_));
        // Register with spdlog global registry so periodic flush_every() flushes this logger's sinks
        try {
            spdlog::register_logger(logger);
        } catch (const spdlog::spdlog_ex&) {
            // Logger name already registered; retrieve existing one
            auto existing = spdlog::get(name);
            if (existing) {
                logger = existing;
            }
        }
        loggers_[name] = logger;
    }
}

spdlog::level::level_enum Logger::toSpdlogLevel(LogLevel level) {
    return static_cast<spdlog::level::level_enum>(level);
}

LogLevel Logger::fromSpdlogLevel(spdlog::level::level_enum level) {
    return static_cast<LogLevel>(level);
}

std::string Logger::formatContext(const std::map<std::string, std::string>& context) {
    std::string result;
    for (const auto& [key, value] : context) {
        if (!result.empty()) result += " ";
        result += "[" + key + "=" + value + "]";
    }
    return result;
}

std::string Logger::formatTimestamp(const std::chrono::system_clock::time_point& tp) {
    auto time_t = std::chrono::system_clock::to_time_t(tp);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        tp.time_since_epoch()) % 1000;
    
    std::stringstream ss;
    ss << std::put_time(std::gmtime(&time_t), "%Y-%m-%dT%H:%M:%S");
    ss << '.' << std::setfill('0') << std::setw(3) << ms.count() << "Z";
    
    return ss.str();
}

} // namespace logging
} // namespace astro_mount