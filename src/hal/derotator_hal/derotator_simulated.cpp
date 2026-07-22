#include "hal/derotator_control.h"
#include <cmath>
#include <thread>
#include <chrono>

namespace astro_mount { namespace hal {

class SimulatedDerotator : public DerotatorControl {
public:
    std::string name() const override { return "simulated_derotator"; }
    bool connect() override { connected_ = true; return true; }
    void disconnect() override { connected_ = false; }
    bool home() override {
        homed_ = true; position_ = 0;
        std::this_thread::sleep_for(std::chrono::seconds(1));
        return true;
    }
    bool setMode(DerotatorMode m) override { mode_ = m; return true; }
    bool setAngle(double a) override {
        target_ = a;
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        position_ = a;
        return true;
    }
    bool setRate(double r) override { rate_ = r; return true; }
    DerotatorStatus getStatus() override {
        DerotatorStatus s; s.mode = mode_; s.homed = homed_; s.current_position_deg = position_;
        s.target_position_deg = target_; s.current_rate_deg_s = rate_; return s;
    }
    bool isMoving() const override { return false; }
    bool initialize() override { return true; }
private:
    bool connected_{false}, homed_{false};
    double position_{0}, target_{0}, rate_{0};
    DerotatorMode mode_{DerotatorMode::DISABLED};
};

}} // namespace
