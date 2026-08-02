#ifndef FIELD_ROTATION_MODEL_H
#define FIELD_ROTATION_MODEL_H
#include <array>
#include <cstdint>

namespace astro_mount { namespace models {

struct FieldRotationResult {
    double angle_deg{0.0};
    double rate_arcsec_s{0.0};
    double predicted_angle_10min{0.0};
};

class FieldRotationModel {
public:
    /// ALT_AZ: ρ = atan2(sin(HA), cos(HA)*sin(lat) - tan(dec)*cos(lat))
    static FieldRotationResult calculateAltAz(double ha_hours, double dec_deg, double lat_deg);

    /// CASUAL: Uses quaternion Q to transform mount frame → horizontal
    static FieldRotationResult calculateCasual(double axis1_deg, double axis2_deg,
        const std::array<double,4>& quaternion, double ha_hours, double dec_deg, double lat_deg);

    /// EQUATORIAL: returns 0 (no rotation)
    static FieldRotationResult calculateEquatorial();

    /// Get rate [arcsec/s] from RA/Dec/Lat/HA
    static double getFieldRotationRate(double ha_hours, double dec_deg, double lat_deg);
};

}} // namespace astro_mount::models
#endif
