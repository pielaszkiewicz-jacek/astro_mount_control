#ifndef MOUNT_CONTROLLER_H
#define MOUNT_CONTROLLER_H

#include <array>
#include <memory>
#include <string>
#include <chrono>
#include <vector>
#include <functional>
#include "proto/mount_controller.pb.h"
#include "models/ephemeris_tracker.h"
#include "hal/hal_config.h"
#include "config/mount_config.h"
#include "config/tracking_config.h"
#include "config/safety_config.h"
#include "config/calibration_config.h"

// Forward declaration for HAL
namespace astro_mount {
namespace hal {
class HALInterface;
} // namespace hal
} // namespace astro_mount

namespace astro_mount {

// Forward declaration of FieldRotationParams for the controller API.
// Full definition is in models/field_rotation_model.h
struct FieldRotationParams {
    double computed_rate_deg_s{0.0};
    double applied_correction_deg{0.0};
    double latitude_deg{0.0};
    double altitude_deg{0.0};
    double azimuth_deg{0.0};
    double temperature_c{15.0};
    double flexure_correction_deg{0.0};
};

namespace controllers {

/**
 * @brief Main mount controller class
 *
 * Integrates all components: astronomical calculations, TPOINT model,
 * Kalman filter, encoders, and HAL (Hardware Abstraction Layer) interface.
 * All hardware control is routed through the HAL abstractions
 * (MotorControl / EncoderReader / SafetyMonitor).
 */
class MountController {
public:
    /**
     * @brief Gamepad navigation mode — how joystick axes map to mount motion
     */
    enum class GamepadMode {
        RAW = 0,           ///< Raw axis velocity control (always available)
        CELESTIAL = 1,     ///< Navigate in RA/Dec (requires bootstrap calibration)
        ALT_AZ = 2,        ///< Navigate in Alt/Az (requires bootstrap calibration)
        PRECISION = 3      ///< Fine RA/Dec control at low speed (requires TPOINT calibration)
    };

    /**
     * @brief Combined controller configuration using domain-specific sub-configs
     *
     * Replaces the monolithic ControllerConfig (~150 fields) with
     * domain-specific configuration structs.
     *
     * Fields kept at this level: network, logging, telescope, and HAL
     * config — these are coordination-level settings that don't belong
     * in any single domain config.
     */
    struct ControllerConfig {
        // Domain-specific configurations (single source of truth)
        config::MountConfig mount_config;           ///< Mount type, location, rates, encoders, axis physics
        config::TrackingConfig tracking_config;     ///< Loop timing, guider, field rotation
        config::SafetyConfig safety_config;         ///< Soft limits, meridian flip, park, refraction
        config::CalibrationConfig calibration_config; ///< TPOINT, bootstrap
        
        // HAL configuration (from JSON "hal" section)
        hal::HALConfig hal_config;
        
        // Network configuration
        std::string grpc_address{"0.0.0.0"};
        int grpc_port{50051};
        int network_max_connections{10};
        bool network_enable_ssl{false};
        std::string network_ssl_cert_path;
        std::string network_ssl_key_path;
        
        // Logging configuration
        std::string log_level{"info"};
        std::string log_directory{"/var/log/astro-mount"};
        bool log_console_output{true};
        
        // Telescope parameters
        double focal_length{1000.0};
        double aperture{100.0};
    };

    struct MountStatus {
        enum class State {
            UNINITIALIZED,
            INITIALIZING,
            IDLE,
            SLEWING,
            TRACKING,
            MERIDIAN_FLIP,   ///< Mount is executing a meridian flip
            PARKING,
            PARKED,
            ERROR
        };
        
