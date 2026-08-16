#include "st4guider/include/st4_guider_service_impl.h"
#include "controllers/st4_guider.h"
#include "controllers/st4_calibration.h"
#include "hal/st4_control.h"
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include <thread>
#include <chrono>

namespace astro_st4guider {

using astro_mount::controllers::St4Guider;
using astro_mount::controllers::St4CalibrationParams;
using astro_mount::hal::St4Control;
using astro_mount::hal::St4Direction;
using astro_mount::hal::SimulatedSt4;

St4GuiderServiceImpl::St4GuiderServiceImpl(const std::string& config_path) {
    try {
        if (!configureFromJson(config_path)) {
            std::cerr << "[St4GuiderServiceImpl] Using simulated ST4 interface\n";
            auto hal = std::make_unique<SimulatedSt4>();
            hal->initialize();
            guider_ = std::make_unique<St4Guider>(std::move(hal));
            interface_type_ = "simulated";
            connected_ = true;
        }
    } catch (const std::exception& e) {
        std::cerr << "[St4GuiderServiceImpl] Error: " << e.what() << "\n";
    }
}

St4GuiderServiceImpl::~St4GuiderServiceImpl() {
    if (guider_) guider_->stopPHD2();
}

bool St4GuiderServiceImpl::configureFromJson(const std::string& config_path) {
    std::ifstream file(config_path);
    if (!file.is_open()) return false;
    nlohmann::json config;
    file >> config;

    interface_type_ = config.value("interface_type", "simulated");
    // PHD2 endpoint — allows PHD2 to run on a different host.
    phd2_host_ = config.value("phd2_host", "localhost");
    phd2_port_ = config.value("phd2_port", 4400);
    std::unique_ptr<St4Control> hal;

    if (interface_type_ == "gpio_sysfs") {
        // GPIO sysfs ST4 interface (pins from config). Requires kernel sysfs
        // GPIO access; falls back gracefully at runtime if unavailable.
        hal = std::make_unique<astro_mount::hal::GpioSysfsSt4>(
            config.value("pin_ra_plus", 17),
            config.value("pin_ra_minus", 18),
            config.value("pin_dec_plus", 22),
            config.value("pin_dec_minus", 23));
    } else {
        interface_type_ = "simulated";
        hal = std::make_unique<SimulatedSt4>();
    }

    if (!hal->initialize()) {
        std::cerr << "[St4GuiderServiceImpl] HAL init failed — falling back to simulated\n";
        hal = std::make_unique<SimulatedSt4>();
        hal->initialize();
        interface_type_ = "simulated";
    }

    connected_ = hal->isConnected();
    guider_ = std::make_unique<St4Guider>(std::move(hal));
    return true;
}

grpc::Status St4GuiderServiceImpl::StartGuiding(
    grpc::ServerContext*, const astro_mount::St4GuiderConfig* req, google::protobuf::Empty*) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!guider_) {
        return grpc::Status(grpc::StatusCode::UNAVAILABLE, "Guider not initialised");
    }
    if (req->interface_type().size() > 0) interface_type_ = req->interface_type();

    // Apply guide-loop parameters (aggression, inversion, pulse limits).
    guider_->setGuideParams(
        req->aggression() > 0.0 ? req->aggression() : 1.0,
        req->invert_ra(), req->invert_dec(),
        req->min_pulse_ms() > 0 ? static_cast<int>(req->min_pulse_ms()) : 10,
        req->max_pulse_ms() > 0 ? static_cast<int>(req->max_pulse_ms()) : 3000);

    // PHD2 endpoint: request overrides config; both fall back to localhost:4400.
    // This allows PHD2 to run on a different host (e.g. a guiding PC or a
    // container) — the St4Guider connects over TCP and streams GuideStep events.
    std::string phd2_host = req->phd2_host().empty() ? phd2_host_ : req->phd2_host();
    int phd2_port = req->phd2_port() > 0 ? req->phd2_port() : phd2_port_;
    phd2_host_ = phd2_host;
    phd2_port_ = phd2_port;

    // N3: start a real, persistent PHD2 connection and only mark guiding as
    // active when it succeeded — previously the result was ignored and guiding
    // was reported regardless of reachability.
    bool phd2_ok = guider_->startPHD2(phd2_host, phd2_port);
    phd2_connected_ = phd2_ok;
    if (!phd2_ok) {
        guiding_ = false;
        guider_->setGuiding(false);
        return grpc::Status(grpc::StatusCode::UNAVAILABLE,
                            "PHD2 not reachable on " + phd2_host + ":" +
                            std::to_string(phd2_port) + " — guiding not started");
    }

    // Ask PHD2 to begin its guide loop — GuideStep events then stream back and
    // are applied as ST4 corrections by the guiding loop.
    if (guider_->sendPhd2Method("start_guiding")) {
        guiding_ = true;
        guider_->setGuiding(true);
        return grpc::Status::OK;
    }
    // Socket write failed right after connect — treat as unreachable.
    guider_->stopPHD2();
    phd2_connected_ = false;
    guiding_ = false;
    guider_->setGuiding(false);
    return grpc::Status(grpc::StatusCode::UNAVAILABLE,
                        "Failed to send start_guiding to PHD2");
}

