#include "models/kalman_filter.h"
#include <Eigen/Dense>
#include <cmath>
#include <algorithm>
#include <numeric>
#include <fstream>
#include <sstream>

namespace astro_mount {
namespace models {

using Eigen::MatrixXd;
using Eigen::VectorXd;

class KalmanFilter::Impl {
public:
    Impl(const FilterConfig& config)
        : state_dim_(config.tpoint_param_count + 9),  // 4(ori) + N(tpoint) + 2(rates) + 3(env)
          measurement_dim_(6),
          tpoint_param_count_(config.tpoint_param_count),
          is_initialized_(false),
          timestamp_(0.0),

          // Adaptation state (was static)
          update_count_(0),
          sum_innovation_outer_(MatrixXd::Zero(measurement_dim_, measurement_dim_)),

          // Adaptive process noise state (was static)
          innovation_sum_(VectorXd::Zero(measurement_dim_)),
          innovation_outer_sum_(MatrixXd::Zero(measurement_dim_, measurement_dim_)),
          innovation_count_(0),

          // Adaptive filtering flags from config
          adaptive_q_enabled_(config.use_adaptive_q),
          adaptive_r_enabled_(config.use_adaptive_r),
          adaptation_window_(50),
          innovation_threshold_(config.innovation_threshold) {

        // Initialize matrices
        x_ = VectorXd::Zero(state_dim_);
        
        // Initial covariance from config
        P_ = MatrixXd::Identity(state_dim_, state_dim_) * 1000.0;
        int ori_dim = 4;
        int rates_dim = 2;
        int env_dim = 3;
        P_.block(0, 0, ori_dim, ori_dim) = MatrixXd::Identity(ori_dim, ori_dim) 
            * config.initial_orientation_uncertainty;
        P_.block(ori_dim, ori_dim, tpoint_param_count_, tpoint_param_count_) 
            = MatrixXd::Identity(tpoint_param_count_, tpoint_param_count_) 
            * config.initial_tpoint_uncertainty;
        P_.block(ori_dim + tpoint_param_count_, ori_dim + tpoint_param_count_, 
                 rates_dim, rates_dim) 
            = MatrixXd::Identity(rates_dim, rates_dim) 
            * config.initial_rate_uncertainty;
        P_.block(ori_dim + tpoint_param_count_ + rates_dim, 
                 ori_dim + tpoint_param_count_ + rates_dim, 
                 env_dim, env_dim) 
            = MatrixXd::Identity(env_dim, env_dim) * 10.0; // env uncertainty

        // State transition matrix (identity by default)
        F_ = MatrixXd::Identity(state_dim_, state_dim_);

        // Process noise from config
        Q_ = MatrixXd::Identity(state_dim_, state_dim_) * 0.01;
        Q_.block(0, 0, ori_dim, ori_dim) = MatrixXd::Identity(ori_dim, ori_dim) 
            * config.orientation_process_noise;
        Q_.block(ori_dim, ori_dim, tpoint_param_count_, tpoint_param_count_) 
            = MatrixXd::Identity(tpoint_param_count_, tpoint_param_count_) 
            * config.tpoint_process_noise;
        Q_.block(ori_dim + tpoint_param_count_, ori_dim + tpoint_param_count_, 
                 rates_dim, rates_dim) 
            = MatrixXd::Identity(rates_dim, rates_dim) 
            * config.rate_process_noise;
        Q_.block(ori_dim + tpoint_param_count_ + rates_dim, 
                 ori_dim + tpoint_param_count_ + rates_dim, 
                 env_dim, env_dim) 
            = MatrixXd::Identity(env_dim, env_dim) 
            * config.env_process_noise;

        // Measurement matrix (default: identity on observable subspace)
        H_ = MatrixXd::Zero(measurement_dim_, state_dim_);
        for (int i = 0; i < std::min(measurement_dim_, state_dim_); ++i) {
            H_(i, i) = 1.0;
        }

        // Measurement noise from config
        R_ = MatrixXd::Identity(measurement_dim_, measurement_dim_) 
            * config.position_measurement_noise;

        I_ = MatrixXd::Identity(state_dim_, state_dim_);

        // Last innovation cache
        last_innovation_ = VectorXd::Zero(measurement_dim_);
        last_innovation_cov_ = MatrixXd::Identity(measurement_dim_, measurement_dim_);

        // P_before tracking for information gain (was static, now per-instance)
        P_before_ = MatrixXd::Identity(state_dim_, state_dim_);
    }

    void initialize(const VectorXd& initial_state, const MatrixXd& initial_covariance,
                    double timestamp) {
        x_ = initial_state;
        P_ = initial_covariance;
        timestamp_ = timestamp;
        is_initialized_ = true;

        // Reset adaptation state
        resetAdaptationState();
    }

