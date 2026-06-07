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

// Forward declaration for HAL
namespace astro_mount {
namespace hal {
class HALInterface;
} // namespace hal
} // namespace astro_mount

namespace astro_mount {
namespace controllers {

// Forward declarations
class ICanOpenInterface;

/**
 * @brief Main mount controller class
 * 
 * Integrates all components: astronomical calculations, TPOINT model,
 * Kalman filter, encoders, and CanOpen interface.
 */
class MountController {
public:
    enum class MountType {
        EQUATORIAL,
        ALT_AZ,
        UNKNOWN,
        CASUAL   ///< Randomly oriented mount with quaternion orientation
    };

    /**
     * @brief Bootstrap calibration mode
     *
     * Determines how bootstrap measurements are collected:
     * - MANUAL: User manually points at stars (gamepad), adds measurements
     * - HYBRID: First 3 measurements manual, then automatic slews
     * - AUTOMATIC: Fully automatic with plate solver orchestrator
     */
    enum class BootstrapMode {
        BOOTSTRAP_MANUAL = 0,       ///< Fully manual pointing
        BOOTSTRAP_HYBRID = 1,       ///< Manual + automatic after 3 measurements
        BOOTSTRAP_AUTOMATIC = 2     ///< Fully automatic with plate solver
    };

    /**
     * @brief Mount orientation represented as a unit quaternion
     *
     * Describes the rotation from the local horizontal frame (ENU: East, North, Up)
     * to the mount's axis frame. The identity quaternion [1,0,0,0] corresponds to
     * an Alt-Az mount at the equator (or an equatorial mount at the pole).
     *
     * The quaternion is stored as [qx, qy, qz, qw] where qw is the scalar part.
     */
    struct MountOrientation {
        std::array<double, 4> quaternion{0.0, 0.0, 0.0, 1.0};  // [qx, qy, qz, qw] — identity

        /// Check if this orientation is valid (unit quaternion within tolerance)
        bool isValid() const;

        /// Build orientation from axis angles (axis1 altitude, axis1 azimuth)
        void setFromAxisAngles(double axis1_altitude, double axis1_azimuth);

        /// Convert to 3x3 rotation matrix (row-major)
        std::array<double, 9> toRotationMatrix() const;
    };

    enum class TrackingMode {
        SIDEREAL,
        SOLAR,
        LUNAR,
        CUSTOM,
        OFF
    };

    struct AxisPhysicalParameters {
        // Motor parameters
        double motor_steps_per_rev;      // Steps per revolution
        double motor_microstepping;      // Microstepping factor (e.g., 16, 32, 64)
        double motor_step_angle;         // Step angle [arcseconds]
        
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

    struct ControllerConfig {
        // Mount type
        MountType mount_type{MountType::EQUATORIAL};
        
        // Location
        double latitude{0.0};
        double longitude;
        double altitude;
        
        // Mount physical parameters
        double mount_height;
        double pier_west;
        double pier_east;
        
        // Mount parameters
        double max_slew_rate;
        double max_tracking_rate;
        double slew_acceleration;
        double tracking_acceleration;
        double position_tolerance;
        double rate_tolerance;
        
        // Environmental defaults
        double default_temperature;
        double default_pressure;
        double default_humidity;
        
        // Encoder configuration
        bool use_encoders;
        bool encoders_absolute;
        double encoder_resolution;
        
        // Kalman filter parameters
        // process_noise: kinematic model uncertainty (deg/sqrt(s)), default ~0.001° ≈ 3.6"
        // measurement_noise: encoder/position measurement uncertainty (deg), default ~0.001°
        double process_noise{0.001};
        double measurement_noise{0.001};
        
        // TPOINT parameters
        uint32_t tpoint_enabled_terms{0};
        
        // Network configuration
        std::string canopen_interface;
        int canopen_node_id;
        std::string grpc_address;
        int grpc_port;
        
        // Logging configuration
        std::string log_level;
        std::string log_directory;
        int log_rotation_days;
        
        // Telescope parameters
        double focal_length;
        double aperture;
        
        // Guider configuration
        bool enable_guider;
        double guider_max_correction;
        double guider_aggression;
        
        // Meridian flip configuration
        bool meridian_flip_enabled{true};
        double meridian_flip_delay_minutes{5.0};
        double meridian_flip_hysteresis_degrees{0.5};
        double meridian_flip_timeout_seconds{120.0};  ///< Max time for flip slew before ERROR state
        
        // Soft limits
        bool soft_limits_enabled{true};
        double soft_limit_axis1_min{-270.0};
        double soft_limit_axis1_max{270.0};
        double soft_limit_axis2_min{-5.0};
        double soft_limit_axis2_max{185.0};
        double soft_limit_warning_degrees{10.0};       ///< Distance from hard limit to start warning [degrees]
        double soft_limit_deceleration_degrees{5.0};   ///< Distance from hard limit to start deceleration [degrees]
        double soft_limit_tracking_rate_factor{0.1};    ///< Minimum tracking rate factor at hard limit (0..1)
        
        // Park position (configurable, e.g. NCP for equatorial: HA=0°, Dec=90°)
        double park_position_axis1{0.0};   ///< Park target for axis 1 (HA) [degrees]
        double park_position_axis2{90.0};  ///< Park target for axis 2 (Dec) [degrees] — NCP default
        
