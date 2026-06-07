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
    /// Note: This is a simplified helper. The actual controller performs all
    /// astrometric corrections internally. When querying GetState(), the controller
    /// already provides current_position (axis1/axis2) which this helper converts
    /// to the RA/Dec values expected by ASCOM.
    /// </summary>
    public class ConversionHelper
    {
        private readonly double _latitude;  // Observer latitude in degrees

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

        /// <summary>
        /// Update the observer latitude (e.g., when site configuration changes).
        /// </summary>
        public void SetLatitude(double latitude)
        {
            _latitude = latitude;
        }
    }
}
