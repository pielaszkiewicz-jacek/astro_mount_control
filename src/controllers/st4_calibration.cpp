#include "controllers/st4_calibration.h"
#include <thread>
#include <chrono>
#include <cmath>
#include <algorithm>
#include <limits>

namespace astro_mount { namespace controllers {

double St4Calibration::fitSlope(const std::vector<std::pair<double, double>>& pts) {
    // Least-squares fit of y = slope·x through the origin:
    //   slope = Σ(x·y) / Σ(x²)
    double sum_xy = 0.0, sum_xx = 0.0;
    for (const auto& p : pts) {
        sum_xy += p.first * p.second;
        sum_xx += p.first * p.first;
    }
    if (sum_xx <= std::numeric_limits<double>::epsilon()) {
        return 0.0;
    }
    double slope = sum_xy / sum_xx;
    return std::isfinite(slope) ? slope : 0.0;
}

St4CalibrationParams St4Calibration::theoretical(double sidereal_rate_arcsec_s) {
    St4CalibrationParams p;
    // An ideal equatorial mount tracks at the sidereal rate, so an RA (E/W)
    // guide pulse moves the star at ~15.041 arcsec/s. Dec (N/S) has no
    // tracking drift on a well-aligned equatorial mount.
    p.ra_arcsec_per_ms = (std::isfinite(sidereal_rate_arcsec_s) && sidereal_rate_arcsec_s > 0.0)
                             ? sidereal_rate_arcsec_s / 1000.0
                             : 15.041 / 1000.0;
    p.dec_arcsec_per_ms = 0.0;
    p.calibrated = true;
    p.method = "theoretical";
    return p;
}

St4CalibrationParams St4Calibration::calibrate(hal::St4Control& hal,
                                               int pulse_duration_ms,
                                               int steps,
                                               int step_delay_ms,
                                               St4DisplacementProbe probe) {
    // Guard against unusable parameters.
    if (steps < 2) steps = 2;
    if (pulse_duration_ms < 1) pulse_duration_ms = 1;
    if (step_delay_ms < 1) step_delay_ms = 50;

    // Without a displacement probe we cannot measure the actual response, so
    // fall back to the theoretical sidereal-rate calibration.
    if (!probe) {
        return theoretical();
    }

    St4CalibrationParams result;
    result.method = "measured";

    const hal::St4Direction ra_dirs[2] = {hal::St4Direction::EAST, hal::St4Direction::WEST};
    const hal::St4Direction dec_dirs[2] = {hal::St4Direction::NORTH, hal::St4Direction::SOUTH};

    auto calibrate_axis = [&](const hal::St4Direction dirs[2], double& rate_out) {
        std::vector<std::pair<double, double>> pts;
        pts.reserve(2 * steps);

        for (int d = 0; d < 2; ++d) {
            const hal::St4Direction dir = dirs[d];
            double cumulative_ms = 0.0, cumulative_arcsec = 0.0;

            for (int s = 0; s < steps; ++s) {
                int dur = pulse_duration_ms * (s + 1);  // increasing duration
                if (!hal.pulse(dir, dur)) {
                    continue;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(step_delay_ms));

                double disp = probe(dir, dur);
                if (!std::isfinite(disp)) {
                    continue;
                }
                // Accumulate so each point is the total displacement for the
                // total elapsed pulse time (independent of direction sign).
                cumulative_ms += dur;
                cumulative_arcsec += disp;
                pts.emplace_back(cumulative_ms, cumulative_arcsec);
            }
        }

        // Average the slopes of the two opposite directions to cancel any
        // systematic bias, then take the magnitude as the per-axis rate.
        rate_out = std::abs(fitSlope(pts));
    };

    calibrate_axis(ra_dirs, result.ra_arcsec_per_ms);
    calibrate_axis(dec_dirs, result.dec_arcsec_per_ms);

    result.calibrated = true;
    return result;
}

}} // namespace astro_mount::controllers