    void predict(double dt) {
        if (!is_initialized_) {
            return;
        }

        // Update state transition matrix based on dt
        updateStateTransitionMatrix(dt);

        // Predict state
        x_ = F_ * x_;

        // Predict covariance (Joseph form for numerical stability)
        P_ = F_ * P_ * F_.transpose() + Q_;

        // Symmetrize to prevent asymmetry drift
        P_ = 0.5 * (P_ + P_.transpose());

        // Save predicted state/covariance and transition matrix for RTS smoother
        x_pred_ = x_;
        P_pred_ = P_;
        F_for_rts_ = F_;

        // Update timestamp
        timestamp_ += dt;
    }

    void update(const VectorXd& measurement, const MatrixXd& measurement_matrix,
                const MatrixXd& measurement_noise) {
        if (!is_initialized_) {
            return;
        }

        H_ = measurement_matrix;
        R_ = measurement_noise;

        // Calculate innovation
        VectorXd y = measurement - H_ * x_;
        last_innovation_ = y;

        // Calculate innovation covariance S = H*P*H^T + R using LDLT solver
        MatrixXd HPHt = H_ * P_ * H_.transpose();
        last_innovation_cov_ = HPHt + R_;

        // Solve for Kalman gain: K = P * H^T * S^{-1}
        // Using LDLT decomposition instead of explicit inverse
        Eigen::LDLT<MatrixXd> ldlt_S(last_innovation_cov_);
        if (ldlt_S.info() != Eigen::Success) {
            // Fallback: use regular inverse if LDLT fails
            MatrixXd S_inv = last_innovation_cov_.inverse();
            MatrixXd K = P_ * H_.transpose() * S_inv;
            applyKalmanUpdate(y, K);
            return;
        }

        // K = P * H^T * S^{-1}
        // Solve: S * K^T = H * P  =>  K^T = S.solve(H * P)
        // H_P = H * P is (measurement_dim × state_dim), S is (measurement_dim × measurement_dim)
        // K^T = S.solve(H_P) gives (measurement_dim × state_dim)
        // K = (K^T)^T gives (state_dim × measurement_dim)
        MatrixXd H_P = H_ * P_;
        MatrixXd K_transpose = ldlt_S.solve(H_P);
        MatrixXd K = K_transpose.transpose();

        applyKalmanUpdate(y, K);
    }

    void applyKalmanUpdate(const VectorXd& innovation, const MatrixXd& K) {
        // Save pre-update predicted values for RTS smoother
        // (x_pred_, P_pred_, F_for_rts_ were set by predict())

        // Update state estimate
        x_ = x_ + K * innovation;

        // Update covariance estimate using Joseph form for numerical stability
        // P = (I - K*H) * P * (I - K*H)^T + K * R * K^T
        MatrixXd I_KH = I_ - K * H_;
        P_ = I_KH * P_ * I_KH.transpose() + K * R_ * K.transpose();

        // Symmetrize to prevent asymmetry drift
        P_ = 0.5 * (P_ + P_.transpose());

        // Store this step in RTS history (predicted + filtered + transition matrix)
        rts_history_.push_back({x_pred_, P_pred_, x_, P_, F_for_rts_});
        if (rts_history_.size() > 50) {
            rts_history_.erase(rts_history_.begin());
        }

        // Adaptive noise estimation
        if (adaptive_q_enabled_ || adaptive_r_enabled_) {
            adaptiveNoiseEstimation(innovation, K);
        }
    }

    void adaptiveNoiseEstimation(const VectorXd& innovation, const MatrixXd& K) {
        update_count_++;

        // Update innovation statistics with forgetting factor
        const double forgetting_factor = 0.95;
        if (update_count_ == 1) {
            sum_innovation_outer_ = innovation * innovation.transpose();
        } else {
            sum_innovation_outer_ = forgetting_factor * sum_innovation_outer_ 
                                  + innovation * innovation.transpose();
        }

        if (update_count_ < adaptation_window_) {
            return; // Wait for enough data
        }

        // Compute estimated innovation covariance
        double effective_count = (1.0 - std::pow(forgetting_factor, update_count_)) 
                               / (1.0 - forgetting_factor);
        MatrixXd estimated_S = sum_innovation_outer_ / effective_count;

        if (adaptive_r_enabled_) {
            // Adapt measurement noise R
            MatrixXd HPHt = H_ * P_ * H_.transpose();
            MatrixXd R_estimated = estimated_S - HPHt;

            // Ensure R is positive definite
            for (int i = 0; i < R_estimated.rows(); ++i) {
                R_estimated(i, i) = std::max(R_estimated(i, i), 0.1 * R_(i, i));
                for (int j = 0; j < R_estimated.cols(); ++j) {
                    if (i != j) {
                        R_estimated(i, j) *= 0.5; // Reduce off-diagonal terms
                    }
                }
            }

            // Blend with current R
            const double adaptation_rate = 0.1;
            R_ = (1.0 - adaptation_rate) * R_ + adaptation_rate * R_estimated;
        }

        if (adaptive_q_enabled_) {
            // Adapt process noise Q based on state update magnitude
            double state_update_norm = (K * innovation).norm();
            if (state_update_norm > 0.1) {
                double scale_factor = 1.0 + 0.1 * state_update_norm;
                Q_ *= scale_factor;
            } else if (state_update_norm < 0.01) {
                Q_ *= 0.99;
            }

            // Clamp Q to reasonable bounds
            for (int i = 0; i < Q_.rows(); ++i) {
                Q_(i, i) = std::max(1e-6, std::min(Q_(i, i), 100.0));
            }
        }
    }

