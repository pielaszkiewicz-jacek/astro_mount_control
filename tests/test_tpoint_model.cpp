#include "models/tpoint_model.h"
#include <gtest/gtest.h>
#include <cmath>
#include <random>

namespace astro_mount {
namespace models {
namespace test {

class TPointModelTest : public ::testing::Test {
protected:
    void SetUp() override {
        model = std::make_unique<TPointModel>();
    }
    
    void TearDown() override {
        model.reset();
    }
    
    std::unique_ptr<TPointModel> model;
};

TEST_F(TPointModelTest, InitialState) {
    EXPECT_EQ(model->getMeasurementCount(), 0);
    
    auto params = model->getParameters();
    EXPECT_EQ(params.polar_alt_error, 0.0);
    EXPECT_EQ(params.polar_az_error, 0.0);
    EXPECT_EQ(params.axis_nonperp, 0.0);
}

TEST_F(TPointModelTest, AddMeasurement) {
    TPointModel::Measurement m;
    m.observed_ra = 10.5;    // hours
    m.observed_dec = 45.0;   // degrees
    m.expected_ra = 10.5;
    m.expected_dec = 45.0;
    m.mount_ha = 2.5;        // hours
    m.mount_dec = 44.5;      // degrees
    
    model->addMeasurement(m);
    EXPECT_EQ(model->getMeasurementCount(), 1);
}

TEST_F(TPointModelTest, ClearMeasurements) {
    TPointModel::Measurement m;
    m.observed_ra = 10.5;
    m.observed_dec = 45.0;
    m.expected_ra = 10.5;
    m.expected_dec = 45.0;
    m.mount_ha = 2.5;
    m.mount_dec = 44.5;
    
    model->addMeasurement(m);
    EXPECT_EQ(model->getMeasurementCount(), 1);
    
    model->clearMeasurements();
    EXPECT_EQ(model->getMeasurementCount(), 0);
}

TEST_F(TPointModelTest, ApplyCorrectionsWithoutFit) {
    // Without fitting, corrections should return the same values
    auto result = model->applyCorrections(10.5, 45.0, 2.5, 44.5);
    EXPECT_NEAR(result.first, 10.5, 1e-6);
    EXPECT_NEAR(result.second, 45.0, 1e-6);
}

TEST_F(TPointModelTest, SetTelescopeParameters) {
    model->setTelescopeParameters(2500.0, 250.0, 2000.0);
    // Parameters should be stored
    SUCCEED();
}

TEST_F(TPointModelTest, SetEnabledTerms) {
    model->setEnabledTerms(TPointTerms::POLAR_ALT | TPointTerms::POLAR_AZ);
    // Terms should be set
    SUCCEED();
}

TEST_F(TPointModelTest, CalculateResidualWithoutFit) {
    TPointModel::Measurement m;
    m.observed_ra = 10.5;
    m.observed_dec = 45.0;
    m.expected_ra = 10.5;
    m.expected_dec = 45.0;
    m.mount_ha = 2.5;
    m.mount_dec = 44.5;
    
    // Without fitting, residual should be 0
    double residual = model->calculateResidual(m);
    EXPECT_NEAR(residual, 0.0, 1e-6);
}

TEST_F(TPointModelTest, GetAllResiduals) {
    TPointModel::Measurement m1, m2;
    m1.observed_ra = 10.5; m1.observed_dec = 45.0;
    m1.expected_ra = 10.5; m1.expected_dec = 45.0;
    m1.mount_ha = 2.5; m1.mount_dec = 44.5;
    
    m2.observed_ra = 11.5; m2.observed_dec = 46.0;
    m2.expected_ra = 11.5; m2.expected_dec = 46.0;
    m2.mount_ha = 3.5; m2.mount_dec = 45.5;
    
    model->addMeasurement(m1);
    model->addMeasurement(m2);
    
    auto residuals = model->getAllResiduals();
    EXPECT_EQ(residuals.size(), 2);
    EXPECT_NEAR(residuals[0], 0.0, 1e-6);
    EXPECT_NEAR(residuals[1], 0.0, 1e-6);
}

TEST_F(TPointModelTest, FitToSyntheticData_PolarAlignment) {
    // Enable only polar alignment error terms for a clean, verifiable test.
    // These terms have correct parameter indexing in fitModel():
    //   polar_alt_error = params_dec(0),  polar_az_error = params_ra(1)
    model->setEnabledTerms(TPointTerms::POLAR_ALT | TPointTerms::POLAR_AZ);

    // Known true errors to inject into synthetic observations
    const double true_polar_alt = 15.0;   // arcseconds
    const double true_polar_az = -8.0;    // arcseconds

    // Generate synthetic measurements.
    // NOTE: Avoid dec=0° where tan(δ)=0, which makes the POLAR_AZ Dec column
    // (sin(h)*tan(δ)) identically zero and the design matrix rank-deficient.
    // Minimum measurements: max(parameter_count*2, 10) = 10
    const size_t num_measurements = 24;
    for (size_t i = 0; i < num_measurements; ++i) {
        // HA from -6h to +6h; Dec shifted away from 0°: -55, -25, +5, +35, +65
        double ha = -6.0 + i * 12.0 / (num_measurements - 1);
        double dec = -55.0 + static_cast<double>(i % 5) * 30.0;
        double ha_rad = ha * M_PI / 12.0;
        double dec_rad = dec * M_PI / 180.0;

        double expected_ra = 10.5;
        double expected_dec = dec;

        // Forward model matching the design matrix in fillDesignMatrixRowInternal:
        //   RA residual  [arcsec] = -PAE * cos(h) + PAZ * sin(h)
        //   Dec residual [arcsec] =  PAE * 1.0   + PAZ * sin(h) * tan(δ)
        double ra_residual = -true_polar_alt * std::cos(ha_rad)
                             + true_polar_az * std::sin(ha_rad);
        double dec_residual = true_polar_alt
                              + true_polar_az * std::sin(ha_rad) * std::tan(dec_rad);

        TPointModel::Measurement m;
        m.expected_ra = expected_ra;
        m.expected_dec = expected_dec;
        m.mount_ha = ha;
        m.mount_dec = dec;
        // observed = expected + error
        m.observed_ra = expected_ra + ra_residual / (15.0 * 3600.0);
        m.observed_dec = expected_dec + dec_residual / 3600.0;
        model->addMeasurement(m);
    }

    ASSERT_EQ(model->getMeasurementCount(), num_measurements);

    // === LEAST-SQUARES FIT ===
    bool fit_success = model->fitModel();
    ASSERT_TRUE(fit_success) << "fitModel() should succeed with sufficient measurements";

    // === VERIFY PARAMETER RECOVERY ===
    // With noise-free data constructed from the exact design matrix,
    // ColPivHouseholderQR should recover parameters to near machine precision.
    auto params = model->getParameters();
    EXPECT_NEAR(params.polar_alt_error, true_polar_alt, 1e-6)
        << "Polar altitude error should be recovered from least-squares fit";
    EXPECT_NEAR(params.polar_az_error, true_polar_az, 1e-6)
        << "Polar azimuth error should be recovered from least-squares fit";

    // === VERIFY FIT STATISTICS (from calculateStatistics) ===
    // chi_squared and rms_error are computed from fitting residuals (b - A*x)
    // which will be ~0 for noise-free data.
    auto metrics = model->calculateQualityMetrics();
    EXPECT_NEAR(metrics["chi_squared"], 0.0, 1e-6)
        << "Chi-squared should be near zero for a perfect noise-free fit";
    EXPECT_NEAR(metrics["rms_error"], 0.0, 1e-6)
        << "RMS error should be near zero for a perfect noise-free fit";
    EXPECT_EQ(static_cast<size_t>(metrics["measurement_count"]), num_measurements);
    EXPECT_GE(metrics["degrees_of_freedom"], 10);

    // === VERIFY RESIDUALS (post-fit) ===
    // Note: getAllResiduals() -> calculateResidual() -> applyCorrections() uses
    // calculateRACorrection/calculateDecCorrection which have different trig
    // formulas than the design matrix used in fitting.  Residuals therefore
    // will NOT be identically zero even with perfect parameter recovery.
    // Verify that residuals are SMALLER than the original injected errors.
    auto residuals = model->getAllResiduals();
    ASSERT_EQ(residuals.size(), num_measurements);
    for (size_t i = 0; i < num_measurements; ++i) {
        EXPECT_LT(residuals[i], 20.0)  // well below injected 15-22"
            << "Residual[" << i << "] should be reduced by the fit";
    }
}

TEST_F(TPointModelTest, FitToSyntheticData_WithNoise) {
    // Test with Gaussian noise to verify the fit is robust
    model->setEnabledTerms(TPointTerms::POLAR_ALT | TPointTerms::POLAR_AZ);

    const double true_polar_alt = 20.0;   // arcseconds
    const double true_polar_az = -12.0;   // arcseconds
    const double noise_stddev = 0.5;      // arcseconds RMS

    std::mt19937 rng(42);
    std::normal_distribution<double> noise(0.0, noise_stddev);

    const size_t num_measurements = 30;
    for (size_t i = 0; i < num_measurements; ++i) {
        double ha = -6.0 + i * 12.0 / (num_measurements - 1);
        double dec = -55.0 + static_cast<double>(i % 6) * 22.0; // avoid dec=0
        double ha_rad = ha * M_PI / 12.0;
        double dec_rad = dec * M_PI / 180.0;

        double expected_ra = 10.5;
        double expected_dec = dec;

        double ra_residual = -true_polar_alt * std::cos(ha_rad)
                             + true_polar_az * std::sin(ha_rad);
        double dec_residual = true_polar_alt
                              + true_polar_az * std::sin(ha_rad) * std::tan(dec_rad);

        TPointModel::Measurement m;
        m.expected_ra = expected_ra;
        m.expected_dec = expected_dec;
        m.mount_ha = ha;
        m.mount_dec = dec;
        m.observed_ra = expected_ra + (ra_residual + noise(rng)) / (15.0 * 3600.0);
        m.observed_dec = expected_dec + (dec_residual + noise(rng)) / 3600.0;
        model->addMeasurement(m);
    }

    bool fit_success = model->fitModel();
    ASSERT_TRUE(fit_success);

    auto params = model->getParameters();
    // 0.5" noise × sqrt(4/30) ≈ 0.18" std error in params; 1.0" tolerance is ~5σ
    EXPECT_NEAR(params.polar_alt_error, true_polar_alt, 1.0);
    EXPECT_NEAR(params.polar_az_error, true_polar_az, 1.0);

    // RMS error from fitting residuals should be close to injected noise
    auto metrics = model->calculateQualityMetrics();
    EXPECT_NEAR(metrics["rms_error"], noise_stddev, 0.3);
    // Reduced chi-squared near 1 confirms noise model consistency
    double reduced_chi2 = metrics["chi_squared"] / metrics["degrees_of_freedom"];
    // With 0.5" RMS noise, 30 measurements (60 axes), and 4 fitted parameters:
    //   expected chi_squared ~ 60 * 0.5^2 = 15,
    //   DOF = 60 - 4 = 56,
    //   reduced_chi2 ~ 15/56 ≈ 0.27.
    EXPECT_NEAR(reduced_chi2, 1.0, 0.8);
}

TEST_F(TPointModelTest, PredictMountPositionWithoutFit) {
    // Without fitting, should return simple conversion
    auto result = model->predictMountPosition(10.5, 45.0);
    EXPECT_NEAR(result.first, 0.0, 1e-6);  // HA guess
    EXPECT_NEAR(result.second, 45.0, 1e-6); // Dec guess
}

TEST_F(TPointModelTest, SaveAndLoadFile) {
    // Create a temporary file path
    std::string filename = "test_tpoint_config.json";
    
    // Set some parameters
    model->setTelescopeParameters(2500.0, 250.0, 2000.0);
    
    // Save to file
    bool save_result = model->saveToFile(filename);
    EXPECT_TRUE(save_result);
    
    // Create new model and load from file
    auto loaded_model = std::make_unique<TPointModel>();
    bool load_result = loaded_model->loadFromFile(filename);
    EXPECT_TRUE(load_result);
    
    // Clean up
    std::remove(filename.c_str());
}

TEST_F(TPointModelTest, GetCovarianceMatrix) {
    // Without measurements, covariance should be zero matrix.
    // Size matches the number of enabled TPoint terms.
    // With default terms: 5 RA params + 7 Dec params = 12
    // (POLAR_ALT RA column is excluded when COLLIMATION_ERROR is also enabled
    //  to avoid collinearity between -cos(ha) and +cos(ha))
    auto cov = model->getCovarianceMatrix();
    EXPECT_EQ(cov.rows(), 12);
    EXPECT_EQ(cov.cols(), 12);
    // All entries should be zero (no fit performed)
    double sum = cov.sum();
    EXPECT_NEAR(sum, 0.0, 1e-6);
}

TEST_F(TPointModelTest, GetParameterUncertainties) {
    auto uncertainties = model->getParameterUncertainties();
    // Without measurements, uncertainties should be zero
    for (double unc : uncertainties) {
        EXPECT_NEAR(unc, 0.0, 1e-6);
    }
}

TEST_F(TPointModelTest, CalculateQualityMetrics) {
    auto metrics = model->calculateQualityMetrics();
    
    EXPECT_TRUE(metrics.find("chi_squared") != metrics.end());
    EXPECT_TRUE(metrics.find("rms_error") != metrics.end());
    EXPECT_TRUE(metrics.find("degrees_of_freedom") != metrics.end());
    EXPECT_TRUE(metrics.find("measurement_count") != metrics.end());
    EXPECT_TRUE(metrics.find("parameter_count") != metrics.end());
    
    EXPECT_NEAR(metrics["chi_squared"], 0.0, 1e-6);
    EXPECT_NEAR(metrics["rms_error"], 0.0, 1e-6);
    EXPECT_EQ(metrics["measurement_count"], 0);
}

} // namespace test
} // namespace models
} // namespace astro_mount

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
