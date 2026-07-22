#include "hal/hal_factory.h"
#include "hal/simulated_hal/simulated_hal.h"
#include "canopen_hal/canopen_hal.h"
#include "hal/mf7025v2_hal/mf7025v2_hal.h"
#include "serial_hal/serial_hal.h"
#include "ethernet_hal/ethernet_hal.h"
#include "gamepad_hal/gamepad_hal.h"
#include <fstream>
#include <stdexcept>
#include <iostream>

using namespace astro_mount::hal;

namespace astro_mount {
namespace hal {

std::unique_ptr<HALInterface> HALFactory::create(const HALConfig& config) {
    switch (config.type) {
        case HALType::SIMULATED:
            return createSimulatedHAL(config);
        case HALType::CANOPEN:
            return createCanOpenHAL(config);
        case HALType::MF7025V2:
            return createMf7025v2HAL(config);
        case HALType::SERIAL:
            return createSerialHAL(config);
        case HALType::ETHERNET:
            return createEthernetHAL(config);
        case HALType::GAMEPAD:
            return createGamepadHAL(config);
        case HALType::CUSTOM:
            throw std::runtime_error("Custom HAL implementation not yet supported");
        default:
            throw std::runtime_error("Unknown HAL type");
    }
}

std::unique_ptr<HALInterface> HALFactory::create(HALType type) {
    HALConfig config = getDefaultConfig(type);
    return create(config);
}

std::unique_ptr<HALInterface> HALFactory::create(const std::string& type_name) {
    HALType type = HALConfig::typeFromString(type_name);
    return create(type);
}

std::vector<HALType> HALFactory::getAvailableTypes() {
    std::vector<HALType> types;
    
    // Simulated HAL is always available
    types.push_back(HALType::SIMULATED);
    
    // Check for CANopen support
#ifdef HAVE_CANOPENSOCKET
    if (checkCANOpenSupport()) {
        types.push_back(HALType::CANOPEN);
    }
#endif
    
    // Check for serial support
    if (checkSerialSupport()) {
        types.push_back(HALType::SERIAL);
    }
    
    // Check for Ethernet support
    if (checkEthernetSupport()) {
        types.push_back(HALType::ETHERNET);
    }
    
    // Gamepad HAL is always available
    types.push_back(HALType::GAMEPAD);
    
    return types;
}

std::vector<std::string> HALFactory::getAvailableTypeNames() {
    std::vector<std::string> names;
    std::vector<HALType> types = getAvailableTypes();
    
    for (const auto& type : types) {
        switch (type) {
            case HALType::SIMULATED: names.push_back("simulated"); break;
            case HALType::CANOPEN: names.push_back("canopen"); break;
            case HALType::MF7025V2: names.push_back("mf7025v2"); break;
            case HALType::SERIAL: names.push_back("serial"); break;
            case HALType::ETHERNET: names.push_back("ethernet"); break;
            case HALType::GAMEPAD: names.push_back("gamepad"); break;
            case HALType::CUSTOM: names.push_back("custom"); break;
            default: break;
        }
    }
    
    return names;
}

bool HALFactory::isTypeAvailable(HALType type) {
    auto available_types = getAvailableTypes();
    return std::find(available_types.begin(), available_types.end(), type) != available_types.end();
}

HALType HALFactory::getDefaultType() {
    // Try to use CANopen if available, otherwise simulated
    if (isTypeAvailable(HALType::CANOPEN)) {
        return HALType::CANOPEN;
    }
    return HALType::SIMULATED;
}

HALConfig HALFactory::getDefaultConfig(HALType type) {
    HALConfig config;
    config.type = type;
    config.name = "Default_" + config.getTypeString() + "_HAL";
    
    // Set up default axes
    for (int i = 0; i < 2; ++i) {
        HALConfig::AxisConfig axis;
        axis.id = i;
        axis.name = (i == 0) ? "RA_Axis" : "Dec_Axis";
        
        // Motor config
        axis.motor_config.type = (type == HALType::CANOPEN) ? 
            MotorType::CANOPEN_SERVO : MotorType::VIRTUAL;
        axis.motor_config.max_velocity = 2.0;
        axis.motor_config.max_acceleration = 0.5;
        axis.motor_config.encoder_counts_per_degree = 10000.0;
        
        // Encoder config
        axis.encoder_config.type = (type == HALType::CANOPEN) ? 
            EncoderType::ABSOLUTE : EncoderType::VIRTUAL;
        axis.encoder_config.counts_per_degree = 10000.0;
        
        // Safety limits
        axis.safety_limits.min_position = (i == 0) ? -270.0 : -90.0;
        axis.safety_limits.max_position = (i == 0) ? 270.0 : 90.0;
        axis.safety_limits.max_velocity = 5.0;
        axis.safety_limits.max_acceleration = 2.0;
        
        config.axes.push_back(axis);
    }
    
    // Type-specific configuration
    switch (type) {
        case HALType::MF7025V2:
            config.mf7025v2.can_interface = "can0";
            config.mf7025v2.bitrate = 1000000;
            config.mf7025v2.sdo_timeout_ms = 100;
            config.mf7025v2.position_units_per_degree = 100.0;
            config.mf7025v2.velocity_units_per_dps = 100.0;
            // Set motor type for MF7025v2
            for (auto& axis : config.axes) {
                axis.motor_config.type = MotorType::BRUSHLESS_DC;
                axis.encoder_config.type = EncoderType::ABSOLUTE;
            }
            break;
            
        case HALType::CANOPEN:
            config.canopen.interface_name = "can0";
            config.canopen.bitrate = 125000;
            config.canopen.node_id = 1;
            config.canopen.use_sync = true;
            config.canopen.sync_period_ms = 100;
            break;
            
        case HALType::SERIAL:
            config.serial.port = "/dev/ttyUSB0";
            config.serial.baud_rate = 115200;
            config.serial.protocol = "modbus";
            break;
            
        case HALType::ETHERNET:
            config.ethernet.ip_address = "192.168.1.100";
            config.ethernet.port = 502;
            config.ethernet.protocol = "modbus_tcp";
            break;
            
        case HALType::SIMULATED:
            config.simulated.simulation_update_rate = 100.0;
            config.simulated.position_noise_stddev = 0.001;
            config.simulated.velocity_noise_stddev = 0.0001;
            break;
            
        case HALType::GAMEPAD:
            config.gamepad.device_path = "";
            config.gamepad.deadzone = 0.15;
            config.gamepad.sensitivity = 1.0;
            config.gamepad.max_velocity_deg_s = 5.0;
            config.gamepad.invert_axis1 = false;
            config.gamepad.invert_axis2 = false;
            config.gamepad.speed_presets = {0.5, 1.0, 2.0, 3.0, 5.0};
            config.gamepad.update_rate_hz = 50.0;
            config.gamepad.gamepad_mode = 0;  // RAW
            break;
            
        default:
            break;
    }
    
    return config;

}

HALConfig HALFactory::loadConfigFromFile(const std::string& filename) {
    try {
        std::ifstream file(filename);
        if (!file.is_open()) {
            throw std::runtime_error("Cannot open config file: " + filename);
        }
        
        nlohmann::json json;
        file >> json;
        file.close();
        
        return HALConfig::fromJson(json);
    } catch (const std::exception& e) {
        throw std::runtime_error("Failed to load config from " + filename + ": " + e.what());
    }
}

bool HALFactory::saveConfigToFile(const HALConfig& config, const std::string& filename) {
    try {
        nlohmann::json json = config.toJson();
        
        std::ofstream file(filename);
        if (!file.is_open()) {
            return false;
        }
        
        file << json.dump(4); // Pretty print with 4 spaces indentation
        file.close();
        
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Failed to save config to " << filename << ": " << e.what() << std::endl;
        return false;
    }
}

// Helper methods for checking support
bool HALFactory::checkCANOpenSupport() {
    // In a real implementation, this would check for CANopen libraries
    // For now, we'll check compile-time defines
#ifdef HAVE_CANOPENSOCKET
    return true;
#else
    return false;
#endif
}

bool HALFactory::checkSerialSupport() {
    // In a real implementation, this would check for serial ports
    // For now, we'll assume it's available on most systems
    return true;
}

bool HALFactory::checkEthernetSupport() {
    // Ethernet support is assumed to be available
    return true;
}

// Implementation creation methods
std::unique_ptr<HALInterface> HALFactory::createSimulatedHAL(const HALConfig& config) {
    return std::make_unique<SimulatedHAL>(config);
}

std::unique_ptr<HALInterface> HALFactory::createCanOpenHAL(const HALConfig& config) {
    auto hal = CanOpenHAL::create(config);
    if (!hal) {
        throw std::runtime_error("Failed to create CanOpenHAL");
    }
    return hal;
}

std::unique_ptr<HALInterface> HALFactory::createSerialHAL(const HALConfig& config) {
    try {
        return std::make_unique<SerialHAL>(config);
    } catch (const std::exception& e) {
        std::cerr << "Failed to create SerialHAL: " << e.what() << std::endl;
        throw;
    }
}

std::unique_ptr<HALInterface> HALFactory::createEthernetHAL(const HALConfig& config) {
    try {
        return std::make_unique<EthernetHAL>(config);
    } catch (const std::exception& e) {
        std::cerr << "Failed to create EthernetHAL: " << e.what() << std::endl;
        throw;
    }
}

std::unique_ptr<HALInterface> HALFactory::createGamepadHAL(const HALConfig& config) {
    try {
        return std::make_unique<GamepadHAL>(config);
    } catch (const std::exception& e) {
        std::cerr << "Failed to create GamepadHAL: " << e.what() << std::endl;
        throw;
    }
}

std::unique_ptr<HALInterface> HALFactory::createMf7025v2HAL(const HALConfig& config) {
    try {
        auto hal = Mf7025v2Hal::create(config);
        if (!hal) {
            throw std::runtime_error("Failed to create Mf7025v2Hal");
        }
        return hal;
    } catch (const std::exception& e) {
        std::cerr << "Failed to create Mf7025v2Hal: " << e.what() << std::endl;
        throw;
    }
}

} // namespace hal
} // namespace astro_mount