    void update(const VectorXd& measurement) {
        // Use default measurement matrix (identity on observable subspace)
        MatrixXd H = MatrixXd::Zero(measurement_dim_, state_dim_);
        for (int i = 0; i < std::min(measurement_dim_, state_dim_); ++i) {
            H(i, i) = 1.0;
        }
        update(measurement, H, R_);
    }

    VectorXd getState() const {
        return x_;
    }

    KalmanFilter::State getMappedState() const {
        KalmanFilter::State state;
        if (!is_initialized_ || state_dim_ < 4 + tpoint_param_count_ + 2 + 3) {
            return state;
        }

        int idx = 0;

        // Orientation quaternion (4)
        state.orientation = x_.segment(idx, 4);
        idx += 4;

        // TPOINT parameters (tpoint_param_count_)
        state.tpoint_params = x_.segment(idx, tpoint_param_count_);
        idx += tpoint_param_count_;

        // Mount rates (2)
        state.mount_rates = x_.segment(idx, 2);
        idx += 2;

        // Environmental parameters (3)
        state.temperature = x_(idx++);
        state.pressure = x_(idx++);
        state.humidity = x_(idx);

        state.timestamp = std::chrono::system_clock::now();
        return state;
    }

    MatrixXd getCovariance() const {
        return P_;
    }

    VectorXd getLastInnovation() const {
        return last_innovation_;
    }

    MatrixXd getLastInnovationCovariance() const {
        return last_innovation_cov_;
    }

    double getTimestamp() const {
        return timestamp_;
    }

    bool isInitialized() const {
        return is_initialized_;
    }

    void setProcessNoise(const MatrixXd& Q) {
        Q_ = Q;
    }

    void setMeasurementNoise(const MatrixXd& R) {
        R_ = R;
    }

    void setStateTransitionMatrix(const MatrixXd& F) {
        F_ = F;
    }

    MatrixXd computeInnovationCovariance() const {
        return H_ * P_ * H_.transpose() + R_;
    }

    VectorXd computeInnovation(const VectorXd& measurement) const {
        return measurement - H_ * x_;
    }

    double computeLogLikelihood(const VectorXd& measurement) const {
        VectorXd innovation = computeInnovation(measurement);
        MatrixXd S = computeInnovationCovariance();

        // Use Cholesky decomposition for numerical stability
        Eigen::LLT<MatrixXd> llt(S);
        if (llt.info() == Eigen::NumericalIssue) {
            return -1e9; // Filter degraded
        }

        // Log-determinant: log|S| = 2 * sum(log(diag(L)))
        const MatrixXd& L = llt.matrixL();
        double log_det_S = 0.0;
        for (int i = 0; i < L.rows(); ++i) {
            log_det_S += std::log(std::abs(L(i, i)));
        }
        log_det_S *= 2.0;

        // Innovation term: innovation^T * S^{-1} * innovation
        // Solve L * y = innovation, then y^T * y
        VectorXd y = llt.solve(innovation);
        double innovation_term = y.squaredNorm();

        double constant_term = measurement_dim_ * std::log(2.0 * M_PI);

        return -0.5 * (innovation_term + log_det_S + constant_term);
    }

    MatrixXd computeStateUncertainty() const {
        return P_;
    }

    std::vector<double> getStateUncertainties() const {
        std::vector<double> uncertainties;
        uncertainties.reserve(state_dim_);

        for (int i = 0; i < state_dim_; ++i) {
            uncertainties.push_back(std::sqrt(std::abs(P_(i, i))));
        }

        return uncertainties;
    }

    double computeMahalanobisDistance(const VectorXd& measurement) const {
        VectorXd innovation = computeInnovation(measurement);
        MatrixXd S = computeInnovationCovariance();

        // Use LDLT solver instead of explicit inverse
        Eigen::LDLT<MatrixXd> ldlt_S(S);
        if (ldlt_S.info() != Eigen::Success) {
            return 1e9 * innovation.norm(); // Degenerate case
        }

        VectorXd solved = ldlt_S.solve(innovation);
        return std::sqrt(std::abs(innovation.dot(solved)));
    }