        State state;
        double axis1_position;      // Degrees (servo/motor shaft)
        double axis2_position;      // Degrees (servo/motor shaft)
        double telescope_axis1_position; // Degrees (telescope axis, after gear ratio)
        double telescope_axis2_position; // Degrees (telescope axis, after gear ratio)
        double axis1_rate;          // Degrees/sec (commanded tracking/slewing rate)
        double axis2_rate;          // Degrees/sec (commanded tracking/slewing rate)
        double actual_axis1_rate;   // Degrees/sec (actual HAL motor velocity)
        double actual_axis2_rate;   // Degrees/sec (actual HAL motor velocity)
        double axis1_target;        // Degrees
        double axis2_target;        // Degrees
        
        bool encoders_active;
        bool guider_active;
        bool tpoint_calibrated;
        
        double tracking_error_ra;   // Arcseconds
        double tracking_error_dec;  // Arcseconds
        
        /// Meridian flip status
        bool meridian_flip_pending{false};  ///< True if a flip is pending (waiting for delay)
        bool meridian_flip_in_progress{false}; ///< True if the flip slew is being executed
        int pier_side{1};                    ///< 1=East pier, -1=West pier
        double time_to_meridian{0.0};        ///< Time until meridian crossing [hours] (negative = past)
        
        /// Soft safety limits status
        bool soft_limit_warning_active{false};    ///< True when any axis is within the warning zone
        bool soft_limit_deceleration_active{false}; ///< True when any axis is within the deceleration zone
        double soft_limit_distance_axis1{0.0};    ///< Shortest distance to nearest soft limit on axis1 [degrees]
        double soft_limit_distance_axis2{0.0};    ///< Shortest distance to nearest soft limit on axis2 [degrees]
        std::string soft_limit_warning_message;   ///< Human-readable warning description
        
        // === NEW: Bootstrap / encoder status fields ===
        bool encoders_absolute{false};            ///< Encoder type from config
        int bootstrap_mode{0};                    ///< Current bootstrap mode (BootstrapMode enum)
        bool bootstrap_calibrated{false};         ///< Whether bootstrap calibration completed
        int bootstrap_measurement_count{0};       ///< Number of bootstrap measurements stored
        
        std::chrono::system_clock::time_point timestamp;
        std::string error_message;
    };

    MountController();
    explicit MountController(std::unique_ptr<hal::HALInterface> hal_interface);
    ~MountController();

    /**
     * @brief Initialize controller with configuration
     * @param config Controller configuration
     * @return True if initialization successful
     */
    bool initialize(const ControllerConfig& config);

    /**
     * @brief Shutdown controller
     */
    void shutdown();

    /**
     * @brief Slew to equatorial coordinates
     * @param ra Right ascension in hours
     * @param dec Declination in degrees
     * @return True if command accepted
     */
    bool slewToEquatorial(double ra, double dec);

    /**
     * @brief Slew to horizontal coordinates
     * @param altitude Altitude in degrees
     * @param azimuth Azimuth in degrees
     * @return True if command accepted
     */
    bool slewToHorizontal(double altitude, double azimuth);

    /**
     * @brief Start tracking object
     * @param ra Right ascension in hours
     * @param dec Declination in degrees
     * @param mode Tracking mode
     * @return True if command accepted
     */
    bool startTracking(double ra, double dec, config::TrackingMode mode = config::TrackingMode::SIDEREAL);

    /**
     * @brief Stop tracking/slewing
     */
    void stop();

    /**
     * @brief Park mount
     */
    void park();

    /**
     * @brief Unpark mount
     */
    void unpark();

    /**
     * @brief Get current status
     * @return Mount status
     */
    MountStatus getStatus() const;

    /**
     * @brief Read live axis positions from the HAL hardware and update cached state.
     *
     * Call periodically (e.g. from the main loop) to keep getStatus()
     * in sync with the physical hardware.  Without this, positions
     * would only be updated during active slew/track operations.
     */
    void refreshPositions();

