#include "controllers/derotator_controller.h"
#include <chrono>
#include <thread>

namespace astro_mount { namespace controllers {

DerotatorController::DerotatorController(std::unique_ptr<hal::DerotatorControl> hal)
    : hal_(std::move(hal)) {}

DerotatorController::~DerotatorController() { running_ = false; if (loop_thread_ && loop_thread_->joinable()) loop_thread_->join(); }

bool DerotatorController::setMode(hal::DerotatorMode mode) { mode_ = mode; return hal_->setMode(mode); }
bool DerotatorController::setAngle(double angle_deg) { return hal_->setAngle(angle_deg); }
bool DerotatorController::setRate(double rate_deg_s) { return hal_->setRate(rate_deg_s); }
bool DerotatorController::home() { return hal_->home(); }
hal::DerotatorStatus DerotatorController::getStatus() const { return hal_->getStatus(); }

models::FieldRotationResult DerotatorController::getFieldRotation() const {
    // Would compute from mount position
    return {};
}

void DerotatorController::setPositionCallback(std::function<void(double, double)> cb) {
    position_callback_ = std::move(cb);
}

}} // namespace
