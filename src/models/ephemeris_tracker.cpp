#include "models/ephemeris_tracker.h"
#include "core/astronomical_calculations.h"
#include "logging/logger.h"
#include <sofa.h>
#include <sofam.h>
#include <chrono>
#include <thread>
#include <sstream>
#include <iomanip>
#include <cmath>
#include <algorithm>
#include <stdexcept>
#include <random>

namespace astro_mount {
namespace models {

using namespace std::chrono;

// ============================================================================
// EphemerisInterpolator implementation
// ============================================================================

EphemerisInterpolator::EphemerisInterpolator(
    const std::vector<::astro_mount::EphemerisPoint>& points,
    int interpolation_order)
    : points_(points), interpolation_order_(interpolation_order) {
    
    if (points_.empty()) {
        API_LOG_ERROR("EphemerisInterpolator: No points provided");
        valid_ = false;
        return;
    }
    
    // Sort points by time
    std::sort(points_.begin(), points_.end(),
        [](const ::astro_mount::EphemerisPoint& a, const ::astro_mount::EphemerisPoint& b) {
            return a.timestamp().seconds() < b.timestamp().seconds() ||
                  (a.timestamp().seconds() == b.timestamp().seconds() &&
                   a.timestamp().nanos() < b.timestamp().nanos());
        });
    
    // Check for duplicates and sufficient points
    for (size_t i = 1; i < points_.size(); ++i) {
        const auto& prev = points_[i-1];
        const auto& curr = points_[i];
        if (prev.timestamp().seconds() == curr.timestamp().seconds() &&
            prev.timestamp().nanos() == curr.timestamp().nanos()) {
            API_LOG_WARN("EphemerisInterpolator: Duplicate timestamp found");
        }
    }
    
    if (points_.size() < 2) {
        API_LOG_ERROR("EphemerisInterpolator: At least 2 points required");
        valid_ = false;
        return;
    }
    
    // Set time range
    const auto& first = points_.front();
    const auto& last = points_.back();
    start_time_ = system_clock::from_time_t(first.timestamp().seconds()) +
                 nanoseconds(first.timestamp().nanos());
    end_time_ = system_clock::from_time_t(last.timestamp().seconds()) +
               nanoseconds(last.timestamp().nanos());
    
    // ================================================================
    // NUMERICAL STABILITY: Precompute time/value vectors
    // ================================================================
    // Converting protobuf timestamps (seconds+nanos) to double on every
    // interpolation call accumulates floating-point error and wastes CPU.
    // Precompute once in the constructor and reuse for all queries.
    // Also precompute barycentric weights for stable O(n) evaluation.
    // ================================================================
    precomputed_times_.reserve(points_.size());
    precomputed_ra_.reserve(points_.size());
    precomputed_dec_.reserve(points_.size());
    precomputed_ra_rate_.reserve(points_.size());
    precomputed_dec_rate_.reserve(points_.size());
    
    for (const auto& point : points_) {
        auto point_time = system_clock::from_time_t(point.timestamp().seconds()) +
                         nanoseconds(point.timestamp().nanos());
        auto point_duration = point_time.time_since_epoch();
        double point_t = duration_cast<std::chrono::duration<double>>(point_duration).count();
        
        precomputed_times_.push_back(point_t);
        precomputed_ra_.push_back(point.ra());
        precomputed_dec_.push_back(point.dec());
        precomputed_ra_rate_.push_back(point.ra_rate());
        precomputed_dec_rate_.push_back(point.dec_rate());
    }
    
    // Precompute barycentric weights for all points.
    // Barycentric weights depend only on node positions (times), not on values,
    // so all components (ra, dec, ra_rate, dec_rate) share the same weights.
    auto bw = computeBarycentricWeights(precomputed_times_);
    barycentric_weights_ra_ = bw;
    barycentric_weights_dec_ = bw;
    barycentric_weights_ra_rate_ = bw;
    barycentric_weights_dec_rate_ = bw;
    
    valid_ = true;
    API_LOG_DEBUG("EphemerisInterpolator: Created with {} points, order {}, "
                 "time range {} to {}",
                 points_.size(), interpolation_order_,
                 system_clock::to_time_t(start_time_),
                 system_clock::to_time_t(end_time_));
}

std::tuple<double, double, double, double>
EphemerisInterpolator::getPositionAtTime(
    const system_clock::time_point& timestamp) const {
    
    if (!valid_) {
        throw std::runtime_error("EphemerisInterpolator not valid");
    }
    
    // Convert timestamp to seconds since epoch
    auto duration = timestamp.time_since_epoch();
    double t = duration_cast<std::chrono::duration<double>>(duration).count();
    
    // ================================================================
    // NUMERICAL STABILITY: Use precomputed vectors (no protobuf→double
    // conversion on every call, reducing floating-point accumulation).
    // ================================================================
    
    // ================================================================
    // NUMERICAL STABILITY: Extrapolation guard
    // ================================================================
    // Barycentric/Lagrange polynomials diverge rapidly outside the data
    // range. Clamp the query time to the valid ephemeris range to
    // prevent catastrophic extrapolation errors (NaN, Inf, wild values).
    // ================================================================
    double t_min = precomputed_times_.front();
    double t_max = precomputed_times_.back();
    
    if (t < t_min || t > t_max) {
        API_LOG_WARN("EphemerisInterpolator: Time {} is outside ephemeris range [{}, {}]. "
                     "Clamping to nearest boundary to prevent divergent extrapolation.",
                     t, t_min, t_max);
        t = std::clamp(t, t_min, t_max);
    }
    
    // Interpolate each component using barycentric Lagrange formula
    // which is numerically superior to computing each basis polynomial
    // separately (fewer intermediate products, natural normalization).
    double ra, dec, ra_rate, dec_rate;
    
    switch (interpolation_order_) {
        case 1: // Linear
            ra = interpolateLinear(t, precomputed_ra_, precomputed_times_);
            dec = interpolateLinear(t, precomputed_dec_, precomputed_times_);
            ra_rate = interpolateLinear(t, precomputed_ra_rate_, precomputed_times_);
            dec_rate = interpolateLinear(t, precomputed_dec_rate_, precomputed_times_);
            break;
            
        case 2: // Quadratic via barycentric (more stable than Lagrange basis)
            ra = interpolateBarycentric(t, precomputed_ra_, barycentric_weights_ra_);
            dec = interpolateBarycentric(t, precomputed_dec_, barycentric_weights_dec_);
            ra_rate = interpolateBarycentric(t, precomputed_ra_rate_, barycentric_weights_ra_rate_);
            dec_rate = interpolateBarycentric(t, precomputed_dec_rate_, barycentric_weights_dec_rate_);
            break;
            
        case 3: // Cubic via barycentric (default, more stable than Lagrange basis)
        default:
            ra = interpolateBarycentric(t, precomputed_ra_, barycentric_weights_ra_);
            dec = interpolateBarycentric(t, precomputed_dec_, barycentric_weights_dec_);
            ra_rate = interpolateBarycentric(t, precomputed_ra_rate_, barycentric_weights_ra_rate_);
            dec_rate = interpolateBarycentric(t, precomputed_dec_rate_, barycentric_weights_dec_rate_);
            break;
    }
    
    return std::make_tuple(ra, dec, ra_rate, dec_rate);
}

std::pair<system_clock::time_point, system_clock::time_point>
EphemerisInterpolator::getTimeRange() const {
    return {start_time_, end_time_};
}

std::optional<std::tuple<double, double, double, double>>
EphemerisInterpolator::predictPosition(
    const system_clock::time_point& timestamp,
    double extrapolation_seconds) const {
    
    if (!valid_) {
        return std::nullopt;
    }
    
    auto duration = timestamp.time_since_epoch();
    double t = duration_cast<std::chrono::duration<double>>(duration).count();
    
    // Check if we can extrapolate
    auto start_duration = start_time_.time_since_epoch();
    double start_t = duration_cast<std::chrono::duration<double>>(start_duration).count();
    auto end_duration = end_time_.time_since_epoch();
    double end_t = duration_cast<std::chrono::duration<double>>(end_duration).count();
    
    // Don't extrapolate too far
    if (t < start_t - extrapolation_seconds ||
        t > end_t + extrapolation_seconds) {
        return std::nullopt;
    }
    
    // For prediction, use last known rate and (if available) acceleration
    const size_t n = precomputed_times_.size();
    if (n < 2) {
        return std::nullopt;
    }
    
    // Use precomputed vectors (avoids repeated protobuf→double conversion)
    double last_t = precomputed_times_.back();
    
    double dt = t - last_t;           // seconds since last known point
    double dt_hours = dt / 3600.0;    // same in hours
    
    // ================================================================
    // NUMERICAL STABILITY: Acceleration-aware extrapolation
    // ================================================================
    // Simple linear extrapolation using only the last rate can diverge
    // quickly for accelerating objects. When 3+ points are available,
    // compute acceleration from the last two rate differences and use
    // quadratic extrapolation: x(t) = x0 + v0*dt + 0.5*a*dt²
    // ================================================================
    
    double ra, dec, ra_rate, dec_rate;
    
    if (n >= 3) {
        double t2_t = precomputed_times_[n - 2];
        double t3_t = precomputed_times_[n - 3];
        
        // Time intervals between the last 3 points (in hours)
        double dt_32 = (last_t - t2_t) / 3600.0;
        double dt_21 = (t2_t - t3_t) / 3600.0;
        
        // Rate of change of rate (acceleration) in (hours/hour)/hour
        // Guard against division by zero if times are identical
        const double min_dt = 1e-9; // ~3.6 microseconds
        double ra_accel = 0.0, dec_accel = 0.0;
        
        if (std::abs(dt_32) > min_dt && std::abs(dt_21) > min_dt) {
            double ra_rate_last = precomputed_ra_rate_.back();
            double ra_rate_p3 = precomputed_ra_rate_[n - 3];
            double dec_rate_last = precomputed_dec_rate_.back();
            double dec_rate_p3 = precomputed_dec_rate_[n - 3];
            
            // Acceleration: change in rate divided by time between midpoints
            // Use central difference for better numerical behavior
            double dt_mid = (dt_32 + dt_21) * 0.5;
            if (std::abs(dt_mid) > min_dt) {
                ra_accel = (ra_rate_last - ra_rate_p3) / (2.0 * dt_mid);
                dec_accel = (dec_rate_last - dec_rate_p3) / (2.0 * dt_mid);
            }
        }
        
        // Quadratic extrapolation with acceleration
        // position = x0 + v0*dt_hours + 0.5*a*dt_hours²
        ra = precomputed_ra_.back() + precomputed_ra_rate_.back() * dt_hours +
             0.5 * ra_accel * dt_hours * dt_hours;
        dec = precomputed_dec_.back() + precomputed_dec_rate_.back() * dt_hours +
              0.5 * dec_accel * dt_hours * dt_hours;
        
        // Rate at predicted time: v = v0 + a*dt_hours
        ra_rate = precomputed_ra_rate_.back() + ra_accel * dt_hours;
        dec_rate = precomputed_dec_rate_.back() + dec_accel * dt_hours;
        
        // Clamp rates to physically plausible bounds
        // Max rate for any solar system object is tiny compared to Earth rotation
        ra_rate = std::clamp(ra_rate, -100.0, 100.0);
        dec_rate = std::clamp(dec_rate, -100.0, 100.0);
        
    } else {
        // Fallback: simple linear prediction using last rate only (2 points)
        ra = precomputed_ra_.back() + precomputed_ra_rate_.back() * dt_hours;
        dec = precomputed_dec_.back() + precomputed_dec_rate_.back() * dt_hours;
        ra_rate = precomputed_ra_rate_.back();
        dec_rate = precomputed_dec_rate_.back();
    }
    
    return std::make_tuple(ra, dec, ra_rate, dec_rate);
}

// ================================================================
// NUMERICAL STABILITY: Barycentric Lagrange interpolation
// ================================================================
// The barycentric form of Lagrange interpolation is numerically
// superior to computing each basis polynomial separately:
//
//   p(t) = (∑ w_j·y_j/(t-t_j)) / (∑ w_j/(t-t_j))
//
// where w_j = 1/∏_{k≠j}(t_j-t_k). Benefits:
//   - Precomputed weights → O(n) evaluation (vs O(n²) for standard Lagrange)
//   - Weights depend only on node positions, not values
//   - Cancellation errors affect numerator & denominator similarly
//   - Avoids computing large intermediate products
// ================================================================

std::vector<double> EphemerisInterpolator::computeBarycentricWeights(
    const std::vector<double>& times) {
    
    const size_t n = times.size();
    std::vector<double> weights(n, 1.0);
    
    if (n == 0) return weights;
    
    // Compute product w_j = ∏_{k≠j} (t_j - t_k)
    // Then invert to get barycentric weights
    for (size_t j = 0; j < n; ++j) {
        for (size_t k = 0; k < n; ++k) {
            if (k == j) continue;
            double diff = times[j] - times[k];
            // Guard against duplicate timestamps
            if (std::abs(diff) < 1e-30) {
                weights[j] = 0.0;
                break;
            }
            weights[j] *= diff;
        }
    }
    
    // Invert: w_j = 1 / ∏_{k≠j} (t_j - t_k)
    for (size_t j = 0; j < n; ++j) {
        if (std::abs(weights[j]) < 1e-300) {
            weights[j] = 0.0; // effectively ignore this node
        } else {
            weights[j] = 1.0 / weights[j];
        }
    }
    
    return weights;
}

double EphemerisInterpolator::interpolateBarycentric(
    double t, const std::vector<double>& values,
    const std::vector<double>& weights) const {
    
    const size_t n = values.size();
    if (n != weights.size() || n < 2) {
        throw std::runtime_error("Invalid data for barycentric interpolation");
    }
    
    // ================================================================
    // Barycentric Lagrange formula:
    //   p(t) = (∑ w_j·y_j/(t-t_j)) / (∑ w_j/(t-t_j))
    //
    // The precomputed_times_ member contains the node positions that
    // correspond to the weights. We iterate over all n nodes, summing
    // numerator and denominator contributions.
    //
    // If t exactly matches a node (within epsilon), return the
    // corresponding value directly to avoid division by zero.
    // ================================================================
    const double eps = 1e-15;
    
    // Check if t exactly matches a node time
    for (size_t j = 0; j < n; ++j) {
        if (std::abs(t - precomputed_times_[j]) < eps) {
            return values[j];
        }
    }
    
    double numerator = 0.0;
    double denominator = 0.0;
    
    for (size_t j = 0; j < n; ++j) {
        double diff = t - precomputed_times_[j];
        if (std::abs(weights[j]) < 1e-300) {
            // Near-duplicate node, skip it
            continue;
        }
        double term = weights[j] / diff;
        numerator += term * values[j];
        denominator += term;
    }
    
    // Guard against catastrophic cancellation
    if (std::abs(denominator) < 1e-300) {
        API_LOG_WARN("EphemerisInterpolator: Near-zero denominator in "
                     "barycentric interpolation, falling back to linear");
        return interpolateLinear(t, values, precomputed_times_);
    }
    
    return numerator / denominator;
}

double EphemerisInterpolator::interpolateLinear(
    double t, const std::vector<double>& values,
    const std::vector<double>& times) const {
    
    if (values.size() != times.size() || values.size() < 2) {
        throw std::runtime_error("Invalid data for linear interpolation");
    }
    
    // Find bounding indices
    size_t idx = 0;
    while (idx < times.size() - 1 && times[idx + 1] < t) {
        ++idx;
    }
    
    if (idx >= times.size() - 1) {
        idx = times.size() - 2;
    }
    
    double t0 = times[idx];
    double t1 = times[idx + 1];
    double v0 = values[idx];
    double v1 = values[idx + 1];
    
    if (t1 == t0) {
        return v0;
    }
    
    double alpha = (t - t0) / (t1 - t0);
    return v0 + alpha * (v1 - v0);
}

double EphemerisInterpolator::interpolateQuadratic(
    double t, const std::vector<double>& values,
    const std::vector<double>& times) const {
    
    if (values.size() != times.size() || values.size() < 3) {
        // Fall back to linear if not enough points
        return interpolateLinear(t, values, times);
    }
    
    // Find three points for quadratic interpolation
    size_t idx = 0;
    while (idx < times.size() - 1 && times[idx + 1] < t) {
        ++idx;
    }
    
    if (idx == 0) idx = 0;
    else if (idx >= times.size() - 2) idx = times.size() - 3;
    else idx = idx - 1;
    
    double t0 = times[idx];
    double t1 = times[idx + 1];
    double t2 = times[idx + 2];
    double v0 = values[idx];
    double v1 = values[idx + 1];
    double v2 = values[idx + 2];
    
    // ================================================================
    // NUMERICAL STABILITY: Denominator protection for Lagrange basis
    // ================================================================
    // Check for near-zero denominators which would produce NaN/Inf.
    // This can happen with duplicate timestamps or extremely close
    // data points (sub-millisecond spacing).
    // ================================================================
    const double eps = 1e-12;
    double dL0 = (t0 - t1) * (t0 - t2);
    double dL1 = (t1 - t0) * (t1 - t2);
    double dL2 = (t2 - t0) * (t2 - t1);
    
    if (std::abs(dL0) < eps || std::abs(dL1) < eps || std::abs(dL2) < eps) {
        API_LOG_WARN("EphemerisInterpolator: Near-zero denominator in quadratic "
                     "interpolation (denom=[{:.6e},{:.6e},{:.6e}]), falling back to linear",
                     dL0, dL1, dL2);
        return interpolateLinear(t, values, times);
    }
    
    // Quadratic interpolation using Lagrange polynomials
    double L0 = ((t - t1) * (t - t2)) / dL0;
    double L1 = ((t - t0) * (t - t2)) / dL1;
    double L2 = ((t - t0) * (t - t1)) / dL2;
    
    return L0 * v0 + L1 * v1 + L2 * v2;
}

double EphemerisInterpolator::interpolateCubic(
    double t, const std::vector<double>& values,
    const std::vector<double>& times) const {
    
    if (values.size() != times.size() || values.size() < 4) {
        // Fall back to quadratic if not enough points
        return interpolateQuadratic(t, values, times);
    }
    
    // Find four points for cubic interpolation
    size_t idx = 0;
    while (idx < times.size() - 1 && times[idx + 1] < t) {
        ++idx;
    }
    
    if (idx == 0) idx = 0;
    else if (idx == 1) idx = 0;
    else if (idx >= times.size() - 3) idx = times.size() - 4;
    else idx = idx - 2;
    
    double t0 = times[idx];
    double t1 = times[idx + 1];
    double t2 = times[idx + 2];
    double t3 = times[idx + 3];
    double v0 = values[idx];
    double v1 = values[idx + 1];
    double v2 = values[idx + 2];
    double v3 = values[idx + 3];
    
    // ================================================================
    // NUMERICAL STABILITY: Denominator protection for Lagrange basis
    // ================================================================
    // Check for near-zero denominators. With 4 points, the triple
    // products can be extremely small if any two points are close
    // together, leading to catastrophic NaN/Inf in interpolated values.
    // ================================================================
    const double eps = 1e-12;
    double dL0 = (t0 - t1) * (t0 - t2) * (t0 - t3);
    double dL1 = (t1 - t0) * (t1 - t2) * (t1 - t3);
    double dL2 = (t2 - t0) * (t2 - t1) * (t2 - t3);
    double dL3 = (t3 - t0) * (t3 - t1) * (t3 - t2);
    
    if (std::abs(dL0) < eps || std::abs(dL1) < eps ||
        std::abs(dL2) < eps || std::abs(dL3) < eps) {
        API_LOG_WARN("EphemerisInterpolator: Near-zero denominator in cubic "
                     "interpolation (denom=[{:.6e},{:.6e},{:.6e},{:.6e}]), "
                     "falling back to quadratic",
                     dL0, dL1, dL2, dL3);
        return interpolateQuadratic(t, values, times);
    }
    
    // Cubic interpolation using Lagrange polynomials
    double L0 = ((t - t1) * (t - t2) * (t - t3)) / dL0;
    double L1 = ((t - t0) * (t - t2) * (t - t3)) / dL1;
    double L2 = ((t - t0) * (t - t1) * (t - t3)) / dL2;
    double L3 = ((t - t0) * (t - t1) * (t - t2)) / dL3;
    
    return L0 * v0 + L1 * v1 + L2 * v2 + L3 * v3;
}

// ============================================================================
// EphemerisModel implementation
// ============================================================================

class EphemerisModel::Impl {
public:
    Impl(const ::astro_mount::EphemerisData& ephemeris_data, const Config& config)
        : ephemeris_data_(ephemeris_data), config_(config) {
        
        object_id_ = ephemeris_data.object_id();
        object_name_ = ephemeris_data.object_name();
        object_type_ = ephemeris_data.object_type();
        reference_frame_ = ephemeris_data.reference_frame();
        
        // Create interpolator
        std::vector<::astro_mount::EphemerisPoint> points(
            ephemeris_data.points().begin(),
            ephemeris_data.points().end());
        
        interpolator_ = std::make_unique<EphemerisInterpolator>(
            points, static_cast<int>(ephemeris_data.interpolation_order()));
        
        // Create astronomical calculations
        astro_calc_ = std::make_unique<core::AstronomicalCalculations>();
        
        API_LOG_INFO("EphemerisModel: Created for object '{}' ({}) with {} points",
                    object_name_, object_id_, points.size());
    }
    
