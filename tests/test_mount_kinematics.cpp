#include "core/mount_kinematics.h"

#include <array>
#include <cmath>

#include <gtest/gtest.h>

namespace astro_mount {
namespace core {
namespace test {

namespace {
constexpr double kPi = 3.14159265358979323846;
constexpr double kGear = 360.0;
constexpr double kLatitude = 52.0;
} // namespace

// ── EQUATORIAL: constant-Jacobian case ───────────────────────────────────────

TEST(MountKinematicsTest, EquatorialSiderealRate)
{
    MountKinematics kin(config::MountType::EQUATORIAL, kLatitude, kGear, kGear);
    const TrackingRates rates = kin.trackingRates(config::TrackingMode::SIDEREAL, 0.0, 0.0);
    EXPECT_DOUBLE_EQ(rates.axis1, 0.004178 * kGear);
    EXPECT_DOUBLE_EQ(rates.axis2, 0.0);
}

TEST(MountKinematicsTest, EquatorialSolarLunarOffRates)
{
    MountKinematics kin(config::MountType::EQUATORIAL, kLatitude, kGear, kGear);

    const TrackingRates solar = kin.trackingRates(config::TrackingMode::SOLAR, 0.0, 0.0);
    EXPECT_DOUBLE_EQ(solar.axis1, 0.004167 * kGear);
    EXPECT_DOUBLE_EQ(solar.axis2, 0.0);

    const TrackingRates lunar = kin.trackingRates(config::TrackingMode::LUNAR, 0.0, 0.0);
    EXPECT_DOUBLE_EQ(lunar.axis1, 0.004079 * kGear);
    EXPECT_DOUBLE_EQ(lunar.axis2, 0.0);

    const TrackingRates off = kin.trackingRates(config::TrackingMode::OFF, 0.0, 0.0);
    EXPECT_DOUBLE_EQ(off.axis1, 0.0);
    EXPECT_DOUBLE_EQ(off.axis2, 0.0);
}

TEST(MountKinematicsTest, EquatorialCustomRates)
{
    MountKinematics kin(config::MountType::EQUATORIAL, kLatitude, kGear, kGear);
    const TrackingRates rates = kin.trackingRates(config::TrackingMode::CUSTOM, 0.0, 0.0,
                                                  15.0 /* arcsec/s RA */,
                                                  30.0 /* arcsec/s Dec */);
    EXPECT_DOUBLE_EQ(rates.axis1, 15.0 / 3600.0 * kGear);
    EXPECT_DOUBLE_EQ(rates.axis2, 30.0 / 3600.0 * kGear);
}

TEST(MountKinematicsTest, EquatorialCustomZeroFallsBack)
{
    MountKinematics kin(config::MountType::EQUATORIAL, kLatitude, kGear, kGear);
    const TrackingRates rates = kin.trackingRates(config::TrackingMode::CUSTOM, 0.0, 0.0,
                                                  0.0, 0.0, 1.5 /* max_tracking_rate */);
    EXPECT_DOUBLE_EQ(rates.axis1, 1.5);
    EXPECT_DOUBLE_EQ(rates.axis2, 0.0);
}

// ── ALT_AZ / CASUAL: position-dependent case ─────────────────────────────────

TEST(MountKinematicsTest, AltAzSiderealAtEquatorEastHorizon)
{
    // latitude 0, altitude 0, azimuth 0: alt_rate = ω, az_rate = 0, so the
    // axis1 servo rate is ω·(180/π)·gear and axis2 is zero.
    MountKinematics kin(config::MountType::ALT_AZ, 0.0, kGear, kGear);
    const TrackingRates rates = kin.trackingRates(config::TrackingMode::SIDEREAL, 0.0, 0.0);

    const double omega = 7.2921150e-5;
    const double expected_axis1 = omega * (180.0 / kPi) * kGear;
    EXPECT_NEAR(rates.axis1, expected_axis1, 1e-9);
    EXPECT_NEAR(rates.axis2, 0.0, 1e-9);
}

TEST(MountKinematicsTest, CasualIdentityEqualsAltAz)
{
    const std::array<double, 4> identity{0.0, 0.0, 0.0, 1.0};
    MountKinematics altaz(config::MountType::ALT_AZ, kLatitude, kGear, kGear);
    MountKinematics casual(config::MountType::CASUAL, kLatitude, kGear, kGear, identity);

    const double positions[][2] = {
        {0.0, 0.0}, {30.0, 45.0}, {60.0, 120.0}, {-20.0, 200.0},
    };
    for (const auto& pos : positions) {
        const TrackingRates a = altaz.trackingRates(config::TrackingMode::SIDEREAL, pos[0], pos[1]);
        const TrackingRates c = casual.trackingRates(config::TrackingMode::SIDEREAL, pos[0], pos[1]);
        EXPECT_DOUBLE_EQ(c.axis1, a.axis1) << "axis1 @ " << pos[0] << ", " << pos[1];
        EXPECT_DOUBLE_EQ(c.axis2, a.axis2) << "axis2 @ " << pos[0] << ", " << pos[1];
    }
}

TEST(MountKinematicsTest, CasualRotatedQuaternionIsApplied)
{
    // 90° pitch about the East axis: the mount's altitude-like axis is tilted
    // away from the local vertical, so the projected tracking rates must differ
    // from the identity (ALT_AZ) case.  (A pure yaw about the vertical is
    // deliberately NOT used here: the sidereal velocity field is axisymmetric
    // about the vertical, so a yaw leaves the rates unchanged.)
    const double half = 0.5 * (90.0 * kPi / 180.0);
    const std::array<double, 4> pitch90{std::sin(half), 0.0, 0.0, std::cos(half)};

    MountKinematics altaz(config::MountType::ALT_AZ, kLatitude, kGear, kGear);
    MountKinematics casual(config::MountType::CASUAL, kLatitude, kGear, kGear, pitch90);

    const TrackingRates a = altaz.trackingRates(config::TrackingMode::SIDEREAL, 30.0, 45.0);
    const TrackingRates c = casual.trackingRates(config::TrackingMode::SIDEREAL, 30.0, 45.0);

    EXPECT_GT(std::abs(c.axis1 - a.axis1) + std::abs(c.axis2 - a.axis2), 1e-9);
}

TEST(MountKinematicsTest, SolarAndLunarScalePositionDependentRates)
{
    MountKinematics kin(config::MountType::ALT_AZ, kLatitude, kGear, kGear);

    const TrackingRates sidereal = kin.trackingRates(config::TrackingMode::SIDEREAL, 30.0, 45.0);
    const TrackingRates solar = kin.trackingRates(config::TrackingMode::SOLAR, 30.0, 45.0);
    const TrackingRates lunar = kin.trackingRates(config::TrackingMode::LUNAR, 30.0, 45.0);

    // SIDEREAL factor 1.0, SOLAR 0.9972, LUNAR 0.9760 — all from the same
    // position-dependent pipeline, so the ratios are exact.
    EXPECT_DOUBLE_EQ(solar.axis1, sidereal.axis1 * 0.9972);
    EXPECT_DOUBLE_EQ(solar.axis2, sidereal.axis2 * 0.9972);
    EXPECT_DOUBLE_EQ(lunar.axis1, sidereal.axis1 * 0.9760);
    EXPECT_DOUBLE_EQ(lunar.axis2, sidereal.axis2 * 0.9760);
}

TEST(MountKinematicsTest, OffYieldsZeroRatesForPositionDependentMounts)
{
    MountKinematics kin(config::MountType::ALT_AZ, kLatitude, kGear, kGear);
    const TrackingRates rates = kin.trackingRates(config::TrackingMode::OFF, 30.0, 45.0);
    EXPECT_DOUBLE_EQ(rates.axis1, 0.0);
    EXPECT_DOUBLE_EQ(rates.axis2, 0.0);
}

} // namespace test
} // namespace core
} // namespace astro_mount
