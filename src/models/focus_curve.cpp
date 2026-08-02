#include "models/focus_curve.h"
#include <cmath>
#include <algorithm>
#include <limits>
#include <numeric>

namespace astro_mount {
namespace models {

FocusCurve::FocusCurve() {
    clear();
}

void FocusCurve::addMeasurement(int32_t position, double hfd) {
    data_.positions.push_back(position);
    data_.hfd_values.push_back(hfd);
    fitted_ = false;
}

void FocusCurve::clear() {
    data_.positions.clear();
    data_.hfd_values.clear();
    data_.vcurve_a = 0.0;
    data_.vcurve_b = 0.0;
    data_.vcurve_c = 0.0;
    data_.focus_position = 0;
    data_.focus_hfd = 0.0;
    data_.r_squared = 0.0;
    fitted_ = false;
}

FocusCurveData FocusCurve::fit(const std::string& algorithm) {
    if (!hasEnoughData()) return data_;

    data_.algorithm = algorithm;

    if (algorithm == "hyperbolic") {
        fitHyperbolic(data_);
    } else {
        fitParabolic(data_);
    }

    fitted_ = true;
    return data_;
}

size_t FocusCurve::measurementCount() const {
    return data_.positions.size();
}

bool FocusCurve::hasEnoughData() const {
    return data_.positions.size() >= 3;
}

const FocusCurveData& FocusCurve::getData() const {
    return data_;
}

double FocusCurve::evaluateAt(int32_t position) const {
    if (!fitted_) return 0.0;

    double p = static_cast<double>(position);
    // HFD = A * (p - B)² + C
    return data_.vcurve_a * std::pow(p - data_.vcurve_b, 2) + data_.vcurve_c;
}

int32_t FocusCurve::getFocusDepth() const {
    if (!fitted_) return 0;

    double min_hfd = data_.focus_hfd;
    double threshold = min_hfd * 1.1;  // 10% above minimum

    // Find positions where HFD <= threshold on each side of focus
    int32_t left_pos = data_.focus_position;
    int32_t right_pos = data_.focus_position;

    // Step outward from focus position
    for (int32_t step = 1; step < 50000; ++step) {
        double left_hfd = evaluateAt(data_.focus_position - step);
        double right_hfd = evaluateAt(data_.focus_position + step);

        if (left_hfd <= threshold) left_pos = data_.focus_position - step;
        if (right_hfd <= threshold) right_pos = data_.focus_position + step;

        if (left_hfd > threshold && right_hfd > threshold) break;
    }

    return right_pos - left_pos;
}

// ─── Private Methods ─────────────────────────────────────────────────────────

bool FocusCurve::fitParabolic(FocusCurveData& data) {
    size_t n = data.positions.size();
    if (n < 3) return false;

    // Linearized least-squares fit for parabola: HFD = A*(p-B)² + C
    // We solve using the form: y = a*x² + b*x + c
    // where y = HFD, x = position

    double sum_x = 0, sum_x2 = 0, sum_x3 = 0, sum_x4 = 0;
    double sum_y = 0, sum_xy = 0, sum_x2y = 0;

    for (size_t i = 0; i < n; ++i) {
        double x = static_cast<double>(data.positions[i]);
        double y = data.hfd_values[i];
        double x2 = x * x;
        double x3 = x2 * x;
        double x4 = x3 * x;

        sum_x += x;
        sum_x2 += x2;
        sum_x3 += x3;
        sum_x4 += x4;
        sum_y += y;
        sum_xy += x * y;
        sum_x2y += x2 * y;
    }

    // Solve normal equations using Cramer's rule
    // [n    sum_x  sum_x2] [c]   [sum_y]
    // [sum_x sum_x2 sum_x3] [b] = [sum_xy]
    // [sum_x2 sum_x3 sum_x4] [a]  [sum_x2y]

    double det = n * (sum_x2 * sum_x4 - sum_x3 * sum_x3)
               - sum_x * (sum_x * sum_x4 - sum_x3 * sum_x2)
               + sum_x2 * (sum_x * sum_x3 - sum_x2 * sum_x2);

    if (std::abs(det) < 1e-15) return false;

    double a = (n * (sum_x2 * sum_x2y - sum_x3 * sum_xy)
              - sum_x * (sum_x * sum_x2y - sum_x3 * sum_y)
              + sum_x2 * (sum_x * sum_xy - sum_x2 * sum_y)) / det;

    double b = (n * (sum_x * sum_x2y - sum_x2 * sum_xy)
              - sum_x * (sum_x2 * sum_x2y - sum_x3 * sum_y)
              + sum_x2 * (sum_x2 * sum_xy - sum_x3 * sum_y)) / det;

    double c = (n * (sum_x2 * sum_x2y - sum_x3 * sum_xy)
              - sum_x * (sum_x * sum_x2y - sum_x3 * sum_y)
              + sum_x2 * (sum_x * sum_xy - sum_x2 * sum_y)) / det;

    // Convert to vertex form: HFD = A*(p-B)² + C
    // where: A = a, B = -b/(2*a), C = c - b²/(4*a)
    if (std::abs(a) < 1e-15) return false;

    data.vcurve_a = a;
    data.vcurve_b = -b / (2.0 * a);
    data.vcurve_c = c - b * b / (4.0 * a);

    data.focus_position = static_cast<int32_t>(std::round(data.vcurve_b));
    data.focus_hfd = data.vcurve_c;

    // Calculate R-squared
    double mean_y = sum_y / n;
    double ss_total = 0, ss_residual = 0;
    for (size_t i = 0; i < n; ++i) {
        double x = static_cast<double>(data.positions[i]);
        double y_observed = data.hfd_values[i];
        double y_predicted = evaluateAt(data.positions[i]);
        ss_total += std::pow(y_observed - mean_y, 2);
        ss_residual += std::pow(y_observed - y_predicted, 2);
    }

    data.r_squared = 1.0 - (ss_residual / (ss_total + 1e-15));

    return true;
}

bool FocusCurve::fitHyperbolic(FocusCurveData& data) {
    // Hyperbolic fit: HFD = A*sqrt((p-B)² + C²) + D
    // Requires more measurements, better for fast optics
    // TODO: Implement iterative Levenberg-Marquardt for hyperbolic fit
    // For now, fall back to parabolic
    return fitParabolic(data);
}

} // namespace models
} // namespace astro_mount
