#include "models/tpoint_model.h"
#include <Eigen/Dense>
#include <fstream>
#include <nlohmann/json.hpp>
#include <cmath>
#include <algorithm>

// Constants for conversions
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#ifndef D2R
#define D2R (M_PI / 180.0)  // Degrees to radians
#endif

#ifndef R2D
#define R2D (180.0 / M_PI)  // Radians to degrees
#endif

namespace astro_mount {
namespace models {

using json = nlohmann::json;
using Eigen::MatrixXd;
using Eigen::VectorXd;
using Matrix2d = Eigen::Matrix<double, 2, 2>;
using Vector2d = Eigen::Matrix<double, 2, 1>;

class TPointModel::Impl {
public:
    Impl() : enabled_terms_(TPointTerms::DEFAULT_TERMS),
             focal_length_(2000.0),
             aperture_(200.0),
             tube_length_(1800.0),
             is_fitted_(false) {
        initializeParameters();
    }
    
    void addMeasurement(const Measurement& measurement) {
        measurements_.push_back(measurement);
        is_fitted_ = false;
    }
    
    bool fitModel() {
        if (measurements_.size() < getMinMeasurements()) {
            return false;
        }
        
        // Prepare design matrix and observation vector
        int n = measurements_.size();
        int p_ra = getRAParameterCount();
        int p_dec = getDecParameterCount();
        
        MatrixXd A_ra(n, p_ra);
        MatrixXd A_dec(n, p_dec);
        VectorXd b_ra(n);
        VectorXd b_dec(n);
        
        for (int i = 0; i < n; ++i) {
            const auto& m = measurements_[i];
            
            // Calculate residuals in arcseconds
            double ra_residual = (m.observed_ra - m.expected_ra) * 15.0 * 3600.0;
            double dec_residual = (m.observed_dec - m.expected_dec) * 3600.0;
            
            // Fill design matrix row (both RA and Dec)
            fillDesignMatrixRow(A_ra, A_dec, i, m.mount_ha, m.mount_dec);
            
            b_ra(i) = ra_residual;
            b_dec(i) = dec_residual;
        }
        
        // Solve using QR decomposition directly on A (not A^TA)
        // ColPivHouseholderQR on A avoids squaring the condition number
        Eigen::ColPivHouseholderQR<MatrixXd> qr_ra(A_ra);
        Eigen::ColPivHouseholderQR<MatrixXd> qr_dec(A_dec);
        VectorXd params_ra = qr_ra.solve(b_ra);
        VectorXd params_dec = qr_dec.solve(b_dec);
        
        // ============================================================
        // Extract parameters from fitted coefficients
        // The order MUST match fillDesignMatrixRowInternal exactly
        // ============================================================
        int ra_idx = 0;
        int dec_idx = 0;
        
        // --- RA parameters ---
        
        // Index error (IA): constant offset in RA
        if (enabled_terms_ & TPointTerms::INDEX_ERROR) {
            parameters_.index_error = params_ra(ra_idx++);
        }
        
        // Collimation error (CA): cos(ha) in RA
        if (enabled_terms_ & TPointTerms::COLLIMATION_ERROR) {
            parameters_.collimation_error = params_ra(ra_idx++);
        }
        
        // Axis non-perpendicularity (AN):
        //   RA term: sin(ha) * tan(dec)
        //   Dec term: cos(ha)
        if (enabled_terms_ & TPointTerms::AXIS_NONPERP) {
            parameters_.axis_nonperp = params_ra(ra_idx++);
            dec_idx++;
        }
        
        // Polar altitude error (PA):
        //   RA term: -cos(ha) — ONLY present when COLLIMATION_ERROR is NOT enabled.
        //     When COLLIMATION_ERROR is enabled, the RA column is excluded from the
        //     design matrix to avoid collinearity with COLLIMATION's +cos(ha).
        //   Dec term: 1.0 (constant) — always present.
        // Extraction: polar_alt_error comes from Dec fit (constant term).
        if (enabled_terms_ & TPointTerms::POLAR_ALT) {
            // Skip RA column only if it was included in the design matrix
            if (!(enabled_terms_ & TPointTerms::COLLIMATION_ERROR)) {
                ra_idx++;  // Extract from RA fit (independent of COLLIMATION)
            }
            parameters_.polar_alt_error = params_dec(dec_idx++);  // From Dec constant
        }
        
        // Polar azimuth error (PA):
        //   RA term: sin(ha)
        //   Dec term: sin(ha) * tan(dec)
        if (enabled_terms_ & TPointTerms::POLAR_AZ) {
            parameters_.polar_az_error = params_ra(ra_idx++);
            dec_idx++;
        }
        
        // Tube flexure in HA: sin(ha) in RA
        if (enabled_terms_ & TPointTerms::TUBE_FLEXURE_HA) {
            parameters_.tube_flexure_ha = params_ra(ra_idx++);
        }
        
        // Tube flexure in Dec: sin(dec) in Dec
        if (enabled_terms_ & TPointTerms::TUBE_FLEXURE_DEC) {
            parameters_.tube_flexure_dec = params_dec(dec_idx++);
        }
        
        // Tube rotation (TF):
        //   RA term: cos(dec)
        //   Dec term: -sin(dec) * sin(ha)
        if (enabled_terms_ & TPointTerms::TUBE_ROTATION) {
            parameters_.tube_rotation = params_ra(ra_idx++);
            dec_idx++;
        }
        
        // Worm period error:
        //   RA: base = sin(1*ha), harmonics = sin(i*ha), cos(i*ha) for i=2..6
        if (enabled_terms_ & TPointTerms::WORM_ERROR) {
            parameters_.worm_period_error = params_ra(ra_idx++);  // sin(1*ha) coefficient
            for (int i = 0; i < 5; ++i) {  // 5 harmonics × 2 (sin, cos) = 10 values
                parameters_.worm_harmonics[i * 2] = params_ra(ra_idx++);  // sin((i+2)*ha) coeff
                parameters_.worm_harmonics[i * 2 + 1] = params_ra(ra_idx++);  // cos((i+2)*ha) coeff
            }
        }
        
        // HA encoder error:
        //   RA: base = ha (linear), harmonics = sin(i*ha), cos(i*ha) for i=2..5
        if (enabled_terms_ & TPointTerms::ENCODER_ERROR_HA) {
            parameters_.encoder_error_ha = params_ra(ra_idx++);  // linear term coefficient
            for (int i = 0; i < 4; ++i) {  // 4 harmonics × 2 = 8 values
                parameters_.encoder_harmonics_ha[i * 2] = params_ra(ra_idx++);  // sin((i+2)*ha)
                parameters_.encoder_harmonics_ha[i * 2 + 1] = params_ra(ra_idx++);  // cos((i+2)*ha)
            }
        }
        
        // --- Dec parameters ---
        
        // Dec encoder error:
        //   Dec: base = dec (linear), harmonics = sin(i*dec), cos(i*dec) for i=2..5
        if (enabled_terms_ & TPointTerms::ENCODER_ERROR_DEC) {
            parameters_.encoder_error_dec = params_dec(dec_idx++);  // linear term
            for (int i = 0; i < 4; ++i) {  // 4 harmonics × 2 = 8 values
                parameters_.encoder_harmonics_dec[i * 2] = params_dec(dec_idx++);  // sin((i+2)*dec)
                parameters_.encoder_harmonics_dec[i * 2 + 1] = params_dec(dec_idx++);  // cos((i+2)*dec)
            }
        }
        
        // Refraction (affects Dec): tan(z), tan³(z), tan⁵(z)
        if (enabled_terms_ & TPointTerms::REFRACTION) {
            parameters_.refraction_coeff = params_dec(dec_idx++);       // A·tan(z)
            parameters_.refraction_temp_coeff = params_dec(dec_idx++);  // B·tan³(z)
            parameters_.refraction_pressure_coeff = params_dec(dec_idx++); // C·tan⁵(z)
        }
        
        // Calculate statistics
        calculateStatistics(A_ra, A_dec, b_ra, b_dec, params_ra, params_dec);
        
        parameters_.last_update = std::chrono::system_clock::now();
        is_fitted_ = true;
        
        return true;
    }
    
