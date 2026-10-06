#ifndef MOUNT_KINEMATICS_H
#define MOUNT_KINEMATICS_H

#include <array>
#include <cmath>
#include <utility>

#include "config/mount_config.h"
#include "config/tracking_config.h"
#include "core/astronomical_calculations.h"

namespace astro_mount {
namespace core {

/**
 * @brief Axis pair of tracking rates in servo degrees/second.
 *
 * axis1/axis2 are already multiplied by the per-axis gear ratio, i.e. they are
 * the values the HAL motor layer expects (see docs/pl/wyznaczanie_predkosci_sledzenia.md §1).
 */
struct TrackingRates {
    double axis1{0.0};
    double axis2{0.0};
};

/**
 * @brief Unified mount kinematics — computes tracking rates for every mount type
 *        from a single entry point.
 *
 * EQUATORIAL is the constant-Jacobian special case (ideal sidereal rate in HA,
 * zero in Dec, plus the CUSTOM user rates). ALT_AZ and CASUAL share the same
 * position-dependent sidereal kinematics modulo the ENU→mount rotation
 * quaternion Q (Q is the identity for ALT_AZ).
 *
 * See docs/pl/wyznaczanie_predkosci_sledzenia.md and
 * docs/pl/analiza_typow_montazu.md.
 */
class MountKinematics {
public:
    /**
     * @param mount_type   EQUATORIAL / ALT_AZ / CASUAL (UNKNOWN falls back to
     *                     the ALT_AZ identity-rotation path, matching the
     *                     legacy controller behaviour).
     * @param latitude_deg Observer latitude [degrees, positive north].
     * @param gear_ha      HA/axis1 gear ratio (motor:output).
     * @param gear_dec     Dec/axis2 gear ratio (motor:output).
     * @param quaternion   ENU→mount rotation [qx, qy, qz, qw]; only used by
     *                     CASUAL. Identity by default.
     */
    MountKinematics(config::MountType mount_type, double latitude_deg,
                    double gear_ha, double gear_dec,
                    const std::array<double, 4>& quaternion = {0.0, 0.0, 0.0, 1.0})
        : mount_type_(mount_type),
          latitude_deg_(latitude_deg),
          gear_ha_(gear_ha > 0.0 ? gear_ha : 1.0),
          gear_dec_(gear_dec > 0.0 ? gear_dec : 1.0),
          quaternion_(quaternion) {}

    /**
     * @brief Compute tracking rates for both axes [servo deg/s].
     *
     * @param mode                      Tracking mode.
     * @param axis1_tel                 Current axis1 position [telescope deg].
     * @param axis2_tel                 Current axis2 position [telescope deg].
     * @param custom_rate_ra_arcsec_s   CUSTOM RA rate [arcsec/s], EQUATORIAL only.
     * @param custom_rate_dec_arcsec_s  CUSTOM Dec rate [arcsec/s], EQUATORIAL only.
     * @param max_tracking_rate         Fallback servo rate for CUSTOM when the RA
     *                                  rate is zero (EQUATORIAL only).
     * @return TrackingRates in servo deg/s.
     */
    TrackingRates trackingRates(config::TrackingMode mode,
                                double axis1_tel, double axis2_tel,
                                double custom_rate_ra_arcsec_s = 0.0,
                                double custom_rate_dec_arcsec_s = 0.0,
                                double max_tracking_rate = 1.0) const {
        if (mount_type_ == config::MountType::EQUATORIAL) {
            return equatorialRates(mode, custom_rate_ra_arcsec_s,
                                   custom_rate_dec_arcsec_s, max_tracking_rate);
        }
        return mountFrameRates(mode, axis1_tel, axis2_tel);
    }

private:
    static double trackingModeFactor(config::TrackingMode mode) {
        switch (mode) {
            case config::TrackingMode::SIDEREAL: return 1.0;
            case config::TrackingMode::SOLAR:    return 0.9972;
            case config::TrackingMode::LUNAR:    return 0.9760;
            case config::TrackingMode::CUSTOM:   return 1.0;
            case config::TrackingMode::OFF:      return 0.0;
        }
        return 1.0;
    }

    // EQUATORIAL: constant (ideal) rates.  Numerically identical to the legacy
    // startTracking() switch statement.
    TrackingRates equatorialRates(config::TrackingMode mode,
                                  double custom_rate_ra_arcsec_s,
                                  double custom_rate_dec_arcsec_s,
                                  double max_tracking_rate) const {
        TrackingRates out;
        switch (mode) {
            case config::TrackingMode::SIDEREAL:
                out.axis1 = 0.004178 * gear_ha_;
                out.axis2 = 0.0;
                break;
            case config::TrackingMode::SOLAR:
                out.axis1 = 0.004167 * gear_ha_;
                out.axis2 = 0.0;
                break;
            case config::TrackingMode::LUNAR:
                out.axis1 = 0.004079 * gear_ha_;
                out.axis2 = 0.0;
                break;
            case config::TrackingMode::CUSTOM:
                if (custom_rate_ra_arcsec_s != 0.0) {
                    out.axis1 = custom_rate_ra_arcsec_s / 3600.0 * gear_ha_;
                } else {
                    out.axis1 = max_tracking_rate;
                }
                if (custom_rate_dec_arcsec_s != 0.0) {
                    out.axis2 = custom_rate_dec_arcsec_s / 3600.0 * gear_dec_;
                } else {
                    out.axis2 = 0.0;
                }
                break;
            case config::TrackingMode::OFF:
                out.axis1 = 0.0;
                out.axis2 = 0.0;
                break;
        }
        return out;
    }

