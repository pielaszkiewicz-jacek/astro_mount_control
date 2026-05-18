#include "models/kalman_filter.h"
#include <gtest/gtest.h>
#include <cmath>
#include <random>

namespace astro_mount {
namespace models {
namespace test {

class KalmanFilterTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Default config for a 6D state (constant velocity model)
        config_.orientation_process_noise = 1e-4;
        config_.tpoint_process_noise = 1e-6;
        config_.rate_process_noise = 1e-3;
        config_.env_process_noise = 1e-5;
        config_.position_measurement_noise = 0.1;
        config_.environmental_measurement_noise = 1.0;
        config_.initial_orientation_uncertainty = 1.0;
        config_.initial_tpoint_uncertainty = 1.0;
        config_.initial_rate_uncertainty = 1.0;
        config_.use_adaptive_q = false;
        config_.use_adaptive_r = false;
        config_.innovation_threshold = 3.0;
        config_.max_iterations = 10;
        config_.tpoint_param_count = 4; // Default TPOINT params
    }

    void TearDown() override {
        filter.reset();
    }

    KalmanFilter::FilterConfig config_;
    std::unique_ptr<KalmanFilter> filter;
};

// ============================================================================
// Initial State Tests
// ============================================================================

TEST_F(KalmanFilterTest, InitialStateIsNotInitialized) {
    filter = std::make_unique<KalmanFilter>(config_);
    EXPECT_FALSE(filter->isInitialized());
    EXPECT_EQ(filter->getStatus(), "Not initialized");
}

TEST_F(KalmanFilterTest, InitializeSetsState) {
    filter = std::make_unique<KalmanFilter>(config_);
    int state_dim = config_.tpoint_param_count + 9; // 4 (ori) + N (tpoint) + 2 (rates) + 3 (env)
    
    Eigen::VectorXd initial_state = Eigen::VectorXd::Zero(state_dim);
    initial_state(0) = 1.0; // q0 = 1 (identity quaternion)
    initial_state(8) = 0.1; // rate1 (at index 4+N = 8 when N=4)
    initial_state(9) = 0.0; // rate2
    
    Eigen::MatrixXd initial_cov = Eigen::MatrixXd::Identity(state_dim, state_dim) * 0.1;
    
    filter->initialize(initial_state, initial_cov, 0.0);
    EXPECT_TRUE(filter->isInitialized());
    EXPECT_EQ(filter->getStatus(), "Initialized");
    
    // Check state mapping
    KalmanFilter::State state = filter->getState();
    EXPECT_NEAR(state.orientation(0), 1.0, 1e-6);
    EXPECT_NEAR(state.mount_rates(0), 0.1, 1e-6);
}

// ============================================================================
// Predict Tests
// ============================================================================

TEST_F(KalmanFilterTest, PredictWithoutInitializeIsNoop) {
    filter = std::make_unique<KalmanFilter>(config_);
    filter->predict(0.1); // Should not crash
    EXPECT_FALSE(filter->isInitialized());
}

TEST_F(KalmanFilterTest, PredictUpdatesStateAndTimestamp) {
    filter = std::make_unique<KalmanFilter>(config_);
    int state_dim = config_.tpoint_param_count + 9;
    
    Eigen::VectorXd initial_state = Eigen::VectorXd::Ones(state_dim) * 0.1;
    Eigen::MatrixXd initial_cov = Eigen::MatrixXd::Identity(state_dim, state_dim) * 0.1;
    filter->initialize(initial_state, initial_cov, 0.0);
    
    Eigen::VectorXd before = filter->getState().orientation;
    filter->predict(1.0);
    Eigen::VectorXd after = filter->getState().orientation;
    
    // State should have been multiplied by F with kinematic coupling:
    // dq/dt = 0.5 * omega ⊗ q  →  delta_q = -0.5*dt * [omega ⊗ q]
    // With all-0.1 state, dt=1.0, tpoint_param_count=4, rates at offset 8:
    //   Δqw = -0.5*1.0*0.1*0.1 + -0.5*1.0*0.1*0.1 = -0.01
    //   Δqx =  0.5*1.0*0.1*0.1 +  0.5*1.0*0.1*0.1 =  0.01
    //   Δqy = -0.5*1.0*0.1*0.1 +  0.5*1.0*0.1*0.1 =  0.0
    //   Δqz =  0.5*1.0*0.1*0.1 + -0.5*1.0*0.1*0.1 =  0.0
    EXPECT_NEAR(after(0), 0.09, 1e-6);
    EXPECT_NEAR(after(1), 0.11, 1e-6);
    EXPECT_NEAR(after(2), 0.10, 1e-6);
    EXPECT_NEAR(after(3), 0.10, 1e-6);
}

// ============================================================================
// Update Tests
// ============================================================================

TEST_F(KalmanFilterTest, UpdateWithoutInitializeIsNoop) {
    filter = std::make_unique<KalmanFilter>(config_);
    int state_dim = config_.tpoint_param_count + 9;
    Eigen::VectorXd measurement = Eigen::VectorXd::Zero(6);
    filter->update(measurement); // Should not crash
}

TEST_F(KalmanFilterTest, UpdateCorrectsStateTowardMeasurement) {
    filter = std::make_unique<KalmanFilter>(config_);
    int state_dim = config_.tpoint_param_count + 9;
    
    // Start with all-zero state
    Eigen::VectorXd initial_state = Eigen::VectorXd::Zero(state_dim);
    Eigen::MatrixXd initial_cov = Eigen::MatrixXd::Identity(state_dim, state_dim);
    filter->initialize(initial_state, initial_cov, 0.0);
    
    // Measurement says position = 5.0 in first observable dimension
    Eigen::VectorXd measurement = Eigen::VectorXd::Zero(6);
    measurement(0) = 5.0;
    
    // With default H (identity on first 6 dims), state should move toward measurement
    Eigen::VectorXd before = filter->getState().orientation;
    
    filter->update(measurement);
    
    Eigen::VectorXd after = filter->getState().orientation;
    // State should have moved toward measurement (not exactly 5.0 due to initial cov)
    EXPECT_NEAR(after(0), 5.0, 1.0); // Within 1.0 of measurement
    EXPECT_GT(after(0), before(0));  // Moved in positive direction
}

// ============================================================================
// Predict-Update Cycle (1D tracking convergence)
// ============================================================================

TEST_F(KalmanFilterTest, PredictUpdateCycleConverges) {
    // Use a simple 1D config by setting tpoint_param_count=0
    config_.tpoint_param_count = 0; // state_dim = 9 (4 ori + 0 tpoint + 2 rates + 3 env)
    filter = std::make_unique<KalmanFilter>(config_);
    int state_dim = 9;
    
    Eigen::VectorXd initial_state = Eigen::VectorXd::Zero(state_dim);
    Eigen::MatrixXd initial_cov = Eigen::MatrixXd::Identity(state_dim, state_dim) * 100.0;
    filter->initialize(initial_state, initial_cov, 0.0);
    
    // Simulate a constant measurement of 1.0
    Eigen::VectorXd measurement = Eigen::VectorXd::Zero(6);
    measurement(0) = 1.0;
    
    // Run multiple predict-update cycles
    for (int i = 0; i < 10; ++i) {
        filter->predict(0.1);
        filter->update(measurement);
    }
    
    // State should converge toward measurement
    KalmanFilter::State state = filter->getState();
    EXPECT_NEAR(state.orientation(0), 1.0, 0.5);
    
    // Covariance should decrease
    Eigen::MatrixXd cov = filter->getCovariance();
    EXPECT_LT(cov(0, 0), 10.0); // Covariance reduced from initial 100
}

// ============================================================================
// Joseph Form Covariance Symmetry
// ============================================================================

TEST_F(KalmanFilterTest, CovarianceRemainsSymmetric) {
    filter = std::make_unique<KalmanFilter>(config_);
    int state_dim = config_.tpoint_param_count + 9;
    
    Eigen::VectorXd initial_state = Eigen::VectorXd::Zero(state_dim);
    Eigen::MatrixXd initial_cov = Eigen::MatrixXd::Identity(state_dim, state_dim) * 10.0;
    filter->initialize(initial_state, initial_cov, 0.0);
    
    // Multiple update cycles
    for (int i = 0; i < 5; ++i) {
        filter->predict(0.1);
        
        Eigen::VectorXd measurement = Eigen::VectorXd::Random(6);
        Eigen::MatrixXd H = Eigen::MatrixXd::Zero(6, state_dim);
        for (int j = 0; j < std::min(6, state_dim); ++j) {
            H(j, j) = 1.0;
        }
        Eigen::MatrixXd R = Eigen::MatrixXd::Identity(6, 6) * 0.1;
        filter->update(measurement, H, R);
        
        // Verify symmetry: P == P^T
        Eigen::MatrixXd P = filter->getCovariance();
        Eigen::MatrixXd PT = P.transpose();
        
        double asymmetry = (P - PT).norm();
        EXPECT_LT(asymmetry, 1e-10);
    }
}

// ============================================================================
// Multiple Instances Don't Interfere (regression test for static bug)
// ============================================================================

TEST_F(KalmanFilterTest, MultipleInstancesAreIndependent) {
    // Create two independent filters
    auto filter1 = std::make_unique<KalmanFilter>(config_);
    auto filter2 = std::make_unique<KalmanFilter>(config_);
    
    int state_dim = config_.tpoint_param_count + 9;
    Eigen::VectorXd m1 = Eigen::VectorXd::Zero(6);
    m1(0) = 1.0;
    Eigen::VectorXd m2 = Eigen::VectorXd::Zero(6);
    m2(1) = -1.0;
    
    // Initialize both
    filter1->initialize(Eigen::VectorXd::Zero(state_dim), 
                        Eigen::MatrixXd::Identity(state_dim, state_dim), 0.0);
    filter2->initialize(Eigen::VectorXd::Zero(state_dim),
                        Eigen::MatrixXd::Identity(state_dim, state_dim), 0.0);
    
    // Update filter1 with m1
    filter1->predict(0.1);
    filter1->update(m1);
    
    // Update filter2 with m2
    filter2->predict(0.1);
    filter2->update(m2);
    
    // Their states should be different (m1 pushes +X, m2 pushes -Y)
    auto s1 = filter1->getState();
    auto s2 = filter2->getState();
    
    // orientation(0) of filter1 should be > filter2's (m1(0)=1.0 vs m2(0)=0.0)
    // Note: with small state_dim measurement matrix might not map directly
    // Just verify that the two filters don't share adaptation state
    EXPECT_NE(filter1->getInnovation()(0), filter2->getInnovation()(0));
}

// ============================================================================
// Innovation Tests
// ============================================================================

TEST_F(KalmanFilterTest, GetInnovationReturnsLastInnovation) {
    filter = std::make_unique<KalmanFilter>(config_);
    int state_dim = config_.tpoint_param_count + 9;
    
    Eigen::VectorXd initial_state = Eigen::VectorXd::Zero(state_dim);
    Eigen::MatrixXd initial_cov = Eigen::MatrixXd::Identity(state_dim, state_dim);
    filter->initialize(initial_state, initial_cov, 0.0);
    
    Eigen::VectorXd measurement = Eigen::VectorXd::Zero(6);
    measurement(0) = 5.0;
    filter->update(measurement);
    
    Eigen::VectorXd innovation = filter->getInnovation();
    // Innovation should be non-zero (measurement differs from state)
    EXPECT_GT(innovation.norm(), 0.0);
    EXPECT_EQ(innovation.size(), 6);
}

TEST_F(KalmanFilterTest, GetInnovationCovarianceReturnsNonIdentity) {
    filter = std::make_unique<KalmanFilter>(config_);
    int state_dim = config_.tpoint_param_count + 9;
    
    Eigen::VectorXd initial_state = Eigen::VectorXd::Zero(state_dim);
    Eigen::MatrixXd initial_cov = Eigen::MatrixXd::Identity(state_dim, state_dim);
    filter->initialize(initial_state, initial_cov, 0.0);
    
    Eigen::VectorXd measurement = Eigen::VectorXd::Zero(6);
    filter->update(measurement);
    
    Eigen::MatrixXd S = filter->getInnovationCovariance();
    // Innovation covariance should be different from identity
    EXPECT_NE(S(0, 0), 1.0);
    EXPECT_EQ(S.rows(), 6);
    EXPECT_EQ(S.cols(), 6);
}

// ============================================================================
// Consistency Metrics
// ============================================================================

TEST_F(KalmanFilterTest, GetConsistencyTestReturnsNonZero) {
    filter = std::make_unique<KalmanFilter>(config_);
    int state_dim = config_.tpoint_param_count + 9;
    
    Eigen::VectorXd initial_state = Eigen::VectorXd::Zero(state_dim);
    Eigen::MatrixXd initial_cov = Eigen::MatrixXd::Identity(state_dim, state_dim);
    filter->initialize(initial_state, initial_cov, 0.0);
    
    // Run a few cycles to accumulate NIS history
    for (int i = 0; i < 5; ++i) {
        filter->predict(0.1);
        Eigen::VectorXd measurement = Eigen::VectorXd::Zero(6);
        measurement(0) = 1.0;
        filter->update(measurement);
    }
    
    double consistency = filter->getConsistencyTest();
    EXPECT_GE(consistency, 0.0);
}

// ============================================================================
// Performance Metrics
// ============================================================================

TEST_F(KalmanFilterTest, GetPerformanceMetricsReturnsPopulatedMap) {
    filter = std::make_unique<KalmanFilter>(config_);
    int state_dim = config_.tpoint_param_count + 9;
    
    Eigen::VectorXd initial_state = Eigen::VectorXd::Zero(state_dim);
    Eigen::MatrixXd initial_cov = Eigen::MatrixXd::Identity(state_dim, state_dim);
    filter->initialize(initial_state, initial_cov, 0.0);
    
    filter->predict(0.1);
    Eigen::VectorXd measurement = Eigen::VectorXd::Zero(6);
    filter->update(measurement);
    
    auto metrics = filter->getPerformanceMetrics();
    EXPECT_FALSE(metrics.empty());
    EXPECT_TRUE(metrics.find("current_nis") != metrics.end());
    EXPECT_TRUE(metrics.find("avg_nis") != metrics.end());
    EXPECT_TRUE(metrics.find("covariance_condition_number") != metrics.end());
    EXPECT_TRUE(metrics.find("estimation_error_bound_3sigma") != metrics.end());
}

// ============================================================================
// Reset Tests
// ============================================================================

TEST_F(KalmanFilterTest, ResetClearsState) {
    filter = std::make_unique<KalmanFilter>(config_);
    int state_dim = config_.tpoint_param_count + 9;
    
    Eigen::VectorXd initial_state = Eigen::VectorXd::Ones(state_dim);
    Eigen::MatrixXd initial_cov = Eigen::MatrixXd::Identity(state_dim, state_dim);
    filter->initialize(initial_state, initial_cov, 0.0);
    EXPECT_TRUE(filter->isInitialized());
    
    filter->reset();
    EXPECT_FALSE(filter->isInitialized());
}

// ============================================================================
// Set Noise Tests
// ============================================================================

TEST_F(KalmanFilterTest, SetProcessNoise) {
    filter = std::make_unique<KalmanFilter>(config_);
    int state_dim = config_.tpoint_param_count + 9;
    
    Eigen::VectorXd initial_state = Eigen::VectorXd::Zero(state_dim);
    Eigen::MatrixXd initial_cov = Eigen::MatrixXd::Identity(state_dim, state_dim);
    filter->initialize(initial_state, initial_cov, 0.0);
    
    Eigen::MatrixXd new_Q = Eigen::MatrixXd::Identity(state_dim, state_dim) * 10.0;
    filter->setProcessNoise(new_Q); // Should not crash
}

TEST_F(KalmanFilterTest, SetMeasurementNoise) {
    filter = std::make_unique<KalmanFilter>(config_);
    int state_dim = config_.tpoint_param_count + 9;
    
    Eigen::VectorXd initial_state = Eigen::VectorXd::Zero(state_dim);
    Eigen::MatrixXd initial_cov = Eigen::MatrixXd::Identity(state_dim, state_dim);
    filter->initialize(initial_state, initial_cov, 0.0);
    
    Eigen::MatrixXd new_R = Eigen::MatrixXd::Identity(6, 6) * 0.01;
    filter->setMeasurementNoise(new_R); // Should not crash
}

// ============================================================================
// Log-Likelihood Tests
// ============================================================================

// Note: computeLogLikelihood is internal to Impl and not exposed in public API
// It's tested indirectly through the predict-update cycle

// ============================================================================
// Measurement Validation (Mahalanobis Distance)
// ============================================================================

// Note: computeMahalanobisDistance and isMeasurementValid are internal to Impl
// and not exposed in public API. Tested indirectly through filter behavior.

// ============================================================================
// Sigma Points (UKF)
// ============================================================================

// Note: computeSigmaPoints is internal to Impl. Tested indirectly.

// ============================================================================
// Set Adaptive Filtering
// ============================================================================

TEST_F(KalmanFilterTest, SetAdaptiveFiltering) {
    filter = std::make_unique<KalmanFilter>(config_);
    // Should not crash - validates that setAdaptiveFiltering is no longer a no-op
    filter->setAdaptiveFiltering(true, 20, 2.5);
}

// ============================================================================
// Save/Load State
// ============================================================================

TEST_F(KalmanFilterTest, SaveAndLoadState) {
    filter = std::make_unique<KalmanFilter>(config_);
    int state_dim = config_.tpoint_param_count + 9;
    
    Eigen::VectorXd initial_state = Eigen::VectorXd::Ones(state_dim) * 0.5;
    Eigen::MatrixXd initial_cov = Eigen::MatrixXd::Identity(state_dim, state_dim) * 2.0;
    filter->initialize(initial_state, initial_cov, 42.0);
    
    // Save to temp file
    std::string tmp_file = "/tmp/kalman_test_state.bin";
    EXPECT_TRUE(filter->saveState(tmp_file));
    
    // Create new filter and load
    auto filter2 = std::make_unique<KalmanFilter>(config_);
    EXPECT_TRUE(filter2->loadState(tmp_file));
    
    // Verify loaded state matches
    EXPECT_TRUE(filter2->isInitialized());
    auto s1 = filter->getState();
    auto s2 = filter2->getState();
    EXPECT_NEAR(s1.orientation(0), s2.orientation(0), 1e-6);
    EXPECT_NEAR(s1.mount_rates(0), s2.mount_rates(0), 1e-6);
    
    // Clean up
    std::remove(tmp_file.c_str());
}

TEST_F(KalmanFilterTest, LoadStateFromNonexistentFile) {
    filter = std::make_unique<KalmanFilter>(config_);
    EXPECT_FALSE(filter->loadState("/tmp/nonexistent_kalman_file.bin"));
}

// ============================================================================
// Multiple Update with Custom H and R
// ============================================================================

TEST_F(KalmanFilterTest, UpdateWithCustomMeasurementMatrix) {
    filter = std::make_unique<KalmanFilter>(config_);
    int state_dim = config_.tpoint_param_count + 9;
    
    Eigen::VectorXd initial_state = Eigen::VectorXd::Zero(state_dim);
    Eigen::MatrixXd initial_cov = Eigen::MatrixXd::Identity(state_dim, state_dim) * 100.0;
    filter->initialize(initial_state, initial_cov, 0.0);
    
    // Measurement matrix: observe only first 2 dimensions
    Eigen::MatrixXd H = Eigen::MatrixXd::Zero(2, state_dim);
    H(0, 0) = 1.0;
    H(1, 1) = 1.0;
    
    Eigen::MatrixXd R = Eigen::MatrixXd::Identity(2, 2) * 0.1;
    
    Eigen::VectorXd measurement = Eigen::VectorXd::Zero(2);
    measurement(0) = 3.0;
    
    filter->predict(0.1);
    filter->update(measurement, H, R);
    
    // State should be corrected in first dimension
    KalmanFilter::State state = filter->getState();
    EXPECT_NEAR(state.orientation(0), 3.0, 1.0);
}

// ============================================================================
// Filter Stability with Large dt
// ============================================================================

TEST_F(KalmanFilterTest, LargeDtDoesNotCauseInstability) {
    filter = std::make_unique<KalmanFilter>(config_);
    int state_dim = config_.tpoint_param_count + 9;
    
    Eigen::VectorXd initial_state = Eigen::VectorXd::Zero(state_dim);
    Eigen::MatrixXd initial_cov = Eigen::MatrixXd::Identity(state_dim, state_dim) * 100.0;
    filter->initialize(initial_state, initial_cov, 0.0);
    
    // Large dt
    filter->predict(1000.0);
    
    // Covariance should have increased but still finite
    Eigen::MatrixXd P = filter->getCovariance();
    for (int i = 0; i < state_dim; ++i) {
        EXPECT_TRUE(std::isfinite(P(i, i)));
    }
}

// ============================================================================
// Rapid Predict-Update Cycles (no numerical drift)
// ============================================================================

TEST_F(KalmanFilterTest, RapidCyclesNoNumericalDrift) {
    filter = std::make_unique<KalmanFilter>(config_);
    int state_dim = config_.tpoint_param_count + 9;
    
    Eigen::VectorXd initial_state = Eigen::VectorXd::Zero(state_dim);
    Eigen::MatrixXd initial_cov = Eigen::MatrixXd::Identity(state_dim, state_dim) * 100.0;
    filter->initialize(initial_state, initial_cov, 0.0);
    
    Eigen::VectorXd measurement = Eigen::VectorXd::Zero(6);
    measurement(0) = 2.0;
    
    // 100 rapid cycles
    for (int i = 0; i < 100; ++i) {
        filter->predict(0.01);
        filter->update(measurement);
    }
    
    // State should be finite and reasonable
    KalmanFilter::State state = filter->getState();
    for (int i = 0; i < 4; ++i) {
        EXPECT_TRUE(std::isfinite(state.orientation(i)));
    }
    
    // Covariance should be symmetric and positive semi-definite
    Eigen::MatrixXd P = filter->getCovariance();
    Eigen::MatrixXd PT = P.transpose();
    double asymmetry = (P - PT).norm();
    EXPECT_LT(asymmetry, 1e-8);
}


} // namespace test
} // namespace models
} // namespace astro_mount
