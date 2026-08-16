#ifndef MOUNT_CONFIG_H
#define MOUNT_CONFIG_H

#include <array>
#include <string>
#include <vector>
#include <cstdint>

namespace astro_mount {
namespace config {

/**
 * @brief Per-axis physical parameters
 *
 * Describes the mechanical properties of a single mount axis,
 * including CANopen scaling, encoder resolution, gear ratios,
 * and calibration data.
 */
struct AxisPhysicalParameters {
    // CANopen scaling factors (per-axis)
    double position_counts_per_degree{4000.0 / 360.0};
    double velocity_counts_per_deg_s{4000.0 / 360.0};

    // Encoder parameters
    double encoder_resolution{1000.0};      // Encoder resolution [counts/rev]
    double encoder_counts_per_arcsec{0.0};  // Counts per arcsecond
    double encoder_quantization_error{0.0}; // Quantization error [arcseconds]
    
    // Gear parameters
    double gear_ratio{360.0};               // Total gear ratio (motor:output)
    double worm_ratio{180.0};               // Worm gear ratio (if applicable)
    int worm_teeth{1};                      // Number of worm teeth
    int worm_wheel_teeth{180};              // Number of worm wheel teeth
    
    // Cyclic errors (periodic errors)
    double cyclic_error_amplitude{0.0};     // Amplitude of cyclic error [arcseconds]
    double cyclic_error_period{360.0};      // Period of cyclic error [degrees]
    std::array<double, 8> cyclic_harmonics{}; // Harmonic coefficients for cyclic error
    
    // Backlash parameters
    double backlash{0.0};                   // Backlash [arcseconds]
    double backlash_temp_coeff{0.0};        // Backlash temperature coefficient [arcseconds/°C]
    
    // Stiffness and compliance
    double axis_stiffness{0.0};             // Axis stiffness [arcseconds/Nm]
    double torsional_compliance{0.0};       // Torsional compliance [rad/Nm]
    
    // Temperature coefficients
    double expansion_coeff{0.0};            // Thermal expansion coefficient [1/°C]
    double temp_gear_error_coeff{0.0};      // Gear error temperature coefficient [arcseconds/°C]
    
    // Calibration data
    std::vector<double> calibration_table;  // Calibration table [counts → arcseconds]
    double calibration_temp{20.0};           // Temperature during calibration [°C]
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

/**
 * @brief Mount type enumeration
 */
enum class MountType {
    EQUATORIAL,
    ALT_AZ,
    UNKNOWN,
    CASUAL   ///< Randomly oriented mount with quaternion orientation
};

/**
 * @brief Pure mount parameters — type, location, rates, encoders, axis physics
 *
 * Contains only parameters that describe the physical mount hardware
 * and its configuration. Does NOT include tracking, safety, or calibration
 * settings (those belong in separate domain configs).
 */
struct MountConfig {
    // Mount type
    MountType mount_type{MountType::EQUATORIAL};
    
    // Location
    double latitude{0.0};
    double longitude{0.0};
    double altitude{0.0};
    
    // Mount physical parameters (TPOINT model — R5). mount_height is the pier /
    // tripod height above ground [m] (used for refraction scaling); pier_west and
    // pier_east are the pier-side indicators/heights — the larger of the two
    // selects the active pier, which flips the sign of the axis non-perpendicularity
    // (AN) Dec term.
    double mount_height{0.0};
    double pier_west{0.0};
    double pier_east{0.0};
    
    // Mount parameters
    double max_slew_rate{5.0};
    double max_tracking_rate{1.0};
    double slew_acceleration{2.0};
    double tracking_acceleration{0.5};
    double position_tolerance{0.1};
    double rate_tolerance{0.01};
    
    // Environmental defaults
    double default_temperature{15.0};
    double default_pressure{1013.25};
    double default_humidity{50.0};
    
    // Encoder configuration
    bool use_encoders{false};
    bool encoders_absolute{false};
    double encoder_resolution{1000.0};
    
    // Kalman filter parameters
    // process_noise: kinematic model uncertainty (deg/sqrt(s)), default ~0.001° ≈ 3.6"
    // measurement_noise: encoder/position measurement uncertainty (deg), default ~0.001°
    double process_noise{0.001};
    double measurement_noise{0.001};
    
    // Equatorial tracking mode: false = Profile Position (default, stable),
    // true = Profile Velocity (experimental — 0x606C reports incorrect velocity
    // on some hardware, causing the velocity PID to misbehave).
    bool equatorial_tracking_velocity_mode{false};

    // Per-axis rotation direction inversion.
    // When true, the motor target position/velocity for this axis is negated,
    // reversing the physical rotation direction relative to the computed target.
    // Use this when the telescope/motor wiring causes reversed axis movement.
    bool invert_axis1{false};
    bool invert_axis2{false};
    
    // Mount orientation (for CASUAL mount type)
    MountOrientation mount_orientation;

    // Axis physical parameters
    AxisPhysicalParameters ha_axis_params;
    AxisPhysicalParameters dec_axis_params;
};

} // namespace config
} // namespace astro_mount

#endif // MOUNT_CONFIG_H
