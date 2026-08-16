#include "dome/include/dome_service_impl.h"
#include "hal/dome_control.h"
#include "controllers/dome_controller.h"
#include <chrono>
#include <thread>
#include <google/protobuf/util/time_util.h>

namespace astro_dome {

// ============================================
// Construction / Destruction
// ============================================

DomeServiceImpl::DomeServiceImpl(const std::string& config_path)
    : config_path_(config_path) {
    try {
        // Create dome HAL via factory, then wrap in DomeController
        auto dome_hal = astro_mount::hal::createDomeControl(config_path);
        controller_ = std::make_unique<astro_mount::controllers::DomeController>(
            std::move(dome_hal));
        initialized_ = true;

        // Set up the mount-azimuth callback so auto-sync can work
        initMountAzimuthCallback();

    } catch (const std::exception& e) {
        std::cerr << "[DomeServiceImpl] Initialisation error: " << e.what() << "\n";
        // Keep going — GetStatus will indicate not connected
    }
}

DomeServiceImpl::~DomeServiceImpl() {
    // WatchStatus streams are per-client and self-terminating — there is no
    // shared watcher thread to join here.
}

void DomeServiceImpl::initMountAzimuthCallback() {
    if (!controller_) return;

    // The callback reads the latest value received via UpdateMountAzimuth
    controller_->setMountAzimuthCallback([this]() -> double {
        return latest_mount_azimuth_.load();
    });
    mount_azimuth_callback_initialized_ = true;
}

// ============================================
// Helper: populate DomeStatus from controller
// ============================================

void DomeServiceImpl::populateDomeStatus(DomeStatus* status) const {
    if (!initialized_ || !controller_) {
        status->set_dome_type(DomeType::DT_UNKNOWN);
        status->set_state(DomeState::DS_ERROR);
        status->set_error_message("Dome controller not initialised");
        return;
    }

    auto hal_status = controller_->getStatus();

    // Map dome type
    switch (hal_status.dome_type) {
        case astro_mount::hal::DomeType::ROTATING:
            status->set_dome_type(DomeType::DT_ROTATING);
            break;
        case astro_mount::hal::DomeType::ROLLOFF:
            status->set_dome_type(DomeType::DT_ROLLOFF);
            break;
        default:
            status->set_dome_type(DomeType::DT_UNKNOWN);
            break;
    }

    // Map dome state
    switch (hal_status.state) {
        case astro_mount::hal::DomeState::CLOSED:
            status->set_state(DomeState::DS_CLOSED);
            break;
        case astro_mount::hal::DomeState::OPENING:
            status->set_state(DomeState::DS_OPENING);
            break;
        case astro_mount::hal::DomeState::OPEN:
            status->set_state(DomeState::DS_OPEN);
            break;
        case astro_mount::hal::DomeState::CLOSING:
            status->set_state(DomeState::DS_CLOSING);
            break;
        case astro_mount::hal::DomeState::ROTATING:
            status->set_state(DomeState::DS_ROTATING);
            break;
        case astro_mount::hal::DomeState::PARKED:
            status->set_state(DomeState::DS_PARKED);
            break;
        default:
            status->set_state(DomeState::DS_ERROR);
            break;
    }

    status->set_azimuth_deg(hal_status.azimuth_deg);
    status->set_target_azimuth_deg(hal_status.target_azimuth_deg);
    status->set_can_rotate(hal_status.can_rotate);
    status->set_can_open(hal_status.can_open);
    status->set_shutter_open(hal_status.shutter_open);
    status->set_aperture_deg(hal_status.aperture_deg);
    status->set_parked(hal_status.parked);
    status->set_sync_enabled(controller_->isAutoSyncEnabled());
    status->set_sync_offset_deg(hal_status.sync_offset_deg);
    status->set_error_message(hal_status.error_message);
    status->set_home_azimuth_deg(hal_status.home_azimuth_deg);
    status->set_park_azimuth_deg(hal_status.park_azimuth_deg);
    status->set_connected(true);
    status->set_moving(false);

    // Set timestamp
    auto now = std::chrono::system_clock::now();
    auto ts = google::protobuf::util::TimeUtil::SecondsToTimestamp(
        std::chrono::duration_cast<std::chrono::seconds>(
            now.time_since_epoch()).count());
    *status->mutable_timestamp() = ts;
}

// ============================================
// gRPC method implementations
// ============================================

grpc::Status DomeServiceImpl::OpenShutter(grpc::ServerContext* context,
                                           const google::protobuf::Empty* request,
                                           google::protobuf::Empty* response) {
    std::lock_guard<std::mutex> lock(controller_mutex_);
    if (!initialized_ || !controller_) {
        return grpc::Status(grpc::StatusCode::UNAVAILABLE, "Dome controller not initialised");
    }
    bool ok = controller_->open();
    if (!ok) {
        return grpc::Status(grpc::StatusCode::INTERNAL, "Failed to open dome shutter");
    }
    return grpc::Status::OK;
}

grpc::Status DomeServiceImpl::CloseShutter(grpc::ServerContext* context,
                                            const google::protobuf::Empty* request,
                                            google::protobuf::Empty* response) {
    std::lock_guard<std::mutex> lock(controller_mutex_);
    if (!initialized_ || !controller_) {
        return grpc::Status(grpc::StatusCode::UNAVAILABLE, "Dome controller not initialised");
    }
    bool ok = controller_->close();
    if (!ok) {
        return grpc::Status(grpc::StatusCode::INTERNAL, "Failed to close dome shutter");
    }
    return grpc::Status::OK;
}

grpc::Status DomeServiceImpl::RotateTo(grpc::ServerContext* context,
                                        const RotateRequest* request,
                                        google::protobuf::Empty* response) {
    std::lock_guard<std::mutex> lock(controller_mutex_);
    if (!initialized_ || !controller_) {
        return grpc::Status(grpc::StatusCode::UNAVAILABLE, "Dome controller not initialised");
    }
    bool ok = controller_->rotateTo(request->azimuth_deg());
    if (!ok) {
        return grpc::Status(grpc::StatusCode::INTERNAL, "Failed to rotate dome");
    }
    return grpc::Status::OK;
}

grpc::Status DomeServiceImpl::Halt(grpc::ServerContext* context,
                                    const google::protobuf::Empty* request,
                                    google::protobuf::Empty* response) {
    std::lock_guard<std::mutex> lock(controller_mutex_);
    if (!initialized_ || !controller_) {
        return grpc::Status(grpc::StatusCode::UNAVAILABLE, "Dome controller not initialised");
    }
    // Stop auto-sync — the DomeController sync loop will exit.
    controller_->setAutoSync(false);
    return grpc::Status::OK;
}

grpc::Status DomeServiceImpl::Park(grpc::ServerContext* context,
                                    const google::protobuf::Empty* request,
                                    google::protobuf::Empty* response) {
    std::lock_guard<std::mutex> lock(controller_mutex_);
    if (!initialized_ || !controller_) {
        return grpc::Status(grpc::StatusCode::UNAVAILABLE, "Dome controller not initialised");
    }
    bool ok = controller_->park();
    if (!ok) {
        return grpc::Status(grpc::StatusCode::INTERNAL, "Failed to park dome");
    }
    return grpc::Status::OK;
}

grpc::Status DomeServiceImpl::Unpark(grpc::ServerContext* context,
                                      const google::protobuf::Empty* request,
                                      google::protobuf::Empty* response) {
    std::lock_guard<std::mutex> lock(controller_mutex_);
    if (!initialized_ || !controller_) {
        return grpc::Status(grpc::StatusCode::UNAVAILABLE, "Dome controller not initialised");
    }
    bool ok = controller_->unpark();
    if (!ok) {
        return grpc::Status(grpc::StatusCode::INTERNAL, "Failed to unpark dome");
    }
    return grpc::Status::OK;
}

grpc::Status DomeServiceImpl::GoHome(grpc::ServerContext* context,
                                      const google::protobuf::Empty* request,
                                      google::protobuf::Empty* response) {
    std::lock_guard<std::mutex> lock(controller_mutex_);
    if (!initialized_ || !controller_) {
        return grpc::Status(grpc::StatusCode::UNAVAILABLE, "Dome controller not initialised");
    }
    // Rotate to 0° as a reasonable home position.
    bool ok = controller_->rotateTo(0.0);
    if (!ok) {
        return grpc::Status(grpc::StatusCode::INTERNAL, "Failed to rotate dome to home");
    }
    return grpc::Status::OK;
}

grpc::Status DomeServiceImpl::GetStatus(grpc::ServerContext* context,
                                         const google::protobuf::Empty* request,
                                         DomeStatus* response) {
    std::lock_guard<std::mutex> lock(controller_mutex_);
    populateDomeStatus(response);
    return grpc::Status::OK;
}

grpc::Status DomeServiceImpl::WatchStatus(grpc::ServerContext* context,
                                           const google::protobuf::Empty* request,
                                           grpc::ServerWriter<DomeStatus>* writer) {
    // Per-client loop — only this client's cancellation ends the stream. The
    // previous implementation used the shared `watching_` flag, so one client
    // disconnecting terminated every subscriber's stream (same class of bug as
    // N7 for weather alerts / P13 for the derotator).
    while (!context->IsCancelled()) {
        {
            std::lock_guard<std::mutex> lock(controller_mutex_);
            DomeStatus status;
            populateDomeStatus(&status);
            if (!writer->Write(status)) {
                break; // Client disconnected
            }
        }
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    return grpc::Status::OK;
}

grpc::Status DomeServiceImpl::SetAutoSync(grpc::ServerContext* context,
                                           const AutoSyncConfig* request,
                                           google::protobuf::Empty* response) {
    std::lock_guard<std::mutex> lock(controller_mutex_);
    if (!initialized_ || !controller_) {
        return grpc::Status(grpc::StatusCode::UNAVAILABLE, "Dome controller not initialised");
    }
    controller_->setAutoSync(request->enabled());
    return grpc::Status::OK;
}

grpc::Status DomeServiceImpl::GetAutoSync(grpc::ServerContext* context,
                                           const google::protobuf::Empty* request,
                                           AutoSyncConfig* response) {
    std::lock_guard<std::mutex> lock(controller_mutex_);
    if (!initialized_ || !controller_) {
        return grpc::Status(grpc::StatusCode::UNAVAILABLE, "Dome controller not initialised");
    }
    response->set_enabled(controller_->isAutoSyncEnabled());
    return grpc::Status::OK;
}

grpc::Status DomeServiceImpl::UpdateMountAzimuth(grpc::ServerContext* context,
                                                  const MountAzimuth* request,
                                                  google::protobuf::Empty* response) {
    // Store the latest mount azimuth for the dome sync loop to read.
    // The callback set in initMountAzimuthCallback() reads this value.
    latest_mount_azimuth_.store(request->azimuth_deg());
    return grpc::Status::OK;
}

void DomeServiceImpl::setMountAzimuth(double azimuth_deg) {
    // In-process equivalent of UpdateMountAzimuth: store the latest
    // mount azimuth for the dome sync loop to read via the callback.
    latest_mount_azimuth_.store(azimuth_deg);
}

grpc::Status DomeServiceImpl::CheckHealth(grpc::ServerContext* context,
                                           const google::protobuf::Empty* request,
                                           google::protobuf::Empty* response) {
    if (!initialized_ || !controller_) {
        return grpc::Status(grpc::StatusCode::UNAVAILABLE, "Dome controller not initialised");
    }
    return grpc::Status::OK;
}

} // namespace astro_dome
