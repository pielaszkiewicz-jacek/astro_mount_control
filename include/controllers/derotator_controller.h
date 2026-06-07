#ifndef DEROTATOR_CONTROLLER_H
#define DEROTATOR_CONTROLLER_H

#include <memory>
#include <mutex>
#include <shared_mutex>
#include <thread>
#include <functional>
#include <vector>
#include <chrono>
#include "proto/mount_controller.pb.h"
#include "controllers/icanopen_interface.h"
#include "hal/motor_control.h"
#include "hal/encoder_reader.h"

namespace astro_mount {
namespace controllers {

/**
 * @brief Internal controller for derotator hardware and field rotation compensation.
 *
 * Extracted from MountController::Impl to reduce coupling (~500 lines, 12 member vars,
 * 2 HAL ptrs, 1 work thread dependency). Now a standalone module with its own mutex
 * and thread, injected into MountController::Impl as a member.
 *
 * MountController retains:
 *   - Field rotation rate calculation (depends on mount axis positions)
 *   - Pimpl delegator methods for backward-compatible gRPC API
 *
 * MountController pushes the computed field rotation rate into DerotatorController
 * via setFieldRotationRate(), which is then used by controlFieldRotation() when
 * the controller is in ALT_AZ or TRACKING mode.
 */
class DerotatorController {
public:
    /// Mount type enum (mirrors MountController::MountType for self-containment)
    enum class MountType {
        EQUATORIAL,
        ALT_AZ,
        UNKNOWN,
        CASUAL
    };

    /// Configuration for the derotator controller
    struct Config {
        ::astro_mount::DerotatorConfig derotator;
        MountType mount_type{MountType::EQUATORIAL};
        double latitude_deg{0.0};
    };

    /// Minimal mount state needed by DerotatorController for field rotation
    struct MountState {
        double altitude_deg{45.0};  ///< Current altitude (degrees)
    };

    /// Callback for MountController to provide current mount state
    using MountStateProvider = std::function<MountState()>;

    /**
     * @brief Construct a new DerotatorController
     * @param canopen Non-owning pointer to shared CANopen interface (may be null)
     * @param motor Owning pointer to HAL derotator motor (may be null)
     * @param encoder Owning pointer to HAL derotator encoder (may be null)
     * @param state_provider Callback to get current mount altitude from MountController
     * @param config Derotator configuration (type, gear ratio, etc.)
     */
    DerotatorController(
        ICanOpenInterface* canopen,
        std::unique_ptr<hal::MotorControl> motor,
        std::unique_ptr<hal::EncoderReader> encoder,
        MountStateProvider state_provider,
        const Config& config);

    ~DerotatorController();

    // Move-only (owns unique_ptr members and a thread)
    DerotatorController(DerotatorController&&) noexcept = default;
    DerotatorController& operator=(DerotatorController&&) noexcept = default;
    DerotatorController(const DerotatorController&) = delete;
    DerotatorController& operator=(const DerotatorController&) = delete;

    // ============================================
    // PUBLIC API — called by MountController pimpl delegators
    // ============================================

    /**
     * @brief Configure derotator hardware
     * @param config Derotator configuration (type, gear ratio, max speed, etc.)
     * @return True if configuration successful
     */
    bool configure(const ::astro_mount::DerotatorConfig& config);

    /**
     * @brief Enable or disable field rotation compensation
     *
     * Stores the params and enabled flag. The actual field rotation rate is
     * injected separately by MountController via setFieldRotationRate().
     *
     * @param params Field rotation parameters (enabled flag, altitude reference)
     * @return True if operation successful
     */
    bool enableFieldRotation(const ::astro_mount::FieldRotationParams& params);

    /**
     * @brief Control field rotation (position/rate control)
     *
     * Applies the requested control mode. For ALT_AZ and TRACKING modes,
     * uses the field rotation rate previously set via setFieldRotationRate().
     *
     * @param request Field rotation control request (mode, target angle, rate)
     * @return True if operation successful (derotator must be enabled)
     */
    bool controlFieldRotation(const ::astro_mount::FieldRotationControlRequest& request);

    /**
     * @brief Get derotator status
     * @return Current derotator status (enabled, homed, position, rates)
     */
    ::astro_mount::DerotatorStatus getStatus() const;

    /**
     * @brief Home derotator (find zero position)
     *
     * Runs asynchronously on an internal work thread. The homing sequence:
     *   1. Checks derotator is enabled and not already moving
     *   2. Moves to the requested offset position via CANopen or HAL
     *   3. Waits for target reached (polling with timeout)
     *   4. Optionally runs calibration after homing
     *
     * @param request Homing request parameters (offset, calibrate_after flag)
     * @return True if homing was initiated (or completed synchronously)
     */
    bool home(const ::astro_mount::DerotatorHomingRequest& request);

    /**
     * @brief Get field rotation parameters
     * @return Current field rotation parameters
     */
    ::astro_mount::FieldRotationParams getFieldRotationParams() const;

    /**
     * @brief Set the current field rotation rate (called by MountController)
     *
     * MountController computes the field rotation rate from mount axis positions
     * and pushes it here. This rate is used by controlFieldRotation() when the
     * controller is in ALT_AZ or TRACKING mode.
     *
     * @param rate_deg_s Field rotation rate in degrees/second
     */
    void setFieldRotationRate(double rate_deg_s);

    /**
     * @brief Clear error state — resets derotator_moving_ flag
     *
     * Called by MountController::clearErrors() to ensure the derotator moving
     * flag is reset when mount errors are cleared.
     */
    void clearErrors();

    /**
     * @brief Shutdown — stops work thread, disables motor, releases resources
     *
     * Must be called before destruction to ensure clean teardown.
     */
    void shutdown();

private:
    // ============================================
    // INTERNAL CALIBRATION METHODS
    // ============================================

    bool runDerotatorCalibration();
    bool runCANopenDerotatorCalibration();
    bool runBasicDerotatorCalibration();

    double waitForSettle(int axis_id, double target, double tolerance, int timeout_ms);
    double measureBacklash(int axis_id);
    bool calibrateAbsoluteEncoder(int axis_id);
    bool generateCalibrationTable(int axis_id, std::vector<double>& table);

    // ============================================
    // THREAD MANAGEMENT
    // ============================================

    void joinWorkThreadLocked();
    void joinWorkThread();

    // ============================================
    // MEMBER VARIABLES
    // ============================================

    // Derotator state
    ::astro_mount::DerotatorConfig derotator_config_;
    ::astro_mount::FieldRotationParams field_rotation_params_;
    bool derotator_enabled_{false};
    bool derotator_homed_{false};
    bool derotator_moving_{false};
    double derotator_current_angle_{0.0};
    double derotator_target_angle_{0.0};
    double derotator_current_rate_{0.0};
    double derotator_target_rate_{0.0};

    // Field rotation state
    bool field_rotation_enabled_{false};
    double field_rotation_rate_{0.0};  // Set by MountController via setFieldRotationRate()

    // Dependencies
    ICanOpenInterface* canopen_interface_;  // Non-owning, shared with MountController
    std::unique_ptr<hal::MotorControl> hal_motor_;
    std::unique_ptr<hal::EncoderReader> hal_encoder_;
    MountStateProvider state_provider_;
    Config config_;

    // Thread safety — own mutex and thread (no longer sharing MountController's)
    mutable std::shared_mutex mutex_;
    std::thread work_thread_;
    std::mutex thread_mutex_;
};

} // namespace controllers
} // namespace astro_mount

#endif // DEROTATOR_CONTROLLER_H
