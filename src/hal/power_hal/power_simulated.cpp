#include "hal/power_control.h"
#include <random>

namespace astro_mount { namespace hal {

PowerData SimulatedPower::read() {
    static std::default_random_engine gen(std::random_device{}());
    PowerData d;
    d.voltage_v = 12.5 + std::normal_distribution<double>(0, 0.05)(gen);
    d.current_a = 3.0 + std::normal_distribution<double>(0, 0.1)(gen);
    d.power_w = d.voltage_v * d.current_a;
    d.capacity_ah = 100.0;
    d.charge_percent = 85.0;
    d.charging = false;
    d.on_battery = true;
    d.temperature_c = 25.0;
    d.estimated_runtime_min = d.capacity_ah / d.current_a * 60.0 * (d.charge_percent / 100.0);
    d.input_voltage_v = d.voltage_v;
    d.output_voltage_v = d.voltage_v;
    d.outputs = outputs_;
    d.timestamp = std::chrono::system_clock::now();
    return d;
}

bool SimulatedPower::setOutputEnabled(int id, bool enabled) {
    for (auto& o : outputs_) {
        if (o.id == id) { o.enabled = enabled; return true; }
    }
    return false;
}

bool SimulatedPower::isOutputEnabled(int id) const {
    for (const auto& o : outputs_) {
        if (o.id == id) return o.enabled;
    }
    return false;
}

}} // namespace
