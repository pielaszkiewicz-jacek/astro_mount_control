/**
 * Derotator HAL Factory
 *
 * Includes the derotator HAL implementation .cpp files to give the factory
 * function visibility of the concrete class definitions.
 */

#include "hal/derotator_control.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <stdexcept>
#include <iostream>

// Include the actual class definition so the constructor is visible
#include "../src/hal/derotator_hal/derotator_simulated.cpp"

namespace astro_mount {
namespace hal {

std::unique_ptr<DerotatorControl> createDerotatorControl(const std::string& config_path) {
    nlohmann::json cfg;
    {
        std::ifstream file(config_path);
        if (!file.is_open()) {
            throw std::runtime_error("Cannot open derotator config: " + config_path);
        }
        file >> cfg;
    }

    std::string type = cfg.value("type", "SIMULATED");

    std::unique_ptr<DerotatorControl> control;

    if (type == "SIMULATED" || type == "simulated") {
        control = std::make_unique<SimulatedDerotator>();
    } else {
        throw std::runtime_error("Unknown derotator type: " + type);
    }

    if (!control->connect()) {
        throw std::runtime_error("Failed to connect derotator: " + type);
    }
    if (!control->initialize()) {
        throw std::runtime_error("Failed to initialise derotator: " + type);
    }

    std::cout << "[DerotatorFactory] Created and connected derotator type: " << type << std::endl;
    return control;
}

} // namespace hal
} // namespace astro_mount
