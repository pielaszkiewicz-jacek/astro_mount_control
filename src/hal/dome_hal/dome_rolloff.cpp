#include "hal/dome_control.h"
#include <thread>
#include <chrono>

namespace astro_mount {
namespace hal {

class RollOffRoof : public DomeControl {
public:
    explicit RollOffRoof(const std::string& gpio_open = "gpio17",
                          const std::string& gpio_close = "gpio18",
                          int open_time_s = 30)
        : gpio_open_(gpio_open), gpio_close_(gpio_close), open_time_s_(open_time_s) {}

    std::string name() const override { return "rolloff_roof"; }

    bool connect() override {
        // Initialize GPIO pins for relay control
        connected_ = true;
        return true;
    }

    void disconnect() override { connected_ = false; }
    bool isConnected() const override { return connected_; }

    bool openShutter() override {
        if (!connected_ || state_ == DomeState::OPEN) return true;
        state_ = DomeState::OPENING;
        // Set GPIO high for open relay
        // sleep(open_time_s_)
        std::this_thread::sleep_for(std::chrono::seconds(open_time_s_));
        // Set GPIO low
        state_ = DomeState::OPEN;
        shutter_open_ = true;
        return true;
    }

    bool closeShutter() override {
        if (!connected_ || state_ == DomeState::CLOSED) return true;
        state_ = DomeState::CLOSING;
        // Set GPIO high for close relay
        std::this_thread::sleep_for(std::chrono::seconds(open_time_s_));
        state_ = DomeState::CLOSED;
        shutter_open_ = false;
        return true;
    }

    bool rotateTo(double, bool) override { return false; }
    bool halt() override { return true; }

    bool park() override { return closeShutter(); }
    bool unpark() override { return openShutter(); }
    bool goHome() override { return true; }

    DomeStatus getStatus() override {
        DomeStatus s;
        s.dome_type = DomeType::ROLLOFF;
        s.state = state_;
        s.can_rotate = false;
        s.can_open = true;
        s.shutter_open = shutter_open_;
        return s;
    }

    bool isMoving() const override {
        return state_ == DomeState::OPENING || state_ == DomeState::CLOSING;
    }

    DomeType getDomeType() const override { return DomeType::ROLLOFF; }

private:
    std::string gpio_open_, gpio_close_;
    int open_time_s_;
    DomeState state_{DomeState::CLOSED};
    bool connected_{false}, shutter_open_{false};
};

} // namespace hal
} // namespace astro_mount
