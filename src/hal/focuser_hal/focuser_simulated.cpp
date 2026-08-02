#include "hal/focuser_control.h"
#include <cmath>
#include <random>
#include <thread>
#include <chrono>

namespace astro_mount {
namespace hal {

/**
 * @brief Simulated focuser for testing
 *
 * Simulates a focuser with configurable max position,
 * temperature response, and HFD curve for auto-focus testing.
 */
class SimulatedFocuser : public FocuserControl {
public:
    explicit SimulatedFocuser(int32_t max_position = 100000)
        : max_position_(max_position) {}

    std::string name() const override { return "simulated"; }

    bool connect() override {
        connected_ = true;
        position_ = max_position_ / 2;
        return true;
    }

    void disconnect() override {
        connected_ = false;
    }

    bool isConnected() const override { return connected_; }

    bool moveTo(int32_t position, int32_t speed, bool synchronous) override {
        if (!connected_ || position < 0 || position > max_position_) return false;

        moving_ = true;
        target_position_ = position;

        if (synchronous) {
            // Simulate movement time based on distance and speed
            int32_t distance = std::abs(position - position_);
            int sleep_ms = static_cast<int>((distance * 10.0) / std::max(speed, 1));
            std::this_thread::sleep_for(std::chrono::milliseconds(sleep_ms));
            position_ = position;
            moving_ = false;
        } else {
            // Simulate async movement
            position_ = position;
            moving_ = false;
        }

        return true;
    }

    void halt() override {
        moving_ = false;
    }

    FocuserStatus getStatus() override {
        FocuserStatus status;
        status.position = position_;
        status.max_position = max_position_;
        status.moving = moving_;
        status.connected = connected_;
        status.temperature_c = temperature_;
        status.model_name = "Simulated Focuser";
        status.step_size_microns = 10;
        return status;
    }

    double readTemperature() override {
        static std::default_random_engine gen(std::random_device{}());
        std::normal_distribution<double> noise(0.0, 0.1);
        temperature_ += noise(gen) * 0.1;
        return temperature_;
    }

    bool initialize() override {
        connected_ = true;
        position_ = max_position_ / 2;
        return true;
    }

    bool isReady() const override { return connected_; }

    int32_t getMaxPosition() const override { return max_position_; }

    void simulateHfdMeasurement(int32_t position) {
        // Simulate a V-curve: HFD = (p - p0)² * scale + minHFD + noise
        int32_t p0 = max_position_ / 2;  // Best focus at center
        double scale = 0.001;            // Curve steepness
        double min_hfd = 2.0;            // Minimum HFD
        double ideal_hfd = scale * std::pow(position - p0, 2) + min_hfd;

        static std::default_random_engine gen(std::random_device{}());
        std::normal_distribution<double> noise(0.0, 0.1);
        double measured_hfd = ideal_hfd + noise(gen);

        if (hfd_callback_) {
            hfd_callback_(measured_hfd);
        }
    }

private:
    int32_t max_position_;
    int32_t position_{0};
    int32_t target_position_{0};
    bool moving_{false};
    bool connected_{false};
    double temperature_{20.0};
};

} // namespace hal
} // namespace astro_mount
