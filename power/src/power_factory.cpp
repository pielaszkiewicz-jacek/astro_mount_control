#include "controllers/power_manager.h"
#include "hal/power_control.h"
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>

namespace astro_power {

using astro_mount::controllers::PowerManager;
using astro_mount::hal::PowerControl;
using astro_mount::hal::SimulatedPower;

std::unique_ptr<PowerManager>
createPowerManagerFromConfig(const std::string& config_path) {
    std::ifstream file(config_path);
    if (!file.is_open()) {
        std::cerr << "[PowerFactory] Cannot open config, using simulated\n";
        auto hal = std::make_unique<SimulatedPower>();
        hal->initialize();
        return std::make_unique<PowerManager>(std::move(hal));
    }

    nlohmann::json config;
    file >> config;

    std::string hal_type = config.value("hal_type", "simulated");
    std::unique_ptr<PowerControl> hal;

    if (hal_type == "simulated" || hal_type.empty()) {
        hal = std::make_unique<SimulatedPower>();
    } else {
        std::cerr << "[PowerFactory] Unknown HAL type '" << hal_type << "', using simulated\n";
        hal = std::make_unique<SimulatedPower>();
    }

    if (!hal->initialize()) {
        std::cerr << "[PowerFactory] Failed to initialize power HAL\n";
        return nullptr;
    }

    auto manager = std::make_unique<PowerManager>(std::move(hal));

    if (config.contains("low_voltage_threshold")) {
        manager->setLowVoltageThreshold(config["low_voltage_threshold"].get<double>());
    }

    return manager;
}

} // namespace astro_power
