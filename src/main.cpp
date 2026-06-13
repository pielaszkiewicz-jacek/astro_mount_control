#include <iostream>
#include <memory>
#include <chrono>
#include <thread>
#include <signal.h>
#include <atomic>

#include "config/configuration.h"
#include "logging/logger.h"
#include "controllers/mount_controller.h"
#include "api/grpc_server.h"
#include "proto/mount_controller.pb.h"

using namespace astro_mount;

// Global variables for signal handling
std::unique_ptr<controllers::MountController> mount_controller;
std::unique_ptr<api::GrpcServer> grpc_server_instance;
std::atomic<bool> running{true};

void signal_handler(int signal) {
    auto logger = logging::Logger::get("main");
    logger->info("Received signal {}, shutting down...", signal);
    running = false;
}

int main(int argc, char* argv[]) {
    // Set up signal handling
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    try {
        // Load configuration
        config::Configuration config;
        std::string config_file = "config/default.json";
        
        if (argc > 1) {
            config_file = argv[1];
        }
        
        if (!config.loadFromFile(config_file)) {
            std::cerr << "Failed to load configuration from " << config_file << std::endl;
            std::cerr << "Using default configuration" << std::endl;
            config = config::Configuration::getDefault();
        }
        
        // Initialize logging
        auto logging_config = config.getLoggingConfig();
        logging::Logger::initProgrammatic(
            logging_config.directory,
            100, 10, true, false,
            (logging_config.level == "trace" ? logging::LogLevel::TRACE :
             logging_config.level == "debug" ? logging::LogLevel::DEBUG :
             logging_config.level == "warn" ? logging::LogLevel::WARN :
             logging_config.level == "error" ? logging::LogLevel::ERROR :
             logging_config.level == "critical" ? logging::LogLevel::CRITICAL :
             logging::LogLevel::INFO)
        );
        auto logger = logging::Logger::get("main");
        
        logger->info("Starting Astronomical Mount Controller");
        logger->info("Configuration loaded from: {}", config_file);
        
        // Create mount controller
        mount_controller = std::make_unique<controllers::MountController>();
        
        // Convert configuration to controller config
        controllers::MountController::ControllerConfig controller_config;
        auto mount_config = config.getMountConfig();
        
        controller_config.mount_type = (mount_config.type == "equatorial") ? 
            controllers::MountController::MountType::EQUATORIAL : 
            controllers::MountController::MountType::ALT_AZ;
        
        controller_config.latitude = mount_config.latitude;
        controller_config.longitude = mount_config.longitude;
        controller_config.altitude = mount_config.altitude;
        controller_config.ha_axis_params.gear_ratio = mount_config.ha_axis_params.gear_ratio;
        controller_config.dec_axis_params.gear_ratio = mount_config.dec_axis_params.gear_ratio;
        controller_config.max_slew_rate = mount_config.max_slew_rate;
        controller_config.max_tracking_rate = mount_config.max_tracking_rate;
        controller_config.slew_acceleration = mount_config.slew_acceleration;
        controller_config.tracking_acceleration = mount_config.tracking_acceleration;
        
        // Set mount physical parameters
        controller_config.mount_height = mount_config.mount_height;
        controller_config.pier_west = mount_config.pier_west;
        controller_config.pier_east = mount_config.pier_east;
        controller_config.default_temperature = mount_config.default_temperature;
        controller_config.default_pressure = mount_config.default_pressure;
        controller_config.default_humidity = mount_config.default_humidity;
        
        // Set position/rate tolerance from config
        controller_config.position_tolerance = mount_config.position_tolerance;
        controller_config.rate_tolerance = mount_config.rate_tolerance;
        
        // Set up encoder configuration
        controller_config.use_encoders = mount_config.use_encoders;
        controller_config.encoders_absolute = mount_config.encoders_absolute;
        controller_config.encoder_resolution = mount_config.encoder_resolution;
        
        // Set Kalman filter parameters
        auto kalman_config = config.getKalmanConfig();
        controller_config.process_noise = kalman_config.process_noise;
        controller_config.measurement_noise = kalman_config.measurement_noise;
        
        // Set TPOINT parameters
        auto tpoint_config = config.getTPointConfig();
        controller_config.tpoint_enabled_terms = tpoint_config.enabled_terms;
        
        // Set CanOpen configuration
        auto canopen_config = config.getCanOpenConfig();
        controller_config.canopen_interface = canopen_config.interface;
        controller_config.canopen_node_id = canopen_config.node_id;
        controller_config.canopen_position_counts_per_degree = canopen_config.position_counts_per_degree;
        controller_config.canopen_velocity_counts_per_deg_s = canopen_config.velocity_counts_per_deg_s;
        
        // Set network configuration
        auto network_config = config.getNetworkConfig();
        controller_config.grpc_address = network_config.grpc_address;
        controller_config.grpc_port = network_config.grpc_port;
        
        // Set guider configuration
        auto guider_config = config.getGuiderConfig();
        controller_config.enable_guider = guider_config.enabled;
        controller_config.guider_max_correction = guider_config.max_correction;
        controller_config.guider_aggression = guider_config.aggression;
        
        // Set meridian flip configuration from config
        controller_config.meridian_flip_enabled = mount_config.meridian_flip_enabled;
        controller_config.meridian_flip_delay_minutes = mount_config.meridian_flip_delay_minutes;
        controller_config.meridian_flip_hysteresis_degrees = mount_config.meridian_flip_hysteresis_degrees;
        controller_config.meridian_flip_timeout_seconds = mount_config.meridian_flip_timeout_seconds;
        
        // Set soft limits configuration from config
        controller_config.soft_limits_enabled = mount_config.soft_limits_enabled;
        controller_config.soft_limit_axis1_min = mount_config.soft_limit_axis1_min;
        controller_config.soft_limit_axis1_max = mount_config.soft_limit_axis1_max;
        controller_config.soft_limit_axis2_min = mount_config.soft_limit_axis2_min;
        controller_config.soft_limit_axis2_max = mount_config.soft_limit_axis2_max;
        controller_config.soft_limit_warning_degrees = mount_config.soft_limit_warning_degrees;
        controller_config.soft_limit_deceleration_degrees = mount_config.soft_limit_deceleration_degrees;
        controller_config.soft_limit_tracking_rate_factor = mount_config.soft_limit_tracking_rate_factor;
        
        // Set park position from config
        controller_config.park_position_axis1 = mount_config.park_position_axis1;
        controller_config.park_position_axis2 = mount_config.park_position_axis2;
        
        // Set refraction correction from config
        controller_config.enable_refraction_correction = mount_config.enable_refraction_correction;
        
        // Set axis physical parameters (copy field by field)
        auto& ha = mount_config.ha_axis_params;
        controller_config.ha_axis_params.motor_steps_per_rev = ha.motor_steps_per_rev;
        controller_config.ha_axis_params.motor_microstepping = ha.motor_microstepping;
        controller_config.ha_axis_params.motor_step_angle = ha.motor_step_angle;
        controller_config.ha_axis_params.encoder_resolution = ha.encoder_resolution;
        controller_config.ha_axis_params.encoder_counts_per_arcsec = ha.encoder_counts_per_arcsec;
        controller_config.ha_axis_params.encoder_quantization_error = ha.encoder_quantization_error;
        controller_config.ha_axis_params.gear_ratio = ha.gear_ratio;
        controller_config.ha_axis_params.worm_ratio = ha.worm_ratio;
        controller_config.ha_axis_params.worm_teeth = ha.worm_teeth;
        controller_config.ha_axis_params.worm_wheel_teeth = ha.worm_wheel_teeth;
        controller_config.ha_axis_params.cyclic_error_amplitude = ha.cyclic_error_amplitude;
        controller_config.ha_axis_params.cyclic_error_period = ha.cyclic_error_period;
        controller_config.ha_axis_params.cyclic_harmonics = ha.cyclic_harmonics;
        controller_config.ha_axis_params.backlash = ha.backlash;
        controller_config.ha_axis_params.backlash_temp_coeff = ha.backlash_temp_coeff;
        controller_config.ha_axis_params.axis_stiffness = ha.axis_stiffness;
        controller_config.ha_axis_params.torsional_compliance = ha.torsional_compliance;
        controller_config.ha_axis_params.expansion_coeff = ha.expansion_coeff;
        controller_config.ha_axis_params.temp_gear_error_coeff = ha.temp_gear_error_coeff;
        controller_config.ha_axis_params.calibration_table = ha.calibration_table;
        controller_config.ha_axis_params.calibration_temp = ha.calibration_temp;
        
        auto& dec = mount_config.dec_axis_params;
        controller_config.dec_axis_params.motor_steps_per_rev = dec.motor_steps_per_rev;
        controller_config.dec_axis_params.motor_microstepping = dec.motor_microstepping;
        controller_config.dec_axis_params.motor_step_angle = dec.motor_step_angle;
        controller_config.dec_axis_params.encoder_resolution = dec.encoder_resolution;
        controller_config.dec_axis_params.encoder_counts_per_arcsec = dec.encoder_counts_per_arcsec;
        controller_config.dec_axis_params.encoder_quantization_error = dec.encoder_quantization_error;
        controller_config.dec_axis_params.gear_ratio = dec.gear_ratio;
        controller_config.dec_axis_params.worm_ratio = dec.worm_ratio;
        controller_config.dec_axis_params.worm_teeth = dec.worm_teeth;
        controller_config.dec_axis_params.worm_wheel_teeth = dec.worm_wheel_teeth;
        controller_config.dec_axis_params.cyclic_error_amplitude = dec.cyclic_error_amplitude;
        controller_config.dec_axis_params.cyclic_error_period = dec.cyclic_error_period;
        controller_config.dec_axis_params.cyclic_harmonics = dec.cyclic_harmonics;
        controller_config.dec_axis_params.backlash = dec.backlash;
        controller_config.dec_axis_params.backlash_temp_coeff = dec.backlash_temp_coeff;
        controller_config.dec_axis_params.axis_stiffness = dec.axis_stiffness;
        controller_config.dec_axis_params.torsional_compliance = dec.torsional_compliance;
        controller_config.dec_axis_params.expansion_coeff = dec.expansion_coeff;
        controller_config.dec_axis_params.temp_gear_error_coeff = dec.temp_gear_error_coeff;
        controller_config.dec_axis_params.calibration_table = dec.calibration_table;
        controller_config.dec_axis_params.calibration_temp = dec.calibration_temp;
        
        // Initialize mount controller
        if (!mount_controller->initialize(controller_config)) {
            logger->error("Failed to initialize mount controller");
            return 1;
        }
        
        logger->info("Mount controller initialized successfully");
        
        // Configure derotator from config file (if derotator section exists and is enabled)
        {
            auto derotator_cfg = config.getDerotatorConfig();
            if (derotator_cfg.enabled) {
                ::astro_mount::DerotatorConfig proto_config;
                proto_config.set_type(static_cast<::astro_mount::DerotatorConfig::DerotatorType>(
                    derotator_cfg.type));
                proto_config.set_connection_string(derotator_cfg.connection_string);
                proto_config.set_gear_ratio(derotator_cfg.gear_ratio);
                proto_config.set_max_speed(derotator_cfg.max_speed);
                proto_config.set_max_acceleration(derotator_cfg.max_acceleration);
                proto_config.set_backlash(derotator_cfg.backlash);
                proto_config.set_absolute_encoder(derotator_cfg.absolute_encoder);
                proto_config.set_encoder_resolution(derotator_cfg.encoder_resolution);
                proto_config.set_homing_offset(derotator_cfg.homing_offset);
                for (const auto& val : derotator_cfg.calibration_table) {
                    proto_config.add_calibration_table(val);
                }
                if (!mount_controller->configureDerotator(proto_config)) {
                    logger->warn("Failed to configure derotator from config");
                } else {
                    logger->info("Derotator configured from config file");
                }
            } else {
                logger->info("Derotator disabled in config");
            }
        }
        
        // Configure field rotation from config file (if field_rotation section exists)
        {
            auto fr_params = config.getFieldRotationParams();
            ::astro_mount::FieldRotationParams proto_params;
            proto_params.set_enabled(fr_params.enabled);
            proto_params.set_latitude(fr_params.latitude);
            proto_params.set_altitude(fr_params.altitude);
            proto_params.set_azimuth(fr_params.azimuth);
            proto_params.set_computed_rate(fr_params.computed_rate);
            proto_params.set_applied_correction(fr_params.applied_correction);
            proto_params.set_temperature(fr_params.temperature);
            proto_params.set_flexure_correction(fr_params.flexure_correction);
            if (!mount_controller->enableFieldRotation(proto_params)) {
                logger->warn("Failed to enable field rotation from config");
            } else {
                logger->info("Field rotation configured from config file");
            }
        }
        
        // Create and start gRPC server
        grpc_server_instance = std::make_unique<api::GrpcServer>(
            network_config.grpc_address, 
            network_config.grpc_port,
            *mount_controller
        );
        
        if (!grpc_server_instance->start()) {
            logger->error("Failed to start gRPC server");
            return 1;
        }
        
        logger->info("gRPC server started on {}:{}", 
                     network_config.grpc_address, network_config.grpc_port);
        
        // Main loop
        logger->info("Entering main loop");
        
        while (running) {
            // Refresh live axis positions from CANopen drives before
            // reporting status.  Without this, positions would only be
            // updated during active slew/track operations.
            mount_controller->refreshPositions();

            // Get current status
            auto status = mount_controller->getStatus();
            
            // Log status periodically
            static auto last_log = std::chrono::steady_clock::now();
            auto now = std::chrono::steady_clock::now();
            
            if (std::chrono::duration_cast<std::chrono::seconds>(now - last_log).count() >= 10) {
                logger->info("Mount status: {}, Servo: axis1={:.2f}° axis2={:.2f}° | Telescope: axis1={:.4f}° axis2={:.4f}°",
                    static_cast<int>(status.state),
                    status.axis1_position, status.axis2_position,
                    status.telescope_axis1_position, status.telescope_axis2_position);
                last_log = now;
            }
            
            // Check for errors
            if (status.state == controllers::MountController::MountStatus::State::ERROR) {
                logger->error("Mount error: {}", status.error_message);
            }
            
            // Sleep for a bit
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        
        // Shutdown
        logger->info("Shutting down...");
        
        grpc_server_instance->stop();
        grpc_server_instance.reset();  // Release before global destruction to prevent double-free
        
        mount_controller->shutdown();
        mount_controller.reset();      // Release before global destruction to prevent double-free
        
        logger->info("Shutdown complete");
        
    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}
