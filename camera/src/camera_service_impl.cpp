#include "camera/include/camera_service_impl.h"
#include <chrono>
#include <thread>
#include <cmath>
#include <algorithm>

namespace astro_camera {

grpc::Status CameraServiceImpl::GetCameraInfo(
    grpc::ServerContext*, const google::protobuf::Empty*, astro_mount::CameraInfo* info) {
    // R3: simulated camera — clearly reported as such.
    info->set_name("Simulated Camera");
    info->set_manufacturer("Simulation");
    info->set_sensor_name("IMX571");
    info->set_width(6248);
    info->set_height(4176);
    info->set_pixel_size_um(3.76);
    info->set_max_bin(4);
    info->set_has_filter_wheel(true);
    info->set_num_filters(5);
    for (const char* f : {"L", "R", "G", "B", "Ha"}) info->add_filter_names(f);
    info->set_has_cooler(true);
    info->set_min_cooling_c(35.0);
    info->set_bit_depth(16.0);
    info->set_connected(true);
    info->set_driver_version("simulated-1.0");
    return grpc::Status::OK;
}

grpc::Status CameraServiceImpl::SetFilter(
    grpc::ServerContext*, const astro_mount::FilterRequest* req, google::protobuf::Empty*) {
    std::lock_guard<std::mutex> lock(mtx_);
    filter_position_ = std::max(0, req->position());
    return grpc::Status::OK;
}

grpc::Status CameraServiceImpl::GetFilterPosition(
    grpc::ServerContext*, const google::protobuf::Empty*, astro_mount::FilterPosition* pos) {
    std::lock_guard<std::mutex> lock(mtx_);
    pos->set_current_position(filter_position_);
    pos->set_num_filters(5);
    for (const char* f : {"L", "R", "G", "B", "Ha"}) pos->add_filter_names(f);
    return grpc::Status::OK;
}

grpc::Status CameraServiceImpl::StartExposure(
    grpc::ServerContext* context, const astro_mount::ExposureRequest* req,
    grpc::ServerWriter<astro_mount::ExposureProgress>* writer) {
    // Simulated exposure: stream progress to completion.
    double total_s = std::max(0.1, req->exposure_time_s());
    const int steps = 20;
    for (int i = 0; i <= steps; ++i) {
        if (context->IsCancelled()) return grpc::Status::CANCELLED;
        astro_mount::ExposureProgress p;
        p.set_elapsed_ms(static_cast<int>(i * total_s * 1000.0 / steps));
        p.set_remaining_ms(static_cast<int>((steps - i) * total_s * 1000.0 / steps));
        p.set_progress_percent(100.0 * i / steps);
        p.set_status(i < steps ? "exposing" : "complete");
        p.set_image_size_bytes(0);
        if (!writer->Write(p)) return grpc::Status::CANCELLED;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    return grpc::Status::OK;
}

grpc::Status CameraServiceImpl::AbortExposure(
    grpc::ServerContext*, const google::protobuf::Empty*, google::protobuf::Empty*) {
    return grpc::Status::OK;
}

grpc::Status CameraServiceImpl::StartVideoPreview(
    grpc::ServerContext* context, const astro_mount::VideoPreviewRequest*,
    grpc::ServerWriter<astro_mount::VideoFrame>* writer) {
    // Simulated preview: stream a few empty frames then end.
    const int frames = 5;
    for (int i = 1; i <= frames; ++i) {
        if (context->IsCancelled()) return grpc::Status::CANCELLED;
        astro_mount::VideoFrame frame;
        frame.set_frame_number(i);
        frame.set_width(640);
        frame.set_height(480);
        frame.set_format("jpeg");
        frame.set_data("");  // simulated — no real image data
        if (!writer->Write(frame)) return grpc::Status::CANCELLED;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    return grpc::Status::OK;
}

grpc::Status CameraServiceImpl::StopVideoPreview(
    grpc::ServerContext*, const google::protobuf::Empty*, google::protobuf::Empty*) {
    return grpc::Status::OK;
}

// Simulated cooler: a simple 1st-order approach to the target temperature.
// Power ramps toward 100% while the sensor is above target, then decays as the
// sensor approaches it. When disabled the sensor drifts back toward ambient.
static void updateCoolerSimulation(bool enabled, double target_c,
                                   double& current_c, double& ambient_c,
                                   double& power_percent) {
    if (!enabled) {
        power_percent = 0.0;
        current_c += (ambient_c - current_c) * 0.05;
        return;
    }
    double delta = target_c - current_c;  // negative when cooling is needed
    if (delta < 0.0) {
        // Cooling down: powered, most aggressive while far from target.
        power_percent = std::min(100.0, std::abs(delta) * 8.0 + 25.0);
        current_c += delta * 0.10;  // approaches target_c
    } else {
        // Warming toward target (passive, slower).
        power_percent = std::max(0.0, 25.0 - std::abs(delta) * 3.0);
        current_c += delta * 0.04;
    }
}

grpc::Status CameraServiceImpl::SetCooler(
    grpc::ServerContext*, const astro_mount::SetCoolerRequest* req,
    astro_mount::CoolerStatus* status) {
    std::lock_guard<std::mutex> lock(mtx_);
    cooler_enabled_ = req->enabled();
    cooler_target_c_ = req->target_c();
    if (cooler_enabled_) {
        // A few simulated cooling steps toward the target.
        for (int i = 0; i < 30 && std::abs(cooler_target_c_ - cooler_current_c_) > 0.3; ++i) {
            updateCoolerSimulation(true, cooler_target_c_, cooler_current_c_,
                                   cooler_ambient_c_, cooler_power_percent_);
        }
    }
    updateCoolerSimulation(cooler_enabled_, cooler_target_c_, cooler_current_c_,
                           cooler_ambient_c_, cooler_power_percent_);
    status->set_enabled(cooler_enabled_);
    status->set_target_c(cooler_target_c_);
    status->set_current_c(cooler_current_c_);
    status->set_power_percent(cooler_power_percent_);
    status->set_reached_target(std::abs(cooler_target_c_ - cooler_current_c_) < 0.5);
    status->set_ambient_c(cooler_ambient_c_);
    return grpc::Status::OK;
}

grpc::Status CameraServiceImpl::GetCoolerStatus(
    grpc::ServerContext*, const google::protobuf::Empty*, astro_mount::CoolerStatus* status) {
    std::lock_guard<std::mutex> lock(mtx_);
    updateCoolerSimulation(cooler_enabled_, cooler_target_c_, cooler_current_c_,
                           cooler_ambient_c_, cooler_power_percent_);
    status->set_enabled(cooler_enabled_);
    status->set_target_c(cooler_target_c_);
    status->set_current_c(cooler_current_c_);
    status->set_power_percent(cooler_power_percent_);
    status->set_reached_target(std::abs(cooler_target_c_ - cooler_current_c_) < 0.5);
    status->set_ambient_c(cooler_ambient_c_);
    return grpc::Status::OK;
}

} // namespace astro_camera
