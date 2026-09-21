#include "IndiPropertyMapper.h"
#include <cmath>
#include <ctime>
#include <libnova/julian_day.h>
#include <libnova/precession.h>
#include <libnova/transform.h>

IndiPropertyMapper::IndiPropertyMapper(double latitude, double longitude, double elevation)
    : latitude_(latitude)
    , longitude_(longitude)
    , elevation_(elevation)
{
}

void IndiPropertyMapper::setLocation(double latitude, double longitude, double elevation)
{
    latitude_ = latitude;
    longitude_ = longitude;
    elevation_ = elevation;
}

astro_mount::Coordinates IndiPropertyMapper::toGrpcCoordinates(
    double raHours, double decDegrees) const
{
    // INDI hands the driver JNow (apparent) coordinates. The controller's
    // slew/track/status pipeline also works in the apparent frame: it computes
    // HA = GAST − RA with apparent sidereal time (iauGst94) and applies no
    // precession/nutation to the incoming RA/Dec. Pass the JNow coordinates
    // through unchanged — precessing to J2000 here would make the controller
    // point ~0.4° away from the selected object (precession + nutation +
    // aberration), which shows up as a wrong reported position.
    astro_mount::Coordinates coords;
    coords.set_ra(raHours);
    coords.set_dec(decDegrees);
    coords.set_apply_precession(false);
    coords.set_apply_nutation(false);
    coords.set_apply_refraction(false);
    coords.set_epoch(0.0);  // JNow (of date)
    return coords;
}

void IndiPropertyMapper::jnowToJ2000(double raHours, double decDegrees,
                                     double& raJ2000Hours, double& decJ2000Degrees) const
{
    const double jdNow = (jd_ > 0.0) ? jd_ : ln_get_julian_from_sys();
    const double jdJ2000 = 2451545.0;

    ln_equ_posn meanOfDate{};
    meanOfDate.ra = raHours * 15.0;
    meanOfDate.dec = decDegrees;

    ln_equ_posn j2000{};
    ln_get_equ_prec2(&meanOfDate, jdNow, jdJ2000, &j2000);

    raJ2000Hours = j2000.ra / 15.0;
    decJ2000Degrees = j2000.dec;
}

void IndiPropertyMapper::j2000ToJnow(double raJ2000Hours, double decJ2000Degrees,
                                     double& raHours, double& decDegrees) const
{
    const double jdNow = (jd_ > 0.0) ? jd_ : ln_get_julian_from_sys();
    const double jdJ2000 = 2451545.0;

    ln_equ_posn j2000{};
    j2000.ra = raJ2000Hours * 15.0;
    j2000.dec = decJ2000Degrees;

    ln_equ_posn meanOfDate{};
    ln_get_equ_prec2(&j2000, jdJ2000, jdNow, &meanOfDate);

    raHours = meanOfDate.ra / 15.0;
    decDegrees = meanOfDate.dec;
}

void IndiPropertyMapper::equatorialToHorizontal(double raHours, double decDegrees,
                                                double& altitudeDeg, double& azimuthDeg) const
{
    ln_equ_posn equ{};
    equ.ra = raHours * 15.0;
    equ.dec = decDegrees;

    ln_lnlat_posn observer{};
    observer.lng = longitude_;
    observer.lat = latitude_;

    ln_hrz_posn hrz{};
    ln_get_hrz_from_equ(&equ, &observer,
                        (jd_ > 0.0) ? jd_ : ln_get_julian_from_sys(), &hrz);

    altitudeDeg = hrz.alt;
    azimuthDeg = hrz.az;
}

void IndiPropertyMapper::horizontalToEquatorial(double altitudeDeg, double azimuthDeg,
                                                double& raHours, double& decDegrees) const
{
    ln_hrz_posn hrz{};
    hrz.alt = altitudeDeg;
    hrz.az = azimuthDeg;

    ln_lnlat_posn observer{};
    observer.lng = longitude_;
    observer.lat = latitude_;

    ln_equ_posn equ{};
    ln_get_equ_from_hrz(&hrz, &observer,
                        (jd_ > 0.0) ? jd_ : ln_get_julian_from_sys(), &equ);

    raHours = equ.ra / 15.0;
    decDegrees = equ.dec;
}

