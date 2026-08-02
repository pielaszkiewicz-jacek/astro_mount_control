/**
 * Dome HAL Factory
 *
 * This file includes the three dome HAL implementation .cpp files to give
 * the factory function visibility of their class definitions (constructors,
 * virtual methods, etc.).  This avoids the need to split inline constructor
 * bodies out into separate headers.
 *
 * All three classes are in namespace astro_mount::hal and are linked into
 * the astro_mount_core library alongside this file, so there are no ODR
 * violations — each class is defined in exactly one translation unit
 * (its own .cpp file) and only the factory function lives here.
 */

#include "hal/dome_control.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <stdexcept>
#include <iostream>

// Include the actual class definitions so constructors are visible
#include "../src/hal/dome_hal/dome_simulated.cpp"
#include "../src/hal/dome_hal/dome_serial.cpp"
#include "../src/hal/dome_hal/dome_rolloff.cpp"

namespace astro_mount {
namespace hal {

std::unique_ptr<DomeControl> createDomeControl(const std::string& config_path) {
    // Read and parse JSON config
    nlohmann::json cfg;
    {
        std::ifstream file(config_path);
        if (!file.is_open()) {
            throw std::runtime_error("Cannot open dome config file: " + config_path);
        }
        file >> cfg;
    }

    std::string type = cfg.value("type", "simulated");

    std::unique_ptr<DomeControl> control;

    if (type == "simulated") {
        DomeType dt = (cfg.value("dome_type", "rotating") == "rolloff")
                          ? DomeType::ROLLOFF
                          : DomeType::ROTATING;
        control = std::make_unique<SimulatedDome>(dt);
    } else if (type == "serial") {
        std::string port = cfg.value("device_path", "/dev/ttyUSB0");
        int baud = cfg.value("baud_rate", 9600);
        control = std::make_unique<SerialDome>(port, baud);
    } else if (type == "rolloff") {
        std::string gpio_open = cfg.value("gpio_open_pin", "gpio17");
        std::string gpio_close = cfg.value("gpio_close_pin", "gpio18");
        int open_time = cfg.value("open_time_s", 30);
        control = std::make_unique<RollOffRoof>(gpio_open, gpio_close, open_time);
    } else {
        throw std::runtime_error("Unknown dome type: " + type);
    }

    // Connect
    if (!control->connect()) {
        throw std::runtime_error("Failed to connect dome: " + type);
    }

    std::cout << "[DomeFactory] Created and connected dome type: " << type << std::endl;
    return control;
}

} // namespace hal
} // namespace astro_mount
