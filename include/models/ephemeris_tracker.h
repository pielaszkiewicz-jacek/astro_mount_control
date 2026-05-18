#ifndef EPHEMERIS_TRACKER_H
#define EPHEMERIS_TRACKER_H

#include "proto/mount_controller.grpc.pb.h"
#include "core/astronomical_calculations.h"
#include "models/tpoint_model.h"
#include <cstdint>
#include <memory>
#include <vector>
#include <string>
#include <map>
#include <chrono>
#include <mutex>
#include <atomic>
#include <thread>
#include <functional>

namespace astro_mount {
namespace models {

/**
 * @brief Interpolator for ephemeris data
 */
class EphemerisInterpolator {
public:
    /**
     * @brief Construct a new EphemerisInterpolator object
     * @param points Ephemeris points sorted by time
     * @param interpolation_order Order of interpolation (1=linear, 2=quadratic, 3=cubic)
     */
    EphemerisInterpolator(const std::vector<::astro_mount::EphemerisPoint>& points,
                         int interpolation_order = 3);
    
    /**
     * @brief Check if interpolator is valid
     * @return True if interpolator can be used
     */
    bool isValid() const { return valid_; }
    
    /**
     * @brief Get interpolated position at specified time
     * @param timestamp Time to interpolate at
     * @return Interpolated coordinates (ra in hours, dec in degrees, rates in hours/hour, degrees/hour)
     */
    std::tuple<double, double, double, double> getPositionAtTime(
        const std::chrono::system_clock::time_point& timestamp) const;
    
    /**
     * @brief Get valid time range
     * @return Pair of start and end times
     */
    std::pair<std::chrono::system_clock::time_point,
              std::chrono::system_clock::time_point> getTimeRange() const;
    
    /**
     * @brief Predict position beyond ephemeris range
     * @param timestamp Time to predict for
     * @param extrapolation_seconds How far to extrapolate (max)
     * @return Predicted coordinates or empty if extrapolation not possible
     */
    std::optional<std::tuple<double, double, double, double>> predictPosition(
        const std::chrono::system_clock::time_point& timestamp,
        double extrapolation_seconds) const;
    
    /**
     * @brief Get number of ephemeris points
     * @return Number of points
     */
    size_t getPointCount() const { return points_.size(); }
    
private:
    std::vector<::astro_mount::EphemerisPoint> points_;
    int interpolation_order_;
    bool valid_{false};
    
    // Time range
    std::chrono::system_clock::time_point start_time_;
    std::chrono::system_clock::time_point end_time_;
    
    // ================================================================
    // NUMERICAL STABILITY: Precomputed data (computed once in ctor)
    // ================================================================
    // Caching time/value vectors avoids repeated protobuf→double
    // conversions that accumulate floating-point error. Barycentric
    // weights w_j = 1/∏_{k≠j}(t_j - t_k) enable stable O(n) evaluation
    // via the barycentric Lagrange formula (numerically superior to
    // computing each Lagrange basis polynomial separately).
    // ================================================================
    std::vector<double> precomputed_times_;
    std::vector<double> precomputed_ra_;
    std::vector<double> precomputed_dec_;
    std::vector<double> precomputed_ra_rate_;
    std::vector<double> precomputed_dec_rate_;
    std::vector<double> barycentric_weights_ra_;
    std::vector<double> barycentric_weights_dec_;
    std::vector<double> barycentric_weights_ra_rate_;
    std::vector<double> barycentric_weights_dec_rate_;
    
    // Helper: compute barycentric weights for a set of points
    static std::vector<double> computeBarycentricWeights(
        const std::vector<double>& times);
    
    // Helper: evaluate barycentric Lagrange interpolation at time t
    double interpolateBarycentric(double t,
                                 const std::vector<double>& values,
                                 const std::vector<double>& weights) const;
    
    // Helper methods for interpolation (fallback chain)
    double interpolateLinear(double t, const std::vector<double>& values,
                           const std::vector<double>& times) const;
    double interpolateQuadratic(double t, const std::vector<double>& values,
                              const std::vector<double>& times) const;
    double interpolateCubic(double t, const std::vector<double>& values,
                          const std::vector<double>& times) const;
};

/**
 * @brief Ephemeris model for moving objects
 */
class EphemerisModel {
public:
    /**
     * @brief Ephemeris model configuration
     */
    struct Config {
        double earth_rotation_rate = 15.0410671786692;  // arcseconds/sidereal second
        double max_extrapolation_seconds = 3600.0;      // Max extrapolation time
        double interpolation_update_rate_hz = 10.0;     // Interpolation update rate
        bool apply_refraction = true;                  // Apply atmospheric refraction
        bool apply_aberration = true;                  // Apply aberration
        bool apply_nutation = true;                    // Apply nutation
        bool apply_precession = true;                  // Apply precession
        
