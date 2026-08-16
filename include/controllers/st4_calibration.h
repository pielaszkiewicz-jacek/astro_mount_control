#ifndef ST4_CALIBRATION_H
#define ST4_CALIBRATION_H

#include "hal/st4_control.h"
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace astro_mount { namespace controllers {

/// Calibration coefficients for ST4 guiding.
struct St4CalibrationParams {
    double ra_arcsec_per_ms{0.0};   ///< RA (E/W) calibration [arcsec/ms]
    double dec_arcsec_per_ms{0.0};  ///< Dec (N/S) calibration [arcsec/ms]
    bool calibrated{false};         ///< True after a successful calibration
    std::string method;             ///< "measured" or "theoretical"
};

/// Probe callback. Returns the star/mount displacement (arcseconds) produced
/// by an ST4 pulse of the given direction and duration. May be nullptr, in
/// which case calibration falls back to the theoretical sidereal rate.
using St4DisplacementProbe =
    std::function<double(hal::St4Direction dir, int pulse_ms)>;

/**
 * @brief ST4 autoguiding calibration (arcsec/ms per axis).
 *
 * For each direction (N/S/E/W) a series of pulses of increasing duration is
 * issued and the resulting star displacement (from the probe, e.g. an encoder
 * or a guide camera) is recorded. The per-axis coefficient is the slope of the
 * displacement-vs-duration line through the origin (least squares). Without a
 * probe a theoretical calibration is returned: RA uses the sidereal rate
 * (15.041 arcsec/s), Dec is 0 (an ideal equatorial mount does not drift in
 * Dec while tracking).
 */
class St4Calibration {
public:
    St4Calibration() = default;

    /**
     * @brief Run a full four-direction calibration.
     * @param hal ST4 hardware interface used to send pulses
     * @param pulse_duration_ms Base pulse duration [ms]
     * @param steps Number of pulses per direction (>= 2)
     * @param step_delay_ms Delay between successive pulses [ms]
     * @param probe Displacement probe; if null -> theoretical calibration
     * @return Calibration coefficients
     */
    St4CalibrationParams calibrate(hal::St4Control& hal,
                                   int pulse_duration_ms,
                                   int steps,
                                   int step_delay_ms,
                                   St4DisplacementProbe probe);

    /// Theoretical calibration for an ideal equatorial mount.
    static St4CalibrationParams theoretical(double sidereal_rate_arcsec_s = 15.041);

private:
    /// Least-squares slope through the origin: slope = Σ(x·y)/Σ(x²).
    static double fitSlope(const std::vector<std::pair<double, double>>& pts);
};

}} // namespace astro_mount::controllers

#endif // ST4_CALIBRATION_H
