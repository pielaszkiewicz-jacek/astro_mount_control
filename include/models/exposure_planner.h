#ifndef EXPOSURE_PLANNER_H
#define EXPOSURE_PLANNER_H

#include <vector>
#include <string>
#include <cstdint>

namespace astro_mount {
namespace models {

/**
 * @brief Target exposure parameters for a single exposure in a sequence
 */
struct ExposureStep {
    double exposure_time_s{60.0};    // Exposure duration [seconds]
    int32_t gain{0};                 // Camera gain
    int32_t binning{1};              // Binning
    int32_t filter_position{0};      // Filter position
    std::string filter_name;         // Filter name
    int32_t count{1};                // Number of identical exposures
    std::string image_type{"lights"}; // "lights", "darks", "flats", "bias", "dark_flats"
};

/**
 * @brief Complete exposure plan for a sequence
 */
struct ExposurePlan {
    std::string name;                // Plan name
    std::string target_name;         // Target object name
    std::vector<ExposureStep> steps; // Exposure steps
    bool dither{false};              // Enable dithering between exposures
    bool auto_guide{true};           // Enable auto-guiding during exposures
    bool save_to_disk{true};         // Auto-save images
    std::string output_directory;    // Save directory

    /// @brief Calculate total planned exposure time [seconds]
    double totalExposureTime() const {
        double total = 0;
        for (const auto& step : steps) {
            total += step.exposure_time_s * step.count;
        }
        return total;
    }

    /// @brief Get total number of exposures
    int32_t totalExposures() const {
        int32_t total = 0;
        for (const auto& step : steps) {
            total += step.count;
        }
        return total;
    }
};

/**
 * @brief Exposure planner utility
 *
 * Helps construct exposure sequences for observation sessions.
 * Supports common calibration frame plans and multi-filter sequences.
 */
class ExposurePlanner {
public:
    /**
     * @brief Create a standard LRGB exposure plan
     * @param lum_exposure_s Luminance exposure time [s]
     * @param lum_count Number of L frames
     * @param rgb_exposure_s RGB exposure time [s]
     * @param rgb_count Number of frames per RGB channel
     * @param gain Camera gain
     * @param binning Binning
     */
    static ExposurePlan createLRGB(double lum_exposure_s = 60.0, int lum_count = 30,
                                    double rgb_exposure_s = 120.0, int rgb_count = 15,
                                    int gain = 0, int binning = 1);

    /**
     * @brief Create a narrowband exposure plan (Ha, OIII, SII)
     * @param ha_exposure_s Ha exposure time [s]
     * @param count_per_channel Frames per channel
     * @param gain Camera gain
     */
    static ExposurePlan createNarrowband(double ha_exposure_s = 300.0,
                                          int count_per_channel = 20,
                                          int gain = 100);

    /**
     * @brief Create calibration frame plan
     * @param light_count Number of light frames (for matching darks)
     * @param exposure_time_s Exposure time for darks/flats [s]
     * @param flat_count Number of flat frames
     * @param bias_count Number of bias frames
     * @param dark_count Number of dark frames
     */
    static ExposurePlan createCalibration(int light_count = 30,
                                           double exposure_time_s = 60.0,
                                           int flat_count = 20,
                                           int bias_count = 50,
                                           int dark_count = 20);

    /**
     * @brief Calculate optimal exposure time based on sky conditions
     * @param sky_brightness_mpsas Sky brightness [mag/arcsec²]
     * @param aperture_mm Telescope aperture [mm]
     * @param f_ratio Telescope f-ratio
     * @param read_noise_e Camera read noise [e-]
     * @return Recommended exposure time [seconds]
     */
    static double calculateExposureTime(double sky_brightness_mpsas,
                                         double aperture_mm,
                                         double f_ratio,
                                         double read_noise_e = 1.5);

    /**
     * @brief Estimate SNR for given exposure parameters
     * @param exposure_time_s Exposure time [s]
     * @param aperture_cm2 Aperture area [cm²]
     * @param sky_brightness_mpsas Sky brightness
     * @param read_noise_e Read noise [e-]
     * @param dark_current_e_s Dark current [e-/s]
     * @return Estimated SNR (signal-to-noise ratio)
     */
    static double estimateSnr(double exposure_time_s, double aperture_cm2,
                               double sky_brightness_mpsas,
                               double read_noise_e = 1.5,
                               double dark_current_e_s = 0.01);
};

} // namespace models
} // namespace astro_mount

#endif // EXPOSURE_PLANNER_H
