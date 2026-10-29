using System;

namespace AstroMount
{
    /// <summary>
    /// Coordinate conversion utilities between mount axis space and RA/Dec.
    /// 
    /// For EQUATORIAL mounts:
    ///   axis1 = Hour Angle (HA) in degrees
    ///   axis2 = Declination (Dec) in degrees
    ///   RA = LST - HA  (in hours)
    /// 
    /// For ALT_AZ mounts:
    ///   axis1 = Azimuth in degrees
    ///   axis2 = Altitude in degrees
    ///   Conversion to RA/Dec requires full coordinate transform (alt-az → equatorial).
    /// 
    /// The controller works in the apparent (JNow/of-date) frame. ASCOM clients
    /// exchange J2000 coordinates, so this helper also provides J2000↔JNow
    /// precession (IAU 1976) and equatorial→horizontal conversion.
    /// </summary>
    public class ConversionHelper
    {
        private double _latitude;  // Observer latitude in degrees

        /// <param name="latitude">Observer latitude in degrees (positive North).</param>
        public ConversionHelper(double latitude = 52.0)
        {
            _latitude = latitude;
        }

        /// <summary>
        /// Convert mount axis position to equatorial (RA, Dec) based on mount type.
        /// </summary>
        /// <param name="pos">MountPosition from controller state (axis1, axis2 in degrees).</param>
        /// <param name="mountType">Mount type (equatorial or alt-az).</param>
        /// <param name="lstHours">Local Sidereal Time in hours (required for equatorial conversion).</param>
        /// <returns>Tuple (RA_hours, Dec_degrees).</returns>
        public (double Ra, double Dec) MountPositionToRaDec(
            MountPosition pos,
            MountType mountType,
            double lstHours)
        {
            switch (mountType)
            {
                case MountType.Equatorial:
                    return EquatorialMountToRaDec(pos.Axis1, pos.Axis2, lstHours);

                case MountType.AltAz:
                    return AltAzMountToRaDec(pos.Axis1, pos.Axis2, lstHours);

                case MountType.Casual:
                    // For CASUAL mounts, the controller manages orientation internally.
                    // The axis positions are in mount-internal coordinates that require
                    // the quaternion orientation for proper conversion.
                    // Fall back to equatorial approximation.
                    return EquatorialMountToRaDec(pos.Axis1, pos.Axis2, lstHours);

                case MountType.Unknown:
                default:
                    return (0, 0);
            }
        }

        /// <summary>
        /// Precess J2000 equatorial coordinates to the apparent frame (JNow, of date).
        /// Uses the IAU 1976 precession angles for consistency with the controller.
        /// </summary>
        public (double RaHours, double DecDegrees) J2000ToJNow(double raHours, double decDegrees)
        {
            var angles = PrecessionAngles();
            return Precess(raHours, decDegrees, angles.Zeta, angles.Z, angles.Theta);
        }

        /// <summary>
        /// Precess apparent (JNow) equatorial coordinates to J2000.
        /// Inverse of <see cref="J2000ToJNow"/>.
        /// </summary>
        public (double RaHours, double DecDegrees) JNowToJ2000(double raHours, double decDegrees)
        {
            var angles = PrecessionAngles();
            // Inverse precession: swap and negate the rotation angles.
            return Precess(raHours, decDegrees, -angles.Z, -angles.Zeta, -angles.Theta);
        }

        /// <summary>
        /// Convert equatorial (JNow) coordinates to horizontal (Alt/Az).
        /// </summary>
        /// <param name="raHours">Right ascension in hours (of date).</param>
        /// <param name="decDegrees">Declination in degrees (of date).</param>
        /// <param name="lstHours">Local sidereal time in hours.</param>
        /// <returns>Tuple (Altitude_deg, Azimuth_deg). Azimuth is measured from North, positive East.</returns>
        public (double Altitude, double Azimuth) RaDecToHorizontal(
            double raHours, double decDegrees, double lstHours)
        {
            double haHours = lstHours - raHours;
            double haRad = HoursToRadians(haHours);
            double decRad = DegreesToRadians(decDegrees);
            double latRad = DegreesToRadians(_latitude);

            double sinAlt = Math.Sin(decRad) * Math.Sin(latRad) +
                            Math.Cos(decRad) * Math.Cos(latRad) * Math.Cos(haRad);
            double altRad = Math.Asin(Clamp(sinAlt, -1.0, 1.0));

            // Azimuth measured from South, positive West; add 180° to reference North.
            double azRad = Math.Atan2(
                Math.Sin(haRad),
                Math.Cos(haRad) * Math.Sin(latRad) - Math.Tan(decRad) * Math.Cos(latRad));
            double azDeg = RadiansToDegrees(azRad) + 180.0;
            azDeg = ((azDeg % 360.0) + 360.0) % 360.0;

            return (RadiansToDegrees(altRad), azDeg);
        }