    /**
     * @brief Clear error state and reset to IDLE
     *
     * When the mount enters ERROR state (e.g. soft limit violation, HAL safety
     * monitor trigger, numerical errors), this method provides the only way to
     * recover without a full shutdown/restart cycle. It:
     *   - Stops any background operations and joins the work thread
     *   - Transitions from ERROR → IDLE
     *   - Clears the error message string
     *   - Resets the HAL SafetyMonitor errors (if available)
     *
     * Has no effect if the mount is not in ERROR state.
     */
    void clearErrors();

    // ============================================
    // Meridian Flip API
    // ============================================
    
    /**
     * @brief Manually trigger a meridian flip
     *
     * During tracking, forces an immediate flip to the other pier side.
     * The mount will slew HA+180°, Dec→180°-Dec and resume tracking.
     *
     * @return True if flip was initiated
     */
    bool executeMeridianFlip();
    
    /**
     * @brief Check if a meridian flip is pending (waiting for delay)
     * @return True if a flip is pending
     */
    bool isMeridianFlipPending() const;
    
    /**
     * @brief Get time to meridian crossing
     * @return Time until meridian crossing in hours (negative if past meridian)
     */
    double getTimeToMeridian() const;
    
    /**
     * @brief Get current pier side
     * @return 1 = East pier (normal tracking), -1 = West pier (flipped)
     */
    int getPierSide() const;

    // ============================================
    // Calibration API - Bootstrap (initial alignment)
    // ============================================
    
    /**
     * @brief Add simple measurement for initial calibration/bootstrap
     * 
     * Used for rough alignment and initial plate solving. Assumes:
     * - observed_ra/dec are approximate (from initial plate solving)
     * - mount_ha/mount_dec may be estimated from encoders
     * - Environmental parameters use defaults
     * 
     * @param observed_ra Observed RA in hours
     * @param observed_dec Observed Dec in degrees
     * @param expected_ra Expected RA in hours (from catalog)
     * @param expected_dec Expected Dec in degrees (from catalog)
     * @param mount_ha Mount hour angle in hours (from encoders, optional - defaults to 0)
     * @param mount_dec Mount declination in degrees (from encoders, optional - defaults to expected_dec)
     * @return True if measurement accepted
     */
    bool addBootstrapMeasurement(double observed_ra, double observed_dec,
                                 double expected_ra, double expected_dec,
                                 double mount_ha = 0.0, double mount_dec = 0.0);
    
    /**
     * @brief Run bootstrap calibration to get initial orientation
     *
     * Uses Wahba's problem (SVD-based rotation estimation) to determine
     * the optimal rotation quaternion from mount axis positions to the
     * true horizontal frame. Absorbs encoder offset for incremental encoders.
     * Now supports ALL mount types (CASUAL, EQUATORIAL, ALT_AZ).
     *
     * @return True if bootstrap calibration successful
     */
    bool runBootstrapCalibration();

    /**
     * @brief Set bootstrap calibration mode
     *
     * Controls how measurements are collected:
     * - MANUAL (0): User manually points via gamepad
     * - HYBRID (1): 3 manual then automatic slews
     * - AUTOMATIC (2): Fully automatic with plate solver
     *
     * @param mode Bootstrap mode to set
     * @return True if mode accepted
     */
    bool setBootstrapMode(config::BootstrapMode mode);
    
    /**
     * @brief Get current bootstrap calibration mode
     * @return Current BootstrapMode enum value
     */
    config::BootstrapMode getBootstrapMode() const;
    
    /**
     * @brief Get bootstrap calibration status
     * @return True if bootstrap calibration has been successfully completed
     */
    bool isBootstrapCalibrated() const;
    
    /**
     * @brief Get number of bootstrap measurements stored
     * @return Number of measurements
     */
    size_t getBootstrapMeasurementCount() const;
    
    /**
     * @brief Get bootstrap calibration quaternion estimation error
     * @return Quaternion estimation error in arcseconds, 0.0 if not calibrated
     */
    double getBootstrapQuaternionErrorArcsec() const;
    
