#include "safety/watchdog.h"
#include "logging/logger.h"
#include <gtest/gtest.h>
#include <thread>
#include <chrono>

namespace astro_mount {
namespace safety {
namespace test {

using namespace std::chrono_literals;

// ============================================================================
// Test: WatchdogConfig default values
// ============================================================================
TEST(WatchdogConfigTest, DefaultValues) {
    WatchdogConfig config;

    // Timing parameters
    EXPECT_EQ(config.check_interval_ms, 1000);
    EXPECT_EQ(config.timeout_threshold_ms, 5000);
    EXPECT_EQ(config.max_consecutive_errors, 3);

    // Safety thresholds
    EXPECT_DOUBLE_EQ(config.max_position_error_arcsec, 10.0);
    EXPECT_DOUBLE_EQ(config.max_tracking_error_arcsec, 5.0);
    EXPECT_DOUBLE_EQ(config.max_velocity_error_arcsec, 1.0);
    EXPECT_DOUBLE_EQ(config.max_acceleration_error, 2.0);

    // Temperature thresholds
    EXPECT_DOUBLE_EQ(config.max_motor_temperature_c, 80.0);
    EXPECT_DOUBLE_EQ(config.max_controller_temperature_c, 70.0);

    // Power thresholds
    EXPECT_DOUBLE_EQ(config.min_voltage, 10.0);
    EXPECT_DOUBLE_EQ(config.max_current, 5.0);

    // Action configuration
    EXPECT_TRUE(config.enable_automatic_recovery);
    EXPECT_EQ(config.max_recovery_attempts, 3);
    EXPECT_EQ(config.recovery_delay_ms, 2000);

    // Logging configuration
    EXPECT_FALSE(config.log_all_checks);
    EXPECT_EQ(config.log_level, logging::LogLevel::WARN);
}

// ============================================================================
// Test: WatchdogConfig custom values
// ============================================================================
TEST(WatchdogConfigTest, CustomValues) {
    WatchdogConfig config;
    config.check_interval_ms = 500;
    config.timeout_threshold_ms = 10000;
    config.max_consecutive_errors = 5;
    config.max_position_error_arcsec = 30.0;
    config.max_tracking_error_arcsec = 15.0;
    config.max_motor_temperature_c = 90.0;
    config.min_voltage = 12.0;
    config.max_current = 8.0;
    config.enable_automatic_recovery = false;
    config.max_recovery_attempts = 5;
    config.log_all_checks = true;
    config.log_level = logging::LogLevel::DEBUG;

    EXPECT_EQ(config.check_interval_ms, 500);
    EXPECT_EQ(config.timeout_threshold_ms, 10000);
    EXPECT_EQ(config.max_consecutive_errors, 5);
    EXPECT_DOUBLE_EQ(config.max_position_error_arcsec, 30.0);
    EXPECT_DOUBLE_EQ(config.max_tracking_error_arcsec, 15.0);
    EXPECT_DOUBLE_EQ(config.max_motor_temperature_c, 90.0);
    EXPECT_DOUBLE_EQ(config.min_voltage, 12.0);
    EXPECT_DOUBLE_EQ(config.max_current, 8.0);
    EXPECT_FALSE(config.enable_automatic_recovery);
    EXPECT_EQ(config.max_recovery_attempts, 5);
    EXPECT_TRUE(config.log_all_checks);
    EXPECT_EQ(config.log_level, logging::LogLevel::DEBUG);
}

// ============================================================================
// Test: WatchdogStatus default values
// ============================================================================
TEST(WatchdogStatusTest, DefaultValues) {
    WatchdogStatus status;

    EXPECT_FALSE(status.active);
    EXPECT_FALSE(status.triggered);
    EXPECT_EQ(status.total_checks, 0);
    EXPECT_EQ(status.error_count, 0);
    EXPECT_EQ(status.consecutive_errors, 0);
    EXPECT_EQ(status.recovery_attempts, 0);
    EXPECT_DOUBLE_EQ(status.position_error_arcsec, 0.0);
    EXPECT_DOUBLE_EQ(status.tracking_error_arcsec, 0.0);
    EXPECT_DOUBLE_EQ(status.velocity_error_arcsec, 0.0);
    EXPECT_DOUBLE_EQ(status.motor_temperature_c, 0.0);
    EXPECT_DOUBLE_EQ(status.controller_temperature_c, 0.0);
    EXPECT_DOUBLE_EQ(status.voltage, 0.0);
    EXPECT_DOUBLE_EQ(status.current_axis1, 0.0);
    EXPECT_DOUBLE_EQ(status.current_axis2, 0.0);
    EXPECT_TRUE(status.last_trigger_condition.empty());
    EXPECT_FALSE(status.emergency_stop_triggered);
    EXPECT_TRUE(status.last_action_taken.empty());
}

// ============================================================================
// Test: WatchdogStatus populated values
// ============================================================================
TEST(WatchdogStatusTest, PopulatedValues) {
    WatchdogStatus status;
    auto now = std::chrono::system_clock::now();

    status.active = true;
    status.triggered = false;
    status.last_check_time = now;
    status.last_response_time = now;
    status.total_checks = 42;
    status.error_count = 3;
    status.consecutive_errors = 2;
    status.recovery_attempts = 1;
    status.position_error_arcsec = 2.5;
    status.tracking_error_arcsec = 1.2;
    status.velocity_error_arcsec = 0.3;
    status.motor_temperature_c = 55.0;
    status.controller_temperature_c = 45.0;
    status.voltage = 24.5;
    status.current_axis1 = 1.5;
    status.current_axis2 = 1.2;
    status.last_trigger_condition = "position_error_exceeded";
    status.last_trigger_time = now;
    status.last_action_taken = "emergency_stop";
    status.emergency_stop_triggered = true;

    EXPECT_TRUE(status.active);
    EXPECT_FALSE(status.triggered);
    EXPECT_EQ(status.last_check_time, now);
    EXPECT_EQ(status.last_response_time, now);
    EXPECT_EQ(status.total_checks, 42);
    EXPECT_EQ(status.error_count, 3);
    EXPECT_EQ(status.consecutive_errors, 2);
    EXPECT_EQ(status.recovery_attempts, 1);
    EXPECT_DOUBLE_EQ(status.position_error_arcsec, 2.5);
    EXPECT_DOUBLE_EQ(status.tracking_error_arcsec, 1.2);
    EXPECT_DOUBLE_EQ(status.velocity_error_arcsec, 0.3);
    EXPECT_DOUBLE_EQ(status.motor_temperature_c, 55.0);
    EXPECT_DOUBLE_EQ(status.controller_temperature_c, 45.0);
    EXPECT_DOUBLE_EQ(status.voltage, 24.5);
    EXPECT_DOUBLE_EQ(status.current_axis1, 1.5);
    EXPECT_DOUBLE_EQ(status.current_axis2, 1.2);
    EXPECT_EQ(status.last_trigger_condition, "position_error_exceeded");
    EXPECT_EQ(status.last_trigger_time, now);
    EXPECT_EQ(status.last_action_taken, "emergency_stop");
    EXPECT_TRUE(status.emergency_stop_triggered);
}

// ============================================================================
// Test: WatchdogEvent enum values
// ============================================================================
TEST(WatchdogEventTest, EnumValues) {
    EXPECT_NE(static_cast<int>(WatchdogEvent::MONITORING_STARTED),
              static_cast<int>(WatchdogEvent::MONITORING_STOPPED));
    EXPECT_NE(static_cast<int>(WatchdogEvent::CHECK_PASSED),
              static_cast<int>(WatchdogEvent::CHECK_FAILED));
    EXPECT_NE(static_cast<int>(WatchdogEvent::ERROR_THRESHOLD_EXCEEDED),
              static_cast<int>(WatchdogEvent::TIMEOUT_DETECTED));
    EXPECT_NE(static_cast<int>(WatchdogEvent::RECOVERY_ATTEMPT_STARTED),
              static_cast<int>(WatchdogEvent::RECOVERY_SUCCESSFUL));
    EXPECT_NE(static_cast<int>(WatchdogEvent::PERMANENT_STOP),
              static_cast<int>(WatchdogEvent::SYSTEM_RESUMED));

    // Verify all unique values
    EXPECT_EQ(static_cast<int>(WatchdogEvent::MONITORING_STARTED), 0);
    EXPECT_EQ(static_cast<int>(WatchdogEvent::MONITORING_STOPPED), 1);
    EXPECT_EQ(static_cast<int>(WatchdogEvent::CHECK_PASSED), 2);
    EXPECT_EQ(static_cast<int>(WatchdogEvent::CHECK_FAILED), 3);
    EXPECT_EQ(static_cast<int>(WatchdogEvent::ERROR_THRESHOLD_EXCEEDED), 4);
    EXPECT_EQ(static_cast<int>(WatchdogEvent::TIMEOUT_DETECTED), 5);
    EXPECT_EQ(static_cast<int>(WatchdogEvent::POSITION_ERROR_EXCEEDED), 6);
    EXPECT_EQ(static_cast<int>(WatchdogEvent::TRACKING_ERROR_EXCEEDED), 7);
    EXPECT_EQ(static_cast<int>(WatchdogEvent::TEMPERATURE_EXCEEDED), 8);
    EXPECT_EQ(static_cast<int>(WatchdogEvent::POWER_FAULT_DETECTED), 9);
    EXPECT_EQ(static_cast<int>(WatchdogEvent::EMERGENCY_STOP_TRIGGERED), 10);
    EXPECT_EQ(static_cast<int>(WatchdogEvent::RECOVERY_ATTEMPT_STARTED), 11);
    EXPECT_EQ(static_cast<int>(WatchdogEvent::RECOVERY_ATTEMPT_FAILED), 12);
    EXPECT_EQ(static_cast<int>(WatchdogEvent::RECOVERY_SUCCESSFUL), 13);
    EXPECT_EQ(static_cast<int>(WatchdogEvent::SYSTEM_RESUMED), 14);
    EXPECT_EQ(static_cast<int>(WatchdogEvent::PERMANENT_STOP), 15);
}

// ============================================================================
// Test: WatchdogCallback type (can be constructed and invoked)
// ============================================================================
TEST(WatchdogCallbackTest, CanBeConstructedAndInvoked) {
    bool called = false;
    WatchdogCallback callback = [&called](WatchdogEvent event, const std::string& condition,
                                          const WatchdogStatus& status) {
        called = true;
        EXPECT_EQ(event, WatchdogEvent::CHECK_FAILED);
        EXPECT_EQ(condition, "test_condition");
        EXPECT_FALSE(status.active);
    };

    WatchdogStatus status;
    callback(WatchdogEvent::CHECK_FAILED, "test_condition", status);
    EXPECT_TRUE(called);
}

// ============================================================================
// Test: WatchdogConfig copy semantics
// ============================================================================
TEST(WatchdogConfigTest, CopyAndAssignment) {
    WatchdogConfig original;
    original.check_interval_ms = 500;
    original.timeout_threshold_ms = 10000;
    original.max_consecutive_errors = 5;
    original.enable_automatic_recovery = false;

    // Copy construction
    WatchdogConfig copy(original);
    EXPECT_EQ(copy.check_interval_ms, 500);
    EXPECT_EQ(copy.timeout_threshold_ms, 10000);
    EXPECT_EQ(copy.max_consecutive_errors, 5);
    EXPECT_FALSE(copy.enable_automatic_recovery);

    // Copy assignment
    WatchdogConfig assigned;
    assigned = original;
    EXPECT_EQ(assigned.check_interval_ms, 500);
    EXPECT_EQ(assigned.timeout_threshold_ms, 10000);
    EXPECT_EQ(assigned.max_consecutive_errors, 5);
    EXPECT_FALSE(assigned.enable_automatic_recovery);

    // Modify original, verify copies are independent
    original.check_interval_ms = 2000;
    EXPECT_EQ(copy.check_interval_ms, 500);
    EXPECT_EQ(assigned.check_interval_ms, 500);
}

// ============================================================================
// Test: WatchdogStatus copy semantics
// ============================================================================
TEST(WatchdogStatusTest, CopyAndAssignment) {
    WatchdogStatus original;
    original.active = true;
    original.triggered = true;
    original.total_checks = 100;
    original.error_count = 5;
    original.last_trigger_condition = "test";

    // Copy construction
    WatchdogStatus copy(original);
    EXPECT_TRUE(copy.active);
    EXPECT_TRUE(copy.triggered);
    EXPECT_EQ(copy.total_checks, 100);
    EXPECT_EQ(copy.error_count, 5);
    EXPECT_EQ(copy.last_trigger_condition, "test");

    // Copy assignment
    WatchdogStatus assigned;
    assigned = original;
    EXPECT_TRUE(assigned.active);
    EXPECT_TRUE(assigned.triggered);
    EXPECT_EQ(assigned.total_checks, 100);
    EXPECT_EQ(assigned.error_count, 5);

    // Modify original, verify copies are independent
    original.active = false;
    EXPECT_TRUE(copy.active);
    EXPECT_TRUE(assigned.active);
}

// ============================================================================
// Test: SafetyManager enum SafetySubsystem values
// ============================================================================
TEST(SafetyManagerTest, SafetySubsystemEnum) {
    EXPECT_NE(static_cast<int>(SafetyManager::SafetySubsystem::WATCHDOG),
              static_cast<int>(SafetyManager::SafetySubsystem::EMERGENCY_STOP));
    EXPECT_NE(static_cast<int>(SafetyManager::SafetySubsystem::POSITION_LIMITS),
              static_cast<int>(SafetyManager::SafetySubsystem::VELOCITY_LIMITS));

    EXPECT_EQ(static_cast<int>(SafetyManager::SafetySubsystem::WATCHDOG), 0);
    EXPECT_EQ(static_cast<int>(SafetyManager::SafetySubsystem::EMERGENCY_STOP), 1);
    EXPECT_EQ(static_cast<int>(SafetyManager::SafetySubsystem::POSITION_LIMITS), 2);
    EXPECT_EQ(static_cast<int>(SafetyManager::SafetySubsystem::VELOCITY_LIMITS), 3);
    EXPECT_EQ(static_cast<int>(SafetyManager::SafetySubsystem::TEMPERATURE_MONITOR), 4);
    EXPECT_EQ(static_cast<int>(SafetyManager::SafetySubsystem::POWER_MONITOR), 5);
    EXPECT_EQ(static_cast<int>(SafetyManager::SafetySubsystem::COMMUNICATION_MONITOR), 6);
}

// ============================================================================
// Test: SafetyManager enum SafetyAction values
// ============================================================================
TEST(SafetyManagerTest, SafetyActionEnum) {
    EXPECT_EQ(static_cast<int>(SafetyManager::SafetyAction::NONE), 0);
    EXPECT_EQ(static_cast<int>(SafetyManager::SafetyAction::LOG_WARNING), 1);
    EXPECT_EQ(static_cast<int>(SafetyManager::SafetyAction::LOG_ERROR), 2);
    EXPECT_EQ(static_cast<int>(SafetyManager::SafetyAction::REDUCE_SPEED), 3);
    EXPECT_EQ(static_cast<int>(SafetyManager::SafetyAction::PAUSE_OPERATION), 4);
    EXPECT_EQ(static_cast<int>(SafetyManager::SafetyAction::EMERGENCY_STOP), 5);
    EXPECT_EQ(static_cast<int>(SafetyManager::SafetyAction::POWER_DOWN), 6);
    EXPECT_EQ(static_cast<int>(SafetyManager::SafetyAction::SYSTEM_RESET), 7);
}

// ============================================================================
// Test: SafetyEvent struct construction
// ============================================================================
TEST(SafetyManagerTest, SafetyEventConstruction) {
    SafetyManager::SafetyEvent event;
    event.subsystem = SafetyManager::SafetySubsystem::WATCHDOG;
    event.event_type = "timeout";
    event.description = "Watchdog timeout detected";
    event.severity = logging::LogLevel::ERROR;
    event.timestamp = std::chrono::system_clock::now();
    event.context["axis"] = "HA";
    event.context["timeout_ms"] = "5000";

    EXPECT_EQ(event.subsystem, SafetyManager::SafetySubsystem::WATCHDOG);
    EXPECT_EQ(event.event_type, "timeout");
    EXPECT_EQ(event.description, "Watchdog timeout detected");
    EXPECT_EQ(event.severity, logging::LogLevel::ERROR);
    EXPECT_EQ(event.context.size(), 2);
    EXPECT_EQ(event.context["axis"], "HA");
    EXPECT_EQ(event.context["timeout_ms"], "5000");
}

} // namespace test
} // namespace safety
} // namespace astro_mount