        /// <summary>
        /// Convert equatorial mount axis positions (HA, Dec) to RA/Dec.
        /// </summary>
        /// <param name="haDegrees">Hour Angle in degrees.</param>
        /// <param name="decDegrees">Declination in degrees.</param>
        /// <param name="lstHours">Local Sidereal Time in hours.</param>
        /// <returns>Tuple (RA_hours, Dec_degrees).</returns>
        private (double Ra, double Dec) EquatorialMountToRaDec(
            double haDegrees, double decDegrees, double lstHours)
        {
            // Convert HA from degrees to hours
            double haHours = haDegrees / 15.0;

            // Normalize HA to [-12, 12] hours
            haHours = NormalizeHours(haHours, 12);

            // RA = LST - HA (in hours)
            double raHours = lstHours - haHours;

            // Normalize RA to [0, 24) hours
            raHours = NormalizeHours(raHours, 24);

            // Dec stays the same
            double dec = decDegrees;

            // Clamp Dec to valid range
            dec = Math.Clamp(dec, -90.0, 90.0);

            return (raHours, dec);
        }

        /// <summary>
        /// Convert alt-az mount axis positions (Az, Alt) to RA/Dec.
        /// Uses standard alt-az → equatorial transformation.
        /// </summary>
        private (double Ra, double Dec) AltAzMountToRaDec(
            double azDegrees, double altDegrees, double lstHours)
        {
            // Convert to radians
            double azRad = DegreesToRadians(azDegrees);
            double altRad = DegreesToRadians(altDegrees);
            double latRad = DegreesToRadians(_latitude);

            // Hour Angle (radians)
            double haRad = Math.Atan2(
                -Math.Sin(azRad) * Math.Cos(altRad),
                Math.Cos(altRad) * Math.Cos(azRad) * Math.Sin(latRad) -
                Math.Sin(altRad) * Math.Cos(latRad)
            );

            // Declination (radians)
            double decRad = Math.Asin(
                Math.Sin(altRad) * Math.Sin(latRad) +
                Math.Cos(altRad) * Math.Cos(azRad) * Math.Cos(latRad)
            );

            // Convert HA to hours
            double haHours = RadiansToHours(haRad);

            // RA = LST - HA
            double raHours = lstHours - haHours;
            raHours = NormalizeHours(raHours, 24);

            double decDeg = RadiansToDegrees(decRad);

            return (raHours, decDeg);
        }

        /// <summary>
        /// Apply IAU 1976 precession to equatorial coordinates.
        /// </summary>
        private static (double RaHours, double DecDegrees) Precess(
            double raHours, double decDegrees,
            double zetaDeg, double zDeg, double thetaDeg)
        {
            double ra0 = raHours * 15.0 * Math.PI / 180.0;
            double dec0 = decDegrees * Math.PI / 180.0;
            double zeta = zetaDeg * Math.PI / 180.0;
            double z = zDeg * Math.PI / 180.0;
            double theta = thetaDeg * Math.PI / 180.0;

            double sinDec0 = Math.Sin(dec0);
            double cosDec0 = Math.Cos(dec0);
            double a = ra0 + zeta;

            double A = cosDec0 * Math.Sin(a);
            double B = Math.Cos(theta) * cosDec0 * Math.Cos(a) - Math.Sin(theta) * sinDec0;
            double C = Math.Sin(theta) * cosDec0 * Math.Cos(a) + Math.Cos(theta) * sinDec0;

            double ra = Math.Atan2(A, B) + z;
            double dec = Math.Asin(Clamp(C, -1.0, 1.0));

            double raOut = ra * 12.0 / Math.PI;
            raOut = ((raOut % 24.0) + 24.0) % 24.0;

            return (raOut, dec * 180.0 / Math.PI);
        }

        /// <summary>
        /// IAU 1976 precession angles (in degrees) for the current Julian date.
        /// </summary>
        private static (double Zeta, double Z, double Theta) PrecessionAngles()
        {
            double jd = JulianDateNow();
            double t = (jd - 2451545.0) / 36525.0;

            double zeta = (2306.2181 * t + 0.30188 * t * t + 0.017998 * t * t * t) / 3600.0;
            double z = (2306.2181 * t + 1.09468 * t * t + 0.018203 * t * t * t) / 3600.0;
            double theta = (2004.3109 * t - 0.42665 * t * t - 0.041833 * t * t * t) / 3600.0;

            return (zeta, z, theta);
        }

        private static double JulianDateNow()
        {
            DateTime utcNow = DateTime.UtcNow;
            return utcNow.ToOADate() + 2415018.5;
        }

        /// <summary>
        /// Normalize a value in hours to the range [0, range) or [-range/2, range/2].
        /// </summary>
        private double NormalizeHours(double hours, double range)
        {
            if (range == 0) return hours;

            hours = hours % range;
            if (hours < -range / 2.0)
                hours += range;
            else if (hours > range / 2.0)
                hours -= range;

            return hours;
        }

        private static double HoursToRadians(double hours)
        {
            return hours * Math.PI / 12.0;
        }

        /// <summary>
        /// Convert degrees to radians.
        /// </summary>
        private static double DegreesToRadians(double degrees)
        {
            return degrees * Math.PI / 180.0;
        }

        /// <summary>
        /// Convert radians to degrees.
        /// </summary>
        private static double RadiansToDegrees(double radians)
        {
            return radians * 180.0 / Math.PI;
        }

        /// <summary>
        /// Convert radians to hours.
        /// </summary>
        private static double RadiansToHours(double radians)
        {
            return radians * 12.0 / Math.PI;
        }

        private static double Clamp(double value, double min, double max)
        {
            if (value < min) return min;
            if (value > max) return max;
            return value;
        }

        /// <summary>
        /// Update the observer latitude (e.g., when site configuration changes).
        /// </summary>
        public void SetLatitude(double latitude)
        {
            _latitude = latitude;
        }
    }
}