    ~Impl() = default;
    
    std::tuple<double, double, double, double> getApparentPosition(
        const system_clock::time_point& timestamp,
        double observer_latitude,
        double observer_longitude,
        double observer_altitude) const {
        
        // Get interpolated position
        auto [ra, dec, ra_rate, dec_rate] = interpolator_->getPositionAtTime(timestamp);
        
        // Apply Earth rotation correction using the requested timestamp
        double sidereal_time = calculateSiderealTime(timestamp, observer_longitude);
        applyEarthRotationCorrection(ra, dec, timestamp, observer_longitude, sidereal_time);
        
        // Apply atmospheric corrections if enabled
        if (config_.apply_refraction) {
            // For now, use standard conditions
            applyAtmosphericCorrections(ra, dec, 15.0, 1013.25, 50.0);
        }
        
        // Apply advanced corrections (aberration, nutation, precession) using SOFA
        if (config_.apply_aberration || config_.apply_nutation || config_.apply_precession) {
            // Convert timestamp to Julian Date (same method as calculateSiderealTime)
            auto duration = timestamp.time_since_epoch();
            double seconds = duration_cast<std::chrono::duration<double>>(duration).count();
            double jd = 2440587.5 + seconds / 86400.0;
            
            double ra_before = ra;
            double dec_before = dec;
            
            if (config_.apply_precession && config_.apply_nutation && config_.apply_aberration) {
                // All three corrections enabled — use the combined apparent place method.
                // This applies: precession (J2000→date) → nutation → annual aberration
                // using proper IAU SOFA routines (iauPmat76, iauNut80, iauNumat, iauEpv00, iauAb).
                std::tie(ra, dec) = astro_calc_->calculateApparentPlace(ra, dec, jd);
            } else {
                // Apply individual corrections as configured
                const double J2000 = 2451545.0;
                
                if (config_.apply_precession) {
                    std::tie(ra, dec) = astro_calc_->applyPrecession(ra, dec, J2000, jd);
                }
                
                if (config_.apply_nutation) {
                    std::tie(ra, dec) = astro_calc_->applyNutation(ra, dec, jd);
                }
                
                if (config_.apply_aberration) {
                    std::tie(ra, dec) = applyAberrationCorrection(ra, dec, jd);
                }
            }
            
            API_LOG_DEBUG("EphemerisModel: Advanced corrections applied: "
                         "ΔRA={:.6f}h, ΔDec={:.6f}° (aberration={}, nutation={}, precession={})",
                         ra - ra_before, dec - dec_before,
                         config_.apply_aberration, config_.apply_nutation, config_.apply_precession);
        }
        
        // Apply TPOINT corrections if enabled and model available
        if (config_.apply_tpoint_corrections && config_.tpoint_model) {
            // TPOINT corrections require mount hour angle and declination
            // For now, we approximate mount HA from observer longitude and sidereal time
            double ha = sidereal_time - ra; // hours
            while (ha < -12.0) ha += 24.0;
            while (ha > 12.0) ha -= 24.0;
            
            // Mount declination is same as object declination (for equatorial mounts)
            double mount_dec = dec;
            
            // Apply TPOINT corrections
            auto [corrected_ra, corrected_dec] = config_.tpoint_model->applyCorrections(
                ra, dec, ha, mount_dec);
            
            ra = corrected_ra;
            dec = corrected_dec;
            
            API_LOG_DEBUG("EphemerisModel: TPOINT corrections applied: "
                         "ΔRA={:.6f}h, ΔDec={:.6f}°",
                         corrected_ra - ra, corrected_dec - dec);
        }
        
        return std::make_tuple(ra, dec, ra_rate, dec_rate);
    }
    