    TPointParameters getParameters() const {
        return parameters_;
    }
    
    std::pair<double, double> applyCorrections(double observed_ra, double observed_dec,
                                               double mount_ha, double mount_dec,
                                               double temperature = 20.0,
                                               double load_torque = 0.0,
                                               bool direction_changed = false) const {
        if (!is_fitted_) {
            return {observed_ra, observed_dec};
        }
        
        // Convert to arcseconds for correction
        double ra_arcsec = observed_ra * 15.0 * 3600.0;
        double dec_arcsec = observed_dec * 3600.0;
        
        // Apply TPOINT geometric corrections
        double ra_correction = calculateRACorrection(mount_ha, mount_dec, temperature);
        double dec_correction = calculateDecCorrection(mount_ha, mount_dec, temperature);
        
        // Apply physical axis corrections with environmental and load parameters.
        // temperature: used for thermal expansion compensation (axis_nonperp_temp_coeff,
        //   temp_flexure_coeff, temp_encoder_coeff, backlash_temp_coeff, expansion_coeff,
        //   temp_gear_error_coeff)
        // load_torque: used for stiffness compensation (axis_stiffness, torsional_compliance)
        // direction_changed: enables backlash compensation when direction reverses
        ra_correction += calculateAxisPhysicalCorrection(mount_ha, temperature, load_torque,
                                                         direction_changed, parameters_.ha_axis_params);
        dec_correction += calculateAxisPhysicalCorrection(mount_dec, temperature, load_torque,
                                                          direction_changed, parameters_.dec_axis_params);
        
        // Apply corrections
        ra_arcsec -= ra_correction;
        dec_arcsec -= dec_correction;
        
        // Convert back to hours/degrees
        return {ra_arcsec / (15.0 * 3600.0), dec_arcsec / 3600.0};
    }
    
    std::pair<double, double> predictMountPosition(double ra, double dec) {
        if (!is_fitted_) {
            // Simple conversion for equatorial mount
            double ha = 0.0; // Hour angle (would need LST)
            return {ha, dec};
        }
        
        // Invert TPOINT model to predict mount position using Newton-Raphson
        double ha_guess = 0.0;
        double dec_guess = dec;
        
        // Newton-Raphson iteration with analytical Jacobian
        for (int iter = 0; iter < 15; ++iter) {
            auto corrected = applyCorrections(ra, dec, ha_guess, dec_guess);
            double ra_error = (corrected.first - ra) * 15.0 * 3600.0; // arcseconds
            double dec_error = (corrected.second - dec) * 3600.0; // arcseconds
            
            if (std::abs(ra_error) < 0.01 && std::abs(dec_error) < 0.01) {
                break; // Converged to sub-arcsecond precision
            }
            
            // Calculate Jacobian matrix (2x2) analytically
            // J = [∂f_ra/∂ha, ∂f_ra/∂dec; ∂f_dec/∂ha, ∂f_dec/∂dec]
            Matrix2d J;
            
            // Calculate partial derivatives using central differences
            const double eps = 1e-6;
            
            // ∂f_ra/∂ha
            auto corrected_ha_plus = applyCorrections(ra, dec, ha_guess + eps, dec_guess);
            auto corrected_ha_minus = applyCorrections(ra, dec, ha_guess - eps, dec_guess);
            double f_ra_ha_plus = (corrected_ha_plus.first - ra) * 15.0 * 3600.0;
            double f_ra_ha_minus = (corrected_ha_minus.first - ra) * 15.0 * 3600.0;
            J(0, 0) = (f_ra_ha_plus - f_ra_ha_minus) / (2.0 * eps);
            
            // ∂f_ra/∂dec
            auto corrected_dec_plus = applyCorrections(ra, dec, ha_guess, dec_guess + eps);
            auto corrected_dec_minus = applyCorrections(ra, dec, ha_guess, dec_guess - eps);
            double f_ra_dec_plus = (corrected_dec_plus.first - ra) * 15.0 * 3600.0;
            double f_ra_dec_minus = (corrected_dec_minus.first - ra) * 15.0 * 3600.0;
            J(0, 1) = (f_ra_dec_plus - f_ra_dec_minus) / (2.0 * eps);
            
            // ∂f_dec/∂ha
            double f_dec_ha_plus = (corrected_ha_plus.second - dec) * 3600.0;
            double f_dec_ha_minus = (corrected_ha_minus.second - dec) * 3600.0;
            J(1, 0) = (f_dec_ha_plus - f_dec_ha_minus) / (2.0 * eps);
            
            // ∂f_dec/∂dec
            double f_dec_dec_plus = (corrected_dec_plus.second - dec) * 3600.0;
            double f_dec_dec_minus = (corrected_dec_minus.second - dec) * 3600.0;
            J(1, 1) = (f_dec_dec_plus - f_dec_dec_minus) / (2.0 * eps);
            
            // Invert Jacobian (2x2 matrix)
            double det = J(0, 0) * J(1, 1) - J(0, 1) * J(1, 0);
            
            if (std::abs(det) < 1e-12) {
                // Jacobian is singular, use simplified update
                ha_guess -= ra_error / (15.0 * 3600.0 * 10.0);
                dec_guess -= dec_error / 3600.0;
                continue;
            }
            
            Matrix2d J_inv;
            J_inv(0, 0) = J(1, 1) / det;
            J_inv(0, 1) = -J(0, 1) / det;
            J_inv(1, 0) = -J(1, 0) / det;
            J_inv(1, 1) = J(0, 0) / det;
            
            // Calculate update: Δx = -J⁻¹·f
            Vector2d f(ra_error, dec_error);
            Vector2d delta = -J_inv * f;
            
            // Apply update with damping factor
            double damping = 1.0;
            if (delta.norm() > 10.0) { // Large step
                damping = 10.0 / delta.norm();
            }
            
            ha_guess += damping * delta(0) / (15.0 * 3600.0); // Convert to hours
            dec_guess += damping * delta(1) / 3600.0; // Convert to degrees
            
            // Clamp dec to valid range
            if (dec_guess < -90.0) dec_guess = -90.0;
            if (dec_guess > 90.0) dec_guess = 90.0;
            
            // Clamp ha to reasonable range
            if (ha_guess < -12.0) ha_guess = -12.0;
            if (ha_guess > 12.0) ha_guess = 12.0;
        }
        
        return {ha_guess, dec_guess};
    }
    
