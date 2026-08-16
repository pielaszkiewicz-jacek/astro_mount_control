#include <gtest/gtest.h>
#include "models/field_rotation_model.h"
#include "controllers/st4_calibration.h"
#include "controllers/st4_guider.h"
#include <array>
#include <cmath>
#include <limits>
#include <memory>
#include <vector>

using namespace astro_mount::models;
using namespace astro_mount::controllers;
// The test references the ST4 HAL types via the short alias `hal::`.
namespace hal = astro_mount::hal;

// ============================================================================
// FieldRotationModel — calculateCasual (previously a no-op stub)
// ============================================================================

TEST(FieldRotationCasualTest, IdentityQuaternionMatchesAltAzRate) {
    // With an identity quaternion the CASUAL model reduces to the alt-az model
    // (mount frame == horizontal frame). The rate must match.
    std::array<double,4> id = {0.0, 0.0, 0.0, 1.0};
    double ha = 2.0, dec = 45.0, lat = 52.0;
    auto casual = FieldRotationModel::calculateCasual(30.0, 60.0, id, ha, dec, lat);
    auto altaz  = FieldRotationModel::calculateAltAz(ha, dec, lat);

    EXPECT_NEAR(casual.rate_arcsec_s, altaz.rate_arcsec_s, 1e-9);
    EXPECT_TRUE(std::isfinite(casual.angle_deg));
    EXPECT_TRUE(std::isfinite(casual.rate_arcsec_s));
    EXPECT_TRUE(std::isfinite(casual.predicted_angle_10min));
}

TEST(FieldRotationCasualTest, NonIdentityQuaternionChangesAngle) {
    // A non-identity mount orientation must change the total field-rotation
    // angle relative to the identity case, while the rate (parallactic) is
    // unchanged (the mount-roll term is quasi-static).
    //
    // NOTE: a yaw of the mount about the vertical/zenith axis does NOT roll the
    // image — the altitude axis stays vertical (it is the rotation axis), so the
    // mount-roll term is unchanged (physically correct: yawing a camera about
    // vertical does not rotate the image on the sensor). To exercise the roll
    // term we use a 45° rotation of the mount about its OWN pointing axis,
    // which directly rotates the image on the sensor by 45°.
    std::array<double,4> id = {0.0, 0.0, 0.0, 1.0};
    const double axis1 = 30.0, axis2 = 60.0;
    // Unit pointing vector for axis1=30°, axis2=60°.
    std::array<double,3> u = {
        std::cos(axis1 * M_PI / 180.0) * std::cos(axis2 * M_PI / 180.0),
        std::cos(axis1 * M_PI / 180.0) * std::sin(axis2 * M_PI / 180.0),
        std::sin(axis1 * M_PI / 180.0),
    };
    const double half = 22.5 * M_PI / 180.0;  // 45° roll about the pointing axis
    std::array<double,4> qr = {
        std::sin(half) * u[0],
        std::sin(half) * u[1],
        std::sin(half) * u[2],
        std::cos(half),
    };
    double ha = 2.0, dec = 45.0, lat = 52.0;
    auto casual_id = FieldRotationModel::calculateCasual(axis1, axis2, id, ha, dec, lat);
    auto casual_qr = FieldRotationModel::calculateCasual(axis1, axis2, qr, ha, dec, lat);

    // The 45° roll about the pointing axis must appear in the total angle.
    EXPECT_NE(casual_qr.angle_deg, casual_id.angle_deg);
    EXPECT_GT(std::abs(casual_qr.angle_deg - casual_id.angle_deg), 10.0);
    EXPECT_NEAR(std::abs(casual_qr.angle_deg - casual_id.angle_deg), 45.0, 1e-6);
    // The rate (parallactic) is unchanged by a static mount roll.
    EXPECT_NEAR(casual_qr.rate_arcsec_s, casual_id.rate_arcsec_s, 1e-6);
}

TEST(FieldRotationCasualTest, GuardsNonFiniteInputs) {
    std::array<double,4> id = {0.0, 0.0, 0.0, 1.0};
    auto r = FieldRotationModel::calculateCasual(
        std::numeric_limits<double>::quiet_NaN(), 60.0, id, 2.0, 45.0, 52.0);
    EXPECT_EQ(r.angle_deg, 0.0);
    EXPECT_EQ(r.rate_arcsec_s, 0.0);
}

