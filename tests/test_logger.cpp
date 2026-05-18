#include "logging/logger.h"
#include <gtest/gtest.h>
#include <thread>
#include <chrono>

namespace astro_mount {
namespace logging {
namespace test {

using namespace std::chrono_literals;

// ============================================================================
// Test: LogLevel enum values
// ============================================================================
TEST(LogLevelTest, EnumValues) {
    EXPECT_EQ(static_cast<int>(LogLevel::TRACE), static_cast<int>(spdlog::level::trace));
    EXPECT_EQ(static_cast<int>(LogLevel::DEBUG), static_cast<int>(spdlog::level::debug));
    EXPECT_EQ(static_cast<int>(LogLevel::INFO), static_cast<int>(spdlog::level::info));
    EXPECT_EQ(static_cast<int>(LogLevel::WARN), static_cast<int>(spdlog::level::warn));
    EXPECT_EQ(static_cast<int>(LogLevel::ERROR), static_cast<int>(spdlog::level::err));
    EXPECT_EQ(static_cast<int>(LogLevel::CRITICAL), static_cast<int>(spdlog::level::critical));
    EXPECT_EQ(static_cast<int>(LogLevel::OFF), static_cast<int>(spdlog::level::off));

    // Verify ordering
    EXPECT_LT(static_cast<int>(LogLevel::TRACE), static_cast<int>(LogLevel::DEBUG));
    EXPECT_LT(static_cast<int>(LogLevel::DEBUG), static_cast<int>(LogLevel::INFO));
    EXPECT_LT(static_cast<int>(LogLevel::INFO), static_cast<int>(LogLevel::WARN));
    EXPECT_LT(static_cast<int>(LogLevel::WARN), static_cast<int>(LogLevel::ERROR));
    EXPECT_LT(static_cast<int>(LogLevel::ERROR), static_cast<int>(LogLevel::CRITICAL));
    EXPECT_LT(static_cast<int>(LogLevel::CRITICAL), static_cast<int>(LogLevel::OFF));
}

// ============================================================================
// Test: LogEvent struct construction
// ============================================================================
TEST(LogEventTest, Construction) {
    auto now = std::chrono::system_clock::now();

    LogEvent event("test_event", "test_component");
    event.context["key1"] = "value1";
    event.context["key2"] = "value2";
    event.timestamp = now;

    EXPECT_EQ(event.event_type, "test_event");
    EXPECT_EQ(event.component, "test_component");
    EXPECT_EQ(event.context.size(), 2);
    EXPECT_EQ(event.context["key1"], "value1");
    EXPECT_EQ(event.context["key2"], "value2");
    EXPECT_EQ(event.timestamp, now);
}

// ============================================================================
// Test: LogEvent default values (empty type/component via two-arg constructor)
// ============================================================================
TEST(LogEventTest, DefaultValues) {
    LogEvent event("", "");
    EXPECT_TRUE(event.event_type.empty());
    EXPECT_TRUE(event.component.empty());
    EXPECT_TRUE(event.context.empty());
}

// ============================================================================
// Test: LogEvent addContext
// ============================================================================
TEST(LogEventTest, AddContext) {
    LogEvent event("test", "comp");
    event.addContext("key1", "value1");
    event.addContext("key2", "value2");
    EXPECT_EQ(event.context.size(), 2);
    EXPECT_EQ(event.context["key1"], "value1");
    EXPECT_EQ(event.context["key2"], "value2");
}

// ============================================================================
// Test: Logger initialization and shutdown
// ============================================================================
TEST(LoggerTest, InitAndShutdown) {
    // Initialize logger with empty config path
    EXPECT_TRUE(Logger::init(""));

    // Get default loggers
    auto mount_logger = Logger::mount();
    EXPECT_NE(mount_logger, nullptr);

    auto api_logger = Logger::api();
    EXPECT_NE(api_logger, nullptr);

    auto canopen_logger = Logger::canopen();
    EXPECT_NE(canopen_logger, nullptr);

    auto tpoint_logger = Logger::tpoint();
    EXPECT_NE(tpoint_logger, nullptr);

    auto kalman_logger = Logger::kalman();
    EXPECT_NE(kalman_logger, nullptr);

    auto config_logger = Logger::config();
    EXPECT_NE(config_logger, nullptr);

    auto safety_logger = Logger::safety();
    EXPECT_NE(safety_logger, nullptr);

    // Get by name
    auto named_logger = Logger::get("mount");
    EXPECT_NE(named_logger, nullptr);
    EXPECT_EQ(named_logger, mount_logger);

    // Get non-existent logger should return default logger
    auto nonexistent = Logger::get("nonexistent_component");
    EXPECT_NE(nonexistent, nullptr);

    Logger::shutdown();
}

// ============================================================================
// Test: Logger getLevel and setLevel
// ============================================================================
TEST(LoggerTest, GetSetLevel) {
    Logger::init("");

    EXPECT_EQ(Logger::getLevel(), LogLevel::INFO);  // Default level

    Logger::setLevel(LogLevel::DEBUG);
    EXPECT_EQ(Logger::getLevel(), LogLevel::DEBUG);

    Logger::setLevel(LogLevel::WARN);
    EXPECT_EQ(Logger::getLevel(), LogLevel::WARN);

    Logger::setLevel(LogLevel::ERROR);
    EXPECT_EQ(Logger::getLevel(), LogLevel::ERROR);

    Logger::setLevel(LogLevel::TRACE);
    EXPECT_EQ(Logger::getLevel(), LogLevel::TRACE);

    Logger::setLevel(LogLevel::CRITICAL);
    EXPECT_EQ(Logger::getLevel(), LogLevel::CRITICAL);

    Logger::setLevel(LogLevel::OFF);
    EXPECT_EQ(Logger::getLevel(), LogLevel::OFF);

    // Reset
    Logger::setLevel(LogLevel::INFO);
    EXPECT_EQ(Logger::getLevel(), LogLevel::INFO);

    Logger::shutdown();
}

// ============================================================================
// Test: Logger initProgrammatic
// ============================================================================
TEST(LoggerTest, InitProgrammatic) {
    Logger::shutdown();  // Ensure clean state

    EXPECT_TRUE(Logger::initProgrammatic(
        "/var/log/astro-mount",
        100,    // max_size_mb
        10,     // max_files
        true,   // enable_console
        false,  // enable_syslog
        LogLevel::DEBUG
    ));

    EXPECT_EQ(Logger::getLevel(), LogLevel::DEBUG);

    Logger::shutdown();
}

// ============================================================================
// Test: Logger operations (logging, audit, error, etc.)
// ============================================================================
TEST(LoggerTest, LoggingOperations) {
    Logger::init("");

    // These should not throw
    EXPECT_NO_THROW({
        Logger::mountOperation("slew", "completed", 10.5, 45.0, {});
    });

    EXPECT_NO_THROW({
        Logger::audit("user_login", "admin", {{"ip", "192.168.1.1"}});
    });

    EXPECT_NO_THROW({
        Logger::error("test_component", "test_error", 42, {{"detail", "something_failed"}});
    });

    EXPECT_NO_THROW({
        Logger::calibration("bootstrap", 10, 0.5, true, {});
    });

    EXPECT_NO_THROW({
        Logger::performance("slew_to_target", 1500.5, true, {});
    });

    EXPECT_NO_THROW({
        Logger::health("motor_ha", "temperature", 45.0, "C", 80.0);
    });

    EXPECT_NO_THROW({
        Logger::configChange("mount.max_slew_rate", "3.0", "5.0", "admin");
    });

    EXPECT_NO_THROW({
        Logger::safetyEvent("temperature_exceeded", LogLevel::WARN,
                           "Motor temperature exceeded threshold",
                           {"reduced_speed", "logged_warning"});
    });

    Logger::shutdown();
}

// ============================================================================
// Test: Logger logEvent
// ============================================================================
TEST(LoggerTest, LogEvent) {
    Logger::init("");

    LogEvent event("custom_event", "test");
    event.context["key"] = "value";

    EXPECT_NO_THROW({
        Logger::logEvent(event, "Custom message", LogLevel::INFO);
    });

    Logger::shutdown();
}

// ============================================================================
// Test: Logger flush
// ============================================================================
TEST(LoggerTest, Flush) {
    Logger::init("");

    EXPECT_NO_THROW({
        Logger::flush();
    });

    Logger::shutdown();
}

// ============================================================================
// Test: Logger re-initialization protection
// ============================================================================
TEST(LoggerTest, ReInitProtection) {
    Logger::init("");

    // Second init should be safe (idempotent)
    EXPECT_TRUE(Logger::init(""));

    Logger::shutdown();
}

// ============================================================================
// Test: Logger init with empty path returns true
// ============================================================================
TEST(LoggerTest, InitWithEmptyPath) {
    EXPECT_TRUE(Logger::init(""));
    Logger::shutdown();
}

} // namespace test
} // namespace logging
} // namespace astro_mount