    bool isMeasurementValid(const VectorXd& measurement, double threshold = 3.0) const {
        double mahalanobis_distance = computeMahalanobisDistance(measurement);
        return mahalanobis_distance < threshold;
    }

    VectorXd computeSigmaPoints() const {
        // Unscented Kalman Filter (UKF) sigma points
        int n = state_dim_;
        double alpha = 1e-3;
        double beta = 2.0;
        double kappa = 0.0;
        double lambda = alpha * alpha * (n + kappa) - n;

        MatrixXd sigma_points(n, 2 * n + 1);

        // Compute square root of covariance using LDLT for semi-positive definite matrices
        Eigen::LDLT<MatrixXd> ldlt_P(P_);
        MatrixXd sqrtP;
        if (ldlt_P.info() == Eigen::Success) {
            // LDLT gives L * D * L^T, we need sqrt(P) = L * sqrt(D)
            MatrixXd L = ldlt_P.matrixL();
            VectorXd D = ldlt_P.vectorD();
            // Clamp negative eigenvalues (numerical drift)
            for (int i = 0; i < D.size(); ++i) {
                D(i) = std::max(D(i), 0.0);
            }
            sqrtP = L * D.array().sqrt().matrix().asDiagonal();
        } else {
            // Fallback to LLT with regularization
            MatrixXd P_reg = P_ + MatrixXd::Identity(n, n) * 1e-6;
            Eigen::LLT<MatrixXd> llt_P(P_reg);
            if (llt_P.info() != Eigen::Success) {
                return VectorXd::Zero(n * (2 * n + 1));
            }
            sqrtP = llt_P.matrixL();
        }

        // Mean point
        sigma_points.col(0) = x_;

        // Generate sigma points
        double scale = std::sqrt(n + lambda);
        for (int i = 0; i < n; ++i) {
            sigma_points.col(i + 1) = x_ + scale * sqrtP.col(i);
            sigma_points.col(i + n + 1) = x_ - scale * sqrtP.col(i);
        }

        return sigma_points.reshaped(); // Return as vector
    }

    std::pair<double, double> computeConsistencyMetrics() const {
        // Compute Normalized Innovation Squared (NIS) and consistency test
        // For a consistent filter, NIS should follow chi-square distribution

        double current_nis = 0.0;
        if (measurement_dim_ > 0 && last_innovation_cov_.norm() > 0) {
            // Use last innovation and its covariance
            VectorXd innovation = last_innovation_;

            // NIS = innovation^T * S^{-1} * innovation
            Eigen::LDLT<MatrixXd> ldlt_S(last_innovation_cov_);
            if (ldlt_S.info() == Eigen::Success) {
                VectorXd solved = ldlt_S.solve(innovation);
                current_nis = innovation.dot(solved);
            } else {
                // Fallback: trace-based approximation
                current_nis = last_innovation_cov_.trace() / measurement_dim_;
            }

            nis_history_.push_back(current_nis);
            if (nis_history_.size() > 100) {
                nis_history_.erase(nis_history_.begin());
            }
        }

        // Compute average NIS over window
        double avg_nis = 0.0;
        if (!nis_history_.empty()) {
            avg_nis = std::accumulate(nis_history_.begin(), nis_history_.end(), 0.0) 
                    / nis_history_.size();
        }

        return {current_nis, avg_nis};
    }