TEST(FieldRotationCasualTest, DegenerateQuaternionFallsBackToAltAz) {
    std::array<double,4> zero = {0.0, 0.0, 0.0, 0.0};
    double ha = 2.0, dec = 45.0, lat = 52.0;
    auto casual = FieldRotationModel::calculateCasual(30.0, 60.0, zero, ha, dec, lat);
    auto altaz  = FieldRotationModel::calculateAltAz(ha, dec, lat);
    EXPECT_NEAR(casual.angle_deg, altaz.angle_deg, 1e-9);
    EXPECT_NEAR(casual.rate_arcsec_s, altaz.rate_arcsec_s, 1e-9);
}

TEST(FieldRotationCasualTest, EquatorialIsZero) {
    auto eq = FieldRotationModel::calculateEquatorial();
    EXPECT_DOUBLE_EQ(eq.angle_deg, 0.0);
    EXPECT_DOUBLE_EQ(eq.rate_arcsec_s, 0.0);
}

// ============================================================================
// St4Calibration — theoretical & measured
// ============================================================================

TEST(St4CalibrationTest, TheoreticalSiderealRate) {
    auto p = St4Calibration::theoretical();
    EXPECT_TRUE(p.calibrated);
    EXPECT_EQ(p.method, "theoretical");
    // 15.041 arcsec/s / 1000 = 0.015041 arcsec/ms
    EXPECT_NEAR(p.ra_arcsec_per_ms, 15.041 / 1000.0, 1e-9);
    EXPECT_DOUBLE_EQ(p.dec_arcsec_per_ms, 0.0);
}

// Mock ST4 hardware for calibration testing.
namespace {
class MockSt4 : public hal::St4Control {
public:
    std::string name() const override { return "mock"; }
    bool initialize() override { return true; }
    bool pulse(hal::St4Direction, int) override { return true; }
    bool stop() override { return true; }
    bool isConnected() const override { return true; }
    void shutdown() override {}
};
}

TEST(St4CalibrationTest, MeasuredSlopeFit) {
    MockSt4 hal;
    // Probe returns 0.02 arcsec per ms of pulse duration (linear response).
    auto probe = [](hal::St4Direction, int pulse_ms) -> double {
        return 0.02 * pulse_ms;
    };
    St4Calibration cal;
    auto p = cal.calibrate(hal, 500, 4, 50, probe);
    EXPECT_TRUE(p.calibrated);
    EXPECT_EQ(p.method, "measured");
    // slope = 0.02 arcsec/ms (both RA and Dec share the same probe here).
    EXPECT_NEAR(p.ra_arcsec_per_ms, 0.02, 1e-6);
    EXPECT_NEAR(p.dec_arcsec_per_ms, 0.02, 1e-6);
}

TEST(St4CalibrationTest, NoProbeFallsBackToTheoretical) {
    MockSt4 hal;
    St4Calibration cal;
    auto p = cal.calibrate(hal, 500, 4, 50, nullptr);
    EXPECT_TRUE(p.calibrated);
    EXPECT_EQ(p.method, "theoretical");
}

// ============================================================================
// St4Guider — calibration & pulse conversion
// ============================================================================

TEST(St4GuiderTest, TheoreticalCalibrationAndPulseConversion) {
    auto hal = std::make_unique<MockSt4>();
    St4Guider guider(std::move(hal));

    EXPECT_TRUE(guider.calibrate());
    EXPECT_TRUE(guider.getStats().calibrated);

    auto cal = guider.getCalibration();
    EXPECT_EQ(cal.method, "theoretical");

    // 15 arcsec / (15.041/1000 arcsec/ms) ≈ 997 ms → clamped pulse.
    int ms = guider.correctionToPulseMs(15.0, /*is_ra=*/true);
    EXPECT_GE(ms, 10);
    EXPECT_LE(ms, 3000);
}

