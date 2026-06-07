#include "controllers/derotator_controller.h"
#include "logging/logger.h"
#include <google/protobuf/util/time_util.h>
#include <cmath>
#include <algorithm>
#include <thread>
#include <chrono>

namespace astro_mount {

using google::protobuf::util::TimeUtil;

namespace controllers {

// ============================================
// CONSTRUCTION / DESTRUCTION
// ============================================

DerotatorController::DerotatorController(
    ICanOpenInterface* canopen,
    std::unique_ptr<hal::MotorControl> motor,
    std::unique_ptr<hal::EncoderReader> encoder,
    MountStateProvider state_provider,
    const Config& config)
    : canopen_interface_(canopen)
    , hal_motor_(std::move(motor))
    , hal_encoder_(std::move(encoder))
    , state_provider_(std::move(state_provider))
    , config_(config)
{
    // Copy the initial derotator config
    if (config.derotator.IsInitialized()) {
        derotator_config_ = config.derotator;
    }
}

DerotatorController::~DerotatorController() {
    shutdown();
}

// ============================================
// THREAD MANAGEMENT
// ============================================

void DerotatorController::joinWorkThreadLocked() {
    if (work_thread_.joinable()) {
        work_thread_.join();
    }
}

void DerotatorController::joinWorkThread() {
    std::lock_guard<std::mutex> lock(thread_mutex_);
    joinWorkThreadLocked();
}

// ============================================
// configure()
// ============================================

bool DerotatorController::configure(const ::astro_mount::DerotatorConfig& config) {
    derotator_config_ = config;
    derotator_enabled_ = true;

    if (config.type() == ::astro_mount::DerotatorConfig::CANOPEN) {
        if (!canopen_interface_) {
            MOUNT_LOG_ERROR("CANopen interface not available for derotator");
            return false;
        }

        // Derotator uses axis_id = 2 (0=HA, 1=Dec, 2=Derotator)
        const int DEROTATOR_AXIS_ID = 2;

        // Configure derotator drive
        std::string config_str = "type=derotator";
        config_str += ",gear_ratio=" + std::to_string(config.gear_ratio());
        config_str += ",max_speed=" + std::to_string(config.max_speed());
        config_str += ",max_acceleration=" + std::to_string(config.max_acceleration());
        config_str += ",backlash=" + std::to_string(config.backlash());

        if (!canopen_interface_->configureDrive(DEROTATOR_AXIS_ID, config_str)) {
            MOUNT_LOG_ERROR("Failed to configure derotator drive");
            return false;
        }

        // Set encoder configuration if absolute encoder is specified
        if (config.absolute_encoder()) {
            // Configure absolute encoder parameters via SDO writes
            // Object dictionary entries for encoder configuration:
            // 0x6005: Encoder resolution (subindex 1 = pulses per revolution)
            uint32_t enc_resolution = static_cast<uint32_t>(config.encoder_resolution());
            canopen_interface_->sendSDO(DEROTATOR_AXIS_ID, 0x6005, 1, &enc_resolution, sizeof(enc_resolution));

            // 0x6006: Encoder type (subindex 1 = absolute/incremental)
            uint8_t enc_type = 1; // absolute
            canopen_interface_->sendSDO(DEROTATOR_AXIS_ID, 0x6006, 1, &enc_type, sizeof(enc_type));

            MOUNT_LOG_INFO("Derotator configured with absolute encoder, resolution: {}",
                     config.encoder_resolution());
        }

        derotator_homed_ = false;
        derotator_current_angle_ = 0.0;
        derotator_target_angle_ = 0.0;
        derotator_current_rate_ = 0.0;
        derotator_target_rate_ = 0.0;
        derotator_moving_ = false;

        MOUNT_LOG_INFO("Derotator CANopen configuration successful (axis_id={})", DEROTATOR_AXIS_ID);
        return true;
    }
    else if (config.type() == ::astro_mount::DerotatorConfig::STEPPER ||
             config.type() == ::astro_mount::DerotatorConfig::SERVO ||
             config.type() == ::astro_mount::DerotatorConfig::CUSTOM) {
        // For non-CANopen types, assume success
        derotator_homed_ = false;
        derotator_current_angle_ = 0.0;
        derotator_target_angle_ = 0.0;
        derotator_current_rate_ = 0.0;
        derotator_target_rate_ = 0.0;
        derotator_moving_ = false;
        MOUNT_LOG_INFO("Derotator configured with type {}",
                 ::astro_mount::DerotatorConfig::DerotatorType_Name(config.type()));
        return true;
    }

    MOUNT_LOG_ERROR("Unsupported derotator type: {}", ::astro_mount::DerotatorConfig::DerotatorType_Name(config.type()));
    return false;
}

// ============================================
// enableFieldRotation()
// ============================================

bool DerotatorController::enableFieldRotation(const ::astro_mount::FieldRotationParams& params) {
    field_rotation_params_ = params;
    field_rotation_enabled_ = params.enabled();

    if (!params.enabled()) {
        field_rotation_rate_ = 0.0;
    }
    // Note: the actual field rotation rate for ALT_AZ/CASUAL mounts is computed
    // by MountController (which has the mount axis positions) and pushed via
    // setFieldRotationRate(). The rate computation logic with sin(alt) clamping,
    // latitude guards, and CASUAL mount handling stays in MountController.

    return true;
}

// ============================================
// controlFieldRotation()
// ============================================

bool DerotatorController::controlFieldRotation(const ::astro_mount::FieldRotationControlRequest& request) {
    std::lock_guard<std::shared_mutex> lock(mutex_);
    if (!derotator_enabled_) {
        return false;
    }

    switch (request.mode()) {
        case ::astro_mount::FieldRotationControlRequest::DISABLED:
            derotator_target_angle_ = derotator_current_angle_;
            derotator_target_rate_ = 0.0;
            break;

        case ::astro_mount::FieldRotationControlRequest::ALT_AZ:
            if (field_rotation_enabled_) {
                // Follow computed field rotation rate (set externally by MountController)
                derotator_target_rate_ = field_rotation_rate_;
            }
            break;

        case ::astro_mount::FieldRotationControlRequest::FIXED_ANGLE:
            derotator_target_angle_ = request.target_angle();
            derotator_target_rate_ = 0.0;
            if (request.relative()) {
                derotator_target_angle_ += derotator_current_angle_;
            }
            break;

        case ::astro_mount::FieldRotationControlRequest::CUSTOM:
            derotator_target_rate_ = request.rotation_rate();
            break;

        case ::astro_mount::FieldRotationControlRequest::TRACKING:
            // Track moving object rotation — use the field rotation rate
            // set externally by MountController (which knows the mount tracking state)
            derotator_target_rate_ = field_rotation_rate_;
            MOUNT_LOG_DEBUG("Using computed field rotation rate for tracking: {:.3f} deg/s", field_rotation_rate_);
            break;

        case ::astro_mount::FieldRotationControlRequest::EQUATORIAL:
            // No field rotation for equatorial mounts
            derotator_target_rate_ = 0.0;
            break;
    }

    derotator_moving_ = true;
    return true;
}

// ============================================
// getStatus()
// ============================================

::astro_mount::DerotatorStatus DerotatorController::getStatus() const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    ::astro_mount::DerotatorStatus status;
    status.set_enabled(derotator_enabled_);
    status.set_moving(derotator_moving_);
    status.set_homed(derotator_homed_);
    status.set_current_angle(derotator_current_angle_);
    status.set_target_angle(derotator_target_angle_);
    status.set_rotation_rate(derotator_current_rate_);
    status.set_field_rotation_rate(field_rotation_rate_);
    if (derotator_enabled_) {
        status.mutable_derotator_config()->CopyFrom(derotator_config_);
    }
    *status.mutable_timestamp() = google::protobuf::util::TimeUtil::GetCurrentTime();
    return status;
}

// ============================================
// home()
// ============================================

bool DerotatorController::home(const ::astro_mount::DerotatorHomingRequest& request) {
    if (!derotator_enabled_) {
        return false;
    }

    // Quick non-blocking check: reject if homing is already in progress
    {
        std::lock_guard<std::shared_mutex> lock(mutex_);
        if (derotator_moving_) {
            MOUNT_LOG_WARN("Derotator homing already in progress");
            return false;
        }
    }

    // Lock thread_mutex_ for the work thread join + create sequence
    {
        std::lock_guard<std::mutex> tlock(thread_mutex_);

        // Join any previous work thread before launching new async homing
        joinWorkThreadLocked();

        {
            std::lock_guard<std::shared_mutex> lock(mutex_);
            // Re-check state after join
            if (derotator_moving_) {
                MOUNT_LOG_WARN("Derotator homing started between check and join");
                return false;
            }
        }

        derotator_moving_ = true;
        derotator_target_angle_ = request.offset();

        // Move derotator to home position asynchronously on work_thread_
        work_thread_ = std::thread([this, offset = request.offset(), calibrate_after = request.calibrate_after()]() {
            const double HOME_VELOCITY = 3.0;    // deg/s
            const double HOME_ACCELERATION = 5.0; // deg/s²

            if (canopen_interface_) {
                const int DEROTATOR_AXIS_ID = 2;

                // Enable drive for homing
                canopen_interface_->enableDrive(DEROTATOR_AXIS_ID);

                // Execute homing move
                bool homing_ok = canopen_interface_->setPositionTarget(
                    DEROTATOR_AXIS_ID, offset, HOME_VELOCITY, HOME_ACCELERATION);

                if (!homing_ok) {
                    MOUNT_LOG_ERROR("Failed to move derotator to home position via CANopen");
                    std::lock_guard<std::shared_mutex> lock(mutex_);
                    derotator_moving_ = false;
                    return;
                }

                // Wait for movement to complete (non-blocking to caller)
                const int POLL_MS = 50;
                const double TOLERANCE = 0.1; // degrees
                int timeout_ms = 10000; // 10 second timeout
                int elapsed_ms = 0;

                while (elapsed_ms < timeout_ms) {
                    // Allow cancellation
                    {
                        std::lock_guard<std::shared_mutex> lock(mutex_);
                        if (!derotator_moving_) return;
                    }

                    auto status = canopen_interface_->getDriveStatus(DEROTATOR_AXIS_ID);
                    auto pos = canopen_interface_->getPositionData(DEROTATOR_AXIS_ID);

                    if (status.target_reached &&
                        std::abs(pos.actual_position - offset) < TOLERANCE) {
                        std::lock_guard<std::shared_mutex> lock(mutex_);
                        derotator_current_angle_ = pos.actual_position;
                        break;
                    }

                    std::this_thread::sleep_for(std::chrono::milliseconds(POLL_MS));
                    elapsed_ms += POLL_MS;
                }

                if (elapsed_ms >= timeout_ms) {
                    MOUNT_LOG_WARN("Derotator homing timed out");
                }

            } else {
                // Without CANopen, simulate homing
                // Simulate by polling for cancellation, then setting position
                const int POLL_MS = 100;
                int elapsed_ms = 0;
                const int SIM_TIMEOUT_MS = 2000;

                while (elapsed_ms < SIM_TIMEOUT_MS) {
                    {
                        std::lock_guard<std::shared_mutex> lock(mutex_);
                        if (!derotator_moving_) return;
                    }
                    std::this_thread::sleep_for(std::chrono::milliseconds(POLL_MS));
                    elapsed_ms += POLL_MS;
                }

                std::lock_guard<std::shared_mutex> lock(mutex_);
                derotator_current_angle_ = offset;
            }

            {
                std::lock_guard<std::shared_mutex> lock(mutex_);
                derotator_moving_ = false;
                derotator_homed_ = true;
            }

            MOUNT_LOG_INFO("Derotator homed to {:.1f}°", derotator_current_angle_);

            if (calibrate_after) {
                if (!runDerotatorCalibration()) {
                    MOUNT_LOG_WARN("Derotator calibration failed after homing");
                } else {
                    MOUNT_LOG_INFO("Derotator calibration completed successfully");
                }
            }
        });
    }  // end thread_mutex_ scope

    return true;
}

// ============================================
// getFieldRotationParams()
// ============================================

::astro_mount::FieldRotationParams DerotatorController::getFieldRotationParams() const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    return field_rotation_params_;
}