    void applyRTSsmoother() {
        // Rauch-Tung-Striebel (RTS) smoother implementation
        // True backward recursion through stored history
        //
        // Forward pass (Kalman filter) stores at each step:
        //   x_pred, P_pred  (after predict())
        //   x_filt, P_filt  (after update())
        //   F               (state transition matrix)
        //
        // Backward recursion (RTS smoother):
        //   G_k     = P_filt[k] * F_{k+1}^T * P_pred[k+1]^{-1}
        //   x_s[k]  = x_filt[k] + G_k * (x_s[k+1] - x_pred[k+1])
        //   P_s[k]  = P_filt[k] + G_k * (P_s[k+1] - P_pred[k+1]) * G_k^T

        int n = rts_history_.size();
        if (n < 2) {
            return;  // Need at least 2 steps for meaningful smoothing
        }

        // Create smoothed state/covariance arrays
        std::vector<VectorXd> x_smooth(n);
        std::vector<MatrixXd> P_smooth(n);

        // Initialize the backward recursion with filtered values
        // (the last filtered estimate equals the last smoothed estimate)
        for (int i = 0; i < n; ++i) {
            x_smooth[i] = rts_history_[i].x_filt;
            P_smooth[i] = rts_history_[i].P_filt;
        }

        // Backward recursion: k = n-2 down to 0
        for (int k = n - 2; k >= 0; --k) {
            const auto& step = rts_history_[k];       // current step (k)
            const auto& next_step = rts_history_[k + 1]; // next step (k+1)

            // Compute RTS gain: G_k = P_filt[k] * F_{k+1}^T * P_pred[k+1]^{-1}
            // Solve P_pred[k+1] * G_k^T = F_{k+1} * P_filt[k]
            // using LDLT decomposition for numerical stability
            Eigen::LDLT<MatrixXd> ldlt_P_pred(next_step.P_pred);
            if (ldlt_P_pred.info() != Eigen::Success) {
                // Skip this step if P_pred is not positive definite
                continue;
            }

            // Compute A = F_{k+1} * P_filt[k]
            // Then solve P_pred[k+1] * G_k^T = A  for G_k^T
            MatrixXd A = next_step.F * step.P_filt;
            MatrixXd G_k_transpose = ldlt_P_pred.solve(A);
            MatrixXd G_k = G_k_transpose.transpose();

            // Smooth state: x_s[k] = x_filt[k] + G_k * (x_s[k+1] - x_pred[k+1])
            VectorXd state_diff = x_smooth[k + 1] - next_step.x_pred;
            x_smooth[k] = step.x_filt + G_k * state_diff;

            // Smooth covariance: P_s[k] = P_filt[k] + G_k * (P_s[k+1] - P_pred[k+1]) * G_k^T
            MatrixXd cov_diff = P_smooth[k + 1] - next_step.P_pred;
            P_smooth[k] = step.P_filt + G_k * cov_diff * G_k.transpose();

            // Symmetrize to prevent asymmetry drift
            P_smooth[k] = 0.5 * (P_smooth[k] + P_smooth[k].transpose());
        }

        // Update current state with the smoothed estimate at the most recent step
        x_ = x_smooth[n - 1];
        P_ = P_smooth[n - 1];

        // Ensure symmetry
        P_ = 0.5 * (P_ + P_.transpose());
    }

    void applyAdaptiveProcessNoise() {
        // Adaptive process noise estimation based on innovation statistics
        if (!adaptive_q_enabled_) {
            return;
        }

        VectorXd innovation = last_innovation_;

        innovation_sum_ += innovation;
        innovation_outer_sum_ += innovation * innovation.transpose();
        innovation_count_++;

        if (innovation_count_ >= adaptation_window_) {
            // Compute innovation statistics
            VectorXd mean_innovation = innovation_sum_ / innovation_count_;
            MatrixXd innovation_cov = innovation_outer_sum_ / innovation_count_ 
                                    - mean_innovation * mean_innovation.transpose();

            // Update process noise based on innovation statistics
            // Q_new = Q_old * (1 - alpha) + alpha * (K * innovation_cov * K^T)
            const double alpha = 0.1;

            // Reconstruct Kalman gain approximation
            Eigen::LDLT<MatrixXd> ldlt_S(last_innovation_cov_);
            MatrixXd K;
            if (ldlt_S.info() == Eigen::Success) {
                MatrixXd H_P = H_ * P_;
                MatrixXd K_transpose = ldlt_S.solve(H_P.transpose());
                K = K_transpose.transpose();
            } else {
                K = MatrixXd::Zero(state_dim_, measurement_dim_);
            }

            MatrixXd Q_update = K * innovation_cov * K.transpose();

            // Ensure positive definiteness
            Q_update = 0.5 * (Q_update + Q_update.transpose());
            for (int i = 0; i < Q_update.rows(); ++i) {
                Q_update(i, i) = std::max(Q_update(i, i), 1e-6);
            }

            Q_ = (1.0 - alpha) * Q_ + alpha * Q_update;

            // Symmetrize Q
            Q_ = 0.5 * (Q_ + Q_.transpose());

            // Reset statistics
            innovation_sum_.setZero();
            innovation_outer_sum_.setZero();
            innovation_count_ = 0;
        }
    }

    double computeCramerRaoLowerBound() const {
        // Compute Cramer-Rao Lower Bound (CRLB) for state estimation
        // CRLB = trace(FIM^{-1}) where FIM is Fisher Information Matrix

        // Build FIM = H^T * R^{-1} * H + P^{-1}
        // Use LDLT for both inversions to avoid explicit matrix inverse
        Eigen::LDLT<MatrixXd> ldlt_R(R_);
        if (ldlt_R.info() != Eigen::Success) {
            return -1.0;
        }

        // H^T * R^{-1} * H
        MatrixXd Ht_Rinv_H = H_.transpose() * ldlt_R.solve(H_);

        Eigen::LDLT<MatrixXd> ldlt_P(P_);
        if (ldlt_P.info() != Eigen::Success) {
            MatrixXd FIM = Ht_Rinv_H + MatrixXd::Identity(state_dim_, state_dim_) * 1e-6;
            Eigen::LDLT<MatrixXd> ldlt_fim(FIM);
            if (ldlt_fim.info() != Eigen::Success) {
                return -1.0;
            }
            MatrixXd FIM_inv = ldlt_fim.solve(MatrixXd::Identity(state_dim_, state_dim_));
            return FIM_inv.trace();
        }

        MatrixXd FIM = Ht_Rinv_H + ldlt_P.solve(MatrixXd::Identity(state_dim_, state_dim_));

        // Ensure symmetry
        FIM = 0.5 * (FIM + FIM.transpose());

        // Invert FIM using LDLT
        Eigen::LDLT<MatrixXd> ldlt_fim(FIM);
        if (ldlt_fim.info() != Eigen::Success) {
            return -1.0;
        }

        MatrixXd FIM_inv = ldlt_fim.solve(MatrixXd::Identity(state_dim_, state_dim_));
        return FIM_inv.trace();
    }

