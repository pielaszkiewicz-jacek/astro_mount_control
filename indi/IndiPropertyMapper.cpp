#include "IndiPropertyMapper.h"
#include <cmath>
#include <ctime>

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
    astro_mount::Coordinates coords;
    coords.set_ra(raHours);
    coords.set_dec(decDegrees);
    coords.set_apply_precession(true);
    coords.set_apply_nutation(true);
    coords.set_apply_refraction(true);
    coords.set_epoch(2000.0);
    return coords;
}

bool IndiPropertyMapper::toIndiRaDec(
    const astro_mount::MountPosition& mountPos,
    const astro_mount::ControllerState& state,
    double lstHours,
    double& raHours, double& decDegrees) const
{
    // For equatorial mounts: axis1 = HA (degrees), axis2 = Dec (degrees)
    // RA = LST - HA (convert HA from deg to hours)
    double haHours = mountPos.axis1() / 15.0;

    // Normalize HA to [-12, 12] hours
    if (haHours > 12.0) haHours -= 24.0;
    else if (haHours < -12.0) haHours += 24.0;

    raHours = lstHours - haHours;

    // Normalize RA to [0, 24) hours
    raHours = fmod(raHours, 24.0);
    if (raHours < 0) raHours += 24.0;

    decDegrees = mountPos.axis2();

    // Clamp Dec to valid range
    if (decDegrees > 90.0) decDegrees = 90.0;
    else if (decDegrees < -90.0) decDegrees = -90.0;

    return true;
}

int IndiPropertyMapper::toIndiTrackState(
    astro_mount::ControllerState::MountStatus status) const
{
    using MountStatus = astro_mount::ControllerState::MountStatus;

    switch (status)
    {
    case MountStatus::ControllerState_MountStatus_SLEWING:
        return SCOPE_SLEWING;
    case MountStatus::ControllerState_MountStatus_TRACKING:
        return SCOPE_TRACKING;
    case MountStatus::ControllerState_MountStatus_PARKED:
        return SCOPE_PARKED;
    case MountStatus::ControllerState_MountStatus_ERROR:
        return SCOPE_ERROR;
    case MountStatus::ControllerState_MountStatus_IDLE:
    default:
        return SCOPE_IDLE;
    }
}

int IndiPropertyMapper::toIndiPierSide(double pierSide) const
{
    // gRPC: 1 = East, -1 = West (or 0 = unknown)
    if (pierSide > 0)
        return PIER_EAST;
    else if (pierSide < 0)
        return PIER_WEST;
    else
        return PIER_UNKNOWN;
}

double IndiPropertyMapper::computeLst(double longitudeDeg)
{
    // Simplified LST from system time
    std::time_t now = std::time(nullptr);
    std::tm* utc = std::gmtime(&now);

    // Julian Date since J2000.0 approximation
    int year = utc->tm_year + 1900;
    int month = utc->tm_mon + 1;
    int day = utc->tm_mday;

    // Simple JD calculation
    double jd = 367 * year - std::floor(7 * (year + std::floor((month + 9) / 12.0)) / 4.0)
                + std::floor(275 * month / 9.0) + day + 1721013.5;

    double hour_utc = utc->tm_hour + utc->tm_min / 60.0 + utc->tm_sec / 3600.0;
    jd += hour_utc / 24.0;

    double jd2000 = jd - 2451545.0;

    // GMST (hours)
    double gmst = 18.697374558 + 24.06570982441908 * jd2000;
    gmst = fmod(gmst, 24.0);
    if (gmst < 0) gmst += 24.0;

    // LST
    double lst = gmst + longitudeDeg / 15.0;
    lst = fmod(lst, 24.0);
    if (lst < 0) lst += 24.0;

    return lst;
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
