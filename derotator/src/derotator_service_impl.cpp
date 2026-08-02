#include "derotator/include/derotator_service_impl.h"
#include "hal/derotator_control.h"
#include "controllers/derotator_controller.h"
#include "models/field_rotation_model.h"
#include <chrono>
#include <thread>
#include <google/protobuf/util/time_util.h>

namespace astro_derotator {

DerotatorServiceImpl::DerotatorServiceImpl(const std::string& config_path)
    : config_path_(config_path) {
    try {
        auto hal = astro_mount::hal::createDerotatorControl(config_path);
        controller_ = std::make_unique<astro_mount::controllers::DerotatorController>(
            std::move(hal));
        initialized_ = true;
    } catch (const std::exception& e) {
        std::cerr << "[DerotatorServiceImpl] Init error: " << e.what() << "\n";
    }
}

DerotatorServiceImpl::~DerotatorServiceImpl() {
    if (watching_) {
        watching_ = false;
        if (watch_thread_ && watch_thread_->joinable())
            watch_thread_->join();
    }
}

// ─── Status helpers ──────────────────────────────────────────────────────────

static DerotatorMode mapMode(astro_mount::hal::DerotatorMode m) {
    switch (m) {
        case astro_mount::hal::DerotatorMode::DISABLED:     return DerotatorMode::DM_DISABLED;
        case astro_mount::hal::DerotatorMode::AUTO:         return DerotatorMode::DM_AUTO;
        case astro_mount::hal::DerotatorMode::FIXED_ANGLE:  return DerotatorMode::DM_FIXED_ANGLE;
        case astro_mount::hal::DerotatorMode::MANUAL_RATE:  return DerotatorMode::DM_MANUAL_RATE;
        default: return DerotatorMode::DM_DISABLED;
    }
}

void DerotatorServiceImpl::populateStatus(DerotatorStatus* status) const {
    if (!initialized_ || !controller_) {
        status->set_mode(DerotatorMode::DM_DISABLED);
        status->set_error(true);
        status->set_error_message("Derotator controller not initialised");
        return;
    }
    auto s = controller_->getStatus();
    status->set_mode(mapMode(s.mode));
    status->set_homed(s.homed);
    status->set_current_position_deg(s.current_position_deg);
    status->set_target_position_deg(s.target_position_deg);
    status->set_current_rate_deg_s(s.current_rate_deg_s);
    status->set_moving(s.moving);
    status->set_error(s.error);
    status->set_error_message(s.error_message);
    status->set_connected(true);

    auto now = std::chrono::system_clock::now();
    auto ts = google::protobuf::util::TimeUtil::SecondsToTimestamp(
        std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count());
    *status->mutable_timestamp() = ts;
}

void DerotatorServiceImpl::populateFieldRotation(FieldRotationResult* result) const {
    if (!initialized_ || !controller_) {
        result->set_current_angle_deg(0);
        result->set_current_rate_arcsec_s(0);
        result->set_predicted_angle_10min(0);
        return;
    }
    auto fr = controller_->getFieldRotation();
    result->set_current_angle_deg(fr.angle_deg);
    result->set_current_rate_arcsec_s(fr.rate_arcsec_s);
    result->set_predicted_angle_10min(fr.predicted_angle_10min);
}

// ─── gRPC methods ────────────────────────────────────────────────────────────

grpc::Status DerotatorServiceImpl::SetMode(grpc::ServerContext*,
                                            const SetModeRequest* req,
                                            google::protobuf::Empty*) {
    std::lock_guard<std::mutex> lock(mtx_);
    if (!initialized_ || !controller_)
        return grpc::Status(grpc::StatusCode::UNAVAILABLE, "Not initialised");

    astro_mount::hal::DerotatorMode m;
    switch (req->mode()) {
        case DerotatorMode::DM_DISABLED:    m = astro_mount::hal::DerotatorMode::DISABLED; break;
        case DerotatorMode::DM_AUTO:        m = astro_mount::hal::DerotatorMode::AUTO; break;
        case DerotatorMode::DM_FIXED_ANGLE: m = astro_mount::hal::DerotatorMode::FIXED_ANGLE; break;
        case DerotatorMode::DM_MANUAL_RATE: m = astro_mount::hal::DerotatorMode::MANUAL_RATE; break;
        default: return grpc::Status(grpc::StatusCode::INVALID_ARGUMENT, "Invalid mode");
    }
    return controller_->setMode(m)
        ? grpc::Status::OK
        : grpc::Status(grpc::StatusCode::INTERNAL, "Failed to set mode");
}

grpc::Status DerotatorServiceImpl::SetAngle(grpc::ServerContext*,
                                             const SetAngleRequest* req,
                                             google::protobuf::Empty*) {
    std::lock_guard<std::mutex> lock(mtx_);
    if (!initialized_ || !controller_)
        return grpc::Status(grpc::StatusCode::UNAVAILABLE, "Not initialised");
    return controller_->setAngle(req->angle_deg())
        ? grpc::Status::OK
        : grpc::Status(grpc::StatusCode::INTERNAL, "Failed to set angle");
}

grpc::Status DerotatorServiceImpl::SetRate(grpc::ServerContext*,
                                            const SetRateRequest* req,
                                            google::protobuf::Empty*) {
    std::lock_guard<std::mutex> lock(mtx_);
    if (!initialized_ || !controller_)
        return grpc::Status(grpc::StatusCode::UNAVAILABLE, "Not initialised");
    return controller_->setRate(req->rate_deg_s())
        ? grpc::Status::OK
        : grpc::Status(grpc::StatusCode::INTERNAL, "Failed to set rate");
}

grpc::Status DerotatorServiceImpl::Home(grpc::ServerContext*,
                                         const google::protobuf::Empty*,
                                         google::protobuf::Empty*) {
    std::lock_guard<std::mutex> lock(mtx_);
    if (!initialized_ || !controller_)
        return grpc::Status(grpc::StatusCode::UNAVAILABLE, "Not initialised");
    return controller_->home()
        ? grpc::Status::OK
        : grpc::Status(grpc::StatusCode::INTERNAL, "Failed to home");
}

grpc::Status DerotatorServiceImpl::GetStatus(grpc::ServerContext*,
                                              const google::protobuf::Empty*,
                                              DerotatorStatus* response) {
    std::lock_guard<std::mutex> lock(mtx_);
    populateStatus(response);
    return grpc::Status::OK;
}

grpc::Status DerotatorServiceImpl::WatchStatus(grpc::ServerContext* context,
                                                const google::protobuf::Empty*,
                                                grpc::ServerWriter<DerotatorStatus>* writer) {
    watching_ = true;
    while (watching_ && !context->IsCancelled()) {
        DerotatorStatus status;
        {
            std::lock_guard<std::mutex> lock(mtx_);
            populateStatus(&status);
        }
        if (!writer->Write(status)) break;
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    watching_ = false;
    return grpc::Status::OK;
}

void DerotatorServiceImpl::onMountPositionUpdate(double axis1_deg, double axis2_deg) {
    last_axis1_.store(axis1_deg);
    last_axis2_.store(axis2_deg);
}

grpc::Status DerotatorServiceImpl::GetFieldRotation(grpc::ServerContext*,
                                                     const google::protobuf::Empty*,
                                                     FieldRotationResult* response) {
    std::lock_guard<std::mutex> lock(mtx_);
    populateFieldRotation(response);
    return grpc::Status::OK;
}

grpc::Status DerotatorServiceImpl::UpdateMountPosition(grpc::ServerContext*,
                                                        const MountPositionUpdate* req,
                                                        google::protobuf::Empty*) {
    std::lock_guard<std::mutex> lock(mtx_);
    if (!initialized_ || !controller_)
        return grpc::Status(grpc::StatusCode::UNAVAILABLE, "Not initialised");

    last_axis1_.store(req->axis1_deg());
    last_axis2_.store(req->axis2_deg());
    ha_hours_ = req->ha_hours();
    dec_deg_ = req->dec_deg();
    mount_latitude_ = req->latitude_deg();

    // Forward to controller so position_callback_ gets called
    controller_->setPositionCallback([this](double ax1, double ax2) {
        onMountPositionUpdate(ax1, ax2);
    });

    return grpc::Status::OK;
}

void DerotatorServiceImpl::setMountPosition(double axis1_deg, double axis2_deg,
                                            double latitude_deg, double ha_hours,
                                            double dec_deg) {
    std::lock_guard<std::mutex> lock(mtx_);
    if (!initialized_ || !controller_)
        return;

    last_axis1_.store(axis1_deg);
    last_axis2_.store(axis2_deg);
    ha_hours_ = ha_hours;
    dec_deg_ = dec_deg;
    mount_latitude_ = latitude_deg;

    // Forward to controller so position_callback_ gets called
    controller_->setPositionCallback([this](double ax1, double ax2) {
        onMountPositionUpdate(ax1, ax2);
    });
}

grpc::Status DerotatorServiceImpl::CheckHealth(grpc::ServerContext*,
                                                const google::protobuf::Empty*,
                                                google::protobuf::Empty*) {
    if (!initialized_ || !controller_)
        return grpc::Status(grpc::StatusCode::UNAVAILABLE, "Not initialised");
    return grpc::Status::OK;
}

} // namespace astro_derotator
