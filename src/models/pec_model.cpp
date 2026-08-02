#include "models/pec_model.h"
#include <cmath>
#include <algorithm>
#include <numeric>

namespace astro_mount { namespace models {

PECModel::PECModel() = default;

void PECModel::startTraining(double worm_cycle_s, double sample_rate) {
    data_.worm_cycle_seconds = worm_cycle_s;
    data_.sample_rate_hz = sample_rate;
    data_.error_samples.clear();
    data_.harmonic_amplitudes.clear();
    data_.harmonic_phases.clear();
    data_.trained = false;
    training_ = true;
    samples_collected_ = 0;
    target_samples_ = static_cast<int32_t>(worm_cycle_s * sample_rate * 3); // 3 cycles
}

void PECModel::addSample(double position_deg, double expected_deg, double actual_deg) {
    if (!training_) return;
    double error = actual_deg - expected_deg;  // Error in degrees
    double error_arcsec = error * 3600.0;      // Convert to arcseconds
    data_.error_samples.push_back(error_arcsec);
    samples_collected_++;
}

void PECModel::stopTraining(int num_harmonics) {
    if (!training_) return;
    training_ = false;

    if (data_.error_samples.empty()) return;

    // Calculate statistics
    double sum = std::accumulate(data_.error_samples.begin(), data_.error_samples.end(), 0.0);
    double mean = sum / data_.error_samples.size();
    data_.rms_error_arcsec = std::sqrt(
        std::inner_product(data_.error_samples.begin(), data_.error_samples.end(),
                          data_.error_samples.begin(), 0.0) / data_.error_samples.size());

    auto [min_it, max_it] = std::minmax_element(data_.error_samples.begin(), data_.error_samples.end());
    data_.peak_error_arcsec = std::max(std::abs(*min_it), std::abs(*max_it));

    // Perform FFT to extract harmonics
    performFFT(num_harmonics);
    data_.trained = true;
}

double PECModel::getCorrection(double worm_phase_deg) const {
    if (!enabled_ || !data_.trained) return 0.0;
    return reconstructCorrection(worm_phase_deg);
}

double PECModel::getCorrectionAtTime(double time_since_ref) const {
    if (!enabled_ || !data_.trained) return 0.0;
    double phase = std::fmod(time_since_ref / data_.worm_cycle_seconds * 360.0, 360.0);
    return reconstructCorrection(phase);
}

void PECModel::setData(const PECData& data) {
    data_ = data;
    enabled_ = data.trained;
}

double PECModel::getTrainingProgress() const {
    if (target_samples_ == 0) return 0.0;
    return std::min(100.0 * samples_collected_ / target_samples_, 100.0);
}

void PECModel::performFFT(int num_harmonics) {
    // Simplified FFT: extract harmonics via least-squares fitting
    // For each harmonic k: error(t) = A_k * sin(2π*k*t/T + φ_k)
    size_t N = data_.error_samples.size();
    double T = data_.worm_cycle_seconds;
    double dt = 1.0 / data_.sample_rate_hz;

    data_.harmonic_amplitudes.resize(num_harmonics);
    data_.harmonic_phases.resize(num_harmonics);

    for (int k = 1; k <= num_harmonics; ++k) {
        double sum_sin = 0, sum_cos = 0;
        for (size_t i = 0; i < N; ++i) {
            double t = i * dt;
            double theta = 2.0 * M_PI * k * t / T;
            sum_sin += data_.error_samples[i] * std::sin(theta);
            sum_cos += data_.error_samples[i] * std::cos(theta);
        }
        data_.harmonic_amplitudes[k-1] = 2.0 * std::sqrt(sum_sin*sum_sin + sum_cos*sum_cos) / N;
        data_.harmonic_phases[k-1] = std::atan2(-sum_cos, sum_sin);
    }
}

double PECModel::reconstructCorrection(double phase_deg) const {
    double phase_rad = phase_deg * M_PI / 180.0;
    double correction = 0.0;
    for (size_t k = 0; k < data_.harmonic_amplitudes.size(); ++k) {
        correction += data_.harmonic_amplitudes[k] *
                      std::sin((k + 1) * phase_rad + data_.harmonic_phases[k]);
    }
    return correction;
}

}} // namespace
