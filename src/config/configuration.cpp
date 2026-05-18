#include "config/configuration.h"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <iomanip>

namespace astro_mount {
namespace config {

using json = nlohmann::json;

class Configuration::Impl {
public:
    Impl() : modified_(false) {
        // Initialize with default values
        initializeDefaults();
    }
    
    bool loadFromFile(const std::string& filename) {
        std::ifstream file(filename);
        if (!file.is_open()) {
            return false;
        }
        
        try {
            json data = json::parse(file);
            config_ = data;
            modified_ = false;
            return true;
        } catch (const std::exception& e) {
            // If file exists but is invalid, keep defaults
            return false;
        }
    }
    
    bool saveToFile(const std::string& filename) const {
        std::ofstream file(filename);
        if (!file.is_open()) {
            return false;
        }
        
        file << config_.dump(4);
        return true;
    }
    
    bool loadFromString(const std::string& json_str) {
        try {
            config_ = json::parse(json_str);
            modified_ = false;
            return true;
        } catch (const std::exception& e) {
            return false;
        }
    }
    
    std::string toString() const {
        return config_.dump(4);
    }
    
    std::vector<std::string> validate() const {
        std::vector<std::string> errors;
        
        // ==========================================
        // Validate logging configuration
        // ==========================================
        auto logging = config_.value("logging", json::object());
        if (!logging.contains("level") ||
            !isValidLogLevel(logging.value("level", ""))) {
            errors.push_back("Invalid or missing logging.level");
        }
        int rotation_days = logging.value("rotation_days", 0);
        if (rotation_days <= 0) {
            errors.push_back("Invalid logging.rotation_days (must be > 0)");
        }
        int max_file_size_mb = logging.value("max_file_size_mb", 0);
        if (max_file_size_mb <= 0) {
            errors.push_back("Invalid logging.max_file_size_mb (must be > 0)");
        }
        
        // ==========================================
        // Validate network configuration
        // ==========================================
        auto network = config_.value("network", json::object());
        int port = network.value("grpc_port", 0);
        if (port < 1 || port > 65535) {
            errors.push_back("Invalid network.grpc_port (must be 1-65535)");
        }
        int max_connections = network.value("max_connections", 0);
        if (max_connections <= 0) {
            errors.push_back("Invalid network.max_connections (must be > 0)");
        }
        
        // ==========================================
        // Validate CANopen configuration
        // ==========================================
        auto canopen = config_.value("canopen", json::object());
        int node_id = canopen.value("node_id", 0);
        if (node_id < 1 || node_id > 127) {
            errors.push_back("Invalid canopen.node_id (must be 1-127)");
        }
        int baud_rate = canopen.value("baud_rate", 0);
        if (baud_rate <= 0) {
            errors.push_back("Invalid canopen.baud_rate (must be > 0)");
        }
        int sync_interval_ms = canopen.value("sync_interval_ms", 0);
        if (sync_interval_ms <= 0) {
            errors.push_back("Invalid canopen.sync_interval_ms (must be > 0)");
        }
        
        // ==========================================
        // Validate mount configuration
        // ==========================================
        auto mount = config_.value("mount", json::object());
        double lat = mount.value("latitude", 0.0);
        double lon = mount.value("longitude", 0.0);
        if (lat < -90.0 || lat > 90.0) {
            errors.push_back("Invalid mount.latitude (must be -90 to 90)");
        }
        if (lon < -180.0 || lon > 180.0) {
            errors.push_back("Invalid mount.longitude (must be -180 to 180)");
        }
        double altitude = mount.value("altitude", 0.0);
        if (altitude < -500.0 || altitude > 10000.0) {
            errors.push_back("Invalid mount.altitude (must be -500 to 10000)");
        }
        double max_slew_rate = mount.value("max_slew_rate", 0.0);
        if (max_slew_rate <= 0.0) {
            errors.push_back("Invalid mount.max_slew_rate (must be > 0)");
        }
        double max_tracking_rate = mount.value("max_tracking_rate", 0.0);
        if (max_tracking_rate <= 0.0) {
            errors.push_back("Invalid mount.max_tracking_rate (must be > 0)");
        }
        double slew_acceleration = mount.value("slew_acceleration", 0.0);
        if (slew_acceleration <= 0.0) {
            errors.push_back("Invalid mount.slew_acceleration (must be > 0)");
        }
        double tracking_acceleration = mount.value("tracking_acceleration", 0.0);
        if (tracking_acceleration <= 0.0) {
            errors.push_back("Invalid mount.tracking_acceleration (must be > 0)");
        }
        double mount_height = mount.value("mount_height", 0.0);
        if (mount_height < 0.0) {
            errors.push_back("Invalid mount.mount_height (must be >= 0)");
        }
        
        // Mount axis_physical_parameters validation
        auto axis_params = mount.value("axis_physical_parameters", json::object());
        for (const auto& axis_name : {"ha_axis", "dec_axis"}) {
            auto axis = axis_params.value(axis_name, json::object());
            double motor_steps = axis.value("motor_steps_per_rev", 0.0);
            if (motor_steps <= 0.0) {
                errors.push_back("Invalid mount.axis_physical_parameters." +
                    std::string(axis_name) + ".motor_steps_per_rev (must be > 0)");
            }
            double microstepping = axis.value("motor_microstepping", 0.0);
            if (microstepping <= 0.0) {
                errors.push_back("Invalid mount.axis_physical_parameters." +
                    std::string(axis_name) + ".motor_microstepping (must be > 0)");
            }
            double enc_res = axis.value("encoder_resolution", 0.0);
            if (enc_res <= 0.0) {
                errors.push_back("Invalid mount.axis_physical_parameters." +
                    std::string(axis_name) + ".encoder_resolution (must be > 0)");
            }
            double gear_ratio = axis.value("gear_ratio", 0.0);
            if (gear_ratio <= 0.0) {
                errors.push_back("Invalid mount.axis_physical_parameters." +
                    std::string(axis_name) + ".gear_ratio (must be > 0)");
            }
        }
        
        // ==========================================
        // Validate telescope configuration
        // ==========================================
        auto telescope = config_.value("telescope", json::object());
        double focal = telescope.value("focal_length", 0.0);
        if (focal <= 0.0) {
            errors.push_back("Invalid telescope.focal_length (must be > 0)");
        }
        double aperture = telescope.value("aperture", 0.0);
        if (aperture <= 0.0) {
            errors.push_back("Invalid telescope.aperture (must be > 0)");
        }
        double pixel_size = telescope.value("pixel_size", 0.0);
        if (pixel_size <= 0.0) {
            errors.push_back("Invalid telescope.pixel_size (must be > 0)");
        }
        
        // ==========================================
        // Validate guider configuration
        // ==========================================
        auto guider = config_.value("guider", json::object());
        double max_correction = guider.value("max_correction", 0.0);
        if (max_correction <= 0.0) {
            errors.push_back("Invalid guider.max_correction (must be > 0)");
        }
        double aggression = guider.value("aggression", -1.0);
        if (aggression < 0.0 || aggression > 1.0) {
            errors.push_back("Invalid guider.aggression (must be 0.0 to 1.0)");
        }
        
        // ==========================================
        // Validate Kalman configuration
        // ==========================================
        auto kalman = config_.value("kalman", json::object());
        double process_noise = kalman.value("process_noise", 0.0);
        double measurement_noise = kalman.value("measurement_noise", 0.0);
        if (process_noise < 0.0 || measurement_noise < 0.0) {
            errors.push_back("Kalman noise parameters must be >= 0");
        }
        double innovation_threshold = kalman.value("innovation_threshold", 0.0);
        if (innovation_threshold <= 0.0) {
            errors.push_back("Invalid kalman.innovation_threshold (must be > 0)");
        }
        int max_iterations = kalman.value("max_iterations", 0);
        if (max_iterations <= 0) {
            errors.push_back("Invalid kalman.max_iterations (must be > 0)");
        }
        
        // ==========================================
        // Validate TPOINT configuration
        // ==========================================
        auto tpoint = config_.value("tpoint", json::object());
        uint32_t enabled_terms = tpoint.value("enabled_terms", 0);
        if (enabled_terms == 0) {
            errors.push_back("Invalid tpoint.enabled_terms (must be > 0)");
        }
        double max_residual = tpoint.value("max_residual", 0.0);
        if (max_residual <= 0.0) {
            errors.push_back("Invalid tpoint.max_residual (must be > 0)");
        }
        int min_measurements = tpoint.value("min_measurements", 0);
        if (min_measurements <= 0) {
            errors.push_back("Invalid tpoint.min_measurements (must be > 0)");
        }
        
        // ==========================================
        // Validate derotator configuration
        // ==========================================
        auto derotator = config_.value("derotator", json::object());
        if (!derotator.empty()) {
            double d_gear_ratio = derotator.value("gear_ratio", 0.0);
            if (d_gear_ratio <= 0.0) {
                errors.push_back("Invalid derotator.gear_ratio (must be > 0)");
            }
            double d_max_speed = derotator.value("max_speed", 0.0);
            if (d_max_speed <= 0.0) {
                errors.push_back("Invalid derotator.max_speed (must be > 0)");
            }
        }
        
        return errors;
    }
    