    /**
     * @brief Get bootstrap calibration RA correction
     * @return RA correction in arcseconds
     */
    double getBootstrapRaCorrectionArcsec() const;
    
    /**
     * @brief Get bootstrap calibration Dec correction
     * @return Dec correction in arcseconds
     */
    double getBootstrapDecCorrectionArcsec() const;
    
    // ============================================
    // Metrics and Operation Counters
    // ============================================
    
    /**
     * @brief Get total number of slew operations performed
     * @return Slew count
     */
    size_t getSlewCount() const;
    
    /**
     * @brief Get total number of tracking sessions started
     * @return Track count
     */
    size_t getTrackCount() const;
    
    /**
     * @brief Get total number of TPOINT calibrations performed
     * @return Calibration count
     */
    size_t getCalibrationCount() const;
    
    /**
     * @brief Get total number of tracking loop iterations
     * @return Tracking iteration count
     */
    size_t getTrackingIterationCount() const;
    
    /**
     * @brief Get total accumulated tracking update time in milliseconds
     * @return Total update time in ms
     */
    double getTotalUpdateTimeMs() const;
    
    // ============================================
    // TPOINT Calibration Metrics
    // ============================================
    
    /**
     * @brief Get number of TPOINT calibration measurements stored
     * @return Measurement count
     */
    size_t getTPointMeasurementCount() const;
    
    /**
     * @brief Get TPOINT calibration RMS residual
     * @return RMS residual in arcseconds
     */
    double getTPointResidualRmsArcsec() const;
    
    /**
     * @brief Get TPOINT calibration maximum residual
     * @return Maximum residual in arcseconds
     */
    double getTPointResidualMaxArcsec() const;
    
    /**
     * @brief Get TPOINT calibration chi-squared statistic
     * @return Chi-squared value (sum of squared residuals in arcsec²)
     */
    double getTPointChiSquared() const;
    
    /**
     * @brief Clear all bootstrap measurements
     * 
     * Useful for starting a new calibration session
     */
    void clearBootstrapMeasurements();
    
    // ============================================
    // Calibration API - Full TPOINT calibration
    // ============================================
    
    /**
     * @brief Add measurement for full TPOINT calibration
     * 
     * Used for precise calibration after bootstrap. Includes all parameters
     * for accurate modeling of mount errors and atmospheric refraction.
     * 
     * @param observed_ra Observed RA in hours (precise, from calibrated plate solving)
     * @param observed_dec Observed Dec in degrees (precise, from calibrated plate solving)
     * @param expected_ra Expected RA in hours (from catalog with proper motion/parallax)
     * @param expected_dec Expected Dec in degrees (from catalog with proper motion/parallax)
     * @param mount_ha Mount hour angle in hours (accurate, from calibrated encoders)
     * @param mount_dec Mount declination in degrees (accurate, from calibrated encoders)
     * @param temperature Temperature in Celsius (for precise refraction modeling)
     * @param pressure Pressure in hPa (for precise refraction modeling)
     * @param humidity Relative humidity 0-1 (for precise refraction modeling)
     * @param proper_motion_ra Proper motion in RA (mas/yr) for precise expected position
     * @param proper_motion_dec Proper motion in Dec (mas/yr) for precise expected position
     * @param parallax Parallax in mas (for precise expected position)
     * @param epoch Epoch of coordinates (e.g., 2000.0)
     * @return True if measurement accepted
     */
    bool addTPointMeasurement(double observed_ra, double observed_dec,
                              double expected_ra, double expected_dec,
                              double mount_ha, double mount_dec,
                              double temperature = 15.0, double pressure = 1013.25,
                              double humidity = 0.5,
                              double proper_motion_ra = 0.0, double proper_motion_dec = 0.0,
                              double parallax = 0.0, double epoch = 2000.0);
    
    /**
     * @brief Clear all TPOINT measurements
     *
     * Useful for starting a new TPOINT calibration session
     */
    void clearTPointMeasurements();

