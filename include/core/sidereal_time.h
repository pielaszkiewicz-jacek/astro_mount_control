#pragma once

#include <cmath>

namespace astro_mount {
namespace core {

/**
 * @brief Greenwich Mean Sidereal Time [hours], normalized to [0, 24).
 *
 * Standard IAU 1982 polynomial.  Used by both the mount controller and the
 * INDI driver so the two components share a single, consistent sidereal-time
 * source.
 */
inline double calculateGMST(double jd) {
    const double jd2000 = jd - 2451545.0;
    double gmst = 18.697374558 + 24.06570982441908 * jd2000;
    gmst = std::fmod(gmst, 24.0);
    if (gmst < 0.0) {
        gmst += 24.0;
    }
    return gmst;
}

/**
 * @brief Local Sidereal Time [hours], normalized to [0, 24).
 * @param jd        Julian Date (UTC approximation of UT1 — see controller note)
 * @param longitude Observer longitude in degrees East.
 */
inline double calculateLST(double jd, double longitude) {
    double lst = calculateGMST(jd) + longitude / 15.0;
    lst = std::fmod(lst, 24.0);
    if (lst < 0.0) {
        lst += 24.0;
    }
    return lst;
}

} // namespace core
} // namespace astro_mount
