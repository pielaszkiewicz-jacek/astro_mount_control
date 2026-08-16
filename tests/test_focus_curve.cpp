#include "models/focus_curve.h"
#include <gtest/gtest.h>
#include <cmath>

using namespace astro_mount::models;

namespace {

// Ground-truth coefficients for the synthetic hyperbolic V-curve.
constexpr double HYP_A = 0.02;   // slope
constexpr double HYP_B = 50.0;   // best-focus position [steps]
constexpr double HYP_C = 150.0;  // knee width
constexpr double HYP_D = 2.0;    // asymptotic floor

double hyperbolicHfd(double p) {
    return HYP_A * std::sqrt(std::pow(p - HYP_B, 2) + HYP_C * HYP_C) + HYP_D;
}

} // namespace

TEST(FocusCurveTest, ParabolicFitFindsVertex) {
    FocusCurve curve;
    // HFD = 0.001*(p-200)^2 + 1.5 → vertex at 200, min HFD 1.5
    const double a = 0.001, b = 200.0, c = 1.5;
    for (int p = 0; p <= 400; p += 50) {
        double hfd = a * (p - b) * (p - b) + c;
        curve.addMeasurement(p, hfd);
    }
    auto data = curve.fit("parabolic");
    EXPECT_EQ(data.algorithm, "parabolic");
    EXPECT_NEAR(data.focus_position, b, 2);
    EXPECT_NEAR(data.focus_hfd, c, 0.05);
    EXPECT_GT(data.r_squared, 0.999);
}

TEST(FocusCurveTest, HyperbolicFitRecoversTrueParameters) {
    // R6: the hyperbolic fit must actually fit — not fall back to a parabola.
    FocusCurve curve;
    for (int p = -400; p <= 400; p += 50) {
        curve.addMeasurement(p, hyperbolicHfd(p));
    }
    auto data = curve.fit("hyperbolic");
    EXPECT_EQ(data.algorithm, "hyperbolic");
    EXPECT_NEAR(data.focus_position, HYP_B, 5);
    EXPECT_NEAR(data.focus_hfd, HYP_A * HYP_C + HYP_D, 0.1);
    EXPECT_GT(data.r_squared, 0.999);

    // evaluateAt must use the hyperbolic model after a hyperbolic fit.
    double est = curve.evaluateAt(-250);
    EXPECT_NEAR(est, hyperbolicHfd(-250), 0.5);
}

TEST(FocusCurveTest, HyperbolicFitHandlesNoise) {
    FocusCurve curve;
    unsigned seed = 42;
    for (int p = -400; p <= 400; p += 25) {
        double hfd = hyperbolicHfd(p) + 0.05 * std::sin(static_cast<double>(p) + seed);
        curve.addMeasurement(p, hfd);
    }
    auto data = curve.fit("hyperbolic");
    EXPECT_EQ(data.algorithm, "hyperbolic");
    EXPECT_NEAR(data.focus_position, HYP_B, 15);
    EXPECT_GT(data.r_squared, 0.99);
}

TEST(FocusCurveTest, HyperbolicFitFallsBackWithFewPoints) {
    // 3 points cannot fit the 4-parameter hyperbola → parabolic fallback.
    FocusCurve curve;
    curve.addMeasurement(0, 4.0);
    curve.addMeasurement(100, 1.0);
    curve.addMeasurement(200, 4.0);
    auto data = curve.fit("hyperbolic");
    // fit() marks the data as fitted regardless; the coefficients must be finite
    // and the vertex must land near the measured minimum (parabolic fallback).
    EXPECT_TRUE(std::isfinite(data.focus_position));
    EXPECT_NEAR(data.focus_position, 100, 10);
}

TEST(FocusCurveTest, GetFocusDepth) {
    FocusCurve curve;
    const double a = 0.001, b = 200.0, c = 1.5;
    for (int p = 0; p <= 400; p += 25) {
        curve.addMeasurement(p, a * (p - b) * (p - b) + c);
    }
    curve.fit("parabolic");
    // HFD <= 1.1*c → 0.001*(p-200)^2 <= 0.15 → |p-200| <= ~12.2
    int32_t depth = curve.getFocusDepth();
    EXPECT_GT(depth, 10);
    EXPECT_LT(depth, 40);
}