    std::map<std::string, double> getAdvancedMetrics() const {
        std::map<std::string, double> metrics;

        // Compute various advanced metrics
        auto [current_nis, avg_nis] = computeConsistencyMetrics();
        metrics["current_nis"] = current_nis;
        metrics["avg_nis"] = avg_nis;

        // Compute condition number of covariance matrix using JacobiSVD
        Eigen::JacobiSVD<MatrixXd> svd(P_);
        if (svd.singularValues().size() > 1 && svd.singularValues()(svd.singularValues().size() - 1) > 0) {
            double cond_number = svd.singularValues()(0) / svd.singularValues()(svd.singularValues().size() - 1);
            metrics["covariance_condition_number"] = cond_number;
        } else {
            metrics["covariance_condition_number"] = -1.0;
        }

        // Compute effective degrees of freedom
        double trace_HPHt = (H_ * P_ * H_.transpose()).trace();
        double trace_R = R_.trace();
        double edf = (trace_R > 0) ? (measurement_dim_ - trace_HPHt / trace_R) : 0.0;
        metrics["effective_degrees_of_freedom"] = edf;

        // Compute estimation error bound (3-sigma)
        double error_bound = 3.0 * std::sqrt(std::abs(P_.diagonal().maxCoeff()));
        metrics["estimation_error_bound_3sigma"] = error_bound;

        // Compute filter stability metric
        MatrixXd F_plus_Ft = F_ + F_.transpose();
        Eigen::SelfAdjointEigenSolver<MatrixXd> eigensolver(F_plus_Ft);
        double max_eigenvalue = eigensolver.eigenvalues().maxCoeff();
        metrics["filter_stability"] = (max_eigenvalue < 0) ? 1.0 : 0.0;

        // Compute information gain
        double det_P_before_metric = P_before_.determinant();
        double det_P_after = P_.determinant();
        double information_gain = (det_P_before_metric > 0 && det_P_after > 0)
            ? std::log(det_P_before_metric / det_P_after) : 0.0;
        metrics["information_gain"] = information_gain;
        P_before_ = P_;

        return metrics;
    }

    void reset() {
        x_ = VectorXd::Zero(state_dim_);
        P_ = MatrixXd::Identity(state_dim_, state_dim_) * 1000.0;
        is_initialized_ = false;
        timestamp_ = 0.0;

        // Reset all adaptation state
        resetAdaptationState();
    }

    void resetAdaptationState() {
        update_count_ = 0;
        sum_innovation_outer_ = MatrixXd::Zero(measurement_dim_, measurement_dim_);
        innovation_sum_ = VectorXd::Zero(measurement_dim_);
        innovation_outer_sum_ = MatrixXd::Zero(measurement_dim_, measurement_dim_);
        innovation_count_ = 0;
        rts_history_.clear();
        nis_history_.clear();
        last_innovation_ = VectorXd::Zero(measurement_dim_);
        last_innovation_cov_ = MatrixXd::Identity(measurement_dim_, measurement_dim_);
    }

    bool saveToFile(const std::string& filename) const {
        std::ofstream file(filename, std::ios::binary);
        if (!file.is_open()) {
            return false;
        }

        // Write state vector
        int rows = x_.rows();
        int cols = 1;
        file.write(reinterpret_cast<const char*>(&rows), sizeof(rows));
        file.write(reinterpret_cast<const char*>(&cols), sizeof(cols));
        file.write(reinterpret_cast<const char*>(x_.data()), rows * cols * sizeof(double));

        // Write covariance matrix
        rows = P_.rows();
        cols = P_.cols();
        file.write(reinterpret_cast<const char*>(&rows), sizeof(rows));
        file.write(reinterpret_cast<const char*>(&cols), sizeof(cols));
        file.write(reinterpret_cast<const char*>(P_.data()), rows * cols * sizeof(double));

        // Write timestamp and initialized flag
        file.write(reinterpret_cast<const char*>(&timestamp_), sizeof(timestamp_));
        file.write(reinterpret_cast<const char*>(&is_initialized_), sizeof(is_initialized_));

        file.close();
        return true;
    }