    /**
     * @brief Run TPOINT calibration
     * @return True if calibration successful
     */
    bool runTPointCalibration();

    /**
     * @brief Get TPOINT parameters
     * @return JSON string with TPOINT parameters
     */
    std::string getTPointParameters() const;

    /**
     * @brief Get rotation matrix (quaternion)
     * @return Rotation quaternion as vector [q0, q1, q2, q3]
     */
    std::vector<double> getRotationMatrix() const;

    /**
     * @brief Enable/disable encoders
     * @param enable True to enable encoders
     */
    void setEncodersEnabled(bool enable);

    /**
     * @brief Set encoder type
     * @param absolute True for absolute encoders, false for incremental
     */
    void setEncoderType(bool absolute);

    /**
     * @brief Connect to guider
     * @param connection_string Guider connection string
     * @return True if connection successful
     */
    bool connectGuider(const std::string& connection_string);

    /**
     * @brief Disconnect from guider
     */
    void disconnectGuider();

    /**
     * @brief Apply guider correction
     * @param ra_correction RA correction in arcseconds
     * @param dec_correction Dec correction in arcseconds
     */
    void applyGuiderCorrection(double ra_correction, double dec_correction);

    /**
     * @brief Determine pole position using drift method
     * @param duration_hours Duration of drift measurement in hours
     * @return Pole position (latitude, longitude, accuracy)
     */
    std::tuple<double, double, double> determinePolePosition(double duration_hours = 1.0);

    /**
     * @brief Save controller state to file
     * @param filename File to save to
     * @return True if save successful
     */
    bool saveState(const std::string& filename) const;

    /**
     * @brief Load controller state from file
     * @param filename File to load from
     * @return True if load successful
     */
    bool loadState(const std::string& filename);

    /**
     * @brief Set environmental parameters
     * @param temperature Temperature in Celsius
     * @param pressure Pressure in hPa
     * @param humidity Relative humidity (0-1)
     */
    void setEnvironmentalParams(double temperature, double pressure, double humidity);

    /**
     * @brief Set callback for status updates
     * @param callback Callback function
     */
    void setStatusCallback(std::function<void(const MountStatus&)> callback);

    /**
     * @brief Set callback for error events
     * @param callback Callback function
     */
    void setErrorCallback(std::function<void(const std::string&)> callback);

    /**
     * @brief Get controller configuration
     * @return Controller configuration
     */
    ControllerConfig getConfiguration() const;

    /**
     * @brief Low-level axis control — set position or velocity target.
     *
     * Provides HAL-based manual axis control.
     *
     * @param axis_id Axis index (0 = HA/RA/Azimuth, 1 = Dec/Altitude)
     * @param mode 0 = POSITION_CONTROL, 1 = VELOCITY_CONTROL
     * @param target_position Target position (degrees), used in position mode
     * @param target_velocity Target velocity (deg/s), used in velocity mode
     * @param acceleration Acceleration (deg/s²)
     * @param relative If true, offsets current position/velocity instead of absolute
     * @return True if command accepted
     */
    bool controlAxis(int axis_id, int mode, double target_position,
                     double target_velocity, double acceleration, bool relative);

    /**
     * @brief Stop an axis (smooth or immediate).
     *
     * @param axis_id Axis index (0 or 1)
     * @param decelerate If true, decelerate smoothly; otherwise stop immediately
     * @param deceleration Deceleration rate (deg/s²)
     * @return True if command accepted
     */
    bool stopAxis(int axis_id, bool decelerate, double deceleration);

    /**
     * @brief Emergency stop one or all axes.
     *
     * @param axis_id Axis index, or -1 for all axes
     * @param reset_after If true, clear errors / re-enable after stop
     * @return True if command accepted
     */
    bool emergencyStop(int axis_id, bool reset_after);

