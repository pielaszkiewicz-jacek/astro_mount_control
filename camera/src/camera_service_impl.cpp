#include "camera/include/camera_service_impl.h"
#include "camera/indi_camera_client.h"
#include "logging/logger.h"
#include <string>

namespace astro_camera {

namespace {
std::shared_ptr<spdlog::logger> cameraLog()
{
    return astro_mount::logging::Logger::get("camera");
}
}

CameraServiceImpl::CameraServiceImpl(IndiCameraConfig indi_config)
    : indi_config_(std::move(indi_config))
{
    // The INDI client is always created so the web UI can list and select
    // cameras at runtime. Connection is lazy; an immediate connection is only
    // attempted when a device was pre-configured via environment.
    indi_client_ = std::make_unique<IndiCameraClient>();
    indi_client_->configure(indi_config_.host, indi_config_.port);

    if (indi_config_.enabled && !indi_config_.device.empty()) {
        try {
            if (indi_client_->connectToServer())
                indi_client_->connectCamera(indi_config_.device);
        } catch (const std::exception&) {
        }
    }
}

CameraServiceImpl::~CameraServiceImpl() = default;

void CameraServiceImpl::ensureIndiServerConnected()
{
    if (!indi_client_)
        return;
    try {
        indi_client_->ensureConnected();
    } catch (const std::exception&) {
    }
}

bool CameraServiceImpl::indiCameraActive()
{
    return indi_client_ && !indi_client_->activeDevice().empty();
}

grpc::Status CameraServiceImpl::GetCameraInfo(
    grpc::ServerContext*, const google::protobuf::Empty*, astro_mount::CameraInfo* info) {
    ensureIndiServerConnected();
    if (!indiCameraActive()) {
        info->set_name("No camera selected");
        info->set_connected(false);
        info->set_driver_version("indi-bridge");
        return grpc::Status::OK;
    }

    const auto cam = indi_client_->getInfo();
    const std::string active = indi_client_->activeDevice();
    const std::string display = cam.name.empty() ? active : cam.name;
    info->set_name(display);
    info->set_manufacturer("INDI");
    info->set_sensor_name(display);
    info->set_width(cam.width);
    info->set_height(cam.height);
    info->set_pixel_size_um(cam.pixel_size_um);
    info->set_max_bin(4);
    info->set_has_filter_wheel(!cam.filter_names.empty());
    info->set_num_filters(static_cast<int>(cam.filter_names.size()));
    for (const auto& f : cam.filter_names) info->add_filter_names(f);
    info->set_has_cooler(cam.has_cooler);
    info->set_bit_depth(cam.bit_depth > 0 ? cam.bit_depth : 16.0);
    info->set_connected(true);
    info->set_driver_version("indi-bridge");
    return grpc::Status::OK;
}

grpc::Status CameraServiceImpl::SetFilter(
    grpc::ServerContext*, const astro_mount::FilterRequest* req, google::protobuf::Empty*) {
    ensureIndiServerConnected();
    if (!indiCameraActive())
        return grpc::Status(grpc::StatusCode::FAILED_PRECONDITION, "No INDI camera selected");

    try {
        indi_client_->setFilter(req->position());
    } catch (const std::exception& e) {
        return grpc::Status(grpc::StatusCode::INTERNAL, e.what());
    }
    return grpc::Status::OK;
}

grpc::Status CameraServiceImpl::GetFilterPosition(
    grpc::ServerContext*, const google::protobuf::Empty*, astro_mount::FilterPosition* pos) {
    ensureIndiServerConnected();
    if (!indiCameraActive())
        return grpc::Status(grpc::StatusCode::FAILED_PRECONDITION, "No INDI camera selected");

    const auto cam = indi_client_->getInfo();
    pos->set_num_filters(static_cast<int>(cam.filter_names.size()));
    for (const auto& f : cam.filter_names) pos->add_filter_names(f);
    pos->set_current_position(0);
    return grpc::Status::OK;
}

grpc::Status CameraServiceImpl::StartExposure(
    grpc::ServerContext*, const astro_mount::ExposureRequest* req,
    grpc::ServerWriter<astro_mount::ExposureProgress>* writer) {
    ensureIndiServerConnected();
    cameraLog()->info("StartExposure: {:.3f}s (device='{}')", req->exposure_time_s(),
                      indi_client_ ? indi_client_->activeDevice() : "");

    if (!indiCameraActive()) {
        cameraLog()->warn("StartExposure rejected: no INDI camera selected");
        return grpc::Status(grpc::StatusCode::FAILED_PRECONDITION, "No INDI camera selected");
    }

    const double total_s = std::max(0.1, req->exposure_time_s());
    const int binning = std::max(1, req->binning());
    const double gain = std::max(0.0, static_cast<double>(req->gain()));
    try {
        IndiCameraClient::Exposure capture = indi_client_->capture(
            total_s, total_s + 60.0,
            [writer, &total_s](double remaining_s) {
                astro_mount::ExposureProgress p;
                p.set_elapsed_ms(static_cast<int>((total_s - remaining_s) * 1000.0));
                p.set_remaining_ms(static_cast<int>(remaining_s * 1000.0));
                p.set_progress_percent(100.0 * (total_s - remaining_s) / total_s);
                p.set_status("exposing");
                writer->Write(p);
            },
            binning, gain);

        astro_mount::ExposureProgress finalMsg;
        finalMsg.set_elapsed_ms(static_cast<int>(total_s * 1000.0));
        finalMsg.set_remaining_ms(0);
        finalMsg.set_progress_percent(100.0);
        if (capture.aborted) {
            finalMsg.set_status("aborted");
        } else {
            finalMsg.set_status(capture.complete ? "complete" : "failed");
            if (!capture.bytes.empty()) {
                auto* img = finalMsg.mutable_image();
                img->set_format(capture.format.empty() ? "fits" : capture.format);
                img->set_data(capture.bytes);
            }
        }
        writer->Write(finalMsg);
        if (capture.aborted) {
            cameraLog()->info("StartExposure aborted by operator");
        } else {
            cameraLog()->info("StartExposure complete: {} bytes", capture.bytes.size());
        }
        return grpc::Status::OK;
    } catch (const std::exception& e) {
        cameraLog()->error("StartExposure failed: {}", e.what());
        astro_mount::ExposureProgress p;
        p.set_status("failed");
        p.set_remaining_ms(0);
        writer->Write(p);
        return grpc::Status(grpc::StatusCode::INTERNAL,
                            std::string("INDI exposure failed: ") + e.what());
    }
}

grpc::Status CameraServiceImpl::AbortExposure(
    grpc::ServerContext*, const google::protobuf::Empty*, google::protobuf::Empty*) {
    if (indi_client_)
        indi_client_->abortCapture();
    return grpc::Status::OK;
}

grpc::Status CameraServiceImpl::StartVideoPreview(
    grpc::ServerContext*, const astro_mount::VideoPreviewRequest*,
    grpc::ServerWriter<astro_mount::VideoFrame>*) {
    return grpc::Status(grpc::StatusCode::UNIMPLEMENTED, "Video preview not supported");
}

grpc::Status CameraServiceImpl::StopVideoPreview(
    grpc::ServerContext*, const google::protobuf::Empty*, google::protobuf::Empty*) {
    return grpc::Status::OK;
}

grpc::Status CameraServiceImpl::SetCooler(
    grpc::ServerContext*, const astro_mount::SetCoolerRequest* req,
    astro_mount::CoolerStatus* status) {
    ensureIndiServerConnected();
    if (!indiCameraActive())
        return grpc::Status(grpc::StatusCode::FAILED_PRECONDITION, "No INDI camera selected");

    try {
        indi_client_->setCooler(req->target_c(), req->enabled());
        const auto cooler = indi_client_->getCooler();
        status->set_enabled(cooler.enabled);
        status->set_target_c(cooler.target_c);
        status->set_current_c(cooler.current_c);
        status->set_power_percent(cooler.power_percent);
        status->set_reached_target(cooler.reached_target);
        return grpc::Status::OK;
    } catch (const std::exception& e) {
        return grpc::Status(grpc::StatusCode::INTERNAL,
                            std::string("SetCooler through INDI failed: ") + e.what());
    }
}

grpc::Status CameraServiceImpl::GetCoolerStatus(
    grpc::ServerContext*, const google::protobuf::Empty*, astro_mount::CoolerStatus* status) {
    ensureIndiServerConnected();
    if (!indiCameraActive())
        return grpc::Status(grpc::StatusCode::FAILED_PRECONDITION, "No INDI camera selected");

    const auto cooler = indi_client_->getCooler();
    status->set_enabled(cooler.enabled);
    status->set_target_c(cooler.target_c);
    status->set_current_c(cooler.current_c);
    status->set_power_percent(cooler.power_percent);
    status->set_reached_target(cooler.reached_target);
    return grpc::Status::OK;
}

grpc::Status CameraServiceImpl::ListCameras(
    grpc::ServerContext*, const google::protobuf::Empty*, astro_mount::CameraDeviceList* response) {
    ensureIndiServerConnected();

    if (indi_client_ && indi_client_->isServerConnected()) {
        const auto cameras = indi_client_->listCameras();
        cameraLog()->info("ListCameras: {} device(s) found on INDI server", cameras.size());
        for (const auto& name : cameras) {
            auto* dev = response->add_devices();
            dev->set_name(name);
            dev->set_label(name);
        }
        response->set_active_device(indi_client_->activeDevice());
    } else {
        cameraLog()->warn("ListCameras: not connected to INDI server");
    }
    return grpc::Status::OK;
}

grpc::Status CameraServiceImpl::SelectCamera(
    grpc::ServerContext*, const astro_mount::CameraSelectRequest* request,
    google::protobuf::Empty*) {
    if (!indi_client_)
        return grpc::Status(grpc::StatusCode::INTERNAL, "Camera service not initialised");

    if (request->device().empty()) {
        // Empty device → deselect the camera.
        indi_client_->selectDevice(std::string{});
        return grpc::Status::OK;
    }

    try {
        cameraLog()->info("SelectCamera: connecting to device '{}'", request->device());
        if (!indi_client_->selectDevice(request->device()))
            return grpc::Status(grpc::StatusCode::UNAVAILABLE,
                                "Camera driver did not connect (hardware may be busy or disconnected)");
        cameraLog()->info("SelectCamera: device '{}' connected", request->device());
    } catch (const std::exception& e) {
        cameraLog()->error("SelectCamera failed: {}", e.what());
        return grpc::Status(grpc::StatusCode::INTERNAL, e.what());
    }
    return grpc::Status::OK;
}

grpc::Status CameraServiceImpl::ListProperties(
    grpc::ServerContext*, const google::protobuf::Empty*, astro_mount::CameraPropertyList* response) {
    ensureIndiServerConnected();
    if (!indiCameraActive())
        return grpc::Status(grpc::StatusCode::FAILED_PRECONDITION, "No INDI camera selected");

    for (const auto& prop : indi_client_->listProperties()) {
        auto* p = response->add_properties();
        p->set_name(prop.name);
        p->set_label(prop.label);
        p->set_group(prop.group);
        p->set_type(prop.type);
        p->set_writable(prop.writable);
        for (const auto& el : prop.elements) {
            auto* e = p->add_elements();
            e->set_name(el.name);
            e->set_label(el.label);
            e->set_number_value(el.number_value);
            e->set_number_min(el.number_min);
            e->set_number_max(el.number_max);
            e->set_number_step(el.number_step);
            e->set_switch_state(el.switch_state);
            e->set_text_value(el.text_value);
        }
    }
    return grpc::Status::OK;
}

grpc::Status CameraServiceImpl::SetProperty(
    grpc::ServerContext*, const astro_mount::SetCameraPropertyRequest* request,
    google::protobuf::Empty*) {
    ensureIndiServerConnected();
    if (!indiCameraActive())
        return grpc::Status(grpc::StatusCode::FAILED_PRECONDITION, "No INDI camera selected");

    try {
        indi_client_->setProperty(request->property(), request->element(), request->value());
    } catch (const std::exception& e) {
        return grpc::Status(grpc::StatusCode::INTERNAL, e.what());
    }
    return grpc::Status::OK;
}

} // namespace astro_camera