    bool loadFromFile(const std::string& filename) {
        std::ifstream file(filename, std::ios::binary);
        if (!file.is_open()) {
            return false;
        }

        // Read state vector
        int rows, cols;
        file.read(reinterpret_cast<char*>(&rows), sizeof(rows));
        file.read(reinterpret_cast<char*>(&cols), sizeof(cols));
        if (rows != state_dim_ || cols != 1) {
            file.close();
            return false;
        }
        VectorXd new_x(rows);
        file.read(reinterpret_cast<char*>(new_x.data()), rows * cols * sizeof(double));

        // Read covariance matrix
        file.read(reinterpret_cast<char*>(&rows), sizeof(rows));
        file.read(reinterpret_cast<char*>(&cols), sizeof(cols));
        if (rows != state_dim_ || cols != state_dim_) {
            file.close();
            return false;
        }
        MatrixXd new_P(rows, cols);
        file.read(reinterpret_cast<char*>(new_P.data()), rows * cols * sizeof(double));

        // Read timestamp and initialized flag
        double new_timestamp;
        bool new_is_initialized;
        file.read(reinterpret_cast<char*>(&new_timestamp), sizeof(new_timestamp));
        file.read(reinterpret_cast<char*>(&new_is_initialized), sizeof(new_is_initialized));

        x_ = new_x;
        P_ = new_P;
        timestamp_ = new_timestamp;
        is_initialized_ = new_is_initialized;

        file.close();
        return true;
    }

    void setAdaptiveFiltering(bool enable_q, bool enable_r, int window_size, double threshold) {
        adaptive_q_enabled_ = enable_q;
        adaptive_r_enabled_ = enable_r;
        adaptation_window_ = std::max(10, window_size);
        innovation_threshold_ = std::max(0.1, threshold);
    }

    double getConsistencyTestValue() const {
        auto [current_nis, avg_nis] = computeConsistencyMetrics();
        return avg_nis;
    }

    int getStateDimension() const { return state_dim_; }

private:
    void updateStateTransitionMatrix(double dt) {
        // Reset F to identity
        F_ = MatrixXd::Identity(state_dim_, state_dim_);

        // Simple constant velocity model for 6D test state
        // [pos_x, pos_y, pos_z, vel_x, vel_y, vel_z]
        if (state_dim_ == 6) {
            F_(0, 3) = dt;
            F_(1, 4) = dt;
            F_(2, 5) = dt;
            return;
        }

        // Mount state layout:
        //   [0..3]       = orientation quaternion (qw, qx, qy, qz)
        //   [4..4+N-1]   = TPoint parameters (static — identity transition)
        //   [4+N..4+N+1] = mount rates (rate1, rate2)
        //   [4+N+2..]    = environmental params (static — identity transition)
        //
        // Kinematic coupling: quaternion derivative from angular velocity
        //   dq/dt = 0.5 * ω_quat ⊗ q
        //   where ω_quat = [0, ω_x, ω_y, ω_z]
        //
        // Mount axes (body-frame rotation assumptions):
        //   rate1 → rotation about body x-axis
        //   rate2 → rotation about body y-axis
        //   ω = [0, rate1, rate2, 0]
        //
        // Quaternion product p ⊗ q  (p = [0, rate1, rate2, 0]):
        //   dw/dt = 0.5 * (-rate1*qx - rate2*qy)
        //   dx/dt = 0.5 * ( rate1*qw + rate2*qz)
        //   dy/dt = 0.5 * ( rate2*qw - rate1*qz)
        //   dz/dt = 0.5 * (-rate2*qx + rate1*qy)
        //
        // F(i, j) = ∂(q_pred_i)/∂(rate_j) evaluated at current state x_

        int rates_offset = 4 + tpoint_param_count_;
        if (state_dim_ >= rates_offset + 2) {
            // Extract current quaternion from state
            double qw = x_(0);
            double qx = x_(1);
            double qy = x_(2);
            double qz = x_(3);

            const double half_dt = 0.5 * dt;

            // ∂q_pred / ∂rate1
            F_(0, rates_offset)     = -half_dt * qx;  // dw/d(rate1)
            F_(1, rates_offset)     =  half_dt * qw;  // dx/d(rate1)
            F_(2, rates_offset)     = -half_dt * qz;  // dy/d(rate1)
            F_(3, rates_offset)     =  half_dt * qy;  // dz/d(rate1)

            // ∂q_pred / ∂rate2
            F_(0, rates_offset + 1) = -half_dt * qy;  // dw/d(rate2)
            F_(1, rates_offset + 1) =  half_dt * qz;  // dx/d(rate2)
            F_(2, rates_offset + 1) =  half_dt * qw;  // dy/d(rate2)
            F_(3, rates_offset + 1) = -half_dt * qx;  // dz/d(rate2)

            // rates follow constant-velocity model (identity on diagonal)
            // already set by F_ = Identity above
        }
        // TPoint and env params are static (identity transition)
    }

