#ifndef CONFIGURATION_H
#define CONFIGURATION_H

#include <array>
#include <string>
#include <memory>
#include <map>
#include <vector>
#include <nlohmann/json.hpp>
#include "hal/hal_config.h"

namespace astro_mount {
namespace config {

/**
 * @brief Configuration management system
 * 
 * Handles loading, saving, and validation of JSON configuration files.
 */
class Configuration {
public:
    struct LoggingConfig {
        std::string level;
        std::string directory;
        int rotation_days;
        int max_file_size_mb;
        bool console_output;
    };

    struct NetworkConfig {
        std::string grpc_address;
        int grpc_port;
        int max_connections;
        bool enable_ssl;
        std::string ssl_cert_path;
        std::string ssl_key_path;
    };

    struct CanOpenConfig {
        std::string interface;
        int node_id;
        int baud_rate;
        bool enable_sync;
        int sync_interval_ms;
        std::string accel_mode = "time";  // "time" = drive interprets 0x6083/0x6084 as ramp time,
                                           // "rate" = drive interprets as acceleration rate (°/s²)
    };

    struct AxisPhysicalParameters {
        // CANopen scaling factors (per-axis)
        double position_counts_per_degree;  // counts per degree for 0x6064
        double velocity_counts_per_deg_s;   // counts per °/s for 0x606C

        // Encoder parameters
        double encoder_resolution;       // Encoder resolution [counts/rev]
        double encoder_counts_per_arcsec; // Counts per arcsecond
        double encoder_quantization_error; // Quantization error [arcseconds]
        
        // Gear parameters
        double gear_ratio;               // Total gear ratio (motor:output)
        double worm_ratio;               // Worm gear ratio (if applicable)
        int worm_teeth;                  // Number of worm teeth
        int worm_wheel_teeth;            // Number of worm wheel teeth
        
        // Cyclic errors (periodic errors)
        double cyclic_error_amplitude;   // Amplitude of cyclic error [arcseconds]
        double cyclic_error_period;      // Period of cyclic error [degrees]
        std::array<double, 8> cyclic_harmonics; // Harmonic coefficients for cyclic error
        
        // Backlash parameters
        double backlash;                 // Backlash [arcseconds]
        double backlash_temp_coeff;      // Backlash temperature coefficient [arcseconds/°C]
        
        // Stiffness and compliance
        double axis_stiffness;           // Axis stiffness [arcseconds/Nm]
        double torsional_compliance;     // Torsional compliance [rad/Nm]
        
        // Temperature coefficients
        double expansion_coeff;          // Thermal expansion coefficient [1/°C]
        double temp_gear_error_coeff;    // Gear error temperature coefficient [arcseconds/°C]
        
        // Calibration data
        std::vector<double> calibration_table; // Calibration table [counts → arcseconds]
        double calibration_temp;         // Temperature during calibration [°C]
    };

    struct MountConfig {
        std::string type;
        double latitude;
        double longitude;
        double altitude;
        double max_slew_rate;
        double max_tracking_rate;
        double slew_acceleration;
        double tracking_acceleration;
        
        // Additional mount parameters
        double mount_height;
        double pier_west;
        double pier_east;
        double default_temperature;
        double default_pressure;
        double default_humidity;
        bool use_encoders;
        bool encoders_absolute;
        double encoder_resolution;
        
        // Position/rate tolerance
        double position_tolerance{0.1};
        double rate_tolerance{0.01};
        
        // Meridian flip configuration
        bool meridian_flip_enabled{true};
        double meridian_flip_delay_minutes{5.0};
        double meridian_flip_hysteresis_degrees{0.5};
        double meridian_flip_timeout_seconds{120.0};
        
        // Soft limits
        bool soft_limits_enabled{true};
        double soft_limit_axis1_min{-270.0};
        double soft_limit_axis1_max{270.0};
        double soft_limit_axis2_min{-5.0};
        double soft_limit_axis2_max{185.0};
        double soft_limit_warning_degrees{10.0};
        double soft_limit_deceleration_degrees{5.0};
        double soft_limit_tracking_rate_factor{0.1};
        
