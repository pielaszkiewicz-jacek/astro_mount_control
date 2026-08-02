#ifndef FOCUS_CURVE_H
#define FOCUS_CURVE_H

#include <vector>
#include <cstdint>
#include <string>

namespace astro_mount {
namespace models {

/**
 * @brief Focus curve data and fitting model
 *
 * Represents a V-curve (HFD vs focuser position) used for
 * auto-focusing. Supports parabolic and hyperbolic fitting.
 *
 * The V-curve model:
 *   HFD(p) = A * (p - p0)² + HFD_min
 *
 * where:
 *   p0 = best focus position
 *   HFD_min = minimum HFD at best focus
 *   A = curve steepness (related to telescope f-ratio)
 */
struct FocusCurveData {
    // Raw data points
    std::vector<int32_t> positions;     // Focuser positions [steps]
    std::vector<double> hfd_values;     // Measured HFD [pixels]

    // Fitted parameters (parabolic model: HFD = A*(p-B)² + C)
    double vcurve_a{0.0};               // A coefficient (steepness)
    double vcurve_b{0.0};               // B coefficient (focus position)
    double vcurve_c{0.0};               // C coefficient (minimum HFD)

    // Results
    int32_t focus_position{0};          // Best focus position [steps]
    double focus_hfd{0.0};              // HFD at best focus
    double r_squared{0.0};              // Fit quality (1.0 = perfect)

    // Metadata
    std::string algorithm{"parabolic"}; // "parabolic" or "hyperbolic"
    double temperature_c{20.0};         // Temperature during measurement
};

/**
 * @brief Focus curve fitting engine
 *
 * Analyzes HFD measurements at various focus positions to
 * determine the optimal focus point. Uses least-squares
 * parabolic fitting for the V-curve.
 */
class FocusCurve {
public:
    FocusCurve();

    /**
     * @brief Add a measurement point
     * @param position Focuser position [steps]
     * @param hfd Measured HFD [pixels]
     */
    void addMeasurement(int32_t position, double hfd);

    /**
     * @brief Clear all measurements
     */
    void clear();

    /**
     * @brief Fit the focus curve and find best focus
     * @param algorithm Fitting algorithm ("parabolic" or "hyperbolic")
     * @return Fitted curve data with best focus position
     */
    FocusCurveData fit(const std::string& algorithm = "parabolic");

    /**
     * @brief Get current number of measurements
     */
    size_t measurementCount() const;

    /**
     * @brief Check if we have enough measurements for a reliable fit
     * @return true if at least 3 measurements exist
     */
    bool hasEnoughData() const;

    /**
     * @brief Get the raw data
     */
    const FocusCurveData& getData() const;

    /**
     * @brief Calculate estimated HFD at a given position using fitted curve
     * @param position Focuser position [steps]
     * @return Estimated HFD [pixels]
     */
    double evaluateAt(int32_t position) const;

    /**
     * @brief Get the depth of focus (range where HFD < 1.1 * minHFD)
     * @return Depth of focus in steps
     */
    int32_t getFocusDepth() const;

private:
    /**
     * @brief Parabolic least-squares fit: HFD = A*(p-B)² + C
     */
    bool fitParabolic(FocusCurveData& data);

    /**
     * @brief Hyperbolic fit: HFD = A*sqrt((p-B)² + C²) + D
     * Better for fast optics but requires more points
     */
    bool fitHyperbolic(FocusCurveData& data);

    FocusCurveData data_;
    bool fitted_{false};
};

} // namespace models
} // namespace astro_mount

#endif // FOCUS_CURVE_H
