#include "models/field_rotation_model.h"
#include <cmath>

namespace astro_mount { namespace models {

FieldRotationResult FieldRotationModel::calculateAltAz(double ha_hours, double dec_deg, double lat_deg) {
    double ha_rad = ha_hours * 15.0 * M_PI / 180.0;
    double dec_rad = dec_deg * M_PI / 180.0;
    double lat_rad = lat_deg * M_PI / 180.0;

    double angle = std::atan2(std::sin(ha_rad),
        std::cos(ha_rad) * std::sin(lat_rad) - std::tan(dec_rad) * std::cos(lat_rad));
    double rate = 15.041 * std::cos(lat_rad) * std::cos(dec_rad) * std::cos(ha_rad) /
        (1 - std::pow(std::sin(lat_rad), 2) * std::pow(std::sin(dec_rad), 2));

    FieldRotationResult r;
    r.angle_deg = angle * 180.0 / M_PI;
    r.rate_arcsec_s = rate;
    r.predicted_angle_10min = r.angle_deg + r.rate_arcsec_s * 600.0 / 3600.0;
    return r;
}

FieldRotationResult FieldRotationModel::calculateCasual(double axis1_deg, double axis2_deg,
    const std::array<double,4>& q, double ha_hours, double dec_deg, double lat_deg) {
    // CASUAL: transform mount-frame through quaternion Q to horizontal
    // Simplified model — full implementation requires quaternion rotation math
    return calculateAltAz(ha_hours, dec_deg, lat_deg);
}

FieldRotationResult FieldRotationModel::calculateEquatorial() {
    return FieldRotationResult{};
}

double FieldRotationModel::getFieldRotationRate(double ha_hours, double dec_deg, double lat_deg) {
    return calculateAltAz(ha_hours, dec_deg, lat_deg).rate_arcsec_s;
}

}} // namespace astro_mount::models
