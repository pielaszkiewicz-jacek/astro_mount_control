#ifndef ST4_CONTROL_H
#define ST4_CONTROL_H
#include <string>
#include <chrono>
#include <functional>

namespace astro_mount { namespace hal {

enum class St4Direction { NORTH, SOUTH, EAST, WEST };

class St4Control {
public:
    virtual ~St4Control() = default;
    virtual std::string name() const = 0;
    virtual bool initialize() = 0;
    virtual bool pulse(St4Direction dir, int duration_ms) = 0;
    virtual bool stop() = 0;
    virtual bool isConnected() const = 0;
    virtual void shutdown() = 0;
};

class SimulatedSt4 : public St4Control {
public:
    std::string name() const override { return "simulated_st4"; }
    bool initialize() override { return true; }
    bool pulse(St4Direction dir, int duration_ms) override { return true; }
    bool stop() override { return true; }
    bool isConnected() const override { return true; }
    void shutdown() override {}
};

class GpioSysfsSt4 : public St4Control {
public:
    GpioSysfsSt4(int ra_plus, int ra_minus, int dec_plus, int dec_minus)
        : ra_plus_(ra_plus), ra_minus_(ra_minus), dec_plus_(dec_plus), dec_minus_(dec_minus) {}
    std::string name() const override { return "gpio_sysfs"; }
    bool initialize() override;
    bool pulse(St4Direction dir, int duration_ms) override;
    bool stop() override;
    bool isConnected() const override { return initialized_; }
    void shutdown() override;
private:
    int ra_plus_, ra_minus_, dec_plus_, dec_minus_;
    bool initialized_{false};
    void setPin(int pin, bool high);
};

}} // namespace astro_mount::hal
#endif
