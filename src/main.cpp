#include <iostream>
#include <memory>
#include <chrono>
#include <thread>
#include <signal.h>
#include <atomic>
#include <cmath>

#include "config/configuration.h"
#include "config/mount_config.h"
#include "config/tracking_config.h"
#include "config/safety_config.h"
#include "config/calibration_config.h"
#include "logging/logger.h"
#include "controllers/mount_controller.h"
#include "api/grpc_server.h"
#include "core/astronomical_calculations.h"
#include "config/config_monitor.h"
#include "proto/mount_controller.pb.h"
#include <fstream>
#include <sstream>

// External service gRPC stubs (weather, power remain external processes).
// Dome, derotator and focuser are hosted in-process inside the mount controller.
#include <grpcpp/grpcpp.h>
#include "dome/include/dome_service_impl.h"
#include "derotator/include/derotator_service_impl.h"
#include "focuser/include/focuser_service_impl.h"
#include "st4guider/include/st4_guider_service_impl.h"
#include "pec/include/pec_service_impl.h"
#include "camera/include/camera_service_impl.h"
#include "pulley/include/pulley_service_impl.h"
#include "pidcal/include/pid_calibration_service_impl.h"
#include "notifications/notification_service.h"
#include "notifications/notification_engine.h"
#include "notifications/channels/log_channel.h"
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
std::unique_ptr<astro_st4guider::St4GuiderServiceImpl> st4_guider_service_impl;
std::unique_ptr<astro_pec::PecServiceImpl> pec_service_impl;
std::unique_ptr<astro_camera::CameraServiceImpl> camera_service_impl;
std::unique_ptr<astro_pulley::PulleyServiceImpl> pulley_service_impl;
std::unique_ptr<astro_pidcal::PidCalibrationServiceImpl> pid_calibration_service_impl;

// External service gRPC stubs (weather, power remain external processes)
std::unique_ptr<controllers::WeatherClient> weather_client;
std::unique_ptr<PowerService::Stub> power_stub;

// Integration config
config::Configuration::ExternalIntegrationConfig ext_config;

// Config hot-reload monitor (P10) — watches the config file and logs that a
// restart is required to apply changes.
std::unique_ptr<config::ConfigMonitor> config_monitor;

// Notification engine + gRPC service (R1) — hosted in-process on 50051.
std::shared_ptr<astro_mount::notifications::NotificationEngine> notification_engine;
std::unique_ptr<astro_mount::notifications::NotificationServiceImpl> notification_service_impl;

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

