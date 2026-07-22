#ifndef POWER_CONTROL_H
#define POWER_CONTROL_H
#include <string>
#include <vector>
#include <cstdint>
#include <chrono>

namespace astro_mount { namespace hal {

struct PowerOutput {
    int id{0};
    std::string name;
    bool enabled{false};
    double voltage_v{0.0};
    double current_a{0.0};
    bool overload{false};
};

struct PowerData {
    double voltage_v{0.0};
    double current_a{0.0};
    double power_w{0.0};
    double capacity_ah{0.0};
    double charge_percent{0.0};
    bool charging{false};
    bool on_battery{false};
    double temperature_c{20.0};
    double estimated_runtime_min{0.0};
    double input_voltage_v{0.0};
    double output_voltage_v{0.0};
    std::vector<PowerOutput> outputs;
    std::chrono::system_clock::time_point timestamp;
};

class PowerControl {
public:
    virtual ~PowerControl() = default;
    virtual std::string name() const = 0;
    virtual bool initialize() = 0;
    virtual PowerData read() = 0;
    virtual bool setOutputEnabled(int id, bool enabled) = 0;
    virtual bool isOutputEnabled(int id) const = 0;
    virtual int outputCount() const = 0;
    virtual void shutdown() = 0;
};

class SimulatedPower : public PowerControl {
public:
    std::string name() const override { return "simulated_power"; }
    bool initialize() override { return true; }
    PowerData read() override;
    bool setOutputEnabled(int id, bool enabled) override;
    bool isOutputEnabled(int id) const override;
    int outputCount() const override { return 4; }
    void shutdown() override {}
private:
    std::vector<PowerOutput> outputs_{{0,"Main",true},{1,"Mount",true},{2,"Camera",true},{3,"Focuser",false}};
};

}} // namespace astro_mount::hal
#endif
