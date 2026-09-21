#include "core/mount_coordinates.h"
#include <gtest/gtest.h>

namespace astro_mount {
namespace core {
namespace test {

// foldDec() must map any physical Dec angle into the astronomical [-90°, 90°].
TEST(MountCoordinatesTest, FoldDecKeepsAstronomicalRange)
{
    EXPECT_DOUBLE_EQ(foldDec(0.0), 0.0);
    EXPECT_DOUBLE_EQ(foldDec(45.0), 45.0);
    EXPECT_DOUBLE_EQ(foldDec(-45.0), -45.0);
    EXPECT_DOUBLE_EQ(foldDec(90.0), 90.0);
    EXPECT_DOUBLE_EQ(foldDec(-90.0), -90.0);
    EXPECT_DOUBLE_EQ(foldDec(120.0), 60.0);
    EXPECT_DOUBLE_EQ(foldDec(134.0), 46.0);
    EXPECT_DOUBLE_EQ(foldDec(-120.0), -60.0);
    EXPECT_DOUBLE_EQ(foldDec(-134.0), -46.0);
}

// Round-trip invariant: resolveDecTarget(foldDec(p), p) == p.
// This is what guarantees "slew to current position" is a no-op on the Dec
// axis regardless of pier side.
TEST(MountCoordinatesTest, ResolveDecTargetRoundTrip)
{
    for (double p = -180.0; p <= 180.0; p += 0.5) {
        const double folded = foldDec(p);
        const double resolved = resolveDecTarget(folded, p);
        EXPECT_NEAR(resolved, p, 1e-9) << "p=" << p;
    }
}

// Nearest-equivalent selection keeps the target on the same physical side.
TEST(MountCoordinatesTest, ResolveDecTargetNearestEquivalent)
{
    // Physical Dec at 90° (park): a request for 46° resolves to 46° (within
    // the astronomical range), not the opposite-side 134°.
    EXPECT_DOUBLE_EQ(resolveDecTarget(46.0, 90.0), 46.0);

    // Physical Dec at 134° (past the pole): the same request resolves to the
    // physical 134°, i.e. no movement.
    EXPECT_DOUBLE_EQ(resolveDecTarget(46.0, 134.0), 134.0);

    // Physical Dec at 0°: request for -46° resolves to -46°.
    EXPECT_DOUBLE_EQ(resolveDecTarget(-46.0, 0.0), -46.0);
}

// Floating-point tie at the 90° fold boundary must resolve to the in-range
// candidate, never the opposite-side (180 − dec) value.
TEST(MountCoordinatesTest, ResolveDecTargetFoldBoundaryTie)
{
    // 45.998 is not exactly representable in binary; at current Dec = 90° the
    // distances to 45.998 and to 180−45.998 = 134.002 differ by < 1 ULP.  The
    // result must stay inside [-90, 90].
    EXPECT_NEAR(resolveDecTarget(45.998, 90.0), 45.998, 1e-9);
    EXPECT_NEAR(resolveDecTarget(45.001, 90.0), 45.001, 1e-9);
}

} // namespace test
} // namespace core
} // namespace astro_mount
