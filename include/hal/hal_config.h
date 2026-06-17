#pragma once
#include <string>
#include <vector>
#include <map>
#include <nlohmann/json.hpp>
#include "hal/hal_interface.h"
#include "hal/motor_control.h"
#include "hal/encoder_reader.h"
#include "controllers/icanopen_interface.h"

namespace astro_mount {
namespace hal {

enum class HALType {
    SIMULATED,   // Symulowany hardware
    CANOPEN,     // CANopen/CiA 402
    SERIAL,      // Port szeregowy (RS-232/485)
    ETHERNET,    // Ethernet (EtherCAT, Modbus TCP)
    GAMEPAD,     // Ręczne sterowanie (gamepad/joystick)
    CUSTOM       // Własna implementacja
};

// Typ derotatora
enum class DerotatorType {
    CANOPEN = 0,
    STEPPER = 1,
    SERVO = 2,
    CUSTOM = 3
};

// Konfiguracja derotatora (pole obserwacyjne)
struct DerotatorConfig {
    DerotatorType type{DerotatorType::STEPPER};
    bool enabled{false};
    double gear_ratio{180.0};
    double max_speed{5.0};
    double max_acceleration{2.0};
    double backlash{0.0};
    bool absolute_encoder{false};
    double encoder_resolution{36000.0};
    double homing_offset{0.0};
    std::vector<double> calibration_table;
    std::string connection_string;
};

struct HALConfig {
    HALType type{HALType::SIMULATED};
    std::string name{"Default_HAL"};
    
    // Konfiguracja CANopen
    struct {
        controllers::ICanOpenInterface::Config canopen_config;
        std::string library{"mock"};  // "mock", "canopensocket", "libedssharp", "canfestival"
        std::string interface_name{"can0"};
        uint32_t bitrate{125000};
        uint8_t node_id{1};
        bool use_sync{true};
        uint32_t sync_period_ms{100};
        uint32_t sdo_timeout_ms{1000};
        uint32_t pdo_update_rate{100}; // Hz
        std::string accel_mode{"time"}; // "time" or "rate" (CiA 402 acceleration interpretation)
        
        // === Konfiguracja NMT (Network Management) ===
        struct {
            bool enable_nmt{true};                       // Włączenie monitorowania NMT
            uint32_t heartbeat_period_ms{100};           // Oczekiwany okres heartbeat (100ms)
            uint32_t heartbeat_timeout_ms{500};          // Timeout heartbeat (5x period)
            uint32_t max_missed_heartbeats{3};           // Maksymalna liczba pominiętych heartbeat
            bool enable_bootup_check{true};              // Sprawdzanie bootup po resecie
            uint32_t bootup_timeout_ms{5000};            // Timeout na bootup (5s)
            bool enable_auto_recovery{true};             // Automatyczne przywracanie węzłów
            uint32_t recovery_interval_s{5};             // Min. odstęp między recovery (5s)
            bool enable_node_guarding{false};            // Node Guarding (alternatywa dla heartbeat)
            uint32_t node_guarding_period_ms{1000};      // Okres node guarding (1s)
        } nmt;
    } canopen;
    
    // Konfiguracja Serial
    struct {
        std::string port{"/dev/ttyUSB0"};
        uint32_t baud_rate{115200};
        std::string protocol{"modbus"};  // "modbus", "custom", "ascii"
        uint8_t data_bits{8};
        uint8_t stop_bits{1};
        std::string parity{"none"};      // "none", "even", "odd"
        uint32_t timeout_ms{1000};
    } serial;
    
    // Konfiguracja Ethernet
    struct {
        std::string ip_address{"192.168.1.100"};
        uint16_t port{502};
        std::string protocol{"modbus_tcp"}; // "modbus_tcp", "ethercat", "profinet"
        uint32_t timeout_ms{1000};
        uint32_t retry_count{3};
    } ethernet;
    
    // Konfiguracja symulacji
    struct {
        bool enable_simulation{true};
        double simulation_update_rate{100.0};  // Hz
        double position_noise_stddev{0.001};   // deg
        double velocity_noise_stddev{0.0001};  // deg/s
        bool simulate_errors{false};
        double error_probability{0.01};        // 1% szansy na błąd
    } simulated;
    