/// Build an astro_mount::NotificationConfig from the HAL notifications config
/// (N2) so email/webhook/mqtt channels configured in the JSON config file are
/// created and registered in the NotificationEngine at startup (not only when
/// the web UI re-configures them).
static astro_mount::NotificationConfig buildNotificationConfig(
    const astro_mount::hal::HALConfig& hal_cfg) {
    astro_mount::NotificationConfig cfg;
    const auto& n = hal_cfg.notifications;

    cfg.set_min_severity(n.min_severity);
    cfg.set_notify_on_error(n.notify_on_error);
    cfg.set_notify_on_weather_alert(n.notify_on_weather_alert);
    cfg.set_aggregate_messages(n.aggregate_messages);
    cfg.set_aggregation_interval_minutes(n.aggregation_interval_minutes);

    // Email channel — to_addresses is a comma/semicolon separated string in
    // the config file; the proto + EmailChannel use a repeated/vector list.
    if (n.email.enabled) {
        auto* ch = cfg.add_channels();
        ch->set_type(astro_mount::CHANNEL_EMAIL);
        ch->set_enabled(true);
        auto* email = ch->mutable_email();
        email->set_smtp_host(n.email.smtp_host);
        email->set_smtp_port(n.email.smtp_port);
        email->set_use_tls(n.email.use_tls);
        email->set_username(n.email.username);
        email->set_password(n.email.password);
        email->set_from_address(n.email.from_address);
        std::string to = n.email.to_addresses;
        size_t pos = 0;
        while ((pos = to.find_first_of(",;")) != std::string::npos) {
            std::string addr = to.substr(0, pos);
            if (!addr.empty()) email->add_to_addresses(addr);
            to.erase(0, pos + 1);
        }
        if (!to.empty()) email->add_to_addresses(to);
        email->set_subject_prefix(n.email.subject_prefix);
    }

    // Webhook channel
    if (n.webhook.enabled) {
        auto* ch = cfg.add_channels();
        ch->set_type(astro_mount::CHANNEL_WEBHOOK);
        ch->set_enabled(true);
        auto* webhook = ch->mutable_webhook();
        webhook->set_url(n.webhook.url);
        webhook->set_method(n.webhook.method.empty() ? "POST" : n.webhook.method);
        webhook->set_auth_token(n.webhook.auth_token);
        webhook->set_timeout_seconds(n.webhook.timeout_seconds);
        webhook->set_retry_count(n.webhook.retry_count);
    }

    // MQTT channel
    if (n.mqtt.enabled) {
        auto* ch = cfg.add_channels();
        ch->set_type(astro_mount::CHANNEL_MQTT);
        ch->set_enabled(true);
        auto* mqtt = ch->mutable_mqtt();
        mqtt->set_broker_url(n.mqtt.broker_url);
        mqtt->set_broker_port(n.mqtt.broker_port);
        mqtt->set_client_id(n.mqtt.client_id);
        mqtt->set_topic_prefix(n.mqtt.topic_prefix);
        mqtt->set_use_tls(n.mqtt.use_tls);
        mqtt->set_username(n.mqtt.username);
        mqtt->set_password(n.mqtt.password);
        mqtt->set_qos(n.mqtt.qos);
    }

    return cfg;
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

        // P10: monitor the config file. The controller does not apply changes
        // live (no hot-reload of controllers/services), so on a change we log
        // clearly that a restart is required — edits from the web UI are no
        // longer silently ignored.
        config_monitor = std::make_unique<config::ConfigMonitor>(config_file, 2000);
        config_monitor->setConfigChangeCallback([logger](const config::Configuration&) {
            logger->warn("Configuration file changed — restart the controller to apply changes.");
        });
        if (config_monitor->start()) {
            logger->info("Config monitor active on {}", config_file);
        } else {
            logger->warn("Config monitor failed to start on {}", config_file);
        }

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
        mount_cfg.mount_height = cfg_mount.mount_height;
        mount_cfg.pier_west = cfg_mount.pier_west;
        mount_cfg.pier_east = cfg_mount.pier_east;
        mount_cfg.max_slew_rate = cfg_mount.max_slew_rate;
        mount_cfg.max_tracking_rate = cfg_mount.max_tracking_rate;
        mount_cfg.slew_acceleration = cfg_mount.slew_acceleration;
        mount_cfg.tracking_acceleration = cfg_mount.tracking_acceleration;
        mount_cfg.position_tolerance = cfg_mount.position_tolerance;
        mount_cfg.rate_tolerance = cfg_mount.rate_tolerance;
        mount_cfg.slew_verify_tolerance_servo_deg = cfg_mount.slew_verify_tolerance_servo_deg;
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
        controller_config.tube_length = telescope_cfg.tube_length;
        controller_config.camera_model = telescope_cfg.camera_model;
        controller_config.pixel_size = telescope_cfg.pixel_size;
        controller_config.sensor_width = telescope_cfg.sensor_width;
        controller_config.sensor_height = telescope_cfg.sensor_height;

        // LX200 serial interface
        controller_config.lx200_enabled = telescope_cfg.lx200_enabled;
        controller_config.lx200_port = telescope_cfg.lx200_port;
        controller_config.lx200_baud_rate = telescope_cfg.lx200_baud_rate;
        
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

        // ─── LX200 serial interface ──────────────────────────────────────────
        if (controller_config.lx200_enabled) {
            if (mount_controller->startLx200()) {
                logger->info("LX200 serial interface started on {} @ {} bps",
                    controller_config.lx200_port, controller_config.lx200_baud_rate);
            } else {
                logger->warn("Failed to start LX200 server on {}",
                    controller_config.lx200_port);
            }
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

        // ST4 Guider service (in-process) — Phase 2 (P2)
        if (ext_config.st4_guider_enabled) {
            try {
                st4_guider_service_impl = std::make_unique<astro_st4guider::St4GuiderServiceImpl>(
                    "config/st4_guider_config.json");
                logger->info("ST4 guider subsystem hosted in-process (served on unified gRPC port)");
            } catch (const std::exception& e) {
                logger->warn("Failed to initialise in-process ST4 guider service: {}", e.what());
            }
        }

        // PEC service (in-process) — Phase 2 (P2)
        if (ext_config.pec_enabled) {
            try {
                pec_service_impl = std::make_unique<astro_pec::PecServiceImpl>(
                    "config/pec_config.json");
                logger->info("PEC subsystem hosted in-process (served on unified gRPC port)");
            } catch (const std::exception& e) {
                logger->warn("Failed to initialise in-process PEC service: {}", e.what());
            }
        }

        // Notification service (in-process) — R1. Always hosted so the web UI
        // can configure channels, send test notifications and view status.
        notification_engine = std::make_shared<astro_mount::notifications::NotificationEngine>();
        notification_engine->registerChannel(
            std::make_unique<astro_mount::notifications::LogChannel>());

        // N2: create and register real channels (email/webhook/mqtt) from the
        // loaded config file so they are active from startup — previously only
        // the log channel existed and the other channels were dead code.
        {
            auto notif_cfg = buildNotificationConfig(hal_cfg);
            int enabled_channels = notif_cfg.channels_size();
            notification_engine->configure(notif_cfg);
            if (enabled_channels > 0) {
                logger->info("Notification channels configured from file: {} channel(s) enabled",
                             enabled_channels);
            } else {
                logger->info("No external notification channels enabled in config — log channel active");
            }
        }

        notification_engine->start();
        notification_service_impl = std::make_unique<astro_mount::notifications::NotificationServiceImpl>(
            notification_engine);
        logger->info("Notification service hosted in-process (served on unified gRPC port)");

        // Camera service (in-process) — R3. Simulated camera, always hosted so
        // the web UI exposure/filter/cooler controls have a real backend.
        camera_service_impl = std::make_unique<astro_camera::CameraServiceImpl>();
        logger->info("Camera subsystem hosted in-process (simulated, served on unified gRPC port)");

        // Pulley service (in-process) — R3. Simulated linear actuator, always
        // hosted so the web UI deploy/retract/home controls have a real backend.
        pulley_service_impl = std::make_unique<astro_pulley::PulleyServiceImpl>();
        logger->info("Pulley subsystem hosted in-process (simulated, served on unified gRPC port)");

        // PID calibration service (in-process) — drives real motor loops via
        // the mount controller and writes gains to the drive RAM (volatile).
        pid_calibration_service_impl =
            std::make_unique<astro_pidcal::PidCalibrationServiceImpl>(*mount_controller);
        logger->info("PID calibration service hosted in-process (served on unified gRPC port)");

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
        if (st4_guider_service_impl) {
            grpc_server_instance->registerService(st4_guider_service_impl.get());
        }
        if (pec_service_impl) {
            grpc_server_instance->registerService(pec_service_impl.get());
        }
        if (notification_service_impl) {
            grpc_server_instance->registerService(notification_service_impl.get());
        }
        if (camera_service_impl) {
            grpc_server_instance->registerService(camera_service_impl.get());
        }
        if (pulley_service_impl) {
            grpc_server_instance->registerService(pulley_service_impl.get());
        }
        if (pid_calibration_service_impl) {
            grpc_server_instance->registerService(pid_calibration_service_impl.get());
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
                    ext_config.weather_address, notification_engine);
                weather_client->start(ext_config.weather_poll_interval_ms,
                    [logger](const std::string& msg) {
                        logger->warn("Weather DANGER: {} — auto-parking mount", msg);
                        if (mount_controller) {
                            mount_controller->park();
                        }
                    },
                    [logger](const astro_mount::WeatherStatus& status) {
                        // Forward live environmental conditions from the weather
                        // service to the mount controller so atmospheric refraction
                        // and status reporting use real weather data.
                        const double temperature = status.temperature_c();
                        const double pressure = status.pressure_hpa();
                        const double humidity = status.humidity_percent() / 100.0;
                        if (mount_controller && temperature != 0.0 && pressure != 0.0) {
                            mount_controller->setEnvironmentalParams(
                                temperature, pressure, humidity);
                        } else if (!mount_controller) {
                            logger->warn("Weather: received data but mount controller not ready");
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
                // P12: use the same TLS policy as the rest of the system —
                // SSL when network SSL is enabled, insecure otherwise (previously
                // the power client always used insecure credentials).
                std::shared_ptr<grpc::ChannelCredentials> power_creds;
                if (network_config.enable_ssl) {
                    grpc::SslCredentialsOptions ssl_opts;
                    if (!network_config.ssl_cert_path.empty()) {
                        std::ifstream cert_file(network_config.ssl_cert_path);
                        std::stringstream cert_ss;
                        cert_ss << cert_file.rdbuf();
                        ssl_opts.pem_root_certs = cert_ss.str();
                    }
                    power_creds = grpc::SslCredentials(ssl_opts);
                } else {
                    power_creds = grpc::InsecureChannelCredentials();
                }
                auto channel = grpc::CreateChannel(ext_config.power_address, power_creds);
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

        // Local astronomical-calculations instance for the dome/derotator feeds.
        // setObserverLocation() is required before equatorialToHorizontal() can
        // convert the tracked target into a real azimuth / hour-angle.
        core::AstronomicalCalculations astro_calc;
        astro_calc.setObserverLocation(mount_cfg.latitude, mount_cfg.longitude, mount_cfg.altitude);
        
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
            
            // Dome: feed the TRUE telescope azimuth (no gRPC round-trip).
            // FIX (N4): previously the raw axis1 position (hour angle for an
            // equatorial mount) was fed as "azimuth", so a dome synchronised on
            // the wrong value. Convert the current pointing to horizontal now.
            if (dome_service_impl && ext_config.dome_enabled) {
                auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                    now - last_dome_update).count();
                if (elapsed >= ext_config.dome_update_interval_ms) {
                    double dome_azimuth = status.telescope_axis1_position;  // fallback
                    double jd = core::AstronomicalCalculations::getCurrentJulianDate();
                    if (status.tracking_active &&
                        std::isfinite(status.tracking_target_ra) &&
                        std::isfinite(status.tracking_target_dec)) {
                        // While tracking, the target RA/Dec is the best estimate
                        // of the current pointing.
                        auto [alt, az] = astro_calc.equatorialToHorizontal(
                            status.tracking_target_ra, status.tracking_target_dec, jd, false);
                        if (std::isfinite(az)) dome_azimuth = az;
                    } else if (mount_cfg.mount_type == config::MountType::EQUATORIAL) {
                        // Idle/slewing: derive RA/Dec from telescope axis positions.
                        // For an equatorial mount axis1 = hour angle (telescope deg),
                        // axis2 = declination. HA[hours] = axis1/15, RA = LST - HA.
                        double lst = core::AstronomicalCalculations::calculateLST(jd, mount_cfg.longitude);
                        double ha_h = status.telescope_axis1_position / 15.0;
                        double ra_h = lst - ha_h;
                        ra_h = std::fmod(ra_h, 24.0);
                        if (ra_h < 0.0) ra_h += 24.0;
                        auto [alt, az] = astro_calc.equatorialToHorizontal(
                            ra_h, status.telescope_axis2_position, jd, false);
                        if (std::isfinite(az)) dome_azimuth = az;
                    }
                    dome_service_impl->setMountAzimuth(dome_azimuth);
                    last_dome_update = now;
                }
            }
            
            // Derotator: feed mount position directly (no gRPC round-trip).
            // FIX (N3): ha_hours/dec_deg were hardcoded to 0.0, so the
            // derotator's field-rotation logic received meaningless data.
            // Compute the actual hour angle and declination now.
            if (derotator_service_impl && ext_config.derotator_enabled) {
                auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                    now - last_derotator_update).count();
                if (elapsed >= ext_config.derotator_update_interval_ms) {
                    double ha_hours = 0.0, dec_deg = 0.0;
                    double jd = core::AstronomicalCalculations::getCurrentJulianDate();
                    if (status.tracking_active &&
                        std::isfinite(status.tracking_target_ra) &&
                        std::isfinite(status.tracking_target_dec)) {
                        // While tracking: HA = LST - RA (normalised to [-12, 12]h).
                        double lst = core::AstronomicalCalculations::calculateLST(jd, mount_cfg.longitude);
                        ha_hours = lst - status.tracking_target_ra;
                        ha_hours = std::fmod(ha_hours + 12.0, 24.0);
                        if (ha_hours < 0.0) ha_hours += 24.0;
                        ha_hours -= 12.0;
                        dec_deg = status.tracking_target_dec;
                    } else if (mount_cfg.mount_type == config::MountType::EQUATORIAL) {
                        // Idle/slewing: axis1 = hour angle (telescope deg) → hours,
                        // axis2 = declination (telescope deg).
                        ha_hours = status.telescope_axis1_position / 15.0;
                        dec_deg  = status.telescope_axis2_position;
                    }
                    // P5: pass the mount type (selects the field-rotation model)
                    // and the mount orientation quaternion (for CASUAL).
                    int derotator_mount_type = 0;  // EQUATORIAL
                    switch (mount_cfg.mount_type) {
                        case config::MountType::ALT_AZ: derotator_mount_type = 1; break;
                        case config::MountType::CASUAL: derotator_mount_type = 2; break;
                        case config::MountType::EQUATORIAL:
                        case config::MountType::UNKNOWN:
                        default: derotator_mount_type = 0; break;
                    }
                    derotator_service_impl->setMountPosition(
                        status.telescope_axis1_position,
                        status.telescope_axis2_position,
                        mount_cfg.latitude,
                        ha_hours,
                        dec_deg,
                        derotator_mount_type,
                        mount_cfg.mount_orientation.quaternion
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
        st4_guider_service_impl.reset();
        pec_service_impl.reset();
        camera_service_impl.reset();
        pulley_service_impl.reset();
        pid_calibration_service_impl.reset();

        // Stop the notification engine and release its gRPC service.
        notification_service_impl.reset();
        if (notification_engine) { notification_engine->stop(); notification_engine.reset(); }

        // Stop the config monitor (joins its polling thread).
        if (config_monitor) { config_monitor->stop(); config_monitor.reset(); }

        mount_controller->shutdown();
        mount_controller.reset();
        
        logger->info("Shutdown complete");
        
        // Shut down the logging subsystem (joins spdlog's periodic flush thread
        // and releases all sinks) while still inside main. This is the LAST
        // logging operation — do not log anything after this point.
        logging::Logger::shutdown();
        
    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << std::endl;

        // Tear down the global objects here, while the logging subsystem and
        // all linked libraries are still fully alive.  Relying on implicit
        // destruction at process exit is unsafe: a global destructor
        // (e.g. ~MountController → shutdown → stopGamepad) calls into
        // logging::Logger, whose static state may already have been destroyed
        // by then, causing a segfault after main() returns.
        mount_controller.reset();
        grpc_server_instance.reset();
        dome_service_impl.reset();
        derotator_service_impl.reset();
        focuser_service_impl.reset();
        st4_guider_service_impl.reset();
        pec_service_impl.reset();
        camera_service_impl.reset();
        pulley_service_impl.reset();
        pid_calibration_service_impl.reset();
        weather_client.reset();
        power_stub.reset();
        notification_service_impl.reset();
        if (notification_engine) { notification_engine->stop(); notification_engine.reset(); }
        if (config_monitor) { config_monitor->stop(); config_monitor.reset(); }

        // Shut down spdlog (joins its periodic flush thread) while we are
        // still inside main, avoiding teardown-order issues at process exit.
        logging::Logger::shutdown();

        return 1;
    }
    
    return 0;
}