    LoggingConfig getLoggingConfig() const {
        LoggingConfig config;
        auto logging = config_.value("logging", json::object());
        
        config.level = logging.value("level", "INFO");
        config.directory = logging.value("directory", "/var/log/astro-mount");
        config.rotation_days = logging.value("rotation_days", 7);
        config.max_file_size_mb = logging.value("max_file_size_mb", 100);
        config.console_output = logging.value("console_output", true);
        
        return config;
    }
    
    NetworkConfig getNetworkConfig() const {
        NetworkConfig config;
        auto network = config_.value("network", json::object());
        
        config.grpc_address = network.value("grpc_address", "0.0.0.0");
        config.grpc_port = network.value("grpc_port", 50051);
        config.max_connections = network.value("max_connections", 10);
        config.enable_ssl = network.value("enable_ssl", false);
        config.ssl_cert_path = network.value("ssl_cert_path", "");
        config.ssl_key_path = network.value("ssl_key_path", "");
        
        return config;
    }
    
    CanOpenConfig getCanOpenConfig() const {
        CanOpenConfig config;
        auto canopen = config_.value("canopen", json::object());
        
        config.interface = canopen.value("interface", "can0");
        config.node_id = canopen.value("node_id", 1);
        config.baud_rate = canopen.value("baud_rate", 1000000);
        config.enable_sync = canopen.value("enable_sync", true);
        config.sync_interval_ms = canopen.value("sync_interval_ms", 100);
        
        return config;
    }
    