    // Konfiguracja gamepada
    struct {
        std::string device_path;               // Pusta = automatyczne wykrywanie
        double deadzone{0.15};                 // Strefa martwa joysticka [0..1]
        double sensitivity{1.0};               // Krzywa czułości (1.0 = liniowa)
        double max_velocity_deg_s{5.0};        // Maksymalna prędkość przy pełnym wychyleniu
        bool invert_axis1{false};              // Inwersja osi 1 (LX)
        bool invert_axis2{false};              // Inwersja osi 2 (LY)
        std::vector<double> speed_presets;     // Predefiniowane poziomy prędkości
        double update_rate_hz{50.0};           // Częstotliwość odpytywania
        
        // Mapowanie przycisków: physical_index → nazwa akcji
        // Akcje: "home", "stop", "emergency_stop", "park",
        //        "speed_up", "speed_down", "manual_toggle", "none"
        std::map<int, std::string> button_mapping;
        
        // Mapowanie osi: physical_index → nazwa osi
        // Osie: "lx", "ly", "rx", "ry",
        //       "trigger_l", "trigger_r", "pov_x", "pov_y", "none"
        std::map<int, std::string> axis_mapping;
    } gamepad;
    
    DerotatorConfig derotator;
    
    // Konfiguracja osi
    struct AxisConfig {
        int id{0};
        std::string name{"Axis_0"};
        uint8_t can_node_id{0};  // CANopen node ID (0 = auto: axis_id + 1)
        MotorConfig motor_config;
        EncoderConfig encoder_config;
        
        // Limity bezpieczeństwa
        struct {
            double min_position{-270.0};    // deg
            double max_position{270.0};     // deg
            double max_velocity{5.0};       // deg/s
            double max_acceleration{2.0};   // deg/s²
            double max_current{10.0};       // A
            double max_temperature{80.0};   // °C
        } safety_limits;
    };
    
    std::vector<AxisConfig> axes;
    
    // Parametry PID
    struct PIDParams {
        double kp{1.5};
        double ki{0.2};
        double kd{0.05};
        double integral_limit{1000.0};
        double output_limit{100.0};
        double anti_windup_gain{0.1};
        bool enable_anti_windup{true};
    } pid_params;
    
    // Konfiguracja bezpieczeństwa
    struct {
        bool enable_limits{true};
        bool enable_emergency_stop{true};
        uint32_t emergency_stop_timeout_ms{100};
        bool enable_temperature_monitoring{true};
        bool enable_current_monitoring{true};
        bool enable_voltage_monitoring{true};
        double min_voltage{20.0};      // V
        double max_voltage{30.0};      // V
        uint32_t monitoring_rate{10};  // Hz
    } safety;
    
