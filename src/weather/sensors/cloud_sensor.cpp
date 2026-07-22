#include "weather/sensors/cloud_sensor.h"
#include <random>
#include <cmath>

namespace astro_mount {
namespace weather {

// ─── SimulatedCloudSensor ──────────────────────────────────────────────────

SimulatedCloudSensor::SimulatedCloudSensor(double cover)
    : simulated_cover_(cover), simulated_sky_temp_(-10.0) {
}

bool SimulatedCloudSensor::initialize() {
    initialized_ = true;
    return true;
}

double SimulatedCloudSensor::readCloudCover() {
    static std::default_random_engine gen(std::random_device{}());
    std::normal_distribution<double> noise(0.0, 5.0);
    return std::clamp(simulated_cover_ + noise(gen), 0.0, 100.0);
}

double SimulatedCloudSensor::readSkyBrightness() {
    // Brighter sky = more cloud cover or moonlight
    double base_brightness = 21.5; // Dark sky MPSAS
    double cloud_factor = simulated_cover_ / 100.0 * 3.0;
    return base_brightness - cloud_factor;
}

double SimulatedCloudSensor::readAmbientLight() {
    // Simple ambient light reading
    return simulated_cover_ > 80.0 ? 50.0 : 0.1;
}

double SimulatedCloudSensor::readSkyTemperature() {
    static std::default_random_engine gen(std::random_device{}());
    std::normal_distribution<double> noise(0.0, 0.5);

    // Clear sky = cold IR temp, cloudy = warmer IR temp
    double sky_temp = -15.0 + (simulated_cover_ / 100.0) * 25.0;
    return sky_temp + noise(gen);
}

double SimulatedCloudSensor::readAmbientTemperature() {
    static std::default_random_engine gen(std::random_device{}());
    std::normal_distribution<double> noise(0.0, 0.3);
    return ambient_temp_ + noise(gen);
}

bool SimulatedCloudSensor::isOperational() const {
    return initialized_;
}

void SimulatedCloudSensor::shutdown() {
    initialized_ = false;
}

void SimulatedCloudSensor::setSimulatedConditions(double cover_percent, double sky_temp_c) {
    simulated_cover_ = cover_percent;
    simulated_sky_temp_ = sky_temp_c;
}

} // namespace weather
} // namespace astro_mount
