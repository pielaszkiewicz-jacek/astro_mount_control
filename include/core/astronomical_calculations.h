#ifndef ASTRONOMICAL_CALCULATIONS_H
#define ASTRONOMICAL_CALCULATIONS_H

#include <array>
#include <chrono>
#include <vector>
#include <memory>

namespace astro_mount {
namespace core {

// Forward declarations
struct Coordinates;
struct JulianDate;

/**
 * @brief Astronomical calculations using SOFA library
 * 
 * This class provides astronomical calculations including precession,
 * nutation, atmospheric refraction, and coordinate transformations.
 */
class AstronomicalCalculations {
public:
    AstronomicalCalculations();
    ~AstronomicalCalculations();

    /**
     * @brief Set observer location
     * @param latitude Latitude in degrees (positive north)
     * @param longitude Longitude in degrees (positive east)
     * @param altitude Altitude in meters
     */
    void setObserverLocation(double latitude, double longitude, double altitude);

    /**
     * @brief Set environmental parameters
     * @param temperature Temperature in Celsius
     * @param pressure Pressure in hPa
     * @param humidity Relative humidity (0-1)
     */
    void setEnvironmentalParams(double temperature, double pressure, double humidity);

    /**
     * @brief Convert Julian Date to Modified Julian Date
     * @param jd Julian Date
     * @return Modified Julian Date
     */
    static double jdToMjd(double jd);

    /**
     * @brief Get current Julian Date
     * @return Current Julian Date
     */
    static double getCurrentJulianDate();

    /**
     * @brief Apply precession to coordinates
     * @param ra Right ascension in hours
     * @param dec Declination in degrees
     * @param fromEpoch Starting epoch (Julian Date)
     * @param toEpoch Target epoch (Julian Date)
     * @return Precessed coordinates (ra, dec)
     */
    std::pair<double, double> applyPrecession(double ra, double dec, 
                                              double fromEpoch, double toEpoch);

    /**
     * @brief Apply nutation to coordinates
     * @param ra Right ascension in hours
     * @param dec Declination in degrees
     * @param jd Julian Date
     * @return Nutated coordinates (ra, dec)
     */
    std::pair<double, double> applyNutation(double ra, double dec, double jd);

    /**
     * @brief Apply atmospheric refraction
     * @param altitude Apparent altitude in degrees
     * @param azimuth Azimuth in degrees
     * @param jd Julian Date
     * @return Refraction correction in degrees
     */
    double applyAtmosphericRefraction(double altitude, double azimuth, double jd);

    /**
     * @brief Convert equatorial to horizontal coordinates
     * @param ra Right ascension in hours
     * @param dec Declination in degrees
     * @param jd Julian Date
     * @param apply_refraction If true, apply atmospheric refraction to altitude
     * @return Horizontal coordinates (altitude, azimuth) in degrees
     */
    std::pair<double, double> equatorialToHorizontal(double ra, double dec, double jd,
                                                     bool apply_refraction = true);

    /**
     * @brief Convert horizontal to equatorial coordinates
     * @param altitude Altitude in degrees
     * @param azimuth Azimuth in degrees
     * @param jd Julian Date
     * @param apply_refraction If true, remove atmospheric refraction from altitude
     * @return Equatorial coordinates (ra, dec)
     */
    std::pair<double, double> horizontalToEquatorial(double altitude, double azimuth, double jd,
                                                     bool apply_refraction = true);

    /**
     * @brief Calculate apparent place (includes all corrections)
     * @param ra Mean right ascension in hours
     * @param dec Mean declination in degrees
     * @param jd Julian Date
     * @return Apparent coordinates (ra, dec)
     */
    std::pair<double, double> calculateApparentPlace(double ra, double dec, double jd);