    bool isTimeWithinRange(const system_clock::time_point& timestamp) const {
        auto [start, end] = interpolator_->getTimeRange();
        return timestamp >= start && timestamp <= end;
    }
    
    std::pair<system_clock::time_point, system_clock::time_point> getTimeRange() const {
        return interpolator_->getTimeRange();
    }
    
    double getConfidence(const system_clock::time_point& timestamp) const {
        if (!interpolator_->isValid()) {
            return 0.0;
        }
        
        auto [start, end] = interpolator_->getTimeRange();
        
        // Confidence based on time proximity to ephemeris data
        if (timestamp >= start && timestamp <= end) {
            // Within range: high confidence
            return 0.95;
        }
        
        // Outside range: lower confidence based on distance
        auto duration = timestamp.time_since_epoch();
        double t = duration_cast<std::chrono::duration<double>>(duration).count();
        
        auto start_duration = start.time_since_epoch();
        double start_t = duration_cast<std::chrono::duration<double>>(start_duration).count();
        auto end_duration = end.time_since_epoch();
        double end_t = duration_cast<std::chrono::duration<double>>(end_duration).count();
        
        double max_extrapolation = config_.max_extrapolation_seconds;
        
        if (t < start_t) {
            double diff = start_t - t;
            if (diff > max_extrapolation) return 0.0;
            return 0.8 * (1.0 - diff / max_extrapolation);
        } else {
            double diff = t - end_t;
            if (diff > max_extrapolation) return 0.0;
            return 0.8 * (1.0 - diff / max_extrapolation);
        }
    }
    