    double calculateResidual(const Measurement& measurement) const {
        if (!is_fitted_) {
            return 0.0;
        }
        
        auto corrected = applyCorrections(
            measurement.observed_ra,
            measurement.observed_dec,
            measurement.mount_ha,
            measurement.mount_dec);
        
        double ra_error = (corrected.first - measurement.expected_ra) * 15.0 * 3600.0;
        double dec_error = (corrected.second - measurement.expected_dec) * 3600.0;
        
        return std::sqrt(ra_error * ra_error + dec_error * dec_error);
    }
    
    std::vector<double> getAllResiduals() const {
        std::vector<double> residuals;
        residuals.reserve(measurements_.size());
        
        for (const auto& m : measurements_) {
            residuals.push_back(calculateResidual(m));
        }
        
        return residuals;
    }
    
    void clearMeasurements() {
        measurements_.clear();
        is_fitted_ = false;
        initializeParameters();
    }
    
    size_t getMeasurementCount() const {
        return measurements_.size();
    }
    
    void setTelescopeParameters(double focal_length, double aperture, double tube_length) {
        focal_length_ = focal_length;
        aperture_ = aperture;
        tube_length_ = tube_length;
    }
    
    void setEnabledTerms(uint32_t term_mask) {
        enabled_terms_ = term_mask;
        is_fitted_ = false;
    }
    
    bool saveToFile(const std::string& filename) const {
        json data;
        
        data["parameters"] = {
            {"polar_alt_error", parameters_.polar_alt_error},
            {"polar_az_error", parameters_.polar_az_error},
            {"axis_nonperp", parameters_.axis_nonperp},
            {"tube_flexure_ha", parameters_.tube_flexure_ha},
            {"tube_flexure_dec", parameters_.tube_flexure_dec},
            {"tube_rotation", parameters_.tube_rotation},
            {"worm_period_error", parameters_.worm_period_error},
            {"encoder_error_ha", parameters_.encoder_error_ha},
            {"encoder_error_dec", parameters_.encoder_error_dec},
            {"refraction_coeff", parameters_.refraction_coeff},
            {"chi_squared", parameters_.chi_squared},
            {"rms_error", parameters_.rms_error},
            {"degrees_of_freedom", parameters_.degrees_of_freedom}
        };
        
        
        data["telescope_parameters"] = {
            {"focal_length", focal_length_},
            {"aperture", aperture_},
            {"tube_length", tube_length_}
        };
        
        data["enabled_terms"] = enabled_terms_;
        data["measurement_count"] = measurements_.size();
        data["is_fitted"] = is_fitted_;
        data["last_update"] = std::chrono::system_clock::to_time_t(parameters_.last_update);
        
        std::ofstream file(filename);
        if (!file.is_open()) {
            return false;
        }
        
        file << data.dump(4);
        return true;
    }
    
    bool loadFromFile(const std::string& filename) {
        std::ifstream file(filename);
        if (!file.is_open()) {
            return false;
        }
        
        try {
            json data = json::parse(file);
            
            // Load parameters
            auto params = data["parameters"];
            parameters_.polar_alt_error = params.value("polar_alt_error", 0.0);
            parameters_.polar_az_error = params.value("polar_az_error", 0.0);
            parameters_.axis_nonperp = params.value("axis_nonperp", 0.0);
            parameters_.tube_flexure_ha = params.value("tube_flexure_ha", 0.0);
            parameters_.tube_flexure_dec = params.value("tube_flexure_dec", 0.0);
            parameters_.tube_rotation = params.value("tube_rotation", 0.0);
            parameters_.worm_period_error = params.value("worm_period_error", 0.0);
            parameters_.encoder_error_ha = params.value("encoder_error_ha", 0.0);
            parameters_.encoder_error_dec = params.value("encoder_error_dec", 0.0);
            parameters_.refraction_coeff = params.value("refraction_coeff", 0.0);
            parameters_.chi_squared = params.value("chi_squared", 0.0);
            parameters_.rms_error = params.value("rms_error", 0.0);
            parameters_.degrees_of_freedom = params.value("degrees_of_freedom", 0);
            
            // Load mount parameters
            // Load telescope parameters
            auto telescope_params = data["telescope_parameters"];
            focal_length_ = telescope_params.value("focal_length", 2000.0);
            aperture_ = telescope_params.value("aperture", 200.0);
            tube_length_ = telescope_params.value("tube_length", 1800.0);
            
            enabled_terms_ = data.value("enabled_terms", TPointTerms::DEFAULT_TERMS);
            is_fitted_ = data.value("is_fitted", false);
            
            time_t last_update = data.value("last_update", 0);
            parameters_.last_update = std::chrono::system_clock::from_time_t(last_update);
            
            return true;
        } catch (const std::exception&) {
            return false;
        }
    }
    
