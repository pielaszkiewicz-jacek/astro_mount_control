#include <iostream>
#include <memory>
#include <chrono>
#include <thread>
#include <signal.h>
#include <atomic>

#include "config/configuration.h"
#include "config/mount_config.h"
#include "config/tracking_config.h"
#include "config/safety_config.h"
#include "config/calibration_config.h"
#include "logging/logger.h"
#include "controllers/mount_controller.h"
#include "api/grpc_server.h"
#include "proto/mount_controller.pb.h"

// External service gRPC stubs (weather, power remain external processes).
// Dome, derotator and focuser are hosted in-process inside the mount controller.
#include <grpcpp/grpcpp.h>
#include "dome/include/dome_service_impl.h"
#include "derotator/include/derotator_service_impl.h"
#include "focuser/include/focuser_service_impl.h"
#include "proto/weather.grpc.pb.h"
#include "proto/power.grpc.pb.h"
#include "controllers/weather_client.h"

using namespace astro_mount;

// Global variables for signal handling
std::unique_ptr<controllers::MountController> mount_controller;
std::unique_ptr<api::GrpcServer> grpc_server_instance;
std::atomic<bool> running{true};

// In-process service implementations (dome, derotator, focuser) — config-gated,
// null when disabled. Registered on the mount controller's gRPC server so
// existing clients (web proxy, ASCOM/INDI, GUI) keep working unchanged.
std::unique_ptr<astro_dome::DomeServiceImpl> dome_service_impl;
std::unique_ptr<astro_derotator::DerotatorServiceImpl> derotator_service_impl;
std::unique_ptr<astro_focuser::FocuserServiceImpl> focuser_service_impl;

// External service gRPC stubs (weather, power remain external processes)
std::unique_ptr<controllers::WeatherClient> weather_client;
std::unique_ptr<PowerService::Stub> power_stub;

// Integration config
config::Configuration::ExternalIntegrationConfig ext_config;

void signal_handler(int /*signal*/) {
    // Only set the atomic flag — calling non-async-signal-safe functions
    // (like Logger, malloc, mutex lock) from a signal handler is UB and
    // can cause deadlocks.
    running = false;
}