    void updateConfig(const Config& config) {
        std::lock_guard<std::mutex> lock(cache_mutex_);
        config_ = config;
        position_cache_.clear(); // Clear cache on config change
    }
    
    // Getter methods for EphemerisModel
    std::string getObjectId() const { return object_id_; }
    std::string getObjectName() const { return object_name_; }
    std::string getObjectType() const { return object_type_; }
    Config getConfig() const { return config_; }
    
private:
    ::astro_mount::EphemerisData ephemeris_data_;
    Config config_;
    
    std::string object_id_;
    std::string object_name_;
    std::string object_type_;
    std::string reference_frame_;
    
    std::unique_ptr<EphemerisInterpolator> interpolator_;
    std::unique_ptr<core::AstronomicalCalculations> astro_calc_;
    
    mutable std::mutex cache_mutex_;
    mutable std::map<system_clock::time_point,
                    std::tuple<double, double, double, double>> position_cache_;
    
    std::pair<double, double> applyAberrationCorrection(double ra, double dec, double jd) const {
        // Convert RA/Dec to radians
        // RA: hours → degrees → radians, Dec: degrees → radians
        double ra_rad = ra * 15.0 * (M_PI / 180.0);
        double dec_rad = dec * (M_PI / 180.0);
        
        // Convert spherical to Cartesian (unit vector toward source)
        double pos[3];
        iauS2c(ra_rad, dec_rad, pos);
        
        // Get Earth's barycentric position and velocity from SOFA
        double pvh[2][3], pvb[2][3];
        if (iauEpv00(jd, 0.0, pvh, pvb) != 0) {
            API_LOG_WARN("EphemerisModel: iauEpv00 failed for JD={:.6f}, "
                         "aberration correction skipped", jd);
            return {ra, dec};
        }
        
        // Convert barycentric velocity from AU/day to units of c
        const double C_AUDAY = 173.1446326846693;  // speed of light in AU/day
        double v_vec[3];
        for (int i = 0; i < 3; i++) {
            v_vec[i] = pvb[1][i] / C_AUDAY;
        }
        
        // Compute Earth-Sun distance (AU) from Earth's heliocentric position
        double sun_dist = std::sqrt(pvh[0][0] * pvh[0][0] +
                                    pvh[0][1] * pvh[0][1] +
                                    pvh[0][2] * pvh[0][2]);
        
        // Compute bm1 = sqrt(1 - |v|^2), the reciprocal Lorentz factor
        double v2 = v_vec[0]*v_vec[0] + v_vec[1]*v_vec[1] + v_vec[2]*v_vec[2];
        double bm1 = std::sqrt(1.0 - v2);
        
        // Apply annual aberration using SOFA iauAb
        double pos_aber[3];
        iauAb(pos, v_vec, sun_dist, bm1, pos_aber);
        
        // Convert back to spherical
        double ra_aber_rad, dec_aber_rad;
        iauC2s(pos_aber, &ra_aber_rad, &dec_aber_rad);
        ra_aber_rad = iauAnp(ra_aber_rad);
        
        // Convert back to hours/degrees
        return {ra_aber_rad * (180.0 / M_PI) / 15.0,
                dec_aber_rad * (180.0 / M_PI)};
    }
    
    void applyEarthRotationCorrection(double& ra, double& dec,
                                     const system_clock::time_point& timestamp,
                                     double observer_longitude,
                                     double sidereal_time) const {
        // Proper Earth rotation correction for apparent positions
        // For moving objects, the main effect is the diurnal motion:
        // Apparent RA = Catalog RA - (Local Sidereal Time - Reference Time)
        
        // Earth rotation rate: 15.041 arcseconds per sidereal second
        // Convert to hours: 15.041 / 3600 = 0.004178 hours per second
        const double earth_rotation_rate_hours_per_second = 0.004178;
        
        // Time since ephemeris start, computed from the requested timestamp
        // (NOT system_clock::now(), which would be incorrect for historical/future queries)
        auto [ephem_start, ephem_end] = getTimeRange();
        auto duration = timestamp - ephem_start;
        double dt_seconds = duration_cast<std::chrono::duration<double>>(duration).count();
        
        // Correction in hours due to Earth rotation
        double ra_correction_hours = earth_rotation_rate_hours_per_second * dt_seconds;
        
        // Apply correction to RA (Earth rotates eastward, so apparent RA decreases)
        ra -= ra_correction_hours;
        
        // Normalize RA to 0-24 hours
        while (ra < 0.0) ra += 24.0;
        while (ra >= 24.0) ra -= 24.0;
        
        // For Dec, Earth rotation has negligible direct effect at mid-latitudes
        // but there's a small effect due to polar motion (ignored for now)
        
        // For moving objects, we should also adjust the rates
        // The Earth rotation adds -15.041 arcsec/sec to RA rate
        // This is typically included in the interpolated ephemeris rates
        
        API_LOG_DEBUG("EphemerisModel: Earth rotation correction applied: "
                     "ΔRA = -{:.6f}h, longitude={:.2f}, LST={:.4f}h",
                     ra_correction_hours, observer_longitude, sidereal_time);
    }
    
