#ifndef PEC_MODEL_H
#define PEC_MODEL_H
#include <vector>
#include <cstdint>
#include <complex>

namespace astro_mount { namespace models {

struct PECData {
    std::vector<double> error_samples;      // Raw error [arcsec] over worm cycle
    std::vector<double> harmonic_amplitudes; // Extracted harmonic amplitudes [arcsec]
    std::vector<double> harmonic_phases;     // Extracted harmonic phases [rad]
    double worm_cycle_seconds{638.0};        // Worm period [s]
    double sample_rate_hz{10.0};             // Sample rate [Hz]
    double peak_error_arcsec{0.0};
    double rms_error_arcsec{0.0};
    bool trained{false};
};

class PECModel {
public:
    PECModel();

    /// Start training: begin accumulating encoder error samples
    void startTraining(double worm_cycle_s = 638.0, double sample_rate = 10.0);

    /// Add a single error sample during training
    void addSample(double position_deg, double expected_deg, double actual_deg);

    /// Stop training and perform FFT analysis
    void stopTraining(int num_harmonics = 8);

    /// Get correction for a given worm phase
    double getCorrection(double worm_phase_deg) const;

    /// Get correction for a given time offset
    double getCorrectionAtTime(double time_since_ref) const;

    /// Enable/disable PEC correction
    void setEnabled(bool enabled) { enabled_ = enabled; }
    bool isEnabled() const { return enabled_; }

    /// Save/load PEC data
    PECData getData() const { return data_; }
    void setData(const PECData& data);

    /// Get current status
    double getPeakError() const { return data_.peak_error_arcsec; }
    double getRMSError() const { return data_.rms_error_arcsec; }
    bool isTrained() const { return data_.trained; }
    double getTrainingProgress() const;

private:
    /// Perform FFT on error samples to extract harmonics
    void performFFT(int num_harmonics);

    /// Reconstruct correction curve from harmonics
    double reconstructCorrection(double phase_deg) const;

    PECData data_;
    bool training_{false};
    bool enabled_{false};
    double reference_time_{0.0};
    int64_t samples_collected_{0};
    int32_t target_samples_{0};
};

}} // namespace astro_mount::models
#endif
