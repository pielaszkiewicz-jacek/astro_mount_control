#pragma once

#include <cmath>

namespace astro_mount {
namespace core {

/**
 * @brief Fold a physical Dec axis angle into the astronomical range [-90°, 90°]
 *        using the pier-side convention.
 *
 * A telescope pointing at Dec = 120° (past the celestial pole) sees the same
 * sky position as Dec = 60° on the opposite pier side.  The readback path
 * (getStatus) folds the physical angle so the reported on-sky Dec is always
 * within [-90°, 90°].
 */
inline double foldDec(double dec_deg) {
    if (dec_deg > 90.0) {
        dec_deg = 180.0 - dec_deg;
    } else if (dec_deg < -90.0) {
        dec_deg = -180.0 - dec_deg;
    }
    return dec_deg;
}

/**
 * @brief Choose the physical Dec-axis target (telescope degrees) equivalent to
 *        the requested astronomical Dec that is nearest to the current physical
 *        Dec-axis position.
 *
 * The same sky Dec is reachable at 'dec' and at '180° - dec' (opposite pier
 * side), each wrapped by 360°.  Invariant: resolveDecTarget(foldDec(p), p) == p
 * for p in [-180°, 180°] — this is what makes "slew to current position" a
 * no-op on the Dec axis.
 */
inline double resolveDecTarget(double dec, double current_dec_tel) {
    constexpr double kEps = 1e-9;
    double best = dec;
    double best_dist = std::abs(dec - current_dec_tel);
    const double candidates[] = {
        dec, 180.0 - dec,
        dec - 360.0, dec + 360.0,
        180.0 - dec - 360.0, 180.0 - dec + 360.0
    };
    for (double c : candidates) {
        const double d = std::abs(c - current_dec_tel);
        const bool c_in_range = (c >= -90.0 && c <= 90.0);
        const bool best_in_range = (best >= -90.0 && best <= 90.0);
        if (d < best_dist - kEps) {
            best = c;
            best_dist = d;
        } else if (d <= best_dist + kEps && c_in_range && !best_in_range) {
            // Floating-point near-tie (e.g. current Dec on the 90° fold
            // boundary): prefer the candidate inside the astronomical range
            // [-90, 90] so the target never lands on the opposite pier side
            // and trips the Dec soft limit.
            best = c;
            best_dist = d;
        }
    }
    return best;
}

} // namespace core
} // namespace astro_mount
