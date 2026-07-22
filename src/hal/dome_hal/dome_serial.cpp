#include "hal/dome_control.h"
#include <thread>
#include <chrono>
#include <sstream>
#include <algorithm>
#include <cmath>

namespace astro_mount {
namespace hal {

class SerialDome : public DomeControl {
public:
    explicit SerialDome(const std::string& port = "/dev/ttyUSB0", int baud = 9600)
        : port_(port), baud_(baud) {}

    std::string name() const override { return "serial_dome"; }

    bool connect() override {
        // open(port_.c_str(), O_RDWR | O_NOCTTY);
        connected_ = true;
        return true;
    }

    void disconnect() override { connected_ = false; }
    bool isConnected() const override { return connected_; }

    bool openShutter() override {
        if (!connected_) return false;
        // Send open command: ":Op#"
        state_ = DomeState::OPEN;
        shutter_open_ = true;
        return true;
    }

    bool closeShutter() override {
        if (!connected_) return false;
        // Send close command: ":Cl#"
        state_ = DomeState::CLOSED;
        shutter_open_ = false;
        return true;
    }

    bool rotateTo(double azimuth_deg, bool wait) override {
        if (!connected_) return false;
        target_azimuth_ = std::fmod(azimuth_deg, 360.0);
        if (target_azimuth_ < 0) target_azimuth_ += 360.0;
        // Send rotate command: ":RA+{azimuth*100}#"
        state_ = DomeState::ROTATING;
        if (wait) {
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            azimuth_ = target_azimuth_;
            state_ = DomeState::OPEN;
        }
        return true;
    }

    bool halt() override {
        // Send halt: ":FH#"
        return true;
    }

    bool park() override {
        return rotateTo(park_azimuth_, true);
    }

    bool unpark() override {
        parked_ = false;
        return true;
    }

    bool goHome() override {
        return rotateTo(0.0, true);
    }

    DomeStatus getStatus() override {
        // Query position: ":GP#"
        DomeStatus s;
        s.dome_type = DomeType::ROTATING;
        s.state = state_;
        s.azimuth_deg = azimuth_;
        s.target_azimuth_deg = target_azimuth_;
        s.can_rotate = true;
        s.shutter_open = shutter_open_;
        s.parked = parked_;
        return s;
    }

    bool isMoving() const override { return false; }
    DomeType getDomeType() const override { return DomeType::ROTATING; }

private:
    std::string port_;
    int baud_;
    DomeState state_{DomeState::CLOSED};
    double azimuth_{0.0}, target_azimuth_{0.0};
    double park_azimuth_{180.0};
    bool connected_{false}, shutter_open_{false}, parked_{false};
};

} // namespace hal
} // namespace astro_mount