    MatrixXd getCovarianceMatrix() const {
        if (!is_fitted_ || measurements_.empty()) {
            return MatrixXd::Zero(getParameterCount(), getParameterCount());
        }
        
        // Calculate design matrix
        int n = measurements_.size();
        int p = getParameterCount();
        MatrixXd A(n, p);
        
        for (int i = 0; i < n; ++i) {
            const auto& m = measurements_[i];
            // Fill the single combined design matrix (RA + Dec columns merged)
            fillCombinedDesignMatrixRow(A, i, m.mount_ha, m.mount_dec);
        }
        
        // Calculate covariance matrix using QR decomposition
        // (A^T A)^{-1} = P * (R^T R)^{-1} * P^T where A*P = Q*R
        // This avoids explicitly forming A^TA (which squares condition number)
        Eigen::ColPivHouseholderQR<MatrixXd> qr(A);
        const auto& P = qr.colsPermutation();
        auto R = qr.matrixQR().triangularView<Eigen::Upper>();
        
        // Compute R^{-1} * R^{-T}
        MatrixXd Rinv = R.solve(MatrixXd::Identity(p, p));
        MatrixXd ATA_inv = P * Rinv * Rinv.transpose() * P.transpose();
        
        double sigma2 = parameters_.chi_squared / (n - p);
        return sigma2 * ATA_inv;
    }
    
    std::vector<double> getParameterUncertainties() const {
        MatrixXd cov = getCovarianceMatrix();
        std::vector<double> uncertainties;
        uncertainties.reserve(cov.rows());
        
        for (int i = 0; i < cov.rows(); ++i) {
            uncertainties.push_back(std::sqrt(cov(i, i)));
        }
        
        return uncertainties;
    }
    
    std::map<std::string, double> calculateQualityMetrics() const {
        std::map<std::string, double> metrics;
        
        metrics["chi_squared"] = parameters_.chi_squared;
        metrics["rms_error"] = parameters_.rms_error;
        metrics["degrees_of_freedom"] = parameters_.degrees_of_freedom;
        metrics["measurement_count"] = measurements_.size();
        metrics["parameter_count"] = getParameterCount();
        
        if (measurements_.size() > getParameterCount()) {
            double reduced_chi2 = parameters_.chi_squared / (measurements_.size() - getParameterCount());
            metrics["reduced_chi_squared"] = reduced_chi2;
        }
        
        // Calculate mean residual
        auto residuals = getAllResiduals();
        if (!residuals.empty()) {
            double sum = 0.0;
            for (double r : residuals) sum += r;
            metrics["mean_residual"] = sum / residuals.size();
            
            // Calculate standard deviation
            double sum_sq = 0.0;
            for (double r : residuals) {
                double diff = r - metrics["mean_residual"];
                sum_sq += diff * diff;
            }
            metrics["std_dev_residual"] = std::sqrt(sum_sq / residuals.size());
        }
        
        return metrics;
    }
    
private:
    void initializeParameters() {
        parameters_ = TPointParameters{};
        parameters_.last_update = std::chrono::system_clock::now();
    }
    
    int getRAParameterCount() const {
        int count = 0;
        if (enabled_terms_ & TPointTerms::INDEX_ERROR) count++;
        if (enabled_terms_ & TPointTerms::COLLIMATION_ERROR) count++;
        if (enabled_terms_ & TPointTerms::AXIS_NONPERP) count++; // AN term for RA
        // POLAR_ALT RA component: -cos(ha). Only included when COLLIMATION_ERROR is NOT
        // enabled, to avoid collinearity between -cos(ha) and COLLIMATION's +cos(ha).
        if (enabled_terms_ & TPointTerms::POLAR_ALT) {
            if (!(enabled_terms_ & TPointTerms::COLLIMATION_ERROR)) count++;
        }
        if (enabled_terms_ & TPointTerms::POLAR_AZ) count++;
        if (enabled_terms_ & TPointTerms::TUBE_FLEXURE_HA) count++;
        if (enabled_terms_ & TPointTerms::TUBE_ROTATION) count++;
        if (enabled_terms_ & TPointTerms::WORM_ERROR) count += 1 + (5 * 2); // Base + 5 harmonics with amplitude/phase
        if (enabled_terms_ & TPointTerms::ENCODER_ERROR_HA) count += 1 + (4 * 2); // Base + 4 harmonics with amplitude/phase
        return std::max(count, 1); // At least 1 parameter
    }
    
    int getDecParameterCount() const {
        int count = 0;
        if (enabled_terms_ & TPointTerms::AXIS_NONPERP) count++; // AN term for Dec
        if (enabled_terms_ & TPointTerms::POLAR_ALT) count++; // Dec component of PA
        if (enabled_terms_ & TPointTerms::POLAR_AZ) count++; // Dec component of PA
        if (enabled_terms_ & TPointTerms::TUBE_FLEXURE_DEC) count++;
        if (enabled_terms_ & TPointTerms::TUBE_ROTATION) count++;
        if (enabled_terms_ & TPointTerms::ENCODER_ERROR_DEC) count += 1 + (4 * 2); // Base + 4 harmonics with amplitude/phase
        if (enabled_terms_ & TPointTerms::REFRACTION) count += 3; // A, B, C coefficients
        return std::max(count, 1); // At least 1 parameter
    }
    
    int getParameterCount() const {
        return getRAParameterCount() + getDecParameterCount();
    }
    
    int getMinMeasurements() const {
        return std::max(getParameterCount() * 2, 10);
    }
    
    void fillDesignMatrixRow(MatrixXd& A_ra, MatrixXd& A_dec, int row, double ha, double dec) const {
        fillDesignMatrixRowSeparate(A_ra, A_dec, row, ha, dec);
    }
    
    void fillCombinedDesignMatrixRow(MatrixXd& A, int row, double ha, double dec) const {
        int p_ra = getRAParameterCount();
        int p_dec = getDecParameterCount();
        int p_total = p_ra + p_dec;
        
        // Create temporary matrices for RA and Dec
        Eigen::RowVectorXd row_ra(p_ra);
        Eigen::RowVectorXd row_dec(p_dec);
        
        // Fill RA and Dec rows separately
        fillDesignMatrixRowInternal(row_ra, row_dec, ha, dec);
        
        // Combine into single row
        for (int i = 0; i < p_ra; ++i) {
            A(row, i) = row_ra(i);
        }
        for (int i = 0; i < p_dec; ++i) {
            A(row, p_ra + i) = row_dec(i);
        }
    }
    