        // Error thresholds
        double max_position_error_arcsec = 5.0;        // Max position error
        double max_rate_error_arcsec_per_sec = 0.1;    // Max rate error
        double prediction_confidence_threshold = 0.8;  // Min confidence for predictions
        
        // TPOINT integration
        std::shared_ptr<TPointModel> tpoint_model;     // Optional TPOINT model for mount corrections
        bool apply_tpoint_corrections = true;          // Apply TPOINT corrections if model available
    };
    
    /**
     * @brief Construct a new EphemerisModel object
     * @param ephemeris_data Ephemeris data
     * @param config Model configuration
     */
    explicit EphemerisModel(const ::astro_mount::EphemerisData& ephemeris_data,
                           const Config& config);
    
    /**
     * @brief Destroy the EphemerisModel object
     */
    ~EphemerisModel();
    
    // Delete copy and move constructors
    EphemerisModel(const EphemerisModel&) = delete;
    EphemerisModel& operator=(const EphemerisModel&) = delete;
    EphemerisModel(EphemerisModel&&) = delete;
    EphemerisModel& operator=(EphemerisModel&&) = delete;
    
    /**
     * @brief Get object identifier
     * @return Object identifier
     */
    std::string getObjectId() const;
    
    /**
     * @brief Get object name
     * @return Object name
     */
    std::string getObjectName() const;
    
    /**
     * @brief Get object type
     * @return Object type
     */
    std::string getObjectType() const;
    
    /**
     * @brief Get interpolated position with Earth rotation correction
     * @param timestamp Time for position calculation
     * @param observer_latitude Observer latitude in degrees
     * @param observer_longitude Observer longitude in degrees
     * @param observer_altitude Observer altitude in meters
     * @return Apparent coordinates (ra, dec, ra_rate, dec_rate) with Earth rotation
     */
    std::tuple<double, double, double, double> getApparentPosition(
        const std::chrono::system_clock::time_point& timestamp,
        double observer_latitude,
        double observer_longitude,
        double observer_altitude) const;
    
    /**
     * @brief Check if time is within ephemeris range
     * @param timestamp Time to check
     * @return True if within range
     */
    bool isTimeWithinRange(const std::chrono::system_clock::time_point& timestamp) const;
    
    /**
     * @brief Get ephemeris time range
     * @return Time range (start, end)
     */
    std::pair<std::chrono::system_clock::time_point,
              std::chrono::system_clock::time_point> getTimeRange() const;
    
    /**
     * @brief Get model confidence at specified time
     * @param timestamp Time for confidence calculation
     * @return Confidence value (0-1)
     */
    double getConfidence(const std::chrono::system_clock::time_point& timestamp) const;
    
    /**
     * @brief Update model configuration
     * @param config New configuration
     */
    void updateConfig(const Config& config);
    
    /**
     * @brief Get current configuration
     * @return Current configuration
     */
    Config getConfig() const;
    
private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

/**
 * @brief Ephemeris tracker for continuous tracking of moving objects
 */
class EphemerisTracker {
public:
    /**
     * @brief Tracking configuration
     */
    struct TrackingConfig {
        double lead_time_seconds;           // Time to start before first point
        double slew_margin_seconds;         // Margin for slewing
        bool wait_at_start;                 // Wait at start position
        bool enable_prediction;             // Enable prediction beyond ephemeris
        double prediction_interval_hours;   // Prediction interval
        std::string tracking_mode;          // continuous, sidereal_rate, custom_rate
        double custom_rate_ra;              // Custom RA rate (hours/hour)
        double custom_rate_dec;             // Custom Dec rate (degrees/hour)
        double update_rate_hz;              // Tracking update rate
        double max_position_error_arcsec;   // Max allowed position error
        int max_recovery_attempts;          // Max recovery attempts
        std::string error_source;           // "simulated" (domyślnie) lub "encoder" – źródło błędu pozycji
        
        TrackingConfig() :
            lead_time_seconds(30.0),
            slew_margin_seconds(10.0),
            wait_at_start(true),
            enable_prediction(false),
            prediction_interval_hours(1.0),
            tracking_mode("continuous"),
            custom_rate_ra(0.0),
            custom_rate_dec(0.0),
            update_rate_hz(10.0),
            max_position_error_arcsec(10.0),
            max_recovery_attempts(3),
            error_source("simulated") {}
    };
    