    void applyAtmosphericCorrections(double& ra, double& dec,
                                    double temperature, double pressure,
                                    double humidity) const {
        // Saastamoinen model for atmospheric refraction
        // First, we need to convert RA/Dec to altitude/azimuth
        // For now, we'll implement a simplified version
        
        // Constants
        const double lapse_rate = 0.0065; // K/m
        const double Rd = 287.05; // Gas constant for dry air (J/(kg·K))
        const double g0 = 9.80665; // Standard gravity (m/s²)
        
        // Calculate zenith distance approximation
        // For mid-latitudes, average zenith distance ~45 degrees
        double zenith_approx = 45.0; // degrees
        double zenith_rad = zenith_approx * M_PI / 180.0;
        
        // Calculate refraction using Saastamoinen formula
        double P = pressure; // hPa
        double T = temperature + 273.15; // Convert to Kelvin
        double e = 6.1078 * exp(17.27 * temperature / (temperature + 237.3)) * humidity / 100.0;
        
        // Refraction in arcseconds
        double refraction_arcsec = (16.271 * P / (T * (1.0 - 0.00266 * cos(2.0 * zenith_rad) - 0.00028 * 0.0))) / 
                                  tan(zenith_rad) - 0.0749 * pow(tan(zenith_rad), 3);
        
        // Apply correction (refraction lifts objects, so altitude increases)
        // This affects both RA and Dec, but mainly altitude
        // For equatorial coordinates, the effect is complex
        
        // Simplified: apply small correction to declination
        // In reality, need full conversion to horizontal coordinates
        
        double dec_correction = refraction_arcsec / 3600.0; // Convert to degrees
        dec += dec_correction;
        
        // RA correction depends on hour angle and latitude
        // Simplified: assume small effect for now
        double ra_correction_hours = (refraction_arcsec / 3600.0) / 15.0 * sin(zenith_rad);
        ra += ra_correction_hours;
        
        API_LOG_DEBUG("EphemerisModel: Atmospheric corrections applied: "
                     "refraction={:.3f}\", Δdec={:.6f}°, Δra={:.6f}h, "
                     "T={:.1f}°C, P={:.1f}hPa, RH={:.1f}%",
                     refraction_arcsec, dec_correction, ra_correction_hours,
                     temperature, pressure, humidity);
    }
    
    double calculateSiderealTime(const system_clock::time_point& timestamp,
                               double observer_longitude) const {
        // Convert to Julian date
        auto duration = timestamp.time_since_epoch();
        double seconds = duration_cast<std::chrono::duration<double>>(duration).count();
        double jd = 2440587.5 + seconds / 86400.0;
        
        // Use SOFA-based GMST (iauGst94) via AstronomicalCalculations
        // Returns LST in hours, normalized to [0, 24)
        // This replaces the previous simplified GMST formula (~0.1s accuracy)
        // with the IAU 1994 model (~0.1 mas accuracy)
        return core::AstronomicalCalculations::calculateLST(jd, observer_longitude);
    }
};

// EphemerisModel public interface
EphemerisModel::EphemerisModel(const ::astro_mount::EphemerisData& ephemeris_data,
                             const Config& config)
    : impl_(std::make_unique<Impl>(ephemeris_data, config)) {}

EphemerisModel::~EphemerisModel() = default;

std::tuple<double, double, double, double> EphemerisModel::getApparentPosition(
    const system_clock::time_point& timestamp,
    double observer_latitude,
    double observer_longitude,
    double observer_altitude) const {
    
    return impl_->getApparentPosition(timestamp, observer_latitude,
                                     observer_longitude, observer_altitude);
}

bool EphemerisModel::isTimeWithinRange(const system_clock::time_point& timestamp) const {
    return impl_->isTimeWithinRange(timestamp);
}

std::pair<system_clock::time_point, system_clock::time_point>
EphemerisModel::getTimeRange() const {
    return impl_->getTimeRange();
}

double EphemerisModel::getConfidence(const system_clock::time_point& timestamp) const {
    return impl_->getConfidence(timestamp);
}

std::string EphemerisModel::getObjectId() const {
    return impl_->getObjectId();
}

std::string EphemerisModel::getObjectName() const {
    return impl_->getObjectName();
}

std::string EphemerisModel::getObjectType() const {
    return impl_->getObjectType();
}

EphemerisModel::Config EphemerisModel::getConfig() const {
    return impl_->getConfig();
}

void EphemerisModel::updateConfig(const Config& config) {
    impl_->updateConfig(config);
}

// ============================================================================
// EphemerisTracker implementation
// ============================================================================

class EphemerisTracker::Impl {
public:
    Impl(std::shared_ptr<EphemerisModel> model,
         double observer_latitude,
         double observer_longitude,
         double observer_altitude)
        : model_(model),
          observer_latitude_(observer_latitude),
          observer_longitude_(observer_longitude),
          observer_altitude_(observer_altitude),
          tracking_active_(false),
          stop_requested_(false),
          encoder_reader_callback_(nullptr),
          rng_(std::random_device{}()) {
        
        API_LOG_INFO("EphemerisTracker: Created for object '{}'",
                    model_->getObjectName());
    }
    
    ~Impl() {
        stopTracking();
    }
    
    bool isTracking() const {
        return tracking_active_;
    }
    
    bool startTracking(const system_clock::time_point& start_time,
                      const TrackingConfig& config) {
        std::unique_lock<std::mutex> lock(state_mutex_);
        
        if (tracking_active_) {
            API_LOG_WARN("EphemerisTracker: Tracking already active");
            return false;
        }
        
        config_ = config;
        stats_ = TrackingStats();
        stats_.start_time = start_time;
        tracking_start_time_ = start_time;
        
        // Calculate end time based on ephemeris range
        auto [ephem_start, ephem_end] = model_->getTimeRange();
        
        // Add prediction interval if enabled
        if (config_.enable_prediction) {
            tracking_end_time_ = ephem_end + 
                hours(static_cast<int>(config_.prediction_interval_hours));
        } else {
            tracking_end_time_ = ephem_end;
        }
        
        stats_.end_time = tracking_end_time_;
        
        // Calculate start position with lead time
        auto start_with_lead = start_time - 
            seconds(static_cast<int>(config_.lead_time_seconds));
        
        try {
            auto [ra, dec, ra_rate, dec_rate] = 
                model_->getApparentPosition(start_with_lead,
                                           observer_latitude_,
                                           observer_longitude_,
                                           observer_altitude_);
            
            current_target_ = std::make_tuple(ra, dec, ra_rate, dec_rate);
            current_position_error_arcsec_ = 0.0;
            last_error_message_.clear();
            warnings_.clear();
            tracking_parameters_["tracking_mode"] = config_.tracking_mode;
            tracking_parameters_["object_id"] = model_->getObjectId();
            tracking_parameters_["object_name"] = model_->getObjectName();
            
        } catch (const std::exception& e) {
            API_LOG_ERROR("EphemerisTracker: Failed to get start position: {}", e.what());
            last_error_message_ = e.what();
            return false;
        }
        
        // Start tracking thread
        tracking_active_ = true;
        stop_requested_ = false;
        tracking_thread_ = std::thread(&Impl::trackingLoop, this);
        
        API_LOG_INFO("EphemerisTracker: Started tracking for object '{}' from {} to {}",
                    model_->getObjectName(),
                    system_clock::to_time_t(tracking_start_time_),
                    system_clock::to_time_t(tracking_end_time_));
        
        notifyCallbacks("tracking_started",
                       "Ephemeris tracking started",
                       {{"object_id", model_->getObjectId()},
                        {"object_name", model_->getObjectName()},
                        {"start_time", std::to_string(system_clock::to_time_t(start_time))}});
        
        return true;
    }
    
    void stopTracking() {
        {
            std::unique_lock<std::mutex> lock(state_mutex_);
            if (!tracking_active_) {
                return;
            }
            
            stop_requested_ = true;
            tracking_active_ = false;
        }
        
        if (tracking_thread_.joinable()) {
            tracking_thread_.join();
        }
        
        // Update statistics
        auto now = system_clock::now();
        stats_.end_time = now;
        stats_.total_track_time_seconds = 
            duration_cast<std::chrono::duration<double>>(now - stats_.start_time).count();
        
        API_LOG_INFO("EphemerisTracker: Stopped tracking for object '{}'",
                    model_->getObjectName());
        
        notifyCallbacks("tracking_stopped",
                       "Ephemeris tracking stopped",
                       {{"object_id", model_->getObjectId()},
                        {"object_name", model_->getObjectName()},
                        {"track_time", std::to_string(stats_.total_track_time_seconds)},
                        {"avg_error", std::to_string(stats_.avg_position_error_arcsec)}});
    }
    