    MountConfig getMountConfig() const {
        MountConfig config;
        auto mount = config_.value("mount", json::object());
        
        config.type = mount.value("type", "equatorial");
        config.latitude = mount.value("latitude", 52.0);
        config.longitude = mount.value("longitude", 21.0);
        config.altitude = mount.value("altitude", 100.0);
        config.max_slew_rate = mount.value("max_slew_rate", 5.0);
        config.max_tracking_rate = mount.value("max_tracking_rate", 0.004178);
        config.slew_acceleration = mount.value("slew_acceleration", 1.0);
        config.tracking_acceleration = mount.value("tracking_acceleration", 0.001);
        
        // Load additional mount parameters from JSON
        config.mount_height = mount.value("mount_height", 1.5);
        config.pier_west = mount.value("pier_west", 0.0);
        config.pier_east = mount.value("pier_east", 0.0);
        config.default_temperature = mount.value("default_temperature", 15.0);
        config.default_pressure = mount.value("default_pressure", 1013.25);
        config.default_humidity = mount.value("default_humidity", 0.5);
        config.use_encoders = mount.value("use_encoders", true);
        config.encoders_absolute = mount.value("encoders_absolute", true);
        config.encoder_resolution = mount.value("encoder_resolution", 16384.0);
        
        // Position/rate tolerance (from config file, fallback to struct defaults)
        config.position_tolerance = mount.value("position_tolerance", 0.1);
        config.rate_tolerance = mount.value("rate_tolerance", 0.01);
        
        // Meridian flip configuration
        config.meridian_flip_enabled = mount.value("meridian_flip_enabled", true);
        config.meridian_flip_delay_minutes = mount.value("meridian_flip_delay_minutes", 5.0);
        config.meridian_flip_hysteresis_degrees = mount.value("meridian_flip_hysteresis_degrees", 0.5);
        config.meridian_flip_timeout_seconds = mount.value("meridian_flip_timeout_seconds", 120.0);
        
        // Soft limits
        config.soft_limits_enabled = mount.value("soft_limits_enabled", true);
        config.soft_limit_axis1_min = mount.value("soft_limit_axis1_min", -270.0);
        config.soft_limit_axis1_max = mount.value("soft_limit_axis1_max", 270.0);
        config.soft_limit_axis2_min = mount.value("soft_limit_axis2_min", -5.0);
        config.soft_limit_axis2_max = mount.value("soft_limit_axis2_max", 185.0);
        config.soft_limit_warning_degrees = mount.value("soft_limit_warning_degrees", 10.0);
        config.soft_limit_deceleration_degrees = mount.value("soft_limit_deceleration_degrees", 5.0);
        config.soft_limit_tracking_rate_factor = mount.value("soft_limit_tracking_rate_factor", 0.1);
        
        // Park position
        config.park_position_axis1 = mount.value("park_position_axis1", 0.0);
        config.park_position_axis2 = mount.value("park_position_axis2", 90.0);
        
        // Atmospheric refraction correction
        config.enable_refraction_correction = mount.value("enable_refraction_correction", true);
        
        // Load axis physical parameters
        auto axis_params = mount.value("axis_physical_parameters", json::object());
        auto ha_axis = axis_params.value("ha_axis", json::object());
        auto dec_axis = axis_params.value("dec_axis", json::object());
        
        // HA axis parameters
        config.ha_axis_params.motor_steps_per_rev = ha_axis.value("motor_steps_per_rev", 200.0);
        config.ha_axis_params.motor_microstepping = ha_axis.value("motor_microstepping", 64.0);
        config.ha_axis_params.motor_step_angle = ha_axis.value("motor_step_angle", 101.25);
        config.ha_axis_params.encoder_resolution = ha_axis.value("encoder_resolution", 16384.0);
        config.ha_axis_params.encoder_counts_per_arcsec = ha_axis.value("encoder_counts_per_arcsec", 0.0126);
        config.ha_axis_params.encoder_quantization_error = ha_axis.value("encoder_quantization_error", 39.6);
        config.ha_axis_params.gear_ratio = ha_axis.value("gear_ratio", 360.0);
        config.ha_axis_params.worm_ratio = ha_axis.value("worm_ratio", 180.0);
        config.ha_axis_params.worm_teeth = ha_axis.value("worm_teeth", 1);
        config.ha_axis_params.worm_wheel_teeth = ha_axis.value("worm_wheel_teeth", 180);
        config.ha_axis_params.cyclic_error_amplitude = ha_axis.value("cyclic_error_amplitude", 15.2);
        config.ha_axis_params.cyclic_error_period = ha_axis.value("cyclic_error_period", 360.0);
        
        // Load cyclic harmonics
        auto ha_harmonics = ha_axis.value("cyclic_harmonics", json::array({10.5, 0.0, 3.2, 1.5708, 1.1, 3.1416, 0.5, 4.7124}));
        for (size_t i = 0; i < 8 && i < ha_harmonics.size(); ++i) {
            config.ha_axis_params.cyclic_harmonics[i] = ha_harmonics[i];
        }
        
        config.ha_axis_params.backlash = ha_axis.value("backlash", 8.5);
        config.ha_axis_params.backlash_temp_coeff = ha_axis.value("backlash_temp_coeff", 0.02);
        config.ha_axis_params.axis_stiffness = ha_axis.value("axis_stiffness", 0.5);
        config.ha_axis_params.torsional_compliance = ha_axis.value("torsional_compliance", 1e-6);
        config.ha_axis_params.expansion_coeff = ha_axis.value("expansion_coeff", 11.0e-6);
        config.ha_axis_params.temp_gear_error_coeff = ha_axis.value("temp_gear_error_coeff", 0.05);
        config.ha_axis_params.calibration_temp = ha_axis.value("calibration_temp", 20.0);
        
        // Dec axis parameters
        config.dec_axis_params.motor_steps_per_rev = dec_axis.value("motor_steps_per_rev", 200.0);
        config.dec_axis_params.motor_microstepping = dec_axis.value("motor_microstepping", 64.0);
        config.dec_axis_params.motor_step_angle = dec_axis.value("motor_step_angle", 101.25);
        config.dec_axis_params.encoder_resolution = dec_axis.value("encoder_resolution", 16384.0);
        config.dec_axis_params.encoder_counts_per_arcsec = dec_axis.value("encoder_counts_per_arcsec", 0.0126);
        config.dec_axis_params.encoder_quantization_error = dec_axis.value("encoder_quantization_error", 39.6);
        config.dec_axis_params.gear_ratio = dec_axis.value("gear_ratio", 360.0);
        config.dec_axis_params.worm_ratio = dec_axis.value("worm_ratio", 180.0);
        config.dec_axis_params.worm_teeth = dec_axis.value("worm_teeth", 1);
        config.dec_axis_params.worm_wheel_teeth = dec_axis.value("worm_wheel_teeth", 180);
        config.dec_axis_params.cyclic_error_amplitude = dec_axis.value("cyclic_error_amplitude", 12.8);
        config.dec_axis_params.cyclic_error_period = dec_axis.value("cyclic_error_period", 360.0);
        
        // Load cyclic harmonics for dec axis
        auto dec_harmonics = dec_axis.value("cyclic_harmonics", json::array({8.2, 0.0, 2.5, 1.5708, 0.8, 3.1416, 0.3, 4.7124}));
        for (size_t i = 0; i < 8 && i < dec_harmonics.size(); ++i) {
            config.dec_axis_params.cyclic_harmonics[i] = dec_harmonics[i];
        }
        
        config.dec_axis_params.backlash = dec_axis.value("backlash", 6.3);
        config.dec_axis_params.backlash_temp_coeff = dec_axis.value("backlash_temp_coeff", 0.015);
        config.dec_axis_params.axis_stiffness = dec_axis.value("axis_stiffness", 0.6);
        config.dec_axis_params.torsional_compliance = dec_axis.value("torsional_compliance", 1.2e-6);
        config.dec_axis_params.expansion_coeff = dec_axis.value("expansion_coeff", 11.0e-6);
        config.dec_axis_params.temp_gear_error_coeff = dec_axis.value("temp_gear_error_coeff", 0.04);
        config.dec_axis_params.calibration_temp = dec_axis.value("calibration_temp", 20.0);
        
        return config;
    }
    