    void fillDesignMatrixRowInternal(Eigen::RowVectorXd& row_ra, Eigen::RowVectorXd& row_dec, double ha, double dec) const {
        int col_ra = 0;
        int col_dec = 0;
        double ha_rad = ha * M_PI / 12.0; // hours to radians
        double dec_rad = dec * M_PI / 180.0; // degrees to radians
        
        // Standard TPOINT model terms
        
        // Index error (affects RA) - IA
        if (enabled_terms_ & TPointTerms::INDEX_ERROR) {
            row_ra(col_ra++) = 1.0;
        }
        
        // Collimation error (affects RA) - CA
        if (enabled_terms_ & TPointTerms::COLLIMATION_ERROR) {
            row_ra(col_ra++) = std::cos(ha_rad);
        }
        
        // Non-perpendicularity - affects both RA and Dec
        if (enabled_terms_ & TPointTerms::AXIS_NONPERP) {
            // RA term: AN·sin(h)·tan(δ)
            double tan_dec;
            if (std::abs(dec) < 85.0) {
                tan_dec = std::tan(dec_rad);
            } else {
                double epsilon = (90.0 - std::abs(dec)) * M_PI / 180.0;
                tan_dec = 1.0 / epsilon - epsilon / 3.0;
            }
            row_ra(col_ra++) = std::sin(ha_rad) * tan_dec;
            // Dec term: AN·cos(h)
            row_dec(col_dec++) = std::cos(ha_rad);
        }
        
        // Polar altitude error - affects both RA and Dec
        // RA component: -cos(ha) is ONLY included when COLLIMATION_ERROR is NOT enabled,
        // because COLLIMATION_ERROR's +cos(ha) creates a collinear column.
        // When both are enabled, POLAR_ALT contributes to RA through collimation_error
        // absorbing the cos(ha) component.
        // Dec component: constant offset (always present)
        if (enabled_terms_ & TPointTerms::POLAR_ALT) {
            if (!(enabled_terms_ & TPointTerms::COLLIMATION_ERROR)) {
                row_ra(col_ra++) = -std::cos(ha_rad);  // RA term: -cos(ha)
            }
            row_dec(col_dec++) = 1.0;               // Dec term: constant
        }
        
        // Polar azimuth error - affects both RA and Dec
        if (enabled_terms_ & TPointTerms::POLAR_AZ) {
            // RA term: PA·sin(h)
            row_ra(col_ra++) = std::sin(ha_rad);
            // Dec term: PA·sin(h)·tan(δ)
            double tan_dec;
            if (std::abs(dec) < 85.0) {
                tan_dec = std::tan(dec_rad);
            } else {
                double epsilon = (90.0 - std::abs(dec)) * M_PI / 180.0;
                tan_dec = 1.0 / epsilon - epsilon / 3.0;
            }
            row_dec(col_dec++) = std::sin(ha_rad) * tan_dec;
        }
        
        // Tube flexure in HA (affects RA): sin(ha) * cos(dec)
        // cos(dec) breaks degeneracy with POLAR_AZ (which uses bare sin(ha))
        // Physically: tube flexure depends on gravity projection, varying with dec
        if (enabled_terms_ & TPointTerms::TUBE_FLEXURE_HA) {
            row_ra(col_ra++) = std::sin(ha_rad) * std::cos(dec_rad);
        }
        
        // Tube flexure in Dec (affects Dec): sin(dec)
        if (enabled_terms_ & TPointTerms::TUBE_FLEXURE_DEC) {
            row_dec(col_dec++) = std::sin(dec_rad);
        }
        
        // Tube rotation (affects both RA and Dec)
        if (enabled_terms_ & TPointTerms::TUBE_ROTATION) {
            // RA term: TF·cos(δ)
            row_ra(col_ra++) = std::cos(dec_rad);
            // Dec term: -TF·sin(δ)·sin(h)
            row_dec(col_dec++) = -std::sin(dec_rad) * std::sin(ha_rad);
        }
        
        // Worm period error (affects RA)
        // Terms: sin(1*ha) base + sin(2*ha)/cos(2*ha) ... sin(6*ha)/cos(6*ha)
        if (enabled_terms_ & TPointTerms::WORM_ERROR) {
            row_ra(col_ra++) = std::sin(1.0 * ha_rad);  // 1st harmonic (base)
            // Higher harmonics: sin/cos pairs for i=2..6
            for (int i = 2; i <= 6; ++i) {
                row_ra(col_ra++) = std::sin(i * ha_rad);
                row_ra(col_ra++) = std::cos(i * ha_rad);
            }
        }
        
        // HA encoder error (affects RA)
        // Terms: linear ha + sin(2*ha)/cos(2*ha) ... sin(5*ha)/cos(5*ha)
        if (enabled_terms_ & TPointTerms::ENCODER_ERROR_HA) {
            row_ra(col_ra++) = ha;  // Linear term
            for (int i = 2; i <= 5; ++i) {
                row_ra(col_ra++) = std::sin(i * ha_rad);
                row_ra(col_ra++) = std::cos(i * ha_rad);
            }
        }
        
        // Dec encoder error (affects Dec)
        if (enabled_terms_ & TPointTerms::ENCODER_ERROR_DEC) {
            row_dec(col_dec++) = dec;  // Linear term
            for (int i = 2; i <= 5; ++i) {
                row_dec(col_dec++) = std::sin(i * dec_rad);
                row_dec(col_dec++) = std::cos(i * dec_rad);
            }
        }
        
        // Refraction (affects Dec) - Saastamoinen model
        if (enabled_terms_ & TPointTerms::REFRACTION) {
            double z = M_PI / 2.0 - dec_rad; // zenith distance
            constexpr double MAX_ZENITH_DIST = 87.0 * D2R;
            z = std::min(z, MAX_ZENITH_DIST);
            double tz = std::tan(z);
            row_dec(col_dec++) = tz;              // A·tan(z)
            row_dec(col_dec++) = tz * tz * tz;    // B·tan³(z)
            row_dec(col_dec++) = tz * tz * tz * tz * tz; // C·tan⁵(z)
        }
    }
    