TEST(St4GuiderTest, MeasuredCalibrationConversion) {
    auto hal = std::make_unique<MockSt4>();
    St4Guider guider(std::move(hal));
    auto probe = [](hal::St4Direction, int pulse_ms) -> double {
        return 0.02 * pulse_ms;  // 0.02 arcsec/ms
    };
    EXPECT_TRUE(guider.calibrate(probe, 500, 4, 50));
    auto cal = guider.getCalibration();
    EXPECT_EQ(cal.method, "measured");
    EXPECT_NEAR(cal.ra_arcsec_per_ms, 0.02, 1e-6);

    // 2 arcsec / 0.02 = 100 ms.
    EXPECT_EQ(guider.correctionToPulseMs(2.0, /*is_ra=*/true), 100);
}

TEST(St4GuiderTest, UncalibratedReturnsZeroPulse) {
    auto hal = std::make_unique<MockSt4>();
    St4Guider guider(std::move(hal));
    EXPECT_EQ(guider.correctionToPulseMs(10.0, true), 0);
    EXPECT_FALSE(guider.getStats().calibrated);
}

TEST(St4GuiderTest, PulseStatsCounted) {
    auto hal = std::make_unique<MockSt4>();
    St4Guider guider(std::move(hal));
    EXPECT_TRUE(guider.pulse(hal::St4Direction::EAST, 100));
    auto stats = guider.getStats();
    EXPECT_EQ(stats.pulses_sent, 1);
    EXPECT_EQ(stats.pulses_failed, 0);
}

// ============================================================================
// St4Guider — PHD2 event stream → guiding loop integration
// ============================================================================

namespace {
// ST4 mock that records the pulses it receives.
class RecordingSt4 : public hal::St4Control {
public:
    struct Pulse { hal::St4Direction dir; int ms; };
    std::vector<Pulse> pulses;
    std::string name() const override { return "recording"; }
    bool initialize() override { return true; }
    bool pulse(hal::St4Direction dir, int ms) override {
        pulses.push_back({dir, ms});
        return true;
    }
    bool stop() override { return true; }
    bool isConnected() const override { return true; }
    void shutdown() override {}
};
}

TEST(St4GuiderTest, Phd2GuideStepDrivesPulses) {
    auto hal = std::make_unique<RecordingSt4>();
    RecordingSt4* raw = hal.get();
    St4Guider guider(std::move(hal));

    // Measured calibration 0.02 arcsec/ms on both axes.
    auto probe = [](hal::St4Direction, int pulse_ms) -> double {
        return 0.02 * pulse_ms;
    };
    EXPECT_TRUE(guider.calibrate(probe, 500, 4, 50));
    raw->pulses.clear();  // drop the calibration pulses
    guider.setGuiding(true);

    // GuideStep: +20 arcsec RA (star East of lock → pulse West),
    //           -40 arcsec Dec (star South of lock → pulse North).
    guider.handlePhd2Event(R"({"event":"GuideStep","RADistance":20.0,"DecDistance":-40.0})");

    auto stats = guider.getStats();
    EXPECT_EQ(stats.pulses_sent, 2);
    EXPECT_EQ(raw->pulses.size(), 2u);

    // RA: 20 / 0.02 = 1000 ms, WEST. Dec: 40 / 0.02 = 2000 ms, NORTH.
    EXPECT_EQ(raw->pulses[0].dir, hal::St4Direction::WEST);
    EXPECT_EQ(raw->pulses[0].ms, 1000);
    EXPECT_EQ(raw->pulses[1].dir, hal::St4Direction::NORTH);
    EXPECT_EQ(raw->pulses[1].ms, 2000);

    // Corrections + rolling RMS exposed through stats.
    EXPECT_DOUBLE_EQ(stats.ra_correction, 20.0);
    EXPECT_DOUBLE_EQ(stats.dec_correction, -40.0);
    EXPECT_GT(stats.rms_ra, 0.0);
    EXPECT_GT(stats.rms_dec, 0.0);
}