    TelescopeConfig getTelescopeConfig() const {
        TelescopeConfig config;
        auto telescope = config_.value("telescope", json::object());
        
        config.focal_length = telescope.value("focal_length", 2000.0);
        config.aperture = telescope.value("aperture", 200.0);
        config.tube_length = telescope.value("tube_length", 1800.0);
        config.camera_model = telescope.value("camera_model", "ASI1600");
        config.pixel_size = telescope.value("pixel_size", 3.8);
        config.sensor_width = telescope.value("sensor_width", 4656);
        config.sensor_height = telescope.value("sensor_height", 3520);
        
        return config;
    }
    
    GuiderConfig getGuiderConfig() const {
        GuiderConfig config;
        auto guider = config_.value("guider", json::object());
        
        config.enabled = guider.value("enabled", false);
        config.connection_string = guider.value("connection_string", "");
        config.max_correction = guider.value("max_correction", 10.0);
        config.aggression = guider.value("aggression", 0.5);
        config.exposure_time_ms = guider.value("exposure_time_ms", 2000);
        config.binning = guider.value("binning", 2);
        
        return config;
    }
    
    KalmanConfig getKalmanConfig() const {
        KalmanConfig config;
        auto kalman = config_.value("kalman", json::object());
        
        config.process_noise = kalman.value("process_noise", 0.01);
        config.measurement_noise = kalman.value("measurement_noise", 1.0);
        config.adaptive_q = kalman.value("adaptive_q", true);
        config.adaptive_r = kalman.value("adaptive_r", false);
        config.innovation_threshold = kalman.value("innovation_threshold", 3.0);
        config.max_iterations = kalman.value("max_iterations", 10);
        
        return config;
    }
    
    TPointConfig getTPointConfig() const {
        TPointConfig config;
        auto tpoint = config_.value("tpoint", json::object());
        
        config.enabled_terms = tpoint.value("enabled_terms", 0xFFFF);
        config.min_measurements = tpoint.value("min_measurements", 10);
        config.max_residual = tpoint.value("max_residual", 30.0);
        config.auto_calibrate = tpoint.value("auto_calibrate", true);
        
        return config;
    }
    
    void setLoggingConfig(const LoggingConfig& config) {
        json logging;
        logging["level"] = config.level;
        logging["directory"] = config.directory;
        logging["rotation_days"] = config.rotation_days;
        logging["max_file_size_mb"] = config.max_file_size_mb;
        logging["console_output"] = config.console_output;
        
        config_["logging"] = logging;
        modified_ = true;
    }
    
    void setNetworkConfig(const NetworkConfig& config) {
        json network;
        network["grpc_address"] = config.grpc_address;
        network["grpc_port"] = config.grpc_port;
        network["max_connections"] = config.max_connections;
        network["enable_ssl"] = config.enable_ssl;
        network["ssl_cert_path"] = config.ssl_cert_path;
        network["ssl_key_path"] = config.ssl_key_path;
        
        config_["network"] = network;
        modified_ = true;
    }
    
    void setCanOpenConfig(const CanOpenConfig& config) {
        json canopen;
        canopen["interface"] = config.interface;
        canopen["node_id"] = config.node_id;
        canopen["baud_rate"] = config.baud_rate;
        canopen["enable_sync"] = config.enable_sync;
        canopen["sync_interval_ms"] = config.sync_interval_ms;
        
        config_["canopen"] = canopen;
        modified_ = true;
    }
    
