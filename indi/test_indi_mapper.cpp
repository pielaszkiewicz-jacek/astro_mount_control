#include "IndiPropertyMapper.h"
#include <gtest/gtest.h>

// Tests for the INDI-side coordinate mapping (IndiPropertyMapper).  These
// verify the controller↔INDI boundary: the driver must pass JNow coordinates
// through unchanged and prefer the controller's corrected on-sky position.

namespace {

TEST(IndiPropertyMapperTest, ToGrpcCoordinatesPassesJNowThrough)
{
    IndiPropertyMapper mapper(52.0, 21.0, 100.0);
    const auto coords = mapper.toGrpcCoordinates(12.5, 45.0);

    EXPECT_DOUBLE_EQ(coords.ra(), 12.5);
    EXPECT_DOUBLE_EQ(coords.dec(), 45.0);
    EXPECT_FALSE(coords.apply_precession());
    EXPECT_FALSE(coords.apply_nutation());
    EXPECT_FALSE(coords.apply_refraction());
    EXPECT_DOUBLE_EQ(coords.epoch(), 0.0);
}

TEST(IndiPropertyMapperTest, ToIndiRaDecPrefersCorrectedPosition)
{
    IndiPropertyMapper mapper(52.0, 21.0, 100.0);

    astro_mount::ControllerState state;
    state.set_current_ra(12.5);
    state.set_current_dec(45.0);
    // These must be ignored when current_ra/current_dec are present.
    state.set_telescope_axis1(90.0);
    state.set_telescope_axis2(120.0);

    double ra = 0.0, dec = 0.0;
    ASSERT_TRUE(mapper.toIndiRaDec(state, 18.0, ra, dec));
    EXPECT_DOUBLE_EQ(ra, 12.5);
    EXPECT_DOUBLE_EQ(dec, 45.0);
}

TEST(IndiPropertyMapperTest, ToIndiRaDecFallsBackToTelescopeAxes)
{
    IndiPropertyMapper mapper(52.0, 21.0, 100.0);

    astro_mount::ControllerState state;
    // Both zero → the driver falls back to the raw telescope axes.
    state.set_current_ra(0.0);
    state.set_current_dec(0.0);
    state.set_telescope_axis1(90.0);   // HA = 90° = 6 h
    state.set_telescope_axis2(300.0);  // Dec = 300° → −60°

    double ra = 0.0, dec = 0.0;
    ASSERT_TRUE(mapper.toIndiRaDec(state, 18.0, ra, dec));
    // ra = LST − HA = 18 − 6 = 12
    EXPECT_NEAR(ra, 12.0, 1e-9);
    // dec = 300° − 360° = −60°
    EXPECT_NEAR(dec, -60.0, 1e-9);
}

} // namespace