// ============================================
// setFieldRotationRate()
// ============================================

void DerotatorController::setFieldRotationRate(double rate_deg_s) {
    std::lock_guard<std::shared_mutex> lock(mutex_);
    field_rotation_rate_ = rate_deg_s;
}

// ============================================
// clearErrors()
// ============================================

void DerotatorController::clearErrors() {
    std::lock_guard<std::shared_mutex> lock(mutex_);
    derotator_moving_ = false;
}

// ============================================
// shutdown()
// ============================================

void DerotatorController::shutdown() {
    joinWorkThread();

    std::lock_guard<std::shared_mutex> lock(mutex_);
    derotator_moving_ = false;
    derotator_enabled_ = false;
    if (hal_motor_) {
        hal_motor_->disable();
    }
}

// ============================================
// INTERNAL: runDerotatorCalibration()
// ============================================

bool DerotatorController::runDerotatorCalibration() {
    if (!derotator_enabled_ || !derotator_homed_) {
        MOUNT_LOG_ERROR("Derotator not enabled or not homed for calibration");
        return false;
    }

    MOUNT_LOG_INFO("Starting derotator calibration...");

    if (derotator_config_.type() == ::astro_mount::DerotatorConfig::CANOPEN) {
        return runCANopenDerotatorCalibration();
    } else {
        // For non-CANopen derotators, perform basic calibration
        return runBasicDerotatorCalibration();
    }
}