    void setMountConfig(const MountConfig& config) {
        json mount;
        mount["type"] = config.type;
        mount["latitude"] = config.latitude;
        mount["longitude"] = config.longitude;
        mount["altitude"] = config.altitude;
        mount["max_slew_rate"] = config.max_slew_rate;
        mount["max_tracking_rate"] = config.max_tracking_rate;
        mount["slew_acceleration"] = config.slew_acceleration;
        mount["tracking_acceleration"] = config.tracking_acceleration;
        
        // Additional mount parameters
        mount["mount_height"] = config.mount_height;
        mount["pier_west"] = config.pier_west;
        mount["pier_east"] = config.pier_east;
        mount["default_temperature"] = config.default_temperature;
        mount["default_pressure"] = config.default_pressure;
        mount["default_humidity"] = config.default_humidity;
        mount["use_encoders"] = config.use_encoders;
        mount["encoders_absolute"] = config.encoders_absolute;
        mount["encoder_resolution"] = config.encoder_resolution;
        
        // Position/rate tolerance
        mount["position_tolerance"] = config.position_tolerance;
        mount["rate_tolerance"] = config.rate_tolerance;
        
        // Meridian flip configuration
        mount["meridian_flip_enabled"] = config.meridian_flip_enabled;
        mount["meridian_flip_delay_minutes"] = config.meridian_flip_delay_minutes;
        mount["meridian_flip_hysteresis_degrees"] = config.meridian_flip_hysteresis_degrees;
        mount["meridian_flip_timeout_seconds"] = config.meridian_flip_timeout_seconds;
        
        // Soft limits
        mount["soft_limits_enabled"] = config.soft_limits_enabled;
        mount["soft_limit_axis1_min"] = config.soft_limit_axis1_min;
        mount["soft_limit_axis1_max"] = config.soft_limit_axis1_max;
        mount["soft_limit_axis2_min"] = config.soft_limit_axis2_min;
        mount["soft_limit_axis2_max"] = config.soft_limit_axis2_max;
        mount["soft_limit_warning_degrees"] = config.soft_limit_warning_degrees;
        mount["soft_limit_deceleration_degrees"] = config.soft_limit_deceleration_degrees;
        mount["soft_limit_tracking_rate_factor"] = config.soft_limit_tracking_rate_factor;
        
        // Park position
        mount["park_position_axis1"] = config.park_position_axis1;
        mount["park_position_axis2"] = config.park_position_axis2;
        
        // Atmospheric refraction correction
        mount["enable_refraction_correction"] = config.enable_refraction_correction;
        
        // Axis physical parameters (HA)
        json ha_axis;
        ha_axis["motor_steps_per_rev"] = config.ha_axis_params.motor_steps_per_rev;
        ha_axis["motor_microstepping"] = config.ha_axis_params.motor_microstepping;
        ha_axis["motor_step_angle"] = config.ha_axis_params.motor_step_angle;
        ha_axis["encoder_resolution"] = config.ha_axis_params.encoder_resolution;
        ha_axis["encoder_counts_per_arcsec"] = config.ha_axis_params.encoder_counts_per_arcsec;
        ha_axis["encoder_quantization_error"] = config.ha_axis_params.encoder_quantization_error;
        ha_axis["gear_ratio"] = config.ha_axis_params.gear_ratio;
        ha_axis["worm_ratio"] = config.ha_axis_params.worm_ratio;
        ha_axis["worm_teeth"] = config.ha_axis_params.worm_teeth;
        ha_axis["worm_wheel_teeth"] = config.ha_axis_params.worm_wheel_teeth;
        ha_axis["cyclic_error_amplitude"] = config.ha_axis_params.cyclic_error_amplitude;
        ha_axis["cyclic_error_period"] = config.ha_axis_params.cyclic_error_period;
        ha_axis["cyclic_harmonics"] = config.ha_axis_params.cyclic_harmonics;
        ha_axis["backlash"] = config.ha_axis_params.backlash;
        ha_axis["backlash_temp_coeff"] = config.ha_axis_params.backlash_temp_coeff;
        ha_axis["axis_stiffness"] = config.ha_axis_params.axis_stiffness;
        ha_axis["torsional_compliance"] = config.ha_axis_params.torsional_compliance;
        ha_axis["expansion_coeff"] = config.ha_axis_params.expansion_coeff;
        ha_axis["temp_gear_error_coeff"] = config.ha_axis_params.temp_gear_error_coeff;
        ha_axis["calibration_table"] = config.ha_axis_params.calibration_table;
        ha_axis["calibration_temp"] = config.ha_axis_params.calibration_temp;
        
        // Axis physical parameters (Dec)
        json dec_axis;
        dec_axis["motor_steps_per_rev"] = config.dec_axis_params.motor_steps_per_rev;
        dec_axis["motor_microstepping"] = config.dec_axis_params.motor_microstepping;
        dec_axis["motor_step_angle"] = config.dec_axis_params.motor_step_angle;
        dec_axis["encoder_resolution"] = config.dec_axis_params.encoder_resolution;
        dec_axis["encoder_counts_per_arcsec"] = config.dec_axis_params.encoder_counts_per_arcsec;
        dec_axis["encoder_quantization_error"] = config.dec_axis_params.encoder_quantization_error;
        dec_axis["gear_ratio"] = config.dec_axis_params.gear_ratio;
        dec_axis["worm_ratio"] = config.dec_axis_params.worm_ratio;
        dec_axis["worm_teeth"] = config.dec_axis_params.worm_teeth;
        dec_axis["worm_wheel_teeth"] = config.dec_axis_params.worm_wheel_teeth;
        dec_axis["cyclic_error_amplitude"] = config.dec_axis_params.cyclic_error_amplitude;
        dec_axis["cyclic_error_period"] = config.dec_axis_params.cyclic_error_period;
        dec_axis["cyclic_harmonics"] = config.dec_axis_params.cyclic_harmonics;
        dec_axis["backlash"] = config.dec_axis_params.backlash;
        dec_axis["backlash_temp_coeff"] = config.dec_axis_params.backlash_temp_coeff;
        dec_axis["axis_stiffness"] = config.dec_axis_params.axis_stiffness;
        dec_axis["torsional_compliance"] = config.dec_axis_params.torsional_compliance;
        dec_axis["expansion_coeff"] = config.dec_axis_params.expansion_coeff;
        dec_axis["temp_gear_error_coeff"] = config.dec_axis_params.temp_gear_error_coeff;
        dec_axis["calibration_table"] = config.dec_axis_params.calibration_table;
        dec_axis["calibration_temp"] = config.dec_axis_params.calibration_temp;
        
        json axis_params;
        axis_params["ha_axis"] = ha_axis;
        axis_params["dec_axis"] = dec_axis;
        mount["axis_physical_parameters"] = axis_params;
        
        config_["mount"] = mount;
        modified_ = true;
    }
    
    void setTelescopeConfig(const TelescopeConfig& config) {
        json telescope;
        telescope["focal_length"] = config.focal_length;
        telescope["aperture"] = config.aperture;
        telescope["tube_length"] = config.tube_length;
        telescope["camera_model"] = config.camera_model;
        telescope["pixel_size"] = config.pixel_size;
        telescope["sensor_width"] = config.sensor_width;
        telescope["sensor_height"] = config.sensor_height;
        
        config_["telescope"] = telescope;
        modified_ = true;
    }
    
    void setGuiderConfig(const GuiderConfig& config) {
        json guider;
        guider["enabled"] = config.enabled;
        guider["connection_string"] = config.connection_string;
        guider["max_correction"] = config.max_correction;
        guider["aggression"] = config.aggression;
        guider["exposure_time_ms"] = config.exposure_time_ms;
        guider["binning"] = config.binning;
        
        config_["guider"] = guider;
        modified_ = true;
    }
    
    void setKalmanConfig(const KalmanConfig& config) {
        json kalman;
        kalman["process_noise"] = config.process_noise;
        kalman["measurement_noise"] = config.measurement_noise;
        kalman["adaptive_q"] = config.adaptive_q;
        kalman["adaptive_r"] = config.adaptive_r;
        kalman["innovation_threshold"] = config.innovation_threshold;
        kalman["max_iterations"] = config.max_iterations;
        
        config_["kalman"] = kalman;
        modified_ = true;
    }
    
    void setTPointConfig(const TPointConfig& config) {
        json tpoint;
        tpoint["enabled_terms"] = config.enabled_terms;
        tpoint["min_measurements"] = config.min_measurements;
        tpoint["max_residual"] = config.max_residual;
        tpoint["auto_calibrate"] = config.auto_calibrate;
        
        config_["tpoint"] = tpoint;
        modified_ = true;
    }
    
