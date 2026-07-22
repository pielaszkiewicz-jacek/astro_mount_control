#include "controllers/power_manager.h"
#include <chrono>
#include <thread>

namespace astro_mount { namespace controllers {

PowerManager::PowerManager(std::unique_ptr<hal::PowerControl> hal) : hal_(std::move(hal)) {}
PowerManager::~PowerManager() { stop(); }

void PowerManager::setAutoParkCallback(std::function<void()> cb) { auto_park_cb_ = std::move(cb); }
void PowerManager::setLowVoltageThreshold(double threshold) { low_voltage_threshold_ = threshold; }

void PowerManager::start(int poll_interval_s) {
    hal_->initialize();
    running_ = true;
    monitor_thread_ = std::make_unique<std::thread>([this, poll_interval_s]() {
        while (running_) {
            auto data = hal_->read();
            if (data.on_battery && data.voltage_v < low_voltage_threshold_ && auto_park_cb_) {
                auto_park_cb_();
            }
            std::this_thread::sleep_for(std::chrono::seconds(poll_interval_s));
        }
    });
}

void PowerManager::stop() { running_ = false; if (monitor_thread_ && monitor_thread_->joinable()) monitor_thread_->join(); }
hal::PowerData PowerManager::getStatus() const { return hal_->read(); }

}} // namespace
