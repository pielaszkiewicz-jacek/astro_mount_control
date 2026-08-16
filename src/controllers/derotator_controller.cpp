#include "controllers/derotator_controller.h"
#include <chrono>
#include <thread>
#include <cmath>

namespace astro_mount { namespace controllers {

DerotatorController::DerotatorController(std::unique_ptr<hal::DerotatorControl> hal)
    : hal_(std::move(hal)) {}

DerotatorController::~DerotatorController() { running_ = false; if (loop_thread_ && loop_thread_->joinable()) loop_thread_->join(); }

bool DerotatorController::setMode(hal::DerotatorMode mode) { mode_ = mode; return hal_->setMode(mode); }
bool DerotatorController::setAngle(double angle_deg) { return hal_->setAngle(angle_deg); }
bool DerotatorController::setRate(double rate_deg_s) { return hal_->setRate(rate_deg_s); }
bool DerotatorController::home() { return hal_->home(); }
hal::DerotatorStatus DerotatorController::getStatus() const { return hal_->getStatus(); }

void DerotatorController::setMountPosition(double axis1_deg, double axis2_deg,
                                           double latitude_deg, double ha_hours,
                                           double dec_deg,
                                           MountKind mount_type,
                                           const std::array<double,4>& orientation_q) {
    std::lock_guard<std::mutex> lock(position_mutex_);
    mount_axis1_ = axis1_deg;
    mount_axis2_ = axis2_deg;
    mount_latitude_ = latitude_deg;
    mount_ha_hours_ = ha_hours;
    mount_dec_deg_ = dec_deg;
    mount_type_ = mount_type;
    mount_orientation_ = orientation_q;
}

MountKind DerotatorController::getMountKind() const {
    std::lock_guard<std::mutex> lock(position_mutex_);
    return mount_type_;
}

models::FieldRotationResult DerotatorController::getFieldRotation() const {
    // Compute the field rotation the derotator must counter-rotate against.
    // The field rotation angle (parallactic angle) and its rate depend on the
    // telescope pointing (hour angle, declination) and the site latitude.
    //
    // P5 FIX: the field-rotation model is now selected from the MOUNT TYPE fed
    // through setMountPosition():
    //   - EQUATORIAL → calculateEquatorial() (0 — an equatorial mount tracks the
    //     object, so the sensor sees no field rotation),
    //   - ALT_AZ     → calculateAltAz() (parallactic rotation),
    //   - CASUAL     → calculateCasual() with the mount orientation quaternion.
    // Previously the alt-az model was ALWAYS used, so an equatorial mount
    // (the default mount_type) reported a non-zero rotation rate (P5).
    double ha_hours = 0.0, dec_deg = 0.0, lat_deg = 0.0;
    double axis1 = 0.0, axis2 = 0.0;
    MountKind mtype = MountKind::EQUATORIAL;
    std::array<double,4> q{0.0, 0.0, 0.0, 1.0};
    {
        std::lock_guard<std::mutex> lock(position_mutex_);
        ha_hours = mount_ha_hours_;
        dec_deg  = mount_dec_deg_;
        lat_deg  = mount_latitude_;
        axis1    = mount_axis1_;
        axis2    = mount_axis2_;
        mtype    = mount_type_;
        q        = mount_orientation_;
    }

    // Guard: without a meaningful latitude/dec the model is undefined.
    if (!std::isfinite(ha_hours) || !std::isfinite(dec_deg) || !std::isfinite(lat_deg) ||
        std::abs(lat_deg) > 90.0 || std::abs(dec_deg) > 90.0) {
        return models::FieldRotationResult{};
    }

    switch (mtype) {
        case MountKind::EQUATORIAL:
            // No field rotation for an equatorial mount.
            return models::FieldRotationModel::calculateEquatorial();
        case MountKind::CASUAL:
            return models::FieldRotationModel::calculateCasual(
                axis1, axis2, q, ha_hours, dec_deg, lat_deg);
        case MountKind::ALT_AZ:
        default:
            return models::FieldRotationModel::calculateAltAz(ha_hours, dec_deg, lat_deg);
    }
}

void DerotatorController::setPositionCallback(std::function<void(double, double)> cb) {
    position_callback_ = std::move(cb);
}

}} // namespace
