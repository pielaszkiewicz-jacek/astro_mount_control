#ifndef DEROTATOR_CONTROL_H
#define DEROTATOR_CONTROL_H
#include <string>
#include <cstdint>

namespace astro_mount { namespace hal {

enum class DerotatorMode {
    DISABLED = 0,
    AUTO = 1,
    FIXED_ANGLE = 2,
    MANUAL_RATE = 3
};

struct DerotatorStatus {
    DerotatorMode mode{DerotatorMode::DISABLED};
    bool homed{false};
    double current_position_deg{0.0};
    double target_position_deg{0.0};
    double current_rate_deg_s{0.0};
    bool moving{false};
    bool error{false};
    std::string error_message;
};

class DerotatorControl {
public:
    virtual ~DerotatorControl() = default;
    virtual std::string name() const = 0;
    virtual bool connect() = 0;
    virtual void disconnect() = 0;
    virtual bool home() = 0;
    virtual bool setMode(DerotatorMode mode) = 0;
    virtual bool setAngle(double angle_deg) = 0;
    virtual bool setRate(double rate_deg_s) = 0;
    virtual DerotatorStatus getStatus() = 0;
    virtual bool isMoving() const = 0;
    virtual bool initialize() = 0;
};

}} // namespace astro_mount::hal
#endif