    void fillDesignMatrixRowSeparate(MatrixXd& A_ra, MatrixXd& A_dec, int row, double ha, double dec) const {
        int col_ra = 0;
        int col_dec = 0;
        double ha_rad = ha * M_PI / 12.0; // hours to radians
        double dec_rad = dec * M_PI / 180.0; // degrees to radians
        
        // Index error (affects RA) - IA
        if (enabled_terms_ & TPointTerms::INDEX_ERROR) {
            A_ra(row, col_ra++) = 1.0;
        }
        
        // Collimation error (affects RA) - CA
        if (enabled_terms_ & TPointTerms::COLLIMATION_ERROR) {
            A_ra(row, col_ra++) = std::cos(ha_rad);
        }
        
        // Non-perpendicularity - affects both RA and Dec
        if (enabled_terms_ & TPointTerms::AXIS_NONPERP) {
            double tan_dec;
            if (std::abs(dec) < 85.0) {
                tan_dec = std::tan(dec_rad);
            } else {
                double epsilon = (90.0 - std::abs(dec)) * M_PI / 180.0;
                tan_dec = 1.0 / epsilon - epsilon / 3.0;
            }
            A_ra(row, col_ra++) = std::sin(ha_rad) * tan_dec;
            A_dec(row, col_dec++) = std::cos(ha_rad);
        }
        
        // Polar altitude error - affects both RA and Dec
        // RA component: -cos(ha) is ONLY included when COLLIMATION_ERROR is NOT enabled,
        // because COLLIMATION_ERROR's +cos(ha) creates a collinear column.
        // When both are enabled, POLAR_ALT contributes to RA through collimation_error
        // absorbing the cos(ha) component.
        // Dec component: constant offset (always present)
        if (enabled_terms_ & TPointTerms::POLAR_ALT) {
            if (!(enabled_terms_ & TPointTerms::COLLIMATION_ERROR)) {
                A_ra(row, col_ra++) = -std::cos(ha_rad);  // RA term: -cos(ha)
            }
            A_dec(row, col_dec++) = 1.0;               // Dec term: constant
        }
        
        // Polar azimuth error - affects both RA and Dec
        if (enabled_terms_ & TPointTerms::POLAR_AZ) {
            A_ra(row, col_ra++) = std::sin(ha_rad);
            double tan_dec;
            if (std::abs(dec) < 85.0) {
                tan_dec = std::tan(dec_rad);
            } else {
                double epsilon = (90.0 - std::abs(dec)) * M_PI / 180.0;
                tan_dec = 1.0 / epsilon - epsilon / 3.0;
            }
            A_dec(row, col_dec++) = std::sin(ha_rad) * tan_dec;
        }
        
        // Tube flexure in HA (affects RA): sin(ha) * cos(dec)
        if (enabled_terms_ & TPointTerms::TUBE_FLEXURE_HA) {
            A_ra(row, col_ra++) = std::sin(ha_rad) * std::cos(dec_rad);
        }
        
        // Tube flexure in Dec (affects Dec): sin(dec)
        if (enabled_terms_ & TPointTerms::TUBE_FLEXURE_DEC) {
            A_dec(row, col_dec++) = std::sin(dec_rad);
        }
        
        // Tube rotation (affects both RA and Dec)
        if (enabled_terms_ & TPointTerms::TUBE_ROTATION) {
            A_ra(row, col_ra++) = std::cos(dec_rad);
            A_dec(row, col_dec++) = -std::sin(dec_rad) * std::sin(ha_rad);
        }
        
        // Worm period error (affects RA)
        // Terms: sin(1*ha) base + sin/cos pairs for i=2..6
        if (enabled_terms_ & TPointTerms::WORM_ERROR) {
            A_ra(row, col_ra++) = std::sin(1.0 * ha_rad);  // 1st harmonic
            for (int i = 2; i <= 6; ++i) {
                A_ra(row, col_ra++) = std::sin(i * ha_rad);
                A_ra(row, col_ra++) = std::cos(i * ha_rad);
            }
        }
        
        // HA encoder error (affects RA)
        // Terms: linear ha + sin/cos pairs for i=2..5
        if (enabled_terms_ & TPointTerms::ENCODER_ERROR_HA) {
            A_ra(row, col_ra++) = ha;  // Linear term
            for (int i = 2; i <= 5; ++i) {
                A_ra(row, col_ra++) = std::sin(i * ha_rad);
                A_ra(row, col_ra++) = std::cos(i * ha_rad);
            }
        }
        
        // Dec encoder error (affects Dec)
        if (enabled_terms_ & TPointTerms::ENCODER_ERROR_DEC) {
            A_dec(row, col_dec++) = dec;  // Linear term
            for (int i = 2; i <= 5; ++i) {
                A_dec(row, col_dec++) = std::sin(i * dec_rad);
                A_dec(row, col_dec++) = std::cos(i * dec_rad);
            }
        }
        
        // Refraction (affects Dec)
        if (enabled_terms_ & TPointTerms::REFRACTION) {
            double z = M_PI / 2.0 - dec_rad;
            constexpr double MAX_ZENITH_DIST = 87.0 * D2R;
            z = std::min(z, MAX_ZENITH_DIST);
            double tz = std::tan(z);
            A_dec(row, col_dec++) = tz;              // A·tan(z)
            A_dec(row, col_dec++) = tz * tz * tz;    // B·tan³(z)
            A_dec(row, col_dec++) = tz * tz * tz * tz * tz; // C·tan⁵(z)
        }
    }
    