bool IndiPropertyMapper::toIndiRaDec(
    const astro_mount::ControllerState& state,
    double lstHours,
    double& raHours, double& decDegrees) const
{
    // Prefer the controller's corrected on-sky position (current_ra/current_dec),
    // which already includes bootstrap orientation and TPOINT corrections.
    // Fall back to raw telescope axes for older controllers that don't set it.
    if (state.current_ra() != 0.0 || state.current_dec() != 0.0)
    {
        raHours = state.current_ra();
        decDegrees = state.current_dec();
        raHours = fmod(raHours, 24.0);
        if (raHours < 0) raHours += 24.0;
        return true;
    }

    // telescope_axis1/axis2 are TELESCOPE degrees (servo ÷ gear ratio), already
    // normalized by the controller. current_position() holds RAW servo degrees
    // and must NOT be used here.
    //   equatorial: axis1 = HA in [0°,360°), axis2 = Dec in [0°,360°)
    double axis1Deg = state.telescope_axis1();
    double axis2Deg = state.telescope_axis2();

    // HA → [-12, 12] hours
    double haHours = axis1Deg / 15.0;
    if (haHours > 12.0) haHours -= 24.0;
    else if (haHours < -12.0) haHours += 24.0;

    // RA = LST - HA, normalized to [0, 24) hours
    raHours = lstHours - haHours;
    raHours = fmod(raHours, 24.0);
    if (raHours < 0) raHours += 24.0;

    // Dec from [0°,360°) back to signed degrees
    decDegrees = axis2Deg;
    if (decDegrees > 180.0) decDegrees -= 360.0;

    return true;
}

int IndiPropertyMapper::toIndiTrackState(
    astro_mount::ControllerState::MountStatus status) const
{
    using MountStatus = astro_mount::ControllerState::MountStatus;

    switch (status)
    {
    case MountStatus::ControllerState_MountStatus_SLEWING:
        return INDI::Telescope::SCOPE_SLEWING;
    case MountStatus::ControllerState_MountStatus_TRACKING:
        return INDI::Telescope::SCOPE_TRACKING;
    case MountStatus::ControllerState_MountStatus_PARKED:
        return INDI::Telescope::SCOPE_PARKED;
    case MountStatus::ControllerState_MountStatus_ERROR:
        // INDI 2.x has no SCOPE_ERROR; report an error state as idle so the
        // client still polls and the operator sees the raw controller state.
        return INDI::Telescope::SCOPE_IDLE;
    case MountStatus::ControllerState_MountStatus_IDLE:
    default:
        return INDI::Telescope::SCOPE_IDLE;
    }
}

int IndiPropertyMapper::toIndiPierSide(double pierSide) const
{
    // gRPC: 1 = East, -1 = West (or 0 = unknown)
    if (pierSide > 0)
        return INDI::Telescope::PIER_EAST;
    else if (pierSide < 0)
        return INDI::Telescope::PIER_WEST;
    else
        return INDI::Telescope::PIER_UNKNOWN;
}

double IndiPropertyMapper::computeLst() const
{
    // LST from stored JD (fallback: system time).
    const double jd = (jd_ > 0.0) ? jd_ : ln_get_julian_from_sys();
    const double jd2000 = jd - 2451545.0;

    // GMST (hours)
    double gmst = 18.697374558 + 24.06570982441908 * jd2000;
    gmst = fmod(gmst, 24.0);
    if (gmst < 0) gmst += 24.0;

    // LST
    double lst = gmst + longitude_ / 15.0;
    lst = fmod(lst, 24.0);
    if (lst < 0) lst += 24.0;

    return lst;
}

void IndiPropertyMapper::setJulianDate(double jd)
{
    jd_ = jd;
}

bool IndiPropertyMapper::mountPositionToRaDec(
    double axis1Deg, double axis2Deg,
    double lstHours,
    double& raHours, double& decDegrees) const
{
    // Simplified: equatorial mount with axis1=HA, axis2=Dec
    double haHours = axis1Deg / 15.0;

    if (haHours > 12.0) haHours -= 24.0;
    else if (haHours < -12.0) haHours += 24.0;

    raHours = lstHours - haHours;
    raHours = fmod(raHours, 24.0);
    if (raHours < 0) raHours += 24.0;

    decDegrees = axis2Deg;
    if (decDegrees > 90.0) decDegrees = 90.0;
    else if (decDegrees < -90.0) decDegrees = -90.0;

    return true;
}
