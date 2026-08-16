#include "models/field_rotation_model.h"
#include <cmath>
#include <algorithm>

namespace astro_mount { namespace models {

FieldRotationResult FieldRotationModel::calculateAltAz(double ha_hours, double dec_deg, double lat_deg) {
    // NUMERICAL STABILITY FIX:
    //  - The angle formula previously used tan(δ), which diverges as |δ| → 90°.
    //    Multiplying numerator and denominator by cos(δ) removes the tan() entirely:
    //      q = atan2(sin(H), cos(H)·sin(φ) − tan(δ)·cos(φ))
    //        = atan2(sin(H)·cos(δ), cos(H)·sin(φ)·cos(δ) − sin(δ)·cos(φ))
    //    which is well defined for all δ (including δ = ±90°).
    //  - The rate denominator 1 − sin²(φ)·sin²(δ) → 0 when φ = δ = ±90°; clamp it to
    //    a small epsilon to avoid division-by-zero / Inf.
    if (!std::isfinite(ha_hours) || !std::isfinite(dec_deg) || !std::isfinite(lat_deg)) {
        return FieldRotationResult{};
    }

    double ha_rad = ha_hours * 15.0 * M_PI / 180.0;
    double dec_rad = dec_deg * M_PI / 180.0;
    double lat_rad = lat_deg * M_PI / 180.0;

    double sin_ha = std::sin(ha_rad);
    double cos_ha = std::cos(ha_rad);
    double sin_dec = std::sin(dec_rad);
    double cos_dec = std::cos(dec_rad);
    double sin_lat = std::sin(lat_rad);
    double cos_lat = std::cos(lat_rad);

    // Parallactic/field-rotation angle without tan(δ) singularity.
    double angle = std::atan2(sin_ha * cos_dec,
                              cos_ha * sin_lat * cos_dec - sin_dec * cos_lat);

    // Rate with guarded denominator.
    double denom = 1.0 - sin_lat * sin_lat * sin_dec * sin_dec;
    const double MIN_DENOM = 1e-12;
    if (std::abs(denom) < MIN_DENOM) denom = std::copysign(MIN_DENOM, denom);
    double rate = 15.041 * cos_lat * cos_dec * cos_ha / denom;

    FieldRotationResult r;
    r.angle_deg = angle * 180.0 / M_PI;
    r.rate_arcsec_s = rate;
    r.predicted_angle_10min = r.angle_deg + r.rate_arcsec_s * 600.0 / 3600.0;
    return r;
}

namespace {
// Rotate a vector by a quaternion [x, y, z, w] (w = scalar part):
//   v' = v + 2·w·(q×v) + 2·(q×(q×v))
std::array<double,3> rotateVec(const std::array<double,3>& v,
                               const std::array<double,4>& q) {
    double qx = q[0], qy = q[1], qz = q[2], qw = q[3];
    double vx = v[0], vy = v[1], vz = v[2];
    double c1x = qy * vz - qz * vy;
    double c1y = qz * vx - qx * vz;
    double c1z = qx * vy - qy * vx;
    double c2x = qy * c1z - qz * c1y;
    double c2y = qz * c1x - qx * c1z;
    double c2z = qx * c1y - qy * c1x;
    return {vx + 2.0 * qw * c1x + 2.0 * c2x,
            vy + 2.0 * qw * c1y + 2.0 * c2y,
            vz + 2.0 * qw * c1z + 2.0 * c2z};
}

// Project a vector onto the plane perpendicular to `axis` (unit) and normalize.
std::array<double,3> projectPerp(const std::array<double,3>& v,
                                 const std::array<double,3>& axis) {
    double dot = v[0]*axis[0] + v[1]*axis[1] + v[2]*axis[2];
    double px = v[0] - dot * axis[0];
    double py = v[1] - dot * axis[1];
    double pz = v[2] - dot * axis[2];
    double n = std::sqrt(px*px + py*py + pz*pz);
    if (n < 1e-12) return {0.0, 0.0, 0.0};
    return {px/n, py/n, pz/n};
}
} // anonymous namespace

FieldRotationResult FieldRotationModel::calculateCasual(double axis1_deg, double axis2_deg,
    const std::array<double,4>& q, double ha_hours, double dec_deg, double lat_deg) {
    // CASUAL: an arbitrarily-oriented mount. The field rotation on the sensor is
    // the sum of two contributions:
    //   1. The parallactic angle at the target (same as an alt-az mount), and
    //   2. The mount's own "roll" about the pointing axis — the angle between the
    //      mount's axis1 ("up") direction and the local zenith, both projected
    //      onto the image plane (the plane perpendicular to the pointing vector).
    //
    // The quaternion Q maps the horizontal (ENU) frame onto the mount frame, so
    // the inverse quaternion transforms the mount-frame pointing back to the
    // true horizontal frame where the parallactic angle is well defined.

    // NaN/Inf guard on all inputs.
    if (!std::isfinite(axis1_deg) || !std::isfinite(axis2_deg) ||
        !std::isfinite(ha_hours) || !std::isfinite(dec_deg) || !std::isfinite(lat_deg) ||
        !std::isfinite(q[0]) || !std::isfinite(q[1]) ||
        !std::isfinite(q[2]) || !std::isfinite(q[3])) {
        return FieldRotationResult{};
    }

    // Normalise quaternion; degenerate -> fall back to alt-az model.
    double norm = std::sqrt(q[0]*q[0] + q[1]*q[1] + q[2]*q[2] + q[3]*q[3]);
    if (norm < 1e-12) {
        return calculateAltAz(ha_hours, dec_deg, lat_deg);
    }
    std::array<double,4> qn = {q[0]/norm, q[1]/norm, q[2]/norm, q[3]/norm};

    // Mount-frame pointing unit vector: axis1 = altitude-like, axis2 = azimuth-like.
    double a1 = axis1_deg * M_PI / 180.0;
    double a2 = axis2_deg * M_PI / 180.0;
    std::array<double,3> mount_pointing = {
        std::cos(a1) * std::cos(a2),
        std::cos(a1) * std::sin(a2),
        std::sin(a1)
    };

    // Inverse quaternion: mount frame -> true horizontal (ENU).
    std::array<double,4> q_inv = {-qn[0], -qn[1], -qn[2], qn[3]};
    std::array<double,3> horiz = rotateVec(mount_pointing, q_inv);
    double hnorm = std::sqrt(horiz[0]*horiz[0] + horiz[1]*horiz[1] + horiz[2]*horiz[2]);
    if (hnorm < 1e-12) {
        return calculateAltAz(ha_hours, dec_deg, lat_deg);
    }
    horiz = {horiz[0]/hnorm, horiz[1]/hnorm, horiz[2]/hnorm};

    // 1. Parallactic (field-rotation) angle at the target.
    double ha_rad = ha_hours * 15.0 * M_PI / 180.0;
    double dec_rad = dec_deg * M_PI / 180.0;
    double lat_rad = lat_deg * M_PI / 180.0;
    double sin_ha = std::sin(ha_rad);
    double cos_ha = std::cos(ha_rad);
    double sin_dec = std::sin(dec_rad);
    double cos_dec = std::cos(dec_rad);
    double sin_lat = std::sin(lat_rad);
    double cos_lat = std::cos(lat_rad);
    double angle = std::atan2(sin_ha * cos_dec,
                              cos_ha * sin_lat * cos_dec - sin_dec * cos_lat);
    double parallactic_deg = angle * 180.0 / M_PI;

    // 2. Mount roll about the pointing axis:
    //    angle between the mount's axis1 direction (transformed to horizontal)
    //    and the local zenith, both projected onto the image plane.
    std::array<double,3> mount_up = {0.0, 0.0, 1.0};           // axis1 of mount frame
    std::array<double,3> mount_up_h = rotateVec(mount_up, q_inv); // axis1 in horizontal
    std::array<double,3> zenith = {0.0, 0.0, 1.0};
    std::array<double,3> mp = projectPerp(mount_up_h, horiz);
    std::array<double,3> zp = projectPerp(zenith, horiz);
    double mount_roll_deg = 0.0;
    if (mp[0]*mp[0] + mp[1]*mp[1] + mp[2]*mp[2] > 1e-20 &&
        zp[0]*zp[0] + zp[1]*zp[1] + zp[2]*zp[2] > 1e-20) {
        double dot = mp[0]*zp[0] + mp[1]*zp[1] + mp[2]*zp[2];
        mount_roll_deg = std::acos(std::clamp(dot, -1.0, 1.0)) * 180.0 / M_PI;
        // Sign: right-handed rotation about the pointing axis.
        double cross_x = mp[1]*zp[2] - mp[2]*zp[1];
        double cross_y = mp[2]*zp[0] - mp[0]*zp[2];
        double cross_z = mp[0]*zp[1] - mp[1]*zp[0];
        double hand = cross_x*horiz[0] + cross_y*horiz[1] + cross_z*horiz[2];
        if (hand < 0.0) mount_roll_deg = -mount_roll_deg;
    }

    double total_angle = parallactic_deg + mount_roll_deg;

    // Field-rotation rate: alt-az rate scaled by the parallactic term, with a
    // guarded denominator (same protection as calculateAltAz). The mount-roll
    // term is quasi-static for a fixed mount, so it contributes no rate.
    double denom = 1.0 - sin_lat * sin_lat * sin_dec * sin_dec;
    const double MIN_DENOM = 1e-12;
    if (std::abs(denom) < MIN_DENOM) denom = std::copysign(MIN_DENOM, denom);
    double rate = 15.041 * cos_lat * cos_dec * cos_ha / denom;

    FieldRotationResult r;
    r.angle_deg = total_angle;
    r.rate_arcsec_s = rate;
    r.predicted_angle_10min = r.angle_deg + r.rate_arcsec_s * 600.0 / 3600.0;
    return r;
}

FieldRotationResult FieldRotationModel::calculateEquatorial() {
    return FieldRotationResult{};
}

double FieldRotationModel::getFieldRotationRate(double ha_hours, double dec_deg, double lat_deg) {
    return calculateAltAz(ha_hours, dec_deg, lat_deg).rate_arcsec_s;
}

}} // namespace astro_mount::models