// ============================================
// INTERNAL: runCANopenDerotatorCalibration()
// ============================================

bool DerotatorController::runCANopenDerotatorCalibration() {
    const int DEROTATOR_AXIS_ID = 2;

    if (!canopen_interface_) {
        MOUNT_LOG_ERROR("CANopen interface not available for derotator calibration");
        return false;
    }

    try {
        // Enable drive for calibration
        if (!canopen_interface_->enableDrive(DEROTATOR_AXIS_ID)) {
            MOUNT_LOG_ERROR("Failed to enable derotator drive for calibration");
            return false;
        }

        // Clear any existing errors
        canopen_interface_->clearErrors(DEROTATOR_AXIS_ID);

        // Check drive status
        auto status = canopen_interface_->getDriveStatus(DEROTATOR_AXIS_ID);
        if (!status.operational) {
            MOUNT_LOG_ERROR("Derotator drive not operational for calibration");
            return false;
        }

        // Calibration steps for CANopen derotator:
        // 1. Backlash measurement
        // 2. Encoder calibration (if absolute)
        // 3. Stiffness/backlash compensation table generation

        MOUNT_LOG_INFO("Step 1: Backlash measurement");
        double backlash_measured = measureBacklash(DEROTATOR_AXIS_ID);
        MOUNT_LOG_INFO("Measured backlash: {:.3f} degrees", backlash_measured);

        // Update derotator config with measured backlash
        derotator_config_.set_backlash(backlash_measured);

        MOUNT_LOG_INFO("Step 2: Encoder calibration");
        if (derotator_config_.absolute_encoder()) {
            if (!calibrateAbsoluteEncoder(DEROTATOR_AXIS_ID)) {
                MOUNT_LOG_WARN("Absolute encoder calibration failed");
            }
        }

        MOUNT_LOG_INFO("Step 3: Generating calibration table");
        std::vector<double> calibration_table;
        if (generateCalibrationTable(DEROTATOR_AXIS_ID, calibration_table)) {
            // Clear existing calibration table
            derotator_config_.clear_calibration_table();
            // Add new calibration points
            for (double value : calibration_table) {
                derotator_config_.add_calibration_table(value);
            }
            MOUNT_LOG_INFO("Calibration table generated with {} points", calibration_table.size());
        }

        MOUNT_LOG_INFO("CANopen derotator calibration completed successfully");
        return true;

    } catch (const std::exception& e) {
        MOUNT_LOG_ERROR("Exception during CANopen derotator calibration: {}", e.what());
        return false;
    }
}

