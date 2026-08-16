#include "models/focus_curve.h"
#include <Eigen/Dense>
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
    data_.vcurve_d = 0.0;
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
    if (data_.algorithm == "hyperbolic") {
        // HFD = A * sqrt((p - B)² + C²) + D
        double dx = p - data_.vcurve_b;
        return data_.vcurve_a * std::sqrt(dx * dx + data_.vcurve_c * data_.vcurve_c)
               + data_.vcurve_d;
    }
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

    // NUMERICAL STABILITY / CORRECTNESS FIX:
    // The previous implementation solved the normal equations for y = a·x² + b·x + c
    // directly with Cramer's rule. Two problems: (1) the formulas for b and c were
    // WRONG (c was copy-pasted from a), yielding incorrect vertex parameters; and
    // (2) for large focuser positions the normal matrix is ill-conditioned (κ ~ x²).
    // This version centers the data on the mean position (u = x − x̄), which makes
    // the normal equations well-conditioned, and solves the 3×3 system with Eigen's
    // column-pivoting QR instead of Cramer's rule.

    double sum_x = 0, sum_y = 0;
    for (size_t i = 0; i < n; ++i) {
        sum_x += static_cast<double>(data.positions[i]);
        sum_y += data.hfd_values[i];
    }
    double x_mean = sum_x / n;
    double y_mean = sum_y / n;

    // Moments of the centered variable u = x - x̄:
    //   S1 = Σu, S2 = Σu², S3 = Σu³, S4 = Σu⁴
    //   R0 = Σy, R1 = Σu·y, R2 = Σu²·y
    double S1 = 0, S2 = 0, S3 = 0, S4 = 0;
    double R0 = 0, R1 = 0, R2 = 0;
    for (size_t i = 0; i < n; ++i) {
        double x = static_cast<double>(data.positions[i]);
        double y = data.hfd_values[i];
        double u = x - x_mean;
        double u2 = u * u;
        double u3 = u2 * u;
        double u4 = u3 * u;
        S1 += u; S2 += u2; S3 += u3; S4 += u4;
        R0 += y; R1 += u * y; R2 += u2 * y;
    }

    // Normal equations for y = α·u² + β·u + γ:
    //   [Σu⁴ Σu³ Σu²] [α]   [Σu²y]
    //   [Σu³ Σu² Σu ] [β] = [Σuy ]
    //   [Σu² Σu  n  ] [γ]   [Σy  ]
    Eigen::Matrix3d M;
    M << S4, S3, S2,
         S3, S2, S1,
         S2, S1, static_cast<double>(n);
    Eigen::Vector3d rhs(R2, R1, R0);

    Eigen::Vector3d coeff = M.colPivHouseholderQr().solve(rhs);
    if (!coeff.allFinite()) {
        return false;  // degenerate/singular system
    }
    double alpha = coeff(0);
    double beta  = coeff(1);
    double gamma = coeff(2);

    if (std::abs(alpha) < 1e-15) return false;

    // Convert to vertex form: HFD = A·(p − B)² + C
    //   A = α
    //   B = x̄ − β/(2α)   (vertex in original position units)
    //   C = γ − β²/(4α)
    data.vcurve_a = alpha;
    data.vcurve_b = x_mean - beta / (2.0 * alpha);
    data.vcurve_c = gamma - beta * beta / (4.0 * alpha);

    data.focus_position = static_cast<int32_t>(std::round(data.vcurve_b));
    data.focus_hfd = data.vcurve_c;

    // Calculate R-squared. Evaluate the model INLINE (not via evaluateAt(),
    // which returns 0 until fitted_ is set — that would make every predicted
    // value 0 and drive R² negative during the fit).
    double ss_total = 0, ss_residual = 0;
    for (size_t i = 0; i < n; ++i) {
        double y_observed = data.hfd_values[i];
        double dx = static_cast<double>(data.positions[i]) - data.vcurve_b;
        double y_predicted = data.vcurve_a * dx * dx + data.vcurve_c;
        ss_total += std::pow(y_observed - y_mean, 2);
        ss_residual += std::pow(y_observed - y_predicted, 2);
    }

    data.r_squared = 1.0 - (ss_residual / (ss_total + 1e-15));

    return true;
}

// Residual of the hyperbolic model at the i-th point:
//   r_i = y_i - (A*sqrt((x_i-B)² + C²) + D)
static void hyperbolicResiduals(const std::vector<int32_t>& positions,
                                const std::vector<double>& hfd,
                                const Eigen::Vector4d& p,
                                Eigen::VectorXd& residuals) {
    const size_t n = positions.size();
    residuals.resize(static_cast<Eigen::Index>(n));
    for (size_t i = 0; i < n; ++i) {
        double dx = static_cast<double>(positions[i]) - p(1);
        double model = p(0) * std::sqrt(dx * dx + p(2) * p(2)) + p(3);
        residuals(static_cast<Eigen::Index>(i)) = hfd[i] - model;
    }
}

