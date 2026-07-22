#include "hal/st4_control.h"
#include <thread>
#include <chrono>
#include <fstream>
#include <sstream>

namespace astro_mount { namespace hal {

bool GpioSysfsSt4::initialize() {
    // Export GPIO pins
    auto exportPin = [](int pin) {
        std::ofstream exp("/sys/class/gpio/export");
        exp << pin;
        std::ofstream dir("/sys/class/gpio/gpio" + std::to_string(pin) + "/direction");
        dir << "out";
    };
    exportPin(ra_plus_); exportPin(ra_minus_);
    exportPin(dec_plus_); exportPin(dec_minus_);
    initialized_ = true;
    return true;
}

bool GpioSysfsSt4::pulse(St4Direction dir, int duration_ms) {
    if (!initialized_) return false;
    int pin = ra_plus_;
    switch (dir) {
        case St4Direction::NORTH: pin = dec_plus_; break;
        case St4Direction::SOUTH: pin = dec_minus_; break;
        case St4Direction::EAST:  pin = ra_plus_; break;
        case St4Direction::WEST:  pin = ra_minus_; break;
    }
    setPin(pin, true);
    std::this_thread::sleep_for(std::chrono::milliseconds(duration_ms));
    setPin(pin, false);
    return true;
}

bool GpioSysfsSt4::stop() {
    setPin(ra_plus_, false); setPin(ra_minus_, false);
    setPin(dec_plus_, false); setPin(dec_minus_, false);
    return true;
}

void GpioSysfsSt4::shutdown() {
    stop();
    // Unexport pins
    auto unexportPin = [](int pin) {
        std::ofstream unexp("/sys/class/gpio/unexport");
        unexp << pin;
    };
    unexportPin(ra_plus_); unexportPin(ra_minus_);
    unexportPin(dec_plus_); unexportPin(dec_minus_);
    initialized_ = false;
}

void GpioSysfsSt4::setPin(int pin, bool high) {
    std::ofstream val("/sys/class/gpio/gpio" + std::to_string(pin) + "/value");
    val << (high ? "1" : "0");
}

}} // namespace
