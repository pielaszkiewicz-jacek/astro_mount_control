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
    // Simple model: calculate exposure to achieve a target SNR for a magnitude 18 star.
    //
    // NUMERICAL CORRECTNESS FIX: the previous formula
    //   exposure = SNR² · N_r² / R_s²
    // was dimensionally wrong (it produced units of s²). The exact SNR equation
    //   SNR = R_s·t / sqrt(R_s·t + R_sky·t + N_r²)         (dark current ignored)
    // rearranges to the quadratic  R_s²·t² − SNR²·(R_s + R_sky)·t − SNR²·N_r² = 0,
    // whose positive root is used below (includes sky background and read noise).

    double aperture_cm2 = M_PI * std::pow(aperture_mm / 20.0, 2);  // cm²
    double sky_flux = std::pow(10, (sky_brightness_mpsas - 21.0) / 2.5);  // Normalized sky brightness

    // Photon rate for a mag 18 star through the aperture [e-/s]
    double star_photon_rate = 1000.0 * aperture_cm2 / (f_ratio * f_ratio);

    // Sky background photon rate [e-/pixel/s]
    double sky_photon_rate = sky_flux * 10.0 * aperture_cm2 / (f_ratio * f_ratio);

    double target_snr = 100.0;
    if (star_photon_rate <= 0.0 || !std::isfinite(star_photon_rate)) {
        return 1.0;  // degenerate aperture — cannot integrate signal
    }

    double snr2 = target_snr * target_snr;
    // R_s²·t² − snr2·(R_s + R_sky)·t − snr2·N_r² = 0
    double b = snr2 * (star_photon_rate + sky_photon_rate);
    double c = snr2 * read_noise_e * read_noise_e;
    double disc = b * b + 4.0 * star_photon_rate * star_photon_rate * c;
    double exposure = (b + std::sqrt(disc)) / (2.0 * star_photon_rate * star_photon_rate);

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
