#pragma once
#include <string>
#include <vector>
#include <map>
#include <nlohmann/json.hpp>
#include "hal/hal_interface.h"
#include "hal/motor_control.h"
#include "hal/encoder_reader.h"
namespace astro_mount {
namespace hal {

enum class HALType {
    SIMULATED,   // Symulowany hardware
    SERIAL,      // Port szeregowy (RS-232/485)
    ETHERNET,    // Ethernet (EtherCAT, Modbus TCP)
    GAMEPAD,     // Ręczne sterowanie (gamepad/joystick)
    CUSTOM       // Własna implementacja
};
// (Derotator types removed — derotator functionality eliminated from the project)

struct HALConfig {
    HALType type{HALType::SIMULATED};
    std::string name{"Default_HAL"};
    
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
        bool autostart{false};                 // Automatycznie uruchom gamepad po starcie
        int gamepad_mode{0};                   // Tryb nawigacji: 0=RAW, 1=CELESTIAL, 2=ALT_AZ
        
        // Mapowanie przycisków: physical_index → nazwa akcji
        // Akcje: "home", "stop", "emergency_stop", "park",
        //        "speed_up", "speed_down", "manual_toggle", "none"
        std::map<int, std::string> button_mapping;
        
        // Mapowanie osi: physical_index → nazwa osi
        // Osie: "lx", "ly", "rx", "ry",
        //       "trigger_l", "trigger_r", "pov_x", "pov_y", "none"
        std::map<int, std::string> axis_mapping;
    } gamepad;
    
    // Konfiguracja osi
    struct AxisConfig {
        int id{0};
        std::string name{"Axis_0"};
        uint8_t can_node_id{1};  // CANopen node ID (1-127), must be set explicitly
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
        config.gamepad.autostart = gamepad.value("autostart", false);
        config.gamepad.gamepad_mode = gamepad.value("gamepad_mode", 0);
        
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
            axis.can_node_id = axis_json.value("can_node_id", 1);
            
            // Parse motor config
            auto motor_json = axis_json.value("motor_config", nlohmann::json::object());
            std::string motor_type_str = motor_json.value("type", "STEPPER");
            if (motor_type_str == "STEPPER") axis.motor_config.type = MotorType::STEPPER;
            else if (motor_type_str == "SERVO") axis.motor_config.type = MotorType::SERVO;
            else if (motor_type_str == "BRUSHED_DC") axis.motor_config.type = MotorType::BRUSHED_DC;
            else if (motor_type_str == "BRUSHLESS_DC") axis.motor_config.type = MotorType::BRUSHLESS_DC;
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
        gamepad_json["autostart"] = gamepad.autostart;
        gamepad_json["gamepad_mode"] = gamepad.gamepad_mode;
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
            case HALType::SERIAL: return "serial";
            case HALType::ETHERNET: return "ethernet";
            case HALType::GAMEPAD: return "gamepad";
            case HALType::CUSTOM: return "custom";
            default: return "unknown";
        }
    }
    
    static HALType typeFromString(const std::string& type_str) {
        if (type_str == "simulated") return HALType::SIMULATED;
        if (type_str == "serial") return HALType::SERIAL;
        if (type_str == "ethernet") return HALType::ETHERNET;
        if (type_str == "gamepad") return HALType::GAMEPAD;
        if (type_str == "custom") return HALType::CUSTOM;
        return HALType::SIMULATED;
    }

    // ─── Focuser configuration ───────────────────────────────────────────
    struct {
        std::string type{"simulated"};
        std::string device_path{"/dev/ttyUSB0"};
        int baud_rate{9600};
        int max_position{100000};
        int default_speed{50};
        double temperature_coefficient{0.0};
    } focuser;

    // ─── Camera configuration ────────────────────────────────────────────
    struct {
        std::string type{"simulated"};
        std::string camera_id;
        double default_gain{0};
        double default_exposure_s{1.0};
        int default_binning{1};
        double cooler_target_c{20.0};
        bool cooler_enabled{false};
    } camera;

    // ─── Dome configuration ──────────────────────────────────────────────
    struct {
        std::string type{"simulated"};
        std::string device_path{"/dev/ttyUSB1"};
        int baud_rate{9600};
        double home_azimuth_deg{0.0};
        double park_azimuth_deg{180.0};
        int open_time_s{30};
        std::string gpio_open_pin{"gpio17"};
        std::string gpio_close_pin{"gpio18"};
    } dome;

    // ─── Sequencer configuration ─────────────────────────────────────────
    struct {
        bool auto_focus{true};
        bool auto_guide{true};
        bool dither{true};
        int focus_interval{5};
        bool weather_auto_park{true};
    } sequencer;

    // ─── Notifications configuration ─────────────────────────────────────
    struct {
        struct {
            bool enabled{false};
            std::string smtp_host{"localhost"};
            int smtp_port{587};
            bool use_tls{true};
            std::string username;
            std::string password;
            std::string from_address{"astro-mount@localhost"};
            std::string to_addresses;
            std::string subject_prefix{"[AstroMount]"};
        } email;
        struct {
            bool enabled{false};
            std::string url;
            std::string method{"POST"};
            std::string auth_token;
            int timeout_seconds{10};
            int retry_count{3};
        } webhook;
        struct {
            bool enabled{false};
            std::string broker_url{"localhost"};
            int broker_port{1883};
            std::string client_id{"astro-mount"};
            std::string topic_prefix{"astro-mount/notifications"};
            bool use_tls{false};
            std::string username;
            std::string password;
            int qos{1};
        } mqtt;
        int min_severity{1};
        bool notify_on_error{true};
        bool notify_on_weather_alert{true};
        bool aggregate_messages{false};
        int aggregation_interval_minutes{5};
    } notifications;

    // ─── Derotator configuration ─────────────────────────────────────────
    struct {
        std::string type{"simulated"};
        bool enabled{false};
        std::string device_path{""};
        double home_position_deg{0.0};
        double max_rate_deg_s{5.0};
        int microsteps{256};
        bool invert_direction{false};
    // ─── LX200 configuration ─────────────────────────────────────────────
    struct {
        bool enabled{false};
        std::string port{"/dev/ttyS0"};
        int baud_rate{9600};
        int update_interval_ms{1000};
        bool simulate{true};
    // ─── ST4 Guider configuration ────────────────────────────────────────
    struct {
        std::string interface_type{"simulated"};
        std::string device_path{""};
        int pin_ra_plus{17};
        int pin_ra_minus{22};
        int pin_dec_plus{23};
        int pin_dec_minus{24};
        double aggression{0.8};
        uint32_t min_pulse_ms{10};
        uint32_t max_pulse_ms{3000};
        bool invert_ra{false};
        bool invert_dec{false};
    // ─── PEC configuration ───────────────────────────────────────────────
    struct {
        bool enabled{false};
        double worm_cycle_seconds{638.0};
        int num_harmonics{8};
        double sample_rate_hz{10.0};
        int training_duration_cycles{3};
        bool auto_train{false};
    // ─── Power management configuration ──────────────────────────────────
    struct {
        std::string type{"simulated"};
        std::string i2c_device{"/dev/i2c-1"};
        int i2c_address{0x0B};
        double low_voltage_threshold_v{11.5};
        int poll_interval_s{10};
        bool auto_park_on_low_voltage{true};
    } power;

    } pec;

    } st4_guider;

    } lx200;

    } derotator;
};

} // namespace hal
} // namespace astro_mount