    double calculateRACorrection(double ha, double dec, double temperature = 20.0) const {
        double correction = 0.0;
        double ha_rad = ha * M_PI / 12.0;
        double dec_rad = dec * M_PI / 180.0;
        
        // Temperature compensation
        double temp_diff = temperature - parameters_.calibration_temperature;
        
        // Index error: constant offset in RA
        if (enabled_terms_ & TPointTerms::INDEX_ERROR) {
            correction += parameters_.index_error;
        }
        
        // Collimation error: cos(ha) in RA
        if (enabled_terms_ & TPointTerms::COLLIMATION_ERROR) {
            correction += parameters_.collimation_error * std::cos(ha_rad);
        }
        
        // Axis non-perpendicularity: sin(ha) * tan(dec)
        if (enabled_terms_ & TPointTerms::AXIS_NONPERP) {
            double tan_dec;
            if (std::abs(dec) < 85.0) {
                tan_dec = std::tan(dec_rad);
            } else {
                double epsilon = (90.0 - std::abs(dec)) * D2R;
                tan_dec = 1.0 / epsilon - epsilon / 3.0;
            }
            correction += (parameters_.axis_nonperp + temp_diff * parameters_.axis_nonperp_temp_coeff)
                          * std::sin(ha_rad) * tan_dec;
        }
        // Polar altitude RA component: -cos(ha)
        // Only applied when COLLIMATION_ERROR is NOT enabled, because the RA column
        // (-cos(ha)) is only present in the design matrix when COLLIMATION_ERROR is
        // absent. When COLLIMATION_ERROR IS enabled, the cos(ha) basis is handled
        // entirely through collimation_error, and POLAR_ALT's effect on RA is
        // absorbed into that term. The Dec-determined polar_alt_error is applied
        // only in Dec correction (constant offset).
        if (enabled_terms_ & TPointTerms::POLAR_ALT) {
            if (!(enabled_terms_ & TPointTerms::COLLIMATION_ERROR)) {
                correction += -parameters_.polar_alt_error * std::cos(ha_rad);
            }
        }
        
        
        // Polar azimuth: sin(ha)
        if (enabled_terms_ & TPointTerms::POLAR_AZ) {
            correction += parameters_.polar_az_error * std::sin(ha_rad);
        }
        
        // Tube flexure in HA: sin(ha) * cos(dec)
        if (enabled_terms_ & TPointTerms::TUBE_FLEXURE_HA) {
            correction += (parameters_.tube_flexure_ha + temp_diff * parameters_.temp_flexure_coeff)
                          * std::sin(ha_rad) * std::cos(dec_rad);
        }
        
        // Tube rotation: cos(dec)
        if (enabled_terms_ & TPointTerms::TUBE_ROTATION) {
            correction += parameters_.tube_rotation * std::cos(dec_rad);
        }
        
        // Worm period error: sin(1*ha) base + sin(i*ha)/cos(i*ha) for i=2..6
        if (enabled_terms_ & TPointTerms::WORM_ERROR) {
            correction += parameters_.worm_period_error * std::sin(1.0 * ha_rad);
            for (int i = 0; i < 5; ++i) {
                correction += parameters_.worm_harmonics[i * 2] * std::sin((i + 2) * ha_rad);
                correction += parameters_.worm_harmonics[i * 2 + 1] * std::cos((i + 2) * ha_rad);
            }
        }
        
        // HA encoder error: ha (linear) + sin(i*ha)/cos(i*ha) for i=2..5
        if (enabled_terms_ & TPointTerms::ENCODER_ERROR_HA) {
            correction += (parameters_.encoder_error_ha + temp_diff * parameters_.temp_encoder_coeff) * ha;
            for (int i = 0; i < 4; ++i) {
                correction += parameters_.encoder_harmonics_ha[i * 2] * std::sin((i + 2) * ha_rad);
                correction += parameters_.encoder_harmonics_ha[i * 2 + 1] * std::cos((i + 2) * ha_rad);
            }
        }
        
        return correction;
    }
    
    double calculateDecCorrection(double ha, double dec, double temperature = 20.0) const {
        double correction = 0.0;
        double ha_rad = ha * M_PI / 12.0;
        double dec_rad = dec * M_PI / 180.0;
        
        // Axis non-perpendicularity Dec component: cos(ha)
        if (enabled_terms_ & TPointTerms::AXIS_NONPERP) {
            correction += parameters_.axis_nonperp * std::cos(ha_rad);
        }
        
        // Polar altitude: constant
        if (enabled_terms_ & TPointTerms::POLAR_ALT) {
            correction += parameters_.polar_alt_error;
        }
        
        // Polar azimuth Dec component: sin(ha) * tan(dec)
        if (enabled_terms_ & TPointTerms::POLAR_AZ) {
            double tan_dec;
            if (std::abs(dec) < 85.0) {
                tan_dec = std::tan(dec_rad);
            } else {
                double epsilon = (90.0 - std::abs(dec)) * D2R;
                tan_dec = 1.0 / epsilon - epsilon / 3.0;
            }
            correction += parameters_.polar_az_error * std::sin(ha_rad) * tan_dec;
        }
        
        // Tube flexure in Dec: sin(dec)
        if (enabled_terms_ & TPointTerms::TUBE_FLEXURE_DEC) {
            correction += parameters_.tube_flexure_dec * std::sin(dec_rad);
        }
        
        // Tube rotation Dec component: -sin(dec) * sin(ha)
        if (enabled_terms_ & TPointTerms::TUBE_ROTATION) {
            correction += -parameters_.tube_rotation * std::sin(dec_rad) * std::sin(ha_rad);
        }
        
        // Dec encoder error: dec (linear) + sin(i*dec)/cos(i*dec) for i=2..5
        if (enabled_terms_ & TPointTerms::ENCODER_ERROR_DEC) {
            correction += parameters_.encoder_error_dec * dec;
            for (int i = 0; i < 4; ++i) {
                correction += parameters_.encoder_harmonics_dec[i * 2] * std::sin((i + 2) * dec_rad);
                correction += parameters_.encoder_harmonics_dec[i * 2 + 1] * std::cos((i + 2) * dec_rad);
            }
        }
        
        // Refraction: A*tan(z) + B*tan³(z) + C*tan⁵(z)
        if (enabled_terms_ & TPointTerms::REFRACTION) {
            double z = M_PI / 2.0 - dec_rad;
            constexpr double MAX_ZENITH_DIST = 87.0 * D2R;
            z = std::min(z, MAX_ZENITH_DIST);
            double tz = std::tan(z);
            correction += parameters_.refraction_coeff * tz;
            correction += parameters_.refraction_temp_coeff * tz * tz * tz;
            correction += parameters_.refraction_pressure_coeff * tz * tz * tz * tz * tz;
        }
        
        return correction;
    }
    
