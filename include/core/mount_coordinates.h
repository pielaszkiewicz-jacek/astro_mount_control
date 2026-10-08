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
    // The same sky Dec is reachable at 'dec' and at '180° − dec' (opposite
    // pier side), each wrapped by any whole number of turns.  The previous
    // implementation only tried ±1 turn, which left the returned target up to
    // ~180° away when the physical Dec axis sat at a large multi-turn value
    // (e.g. −674° after a long session) — the drive then chased the distant
    // target (the "Dec axis spins / slew timeout" symptom).  Round each
    // pier-side equivalent to the nearest whole turn of the current position
    // instead, which is exact for any accumulated turn count.
    constexpr double kTurn = 360.0;
    const double other = 180.0 - dec;
    const double n1 = std::round((current_dec_tel - dec) / kTurn);
    const double n2 = std::round((current_dec_tel - other) / kTurn);
    const double c1 = dec + n1 * kTurn;
    const double c2 = other + n2 * kTurn;
    const double d1 = std::abs(c1 - current_dec_tel);
    const double d2 = std::abs(c2 - current_dec_tel);
    if (d1 < d2 - 1e-9) return c1;
    if (d2 < d1 - 1e-9) return c2;
    // Near-tie: prefer the candidate inside the astronomical range [-90°, 90°]
    // so the target never lands on the opposite pier side and trips the Dec
    // soft limit.
    const bool in1 = (c1 >= -90.0 && c1 <= 90.0);
    const bool in2 = (c2 >= -90.0 && c2 <= 90.0);
    if (in1 && !in2) return c1;
    if (in2 && !in1) return c2;
    return c1;
}

} // namespace core
} // namespace astro_mount