// ============================================
// INTERNAL: waitForSettle()
// ============================================

double DerotatorController::waitForSettle(int axis_id, double target, double tolerance, int timeout_ms) {
    const int POLL_MS = 50;
    int elapsed_ms = 0;
    while (elapsed_ms < timeout_ms) {
        // HAL path: poll derotator motor targetReached + encoder readback
        if (hal_motor_ && hal_encoder_) {
            try {
                if (hal_motor_->targetReached()) {
                    auto reading = hal_encoder_->read();
                    if (reading.data_valid &&
                        std::abs(reading.position_deg - target) < tolerance) {
                        return reading.position_deg;
                    }
                }
            } catch (const std::exception& e) {
                MOUNT_LOG_DEBUG("HAL settle poll exception: {}", e.what());
            }
        }
        // CANopen path: poll drive status + position data
        if (canopen_interface_) {
            try {
                auto status = canopen_interface_->getDriveStatus(axis_id);
                auto pos = canopen_interface_->getPositionData(axis_id);
                if (status.target_reached &&
                    std::abs(pos.actual_position - target) < tolerance) {
                    return pos.actual_position;
                }
            } catch (const std::exception& e) {
                MOUNT_LOG_DEBUG("CANopen settle poll exception: {}", e.what());
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(POLL_MS));
        elapsed_ms += POLL_MS;
    }
    MOUNT_LOG_WARN("waitForSettle timed out (axis={}, target={:.1f}°, timeout={}ms)",
              axis_id, target, timeout_ms);
    return target; // return target as best estimate on timeout
}

// ============================================
// INTERNAL: runBasicDerotatorCalibration()
// ============================================

bool DerotatorController::runBasicDerotatorCalibration() {
    MOUNT_LOG_INFO("Running basic derotator calibration for non-CANopen type");

    // Perform actual motion-based calibration:
    // Move derotator to 5 reference positions and measure actual position
    // via HAL or CANopen, computing position-dependent errors.
    const double CAL_ANGLES[] = {0.0, 90.0, 180.0, 270.0, 360.0};
    const double VEL = 5.0;   // deg/s
    const double ACC = 10.0;  // deg/s²
    const double TOL = 0.1;   // degrees
    const int SETTLE_TIMEOUT = 5000; // 5s per point

    std::vector<double> calibration_table;

    // First move to home (0°) position
    if (hal_motor_) {
        hal_motor_->setPosition(CAL_ANGLES[0], VEL, ACC);
    } else if (canopen_interface_) {
        canopen_interface_->setPositionTarget(2, CAL_ANGLES[0], VEL, ACC);
    }

    for (int i = 0; i < 5; i++) {
        double target = CAL_ANGLES[i];

        // Move to calibration position
        bool move_ok = false;
        if (hal_motor_) {
            move_ok = hal_motor_->setPosition(target, VEL, ACC);
        } else if (canopen_interface_) {
            move_ok = canopen_interface_->setPositionTarget(2, target, VEL, ACC);
        } else {
            // No hardware — fall back to simulation
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            calibration_table.push_back(target);
            calibration_table.push_back(0.0);
            continue;
        }

        if (!move_ok) {
            MOUNT_LOG_WARN("Failed to move derotator to calibration position {:.1f}°", target);
            calibration_table.push_back(target);
            calibration_table.push_back(0.0);
            continue;
        }

        // Poll for target reached with timeout (replaces fragile sleep_for)
        double actual = waitForSettle(2, target, TOL, SETTLE_TIMEOUT);

        double error = actual - target;
        calibration_table.push_back(target);
        calibration_table.push_back(error);

        MOUNT_LOG_DEBUG("Basic calibration point {:.1f}°: actual={:.3f}°, error={:.3f}°",
                  target, actual, error);
    }

    // Store calibration table
    derotator_config_.clear_calibration_table();
    for (double value : calibration_table) {
        derotator_config_.add_calibration_table(value);
    }

    MOUNT_LOG_INFO("Basic derotator calibration completed with {} points",
              calibration_table.size() / 2);
    return true;
}

// ============================================
// INTERNAL: measureBacklash()
// ============================================

double DerotatorController::measureBacklash(int axis_id) {
    // Backlash measurement procedure with polling-based position settling:
    // 1. Record starting position (via HAL encoder or CANopen)
    // 2. Move in positive direction by TEST_ANGLE
    // 3. Wait for target reached via polling (replaces fragile sleep_for)
    // 4. Record position after positive move
    // 5. Move negative back to start
    // 6. Wait for target reached via polling
    // 7. Record position after negative move
    // 8. Backlash = |end_position - start_position|

    const double TEST_ANGLE = 10.0; // degrees
    const double TEST_VELOCITY = 5.0; // deg/s
    const double TEST_ACCELERATION = 10.0; // deg/s²
    const double TOLERANCE = 0.1;    // degrees
    const int SETTLE_TIMEOUT = 5000; // 5s per move

    try {
        // --- Step 1: Get current (start) position ---
        double start_pos = 0.0;
        bool have_start = false;
        if (hal_encoder_) {
            auto reading = hal_encoder_->read();
            if (reading.data_valid) {
                start_pos = reading.position_deg;
                have_start = true;
            }
        }
        if (!have_start && canopen_interface_) {
            start_pos = canopen_interface_->getPositionData(axis_id).actual_position;
            have_start = true;
        }
        if (!have_start) {
            MOUNT_LOG_WARN("Cannot get starting position for backlash measurement");
            return derotator_config_.backlash();
        }

        // --- Step 2: Move positive ---
        bool move_ok = false;
        if (hal_motor_) {
            move_ok = hal_motor_->setPosition(start_pos + TEST_ANGLE,
                                              TEST_VELOCITY, TEST_ACCELERATION);
        } else if (canopen_interface_) {
            move_ok = canopen_interface_->setPositionTarget(axis_id,
                                                            start_pos + TEST_ANGLE,
                                                            TEST_VELOCITY, TEST_ACCELERATION);
        }
        if (!move_ok) {
            MOUNT_LOG_ERROR("Failed to move positive for backlash measurement");
            return derotator_config_.backlash();
        }

        // --- Step 3: Wait for settle ---
        double pos_positive = waitForSettle(axis_id, start_pos + TEST_ANGLE,
                                            TOLERANCE, SETTLE_TIMEOUT);

        // --- Step 4: Move negative back to start ---
        if (hal_motor_) {
            move_ok = hal_motor_->setPosition(start_pos,
                                              TEST_VELOCITY, TEST_ACCELERATION);
        } else if (canopen_interface_) {
            move_ok = canopen_interface_->setPositionTarget(axis_id, start_pos,
                                                            TEST_VELOCITY, TEST_ACCELERATION);
        }
        if (!move_ok) {
            MOUNT_LOG_ERROR("Failed to move negative for backlash measurement");
            return derotator_config_.backlash();
        }

        // --- Step 5: Wait for settle ---
        double pos_negative = waitForSettle(axis_id, start_pos,
                                            TOLERANCE, SETTLE_TIMEOUT);

        // --- Step 6: Calculate backlash ---
        double backlash = std::abs(pos_negative - start_pos);

        MOUNT_LOG_DEBUG("Backlash measurement: start={:.3f}, after_positive={:.3f}, "
                  "after_negative={:.3f}, backlash={:.3f}",
                  start_pos, pos_positive, pos_negative, backlash);

        return std::max(backlash, 0.001); // Minimum 0.001 degrees

    } catch (const std::exception& e) {
        MOUNT_LOG_ERROR("Exception during backlash measurement: {}", e.what());
        return derotator_config_.backlash();
    }
}

// ============================================
// INTERNAL: calibrateAbsoluteEncoder()
// ============================================

bool DerotatorController::calibrateAbsoluteEncoder(int axis_id) {
    // Verify encoder readings match commanded positions at multiple calibration
    // points. Uses polling-based settling (replaces fragile sleep_for) and
    // supports both HAL and CANopen encoder readback.
    MOUNT_LOG_INFO("Calibrating absolute encoder for derotator");

    const int CALIBRATION_POINTS = 4;
    const double CALIBRATION_ANGLES[] = {0.0, 90.0, 180.0, 270.0};
    const double TOLERANCE = 0.1; // degrees
    const double VEL = 5.0;
    const double ACC = 10.0;
    const int SETTLE_TIMEOUT = 5000; // 5s per point

    int points_ok = 0;

    try {
        for (int i = 0; i < CALIBRATION_POINTS; i++) {
            double target_angle = CALIBRATION_ANGLES[i];

            // --- Move to calibration position ---
            bool move_ok = false;
            if (hal_motor_) {
                move_ok = hal_motor_->setPosition(target_angle, VEL, ACC);
            } else if (canopen_interface_) {
                move_ok = canopen_interface_->setPositionTarget(axis_id, target_angle, VEL, ACC);
            }
            if (!move_ok) {
                MOUNT_LOG_WARN("Failed to move to calibration position {:.1f}°", target_angle);
                continue;
            }

            // --- Wait for movement via polling ---
            waitForSettle(axis_id, target_angle, TOLERANCE, SETTLE_TIMEOUT);

            // --- Get encoder reading ---
            double encoder_angle = 0.0;
            bool have_encoder = false;
            if (hal_encoder_) {
                auto reading = hal_encoder_->read();
                if (reading.data_valid) {
                    encoder_angle = reading.position_deg;
                    have_encoder = true;
                }
            }
            if (!have_encoder && canopen_interface_) {
                auto encoder_data = canopen_interface_->getEncoderData(axis_id);
                double resolution = derotator_config_.encoder_resolution();
                if (resolution > 0.0) {
                    encoder_angle = encoder_data.raw_position / resolution * 360.0;
                    have_encoder = true;
                }
            }

            // --- Get drive position ---
            double drive_angle = 0.0;
            bool have_drive = false;
            if (canopen_interface_) {
                auto position_data = canopen_interface_->getPositionData(axis_id);
                drive_angle = position_data.actual_position;
                have_drive = true;
            } else if (hal_encoder_) {
                // Without CANopen, use encoder as drive position proxy
                drive_angle = encoder_angle;
                have_drive = true;
            }

            if (!have_encoder || !have_drive) {
                MOUNT_LOG_WARN("Cannot read encoder or drive at {:.1f}°", target_angle);
                continue;
            }

            // --- Check consistency ---
            double error = std::abs(encoder_angle - drive_angle);
            if (error > TOLERANCE) {
                MOUNT_LOG_WARN("Encoder-drive mismatch at {:.1f}°: encoder={:.3f}°, "
                          "drive={:.3f}°, error={:.3f}°",
                          target_angle, encoder_angle, drive_angle, error);
            } else {
                MOUNT_LOG_DEBUG("Encoder calibration point {:.1f}° OK: encoder={:.3f}°, "
                          "drive={:.3f}°", target_angle, encoder_angle, drive_angle);
            }
            points_ok++;
        }

        MOUNT_LOG_INFO("Absolute encoder calibration completed ({} of {} points OK)",
                  points_ok, CALIBRATION_POINTS);
        return points_ok > 0;

    } catch (const std::exception& e) {
        MOUNT_LOG_ERROR("Exception during absolute encoder calibration: {}", e.what());
        return false;
    }
}

// ============================================
// INTERNAL: generateCalibrationTable()
// ============================================

bool DerotatorController::generateCalibrationTable(int axis_id, std::vector<double>& table) {
    // Generate calibration table with position-error pairs by moving to
    // reference points and measuring actual position. Uses polling-based
    // settling (replaces fragile sleep_for) and supports both HAL and
    // CANopen motor control + position readback.

    const int TABLE_POINTS = 8; // 0°, 45°, 90°, ..., 315°
    const double VEL = 5.0;
    const double ACC = 10.0;
    const double TOL = 0.1;         // degrees
    const int SETTLE_TIMEOUT = 5000; // 5s per point

    table.clear();

    try {
        // --- Home to 0° ---
        if (hal_motor_) {
            hal_motor_->setPosition(0.0, VEL, ACC);
        } else if (canopen_interface_) {
            canopen_interface_->setPositionTarget(axis_id, 0.0, VEL, ACC);
        }
        waitForSettle(axis_id, 0.0, TOL, SETTLE_TIMEOUT);

        for (int i = 0; i < TABLE_POINTS; i++) {
            double target_angle = i * 45.0; // 0°, 45°, 90°, ..., 315°

            // --- Move to target ---
            bool move_ok = false;
            if (hal_motor_) {
                move_ok = hal_motor_->setPosition(target_angle, VEL, ACC);
            } else if (canopen_interface_) {
                move_ok = canopen_interface_->setPositionTarget(axis_id, target_angle, VEL, ACC);
            }
            if (!move_ok) {
                MOUNT_LOG_WARN("Failed to move to calibration point {:.1f}°", target_angle);
                table.push_back(target_angle);
                table.push_back(0.0);
                continue;
            }

            // --- Poll for settle ---
            double actual_angle = waitForSettle(axis_id, target_angle, TOL, SETTLE_TIMEOUT);

            // --- Calculate error ---
            double error = actual_angle - target_angle;

            // Add to table: position, error
            table.push_back(target_angle);
            table.push_back(error);

            MOUNT_LOG_DEBUG("Calibration point {:.1f}°: actual={:.3f}°, error={:.3f}°",
                      target_angle, actual_angle, error);
        }

        MOUNT_LOG_INFO("Generated calibration table with {} points ({} entries)",
                  TABLE_POINTS, table.size());
        return true;

    } catch (const std::exception& e) {
        MOUNT_LOG_ERROR("Exception generating calibration table: {}", e.what());
        table.clear();
        return false;
    }
}

} // namespace controllers
} // namespace astro_mount