    /**
     * @brief Calculate field rotation for randomly oriented mount
     * @param ra Target right ascension in hours
     * @param dec Target declination in degrees
     * @param jd Julian Date
     * @param mountOrientation Mount orientation quaternion
     * @return Field rotation angle in degrees
     */
    double calculateFieldRotation(double ra, double dec, double jd,
                                  const std::vector<double>& mountOrientation);

    /**
     * @brief Convert mount-oriented alt/az to celestial equatorial coordinates
     *
     * For a CASUAL (randomly oriented) mount, the mount reports angles in its own
     * reference frame. This function reverses the quaternion rotation and then
     * applies horizontalToEquatorial to obtain true celestial coordinates.
     *
     * @param mount_altitude Altitude in mount frame [degrees]
     * @param mount_azimuth Azimuth in mount frame [degrees]
     * @param jd Julian Date
     * @param mountOrientation Mount orientation quaternion [qx, qy, qz, qw]
     * @return Equatorial coordinates (ra in hours, dec in degrees)
     */
    std::pair<double, double> mountOrientationToEquatorial(double mount_altitude, double mount_azimuth,
                                                           double jd, const std::array<double, 4>& mountOrientation);

    /**
     * @brief Convert celestial equatorial to mount-oriented alt/az coordinates
     *
     * For a CASUAL (randomly oriented) mount, this converts true RA/Dec to
     * mount-frame alt/az by first applying equatorialToHorizontal, then
     * rotating by the mount orientation quaternion.
     *
     * @param ra Right ascension in hours
     * @param dec Declination in degrees
     * @param jd Julian Date
     * @param mountOrientation Mount orientation quaternion [qx, qy, qz, qw]
     * @return Mount-oriented coordinates (altitude, azimuth) in degrees
     */
    std::pair<double, double> equatorialToMountOrientation(double ra, double dec,
                                                           double jd, const std::array<double, 4>& mountOrientation);

    /**
     * @brief Calculate Earth rotation angle
     * @param jd Julian Date
     * @return Earth rotation angle in radians
     */
    static double calculateEarthRotationAngle(double jd);

    /**
     * @brief Calculate Greenwich Mean Sidereal Time
     * @param jd Julian Date
     * @return GMST in hours
     */
    static double calculateGMST(double jd);

    /**
     * @brief Calculate Local Sidereal Time
     * @param jd Julian Date
     * @param longitude Longitude in degrees (positive east)
     * @return LST in hours
     */
    static double calculateLST(double jd, double longitude);

    /**
     * @brief Calculate parallactic angle
     * @param ra Right ascension in hours
     * @param dec Declination in degrees
     * @param jd Julian Date
     * @param latitude Observer latitude in degrees
     * @return Parallactic angle in degrees
     */
    double calculateParallacticAngle(double ra, double dec, double jd, double latitude);

    /**
     * @brief Calculate airmass
     * @param altitude Altitude in degrees
     * @return Airmass (unitless)
     */
    static double calculateAirmass(double altitude);

    /**
     * @brief Calculate barycentric correction
     * @param ra Right ascension in hours
     * @param dec Declination in degrees
     * @param jd Julian Date
     * @return Barycentric correction in km/s
     */
    double calculateBarycentricCorrection(double ra, double dec, double jd);

    /**
     * @brief Calculate proper motion
     * @param ra0 Initial right ascension in hours
     * @param dec0 Initial declination in degrees
     * @param pmRa Proper motion in RA (arcsec/year)
     * @param pmDec Proper motion in Dec (arcsec/year)
     * @param epoch0 Initial epoch (Julian Date)
     * @param epoch1 Target epoch (Julian Date)
     * @return Updated coordinates (ra, dec)
     */
    std::pair<double, double> applyProperMotion(double ra0, double dec0,
                                                double pmRa, double pmDec,
                                                double epoch0, double epoch1);

private:
    class Impl;
    std::unique_ptr<Impl> pimpl;
};

} // namespace core
} // namespace astro_mount

#endif // ASTRONOMICAL_CALCULATIONS_H