    ::astro_mount::EphemerisTrackStatus getStatus() const {
        std::unique_lock<std::mutex> lock(state_mutex_);
        
        ::astro_mount::EphemerisTrackStatus status;
        
        auto [ra, dec, ra_rate, dec_rate] = current_target_;
        
        // Set tracking state
        if (!tracking_active_) {
            status.set_state(::astro_mount::EphemerisTrackStatus::IDLE);
        } else if (stop_requested_) {
            status.set_state(::astro_mount::EphemerisTrackStatus::ENDED);
        } else {
            auto now = system_clock::now();
            if (now < tracking_start_time_) {
                status.set_state(::astro_mount::EphemerisTrackStatus::WAITING_AT_START);
            } else if (now > tracking_end_time_) {
                status.set_state(::astro_mount::EphemerisTrackStatus::ENDED);
            } else {
                status.set_state(::astro_mount::EphemerisTrackStatus::TRACKING);
            }
        }
        
        status.set_object_id(model_->getObjectId());
        status.set_object_name(model_->getObjectName());
        
        auto now = system_clock::now();
        auto timestamp = google::protobuf::Timestamp();
        timestamp.set_seconds(system_clock::to_time_t(now));
        timestamp.set_nanos(duration_cast<nanoseconds>(
            now.time_since_epoch() % seconds(1)).count());
        *status.mutable_current_time() = timestamp;
        
        // Set start and end times
        auto start_timestamp = google::protobuf::Timestamp();
        start_timestamp.set_seconds(system_clock::to_time_t(tracking_start_time_));
        start_timestamp.set_nanos(0);
        *status.mutable_track_start_time() = start_timestamp;
        
        auto end_timestamp = google::protobuf::Timestamp();
        end_timestamp.set_seconds(system_clock::to_time_t(tracking_end_time_));
        end_timestamp.set_nanos(0);
        *status.mutable_track_end_time() = end_timestamp;
        
        // Set current position
        auto current_position = ::astro_mount::Coordinates();
        current_position.set_ra(ra);
        current_position.set_dec(dec);
        *status.mutable_current_position() = current_position;
        
        // Set target position (same as current for now)
        auto target_position = ::astro_mount::Coordinates();
        target_position.set_ra(ra);
        target_position.set_dec(dec);
        *status.mutable_target_position() = target_position;
        
        status.set_position_error_arcsec(current_position_error_arcsec_);
        status.set_ra_rate(ra_rate);
        status.set_dec_rate(dec_rate);
        
        // Calculate remaining time
        if (tracking_active_ && !stop_requested_) {
            auto remaining = tracking_end_time_ - now;
            status.set_time_remaining_seconds(
                duration_cast<std::chrono::duration<double>>(remaining).count());
        } else {
            status.set_time_remaining_seconds(0.0);
        }
        
        status.set_earth_rotation_corrected(true);
        
        if (!last_error_message_.empty()) {
            status.set_error_message(last_error_message_);
        }
        
        // Add warnings
        for (const auto& warning : warnings_) {
            status.add_warnings(warning);
        }
        
        // Add tracking parameters
        for (const auto& [key, value] : tracking_parameters_) {
            (*status.mutable_tracking_parameters())[key] = value;
        }
        
        return status;
    }
    
    EphemerisTracker::TrackingStats getStatistics() const {
        std::unique_lock<std::mutex> lock(state_mutex_);
        return stats_;
    }
    
    std::tuple<double, double, double, double> getCurrentTarget() const {
        std::unique_lock<std::mutex> lock(state_mutex_);
        return current_target_;
    }
    
    std::tuple<double, double, double, double> getStartPosition(
        const system_clock::time_point& start_time,
        double lead_time_seconds) const {
        
        auto start_with_lead = start_time - seconds(static_cast<int>(lead_time_seconds));
        
        try {
            return model_->getApparentPosition(start_with_lead,
                                              observer_latitude_,
                                              observer_longitude_,
                                              observer_altitude_);
        } catch (const std::exception& e) {
            API_LOG_ERROR("EphemerisTracker: Failed to get start position: {}", e.what());
            throw;
        }
    }
    
    bool updateConfig(const TrackingConfig& config) {
        std::unique_lock<std::mutex> lock(state_mutex_);
        
        if (tracking_active_) {
            API_LOG_WARN("EphemerisTracker: Cannot update config while tracking");
            return false;
        }
        
        config_ = config;
        return true;
    }
    
    void registerCallback(TrackingCallback callback) {
        std::lock_guard<std::mutex> lock(callback_mutex_);
        callbacks_.push_back(callback);
    }
    
    void registerEncoderReader(EncoderReaderCallback callback) {
        std::lock_guard<std::mutex> lock(state_mutex_);
        encoder_reader_callback_ = callback;
        API_LOG_INFO("EphemerisTracker: Encoder reader registered (error_source={})",
                    config_.error_source);
    }
    
    std::string getObjectId() const {
        return model_->getObjectId();
    }
    
    std::string getObjectName() const {
        return model_->getObjectName();
    }
    
    double getRemainingTime() const {
        std::unique_lock<std::mutex> lock(state_mutex_);
        
        if (!tracking_active_ || stop_requested_) {
            return 0.0;
        }
        
        auto now = system_clock::now();
        if (now > tracking_end_time_) {
            return 0.0;
        }
        
        auto remaining = tracking_end_time_ - now;
        return duration_cast<std::chrono::duration<double>>(remaining).count();
    }
    
    bool performRecovery() {
        std::unique_lock<std::mutex> lock(state_mutex_);
        
        if (!tracking_active_) {
            return false;
        }
        
        stats_.recovery_attempts++;
        
        // Try to get current position
        try {
            auto now = system_clock::now();
            auto [ra, dec, ra_rate, dec_rate] = 
                model_->getApparentPosition(now,
                                           observer_latitude_,
                                           observer_longitude_,
                                           observer_altitude_);
            
            current_target_ = std::make_tuple(ra, dec, ra_rate, dec_rate);
            current_position_error_arcsec_ = 0.0;
            last_error_message_.clear();
            
            API_LOG_INFO("EphemerisTracker: Recovery successful for object '{}'",
                        model_->getObjectName());
            
            notifyCallbacks("recovery_successful",
                           "Tracking recovery successful",
                           {{"object_id", model_->getObjectId()},
                            {"recovery_attempt", std::to_string(stats_.recovery_attempts)}});
            
            return true;
            
        } catch (const std::exception& e) {
            API_LOG_ERROR("EphemerisTracker: Recovery failed: {}", e.what());
            last_error_message_ = e.what();
            
            notifyCallbacks("recovery_failed",
                           "Tracking recovery failed",
                           {{"object_id", model_->getObjectId()},
                            {"error", e.what()},
                            {"recovery_attempt", std::to_string(stats_.recovery_attempts)}});
            
            return false;
        }
    }
    
private:
    void trackingLoop() {
        API_LOG_DEBUG("EphemerisTracker: Tracking thread started");
        
        while (!stop_requested_) {
            try {
                updateTracking();
                
                // Sleep based on update rate
                auto sleep_time = milliseconds(
                    static_cast<int>(1000.0 / config_.update_rate_hz));
                std::this_thread::sleep_for(sleep_time);
                
            } catch (const std::exception& e) {
                API_LOG_ERROR("EphemerisTracker: Error in tracking loop: {}", e.what());
                handleTrackingError(e.what());
                
                // Sleep longer on error
                std::this_thread::sleep_for(seconds(1));
            }
        }
        
        API_LOG_DEBUG("EphemerisTracker: Tracking thread stopped");
    }
    