bool FocusCurve::fitHyperbolic(FocusCurveData& data) {
    // Hyperbolic fit: HFD = A*sqrt((p-B)² + C²) + D
    //   A  — slope of the V wings (steepness)
    //   B  — best-focus position (vertex)
    //   C  — curvature radius at the vertex ("knee" width, C > 0)
    //   D  — asymptotic floor below the measured minimum
    // Near the vertex the model behaves like
    //   HFD ≈ (A*C + D) + (A/(2C))·(p-B)²,
    // so the parabolic curvature α ≈ A/(2C) and the minimum ≈ A*C + D.
    const size_t n = data.positions.size();
    // Four parameters need at least four points; otherwise fall back to the
    // parabolic fit so the caller still gets a usable V-curve.
    if (n < 4) return fitParabolic(data);

    // Initial guess from a parabolic pre-fit:
    //   A = 2·C·α,  C = sqrt((min - D)/(2·α)),  D = 0.6·min
    FocusCurveData tmp = data;
    if (!fitParabolic(tmp)) return false;
    double alpha = tmp.vcurve_a;
    if (std::abs(alpha) < 1e-15) return false;
    double min_hfd = tmp.vcurve_c;
    if (min_hfd <= 1e-9) min_hfd = 1e-6;
    double d0 = min_hfd * 0.6;
    double c0 = std::sqrt(std::max(1e-6, (min_hfd - d0) / (2.0 * std::abs(alpha))));
    double a0 = 2.0 * c0 * std::abs(alpha);

    Eigen::Vector4d p(a0, tmp.vcurve_b, c0, d0);

    // Levenberg-Marquardt with a numerical Jacobian (central differences).
    const int max_iter = 120;
    double lambda = 1e-3;
    Eigen::VectorXd residuals;
    hyperbolicResiduals(data.positions, data.hfd_values, p, residuals);
    double cost = residuals.squaredNorm();

    for (int iter = 0; iter < max_iter; ++iter) {
        Eigen::MatrixXd J(n, 4);
        const double h = 1e-6;
        for (int k = 0; k < 4; ++k) {
            Eigen::Vector4d pp = p, pm = p;
            pp(k) += h; pm(k) -= h;
            Eigen::VectorXd rp, rm;
            hyperbolicResiduals(data.positions, data.hfd_values, pp, rp);
            hyperbolicResiduals(data.positions, data.hfd_values, pm, rm);
            J.col(k) = (rp - rm) / (2.0 * h);
        }

        Eigen::Matrix4d jtj = J.transpose() * J;
        Eigen::Matrix4d H = jtj;
        for (int k = 0; k < 4; ++k) H(k, k) += lambda * (jtj(k, k) + 1e-12);
        Eigen::Vector4d g = J.transpose() * residuals;
        // Use column-pivoting QR for robustness on the (symmetric) normal matrix.
        Eigen::Vector4d delta = H.colPivHouseholderQr().solve(-g);
        if (!delta.allFinite()) break;

        Eigen::Vector4d p_new = p + delta;
        p_new(0) = std::max(p_new(0), 1e-9);  // A > 0
        p_new(2) = std::max(p_new(2), 1e-9);  // C > 0

        Eigen::VectorXd r_new;
        hyperbolicResiduals(data.positions, data.hfd_values, p_new, r_new);
        double new_cost = r_new.squaredNorm();

        if (new_cost < cost) {
            p = p_new;
            residuals = r_new;
            cost = new_cost;
            lambda = std::max(lambda * 0.5, 1e-12);
            if (delta.norm() < 1e-8) break;  // converged
        } else {
            lambda *= 10.0;
            if (lambda > 1e12) break;  // give up — step not improving
        }
    }

    // Store the fitted coefficients and the model's true minimum at the vertex.
    data.vcurve_a = p(0);
    data.vcurve_b = p(1);
    data.vcurve_c = p(2);
    data.vcurve_d = p(3);
    data.focus_position = static_cast<int32_t>(std::round(p(1)));
    data.focus_hfd = p(0) * std::abs(p(2)) + p(3);

    // R-squared against the hyperbolic model.
    double sum_y = std::accumulate(data.hfd_values.begin(), data.hfd_values.end(), 0.0);
    double y_mean = sum_y / static_cast<double>(n);
    double ss_total = 0.0, ss_residual = 0.0;
    for (size_t i = 0; i < n; ++i) {
        double y = data.hfd_values[i];
        ss_total += (y - y_mean) * (y - y_mean);
        double model = p(0) * std::sqrt(std::pow(data.positions[i] - p(1), 2) + p(2) * p(2)) + p(3);
        ss_residual += (y - model) * (y - model);
    }
    data.r_squared = 1.0 - (ss_residual / (ss_total + 1e-15));

    return true;
}

} // namespace models
} // namespace astro_mount