    int state_dim_;
    int measurement_dim_;
    int tpoint_param_count_;
    bool is_initialized_;
    double timestamp_;

    // Adaptive filtering flags
    bool adaptive_q_enabled_;
    bool adaptive_r_enabled_;
    int adaptation_window_;
    double innovation_threshold_;

    // Kalman filter matrices
    VectorXd x_;  // State vector
    MatrixXd P_;  // State covariance
    MatrixXd F_;  // State transition matrix
    MatrixXd Q_;  // Process noise covariance
    MatrixXd H_;  // Measurement matrix
    MatrixXd R_;  // Measurement noise covariance
    MatrixXd I_;  // Identity matrix

    // Last innovation cache (for getInnovation/getInnovationCovariance)
    VectorXd last_innovation_;
    MatrixXd last_innovation_cov_;

    // P_before for information gain tracking (was static, now per-instance)
    mutable MatrixXd P_before_;

    // Adaptation state (was static - now per-instance)
    int update_count_;
    MatrixXd sum_innovation_outer_;

    // Adaptive process noise state (was static)
    VectorXd innovation_sum_;
    MatrixXd innovation_outer_sum_;
    int innovation_count_;

    // RTS smoother structures
    struct RTSStep {
        VectorXd x_pred;   // predicted state before measurement update
        MatrixXd P_pred;   // predicted covariance before measurement update
        VectorXd x_filt;   // filtered state after measurement update
        MatrixXd P_filt;   // filtered covariance after measurement update
        MatrixXd F;        // state transition matrix used at this step
    };
    std::vector<RTSStep> rts_history_;

    // Temporary storage set by predict() and consumed by applyKalmanUpdate()
    VectorXd x_pred_;
    MatrixXd P_pred_;
    MatrixXd F_for_rts_;

    // Consistency metrics state (was static)
    mutable std::vector<double> nis_history_;
};


// Public interface implementations
KalmanFilter::KalmanFilter(const FilterConfig& config)
    : pimpl(std::make_unique<Impl>(config)) {}

KalmanFilter::~KalmanFilter() = default;

void KalmanFilter::initialize(const Eigen::VectorXd& initial_state,
                              const Eigen::MatrixXd& initial_covariance,
                              double timestamp) {
    pimpl->initialize(initial_state, initial_covariance, timestamp);
}

void KalmanFilter::predict(double dt) {
    pimpl->predict(dt);
}

void KalmanFilter::update(const Eigen::VectorXd& measurement,
                          const Eigen::MatrixXd& measurement_matrix,
                          const Eigen::MatrixXd& measurement_noise) {
    pimpl->update(measurement, measurement_matrix, measurement_noise);
}

void KalmanFilter::update(const Eigen::VectorXd& measurement) {
    pimpl->update(measurement);
}

KalmanFilter::State KalmanFilter::getState() const {
    return pimpl->getMappedState();
}

Eigen::MatrixXd KalmanFilter::getCovariance() const {
    return pimpl->getCovariance();
}

Eigen::VectorXd KalmanFilter::getInnovation() const {
    return pimpl->getLastInnovation();
}

Eigen::MatrixXd KalmanFilter::getInnovationCovariance() const {
    return pimpl->getLastInnovationCovariance();
}

std::string KalmanFilter::getStatus() const {
    return pimpl->isInitialized() ? "Initialized" : "Not initialized";
}

bool KalmanFilter::isInitialized() const {
    return pimpl->isInitialized();
}

void KalmanFilter::reset() {
    pimpl->reset();
}

void KalmanFilter::setProcessNoise(const Eigen::MatrixXd& Q) {
    pimpl->setProcessNoise(Q);
}

void KalmanFilter::setMeasurementNoise(const Eigen::MatrixXd& R) {
    pimpl->setMeasurementNoise(R);
}

std::map<std::string, double> KalmanFilter::getPerformanceMetrics() const {
    return pimpl->getAdvancedMetrics();
}

bool KalmanFilter::saveState(const std::string& filename) const {
    return pimpl->saveToFile(filename);
}

bool KalmanFilter::loadState(const std::string& filename) {
    return pimpl->loadFromFile(filename);
}

void KalmanFilter::setAdaptiveFiltering(bool enable, int window_size, double threshold) {
    // enable=true enables both Q and R adaptation
    pimpl->setAdaptiveFiltering(enable, enable, window_size, threshold);
}

double KalmanFilter::getConsistencyTest() const {
    return pimpl->getConsistencyTestValue();
}


} // namespace models
} // namespace astro_mount