    DerotatorConfig getDerotatorConfig() const {
        DerotatorConfig config;
        auto derotator = config_.value("derotator", json::object());
        
        // Map string type to enum
        std::string type_str = derotator.value("type", "STEPPER");
        if (type_str == "CANOPEN") config.type = DerotatorConfig::CANOPEN;
        else if (type_str == "STEPPER") config.type = DerotatorConfig::STEPPER;
        else if (type_str == "SERVO") config.type = DerotatorConfig::SERVO;
        else if (type_str == "CUSTOM") config.type = DerotatorConfig::CUSTOM;
        else config.type = DerotatorConfig::STEPPER;
        
        config.enabled = derotator.value("enabled", false);
        config.connection_string = derotator.value("connection_string", "");
        config.gear_ratio = derotator.value("gear_ratio", 180.0);
        config.max_speed = derotator.value("max_speed", 5.0);
        config.max_acceleration = derotator.value("max_acceleration", 2.0);
        config.backlash = derotator.value("backlash", 15.0);
        config.absolute_encoder = derotator.value("absolute_encoder", false);
        config.encoder_resolution = derotator.value("encoder_resolution", 4096.0);
        config.homing_offset = derotator.value("homing_offset", 0.0);
        
        // Load calibration table
        config.calibration_table.clear();
        auto calib_array = derotator.value("calibration_table", json::array());
        for (const auto& val : calib_array) {
            if (val.is_number()) {
                config.calibration_table.push_back(val.get<double>());
            }
        }
        
        return config;
    }
    
    FieldRotationParams getFieldRotationParams() const {
        FieldRotationParams config;
        auto field_rotation = config_.value("field_rotation", json::object());
        
        config.enabled = field_rotation.value("enabled", false);
        config.latitude = field_rotation.value("latitude", 52.0);
        config.altitude = field_rotation.value("altitude", 100.0);
        config.azimuth = field_rotation.value("azimuth", 0.0);
        config.computed_rate = field_rotation.value("computed_rate", 0.0);
        config.applied_correction = field_rotation.value("applied_correction", 0.0);
        config.temperature = field_rotation.value("temperature", 15.0);
        config.flexure_correction = field_rotation.value("flexure_correction", 0.0);
        
        return config;
    }
    
    void setDerotatorConfig(const DerotatorConfig& config) {
        json derotator;
        
        // Map enum to string type
        std::string type_str;
        switch (config.type) {
            case DerotatorConfig::CANOPEN: type_str = "CANOPEN"; break;
            case DerotatorConfig::STEPPER: type_str = "STEPPER"; break;
            case DerotatorConfig::SERVO: type_str = "SERVO"; break;
            case DerotatorConfig::CUSTOM: type_str = "CUSTOM"; break;
            default: type_str = "STEPPER";
        }
        derotator["type"] = type_str;
        derotator["connection_string"] = config.connection_string;
        derotator["gear_ratio"] = config.gear_ratio;
        derotator["max_speed"] = config.max_speed;
        derotator["max_acceleration"] = config.max_acceleration;
        derotator["backlash"] = config.backlash;
        derotator["absolute_encoder"] = config.absolute_encoder;
        derotator["encoder_resolution"] = config.encoder_resolution;
        derotator["homing_offset"] = config.homing_offset;
        
        // Save calibration table
        json calib_array = json::array();
        for (const auto& val : config.calibration_table) {
            calib_array.push_back(val);
        }
        derotator["calibration_table"] = calib_array;
        
        config_["derotator"] = derotator;
        modified_ = true;
    }
    
    void setFieldRotationParams(const FieldRotationParams& config) {
        json field_rotation;
        field_rotation["enabled"] = config.enabled;
        field_rotation["latitude"] = config.latitude;
        field_rotation["altitude"] = config.altitude;
        field_rotation["azimuth"] = config.azimuth;
        field_rotation["computed_rate"] = config.computed_rate;
        field_rotation["applied_correction"] = config.applied_correction;
        field_rotation["temperature"] = config.temperature;
        field_rotation["flexure_correction"] = config.flexure_correction;
        
        config_["field_rotation"] = field_rotation;
        modified_ = true;
    }
    
    // Delegate to hal::HALConfig for JSON serialization
    astro_mount::hal::HALConfig getHALConfig() const {
        auto hal_json = config_.value("hal", json::object());
        return astro_mount::hal::HALConfig::fromJson(hal_json);
    }
    
    void setHALConfig(const astro_mount::hal::HALConfig& config) {
        config_["hal"] = config.toJson();
        modified_ = true;
    }
    
    std::string getValue(const std::string& path) const {
        try {
            json current = config_;
            std::istringstream path_stream(path);
            std::string segment;
            
            while (std::getline(path_stream, segment, '.')) {
                if (current.is_object() && current.contains(segment)) {
                    current = current[segment];
                } else {
                    return "";
                }
            }
            
            if (current.is_string()) {
                return current.get<std::string>();
            } else {
                return current.dump();
            }
        } catch (const std::exception&) {
            return "";
        }
    }
    
    bool setValue(const std::string& path, const std::string& value) {
        try {
            json* current = &config_;
            std::istringstream path_stream(path);
            std::string segment;
            std::vector<std::string> segments;
            
            while (std::getline(path_stream, segment, '.')) {
                segments.push_back(segment);
            }
            
            // Navigate to parent
            for (size_t i = 0; i < segments.size() - 1; ++i) {
                if (!current->contains(segments[i])) {
                    (*current)[segments[i]] = json::object();
                }
                current = &(*current)[segments[i]];
            }
            
            // Try to parse value as JSON, otherwise treat as string
            try {
                (*current)[segments.back()] = json::parse(value);
            } catch (const std::exception&) {
                (*current)[segments.back()] = value;
            }
            
            modified_ = true;
            return true;
        } catch (const std::exception&) {
            return false;
        }
    }
    
    bool isModified() const {
        return modified_;
    }
    
    void resetModified() {
        modified_ = false;
    }
    
    void merge(const json& other, bool overwrite = true) {
        if (overwrite) {
            config_.update(other);
        } else {
            // Only add missing keys
            for (auto& [key, value] : other.items()) {
                if (!config_.contains(key)) {
                    config_[key] = value;
                }
            }
        }
        modified_ = true;
    }
    