    void updateTracking() {
        std::unique_lock<std::mutex> lock(state_mutex_);
        
        if (!tracking_active_ || stop_requested_) {
            return;
        }
        
        auto now = system_clock::now();
        
        // Check if tracking should end
        if (now > tracking_end_time_) {
            API_LOG_INFO("EphemerisTracker: Tracking ended for object '{}'",
                        model_->getObjectName());
            stop_requested_ = true;
            return;
        }
        
        // Update target position
        try {
            auto [ra, dec, ra_rate, dec_rate] = 
                model_->getApparentPosition(now,
                                           observer_latitude_,
                                           observer_longitude_,
                                           observer_altitude_);
            
            current_target_ = std::make_tuple(ra, dec, ra_rate, dec_rate);
            
            // Calculate position error based on configured source
            double error_arcsec = 0.0;
            if (config_.error_source == "encoder" && encoder_reader_callback_) {
                // Real position from encoder feedback
                try {
                    auto [encoder_ra, encoder_dec] = encoder_reader_callback_();
                    
                    // Calculate error as difference between target and actual encoder position
                    double ra_diff = (ra - encoder_ra) * 15.0; // Convert hours to degrees
                    double dec_diff = dec - encoder_dec;
                    error_arcsec = std::sqrt(ra_diff * ra_diff + dec_diff * dec_diff) * 3600.0;
                    
                    API_LOG_DEBUG("EphemerisTracker: Encoder error = {:.2f} arcsec "
                                 "(target RA={:.6f}h Dec={:.6f}°, "
                                 "encoder RA={:.6f}h Dec={:.6f}°)",
                                 error_arcsec, ra, dec, encoder_ra, encoder_dec);
                } catch (const std::exception& e) {
                    API_LOG_ERROR("EphemerisTracker: Encoder read failed: {}", e.what());
                    error_arcsec = 999.0; // Max error on failure
                }
            } else {
                // Simulated position error (for testing without hardware)
                error_arcsec = std::uniform_real_distribution<double>(0.0, 10.0)(rng_);
            }
            current_position_error_arcsec_ = error_arcsec;
            
            // Update statistics
            updateStatistics(error_arcsec, 0.0);
            
            stats_.tracking_updates++;
            
        } catch (const std::exception& e) {
            API_LOG_ERROR("EphemerisTracker: Failed to update tracking: {}", e.what());
            last_error_message_ = e.what();
            stats_.errors++;
            
            // Check if we should attempt recovery
            if (stats_.errors > 3 && stats_.recovery_attempts < config_.max_recovery_attempts) {
                lock.unlock(); // Release lock before recovery
                performRecovery();
            }
        }
    }
    
    void handleTrackingError(const std::string& error_message) {
        std::unique_lock<std::mutex> lock(state_mutex_);
        
        last_error_message_ = error_message;
        stats_.errors++;
        warnings_.push_back("Tracking error: " + error_message);
        
        notifyCallbacks("tracking_error",
                       "Tracking error occurred",
                       {{"object_id", model_->getObjectId()},
                        {"error", error_message},
                        {"error_count", std::to_string(stats_.errors)}});
    }
    
    void notifyCallbacks(const std::string& event_type,
                        const std::string& message,
                        const std::map<std::string, std::string>& context) {
        std::lock_guard<std::mutex> lock(callback_mutex_);
        
        for (const auto& callback : callbacks_) {
            try {
                callback(event_type, message, context);
            } catch (const std::exception& e) {
                API_LOG_ERROR("EphemerisTracker: Callback error: {}", e.what());
            }
        }
    }
    
    void updateStatistics(double position_error, double rate_error) {
        // Update running averages
        if (stats_.tracking_updates == 0) {
            stats_.avg_position_error_arcsec = position_error;
            stats_.avg_rate_error = rate_error;
            stats_.max_position_error_arcsec = position_error;
        } else {
            stats_.avg_position_error_arcsec = 
                (stats_.avg_position_error_arcsec * stats_.tracking_updates + position_error) /
                (stats_.tracking_updates + 1);
            stats_.avg_rate_error = 
                (stats_.avg_rate_error * stats_.tracking_updates + rate_error) /
                (stats_.tracking_updates + 1);
            
            if (position_error > stats_.max_position_error_arcsec) {
                stats_.max_position_error_arcsec = position_error;
            }
        }
        
        // Check if prediction was used
        auto now = system_clock::now();
        auto [ephem_start, ephem_end] = model_->getTimeRange();
        if (now > ephem_end && config_.enable_prediction) {
            stats_.prediction_count++;
        }
        
        stats_.earth_rotation_applied = true;
    }
    
    std::shared_ptr<EphemerisModel> model_;
    double observer_latitude_;
    double observer_longitude_;
    double observer_altitude_;
    
    TrackingConfig config_;
    TrackingStats stats_;
    
    std::atomic<bool> tracking_active_{false};
    std::atomic<bool> stop_requested_{false};
    system_clock::time_point tracking_start_time_;
    system_clock::time_point tracking_end_time_;
    
    mutable std::mutex state_mutex_;
    std::tuple<double, double, double, double> current_target_;
    double current_position_error_arcsec_{0.0};
    std::string last_error_message_;
    std::vector<std::string> warnings_;
    std::map<std::string, std::string> tracking_parameters_;
    
    mutable std::mt19937 rng_;
    
    std::vector<TrackingCallback> callbacks_;
    mutable std::mutex callback_mutex_;
    
    EncoderReaderCallback encoder_reader_callback_;
    
    std::thread tracking_thread_;
};

// EphemerisTracker public interface
EphemerisTracker::EphemerisTracker(std::shared_ptr<EphemerisModel> model,
                                  double observer_latitude,
                                  double observer_longitude,
                                  double observer_altitude)
    : impl_(std::make_unique<Impl>(model, observer_latitude,
                                  observer_longitude, observer_altitude)) {}

EphemerisTracker::~EphemerisTracker() = default;

bool EphemerisTracker::isTracking() const {
    return impl_ ? impl_->isTracking() : false;
}

bool EphemerisTracker::startTracking(const system_clock::time_point& start_time,
                                    const TrackingConfig& config) {
    return impl_->startTracking(start_time, config);
}

void EphemerisTracker::stopTracking() {
    impl_->stopTracking();
}

::astro_mount::EphemerisTrackStatus EphemerisTracker::getStatus() const {
    return impl_->getStatus();
}

EphemerisTracker::TrackingStats EphemerisTracker::getStatistics() const {
    return impl_->getStatistics();
}

std::tuple<double, double, double, double> EphemerisTracker::getCurrentTarget() const {
    return impl_->getCurrentTarget();
}

std::tuple<double, double, double, double> EphemerisTracker::getStartPosition(
    const system_clock::time_point& start_time,
    double lead_time_seconds) const {
    
    return impl_->getStartPosition(start_time, lead_time_seconds);
}

bool EphemerisTracker::updateConfig(const TrackingConfig& config) {
    return impl_->updateConfig(config);
}

void EphemerisTracker::registerCallback(TrackingCallback callback) {
    impl_->registerCallback(callback);
}

void EphemerisTracker::registerEncoderReader(EncoderReaderCallback callback) {
    impl_->registerEncoderReader(callback);
}

std::string EphemerisTracker::getObjectId() const {
    return impl_->getObjectId();
}

std::string EphemerisTracker::getObjectName() const {
    return impl_->getObjectName();
}

double EphemerisTracker::getRemainingTime() const {
    return impl_->getRemainingTime();
}

bool EphemerisTracker::performRecovery() {
    return impl_->performRecovery();
}

// ============================================================================
// EphemerisTrackerManager implementation
// ============================================================================

class EphemerisTrackerManager::Impl {
public:
    Impl() : next_tracker_id_(1) {
        API_LOG_INFO("EphemerisTrackerManager: Created");
    }
    
    ~Impl() {
        std::lock_guard<std::mutex> lock(mutex_);
        active_trackers_.clear();
        API_LOG_INFO("EphemerisTrackerManager: Destroyed");
    }
    
    bool uploadEphemeris(const ::astro_mount::EphemerisData& ephemeris_data) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        std::string object_id = ephemeris_data.object_id();
        if (object_id.empty()) {
            API_LOG_ERROR("EphemerisTrackerManager: Object ID cannot be empty");
            return false;
        }
        
        // Create and store model
        EphemerisModel::Config default_config;
        auto model = std::make_shared<EphemerisModel>(ephemeris_data, default_config);
        ephemeris_models_[object_id] = model;
        
        API_LOG_INFO("EphemerisTrackerManager: Uploaded ephemeris for object '{}' ({})",
                    ephemeris_data.object_name(), object_id);
        
        return true;
    }
    
    std::string startTracking(const std::string& object_id,
                             const system_clock::time_point& start_time,
                             const EphemerisTracker::TrackingConfig& config) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        // Check if model exists
        auto it = ephemeris_models_.find(object_id);
        if (it == ephemeris_models_.end()) {
            API_LOG_ERROR("EphemerisTrackerManager: No ephemeris data for object '{}'",
                         object_id);
            return "";
        }
        
