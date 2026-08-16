#include "weather/sensors/wind_sensor.h"
#include <cmath>
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
// P8: real sysfs GPIO pulse-counting anemometer. When the pin is not
// available on the system, initialize() fails and isOperational() returns
// false — the weather monitor then honestly reports "no sensor" instead of
// the previous fake 0.0 reading.

GpioWindSensor::GpioWindSensor(int gpio_pin, double pulses_per_ms)
    : gpio_pin_(gpio_pin), pulses_per_ms_(pulses_per_ms) {
}

bool GpioWindSensor::initialize() {
#ifdef __linux__
    if (fd_ >= 0) return true;
    if (!gpioExport(gpio_pin_)) return false;
    // Let udev create the sysfs entries.
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    if (!gpioSetDirection(gpio_pin_, "in")) { gpioUnexport(gpio_pin_); return false; }
    fd_ = gpioOpenValue(gpio_pin_);
    if (fd_ < 0) { gpioUnexport(gpio_pin_); return false; }
    return true;
#else
    return false;  // no GPIO support on this platform → sensor not present
#endif
}

double GpioWindSensor::readWindSpeed() {
    if (fd_ < 0) return 0.0;
    // Count rising edges over a short window and convert to m/s using the
    // anemometer calibration (pulses per ms per m/s).
    const int WINDOW_MS = 100;
    int last = 0;
    if (!gpioReadValue(fd_, last)) return 0.0;
    int edges = 0;
    auto start = std::chrono::steady_clock::now();
    while (std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now() - start).count() < WINDOW_MS) {
        int v = 0;
        if (gpioReadValue(fd_, v)) {
            if (v == 1 && last == 0) edges++;
            last = v;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    if (pulses_per_ms_ <= 0.0) return 0.0;
    current_speed_ = std::max(0.0,
        (static_cast<double>(edges) / WINDOW_MS) / pulses_per_ms_);
    return current_speed_;
}

double GpioWindSensor::readWindGust() {
    // Basic gust: read a fresh speed and keep the larger of current/last.
    double s = readWindSpeed();
    return std::max(current_speed_, s);
}

double GpioWindSensor::readWindDirection() {
    // GPIO-only anemometer can't determine direction.
    return 0.0;
}

bool GpioWindSensor::isOperational() const {
    return fd_ >= 0;
}

void GpioWindSensor::shutdown() {
#ifdef __linux__
    if (fd_ >= 0) { ::close(fd_); fd_ = -1; }
    gpioUnexport(gpio_pin_);
#else
    fd_ = -1;
#endif
}

} // namespace weather
} // namespace astro_mount
