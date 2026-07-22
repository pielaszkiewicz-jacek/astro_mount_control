#include "controllers/dome_controller.h"
#include <chrono>
#include <thread>
#include <cmath>

namespace astro_mount {
namespace controllers {

DomeController::DomeController(std::unique_ptr<hal::DomeControl> dome_hal)
    : dome_hal_(std::move(dome_hal)) {
}

DomeController::~DomeController() {
    running_ = false;
    if (sync_thread_ && sync_thread_->joinable()) {
        sync_thread_->join();
    }
}

bool DomeController::open() {
    if (!dome_hal_->isConnected()) return false;
    return dome_hal_->openShutter();
}

bool DomeController::close() {
    if (!dome_hal_->isConnected()) return false;
    return dome_hal_->closeShutter();
}

bool DomeController::rotateTo(double azimuth_deg) {
    if (!dome_hal_->isConnected()) return false;
    return dome_hal_->rotateTo(azimuth_deg, true);
}

bool DomeController::park() {
    if (!dome_hal_->isConnected()) return false;
    return dome_hal_->park();
}

bool DomeController::unpark() {
    if (!dome_hal_->isConnected()) return false;
    return dome_hal_->unpark();
}

hal::DomeStatus DomeController::getStatus() const {
    return dome_hal_->getStatus();
}

void DomeController::setMountAzimuthCallback(std::function<double()> callback) {
    mount_azimuth_callback_ = std::move(callback);
}

void DomeController::setAutoSync(bool enabled) {
    auto_sync_ = enabled;
    if (enabled && !sync_thread_) {
        running_ = true;
        sync_thread_ = std::make_unique<std::thread>(&DomeController::syncLoop, this);
    } else if (!enabled && sync_thread_) {
        running_ = false;
        if (sync_thread_->joinable()) sync_thread_->join();
        sync_thread_.reset();
    }
}

bool DomeController::isAutoSyncEnabled() const {
    return auto_sync_;
}

void DomeController::syncLoop() {
    while (running_) {
        if (auto_sync_ && mount_azimuth_callback_) {
            double mount_az = mount_azimuth_callback_();
            double dome_az = calculateDomeAzimuth(mount_az);
            auto status = dome_hal_->getStatus();

            // Only rotate if difference > 5 degrees and dome isn't already moving
            double diff = std::abs(dome_az - status.azimuth_deg);
            if (diff > 5.0 && !dome_hal_->isMoving()) {
                dome_hal_->rotateTo(dome_az, false);
            }
        }
        std::this_thread::sleep_for(std::chrono::seconds(10));
    }
}

double DomeController::calculateDomeAzimuth(double mount_az) const {
    // Simple dome sync: dome azimuth = mount azimuth + offset
    // Override this for more complex geometry (e.g. equatorial pier)
    return std::fmod(mount_az + 180.0, 360.0);
}

} // namespace controllers
} // namespace astro_mount
