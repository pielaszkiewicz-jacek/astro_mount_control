#include "weather/sensors/rain_sensor.h"
#include <random>
#include <fstream>
#include <thread>
#include <chrono>
#include <string>

#ifdef __linux__
#include <unistd.h>
#include <fcntl.h>
#endif

namespace astro_mount {
namespace weather {

#ifdef __linux__
namespace {
bool gpioExport(int pin) {
    std::ofstream out("/sys/class/gpio/export");
    if (!out.is_open()) return false;
    out << pin;
    return true;
}
bool gpioUnexport(int pin) {
    std::ofstream out("/sys/class/gpio/unexport");
    if (!out.is_open()) return false;
    out << pin;
    return true;
}
bool gpioSetDirection(int pin, const char* dir) {
    std::ofstream out("/sys/class/gpio/gpio" + std::to_string(pin) + "/direction");
    if (!out.is_open()) return false;
    out << dir;
    return true;
}
int gpioOpenValue(int pin) {
    return ::open(("/sys/class/gpio/gpio" + std::to_string(pin) + "/value").c_str(),
                  O_RDONLY);
}
bool gpioReadValue(int fd, int& value) {
    if (fd < 0) return false;
    char buf[8] = {0};
    if (::pread(fd, buf, sizeof(buf) - 1, 0) <= 0) return false;
    value = (buf[0] == '1') ? 1 : 0;
    return true;
}
} // anonymous namespace
#endif // __linux__

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
// P8: real sysfs GPIO binary wet/dry sensor. When the pin is not available on
// the system, initialize() fails and isOperational() returns false — the
// weather monitor then honestly reports "no sensor" instead of the previous
// fake `false` reading.

GpioRainSensor::GpioRainSensor(int gpio_pin, bool invert)
    : gpio_pin_(gpio_pin), invert_(invert) {
}

bool GpioRainSensor::initialize() {
#ifdef __linux__
    if (fd_ >= 0) return true;
    if (!gpioExport(gpio_pin_)) return false;
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    if (!gpioSetDirection(gpio_pin_, "in")) { gpioUnexport(gpio_pin_); return false; }
    fd_ = gpioOpenValue(gpio_pin_);
    if (fd_ < 0) { gpioUnexport(gpio_pin_); return false; }
    return true;
#else
    return false;  // no GPIO support on this platform → sensor not present
#endif
}

bool GpioRainSensor::readRainDetected() {
    if (fd_ < 0) return false;
    int value = 0;
    if (!gpioReadValue(fd_, value)) return false;
    bool wet = (value == 1);
    return invert_ ? !wet : wet;
}

double GpioRainSensor::readRainRate() {
    // GPIO binary sensor can't measure rate directly.
    return readRainDetected() ? 1.0 : 0.0;
}

double GpioRainSensor::readTotalRainfall() {
    // Crude accumulation: count the time the sensor stays wet.
    if (readRainDetected()) {
        total_rainfall_ += 1.0 / 3600.0;  // 1 mm/h equivalent per second
    }
    return total_rainfall_;
}

bool GpioRainSensor::isOperational() const {
    return fd_ >= 0;
}

void GpioRainSensor::shutdown() {
#ifdef __linux__
    if (fd_ >= 0) { ::close(fd_); fd_ = -1; }
    gpioUnexport(gpio_pin_);
#else
    fd_ = -1;
#endif
}

} // namespace weather
} // namespace astro_mount