/// Convert Configuration::AxisPhysicalParameters → config::AxisPhysicalParameters
static config::AxisPhysicalParameters convertAxisParams(
    const config::Configuration::AxisPhysicalParameters& src) {
    config::AxisPhysicalParameters dst;
    dst.position_counts_per_degree = src.position_counts_per_degree;
    dst.velocity_counts_per_deg_s = src.velocity_counts_per_deg_s;
    dst.encoder_resolution = src.encoder_resolution;
    dst.encoder_counts_per_arcsec = src.encoder_counts_per_arcsec;
    dst.encoder_quantization_error = src.encoder_quantization_error;
    dst.gear_ratio = src.gear_ratio;
    dst.worm_ratio = src.worm_ratio;
    dst.worm_teeth = src.worm_teeth;
    dst.worm_wheel_teeth = src.worm_wheel_teeth;
    dst.cyclic_error_amplitude = src.cyclic_error_amplitude;
    dst.cyclic_error_period = src.cyclic_error_period;
    dst.cyclic_harmonics = src.cyclic_harmonics;
    dst.backlash = src.backlash;
    dst.backlash_temp_coeff = src.backlash_temp_coeff;
    dst.axis_stiffness = src.axis_stiffness;
    dst.torsional_compliance = src.torsional_compliance;
    dst.expansion_coeff = src.expansion_coeff;
    dst.temp_gear_error_coeff = src.temp_gear_error_coeff;
    dst.calibration_table = src.calibration_table;
    dst.calibration_temp = src.calibration_temp;
    return dst;
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
        
        // Read external service integration config
        ext_config = config.getExternalIntegrationConfig();
        
        // Create mount controller
        mount_controller = std::make_unique<controllers::MountController>();
        
        // === Convert Configuration to domain-specific sub-configs ===
        controllers::MountController::ControllerConfig controller_config;
        
        auto cfg_mount = config.getMountConfig(); // Configuration::MountConfig
        auto hal_cfg = config.getHALConfig();      // hal::HALConfig
        
        // --- MountConfig ---
        config::MountConfig mount_cfg;
        if (cfg_mount.type == "equatorial")
            mount_cfg.mount_type = config::MountType::EQUATORIAL;
        else if (cfg_mount.type == "casual")
            mount_cfg.mount_type = config::MountType::CASUAL;
        else if (cfg_mount.type == "alt_az" || cfg_mount.type == "altaz")
            mount_cfg.mount_type = config::MountType::ALT_AZ;
        else
            mount_cfg.mount_type = config::MountType::UNKNOWN;
        mount_cfg.latitude = cfg_mount.latitude;
        mount_cfg.longitude = cfg_mount.longitude;
        mount_cfg.altitude = cfg_mount.altitude;
        mount_cfg.max_slew_rate = cfg_mount.max_slew_rate;
        mount_cfg.max_tracking_rate = cfg_mount.max_tracking_rate;
        mount_cfg.slew_acceleration = cfg_mount.slew_acceleration;
        mount_cfg.tracking_acceleration = cfg_mount.tracking_acceleration;
        mount_cfg.position_tolerance = cfg_mount.position_tolerance;
        mount_cfg.rate_tolerance = cfg_mount.rate_tolerance;
        mount_cfg.default_temperature = cfg_mount.default_temperature;
        mount_cfg.default_pressure = cfg_mount.default_pressure;
        mount_cfg.default_humidity = cfg_mount.default_humidity;
        mount_cfg.use_encoders = cfg_mount.use_encoders;
        mount_cfg.encoders_absolute = cfg_mount.encoders_absolute;
        mount_cfg.ha_axis_params = convertAxisParams(cfg_mount.ha_axis_params);
        mount_cfg.dec_axis_params = convertAxisParams(cfg_mount.dec_axis_params);
        controller_config.mount_config = mount_cfg;
        
        // --- TrackingConfig ---
        config::TrackingConfig track_cfg;
        track_cfg.controller_poll_ms = cfg_mount.controller_poll_ms;
        track_cfg.tracking_update_ms = cfg_mount.tracking_update_ms;
        // Guider parameters
        auto guider_cfg = config.getGuiderConfig();
        track_cfg.enable_guider = guider_cfg.enabled;
        track_cfg.guider_max_correction = guider_cfg.max_correction;
        track_cfg.guider_aggression = guider_cfg.aggression;
        controller_config.tracking_config = track_cfg;
        
        // --- SafetyConfig ---
        config::SafetyConfig safety_cfg;
        safety_cfg.soft_limits_enabled = cfg_mount.soft_limits_enabled;
        safety_cfg.soft_limit_axis1_min = cfg_mount.soft_limit_axis1_min;
        safety_cfg.soft_limit_axis1_max = cfg_mount.soft_limit_axis1_max;
        safety_cfg.soft_limit_axis2_min = cfg_mount.soft_limit_axis2_min;
        safety_cfg.soft_limit_axis2_max = cfg_mount.soft_limit_axis2_max;
        safety_cfg.soft_limit_warning_degrees = cfg_mount.soft_limit_warning_degrees;
        safety_cfg.soft_limit_deceleration_degrees = cfg_mount.soft_limit_deceleration_degrees;
        safety_cfg.soft_limit_tracking_rate_factor = cfg_mount.soft_limit_tracking_rate_factor;
        safety_cfg.meridian_flip_enabled = cfg_mount.meridian_flip_enabled;
        safety_cfg.meridian_flip_delay_minutes = cfg_mount.meridian_flip_delay_minutes;
        safety_cfg.meridian_flip_hysteresis_degrees = cfg_mount.meridian_flip_hysteresis_degrees;
        safety_cfg.meridian_flip_timeout_seconds = cfg_mount.meridian_flip_timeout_seconds;
        safety_cfg.park_position_axis1 = cfg_mount.park_position_axis1;
        safety_cfg.park_position_axis2 = cfg_mount.park_position_axis2;
        safety_cfg.enable_refraction_correction = cfg_mount.enable_refraction_correction;
        controller_config.safety_config = safety_cfg;
        
        // --- CalibrationConfig ---
        config::CalibrationConfig calib_cfg;
        auto tpoint_cfg = config.getTPointConfig();
        calib_cfg.tpoint_enabled_terms = tpoint_cfg.enabled_terms;
        controller_config.calibration_config = calib_cfg;
        
        // --- HALConfig ---
        controller_config.hal_config = hal_cfg;
        
        // Populate ControllerConfig with logging settings from config file
        controller_config.log_level = logging_config.level;
        controller_config.log_directory = logging_config.directory;
        controller_config.log_console_output = logging_config.console_output;
        controller_config.log_rotation_days = logging_config.rotation_days;
        controller_config.log_max_file_size_mb = logging_config.max_file_size_mb;

        // Set network configuration
        auto network_config = config.getNetworkConfig();
        controller_config.grpc_address = network_config.grpc_address;
        controller_config.grpc_port = network_config.grpc_port;
        controller_config.network_max_connections = network_config.max_connections;
        controller_config.network_enable_ssl = network_config.enable_ssl;
        controller_config.network_ssl_cert_path = network_config.ssl_cert_path;
        controller_config.network_ssl_key_path = network_config.ssl_key_path;
        
        // Set telescope parameters
        auto telescope_cfg = config.getTelescopeConfig();
        controller_config.focal_length = telescope_cfg.focal_length;
        controller_config.aperture = telescope_cfg.aperture;
        
        // Initialize mount controller
        if (!mount_controller->initialize(controller_config)) {
            logger->error("Failed to initialize mount controller");
            return 1;
        }
        
        logger->info("Mount controller initialized successfully");
        
        // Enable config persistence: changes from the UI will be saved to disk
        mount_controller->setConfigFilePath(config_file);
        
        // Auto-start gamepad loop if configured
        if (controller_config.hal_config.gamepad.autostart) {
            logger->info("Gamepad autostart enabled — starting gamepad loop");
            mount_controller->startGamepadLoop();
        }
        
        // ─── In-process subsystem services (dome, derotator, focuser) ────────
        // These subsystems are hosted inside the mount controller process and
        // served via gRPC on the same unified server (mount controller port).
        // No separate listening ports are used. Config-gated, disabled by
        // default.

        // Dome service (in-process)
        if (ext_config.dome_enabled) {
            try {
                dome_service_impl = std::make_unique<astro_dome::DomeServiceImpl>(
                    "config/dome_config.json");
                logger->info("Dome subsystem hosted in-process (served on unified gRPC port)");
            } catch (const std::exception& e) {
                logger->warn("Failed to initialise in-process dome service: {}", e.what());
            }
        }

        // Derotator service (in-process)
        if (ext_config.derotator_enabled) {
            try {
                derotator_service_impl = std::make_unique<astro_derotator::DerotatorServiceImpl>(
                    "config/derotator_config.json");
                logger->info("Derotator subsystem hosted in-process (served on unified gRPC port)");
            } catch (const std::exception& e) {
                logger->warn("Failed to initialise in-process derotator service: {}", e.what());
            }
        }

        // Focuser service (in-process)
        if (ext_config.focuser_enabled) {
            try {
                focuser_service_impl = std::make_unique<astro_focuser::FocuserServiceImpl>(
                    "config/focuser_config.json");
                logger->info("Focuser subsystem hosted in-process (served on unified gRPC port)");
            } catch (const std::exception& e) {
                logger->warn("Failed to initialise in-process focuser service: {}", e.what());
            }
        }

        // Create and start gRPC server
        grpc_server_instance = std::make_unique<api::GrpcServer>(
            network_config.grpc_address,
            network_config.grpc_port,
            *mount_controller,
            network_config.enable_ssl,
            network_config.ssl_cert_path,
            network_config.ssl_key_path
        );

        // Register in-process subsystem services on the unified gRPC server.
        // All services (mount, dome, derotator, focuser) are served on the
        // single mount controller port (50051) — no separate ports.
        if (dome_service_impl) {
            grpc_server_instance->registerService(dome_service_impl.get());
        }
        if (derotator_service_impl) {
            grpc_server_instance->registerService(derotator_service_impl.get());
        }
        if (focuser_service_impl) {
            grpc_server_instance->registerService(focuser_service_impl.get());
        }

        if (!grpc_server_instance->start()) {
            logger->error("Failed to start gRPC server");
            return 1;
        }
        
        logger->info("gRPC server started on {}:{}",
                     network_config.grpc_address, network_config.grpc_port);
        
        // ─── External service gRPC clients (config-gated) ───────────────────
        // (weather and power remain external processes)
        
        // Weather service integration
        if (ext_config.weather_enabled) {
            try {
                weather_client = std::make_unique<controllers::WeatherClient>(
                    ext_config.weather_address);
                weather_client->start(ext_config.weather_poll_interval_ms,
                    [logger](const std::string& msg) {
                        logger->warn("Weather DANGER: {} — auto-parking mount", msg);
                        if (mount_controller) {
                            mount_controller->park();
                        }
                    });
                logger->info("Weather integration enabled — connected to {}",
                             ext_config.weather_address);
            } catch (const std::exception& e) {
                logger->warn("Failed to create weather client: {}", e.what());
            }
        }
        
        // Power service integration
        if (ext_config.power_enabled) {
            try {
                auto channel = grpc::CreateChannel(
                    ext_config.power_address, grpc::InsecureChannelCredentials());
                power_stub = PowerService::NewStub(channel);
                logger->info("Power integration enabled — connected to {}", ext_config.power_address);

                // Check initial power status
                grpc::ClientContext ctx;
                google::protobuf::Empty req;
                PowerStatus power_status;
                auto status = power_stub->GetPowerStatus(&ctx, req, &power_status);
                if (status.ok()) {
                    logger->info("Power: {:.1f}V, {:.1f}A, {:.0f}% charge",
                        power_status.voltage_v(), power_status.current_a(),
                        power_status.charge_percent());
                    if (power_status.on_battery()) {
                        logger->warn("Power: Running on battery — estimated {} min remaining",
                            power_status.estimated_runtime_min());
                    }
                } else {
                    logger->warn("Power: initial status check failed: {}", status.error_message());
                }
            } catch (const std::exception& e) {
                logger->warn("Failed to create power gRPC stub: {}", e.what());
            }
        }
        
        // ─── Main loop ──────────────────────────────────────────────────────
        logger->info("Entering main loop");
        
        auto last_dome_update = std::chrono::steady_clock::now();
        auto last_derotator_update = std::chrono::steady_clock::now();
        auto last_power_update = std::chrono::steady_clock::now();
        
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
            
            // ── In-process subsystem updates (config-gated) ─────────────────
            
            // Dome: feed mount azimuth directly (no gRPC round-trip)
            if (dome_service_impl && ext_config.dome_enabled) {
                auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                    now - last_dome_update).count();
                if (elapsed >= ext_config.dome_update_interval_ms) {
                    dome_service_impl->setMountAzimuth(status.telescope_axis1_position);
                    last_dome_update = now;
                }
            }
            
            // Derotator: feed mount position directly (no gRPC round-trip)
            if (derotator_service_impl && ext_config.derotator_enabled) {
                auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                    now - last_derotator_update).count();
                if (elapsed >= ext_config.derotator_update_interval_ms) {
                    derotator_service_impl->setMountPosition(
                        status.telescope_axis1_position,
                        status.telescope_axis2_position,
                        mount_cfg.latitude,
                        0.0, // ha_hours — would need LST - RA
                        0.0  // dec_deg
                    );
                    last_derotator_update = now;
                }
            }
            
            // Power: poll status periodically
            if (power_stub && ext_config.power_enabled) {
                auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                    now - last_power_update).count();
                if (elapsed >= ext_config.power_poll_interval_ms) {
                    grpc::ClientContext ctx;
                    google::protobuf::Empty req;
                    PowerStatus power_status;
                    auto grpc_status = power_stub->GetPowerStatus(&ctx, req, &power_status);
                    if (grpc_status.ok()) {
                        if (power_status.on_battery()) {
                            logger->warn("Power on battery: {:.1f}V, {:.0f}%, {} min remaining",
                                power_status.voltage_v(), power_status.charge_percent(),
                                power_status.estimated_runtime_min());
                            if (power_status.charge_percent() < 20.0) {
                                logger->critical("Critical battery! Auto-parking mount.");
                                mount_controller->park();
                            }
                        }
                    } else {
                        logger->warn("Power GetPowerStatus failed: {}", grpc_status.error_message());
                    }
                    last_power_update = now;
                }
            }
            
            // Sleep for a bit
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        
        // Shutdown
        logger->info("Shutting down...");
        
        // Stop background threads that access CANopen before stopping gRPC.
        mount_controller->stopGamepad();
        logger->info("Gamepad loop stopped, proceeding with gRPC server shutdown");
        
        // Shut down gRPC server
        grpc_server_instance->stop();
        logger->info("gRPC server stopped, waiting for handler drain...");
        
        // Safety margin
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        
        grpc_server_instance.reset();
        
        // Release in-process subsystem services (must outlive the gRPC server,
        // so reset them only after grpc_server_instance has been destroyed)
        dome_service_impl.reset();
        derotator_service_impl.reset();
        focuser_service_impl.reset();
        
        mount_controller->shutdown();
        mount_controller.reset();
        
        logger->info("Shutdown complete");
        
    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}
