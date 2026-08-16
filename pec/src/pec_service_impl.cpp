#include "pec/include/pec_service_impl.h"
#include "models/pec_model.h"
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include <thread>
#include <chrono>
#include <cmath>

namespace astro_pec {

using astro_mount::models::PECData;
using astro_mount::models::PECModel;

PecServiceImpl::PecServiceImpl(const std::string& config_path)
    : config_path_(config_path),
      data_path_("config/pec_data.json") {
    pec_ = std::make_unique<PECModel>();

    try {
        if (!loadFromJson()) {
            std::cerr << "[PecServiceImpl] No saved PEC data — starting untrained\n";
        }
    } catch (const std::exception& e) {
        std::cerr << "[PecServiceImpl] Data load error: " << e.what() << "\n";
    }

    // Config-level enable flag (may be overridden via SetPECEnabled).
    try {
        std::ifstream f(config_path);
        if (f.is_open()) {
            nlohmann::json cfg;
            f >> cfg;
            enabled_ = cfg.value("enabled", false);
            pec_->setEnabled(enabled_);
        }
    } catch (...) { /* keep defaults */ }
}

PecServiceImpl::~PecServiceImpl() {
    try { saveToJson(); } catch (...) { /* best effort */ }
}

grpc::Status PecServiceImpl::StartTraining(
    grpc::ServerContext* context, const astro_mount::PECTrainingConfig* req,
    grpc::ServerWriter<astro_mount::PECTrainingProgress>* writer) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!pec_) {
        return grpc::Status(grpc::StatusCode::UNAVAILABLE, "PEC not initialised");
    }

    double worm_cycle_s = req->worm_cycle_seconds() > 0 ? req->worm_cycle_seconds() : 638.0;
    int num_harmonics   = req->num_harmonics() > 0 ? req->num_harmonics() : 8;
    double sample_rate  = req->sample_rate_hz() > 0 ? req->sample_rate_hz() : 10.0;

    // No external encoder feed is wired in-process, so training is driven by
    // SYNTHETIC periodic-error samples (a fundamental + 2nd harmonic worm PE).
    // This exercises the full PECModel pipeline (sample collection → per-cycle
    // DFT → harmonic reconstruction) so the feature is demonstrable; a real
    // encoder source can replace addSample() calls in a later phase.
    pec_->startTraining(worm_cycle_s, sample_rate);

    const int steps = 120;
    for (int i = 1; i <= steps; ++i) {
        if (context->IsCancelled()) return grpc::Status::CANCELLED;

        double phase_rad = 2.0 * M_PI * i / steps;
        // Synthetic PE in arcsec: fundamental 8" + 2nd harmonic 3".
        double error_arcsec = 8.0 * std::sin(phase_rad) + 3.0 * std::sin(2.0 * phase_rad);
        double position_deg = 360.0 * i / steps;
        // addSample() computes error = actual − expected in degrees, then
        // converts to arcseconds — pass error_arcsec/3600 as the deviation.
        pec_->addSample(position_deg, 0.0, error_arcsec / 3600.0);

        astro_mount::PECTrainingProgress p;
        p.set_progress_percent(100.0 * i / steps);
        p.set_elapsed_seconds(i);
        p.set_remaining_seconds(steps - i);
        p.set_current_error_arcsec(error_arcsec);
        p.set_status("sampling");
        if (!writer->Write(p)) return grpc::Status::CANCELLED;
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }

    pec_->stopTraining(num_harmonics);
    enabled_ = true;
    pec_->setEnabled(true);
    saveToJson();

    astro_mount::PECTrainingProgress done;
    done.set_progress_percent(100.0);
    done.set_status("complete");
    done.set_peak_error_arcsec(pec_->getPeakError());
    done.set_rms_error_arcsec(pec_->getRMSError());
    writer->Write(done);
    return grpc::Status::OK;
}

grpc::Status PecServiceImpl::StopTraining(
    grpc::ServerContext*, const google::protobuf::Empty*, google::protobuf::Empty*) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!pec_) {
        return grpc::Status(grpc::StatusCode::UNAVAILABLE, "PEC not initialised");
    }
    pec_->stopTraining(8);
    saveToJson();
    return grpc::Status::OK;
}

