#ifndef TPOINT_MODEL_H
#define TPOINT_MODEL_H

#include <vector>
#include <memory>
#include <chrono>
#include <map>
#include <string>
#include <Eigen/Dense>

namespace astro_mount {
namespace models {

/**
 * @brief TPOINT model for mount and telescope geometry errors
 * 
 * Implements the full TPOINT model for correcting mount geometry errors,
 * including polar misalignment, non-perpendicularity, flexure, and tube geometry.
 */
class TPointModel {
public:
    struct Measurement {
        // Observed coordinates (from image/encoder) - REQUIRED
        double observed_ra;      // Observed RA in hours
        double observed_dec;     // Observed Dec in degrees
        
        // Expected coordinates (from catalog) - REQUIRED
        double expected_ra;      // Expected RA in hours  
        double expected_dec;     // Expected Dec in degrees
        
        // Mount position - REQUIRED for TPOINT model
        double mount_ha;         // Mount hour angle in hours
        double mount_dec;        // Mount declination in degrees
        
        // Environmental conditions - USED for refraction modeling
        double temperature;      // Temperature in Celsius
        double pressure;         // Pressure in hPa
        double humidity;         // Relative humidity (0-1)
        
        // Quality metrics - USED for weighted fitting
        double snr;              // Signal-to-noise ratio
        double seeing;           // Seeing in arcseconds
        
        // Time information - USED for proper motion correction
        std::chrono::system_clock::time_point timestamp;
        double julian_date;      // Julian date of observation
        
        // Proper motion - USED for precise expected coordinates
        double proper_motion_ra; // Proper motion in RA (mas/yr)
        double proper_motion_dec;// Proper motion in Dec (mas/yr)
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

    struct TPointParameters {
        // Polar alignment errors (sub-arcsecond precision)
        double polar_alt_error;      // Polar altitude error [arcseconds]
        double polar_az_error;       // Polar azimuth error [arcseconds]
        
        // Index error (constant offset in RA) and collimation error (cos(ha) in RA)
        double index_error;          // Index error [arcseconds]
        double collimation_error;    // Collimation error [arcseconds]
        
        // Non-perpendicularity with temperature compensation
        double axis_nonperp;         // Axis non-perpendicularity [arcseconds]
        double axis_nonperp_temp_coeff; // Temperature coefficient [arcseconds/°C]
        
        // Tube geometry with harmonic analysis
        double tube_flexure_ha;      // Tube flexure in HA [arcseconds/rad]
        double tube_flexure_dec;     // Tube flexure in Dec [arcseconds/rad]
        double tube_rotation;        // Tube rotation error [arcseconds]
        
        // Mount specific errors with harmonic components
        double worm_period_error;    // Worm period error [arcseconds]
        std::array<double, 10> worm_harmonics; // Harmonic coefficients for worm error (5 harmonics × 2)
        
        // Encoder errors with high-order harmonics
        double encoder_error_ha;     // HA encoder error [arcseconds]
        double encoder_error_dec;    // Dec encoder error [arcseconds]
        std::array<double, 8> encoder_harmonics_ha;  // HA encoder harmonics
        std::array<double, 8> encoder_harmonics_dec; // Dec encoder harmonics
        
        // Atmospheric refraction model with environmental dependence
        double refraction_coeff;     // Refraction coefficient
        double refraction_temp_coeff; // Temperature coefficient
        double refraction_pressure_coeff; // Pressure coefficient
        
        // Temperature-dependent terms
        double temp_flexure_coeff;   // Flexure temperature coefficient [arcseconds/°C]
        double temp_encoder_coeff;   // Encoder temperature coefficient [arcseconds/°C]
        
        // Physical axis parameters
        AxisPhysicalParameters ha_axis_params;  // Hour angle axis parameters
        AxisPhysicalParameters dec_axis_params; // Declination axis parameters
        
        // Statistical measures with uncertainty estimates
        double chi_squared;          // Chi-squared of fit
        double rms_error;            // RMS error [arcseconds]
        double max_error;            // Maximum error [arcseconds]
        double mean_error;           // Mean error [arcseconds]
        int degrees_of_freedom;      // Degrees of freedom
        
        // Covariance matrix (compressed storage)
        std::vector<double> covariance; // Upper triangular covariance matrix
        
        // Quality metrics
        double condition_number;     // Condition number of design matrix
        double parameter_uncertainty_scale; // Scale factor for parameter uncertainties
        
        std::chrono::system_clock::time_point last_update;
        double calibration_temperature; // Temperature during calibration [°C]
    };

    TPointModel();
    ~TPointModel();

    /**
     * @brief Add a measurement to the model
     * @param measurement Measurement data
     */
    void addMeasurement(const Measurement& measurement);