    const json& getJson() const {
        return config_;
    }
    
private:
    void initializeDefaults() {
        // Set default configuration
        config_ = json::object();
        
        LoggingConfig logging_default;
        logging_default.level = "INFO";
        logging_default.directory = "/var/log/astro-mount";
        logging_default.rotation_days = 7;
        logging_default.max_file_size_mb = 100;
        logging_default.console_output = true;
        setLoggingConfig(logging_default);
        
        NetworkConfig network_default;
        network_default.grpc_address = "0.0.0.0";
        network_default.grpc_port = 50051;
        network_default.max_connections = 10;
        network_default.enable_ssl = false;
        network_default.ssl_cert_path = "";
        network_default.ssl_key_path = "";
        setNetworkConfig(network_default);
        
        CanOpenConfig canopen_default;
        canopen_default.interface = "can0";
        canopen_default.node_id = 1;
        canopen_default.baud_rate = 1000000;
        canopen_default.enable_sync = true;
        canopen_default.sync_interval_ms = 100;
        setCanOpenConfig(canopen_default);
        
        MountConfig mount_default;
        mount_default.type = "equatorial";
        mount_default.latitude = 52.0;
        mount_default.longitude = 21.0;
        mount_default.altitude = 100.0;
        mount_default.max_slew_rate = 5.0;
        mount_default.max_tracking_rate = 0.004178;
        mount_default.slew_acceleration = 1.0;
        mount_default.tracking_acceleration = 0.001;
        
        // Additional mount parameters
        mount_default.mount_height = 1.5;
        mount_default.pier_west = 0.0;
        mount_default.pier_east = 0.0;
        mount_default.default_temperature = 15.0;
        mount_default.default_pressure = 1013.25;
        mount_default.default_humidity = 0.5;
        mount_default.use_encoders = true;
        mount_default.encoders_absolute = true;
        mount_default.encoder_resolution = 16384.0;
        
        // Position/rate tolerance
        mount_default.position_tolerance = 0.1;
        mount_default.rate_tolerance = 0.01;
        
        // Meridian flip
        mount_default.meridian_flip_enabled = true;
        mount_default.meridian_flip_delay_minutes = 5.0;
        mount_default.meridian_flip_hysteresis_degrees = 0.5;
        mount_default.meridian_flip_timeout_seconds = 120.0;
        
        // Soft limits
        mount_default.soft_limits_enabled = true;
        mount_default.soft_limit_axis1_min = -270.0;
        mount_default.soft_limit_axis1_max = 270.0;
        mount_default.soft_limit_axis2_min = -5.0;
        mount_default.soft_limit_axis2_max = 185.0;
        mount_default.soft_limit_warning_degrees = 10.0;
        mount_default.soft_limit_deceleration_degrees = 5.0;
        mount_default.soft_limit_tracking_rate_factor = 0.1;
        
        // Park position
        mount_default.park_position_axis1 = 0.0;
        mount_default.park_position_axis2 = 90.0;
        
        // Refraction correction
        mount_default.enable_refraction_correction = true;
        
        // HA axis physical parameters
        mount_default.ha_axis_params.motor_steps_per_rev = 200.0;
        mount_default.ha_axis_params.motor_microstepping = 64.0;
        mount_default.ha_axis_params.motor_step_angle = 101.25;
        mount_default.ha_axis_params.encoder_resolution = 16384.0;
        mount_default.ha_axis_params.encoder_counts_per_arcsec = 0.0126;
        mount_default.ha_axis_params.encoder_quantization_error = 39.6;
        mount_default.ha_axis_params.gear_ratio = 360.0;
        mount_default.ha_axis_params.worm_ratio = 180.0;
        mount_default.ha_axis_params.worm_teeth = 1;
        mount_default.ha_axis_params.worm_wheel_teeth = 180;
        mount_default.ha_axis_params.cyclic_error_amplitude = 15.2;
        mount_default.ha_axis_params.cyclic_error_period = 360.0;
        mount_default.ha_axis_params.cyclic_harmonics = {10.5, 0.0, 3.2, 1.5708, 1.1, 3.1416, 0.5, 4.7124};
        mount_default.ha_axis_params.backlash = 8.5;
        mount_default.ha_axis_params.backlash_temp_coeff = 0.02;
        mount_default.ha_axis_params.axis_stiffness = 0.5;
        mount_default.ha_axis_params.torsional_compliance = 1e-6;
        mount_default.ha_axis_params.expansion_coeff = 11.0e-6;
        mount_default.ha_axis_params.temp_gear_error_coeff = 0.05;
        mount_default.ha_axis_params.calibration_temp = 20.0;
        
        // Dec axis physical parameters
        mount_default.dec_axis_params.motor_steps_per_rev = 200.0;
        mount_default.dec_axis_params.motor_microstepping = 64.0;
        mount_default.dec_axis_params.motor_step_angle = 101.25;
        mount_default.dec_axis_params.encoder_resolution = 16384.0;
        mount_default.dec_axis_params.encoder_counts_per_arcsec = 0.0126;
        mount_default.dec_axis_params.encoder_quantization_error = 39.6;
        mount_default.dec_axis_params.gear_ratio = 360.0;
        mount_default.dec_axis_params.worm_ratio = 180.0;
        mount_default.dec_axis_params.worm_teeth = 1;
        mount_default.dec_axis_params.worm_wheel_teeth = 180;
        mount_default.dec_axis_params.cyclic_error_amplitude = 12.8;
        mount_default.dec_axis_params.cyclic_error_period = 360.0;
        mount_default.dec_axis_params.cyclic_harmonics = {8.2, 0.0, 2.5, 1.5708, 0.8, 3.1416, 0.3, 4.7124};
        mount_default.dec_axis_params.backlash = 6.3;
        mount_default.dec_axis_params.backlash_temp_coeff = 0.015;
        mount_default.dec_axis_params.axis_stiffness = 0.6;
        mount_default.dec_axis_params.torsional_compliance = 1.2e-6;
        mount_default.dec_axis_params.expansion_coeff = 11.0e-6;
        mount_default.dec_axis_params.temp_gear_error_coeff = 0.04;
        mount_default.dec_axis_params.calibration_temp = 20.0;
        
        setMountConfig(mount_default);
        
        TelescopeConfig telescope_default;
        telescope_default.focal_length = 2000.0;
        telescope_default.aperture = 200.0;
        telescope_default.tube_length = 1800.0;
        telescope_default.camera_model = "ASI1600";
        telescope_default.pixel_size = 3.8;
        telescope_default.sensor_width = 4656;
        telescope_default.sensor_height = 3520;
        setTelescopeConfig(telescope_default);
        
        GuiderConfig guider_default;
        guider_default.enabled = false;
        guider_default.connection_string = "";
        guider_default.max_correction = 10.0;
        guider_default.aggression = 0.5;
        guider_default.exposure_time_ms = 2000;
        guider_default.binning = 2;
        setGuiderConfig(guider_default);
        
        KalmanConfig kalman_default;
        kalman_default.process_noise = 0.01;
        kalman_default.measurement_noise = 1.0;
        kalman_default.adaptive_q = true;
        kalman_default.adaptive_r = false;
        kalman_default.innovation_threshold = 3.0;
        kalman_default.max_iterations = 10;
        setKalmanConfig(kalman_default);
        
        TPointConfig tpoint_default;
        tpoint_default.enabled_terms = 0xFFFF;
        tpoint_default.min_measurements = 10;
        tpoint_default.max_residual = 30.0;
        tpoint_default.auto_calibrate = true;
        setTPointConfig(tpoint_default);
        
        modified_ = false;
    }
    
