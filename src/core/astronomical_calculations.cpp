#include "core/astronomical_calculations.h"
#include <sofa.h>
#include <sofam.h>
#include <cmath>
#include <iostream>
#include <chrono>
#include <mutex>

// Constants for conversions
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#ifndef D2R
#define D2R (M_PI / 180.0)  // Degrees to radians
#endif

#ifndef R2D
#define R2D (180.0 / M_PI)  // Radians to degrees
#endif

namespace astro_mount {
namespace core {

class AstronomicalCalculations::Impl {
public:
    Impl() : latitude(0.0), longitude(0.0), altitude(0.0),
             temperature(15.0), pressure(1013.25), humidity(0.5) {}
    
    std::mutex env_mutex_;  // Protects all environmental and location params
    
    double latitude;      // Degrees
    double longitude;     // Degrees
    double altitude;      // Meters
    
    double temperature;   // Celsius
    double pressure;      // hPa
    double humidity;      // 0-1
    
    // SOFA-specific state
    // (would include any SOFA-specific context here)
};

AstronomicalCalculations::AstronomicalCalculations() 
    : pimpl(std::make_unique<Impl>()) {}

AstronomicalCalculations::~AstronomicalCalculations() = default;

void AstronomicalCalculations::setObserverLocation(double latitude, double longitude, double altitude) {
    std::lock_guard<std::mutex> lock(pimpl->env_mutex_);
    pimpl->latitude = latitude;
    pimpl->longitude = longitude;
    pimpl->altitude = altitude;
}

void AstronomicalCalculations::setEnvironmentalParams(double temperature, double pressure, double humidity) {
    std::lock_guard<std::mutex> lock(pimpl->env_mutex_);
    pimpl->temperature = temperature;
    pimpl->pressure = pressure;
    pimpl->humidity = humidity;
}

double AstronomicalCalculations::jdToMjd(double jd) {
    return jd - 2400000.5;
}

double AstronomicalCalculations::getCurrentJulianDate() {
    auto now = std::chrono::system_clock::now();
    auto duration = now.time_since_epoch();
    auto days = std::chrono::duration_cast<std::chrono::duration<double, std::ratio<86400>>>(duration);
    
    // Unix epoch (1970-01-01) is JD 2440587.5
    return 2440587.5 + days.count();
}

std::pair<double, double> AstronomicalCalculations::applyPrecession(double ra, double dec, 
                                                                    double fromEpoch, double toEpoch) {
    // Convert RA/Dec to radians
    double ra_rad = ra * 15.0 * D2R;  // hours to degrees to radians
    double dec_rad = dec * D2R;
    
    // Split epochs using J2000 method for best precision
    // J2000.0 epoch = 2451545.0
    const double J2000 = 2451545.0;
    double fromDate1 = J2000;
    double fromDate2 = fromEpoch - J2000;
    double toDate1 = J2000;
    double toDate2 = toEpoch - J2000;
    
    // Compute precession matrix from J2000 to fromEpoch
    double rFrom[3][3];
    iauPmat76(fromDate1, fromDate2, rFrom);
    
    // Compute precession matrix from J2000 to toEpoch
    double rTo[3][3];
    iauPmat76(toDate1, toDate2, rTo);
    
    // Transpose rFrom to get matrix from fromEpoch to J2000
    double rFromT[3][3];
    iauTr(rFrom, rFromT);
    
    // Combine: r = rTo * rFromT (fromEpoch -> J2000 -> toEpoch)
    double r[3][3];
    iauRxr(rTo, rFromT, r);
    
    // Convert spherical to Cartesian
    double pos[3];
    iauS2c(ra_rad, dec_rad, pos);
    
    // Apply precession
    double pos1[3];
    iauRxp(r, pos, pos1);
    
    // Convert back to spherical
    double ra1, dec1;
    iauC2s(pos1, &ra1, &dec1);
    
    // Normalize RA
    ra1 = iauAnp(ra1);
    
    // Convert back to hours/degrees
    return {ra1 * R2D / 15.0, dec1 * R2D};
}

std::pair<double, double> AstronomicalCalculations::applyNutation(double ra, double dec, double jd) {
    // Convert RA/Dec to radians
    double ra_rad = ra * 15.0 * D2R;
    double dec_rad = dec * D2R;
    
    // Get nutation parameters
    double dpsi, deps;
    iauNut80(jd, 0.0, &dpsi, &deps);
    
    // Get mean obliquity
    double epsa = iauObl80(jd, 0.0);
    
    // Create nutation matrix
    double r[3][3];
    iauNumat(epsa, dpsi, deps, r);
    
    // Convert spherical to Cartesian
    double pos[3];
    iauS2c(ra_rad, dec_rad, pos);
    
    // Apply nutation
    double pos1[3];
    iauRxp(r, pos, pos1);
    
    // Convert back to spherical
    double ra1, dec1;
    iauC2s(pos1, &ra1, &dec1);
    
    // Normalize RA
    ra1 = iauAnp(ra1);
    
    // Convert back to hours/degrees
    return {ra1 * R2D / 15.0, dec1 * R2D};
}

double AstronomicalCalculations::applyAtmosphericRefraction(double altitude, double azimuth, double jd) {
    // Convert to radians
    double alt_rad = altitude * D2R;
    double az_rad = azimuth * D2R;
    
    // Simple refraction model (Saemundsson formula)
    // More sophisticated models would use SOFA's refco function
    
    double pressure_mbar, temperature_c;
    {
        std::lock_guard<std::mutex> lock(pimpl->env_mutex_);
        pressure_mbar = pimpl->pressure;
        temperature_c = pimpl->temperature;
    }
    
    // Convert to standard conditions if needed
    double t_k = temperature_c + 273.15;
    double p_corr = pressure_mbar * 283.15 / (t_k * 1.33322);
    
    // Clamp altitude to avoid tan singularity near zenith.
    // The Saemundsson formula's argument (h + 10.3/(h + 5.11)) passes through
    // 90° at ~89.89° altitude, where tan(90°) is mathematically undefined.
    // Beyond this point, tan() becomes negative, yielding unphysical negative
    // refraction. Refraction at zenith is zero, so clamping is physically correct.
    // The threshold of 85.0° keeps the tan argument safely below 90° while still
    // capturing meaningful refraction at high altitudes (~5 arcseconds at 85°).
    const double MAX_ALT_FOR_REFRACTION = 85.0;
    double alt_clamped = std::min(altitude, MAX_ALT_FOR_REFRACTION);
    
    // Saemundsson formula
    double r = 1.02 / tan((alt_clamped + 10.3 / (alt_clamped + 5.11)) * D2R);
    
    // Guard against negative refraction from tan() when argument exceeds 90°
    // (can still occur with float imprecision near the clamp boundary)
    if (r < 0.0) r = 0.0;
    
    r *= p_corr / 1010.0 * 283.15 / t_k;
    
    return r / 60.0;  // Convert arcminutes to degrees
}

std::pair<double, double> AstronomicalCalculations::equatorialToHorizontal(double ra, double dec, double jd,
                                                                           bool apply_refraction) {
    // Convert RA/Dec to radians
    double ra_rad = ra * 15.0 * D2R;
    double dec_rad = dec * D2R;
    
    // Read location params under lock
    double lat, lon;
    {
        std::lock_guard<std::mutex> lock(pimpl->env_mutex_);
        lat = pimpl->latitude;
        lon = pimpl->longitude;
    }
    
    // Get Local Apparent Sidereal Time
    double gast = iauGst94(jd, 0.0);
    double lst = gast + lon * D2R / 15.0;
    
    // Convert to hour angle
    double ha = lst - ra_rad;
    
    // Convert to horizontal coordinates
    double az, alt;
    iauHd2ae(ha, dec_rad, lat * D2R, &az, &alt);
    
    // Apply atmospheric refraction if requested
    if (apply_refraction) {
        double refraction = applyAtmosphericRefraction(alt * R2D, az * R2D, jd);
        alt += refraction * D2R;
    }
    
    // Normalize azimuth
    az = iauAnp(az);
    
    return {alt * R2D, az * R2D};
}

std::pair<double, double> AstronomicalCalculations::horizontalToEquatorial(double altitude, double azimuth, double jd,
                                                                           bool apply_refraction) {
    // Convert to radians
    double alt_rad = altitude * D2R;
    double az_rad = azimuth * D2R;
    
    // Remove atmospheric refraction if requested
    if (apply_refraction) {
        double refraction = applyAtmosphericRefraction(altitude, azimuth, jd);
        alt_rad -= refraction * D2R;
    }
    
    // Read location params under lock
    double lat, lon;
    {
        std::lock_guard<std::mutex> lock(pimpl->env_mutex_);
        lat = pimpl->latitude;
        lon = pimpl->longitude;
    }
    
    // Convert to equatorial coordinates
    double ha, dec;
    iauAe2hd(az_rad, alt_rad, lat * D2R, &ha, &dec);
    
    // Get Local Apparent Sidereal Time
    double gast = iauGst94(jd, 0.0);
    double lst = gast + lon * D2R / 15.0;
    
    // Convert hour angle to right ascension
    double ra = lst - ha;
    
    // Normalize RA
    ra = iauAnp(ra);
    
    return {ra * R2D / 15.0, dec * R2D};
}

std::pair<double, double> AstronomicalCalculations::calculateApparentPlace(double ra, double dec, double jd) {
    // Convert J2000/ICRS RA/Dec to radians
    double ra_rad = ra * 15.0 * D2R;  // hours → degrees → radians
    double dec_rad = dec * D2R;
    
    // Convert spherical to Cartesian (unit vector toward source)
    double pos[3];
    iauS2c(ra_rad, dec_rad, pos);
    
    // Step 1: Precess from J2000 (JD DJ00 = 2451545.0) to target date
    double rprec[3][3];
    iauPmat76(DJ00, jd - DJ00, rprec);
    
    double pos_prec[3];
    iauRxp(rprec, pos, pos_prec);
    
    // Step 2: Apply nutation
    double dpsi, deps;
    iauNut80(jd, 0.0, &dpsi, &deps);
    double epsa = iauObl80(jd, 0.0);
    
    double rnut[3][3];
    iauNumat(epsa, dpsi, deps, rnut);
    
    double pos_nut[3];
    iauRxp(rnut, pos_prec, pos_nut);
    
    // Step 3: Apply annual aberration
    double pvh[2][3], pvb[2][3];
    int epv_status = iauEpv00(jd, 0.0, pvh, pvb);
    
    // Convert barycentric velocity from AU/day to units of c
    const double C_AUDAY = 173.1446326846693;
    double v_vec[3];
    for (int i = 0; i < 3; i++) {
        v_vec[i] = pvb[1][i] / C_AUDAY;
    }
    
    // Compute Earth-Sun distance (AU) from Earth's heliocentric position
    double sun_dist = std::sqrt(pvh[0][0] * pvh[0][0] +
                                pvh[0][1] * pvh[0][1] +
                                pvh[0][2] * pvh[0][2]);
    
    // Compute bm1 = sqrt(1 - |v|^2), the reciprocal Lorentz factor
    double v2 = v_vec[0]*v_vec[0] + v_vec[1]*v_vec[1] + v_vec[2]*v_vec[2];
    double bm1 = std::sqrt(1.0 - v2);
    
    double pos_aber[3];
    iauAb(pos_nut, v_vec, sun_dist, bm1, pos_aber);
    
    double ra1, dec1;
    iauC2s(pos_aber, &ra1, &dec1);
    ra1 = iauAnp(ra1);
    
    return {ra1 * R2D / 15.0, dec1 * R2D};
}

double AstronomicalCalculations::calculateFieldRotation(double ra, double dec, double jd,
                                                       const std::vector<double>& mountOrientation) {
    // Convert target to horizontal coordinates to check visibility
    auto [alt, az] = equatorialToHorizontal(ra, dec, jd, true);
    
    // If target is at or below the horizon, field rotation is not meaningful
    if (alt <= 0.0) {
        return 0.0;
    }
    
    // Step 1: Compute the parallactic angle at the target position
    // The parallactic angle is the angle between the direction of the celestial pole
    // and the zenith direction at the target. For an alt-az mount, this IS the
    // field rotation angle.
    
    // Get observer latitude from pimpl
    double lat;
    {
        std::lock_guard<std::mutex> lock(pimpl->env_mutex_);
        lat = pimpl->latitude;
    }
    
    double q = calculateParallacticAngle(ra, dec, jd, lat);
    
    // Step 2: If mount orientation quaternion is provided, adjust field rotation
    // The quaternion represents the mount's orientation relative to the local
    // horizontal frame. We extract the rotation around the vertical (zenith) axis
    // and add it to the parallactic angle to get the effective field rotation
    // for a randomly oriented mount.
    if (mountOrientation.size() >= 4) {
        double qx = mountOrientation[0];
        double qy = mountOrientation[1];
        double qz = mountOrientation[2];
        double qw = mountOrientation[3];
        
        // Normalize the quaternion
        double norm = std::sqrt(qx*qx + qy*qy + qz*qz + qw*qw);
        if (norm > 1.0e-12) {
            qx /= norm; qy /= norm; qz /= norm; qw /= norm;
        }
        
        // Extract the yaw angle (rotation around vertical/zenith axis) from quaternion.
        // For a quaternion representing rotation from horizontal frame to mount frame:
        // yaw = atan2(2*(w*z + x*y), 1 - 2*(y^2 + z^2))
        double sin_yaw = 2.0 * (qw * qz + qx * qy);
        double cos_yaw = 1.0 - 2.0 * (qy * qy + qz * qz);
        double mount_yaw_deg = std::atan2(sin_yaw, cos_yaw) * R2D;
        
        // The mount's yaw offset modifies the effective field rotation angle
        q += mount_yaw_deg;
    }
    
    // Normalize to [-180, 180] degrees
    q = std::fmod(q, 360.0);
    if (q > 180.0) q -= 360.0;
    if (q < -180.0) q += 360.0;
    
    return q;
}

double AstronomicalCalculations::calculateEarthRotationAngle(double jd) {
    return iauEra00(jd, 0.0);
}

double AstronomicalCalculations::calculateGMST(double jd) {
    double ut1 = 0.0;  // Assuming UT1 = UTC for simplicity
    return iauGst94(jd, ut1) * R2D / 15.0;  // Convert to hours
}

double AstronomicalCalculations::calculateLST(double jd, double longitude) {
    double gmst = calculateGMST(jd);
    double lst = gmst + longitude / 15.0;
    // Normalize to [0, 24) hours
    if (lst < 0.0) {
        lst += 24.0;
    } else if (lst >= 24.0) {
        lst -= 24.0;
    }
    return lst;
}

double AstronomicalCalculations::calculateParallacticAngle(double ra, double dec, double jd, double latitude) {
    // Convert to radians
    double ra_rad = ra * 15.0 * D2R;
    double dec_rad = dec * D2R;
    double lat_rad = latitude * D2R;
    
    // Get Local Sidereal Time
    double lst = calculateLST(jd, pimpl->longitude) * 15.0 * D2R;
    double ha = lst - ra_rad;
    
    // Calculate parallactic angle
    double q = atan2(sin(ha), tan(lat_rad) * cos(dec_rad) - sin(dec_rad) * cos(ha));
    
    return q * R2D;
}

double AstronomicalCalculations::calculateAirmass(double altitude) {
    // Simple airmass approximation (Young 1994)
    if (altitude <= 0) return 999.0;
    
    double alt_rad = altitude * D2R;
    double secz = 1.0 / cos(M_PI/2.0 - alt_rad);  // secant of zenith distance
    
    // More accurate formula for low altitudes
    if (altitude < 10.0) {
        secz = 1.0 / (sin(alt_rad) + 0.50572 * pow(altitude + 6.07995, -1.636));
    }
    
    return secz;
}

double AstronomicalCalculations::calculateBarycentricCorrection(double ra, double dec, double jd) {
    // Step 1: Get Earth's position and velocity in the Solar System Barycenter frame
    double pvh[2][3], pvb[2][3];
    int epv_status = iauEpv00(jd, 0.0, pvh, pvb);
    
    if (epv_status != 0) {
        // Ephemeris calculation failed; return zero correction
        return 0.0;
    }
    
    // Step 2: Get observer's geodetic location
    double lat, lon, height;
    {
        std::lock_guard<std::mutex> lock(pimpl->env_mutex_);
        lat = pimpl->latitude;
        lon = pimpl->longitude;
        height = pimpl->altitude;
    }
    
    // Step 3: Compute Earth Rotation Angle for observer velocity
    double era = iauEra00(jd, 0.0);
    
    // Step 4: Compute observer's geocentric position and velocity (AU, AU/day)
    // using SOFA's iauPvtob function.
    // xp, yp (polar motion) and sp (TIO locator) are set to 0 for approximation.
    double pvobs[2][3];
    iauPvtob(lon * D2R, lat * D2R, height, 0.0, 0.0, 0.0, era, pvobs);
    
    // Step 5: Total observer velocity relative to barycenter =
    //         Earth barycentric velocity + observer geocentric velocity
    double v_obs_bary[3];
    for (int i = 0; i < 3; i++) {
        v_obs_bary[i] = pvb[1][i] + pvobs[1][i];  // AU/day
    }
    
    // Step 6: Compute unit vector toward target star from RA/Dec
    double ra_rad = ra * 15.0 * D2R;   // hours → degrees → radians
    double dec_rad = dec * D2R;         // degrees → radians
    double s_unit[3];
    iauS2c(ra_rad, dec_rad, s_unit);
    
    // Step 7: Barycentric correction = -(v_obs_bary · s_unit)
    // v_obs_bary is in AU/day. Convert to km/s:
    //   1 AU = 149597870.7 km
    //   1 day = 86400 s
    //   AU/day * 149597870.7 / 86400 = km/s
    const double AU_KM = 149597870.7;
    const double DAY_S = 86400.0;
    const double AUDAY_TO_KMS = AU_KM / DAY_S;
    
    double dot = 0.0;
    for (int i = 0; i < 3; i++) {
        dot += v_obs_bary[i] * s_unit[i];
    }
    
    return -dot * AUDAY_TO_KMS;  // km/s
}

std::pair<double, double> AstronomicalCalculations::applyProperMotion(double ra0, double dec0,
                                                                      double pmRa, double pmDec,
                                                                      double epoch0, double epoch1) {
    // Convert to radians
    double ra_rad = ra0 * 15.0 * D2R;
    double dec_rad = dec0 * D2R;
    
    // Convert proper motion to radians/year
    // NOTE: pmRa is in arcsec/year (angular measure along RA direction).
    // Conversion: arcsec -> deg -> rad. No /15 factor needed since we're
    // converting to radians, not hours. The cos(dec) division below handles
    // the RA-to-angular conversion.
    double pm_ra_rad = pmRa / 3600.0 * D2R;  // arcsec/year to rad/year
    double pm_dec_rad = pmDec / 3600.0 * D2R;       // arcsec/year to rad/year
    
    // Time difference in years
    double dt_years = (epoch1 - epoch0) / 365.25;
    
    // Apply proper motion
    double ra1 = ra_rad + pm_ra_rad * dt_years / cos(dec_rad);
    double dec1 = dec_rad + pm_dec_rad * dt_years;
    
    // Normalize RA
    ra1 = iauAnp(ra1);
    
    // Convert back to hours/degrees
    return {ra1 * R2D / 15.0, dec1 * R2D};
}

} // namespace core
} // namespace astro_mount