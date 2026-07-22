#include "weather/sensors/wind_sensor.h"
#include <cmath>
#include <random>

namespace astro_mount {
namespace weather {

// ─── SimulatedWindSensor ───────────────────────────────────────────────────

SimulatedWindSensor::SimulatedWindSensor(double base_speed)
    : base_speed_(base_speed) {
}

bool SimulatedWindSensor::initialize() {
    initialized_ = true;
    return true;
}

double SimulatedWindSensor::readWindSpeed() {
    static std::default_random_engine gen(std::random_device{}());
    std::normal_distribution<double> noise(0.0, 0.5);
    current_speed_ = std::max(0.0, base_speed_ + noise(gen));
    return current_speed_;
}

double SimulatedWindSensor::readWindGust() {
    static std::default_random_engine gen(std::random_device{}());
    std::normal_distribution<double> noise(1.0, 1.0);
    current_gust_ = std::max(current_speed_, current_speed_ * 1.5 + noise(gen));
    return current_gust_;
}

double SimulatedWindSensor::readWindDirection() {
    static std::default_random_engine gen(std::random_device{}());
    std::uniform_real_distribution<double> dir(0.0, 360.0);
    current_direction_ = dir(gen);
    return current_direction_;
}

bool SimulatedWindSensor::isOperational() const {
    return initialized_;
}

void SimulatedWindSensor::shutdown() {
    initialized_ = false;
}

void SimulatedWindSensor::setSimulatedWind(double speed, double gust, double direction) {
    current_speed_ = speed;
    current_gust_ = gust;
    current_direction_ = direction;
}

// ─── GpioWindSensor ────────────────────────────────────────────────────────

GpioWindSensor::GpioWindSensor(int gpio_pin, double pulses_per_ms)
    : gpio_pin_(gpio_pin), pulses_per_ms_(pulses_per_ms) {
}

bool GpioWindSensor::initialize() {
    // TODO: Implement GPIO interrupt for pulse counting
    // Anemometer generates pulses proportional to wind speed
    fd_ = 0; // Placeholder
    return true;
}

double GpioWindSensor::readWindSpeed() {
    // TODO: Count pulses over a time window and convert to m/s
    return 0.0;
}

double GpioWindSensor::readWindGust() {
    // TODO: Track peak wind speed over last 10 minutes
    return 0.0;
}

double GpioWindSensor::readWindDirection() {
    // GPIO-only anemometer can't determine direction
    return 0.0;
}

bool GpioWindSensor::isOperational() const {
    return fd_ >= 0;
}

void GpioWindSensor::shutdown() {
    fd_ = -1;
}

} // namespace weather
} // namespace astro_mount