    /**
     * @brief Fit the TPOINT model to all measurements
     * @return True if fit successful
     */
    bool fitModel();

    /**
     * @brief Get current TPOINT parameters
     * @return TPOINT parameters
     */
    TPointParameters getParameters() const;

    /**
     * @brief Apply TPOINT corrections to observed coordinates
     * @param observed_ra Observed RA in hours
     * @param observed_dec Observed Dec in degrees
     * @param mount_ha Mount hour angle in hours
     * @param mount_dec Mount declination in degrees
     * @param temperature Current ambient temperature [°C] (default 20.0)
     * @param load_torque Current load torque on axis [Nm] (default 0.0)
     * @param direction_changed True if mount direction changed since last iteration
     * @return Corrected coordinates (ra, dec)
     */
    std::pair<double, double> applyCorrections(double observed_ra, double observed_dec,
                                               double mount_ha, double mount_dec,
                                               double temperature = 20.0,
                                               double load_torque = 0.0,
                                               bool direction_changed = false);

    /**
     * @brief Predict mount position for given celestial coordinates
     * @param ra Right ascension in hours
     * @param dec Declination in degrees
     * @return Required mount positions (ha, dec)
     */
    std::pair<double, double> predictMountPosition(double ra, double dec);

    /**
     * @brief Calculate residual error for a measurement
     * @param measurement Measurement to evaluate
     * @return Residual error in arcseconds
     */
    double calculateResidual(const Measurement& measurement) const;

    /**
     * @brief Get all measurement residuals
     * @return Vector of residuals in arcseconds
     */
    std::vector<double> getAllResiduals() const;

    /**
     * @brief Clear all measurements
     */
    void clearMeasurements();

    /**
     * @brief Get number of measurements
     * @return Number of measurements
     */
    size_t getMeasurementCount() const;

    /**
     * @brief Set mount parameters
     * @param mount_height Mount height in meters
     * @param pier_west Pier west offset in meters
     * @param pier_east Pier east offset in meters
     */
    void setMountParameters(double mount_height, double pier_west, double pier_east);

    /**
     * @brief Set telescope parameters
     * @param focal_length Focal length in mm
     * @param aperture Aperture in mm
     * @param tube_length Tube length in mm
     */
    void setTelescopeParameters(double focal_length, double aperture, double tube_length);

    /**
     * @brief Enable/disable specific error terms
     * @param term_mask Bitmask of error terms to enable
     */
    void setEnabledTerms(uint32_t term_mask);

    /**
     * @brief Save model to file
     * @param filename File to save to
     * @return True if save successful
     */
    bool saveToFile(const std::string& filename) const;

    /**
     * @brief Load model from file
     * @param filename File to load from
     * @return True if load successful
     */
    bool loadFromFile(const std::string& filename);

    /**
     * @brief Get covariance matrix
     * @return Covariance matrix of parameters
     */
    Eigen::MatrixXd getCovarianceMatrix() const;

    /**
     * @brief Get parameter uncertainties
     * @return Vector of parameter uncertainties
     */
    std::vector<double> getParameterUncertainties() const;

    /**
     * @brief Calculate model quality metrics
     * @return Map of quality metrics
     */
    std::map<std::string, double> calculateQualityMetrics() const;

private:
    class Impl;
    std::unique_ptr<Impl> pimpl;
};

// Error term bitmask definitions
namespace TPointTerms {
    constexpr uint32_t INDEX_ERROR = 1 << 0;
    constexpr uint32_t COLLIMATION_ERROR = 1 << 1;
    constexpr uint32_t AXIS_NONPERP = 1 << 2;
    constexpr uint32_t POLAR_ALT = 1 << 3;
    constexpr uint32_t POLAR_AZ = 1 << 4;
    constexpr uint32_t TUBE_FLEXURE_HA = 1 << 5;
    constexpr uint32_t TUBE_FLEXURE_DEC = 1 << 6;
    constexpr uint32_t TUBE_ROTATION = 1 << 7;
    constexpr uint32_t WORM_ERROR = 1 << 8;
    constexpr uint32_t ENCODER_ERROR_HA = 1 << 9;
    constexpr uint32_t ENCODER_ERROR_DEC = 1 << 10;
    constexpr uint32_t REFRACTION = 1 << 11;
    
    constexpr uint32_t ALL_TERMS = 0xFFFFFFFF;
    constexpr uint32_t DEFAULT_TERMS = INDEX_ERROR | COLLIMATION_ERROR | AXIS_NONPERP | 
                                       POLAR_ALT | POLAR_AZ | TUBE_FLEXURE_HA | 
                                       TUBE_FLEXURE_DEC | REFRACTION;
}

} // namespace models
} // namespace astro_mount

#endif // TPOINT_MODEL_H