    // Metody statyczne
    static HALConfig fromJson(const nlohmann::json& json) {
        HALConfig config;
        if (json.is_null() || json.empty()) {
            return getDefault();
        }
        
        // Parse HAL type
        std::string type_str = json.value("type", "simulated");
        config.type = typeFromString(type_str);
        config.name = json.value("name", "Default_HAL");
        
        // Parse simulated configuration
        auto simulated = json.value("simulated", nlohmann::json::object());
        config.simulated.enable_simulation = simulated.value("enable_simulation", true);
        config.simulated.simulation_update_rate = simulated.value("simulation_update_rate", 100.0);
        config.simulated.position_noise_stddev = simulated.value("position_noise_stddev", 0.001);
        config.simulated.velocity_noise_stddev = simulated.value("velocity_noise_stddev", 0.0001);
        config.simulated.simulate_errors = simulated.value("simulate_errors", false);
        config.simulated.error_probability = simulated.value("error_probability", 0.01);
        
        // Parse CANopen configuration
        auto canopen = json.value("canopen", nlohmann::json::object());
        config.canopen.library = canopen.value("library", "mock");
        config.canopen.interface_name = canopen.value("interface_name", "can0");
        config.canopen.bitrate = canopen.value("bitrate", 125000);
        config.canopen.node_id = canopen.value("node_id", 1);
        config.canopen.use_sync = canopen.value("use_sync", true);
        config.canopen.sync_period_ms = canopen.value("sync_period_ms", 100);
        config.canopen.sdo_timeout_ms = canopen.value("sdo_timeout_ms", 1000);
        config.canopen.pdo_update_rate = canopen.value("pdo_update_rate", 100);
        config.canopen.accel_mode = canopen.value("accel_mode", "time");
        
        // Parse NMT configuration
        auto nmt_json = canopen.value("nmt", nlohmann::json::object());
        config.canopen.nmt.enable_nmt = nmt_json.value("enable_nmt", true);
        config.canopen.nmt.heartbeat_period_ms = nmt_json.value("heartbeat_period_ms", 100);
        config.canopen.nmt.heartbeat_timeout_ms = nmt_json.value("heartbeat_timeout_ms", 500);
        config.canopen.nmt.max_missed_heartbeats = nmt_json.value("max_missed_heartbeats", 3);
        config.canopen.nmt.enable_bootup_check = nmt_json.value("enable_bootup_check", true);
        config.canopen.nmt.bootup_timeout_ms = nmt_json.value("bootup_timeout_ms", 5000);
        config.canopen.nmt.enable_auto_recovery = nmt_json.value("enable_auto_recovery", true);
        config.canopen.nmt.recovery_interval_s = nmt_json.value("recovery_interval_s", 5);
        config.canopen.nmt.enable_node_guarding = nmt_json.value("enable_node_guarding", false);
        config.canopen.nmt.node_guarding_period_ms = nmt_json.value("node_guarding_period_ms", 1000);
        
        // Parse serial configuration
        auto serial = json.value("serial", nlohmann::json::object());
        config.serial.port = serial.value("port", "/dev/ttyUSB0");
        config.serial.baud_rate = serial.value("baud_rate", 115200);
        config.serial.protocol = serial.value("protocol", "modbus");
        config.serial.data_bits = serial.value("data_bits", 8);
        config.serial.stop_bits = serial.value("stop_bits", 1);
        config.serial.parity = serial.value("parity", "none");
        config.serial.timeout_ms = serial.value("timeout_ms", 1000);
        
        // Parse ethernet configuration
        auto ethernet = json.value("ethernet", nlohmann::json::object());
        config.ethernet.ip_address = ethernet.value("ip_address", "192.168.1.100");
        config.ethernet.port = ethernet.value("port", 502);
        config.ethernet.protocol = ethernet.value("protocol", "modbus_tcp");
        config.ethernet.timeout_ms = ethernet.value("timeout_ms", 1000);
        config.ethernet.retry_count = ethernet.value("retry_count", 3);
        
        // Parse gamepad configuration
        auto gamepad = json.value("gamepad", nlohmann::json::object());
        config.gamepad.device_path = gamepad.value("device_path", "");
        config.gamepad.deadzone = gamepad.value("deadzone", 0.15);
        config.gamepad.sensitivity = gamepad.value("sensitivity", 1.0);
        config.gamepad.max_velocity_deg_s = gamepad.value("max_velocity_deg_s", 5.0);
        config.gamepad.invert_axis1 = gamepad.value("invert_axis1", false);
        config.gamepad.invert_axis2 = gamepad.value("invert_axis2", false);
        config.gamepad.update_rate_hz = gamepad.value("update_rate_hz", 50.0);
        
        auto speed_presets = gamepad.value("speed_presets", nlohmann::json::array());
        config.gamepad.speed_presets.clear();
        for (const auto& val : speed_presets) {
            if (val.is_number()) {
                config.gamepad.speed_presets.push_back(val.get<double>());
            }
        }
        
        // Parse gamepad button mapping
        auto btn_mapping = gamepad.value("button_mapping", nlohmann::json::object());
        config.gamepad.button_mapping.clear();
        for (auto it = btn_mapping.begin(); it != btn_mapping.end(); ++it) {
            int idx = std::stoi(it.key());
            config.gamepad.button_mapping[idx] = it->get<std::string>();
        }
        
        // Parse gamepad axis mapping
        auto axis_mapping = gamepad.value("axis_mapping", nlohmann::json::object());
        config.gamepad.axis_mapping.clear();
        for (auto it = axis_mapping.begin(); it != axis_mapping.end(); ++it) {
            int idx = std::stoi(it.key());
            config.gamepad.axis_mapping[idx] = it->get<std::string>();
        }
        
        // Parse axes configurations
        config.axes.clear();
        auto axes = json.value("axes", nlohmann::json::array());
        for (const auto& axis_json : axes) {
            AxisConfig axis;
            axis.id = axis_json.value("id", 0);
            axis.name = axis_json.value("name", "Axis_0");
            axis.can_node_id = axis_json.value("can_node_id", 0);
            
            // Parse motor config
            auto motor_json = axis_json.value("motor_config", nlohmann::json::object());
            std::string motor_type_str = motor_json.value("type", "STEPPER");
            if (motor_type_str == "STEPPER") axis.motor_config.type = MotorType::STEPPER;
            else if (motor_type_str == "SERVO") axis.motor_config.type = MotorType::SERVO;
            else if (motor_type_str == "BRUSHED_DC") axis.motor_config.type = MotorType::BRUSHED_DC;
            else if (motor_type_str == "BRUSHLESS_DC") axis.motor_config.type = MotorType::BRUSHLESS_DC;
            else if (motor_type_str == "CANOPEN_SERVO") axis.motor_config.type = MotorType::CANOPEN_SERVO;
            else axis.motor_config.type = MotorType::STEPPER;
            
            std::string control_mode_str = motor_json.value("default_mode", "POSITION");
            if (control_mode_str == "POSITION") axis.motor_config.default_mode = ControlMode::POSITION;
            else if (control_mode_str == "VELOCITY") axis.motor_config.default_mode = ControlMode::VELOCITY;
            else if (control_mode_str == "TORQUE") axis.motor_config.default_mode = ControlMode::TORQUE;
            else if (control_mode_str == "TRAJECTORY") axis.motor_config.default_mode = ControlMode::TRAJECTORY;
            else axis.motor_config.default_mode = ControlMode::POSITION;
            
            axis.motor_config.max_velocity = motor_json.value("max_velocity", 2.0);
            axis.motor_config.max_acceleration = motor_json.value("max_acceleration", 0.5);
            axis.motor_config.max_torque = motor_json.value("max_torque", 100.0);
            axis.motor_config.encoder_counts_per_degree = motor_json.value("encoder_counts_per_degree", 10000.0);
            axis.motor_config.gear_ratio = motor_json.value("gear_ratio", 1.0);
            axis.motor_config.enable_current_limit = motor_json.value("enable_current_limit", true);
            axis.motor_config.current_limit = motor_json.value("current_limit", 5.0);
            axis.motor_config.enable_temperature_protection = motor_json.value("enable_temperature_protection", true);
            axis.motor_config.max_temperature = motor_json.value("max_temperature", 80.0);
            
            // Parse encoder config
            auto encoder_json = axis_json.value("encoder_config", nlohmann::json::object());
            std::string encoder_type_str = encoder_json.value("type", "ABSOLUTE");
            if (encoder_type_str == "ABSOLUTE") axis.encoder_config.type = EncoderType::ABSOLUTE;
            else if (encoder_type_str == "INCREMENTAL") axis.encoder_config.type = EncoderType::INCREMENTAL;
            else if (encoder_type_str == "RESOLVER") axis.encoder_config.type = EncoderType::RESOLVER;
            else if (encoder_type_str == "HALL_SENSOR") axis.encoder_config.type = EncoderType::HALL_SENSOR;
            else axis.encoder_config.type = EncoderType::ABSOLUTE;
            
            std::string interface_str = encoder_json.value("interface", "SSI");
            if (interface_str == "SSI") axis.encoder_config.interface = EncoderInterface::SSI;
            else if (interface_str == "QUADRATURE") axis.encoder_config.interface = EncoderInterface::QUADRATURE;
            else if (interface_str == "BISS") axis.encoder_config.interface = EncoderInterface::BISS;
            else if (interface_str == "ENDAT") axis.encoder_config.interface = EncoderInterface::ENDAT;
            else if (interface_str == "CANOPEN") axis.encoder_config.interface = EncoderInterface::CANOPEN;
            else axis.encoder_config.interface = EncoderInterface::SSI;
            
            axis.encoder_config.resolution = encoder_json.value("resolution", 16384);
            axis.encoder_config.counts_per_degree = encoder_json.value("counts_per_degree", 10000.0);
            axis.encoder_config.use_index_pulse = encoder_json.value("use_index_pulse", true);
            axis.encoder_config.use_direction_signal = encoder_json.value("use_direction_signal", true);
            axis.encoder_config.max_velocity = encoder_json.value("max_velocity", 1000.0);
            axis.encoder_config.enable_error_detection = encoder_json.value("enable_error_detection", true);
            axis.encoder_config.error_threshold = encoder_json.value("error_threshold", 10);
            axis.encoder_config.calibration_offset = encoder_json.value("calibration_offset", 0.0);
            
            // Parse safety limits
            auto safety_json = axis_json.value("safety_limits", nlohmann::json::object());
            axis.safety_limits.min_position = safety_json.value("min_position", -270.0);
            axis.safety_limits.max_position = safety_json.value("max_position", 270.0);
            axis.safety_limits.max_velocity = safety_json.value("max_velocity", 5.0);
            axis.safety_limits.max_acceleration = safety_json.value("max_acceleration", 2.0);
            axis.safety_limits.max_current = safety_json.value("max_current", 10.0);
            axis.safety_limits.max_temperature = safety_json.value("max_temperature", 80.0);
            
            config.axes.push_back(axis);
        }
        
        // Parse derotator configuration
        auto derotator = json.value("derotator", nlohmann::json::object());
        std::string derotator_type_str = derotator.value("type", "STEPPER");
        if (derotator_type_str == "CANOPEN") config.derotator.type = DerotatorType::CANOPEN;
        else if (derotator_type_str == "STEPPER") config.derotator.type = DerotatorType::STEPPER;
        else if (derotator_type_str == "SERVO") config.derotator.type = DerotatorType::SERVO;
        else if (derotator_type_str == "CUSTOM") config.derotator.type = DerotatorType::CUSTOM;
        else config.derotator.type = DerotatorType::STEPPER;
        config.derotator.enabled = derotator.value("enabled", false);
        config.derotator.gear_ratio = derotator.value("gear_ratio", 180.0);
        config.derotator.max_speed = derotator.value("max_speed", 5.0);
        config.derotator.max_acceleration = derotator.value("max_acceleration", 2.0);
        config.derotator.backlash = derotator.value("backlash", 0.0);
        config.derotator.absolute_encoder = derotator.value("absolute_encoder", false);
        config.derotator.encoder_resolution = derotator.value("encoder_resolution", 36000.0);
        config.derotator.homing_offset = derotator.value("homing_offset", 0.0);
        config.derotator.connection_string = derotator.value("connection_string", "");
        
        // Load calibration table
        config.derotator.calibration_table.clear();
        auto calib_array = derotator.value("calibration_table", nlohmann::json::array());
        for (const auto& val : calib_array) {
            if (val.is_number()) {
                config.derotator.calibration_table.push_back(val.get<double>());
            }
        }
        
        // Parse PID parameters
        auto pid_json = json.value("pid_params", nlohmann::json::object());
        config.pid_params.kp = pid_json.value("kp", 1.5);
        config.pid_params.ki = pid_json.value("ki", 0.2);
        config.pid_params.kd = pid_json.value("kd", 0.05);
        config.pid_params.integral_limit = pid_json.value("integral_limit", 1000.0);
        config.pid_params.output_limit = pid_json.value("output_limit", 100.0);
        config.pid_params.anti_windup_gain = pid_json.value("anti_windup_gain", 0.1);
        config.pid_params.enable_anti_windup = pid_json.value("enable_anti_windup", true);
        
        // Parse safety configuration
        auto safety_json = json.value("safety", nlohmann::json::object());
        config.safety.enable_limits = safety_json.value("enable_limits", true);
        config.safety.enable_emergency_stop = safety_json.value("enable_emergency_stop", true);
        config.safety.emergency_stop_timeout_ms = safety_json.value("emergency_stop_timeout_ms", 100);
        config.safety.enable_temperature_monitoring = safety_json.value("enable_temperature_monitoring", true);
        config.safety.enable_current_monitoring = safety_json.value("enable_current_monitoring", true);
        config.safety.enable_voltage_monitoring = safety_json.value("enable_voltage_monitoring", true);
        config.safety.min_voltage = safety_json.value("min_voltage", 20.0);
        config.safety.max_voltage = safety_json.value("max_voltage", 30.0);
        config.safety.monitoring_rate = safety_json.value("monitoring_rate", 10);
        
        return config;
    }
    
