#include "weather/sensors/gps_receiver.h"
#include <chrono>

namespace astro_mount {
namespace weather {

// ─── SimulatedGpsReceiver ──────────────────────────────────────────────────

SimulatedGpsReceiver::SimulatedGpsReceiver(double lat, double lon, double alt) {
    position_.latitude = lat;
    position_.longitude = lon;
    position_.altitude_m = alt;
    position_.fix_valid = true;
    position_.satellites_visible = 8;
    position_.hdop = 1.2;
}

bool SimulatedGpsReceiver::initialize() {
    initialized_ = true;
    return true;
}

GpsData SimulatedGpsReceiver::readPosition() {
    position_.timestamp = std::chrono::system_clock::now();
    return position_;
}

bool SimulatedGpsReceiver::hasFix() const {
    return position_.fix_valid;
}

bool SimulatedGpsReceiver::isOperational() const {
    return initialized_;
}

void SimulatedGpsReceiver::shutdown() {
    initialized_ = false;
}

void SimulatedGpsReceiver::setSimulatedPosition(double lat, double lon, double alt) {
    position_.latitude = lat;
    position_.longitude = lon;
    position_.altitude_m = alt;
    position_.timestamp = std::chrono::system_clock::now();
}

} // namespace weather
} // namespace astro_mount