    /**
     * @brief Get current axis status (position, velocity, drive state).
     *
     * @param status [out] Proto AxisStatus message
     * @return True if status retrieved successfully
     */
    bool getAxisStatus(::astro_mount::AxisStatus& status) const;

    /**
     * @brief Update controller configuration
     * @param config New configuration
     * @return True if update successful
     */
    bool updateConfiguration(const ControllerConfig& config);

    /**
     * @brief Set the configuration file path for persistence
     *
     * When set, updateConfiguration() will also save changes to this file
     * so they survive controller restarts.
     *
     * @param path Path to the JSON configuration file
     */
    void setConfigFilePath(const std::string& path);

    // ============================================
    // Mount Orientation API (for CASUAL mount type)
    // ============================================
    
    /**
     * @brief Set mount orientation quaternion
     *
     * For CASUAL mount type, sets the orientation of the mount's axes
     * relative to the local horizontal frame.
     *
     * @param orientation Mount orientation quaternion
     * @return True if orientation was accepted
     */
    bool setMountOrientation(const config::MountOrientation& orientation);
    
    /**
     * @brief Get current mount orientation
     * @return Current mount orientation
     */
    config::MountOrientation getMountOrientation() const;
    
    /**
     * @brief Upload ephemeris data for moving object tracking
     * @param object_id Object identifier
     * @param object_name Human-readable object name
     * @param object_type Type of object ("comet", "satellite", "asteroid", etc.)
     * @param points Vector of ephemeris points (timestamp, ra, dec, rates)
     * @param interpolation_order Interpolation order (1=linear, 2=quadratic, 3=cubic)
     * @return True if ephemeris uploaded successfully
     */
    bool uploadEphemeris(const std::string& object_id,
                        const std::string& object_name,
                        const std::string& object_type,
                        const std::vector<std::tuple<std::chrono::system_clock::time_point,
                                                      double, double, double, double>>& points,
                        int interpolation_order = 3);
    
    /**
     * @brief Start tracking of moving object using ephemeris
     * @param object_id Object identifier
     * @param start_time Time to start tracking
     * @param lead_time_seconds Time to start before first point (for pre-slewing)
     * @param wait_at_start Wait at start position until start_time
     * @param enable_prediction Enable prediction beyond ephemeris range
     * @param prediction_interval_hours Prediction interval if enabled
     * @param tracking_mode Tracking mode ("continuous", "sidereal_rate", "custom_rate")
     * @param custom_rate_ra Custom RA rate (hours/hour)
     * @param custom_rate_dec Custom Dec rate (degrees/hour)
     * @return Tracker ID or empty string if failed
     */
    std::string startEphemerisTracking(
        const std::string& object_id,
        const std::chrono::system_clock::time_point& start_time,
        double lead_time_seconds = 30.0,
        bool wait_at_start = true,
        bool enable_prediction = false,
        double prediction_interval_hours = 1.0,
        const std::string& tracking_mode = "continuous",
        double custom_rate_ra = 0.0,
        double custom_rate_dec = 0.0);
    
    /**
     * @brief Start tracking with ephemeris data (upload and track in one call)
     * @param object_id Object identifier
     * @param object_name Human-readable object name
     * @param object_type Type of object
     * @param points Vector of ephemeris points
     * @param start_time Time to start tracking
     * @param lead_time_seconds Time to start before first point
     * @param interpolation_order Interpolation order
     * @param tracking_mode Tracking mode
     * @return Tracker ID or empty string if failed
     */
    std::string startEphemerisTrackingWithData(
        const std::string& object_id,
        const std::string& object_name,
        const std::string& object_type,
        const std::vector<std::tuple<std::chrono::system_clock::time_point,
                                      double, double, double, double>>& points,
        const std::chrono::system_clock::time_point& start_time,
        double lead_time_seconds = 30.0,
        int interpolation_order = 3,
        const std::string& tracking_mode = "continuous");
    