        // Park position
        double park_position_axis1{0.0};
        double park_position_axis2{90.0};
        
        // Atmospheric refraction correction
        bool enable_refraction_correction{true};
        
        // Loop timing
        int controller_poll_ms{50};
        int tracking_update_ms{20};
        
        // Mount orientation quaternion (for CASUAL mount type)
        std::array<double, 4> orientation_quaternion{1.0, 0.0, 0.0, 0.0};  // [qx, qy, qz, qw]
        
        // Axis physical parameters
        AxisPhysicalParameters ha_axis_params;
        AxisPhysicalParameters dec_axis_params;
    };

    struct TelescopeConfig {
        double focal_length;
        double aperture;
        double tube_length;
        std::string camera_model;
        double pixel_size;
        int sensor_width;
        int sensor_height;
    };

    struct GuiderConfig {
        bool enabled;
        std::string connection_string;
        double max_correction;
        double aggression;
        int exposure_time_ms;
        int binning;
    };

    struct KalmanConfig {
        double process_noise;
        double measurement_noise;
        bool adaptive_q;
        bool adaptive_r;
        double innovation_threshold;
        int max_iterations;
    };

    struct TPointConfig {
        uint32_t enabled_terms;
        int min_measurements;
        double max_residual;
        bool auto_calibrate;
    };

    struct DerotatorConfig {
        enum DerotatorType {
            CANOPEN = 0,
            STEPPER = 1,
            SERVO = 2,
            CUSTOM = 3
        };

        DerotatorType type;
        bool enabled;
        std::string connection_string;
        double gear_ratio;
        double max_speed;
        double max_acceleration;
        double backlash;
        bool absolute_encoder;
        double encoder_resolution;
        double homing_offset;
        std::vector<double> calibration_table;
    };

    struct FieldRotationParams {
        bool enabled;
        double latitude;
        double altitude;
        double azimuth;
        double computed_rate;
        double applied_correction;
        double temperature;
        double flexure_correction;
    };

    /**
     * @brief Servo initialization SDO sequence entry
     *
     * Defines a single SDO write to be sent during servo drive initialization.
     * Used to configure manufacturer-specific parameters like microstep resolution,
     * electronic gearing, and encoder settings per the drive's manual.
     */
    struct ServoInitEntry {
        int axis;
        uint16_t index;
        uint8_t  subindex;
        int32_t  value;
        std::string description;
        uint8_t data_size = 4;  // 1, 2, or 4 bytes (default 4 for backward compat)
    };

    struct ServoInitConfig {
        bool enabled{false};
        std::vector<ServoInitEntry> sequence;
    };

    // Forward declaration for HAL configuration (defined in hal/hal_config.h)
    // Use astro_mount::hal::HALConfig directly

    Configuration();
    Configuration(const Configuration& other);
    Configuration(Configuration&& other) noexcept;
    ~Configuration();

    Configuration& operator=(const Configuration& other);
    Configuration& operator=(Configuration&& other) noexcept;

    /**
     * @brief Load configuration from file
     * @param filename Configuration file path
     * @return True if load successful
     */
    bool loadFromFile(const std::string& filename);

    /**
     * @brief Save configuration to file
     * @param filename Configuration file path
     * @return True if save successful
     */
    bool saveToFile(const std::string& filename) const;

    /**
     * @brief Load configuration from JSON string
     * @param json_str JSON configuration string
     * @return True if load successful
     */
    bool loadFromString(const std::string& json_str);

    /**
     * @brief Get configuration as JSON string
     * @return JSON configuration string
     */
    std::string toString() const;

    /**
     * @brief Validate configuration
     * @return Vector of validation errors (empty if valid)
     */
    std::vector<std::string> validate() const;

    /**
     * @brief Get logging configuration
     * @return Logging configuration
     */
    LoggingConfig getLoggingConfig() const;

    /**
     * @brief Get network configuration
     * @return Network configuration
     */
    NetworkConfig getNetworkConfig() const;

    /**
     * @brief Get CanOpen configuration
     * @return CanOpen configuration
     */
    CanOpenConfig getCanOpenConfig() const;