TEST(St4GuiderTest, GuideStepIgnoredWhenNotGuiding) {
    auto hal = std::make_unique<RecordingSt4>();
    RecordingSt4* raw = hal.get();
    St4Guider guider(std::move(hal));
    EXPECT_TRUE(guider.calibrate());
    // Not guiding → GuideStep must NOT produce pulses.
    guider.handlePhd2Event(R"({"event":"GuideStep","RADistance":20.0,"DecDistance":-40.0})");
    EXPECT_TRUE(raw->pulses.empty());
    EXPECT_EQ(guider.getStats().pulses_sent, 0);
}

TEST(St4GuiderTest, GuideStepDeadZoneMinPulse) {
    auto hal = std::make_unique<RecordingSt4>();
    RecordingSt4* raw = hal.get();
    St4Guider guider(std::move(hal));
    auto probe = [](hal::St4Direction, int pulse_ms) -> double {
        return 0.02 * pulse_ms;
    };
    EXPECT_TRUE(guider.calibrate(probe, 500, 4, 50));
    raw->pulses.clear();  // drop the calibration pulses
    guider.setGuideParams(1.0, false, false, 100, 3000);  // min pulse 100 ms
    guider.setGuiding(true);

    // 0.5 arcsec / 0.02 = 25 ms < min 100 → dead zone → no pulse.
    guider.handlePhd2Event(R"({"event":"GuideStep","RADistance":0.5,"DecDistance":0.0})");
    EXPECT_TRUE(raw->pulses.empty());

    // 10 arcsec / 0.02 = 500 ms ≥ min → pulse, clamped to [100, 3000].
    guider.handlePhd2Event(R"({"event":"GuideStep","RADistance":10.0,"DecDistance":0.0})");
    ASSERT_EQ(raw->pulses.size(), 1u);
    EXPECT_EQ(raw->pulses[0].ms, 500);
}

TEST(St4GuiderTest, InvertRASwitchesDirection) {
    auto hal = std::make_unique<RecordingSt4>();
    RecordingSt4* raw = hal.get();
    St4Guider guider(std::move(hal));
    auto probe = [](hal::St4Direction, int pulse_ms) -> double {
        return 0.02 * pulse_ms;
    };
    EXPECT_TRUE(guider.calibrate(probe, 500, 4, 50));
    raw->pulses.clear();  // drop the calibration pulses
    guider.setGuideParams(1.0, /*invert_ra=*/true, false, 10, 3000);
    guider.setGuiding(true);

    // With invert_ra, a positive RA error now pulses EAST instead of WEST.
    guider.handlePhd2Event(R"({"event":"GuideStep","RADistance":20.0,"DecDistance":0.0})");
    ASSERT_EQ(raw->pulses.size(), 1u);
    EXPECT_EQ(raw->pulses[0].dir, hal::St4Direction::EAST);
}

TEST(St4GuiderTest, Phd2StateEventsSetGuiding) {
    auto hal = std::make_unique<RecordingSt4>();
    St4Guider guider(std::move(hal));

    guider.handlePhd2Event(R"({"event":"StartGuiding"})");
    EXPECT_TRUE(guider.isGuiding());
    guider.handlePhd2Event(R"({"event":"GuideStopped"})");
    EXPECT_FALSE(guider.isGuiding());
    guider.handlePhd2Event(R"({"event":"AppState","State":"Guiding"})");
    EXPECT_TRUE(guider.isGuiding());
    guider.handlePhd2Event(R"({"event":"StarLost"})");
    EXPECT_FALSE(guider.isGuiding());
}

TEST(St4GuiderTest, CalibrationCompleteIngested) {
    auto hal = std::make_unique<RecordingSt4>();
    St4Guider guider(std::move(hal));

    guider.handlePhd2Event(R"({"event":"CalibrationComplete","calibrated":true,)"
                            R"("ra_arcsec_per_ms":0.015,"dec_arcsec_per_ms":0.008})");
    auto cal = guider.getCalibration();
    EXPECT_TRUE(cal.calibrated);
    EXPECT_EQ(cal.method, "phd2");
    EXPECT_NEAR(cal.ra_arcsec_per_ms, 0.015, 1e-9);
    EXPECT_NEAR(cal.dec_arcsec_per_ms, 0.008, 1e-9);
    EXPECT_TRUE(guider.getStats().calibrated);
}