    /**
     * @brief Tracking statistics
     */
    struct TrackingStats {
        std::chrono::system_clock::time_point start_time;
        std::chrono::system_clock::time_point end_time;
        double total_track_time_seconds = 0.0;
        double avg_position_error_arcsec = 0.0;
        double max_position_error_arcsec = 0.0;
        double avg_rate_error = 0.0;
        uint32_t prediction_count = 0;
        uint32_t recovery_attempts = 0;
        uint32_t tracking_updates = 0;
        uint32_t errors = 0;
        bool earth_rotation_applied = false;
    };
    
    /**
     * @brief Callback for tracking events
     */
    using TrackingCallback = std::function<void(
        const std::string& event_type,
        const std::string& message,
        const std::map<std::string, std::string>& context)>;
    
    /**
     * @brief Construct a new EphemerisTracker object
     * @param model Ephemeris model for tracking
     * @param observer_latitude Observer latitude in degrees
     * @param observer_longitude Observer longitude in degrees
     * @param observer_altitude Observer altitude in meters
     */
    EphemerisTracker(std::shared_ptr<EphemerisModel> model,
                    double observer_latitude,
                    double observer_longitude,
                    double observer_altitude);
    
    /**
     * @brief Destroy the EphemerisTracker object
     */
    ~EphemerisTracker();
    
    // Delete copy and move constructors
    EphemerisTracker(const EphemerisTracker&) = delete;
    EphemerisTracker& operator=(const EphemerisTracker&) = delete;
    EphemerisTracker(EphemerisTracker&&) = delete;
    EphemerisTracker& operator=(EphemerisTracker&&) = delete;
    
    /**
     * @brief Start tracking
     * @param start_time Time to start tracking
     * @param config Tracking configuration
     * @return True if tracking started successfully
     */
    bool startTracking(const std::chrono::system_clock::time_point& start_time,
                      const TrackingConfig& config = TrackingConfig());
    
    /**
     * @brief Stop tracking
     */
    void stopTracking();
    
    /**
     * @brief Check if tracking is active
     * @return True if tracking is active
     */
    bool isTracking() const;
    
    /**
     * @brief Get current tracking status
     * @return Tracking status with current position and error
     */
    ::astro_mount::EphemerisTrackStatus getStatus() const;
    
    /**
     * @brief Get tracking statistics
     * @return Tracking statistics
     */
    TrackingStats getStatistics() const;
    
    /**
     * @brief Get current target position
     * @return Current target coordinates (ra, dec, ra_rate, dec_rate)
     */
    std::tuple<double, double, double, double> getCurrentTarget() const;
    
    /**
     * @brief Get start position for tracking
     * @param start_time Desired start time
     * @param lead_time_seconds Lead time before start
     * @return Start position coordinates
     */
    std::tuple<double, double, double, double> getStartPosition(
        const std::chrono::system_clock::time_point& start_time,
        double lead_time_seconds) const;
    
    /**
     * @brief Update tracking configuration
     * @param config New configuration
     * @return True if configuration updated successfully
     */
    bool updateConfig(const TrackingConfig& config);
    
    /**
     * @brief Callback for encoder position reading
     * @return Tuple (ra_hours, dec_degrees) – actual mount position from encoders
     */
    using EncoderReaderCallback = std::function<std::tuple<double, double>()>;
    
    /**
     * @brief Register encoder reader callback for real position feedback
     * @param callback Funkcja zwracająca (ra, dec) z enkoderów
     * 
     * Gdy config.error_source = "encoder", błąd pozycji liczony jest jako
     * różnica między targetem efemeryd a rzeczywistym odczytem enkodera.
     * Gdy config.error_source = "simulated" (domyślnie), używany jest rand().
     */
    void registerEncoderReader(EncoderReaderCallback callback);
    
    /**
     * @brief Register tracking callback
     * @param callback Callback function
     */
    void registerCallback(TrackingCallback callback);
    
    /**
     * @brief Get object identifier
     * @return Object identifier
     */
    std::string getObjectId() const;
    
    /**
     * @brief Get object name
     * @return Object name
     */
    std::string getObjectName() const;
    
    /**
     * @brief Get remaining track time
     * @return Remaining time in seconds
     */
    double getRemainingTime() const;
    
    /**
     * @brief Perform recovery if tracking error exceeds threshold
     * @return True if recovery successful
     */
    bool performRecovery();
    
private:
    class Impl;
    std::unique_ptr<Impl> impl_;
    