    double calculateAxisPhysicalCorrection(double angle, double temperature, double load_torque,
                                          bool direction_changed, const AxisPhysicalParameters& params) const {
        double correction = 0.0;
        double angle_rad = angle * M_PI / 180.0;
        
        // 1. Cyclic gear errors (periodic errors)
        if (params.cyclic_error_amplitude > 0.0) {
            double cyclic_error = 0.0;
            for (int i = 0; i < 8; i += 2) {
                double amplitude = params.cyclic_harmonics[i];
                double phase = params.cyclic_harmonics[i + 1];
                cyclic_error += amplitude * std::sin((i/2 + 1) * angle_rad + phase);
            }
            correction += cyclic_error;
        }
        
        // 2. Worm gear errors (if applicable)
        if (params.worm_ratio > 0.0) {
            // Worm period error (once per worm revolution)
            double worm_error = params.cyclic_error_amplitude * 
                               std::sin(2.0 * M_PI * angle / 360.0 * params.worm_ratio);
            correction += worm_error;
        }
        
        // 3. Encoder quantization error
        if (params.encoder_quantization_error > 0.0) {
            // Quantization error is ±0.5 LSB
            correction += params.encoder_quantization_error * 0.5;
        }
        
        // 4. Backlash compensation (only when direction changes)
        if (direction_changed && params.backlash > 0.0) {
            double temp_diff = temperature - params.calibration_temp;
            double backlash_correction = params.backlash + temp_diff * params.backlash_temp_coeff;
            correction += backlash_correction;
        }
        
        // 5. Stiffness compensation (load-dependent)
        if (load_torque > 0.0 && params.axis_stiffness > 0.0) {
            correction += load_torque * params.axis_stiffness;
        }
        
        // 6. Temperature expansion compensation
        double temp_diff = temperature - params.calibration_temp;
        if (temp_diff != 0.0 && params.expansion_coeff > 0.0) {
            double expansion_error = params.expansion_coeff * temp_diff * angle;
            correction += expansion_error;
        }
        
        // 7. Gear error temperature compensation
        if (temp_diff != 0.0 && params.temp_gear_error_coeff > 0.0) {
            correction += params.temp_gear_error_coeff * temp_diff;
        }
        
        // 8. Calibration table interpolation (if available)
        if (!params.calibration_table.empty()) {
            double calib_error = interpolateCalibrationTable(angle, params.calibration_table);
            correction += calib_error;
        }
        
        return correction;
    }
    
    double interpolateCalibrationTable(double angle, const std::vector<double>& table) const {
        if (table.empty()) return 0.0;
        
        // Normalize angle to 0-360 degrees
        double normalized_angle = std::fmod(angle, 360.0);
        if (normalized_angle < 0.0) normalized_angle += 360.0;
        
        // Calculate index in calibration table
        double index = normalized_angle * (table.size() - 1) / 360.0;
        int idx1 = static_cast<int>(std::floor(index));
        int idx2 = static_cast<int>(std::ceil(index));
        
        if (idx1 < 0) idx1 = 0;
        if (idx2 >= table.size()) idx2 = table.size() - 1;
        
        if (idx1 == idx2) {
            return table[idx1];
        }
        
        // Linear interpolation
        double t = index - idx1;
        return table[idx1] * (1.0 - t) + table[idx2] * t;
    }
    
    void calculateStatistics(const MatrixXd& A_ra, const MatrixXd& A_dec, const VectorXd& b_ra, const VectorXd& b_dec,
                            const VectorXd& params_ra, const VectorXd& params_dec) {
        int n = measurements_.size();
        int p_ra = params_ra.size();
        int p_dec = params_dec.size();
        int p_total = p_ra + p_dec;
        
        // Calculate residuals
        VectorXd residuals_ra = b_ra - A_ra * params_ra;
        VectorXd residuals_dec = b_dec - A_dec * params_dec;
        
        // Calculate chi-squared
        double chi2_ra = residuals_ra.squaredNorm();
        double chi2_dec = residuals_dec.squaredNorm();
        parameters_.chi_squared = chi2_ra + chi2_dec;
        
        // Calculate RMS error
        double total_error = 0.0;
        for (int i = 0; i < n; ++i) {
            double ra_error = residuals_ra(i);
            double dec_error = residuals_dec(i);
            total_error += ra_error * ra_error + dec_error * dec_error;
        }
        parameters_.rms_error = std::sqrt(total_error / (2.0 * n));
        
        parameters_.degrees_of_freedom = 2 * n - p_total;
    }
    
    std::vector<Measurement> measurements_;
    TPointParameters parameters_;
    uint32_t enabled_terms_;
    double focal_length_;
    double aperture_;
    double tube_length_;
    bool is_fitted_;
};

// Public interface implementation
TPointModel::TPointModel() : pimpl(std::make_unique<Impl>()) {}

TPointModel::~TPointModel() = default;

void TPointModel::addMeasurement(const Measurement& measurement) {
    pimpl->addMeasurement(measurement);
}

bool TPointModel::fitModel() {
    return pimpl->fitModel();
}

TPointModel::TPointParameters TPointModel::getParameters() const {
    return pimpl->getParameters();
}

std::pair<double, double> TPointModel::applyCorrections(double observed_ra, double observed_dec,
                                                        double mount_ha, double mount_dec,
                                                        double temperature,
                                                        double load_torque,
                                                        bool direction_changed) {
    return pimpl->applyCorrections(observed_ra, observed_dec, mount_ha, mount_dec,
                                   temperature, load_torque, direction_changed);
}

std::pair<double, double> TPointModel::predictMountPosition(double ra, double dec) {
    return pimpl->predictMountPosition(ra, dec);
}

double TPointModel::calculateResidual(const Measurement& measurement) const {
    return pimpl->calculateResidual(measurement);
}

std::vector<double> TPointModel::getAllResiduals() const {
    return pimpl->getAllResiduals();
}

void TPointModel::clearMeasurements() {
    pimpl->clearMeasurements();
}

size_t TPointModel::getMeasurementCount() const {
    return pimpl->getMeasurementCount();
}

void TPointModel::setTelescopeParameters(double focal_length, double aperture, double tube_length) {
    pimpl->setTelescopeParameters(focal_length, aperture, tube_length);
}

void TPointModel::setEnabledTerms(uint32_t term_mask) {
    pimpl->setEnabledTerms(term_mask);
}

bool TPointModel::saveToFile(const std::string& filename) const {
    return pimpl->saveToFile(filename);
}

bool TPointModel::loadFromFile(const std::string& filename) {
    return pimpl->loadFromFile(filename);
}

Eigen::MatrixXd TPointModel::getCovarianceMatrix() const {
    return pimpl->getCovarianceMatrix();
}

std::vector<double> TPointModel::getParameterUncertainties() const {
    return pimpl->getParameterUncertainties();
}

std::map<std::string, double> TPointModel::calculateQualityMetrics() const {
    return pimpl->calculateQualityMetrics();
}

} // namespace models
} // namespace astro_mount
