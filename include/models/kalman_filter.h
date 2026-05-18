#ifndef KALMAN_FILTER_H
#define KALMAN_FILTER_H

#include <Eigen/Dense>
#include <vector>
#include <memory>
#include <chrono>
#include <map>
#include <functional>

namespace astro_mount {
namespace models {

// Forward declaration for ExtendedKalmanFilter
class ExtendedKalmanFilter;

/**
 * @brief Extended Kalman Filter for mount parameter estimation
 * 
 * Implements an Extended Kalman Filter (EKF) for continuous refinement
 * of mount orientation and TPOINT parameters based on measurements.
 */
class KalmanFilter {
public:
    struct State {
        // Rotation quaternion (q0, q1, q2, q3)
        Eigen::Vector4d orientation;
        
        // TPOINT parameters
        Eigen::VectorXd tpoint_params;
        
        // Mount rates (rad/s)
        Eigen::Vector2d mount_rates;
        
        // Environmental parameters
        double temperature;
        double pressure;
        double humidity;
        
        std::chrono::system_clock::time_point timestamp;
    };

    struct Measurement {
        // Observed vs expected coordinates (rad)
        Eigen::Vector2d observed;
        Eigen::Vector2d expected;
        
        // Mount position (rad)
        Eigen::Vector2d mount_position;
        
        // Environmental conditions
        double temperature;
        double pressure;
        double humidity;
        
        // Measurement covariance
        Eigen::Matrix2d covariance;
        
        std::chrono::system_clock::time_point timestamp;
    };

    struct FilterConfig {
        // Process noise covariance
        double orientation_process_noise;
        double tpoint_process_noise;
        double rate_process_noise;
        double env_process_noise;
        
        // Measurement noise covariance
        double position_measurement_noise;
        double environmental_measurement_noise;
        
        // Initial covariance
        double initial_orientation_uncertainty;
        double initial_tpoint_uncertainty;
        double initial_rate_uncertainty;
        
        // Filter parameters
        bool use_adaptive_q;
        bool use_adaptive_r;
        double innovation_threshold;
        int max_iterations;
        
        // State dimensions
        int tpoint_param_count;
    };

    KalmanFilter(const FilterConfig& config);
    ~KalmanFilter();

    /**
     * @brief Initialize filter with initial state
     * @param initialState Initial state vector
     * @param initialCovariance Initial covariance matrix
     * @param timestamp Initial timestamp
     */
    void initialize(const Eigen::VectorXd& initialState,
                    const Eigen::MatrixXd& initialCovariance,
                    double timestamp);

    /**
     * @brief Predict state forward in time
     * @param dt Time step in seconds
     */
    void predict(double dt);

    /**
     * @brief Update state with measurement
     * @param measurement Measurement vector
     * @param measurementMatrix Measurement matrix H
     * @param measurementNoise Measurement noise covariance R
     */
    void update(const Eigen::VectorXd& measurement,
                const Eigen::MatrixXd& measurementMatrix,
                const Eigen::MatrixXd& measurementNoise);

    /**
     * @brief Update state with measurement (simple version)
     * @param measurement Measurement vector
     */
    void update(const Eigen::VectorXd& measurement);

    /**
     * @brief Get current state estimate
     * @return Current state
     */
    State getState() const;

    /**
     * @brief Get state covariance matrix
     * @return Covariance matrix
     */
    Eigen::MatrixXd getCovariance() const;

    /**
     * @brief Get innovation (measurement residual)
     * @return Innovation vector
     */
    Eigen::VectorXd getInnovation() const;

    /**
     * @brief Get innovation covariance
     * @return Innovation covariance matrix
     */
    Eigen::MatrixXd getInnovationCovariance() const;

    /**
     * @brief Get filter status
     * @return Status string
     */
    std::string getStatus() const;

    /**
     * @brief Check if filter is initialized
     * @return True if initialized
     */
    bool isInitialized() const;

    /**
     * @brief Reset filter to uninitialized state
     */
    void reset();

    /**
     * @brief Set process noise covariance
     * @param Q Process noise covariance matrix
     */
    void setProcessNoise(const Eigen::MatrixXd& Q);

    /**
     * @brief Set measurement noise covariance
     * @param R Measurement noise covariance matrix
     */
    void setMeasurementNoise(const Eigen::MatrixXd& R);

    /**
     * @brief Get filter performance metrics
     * @return Map of performance metrics
     */
    std::map<std::string, double> getPerformanceMetrics() const;

    /**
     * @brief Save filter state to file
     * @param filename File to save to
     * @return True if save successful
     */
    bool saveState(const std::string& filename) const;

    /**
     * @brief Load filter state from file
     * @param filename File to load from
     * @return True if load successful
     */
    bool loadState(const std::string& filename);

    /**
     * @brief Set adaptive filtering parameters
     * @param enable Enable adaptive filtering
     * @param window_size Window size for adaptation
     * @param threshold Innovation threshold for adaptation
     */
    void setAdaptiveFiltering(bool enable, int window_size = 10, double threshold = 2.0);

    /**
     * @brief Get filter consistency (NEES test)
     * @return Normalized estimation error squared
     */
    double getConsistencyTest() const;

private:
    class Impl;
    std::unique_ptr<Impl> pimpl;
};

} // namespace models
} // namespace astro_mount

#endif // KALMAN_FILTER_H