    // Tracking thread
    void trackingLoop();
    void updateTracking();
    void handleTrackingError(const std::string& error_message);
    
    // State
    std::shared_ptr<EphemerisModel> model_;
    double observer_latitude_;
    double observer_longitude_;
    double observer_altitude_;
    
    TrackingConfig config_;
    TrackingStats stats_;
    
    std::atomic<bool> tracking_active_{false};
    std::atomic<bool> stop_requested_{false};
    std::chrono::system_clock::time_point tracking_start_time_;
    std::chrono::system_clock::time_point tracking_end_time_;
    
    // Current tracking state
    mutable std::mutex state_mutex_;
    std::tuple<double, double, double, double> current_target_;
    double current_position_error_arcsec_{0.0};
    std::string last_error_message_;
    std::vector<std::string> warnings_;
    std::map<std::string, std::string> tracking_parameters_;
    
    // Callbacks
    std::vector<TrackingCallback> callbacks_;
    mutable std::mutex callback_mutex_;
    
    // Thread
    std::thread tracking_thread_;
    
    // Helper methods
    void notifyCallbacks(const std::string& event_type,
                        const std::string& message,
                        const std::map<std::string, std::string>& context = {});
    void updateStatistics(double position_error, double rate_error);
};

/**
 * @brief Ephemeris tracker manager for multiple objects
 */
class EphemerisTrackerManager {
public:
    /**
     * @brief Construct a new EphemerisTrackerManager object
     */
    EphemerisTrackerManager();
    
    /**
     * @brief Destroy the EphemerisTrackerManager object
     */
    ~EphemerisTrackerManager();
    
    // Delete copy and move constructors
    EphemerisTrackerManager(const EphemerisTrackerManager&) = delete;
    EphemerisTrackerManager& operator=(const EphemerisTrackerManager&) = delete;
    EphemerisTrackerManager(EphemerisTrackerManager&&) = delete;
    EphemerisTrackerManager& operator=(EphemerisTrackerManager&&) = delete;
    
    /**
     * @brief Upload ephemeris data
     * @param ephemeris_data Ephemeris data
     * @return True if upload successful
     */
    bool uploadEphemeris(const ::astro_mount::EphemerisData& ephemeris_data);
    
    /**
     * @brief Start tracking for specified object
     * @param object_id Object identifier
     * @param start_time Start time for tracking
     * @param config Tracking configuration
     * @return Tracker ID or empty string if failed
     */
    std::string startTracking(const std::string& object_id,
                             const std::chrono::system_clock::time_point& start_time,
                             const EphemerisTracker::TrackingConfig& config = EphemerisTracker::TrackingConfig());
    
    /**
     * @brief Start tracking with ephemeris data
     * @param ephemeris_data Ephemeris data
     * @param start_time Start time for tracking
     * @param config Tracking configuration
     * @return Tracker ID
     */
    std::string startTrackingWithData(const ::astro_mount::EphemerisData& ephemeris_data,
                                     const std::chrono::system_clock::time_point& start_time,
                                     const EphemerisTracker::TrackingConfig& config = EphemerisTracker::TrackingConfig());
    
    /**
     * @brief Stop tracking for tracker
     * @param tracker_id Tracker identifier
     * @return True if stopped successfully
     */
    bool stopTracking(const std::string& tracker_id);
    
    /**
     * @brief Get tracker status
     * @param tracker_id Tracker identifier
     * @return Tracker status or nullptr if not found
     */
    std::shared_ptr<EphemerisTracker> getTracker(const std::string& tracker_id) const;
    
    /**
     * @brief Get all active trackers
     * @return Map of tracker IDs to trackers
     */
    std::map<std::string, std::shared_ptr<EphemerisTracker>> getActiveTrackers() const;
    
    /**
     * @brief Get tracking status for all trackers
     * @return Vector of tracker statuses
     */
    std::vector<::astro_mount::EphemerisTrackStatus> getAllStatuses() const;
    
    /**
     * @brief Clear ephemeris cache
     */
    void clearCache();
    
    /**
     * @brief Get ephemeris metrics
     * @return Ephemeris metrics
     */
    ::astro_mount::EphemerisMetrics getMetrics() const;
    
    /**
     * @brief Set observer location
     * @param latitude Observer latitude in degrees
     * @param longitude Observer longitude in degrees
     * @param altitude Observer altitude in meters
     */
    void setObserverLocation(double latitude, double longitude, double altitude);
    
private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace models
} // namespace astro_mount

#endif // EPHEMERIS_TRACKER_H