        // Atmospheric refraction correction
        bool enable_refraction_correction{true};  ///< Apply real-time refraction correction in tracking loop
        
        // Mount orientation (for CASUAL mount type)
        MountOrientation mount_orientation;

        // Axis physical parameters
        AxisPhysicalParameters ha_axis_params;
        AxisPhysicalParameters dec_axis_params;
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
        double axis1_position;      // Degrees
        double axis2_position;      // Degrees
        double axis1_rate;          // Degrees/sec
        double axis2_rate;          // Degrees/sec
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
    bool startTracking(double ra, double dec, TrackingMode mode = TrackingMode::SIDEREAL);

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
    bool setBootstrapMode(BootstrapMode mode);
    
    /**
     * @brief Get current bootstrap calibration mode
     * @return Current BootstrapMode enum value
     */
    BootstrapMode getBootstrapMode() const;
    
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
     * @brief Get bootstrap calibration RMS residual in RA
     * @return RMS residual in arcseconds, 0.0 if not calibrated
     */
    double getBootstrapRmsRaArcsec() const;
    
    /**
     * @brief Get bootstrap calibration RMS residual in Dec
     * @return RMS residual in arcseconds, 0.0 if not calibrated
     */
    double getBootstrapRmsDecArcsec() const;
    
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
     * @brief Add measurement for full TPOINT calibration (simplified version)
     * 
     * Version without mount position for backward compatibility.
     * Uses current mount position from encoders.
     * 
     * @param observed_ra Observed RA in hours
     * @param observed_dec Observed Dec in degrees
     * @param expected_ra Expected RA in hours
     * @param expected_dec Expected Dec in degrees
     * @param temperature Temperature in Celsius
     * @param pressure Pressure in hPa
     * @param humidity Relative humidity 0-1
     * @return True if measurement accepted
     */
    bool addTPointMeasurement(double observed_ra, double observed_dec,
                              double expected_ra, double expected_dec,
                              double temperature = 15.0, double pressure = 1013.25,
                              double humidity = 0.5);
    
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
     * @brief Add calibration measurement (legacy API for backward compatibility)
     * 
     * This method is kept for backward compatibility. Internally calls
     * addTPointMeasurement with all parameters.
     * 
     * @param observed_ra Observed RA in hours
     * @param observed_dec Observed Dec in degrees
     * @param expected_ra Expected RA in hours (from catalog)
     * @param expected_dec Expected Dec in degrees (from catalog)
     * @param mount_ha Mount hour angle in hours (from encoders)
     * @param mount_dec Mount declination in degrees (from encoders)
     * @param temperature Temperature in Celsius
     * @param pressure Pressure in hPa
     * @param humidity Relative humidity 0-1
     * @param proper_motion_ra Proper motion in RA (mas/yr)
     * @param proper_motion_dec Proper motion in Dec (mas/yr)
     * @param parallax Parallax in mas
     * @param epoch Epoch of coordinates (e.g., 2000.0)
     * @return True if measurement accepted
     */
    bool addCalibrationMeasurement(double observed_ra, double observed_dec,
                                   double expected_ra, double expected_dec,
                                   double mount_ha, double mount_dec,
                                   double temperature, double pressure,
                                   double humidity,
                                   double proper_motion_ra, double proper_motion_dec,
                                   double parallax, double epoch);

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
     * @brief Update controller configuration
     * @param config New configuration
     * @return True if update successful
     */
    bool updateConfiguration(const ControllerConfig& config);

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
    bool setMountOrientation(const MountOrientation& orientation);
    
    /**
     * @brief Get current mount orientation
     * @return Current mount orientation
     */
    MountOrientation getMountOrientation() const;

    /**
     * @brief Get CanOpen interface reference
     * @return Reference to ICanOpenInterface (abstract interface)
     */
    ICanOpenInterface& getCanOpenInterface();
    
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
    
    // ============================================
    // FIELD ROTATION / DEROTATOR CONTROL
    // ============================================
    
    /**
     * @brief Configure derotator hardware
     * @param config Derotator configuration
     * @return True if configuration successful
     */
    bool configureDerotator(const ::astro_mount::DerotatorConfig& config);
    
    /**
     * @brief Enable or disable field rotation compensation
     * @param params Field rotation parameters
     * @return True if operation successful
     */
    bool enableFieldRotation(const ::astro_mount::FieldRotationParams& params);
    
    /**
     * @brief Control field rotation (position/rate control)
     * @param request Field rotation control request
     * @return True if operation successful
     */
    bool controlFieldRotation(const ::astro_mount::FieldRotationControlRequest& request);
    
    /**
     * @brief Get derotator status
     * @return Derotator status object
     */
    ::astro_mount::DerotatorStatus getDerotatorStatus() const;
    
    /**
     * @brief Home derotator (find zero position)
     * @param request Homing request parameters
     * @return True if homing successful
     */
    bool homeDerotator(const ::astro_mount::DerotatorHomingRequest& request);
    
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
    
private:
    class Impl;
    std::unique_ptr<Impl> pimpl;
};

} // namespace controllers
} // namespace astro_mount

#endif // MOUNT_CONTROLLER_H