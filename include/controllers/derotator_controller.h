#ifndef DEROTATOR_CONTROLLER_H
#define DEROTATOR_CONTROLLER_H
#include <memory>
#include <functional>
#include <atomic>
#include <array>
#include <thread>
#include <mutex>
#include "hal/derotator_control.h"
#include "models/field_rotation_model.h"

namespace astro_mount { namespace controllers {

/// Mount kind used to select the field-rotation model (P5).
/// Values match the `MountPositionUpdate.mount_type` proto field
/// (0=equatorial, 1=alt_az, 2=casual).
enum class MountKind {
    EQUATORIAL = 0,
    ALT_AZ = 1,
    CASUAL = 2
};

class DerotatorController {
public:
    explicit DerotatorController(std::unique_ptr<hal::DerotatorControl> hal);
    ~DerotatorController();

    bool setMode(hal::DerotatorMode mode);
    bool setAngle(double angle_deg);
    bool setRate(double rate_deg_s);
    bool home();
    hal::DerotatorStatus getStatus() const;
    models::FieldRotationResult getFieldRotation() const;

    /// Current mount kind used for field-rotation model selection.
    MountKind getMountKind() const;

    /**
     * @brief Feed the latest mount pointing so field rotation can be computed.
     *
     * The derotator needs the current telescope pointing (hour angle and
     * declination) plus the site latitude to compute the field-rotation angle
     * and rate that the derotator must counter. Stored thread-safely.
     *
     * @param mount_type Mount kind (EQUATORIAL/ALT_AZ/CASUAL) — selects the
     *                   field-rotation model (P5).
     * @param orientation_q Mount orientation quaternion [x,y,z,w] (CASUAL).
     */
    void setMountPosition(double axis1_deg, double axis2_deg,
                          double latitude_deg, double ha_hours, double dec_deg,
                          MountKind mount_type,
                          const std::array<double,4>& orientation_q);

    void setPositionCallback(std::function<void(double ax1, double ax2)> cb);

private:
    std::unique_ptr<hal::DerotatorControl> hal_;
    std::function<void(double, double)> position_callback_;
    hal::DerotatorMode mode_{hal::DerotatorMode::DISABLED};
    std::atomic<bool> running_{false};
    std::unique_ptr<std::thread> loop_thread_;

    // Latest mount pointing (guarded by position_mutex_)
    mutable std::mutex position_mutex_;
    double mount_axis1_{0.0};
    double mount_axis2_{0.0};
    double mount_latitude_{0.0};
    double mount_ha_hours_{0.0};
    double mount_dec_deg_{0.0};
    MountKind mount_type_{MountKind::EQUATORIAL};
    std::array<double,4> mount_orientation_{0.0, 0.0, 0.0, 1.0};  // [x,y,z,w] identity
};

}} // namespace astro_mount::controllers
#endif