    nlohmann::json toJson() const {
        nlohmann::json hal;
        
        // Map HAL type to string
        std::string type_str;
        switch (type) {
            case HALType::SIMULATED: type_str = "simulated"; break;
            case HALType::CANOPEN: type_str = "canopen"; break;
            case HALType::SERIAL: type_str = "serial"; break;
            case HALType::ETHERNET: type_str = "ethernet"; break;
            case HALType::GAMEPAD: type_str = "gamepad"; break;
            case HALType::CUSTOM: type_str = "custom"; break;
            default: type_str = "simulated";
        }
        hal["type"] = type_str;
        hal["name"] = name;
        
        // Save simulated configuration
        nlohmann::json simulated_json;
        simulated_json["enable_simulation"] = simulated.enable_simulation;
        simulated_json["simulation_update_rate"] = simulated.simulation_update_rate;
        simulated_json["position_noise_stddev"] = simulated.position_noise_stddev;
        simulated_json["velocity_noise_stddev"] = simulated.velocity_noise_stddev;
        simulated_json["simulate_errors"] = simulated.simulate_errors;
        simulated_json["error_probability"] = simulated.error_probability;
        hal["simulated"] = simulated_json;
        
        // Save CANopen configuration
        nlohmann::json canopen_json;
        canopen_json["library"] = canopen.library;
        canopen_json["interface_name"] = canopen.interface_name;
        canopen_json["bitrate"] = canopen.bitrate;
        canopen_json["node_id"] = canopen.node_id;
        canopen_json["use_sync"] = canopen.use_sync;
        canopen_json["sync_period_ms"] = canopen.sync_period_ms;
        canopen_json["sdo_timeout_ms"] = canopen.sdo_timeout_ms;
        canopen_json["pdo_update_rate"] = canopen.pdo_update_rate;
        canopen_json["accel_mode"] = canopen.accel_mode;
        hal["canopen"] = canopen_json;
        
        // Save serial configuration
        nlohmann::json serial_json;
        serial_json["port"] = serial.port;
        serial_json["baud_rate"] = serial.baud_rate;
        serial_json["protocol"] = serial.protocol;
        serial_json["data_bits"] = serial.data_bits;
        serial_json["stop_bits"] = serial.stop_bits;
        serial_json["parity"] = serial.parity;
        serial_json["timeout_ms"] = serial.timeout_ms;
        hal["serial"] = serial_json;
        
        // Save ethernet configuration
        nlohmann::json ethernet_json;
        ethernet_json["ip_address"] = ethernet.ip_address;
        ethernet_json["port"] = ethernet.port;
        ethernet_json["protocol"] = ethernet.protocol;
        ethernet_json["timeout_ms"] = ethernet.timeout_ms;
        ethernet_json["retry_count"] = ethernet.retry_count;
        hal["ethernet"] = ethernet_json;
        
        // Save gamepad configuration
        nlohmann::json gamepad_json;
        gamepad_json["device_path"] = gamepad.device_path;
        gamepad_json["deadzone"] = gamepad.deadzone;
        gamepad_json["sensitivity"] = gamepad.sensitivity;
        gamepad_json["max_velocity_deg_s"] = gamepad.max_velocity_deg_s;
        gamepad_json["invert_axis1"] = gamepad.invert_axis1;
        gamepad_json["invert_axis2"] = gamepad.invert_axis2;
        gamepad_json["update_rate_hz"] = gamepad.update_rate_hz;
        nlohmann::json presets_array = nlohmann::json::array();
        for (const auto& val : gamepad.speed_presets) {
            presets_array.push_back(val);
        }
        gamepad_json["speed_presets"] = presets_array;
        
        // Save button mapping
        nlohmann::json btn_mapping_json = nlohmann::json::object();
        for (const auto& [idx, action] : gamepad.button_mapping) {
            btn_mapping_json[std::to_string(idx)] = action;
        }
        gamepad_json["button_mapping"] = btn_mapping_json;
        
        // Save axis mapping
        nlohmann::json axis_mapping_json = nlohmann::json::object();
        for (const auto& [idx, action] : gamepad.axis_mapping) {
            axis_mapping_json[std::to_string(idx)] = action;
        }
        gamepad_json["axis_mapping"] = axis_mapping_json;
        
        hal["gamepad"] = gamepad_json;
        
        // Save axes configurations
        nlohmann::json axes_array = nlohmann::json::array();
        for (const auto& axis : axes) {
            nlohmann::json axis_json;
            axis_json["id"] = axis.id;
            axis_json["name"] = axis.name;
            axis_json["can_node_id"] = axis.can_node_id;
            
            // Save motor config
            nlohmann::json motor_json;
            std::string motor_type_str;
            switch (axis.motor_config.type) {
                case MotorType::STEPPER: motor_type_str = "STEPPER"; break;
                case MotorType::SERVO: motor_type_str = "SERVO"; break;
                case MotorType::BRUSHED_DC: motor_type_str = "BRUSHED_DC"; break;
                case MotorType::BRUSHLESS_DC: motor_type_str = "BRUSHLESS_DC"; break;
                case MotorType::CANOPEN_SERVO: motor_type_str = "CANOPEN_SERVO"; break;
                case MotorType::VIRTUAL: motor_type_str = "VIRTUAL"; break;
                default: motor_type_str = "STEPPER";
            }
            motor_json["type"] = motor_type_str;
            
            std::string control_mode_str;
            switch (axis.motor_config.default_mode) {
                case ControlMode::POSITION: control_mode_str = "POSITION"; break;
                case ControlMode::VELOCITY: control_mode_str = "VELOCITY"; break;
                case ControlMode::TORQUE: control_mode_str = "TORQUE"; break;
                case ControlMode::TRAJECTORY: control_mode_str = "TRAJECTORY"; break;
                case ControlMode::OPEN_LOOP: control_mode_str = "OPEN_LOOP"; break;
                default: control_mode_str = "POSITION";
            }
            motor_json["default_mode"] = control_mode_str;
            
            motor_json["max_velocity"] = axis.motor_config.max_velocity;
            motor_json["max_acceleration"] = axis.motor_config.max_acceleration;
            motor_json["max_torque"] = axis.motor_config.max_torque;
            motor_json["encoder_counts_per_degree"] = axis.motor_config.encoder_counts_per_degree;
            motor_json["gear_ratio"] = axis.motor_config.gear_ratio;
            motor_json["enable_current_limit"] = axis.motor_config.enable_current_limit;
            motor_json["current_limit"] = axis.motor_config.current_limit;
            motor_json["enable_temperature_protection"] = axis.motor_config.enable_temperature_protection;
            motor_json["max_temperature"] = axis.motor_config.max_temperature;
            axis_json["motor_config"] = motor_json;
            
            // Save encoder config
            nlohmann::json encoder_json;
            std::string encoder_type_str;
            switch (axis.encoder_config.type) {
                case EncoderType::ABSOLUTE: encoder_type_str = "ABSOLUTE"; break;
                case EncoderType::INCREMENTAL: encoder_type_str = "INCREMENTAL"; break;
                case EncoderType::RESOLVER: encoder_type_str = "RESOLVER"; break;
                case EncoderType::HALL_SENSOR: encoder_type_str = "HALL_SENSOR"; break;
                case EncoderType::VIRTUAL: encoder_type_str = "VIRTUAL"; break;
                default: encoder_type_str = "ABSOLUTE";
            }
            encoder_json["type"] = encoder_type_str;
            
            std::string interface_str;
            switch (axis.encoder_config.interface) {
                case EncoderInterface::QUADRATURE: interface_str = "QUADRATURE"; break;
                case EncoderInterface::SSI: interface_str = "SSI"; break;
                case EncoderInterface::BISS: interface_str = "BISS"; break;
                case EncoderInterface::ENDAT: interface_str = "ENDAT"; break;
                case EncoderInterface::CANOPEN: interface_str = "CANOPEN"; break;
                case EncoderInterface::ANALOG: interface_str = "ANALOG"; break;
                default: interface_str = "SSI";
            }
            encoder_json["interface"] = interface_str;
            
            encoder_json["resolution"] = axis.encoder_config.resolution;
            encoder_json["counts_per_degree"] = axis.encoder_config.counts_per_degree;
            encoder_json["use_index_pulse"] = axis.encoder_config.use_index_pulse;
            encoder_json["use_direction_signal"] = axis.encoder_config.use_direction_signal;
            encoder_json["max_velocity"] = axis.encoder_config.max_velocity;
            encoder_json["enable_error_detection"] = axis.encoder_config.enable_error_detection;
            encoder_json["error_threshold"] = axis.encoder_config.error_threshold;
            encoder_json["calibration_offset"] = axis.encoder_config.calibration_offset;
            axis_json["encoder_config"] = encoder_json;
            
            // Save safety limits
            nlohmann::json safety_limits_json;
            safety_limits_json["min_position"] = axis.safety_limits.min_position;
            safety_limits_json["max_position"] = axis.safety_limits.max_position;
            safety_limits_json["max_velocity"] = axis.safety_limits.max_velocity;
            safety_limits_json["max_acceleration"] = axis.safety_limits.max_acceleration;
            safety_limits_json["max_current"] = axis.safety_limits.max_current;
            safety_limits_json["max_temperature"] = axis.safety_limits.max_temperature;
            axis_json["safety_limits"] = safety_limits_json;
            
            axes_array.push_back(axis_json);
        }
        hal["axes"] = axes_array;
        
        // Save derotator configuration
        nlohmann::json derotator_json;
        std::string derotator_type_str;
        switch (derotator.type) {
            case DerotatorType::CANOPEN: derotator_type_str = "CANOPEN"; break;
            case DerotatorType::STEPPER: derotator_type_str = "STEPPER"; break;
            case DerotatorType::SERVO: derotator_type_str = "SERVO"; break;
            case DerotatorType::CUSTOM: derotator_type_str = "CUSTOM"; break;
            default: derotator_type_str = "STEPPER";
        }
        derotator_json["type"] = derotator_type_str;
        derotator_json["enabled"] = derotator.enabled;
        derotator_json["gear_ratio"] = derotator.gear_ratio;
        derotator_json["max_speed"] = derotator.max_speed;
        derotator_json["max_acceleration"] = derotator.max_acceleration;
        derotator_json["backlash"] = derotator.backlash;
        derotator_json["absolute_encoder"] = derotator.absolute_encoder;
        derotator_json["encoder_resolution"] = derotator.encoder_resolution;
        derotator_json["homing_offset"] = derotator.homing_offset;
        derotator_json["connection_string"] = derotator.connection_string;
        
        // Save calibration table
        nlohmann::json calib_array = nlohmann::json::array();
        for (const auto& val : derotator.calibration_table) {
            calib_array.push_back(val);
        }
        derotator_json["calibration_table"] = calib_array;
        hal["derotator"] = derotator_json;
        
        // Save PID parameters
        nlohmann::json pid_json;
        pid_json["kp"] = pid_params.kp;
        pid_json["ki"] = pid_params.ki;
        pid_json["kd"] = pid_params.kd;
        pid_json["integral_limit"] = pid_params.integral_limit;
        pid_json["output_limit"] = pid_params.output_limit;
        pid_json["anti_windup_gain"] = pid_params.anti_windup_gain;
        pid_json["enable_anti_windup"] = pid_params.enable_anti_windup;
        hal["pid_params"] = pid_json;
        
        // Save safety configuration
        nlohmann::json safety_json;
        safety_json["enable_limits"] = safety.enable_limits;
        safety_json["enable_emergency_stop"] = safety.enable_emergency_stop;
        safety_json["emergency_stop_timeout_ms"] = safety.emergency_stop_timeout_ms;
        safety_json["enable_temperature_monitoring"] = safety.enable_temperature_monitoring;
        safety_json["enable_current_monitoring"] = safety.enable_current_monitoring;
        safety_json["enable_voltage_monitoring"] = safety.enable_voltage_monitoring;
        safety_json["min_voltage"] = safety.min_voltage;
        safety_json["max_voltage"] = safety.max_voltage;
        safety_json["monitoring_rate"] = safety.monitoring_rate;
        hal["safety"] = safety_json;
        
        return hal;
    }
    
    static HALConfig getDefault() {
        HALConfig config;
        // All fields already initialized with default values in struct declaration
        return config;
    }
    
    // Metody pomocnicze
    std::string getTypeString() const {
        switch (type) {
            case HALType::SIMULATED: return "simulated";
            case HALType::CANOPEN: return "canopen";
            case HALType::SERIAL: return "serial";
            case HALType::ETHERNET: return "ethernet";
            case HALType::GAMEPAD: return "gamepad";
            case HALType::CUSTOM: return "custom";
            default: return "unknown";
        }
    }
    
    static HALType typeFromString(const std::string& type_str) {
        if (type_str == "simulated") return HALType::SIMULATED;
        if (type_str == "canopen") return HALType::CANOPEN;
        if (type_str == "serial") return HALType::SERIAL;
        if (type_str == "ethernet") return HALType::ETHERNET;
        if (type_str == "gamepad") return HALType::GAMEPAD;
        if (type_str == "custom") return HALType::CUSTOM;
        return HALType::SIMULATED;
    }
};

} // namespace hal
} // namespace astro_mount