grpc::Status PecServiceImpl::GetPECStatus(
    grpc::ServerContext*, const google::protobuf::Empty*, astro_mount::PECStatus* response) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!pec_) {
        return grpc::Status(grpc::StatusCode::UNAVAILABLE, "PEC not initialised");
    }
    populateStatus(response);
    return grpc::Status::OK;
}

grpc::Status PecServiceImpl::SetPECEnabled(
    grpc::ServerContext*, const astro_mount::PECEnableRequest* req, google::protobuf::Empty*) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!pec_) {
        return grpc::Status(grpc::StatusCode::UNAVAILABLE, "PEC not initialised");
    }
    enabled_ = req->enabled();
    pec_->setEnabled(enabled_);
    saveToJson();
    return grpc::Status::OK;
}

grpc::Status PecServiceImpl::SavePECData(
    grpc::ServerContext*, const google::protobuf::Empty*, google::protobuf::Empty*) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!pec_) {
        return grpc::Status(grpc::StatusCode::UNAVAILABLE, "PEC not initialised");
    }
    return saveToJson()
        ? grpc::Status::OK
        : grpc::Status(grpc::StatusCode::INTERNAL, "Failed to save PEC data");
}

grpc::Status PecServiceImpl::LoadPECData(
    grpc::ServerContext*, const google::protobuf::Empty*, google::protobuf::Empty*) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!pec_) {
        return grpc::Status(grpc::StatusCode::UNAVAILABLE, "PEC not initialised");
    }
    return loadFromJson()
        ? grpc::Status::OK
        : grpc::Status(grpc::StatusCode::NOT_FOUND, "No saved PEC data");
}

void PecServiceImpl::populateStatus(astro_mount::PECStatus* status) const {
    PECData data = pec_->getData();
    status->set_enabled(pec_->isEnabled());
    status->set_trained(data.trained);
    status->set_peak_error_arcsec(data.peak_error_arcsec);
    status->set_rms_error_arcsec(data.rms_error_arcsec);
    status->set_num_harmonics(static_cast<int32_t>(data.harmonic_amplitudes.size()));
    status->set_worm_cycle_seconds(data.worm_cycle_seconds);
    status->set_correction_arcsec(data.trained ? std::abs(pec_->getCorrection(0.0)) : 0.0);
    status->set_current_phase_deg(0.0);
}

bool PecServiceImpl::saveToJson() const {
    if (!pec_) return false;
    PECData data = pec_->getData();

    nlohmann::json j;
    j["enabled"] = enabled_;
    j["worm_cycle_seconds"] = data.worm_cycle_seconds;
    j["sample_rate_hz"] = data.sample_rate_hz;
    j["peak_error_arcsec"] = data.peak_error_arcsec;
    j["rms_error_arcsec"] = data.rms_error_arcsec;
    j["trained"] = data.trained;
    j["error_samples"] = data.error_samples;
    j["harmonic_amplitudes"] = data.harmonic_amplitudes;
    j["harmonic_phases"] = data.harmonic_phases;

    std::ofstream out(data_path_);
    if (!out.is_open()) return false;
    out << j.dump(4);
    return true;
}

bool PecServiceImpl::loadFromJson() {
    if (!pec_) return false;
    std::ifstream file(data_path_);
    if (!file.is_open()) return false;

    nlohmann::json j;
    file >> j;

    PECData data;
    data.worm_cycle_seconds = j.value("worm_cycle_seconds", 638.0);
    data.sample_rate_hz = j.value("sample_rate_hz", 10.0);
    data.peak_error_arcsec = j.value("peak_error_arcsec", 0.0);
    data.rms_error_arcsec = j.value("rms_error_arcsec", 0.0);
    data.trained = j.value("trained", false);
    if (j.contains("error_samples")) data.error_samples = j["error_samples"].get<std::vector<double>>();
    if (j.contains("harmonic_amplitudes")) data.harmonic_amplitudes = j["harmonic_amplitudes"].get<std::vector<double>>();
    if (j.contains("harmonic_phases")) data.harmonic_phases = j["harmonic_phases"].get<std::vector<double>>();
    pec_->setData(data);

    enabled_ = j.value("enabled", false);
    pec_->setEnabled(enabled_);
    return true;
}

} // namespace astro_pec