        // Create tracker
        std::string tracker_id = "tracker_" + std::to_string(next_tracker_id_++);
        
        auto tracker = std::make_shared<EphemerisTracker>(
            it->second, observer_latitude_, observer_longitude_, observer_altitude_);
        
        if (!tracker->startTracking(start_time, config)) {
            API_LOG_ERROR("EphemerisTrackerManager: Failed to start tracking for object '{}'",
                         object_id);
            return "";
        }
        
        active_trackers_[tracker_id] = tracker;
        
        // Update statistics
        {
            std::lock_guard<std::mutex> stats_lock(stats_mutex_);
            total_tracks_++;
        }
        
        API_LOG_INFO("EphemerisTrackerManager: Started tracking '{}' with ID {}",
                    object_id, tracker_id);
        
        return tracker_id;
    }
    
    std::string startTrackingWithData(const ::astro_mount::EphemerisData& ephemeris_data,
                                     const system_clock::time_point& start_time,
                                     const EphemerisTracker::TrackingConfig& config) {
        // First upload the ephemeris
        if (!uploadEphemeris(ephemeris_data)) {
            return "";
        }
        
        // Then start tracking
        return startTracking(ephemeris_data.object_id(), start_time, config);
    }
    
    bool stopTracking(const std::string& tracker_id) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        auto it = active_trackers_.find(tracker_id);
        if (it == active_trackers_.end()) {
            API_LOG_WARN("EphemerisTrackerManager: Tracker '{}' not found", tracker_id);
            return false;
        }
        
        it->second->stopTracking();
        
        // Capture final statistics before removing tracker from active map
        {
            auto stats = it->second->getStatistics();
            std::lock_guard<std::mutex> stats_lock(stats_mutex_);
            total_track_time_seconds_ += stats.total_track_time_seconds;
            total_position_error_sum_ += stats.avg_position_error_arcsec;
            if (stats.max_position_error_arcsec > max_position_error_) {
                max_position_error_ = stats.max_position_error_arcsec;
            }
            total_predictions_ += stats.prediction_count;
        }
        
        active_trackers_.erase(it);
        
        API_LOG_INFO("EphemerisTrackerManager: Stopped tracker '{}'", tracker_id);
        return true;
    }
    
    std::shared_ptr<EphemerisTracker> getTracker(const std::string& tracker_id) const {
        std::lock_guard<std::mutex> lock(mutex_);
        
        auto it = active_trackers_.find(tracker_id);
        if (it == active_trackers_.end()) {
            return nullptr;
        }
        
        return it->second;
    }
    
    std::map<std::string, std::shared_ptr<EphemerisTracker>> getActiveTrackers() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return active_trackers_;
    }
    
    std::vector<::astro_mount::EphemerisTrackStatus> getAllStatuses() const {
        std::lock_guard<std::mutex> lock(mutex_);
        
        std::vector<::astro_mount::EphemerisTrackStatus> statuses;
        for (const auto& [tracker_id, tracker] : active_trackers_) {
            statuses.push_back(tracker->getStatus());
        }
        
        return statuses;
    }
    
    void clearCache() {
        std::lock_guard<std::mutex> lock(mutex_);
        
        // Stop all active trackers first
        for (auto& [tracker_id, tracker] : active_trackers_) {
            tracker->stopTracking();
        }
        
        active_trackers_.clear();
        ephemeris_models_.clear();
        
        API_LOG_INFO("EphemerisTrackerManager: Cleared cache");
    }
    
    ::astro_mount::EphemerisMetrics getMetrics() const {
        ::astro_mount::EphemerisMetrics metrics;
        
        std::lock_guard<std::mutex> lock(mutex_);
        std::lock_guard<std::mutex> stats_lock(stats_mutex_);
        
        // Start with accumulated metrics from completed (stopped) trackers
        double total_track_time = total_track_time_seconds_;
        double total_error_sum = total_position_error_sum_;
        double max_error = max_position_error_;
        uint32_t total_preds = total_predictions_;
        
        // Collect metrics from all currently active trackers
        for (const auto& [tracker_id, tracker] : active_trackers_) {
            auto stats = tracker->getStatistics();
            
            // Accumulate metrics
            total_track_time += stats.total_track_time_seconds;
            total_error_sum += stats.avg_position_error_arcsec;
            
            if (stats.max_position_error_arcsec > max_error) {
                max_error = stats.max_position_error_arcsec;
            }
            
            total_preds += stats.prediction_count;
        }
        
        if (total_tracks_ > 0) {
            metrics.set_object_id("all");
            metrics.set_object_type("combined");
            metrics.set_total_track_time_seconds(total_track_time);
            metrics.set_avg_position_error_arcsec(total_error_sum / total_tracks_);
            metrics.set_max_position_error_arcsec(max_error);
            metrics.set_avg_tracking_rate_error(0.0); // Would need rate error data
            metrics.set_prediction_count(total_preds);
            metrics.set_prediction_accuracy(0.95); // Placeholder
            metrics.set_earth_rotation_applied(true);
        }
        
        return metrics;
    }
    
    void setObserverLocation(double latitude, double longitude, double altitude) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        observer_latitude_ = latitude;
        observer_longitude_ = longitude;
        observer_altitude_ = altitude;
        
        API_LOG_INFO("EphemerisTrackerManager: Observer location set to "
                    "lat={}, lon={}, alt={}m",
                    latitude, longitude, altitude);
    }
    
private:
    mutable std::mutex mutex_;
    std::map<std::string, std::shared_ptr<EphemerisModel>> ephemeris_models_;
    std::map<std::string, std::shared_ptr<EphemerisTracker>> active_trackers_;
    
    double observer_latitude_{0.0};
    double observer_longitude_{0.0};
    double observer_altitude_{0.0};
    
    int next_tracker_id_{1};
    
    mutable std::mutex stats_mutex_;
    uint64_t total_tracks_{0};
    double total_track_time_seconds_{0.0};
    double total_position_error_sum_{0.0};
    double max_position_error_{0.0};
    uint32_t total_predictions_{0};
};

// EphemerisTrackerManager public interface
EphemerisTrackerManager::EphemerisTrackerManager()
    : impl_(std::make_unique<Impl>()) {}

EphemerisTrackerManager::~EphemerisTrackerManager() = default;

bool EphemerisTrackerManager::uploadEphemeris(const ::astro_mount::EphemerisData& ephemeris_data) {
    return impl_->uploadEphemeris(ephemeris_data);
}

std::string EphemerisTrackerManager::startTracking(
    const std::string& object_id,
    const system_clock::time_point& start_time,
    const EphemerisTracker::TrackingConfig& config) {
    
    return impl_->startTracking(object_id, start_time, config);
}

std::string EphemerisTrackerManager::startTrackingWithData(
    const ::astro_mount::EphemerisData& ephemeris_data,
    const system_clock::time_point& start_time,
    const EphemerisTracker::TrackingConfig& config) {
    
    return impl_->startTrackingWithData(ephemeris_data, start_time, config);
}

bool EphemerisTrackerManager::stopTracking(const std::string& tracker_id) {
    return impl_->stopTracking(tracker_id);
}

std::shared_ptr<EphemerisTracker> EphemerisTrackerManager::getTracker(
    const std::string& tracker_id) const {
    
    return impl_->getTracker(tracker_id);
}

std::map<std::string, std::shared_ptr<EphemerisTracker>>
EphemerisTrackerManager::getActiveTrackers() const {
    return impl_->getActiveTrackers();
}

std::vector<::astro_mount::EphemerisTrackStatus> 
EphemerisTrackerManager::getAllStatuses() const {
    return impl_->getAllStatuses();
}

void EphemerisTrackerManager::clearCache() {
    impl_->clearCache();
}

::astro_mount::EphemerisMetrics EphemerisTrackerManager::getMetrics() const {
    return impl_->getMetrics();
}

void EphemerisTrackerManager::setObserverLocation(double latitude, double longitude,
                                                 double altitude) {
    impl_->setObserverLocation(latitude, longitude, altitude);
}

} // namespace models
} // namespace astro_mount