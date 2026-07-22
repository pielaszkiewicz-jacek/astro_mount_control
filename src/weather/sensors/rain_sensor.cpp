#include "weather/sensors/rain_sensor.h"
#include <random>

namespace astro_mount {
namespace weather {

// ─── SimulatedRainSensor ───────────────────────────────────────────────────

SimulatedRainSensor::SimulatedRainSensor(bool simulate_rain, double rate)
    : simulated_rain_(simulate_rain), simulated_rate_(rate) {
}

bool SimulatedRainSensor::initialize() {
    initialized_ = true;
    return true;
}

bool SimulatedRainSensor::readRainDetected() {
    return simulated_rain_;
}

double SimulatedRainSensor::readRainRate() {
    if (!simulated_rain_) return 0.0;
    // Add some noise to the rate
    static std::default_random_engine gen(std::random_device{}());
    std::normal_distribution<double> noise(0.0, 0.1);
    return std::max(0.0, simulated_rate_ + noise(gen));
}

double SimulatedRainSensor::readTotalRainfall() {
    if (simulated_rain_) {
        total_rainfall_ += readRainRate() / 3600.0; // incremental accumulation
    }
    return total_rainfall_;
}

bool SimulatedRainSensor::isOperational() const {
    return initialized_;
}

void SimulatedRainSensor::shutdown() {
    initialized_ = false;
}

void SimulatedRainSensor::setSimulatedRain(bool detected, double rate_mmh) {
    simulated_rain_ = detected;
    simulated_rate_ = rate_mmh;
}

// ─── GpioRainSensor ────────────────────────────────────────────────────────

GpioRainSensor::GpioRainSensor(int gpio_pin, bool invert)
    : gpio_pin_(gpio_pin), invert_(invert) {
}

bool GpioRainSensor::initialize() {
    // TODO: Implement GPIO initialization via sysfs or libgpiod
    // For Linux: /sys/class/gpio/gpio{pin}/direction = "in"
    fd_ = 0; // Placeholder
    return true;
}

bool GpioRainSensor::readRainDetected() {
    // TODO: Read GPIO pin value
    return false;
}

double GpioRainSensor::readRainRate() {
    // GPIO binary sensor can't measure rate directly
    return readRainDetected() ? 1.0 : 0.0;
}

double GpioRainSensor::readTotalRainfall() {
    // Would need tipping bucket counter for accumulation
    return 0.0;
}

bool GpioRainSensor::isOperational() const {
    return fd_ >= 0;
}

void GpioRainSensor::shutdown() {
    // TODO: Clean up GPIO
    fd_ = -1;
}

} // namespace weather
} // namespace astro_mount
