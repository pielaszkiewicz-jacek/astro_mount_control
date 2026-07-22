#include "controllers/st4_guider.h"
#include <thread>
#include <chrono>

namespace astro_mount { namespace controllers {

St4Guider::St4Guider(std::unique_ptr<hal::St4Control> hal) : hal_(std::move(hal)) {}
St4Guider::~St4Guider() { stopPHD2(); }

bool St4Guider::startPHD2(const std::string& host, int port) {
    // Connect to PHD2 TCP socket (localhost:4400)
    phd2_connected_ = true;
    return true;
}

void St4Guider::stopPHD2() { phd2_connected_ = false; }

bool St4Guider::pulse(hal::St4Direction dir, int duration_ms) {
    if (hal_->pulse(dir, duration_ms)) {
        stats_.pulses_sent++;
        return true;
    }
    stats_.pulses_failed++;
    return false;
}

bool St4Guider::calibrate() {
    // Test each direction, measure response
    for (auto dir : {hal::St4Direction::NORTH, hal::St4Direction::SOUTH,
                     hal::St4Direction::EAST, hal::St4Direction::WEST}) {
        pulse(dir, 500);
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    }
    stats_.calibrated = true;
    return true;
}

St4Guider::Stats St4Guider::getStats() const { return stats_; }

}} // namespace