    bool isValidLogLevel(const std::string& level) const {
        static const std::vector<std::string> valid_levels = {
            "TRACE", "DEBUG", "INFO", "WARN", "ERROR", "FATAL"
        };
        
        std::string upper_level = level;
        std::transform(upper_level.begin(), upper_level.end(), 
                      upper_level.begin(), ::toupper);
        
        return std::find(valid_levels.begin(), valid_levels.end(), 
                        upper_level) != valid_levels.end();
    }
    
    json config_;
    bool modified_;
};

// Public interface implementation
Configuration::Configuration() : pimpl(std::make_unique<Impl>()) {}

Configuration::Configuration(const Configuration& other) 
    : pimpl(std::make_unique<Impl>(*other.pimpl)) {}

Configuration::Configuration(Configuration&& other) noexcept = default;

Configuration::~Configuration() = default;

Configuration& Configuration::operator=(const Configuration& other) {
    if (this != &other) {
        pimpl = std::make_unique<Impl>(*other.pimpl);
    }
    return *this;
}

Configuration& Configuration::operator=(Configuration&& other) noexcept = default;

bool Configuration::loadFromFile(const std::string& filename) {
    return pimpl->loadFromFile(filename);
}

bool Configuration::saveToFile(const std::string& filename) const {
    return pimpl->saveToFile(filename);
}

bool Configuration::loadFromString(const std::string& json_str) {
    return pimpl->loadFromString(json_str);
}

std::string Configuration::toString() const {
    return pimpl->toString();
}

std::vector<std::string> Configuration::validate() const {
    return pimpl->validate();
}

Configuration::LoggingConfig Configuration::getLoggingConfig() const {
    return pimpl->getLoggingConfig();
}

Configuration::NetworkConfig Configuration::getNetworkConfig() const {
    return pimpl->getNetworkConfig();
}

Configuration::CanOpenConfig Configuration::getCanOpenConfig() const {
    return pimpl->getCanOpenConfig();
}

Configuration::MountConfig Configuration::getMountConfig() const {
    return pimpl->getMountConfig();
}

Configuration::TelescopeConfig Configuration::getTelescopeConfig() const {
    return pimpl->getTelescopeConfig();
}

Configuration::GuiderConfig Configuration::getGuiderConfig() const {
    return pimpl->getGuiderConfig();
}

Configuration::KalmanConfig Configuration::getKalmanConfig() const {
    return pimpl->getKalmanConfig();
}

Configuration::TPointConfig Configuration::getTPointConfig() const {
    return pimpl->getTPointConfig();
}

Configuration::DerotatorConfig Configuration::getDerotatorConfig() const {
    return pimpl->getDerotatorConfig();
}

Configuration::FieldRotationParams Configuration::getFieldRotationParams() const {
    return pimpl->getFieldRotationParams();
}

astro_mount::hal::HALConfig Configuration::getHALConfig() const {
    return pimpl->getHALConfig();
}

void Configuration::setLoggingConfig(const LoggingConfig& config) {
    pimpl->setLoggingConfig(config);
}

void Configuration::setNetworkConfig(const NetworkConfig& config) {
    pimpl->setNetworkConfig(config);
}

void Configuration::setCanOpenConfig(const CanOpenConfig& config) {
    pimpl->setCanOpenConfig(config);
}

void Configuration::setMountConfig(const MountConfig& config) {
    pimpl->setMountConfig(config);
}

void Configuration::setTelescopeConfig(const TelescopeConfig& config) {
    pimpl->setTelescopeConfig(config);
}

void Configuration::setGuiderConfig(const GuiderConfig& config) {
    pimpl->setGuiderConfig(config);
}

void Configuration::setKalmanConfig(const KalmanConfig& config) {
    pimpl->setKalmanConfig(config);
}

void Configuration::setTPointConfig(const TPointConfig& config) {
    pimpl->setTPointConfig(config);
}

void Configuration::setDerotatorConfig(const DerotatorConfig& config) {
    pimpl->setDerotatorConfig(config);
}

void Configuration::setFieldRotationParams(const FieldRotationParams& config) {
    pimpl->setFieldRotationParams(config);
}

void Configuration::setHALConfig(const astro_mount::hal::HALConfig& config) {
    pimpl->setHALConfig(config);
}

Configuration Configuration::getDefault() {
    Configuration config;
    // Already initialized with defaults
    return config;
}

void Configuration::merge(const Configuration& other, bool overwrite) {
    pimpl->merge(other.pimpl->getJson(), overwrite);
}

std::string Configuration::getValue(const std::string& path) const {
    return pimpl->getValue(path);
}

bool Configuration::setValue(const std::string& path, const std::string& value) {
    return pimpl->setValue(path, value);
}

bool Configuration::isModified() const {
    return pimpl->isModified();
}

void Configuration::resetModified() {
    pimpl->resetModified();
}

} // namespace config
} // namespace astro_mount