    /**
     * @brief Get mount configuration
     * @return Mount configuration
     */
    MountConfig getMountConfig() const;

    /**
     * @brief Get mount orientation quaternion
     * @return Array of 4 doubles [qx, qy, qz, qw]
     */
    std::array<double, 4> getMountOrientationQuaternion() const;

    /**
     * @brief Get telescope configuration
     * @return Telescope configuration
     */
    TelescopeConfig getTelescopeConfig() const;

    /**
     * @brief Get guider configuration
     * @return Guider configuration
     */
    GuiderConfig getGuiderConfig() const;

    /**
     * @brief Get Kalman filter configuration
     * @return Kalman filter configuration
     */
    KalmanConfig getKalmanConfig() const;

    /**
     * @brief Get TPOINT configuration
     * @return TPOINT configuration
     */
    TPointConfig getTPointConfig() const;

    /**
     * @brief Get derotator configuration
     * @return Derotator configuration
     */
    DerotatorConfig getDerotatorConfig() const;

    /**
     * @brief Get servo initialization configuration
     * @return Servo init configuration (SDO sequence)
     */
    ServoInitConfig getServoInitConfig() const;

    /**
     * @brief Get field rotation parameters
     * @return Field rotation parameters
     */
    FieldRotationParams getFieldRotationParams() const;

    /**
     * @brief Set logging configuration
     * @param config Logging configuration
     */
    void setLoggingConfig(const LoggingConfig& config);

    /**
     * @brief Set network configuration
     * @param config Network configuration
     */
    void setNetworkConfig(const NetworkConfig& config);

    /**
     * @brief Set CanOpen configuration
     * @param config CanOpen configuration
     */
    void setCanOpenConfig(const CanOpenConfig& config);

    /**
     * @brief Set mount configuration
     * @param config Mount configuration
     */
    void setMountConfig(const MountConfig& config);

    /**
     * @brief Set telescope configuration
     * @param config Telescope configuration
     */
    void setTelescopeConfig(const TelescopeConfig& config);

    /**
     * @brief Set guider configuration
     * @param config Guider configuration
     */
    void setGuiderConfig(const GuiderConfig& config);

    /**
     * @brief Set Kalman filter configuration
     * @param config Kalman filter configuration
     */
    void setKalmanConfig(const KalmanConfig& config);

    /**
     * @brief Set TPOINT configuration
     * @param config TPOINT configuration
     */
    void setTPointConfig(const TPointConfig& config);

    /**
     * @brief Set derotator configuration
     * @param config Derotator configuration
     */
    void setDerotatorConfig(const DerotatorConfig& config);

    /**
     * @brief Set field rotation parameters
     * @param config Field rotation parameters
     */
    void setFieldRotationParams(const FieldRotationParams& config);

    /**
     * @brief Get HAL configuration
     * @return HAL configuration
     */
    astro_mount::hal::HALConfig getHALConfig() const;

    /**
     * @brief Set HAL configuration
     * @param config HAL configuration
     */
    void setHALConfig(const astro_mount::hal::HALConfig& config);

    /**
     * @brief Get default configuration
     * @return Default configuration
     */
    static Configuration getDefault();

    /**
     * @brief Merge with another configuration
     * @param other Configuration to merge with
     * @param overwrite True to overwrite existing values
     */
    void merge(const Configuration& other, bool overwrite = true);

    /**
     * @brief Get configuration value by path
     * @param path JSON path (e.g., "logging.level")
     * @return Value as string
     */
    std::string getValue(const std::string& path) const;

    /**
     * @brief Set configuration value by path
     * @param path JSON path (e.g., "logging.level")
     * @param value Value to set
     * @return True if set successful
     */
    bool setValue(const std::string& path, const std::string& value);

    /**
     * @brief Check if configuration has been modified
     * @return True if modified since load
     */
    bool isModified() const;

    /**
     * @brief Reset modification flag
     */
    void resetModified();

private:
    class Impl;
    std::unique_ptr<Impl> pimpl;
};

} // namespace config
} // namespace astro_mount

#endif // CONFIGURATION_H