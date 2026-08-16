#ifndef POWER_MANAGER_H
#define POWER_MANAGER_H
#include <memory>
#include <functional>
#include <atomic>
#include <thread>
#include "hal/power_control.h"

namespace astro_mount { namespace controllers {

class PowerManager {
public:
    explicit PowerManager(std::unique_ptr<hal::PowerControl> hal);
    ~PowerManager();

    void setAutoParkCallback(std::function<void()> cb);
    void setLowVoltageThreshold(double threshold);
    void start(int poll_interval_s = 10);
    void stop();
    hal::PowerData getStatus() const;

    /// @brief Enable/disable a power output channel. Delegates to the HAL.
    bool setOutputEnabled(int id, bool enabled);
    bool isOutputEnabled(int id) const;
    int outputCount() const;

private:
    void monitorLoop();
    std::unique_ptr<hal::PowerControl> hal_;
    std::function<void()> auto_park_cb_;
    double low_voltage_threshold_{11.5};
    std::atomic<bool> running_{false};
    std::unique_ptr<std::thread> monitor_thread_;
};

}} // namespace astro_mount::controllers
#endif