    /**
     * @brief Stop ephemeris tracking
     * @param tracker_id Tracker identifier
     * @return True if stopped successfully
     */
    bool stopEphemerisTracking(const std::string& tracker_id);
    
    /**
     * @brief Get ephemeris tracking status
     * @param tracker_id Tracker identifier
     * @return Status object with current tracking info
     */
    ::astro_mount::EphemerisTrackStatus getEphemerisTrackStatus(
        const std::string& tracker_id) const;
    
    /**
     * @brief Get all active ephemeris trackers
     * @return Vector of tracker IDs
     */
    std::vector<std::string> getActiveEphemerisTrackers() const;
    
    /**
     * @brief Clear ephemeris cache (remove all uploaded ephemeris data)
     */
    void clearEphemerisCache();
    
    /**
     * @brief Get ephemeris metrics for all trackers
     * @return Metrics object with tracking statistics
     */
    ::astro_mount::EphemerisMetrics getEphemerisMetrics() const;
    
    /**
     * @brief Home mount — set reference position for tracking origin.
     *
     * Sets the internal axis positions to known telescope coordinates.
     * Use after physically pointing the mount at a known celestial object
     * (e.g. Polaris at HA=0h, Dec=+90° for equatorial mounts) to establish
     * a correct coordinate reference.
     *
     * Converts telescope degrees to servo degrees internally using gear_ratio.
     * Stops any active motion before applying the new reference.
     *
     * @param request Proto homing request with axis1/axis2 in telescope degrees
     * @return True if the reference was set successfully
     */
    bool home(const ::astro_mount::MountHomingRequest& request);
    
    /**
     * @brief Get field rotation parameters (computed rates)
     * @return Field rotation parameters
     */
    ::astro_mount::FieldRotationParams getFieldRotationParams() const;
    
    /**
     * @brief Get current HAL configuration
     * @param config [out] Proto HAL configuration message
     * @return True if HAL interface is available and config was retrieved
     */
    bool getHALConfig(::astro_mount::HALConfig& config) const;
    
    /**
     * @brief Update HAL configuration
     * @param request Proto HAL configuration request
     * @return True if configuration was applied successfully
     */
    bool setHALConfig(const ::astro_mount::HALConfigRequest& request);
    
    /**
     * @brief Get HAL status information
     * @param status [out] Proto HAL status message
     * @return True if HAL interface is available and status was retrieved
     */
    bool getHALStatus(::astro_mount::HALStatus& status) const;
    
    /**
     * @brief Reinitialize HAL interface with current or new configuration
     * @param request Reinitialization parameters (force_restart flag)
     * @return True if reinitialization was successful
     */
    bool reinitializeHAL(const ::astro_mount::HALReinitRequest& request);
    
    /**
     * @brief Soft restart: shutdown + reload config from file + reinitialize,
     *        preserving bootstrap and TPOINT calibration data.
     * @return True if restart was successful
     */
    bool restart();
    
    /**
     * @brief Hard restart: shutdown + reload config from file + reinitialize,
     *        discarding all calibration data (bootstrap, TPOINT).
     * @return True if restart was successful
     */
    bool hardRestart();
    
    /**
     * @brief Start the gamepad manual-control loop (axis velocity commands).
     * The gamepad input device must already be open (initGamepadInput).
     * Safe to call only after the HAL interface is fully initialised.
     */
    void startGamepadLoop();
    
    /**
     * @brief Stop the gamepad manual-control loop.
     */
    void stopGamepad();
    
    /**
     * @brief Set the gamepad navigation mode.
     * @param mode GamepadMode (RAW=0, CELESTIAL=1, ALT_AZ=2)
     */
    void setGamepadMode(GamepadMode mode);
    
private:
    class Impl;
    std::unique_ptr<Impl> pimpl;
};

} // namespace controllers
} // namespace astro_mount

#endif // MOUNT_CONTROLLER_H