    // ALT_AZ / CASUAL: position-dependent sidereal rates.  Numerically
    // identical to the legacy computeMountFrameRates() implementation.
    TrackingRates mountFrameRates(config::TrackingMode mode,
                                  double axis1_tel, double axis2_tel) const {
        constexpr double kPi = 3.14159265358979323846;

        const bool casual = (mount_type_ == config::MountType::CASUAL);
        const std::array<double, 4> q_enu_to_mount = casual
            ? quaternion_
            : std::array<double, 4>{0.0, 0.0, 0.0, 1.0};

        const auto [true_alt, true_az] =
            AstronomicalCalculations::mountOrientationToHorizontal(
                axis1_tel, axis2_tel, q_enu_to_mount);

        const double alt_rad = true_alt * kPi / 180.0;
        const double az_rad  = true_az * kPi / 180.0;
        const double lat_rad = latitude_deg_ * kPi / 180.0;

        double cos_lat = std::cos(lat_rad);
        const double MIN_COS_LAT = 1e-10;
        if (std::abs(cos_lat) < MIN_COS_LAT) {
            cos_lat = std::copysign(MIN_COS_LAT, cos_lat);
        }

        const double MIN_COS_ALT = std::cos(89.5 * kPi / 180.0);
        double cos_alt = std::cos(alt_rad);
        if (std::abs(cos_alt) < MIN_COS_ALT) {
            cos_alt = std::copysign(MIN_COS_ALT, cos_alt);
        }

        const double omega = 7.2921150e-5 * trackingModeFactor(mode);
        const double alt_rate_rad = omega * cos_lat * std::cos(az_rad);
        const double az_rate_rad  = -omega * cos_lat * std::sin(alt_rad) / cos_alt;

        const double sin_alt = std::sin(alt_rad);
        const double cos_az_h = std::cos(az_rad);
        const double sin_az_h = std::sin(az_rad);

        const double vx = -sin_alt * cos_az_h * alt_rate_rad - cos_alt * sin_az_h * az_rate_rad;
        const double vy = -sin_alt * sin_az_h * alt_rate_rad + cos_alt * cos_az_h * az_rate_rad;
        const double vz = cos_alt * alt_rate_rad;

        double qx = q_enu_to_mount[0];
        double qy = q_enu_to_mount[1];
        double qz = q_enu_to_mount[2];
        double qw = q_enu_to_mount[3];
        const double qnorm = std::sqrt(qx * qx + qy * qy + qz * qz + qw * qw);
        if (qnorm > 1e-15) {
            qx /= qnorm; qy /= qnorm; qz /= qnorm; qw /= qnorm;
        }

        const auto rotateVec = [qx, qy, qz, qw](double vx_, double vy_, double vz_)
            -> std::array<double, 3> {
            const double cross1_x = qy * vz_ - qz * vy_;
            const double cross1_y = qz * vx_ - qx * vz_;
            const double cross1_z = qx * vy_ - qy * vx_;
            const double cross2_x = qy * cross1_z - qz * cross1_y;
            const double cross2_y = qz * cross1_x - qx * cross1_z;
            const double cross2_z = qx * cross1_y - qy * cross1_x;
            return {vx_ + 2.0 * qw * cross1_x + 2.0 * cross2_x,
                    vy_ + 2.0 * qw * cross1_y + 2.0 * cross2_y,
                    vz_ + 2.0 * qw * cross1_z + 2.0 * cross2_z};
        };

        const auto mount_pos = rotateVec(cos_alt * cos_az_h, cos_alt * sin_az_h, sin_alt);
        const auto mount_vel = rotateVec(vx, vy, vz);

        const double m1_deg = std::asin(mount_pos[2]) * 180.0 / kPi;
        const double m2_deg = std::atan2(mount_pos[1], mount_pos[0]) * 180.0 / kPi;
        const double m1_rad = m1_deg * kPi / 180.0;
        const double m2_rad = m2_deg * kPi / 180.0;
        double cos_m1 = std::cos(m1_rad);
        const double MIN_COS_M1 = std::cos(89.5 * kPi / 180.0);
        if (std::abs(cos_m1) < MIN_COS_M1) {
            cos_m1 = std::copysign(MIN_COS_M1, cos_m1);
        }

        const double m1_rate_deg = mount_vel[2] / cos_m1 * 180.0 / kPi;
        const double m2_rate_deg = (-mount_vel[0] * std::sin(m2_rad)
                                    + mount_vel[1] * std::cos(m2_rad))
                                   / cos_m1 * 180.0 / kPi;

        TrackingRates out;
        out.axis1 = m1_rate_deg * gear_ha_;
        out.axis2 = m2_rate_deg * gear_dec_;
        return out;
    }

    config::MountType mount_type_;
    double latitude_deg_;
    double gear_ha_;
    double gear_dec_;
    std::array<double, 4> quaternion_;
};

} // namespace core
} // namespace astro_mount

#endif // MOUNT_KINEMATICS_H
