#ifndef DEROTATOR_CONTROLLER_H
#define DEROTATOR_CONTROLLER_H
#include <memory>
#include <functional>
#include <atomic>
#include <thread>
#include "hal/derotator_control.h"
#include "models/field_rotation_model.h"

namespace astro_mount { namespace controllers {

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

    void setPositionCallback(std::function<void(double ax1, double ax2)> cb);

private:
    std::unique_ptr<hal::DerotatorControl> hal_;
    std::function<void(double, double)> position_callback_;
    hal::DerotatorMode mode_{hal::DerotatorMode::DISABLED};
    std::atomic<bool> running_{false};
    std::unique_ptr<std::thread> loop_thread_;
};

}} // namespace astro_mount::controllers
#endif
