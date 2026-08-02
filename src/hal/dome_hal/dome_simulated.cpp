#include "hal/dome_control.h"
#include <cmath>
#include <thread>
#include <chrono>
#include <algorithm>

namespace astro_mount {
namespace hal {

class SimulatedDome : public DomeControl {
public:
    SimulatedDome(DomeType type = DomeType::ROTATING) : dome_type_(type) {}

    std::string name() const override { return "simulated_dome"; }

    bool connect() override { connected_ = true; return true; }
    void disconnect() override { connected_ = false; }
    bool isConnected() const override { return connected_; }

    bool openShutter() override {
        if (state_ == DomeState::OPEN) return true;
        state_ = DomeState::OPENING;
        std::this_thread::sleep_for(std::chrono::seconds(2));
        state_ = DomeState::OPEN;
        shutter_open_ = true;
        return true;
    }

    bool closeShutter() override {
        if (state_ == DomeState::CLOSED) return true;
        state_ = DomeState::CLOSING;
        std::this_thread::sleep_for(std::chrono::seconds(2));
        state_ = DomeState::CLOSED;
        shutter_open_ = false;
        return true;
    }

    bool rotateTo(double azimuth_deg, bool wait) override {
        if (!connected_ || dome_type_ != DomeType::ROTATING) return false;
        target_azimuth_ = std::fmod(azimuth_deg, 360.0);
        if (target_azimuth_ < 0) target_azimuth_ += 360.0;
        state_ = DomeState::ROTATING;
        if (wait) {
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            azimuth_ = target_azimuth_;
            state_ = shutter_open_ ? DomeState::OPEN : DomeState::CLOSED;
        }
        return true;
    }

    bool halt() override {
        if (state_ == DomeState::ROTATING) {
            state_ = DomeState::OPEN;
        }
        return true;
    }

    bool park() override {
        parked_ = true;
        return rotateTo(park_azimuth_, true);
    }

    bool unpark() override {
        parked_ = false;
        return true;
    }

    bool goHome() override {
        return rotateTo(home_azimuth_, true);
    }

    DomeStatus getStatus() override {
        DomeStatus s;
        s.dome_type = dome_type_;
        s.state = state_;
        s.azimuth_deg = azimuth_;
        s.target_azimuth_deg = target_azimuth_;
        s.can_rotate = (dome_type_ == DomeType::ROTATING);
        s.can_open = true;
        s.shutter_open = shutter_open_;
        s.parked = parked_;
        s.home_azimuth_deg = home_azimuth_;
        s.park_azimuth_deg = park_azimuth_;
        return s;
    }

    bool isMoving() const override {
        return state_ == DomeState::OPENING || state_ == DomeState::CLOSING || state_ == DomeState::ROTATING;
    }

    DomeType getDomeType() const override { return dome_type_; }

private:
    DomeType dome_type_;
    DomeState state_{DomeState::CLOSED};
    double azimuth_{0.0}, target_azimuth_{0.0};
    double home_azimuth_{0.0}, park_azimuth_{180.0};
    bool connected_{false}, shutter_open_{false}, parked_{false};
};

} // namespace hal
} // namespace astro_mount
