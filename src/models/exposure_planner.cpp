#include "models/exposure_planner.h"
#include <cmath>
#include <algorithm>
#include <sstream>

namespace astro_mount {
namespace models {

ExposurePlan ExposurePlanner::createLRGB(double lum_exposure_s, int lum_count,
                                          double rgb_exposure_s, int rgb_count,
                                          int gain, int binning) {
    ExposurePlan plan;
    plan.name = "LRGB";
    plan.steps = {
        {lum_exposure_s, gain, binning, 3, "L", lum_count},
        {rgb_exposure_s, gain, binning, 0, "R", rgb_count},
        {rgb_exposure_s, gain, binning, 1, "G", rgb_count},
        {rgb_exposure_s, gain, binning, 2, "B", rgb_count},
    };
    return plan;
}

ExposurePlan ExposurePlanner::createNarrowband(double ha_exposure_s,
                                                int count_per_channel,
                                                int gain) {
    ExposurePlan plan;
    plan.name = "Narrowband";
    plan.steps = {
        {ha_exposure_s, gain, 1, 4, "Ha", count_per_channel},
        {ha_exposure_s, gain, 1, 5, "OIII", count_per_channel},
        {ha_exposure_s, gain, 1, 6, "SII", count_per_channel},
    };
    return plan;
}

ExposurePlan ExposurePlanner::createCalibration(int light_count,
                                                 double exposure_time_s,
                                                 int flat_count,
                                                 int bias_count,
                                                 int dark_count) {
    ExposurePlan plan;
    plan.name = "Calibration";
    plan.steps = {
        {exposure_time_s, 0, 1, 0, "", dark_count, "darks"},
        {1.0, 0, 1, 0, "", bias_count, "bias"},
        {exposure_time_s, 0, 1, 0, "", flat_count, "flats"},
        {exposure_time_s, 0, 1, 0, "", dark_count, "dark_flats"},
    };
    return plan;
}

double ExposurePlanner::calculateExposureTime(double sky_brightness_mpsas,
                                               double aperture_mm,
                                               double f_ratio,
                                               double read_noise_e) {
    // Simple model: calculate exposure to achieve SNR > 100 for a magnitude 18 star
    // This is a simplified estimation — real calculation depends on many factors

    double aperture_cm2 = M_PI * std::pow(aperture_mm / 20.0, 2);  // cm² (accounting for obstruction)
    double sky_flux = std::pow(10, (sky_brightness_mpsas - 21.0) / 2.5);  // Normalized sky brightness

    // Photon rate for a mag 18 star through the aperture [photons/s]
    double star_photon_rate = 1000.0 * aperture_cm2 / (f_ratio * f_ratio);

    // Sky background photon rate [e-/pixel/s]
    double sky_photon_rate = sky_flux * 10.0 * aperture_cm2 / (f_ratio * f_ratio);

    // Required exposure for SNR = 100 (ignoring dark current)
    // SNR = star_photons / sqrt(star_photons + sky_photons + read_noise²)
    // For bright targets, sky noise dominates
    double target_snr = 100.0;
    double exposure = (target_snr * target_snr * read_noise_e * read_noise_e) /
                      (star_photon_rate * star_photon_rate);

    return std::clamp(exposure, 1.0, 600.0);  // Between 1s and 10min
}

double ExposurePlanner::estimateSnr(double exposure_time_s, double aperture_cm2,
                                     double sky_brightness_mpsas,
                                     double read_noise_e,
                                     double dark_current_e_s) {
    double sky_flux = std::pow(10, (sky_brightness_mpsas - 21.0) / 2.5);
    double star_photon_rate = 1000.0 * aperture_cm2;

    double star_photons = star_photon_rate * exposure_time_s;
    double sky_photons = sky_flux * 10.0 * aperture_cm2 * exposure_time_s;
    double dark_charge = dark_current_e_s * exposure_time_s;

    // Total noise = sqrt(object + sky + dark + read_noise²)
    double total_noise = std::sqrt(star_photons + sky_photons + dark_charge +
                                   read_noise_e * read_noise_e);

    return star_photons / total_noise;
}

} // namespace models
} // namespace astro_mount