grpc::Status St4GuiderServiceImpl::StopGuiding(
    grpc::ServerContext*, const google::protobuf::Empty*, google::protobuf::Empty*) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (guider_) {
        guider_->sendPhd2Method("stop_guiding");
        guider_->stopPHD2();
        guider_->setGuiding(false);
    }
    guiding_ = false;
    phd2_connected_ = false;
    return grpc::Status::OK;
}

grpc::Status St4GuiderServiceImpl::GetStatus(
    grpc::ServerContext*, const google::protobuf::Empty*, astro_mount::St4Status* response) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!guider_) {
        return grpc::Status(grpc::StatusCode::UNAVAILABLE, "Guider not initialised");
    }
    populateStatus(response);
    return grpc::Status::OK;
}

grpc::Status St4GuiderServiceImpl::PulseGuide(
    grpc::ServerContext*, const astro_mount::St4Pulse* req, google::protobuf::Empty*) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!guider_) {
        return grpc::Status(grpc::StatusCode::UNAVAILABLE, "Guider not initialised");
    }
    St4Direction dir;
    switch (req->direction()) {
        case astro_mount::St4Pulse::NORTH: dir = St4Direction::NORTH; break;
        case astro_mount::St4Pulse::SOUTH: dir = St4Direction::SOUTH; break;
        case astro_mount::St4Pulse::EAST:  dir = St4Direction::EAST;  break;
        case astro_mount::St4Pulse::WEST:  dir = St4Direction::WEST;  break;
        default:
            return grpc::Status(grpc::StatusCode::INVALID_ARGUMENT, "Unknown pulse direction");
    }
    guider_->pulse(dir, static_cast<int>(req->duration_ms()));
    return grpc::Status::OK;
}

grpc::Status St4GuiderServiceImpl::Calibrate(
    grpc::ServerContext* context, const astro_mount::St4CalibrateRequest* req,
    grpc::ServerWriter<astro_mount::St4CalibrateProgress>* writer) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!guider_) {
        return grpc::Status(grpc::StatusCode::UNAVAILABLE, "Guider not initialised");
    }

    uint32_t pulse_ms = req->pulse_duration_ms() > 0 ? req->pulse_duration_ms() : 500;
    uint32_t steps    = req->steps() > 0 ? req->steps() : 4;
    uint32_t delay_ms = req->step_delay_ms() > 0 ? req->step_delay_ms() : 500;

    // Stream an initial progress frame.
    astro_mount::St4CalibrateProgress start;
    start.set_direction("RA");
    start.set_step(0);
    start.set_total_steps(steps);
    start.set_complete(false);
    if (!writer->Write(start)) return grpc::Status::CANCELLED;

    // No displacement probe is available in-process (a guide camera would
    // supply it), so calibration uses the theoretical sidereal-rate
    // coefficients — see St4Calibration::theoretical. The result is stored in
    // the guider and exposed through GetStatus.
    bool ok = guider_->calibrate(nullptr, pulse_ms, steps, delay_ms);
    if (context->IsCancelled()) return grpc::Status::CANCELLED;

    astro_mount::St4CalibrateProgress done;
    done.set_direction("ALL");
    done.set_step(steps);
    done.set_total_steps(steps);
    done.set_complete(true);
    done.set_position_arcsec(0.0);
    St4CalibrationParams cal = guider_->getCalibration();
    done.set_calibration_arcsec_per_ms(cal.ra_arcsec_per_ms);
    writer->Write(done);

    return ok
        ? grpc::Status::OK
        : grpc::Status(grpc::StatusCode::INTERNAL, "Calibration failed");
}

void St4GuiderServiceImpl::populateStatus(astro_mount::St4Status* status) const {
    auto stats = guider_ ? guider_->getStats() : St4Guider::Stats{};
    auto cal   = guider_ ? guider_->getCalibration() : St4CalibrationParams{};
    // The guiding loop is driven by PHD2 events, so report the live state
    // held by the guider (StartGuiding/GuideStopped/StarLost events).
    bool live_guiding = guider_ ? guider_->isGuiding() : false;
    bool live_phd2    = guider_ ? guider_->isPHD2Connected() : false;
    status->set_guiding(live_guiding);
    status->set_interface_type(interface_type_);
    // N3: report the live PHD2 connection state (falls back to the HAL state
    // when not guiding).
    status->set_connected(live_phd2 ? live_phd2 : connected_);
    status->set_pulses_sent(stats.pulses_sent);
    status->set_pulses_failed(stats.pulses_failed);
    status->set_ra_correction_arcsec(stats.ra_correction);
    status->set_dec_correction_arcsec(stats.dec_correction);
    status->set_rms_ra(stats.rms_ra);
    status->set_rms_dec(stats.rms_dec);
    status->set_calibrated(stats.calibrated);
}

} // namespace astro_st4guider
