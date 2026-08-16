#include "pulley/include/pulley_service_impl.h"
#include <chrono>
#include <thread>
#include <cmath>
#include <algorithm>

namespace astro_pulley {

// Advance the tracked position toward the target at a rate proportional to the
// configured speed (100% speed → 25%/s for a ~4 s full travel).
void PulleyServiceImpl::integratePosition() const {
    auto now = std::chrono::steady_clock::now();
    double dt = std::chrono::duration<double>(now - last_update_).count();
    if (dt <= 0.0 || dt > 2.0) { last_update_ = now; return; }
    if (position_percent_ == target_percent_) { last_update_ = now; return; }

    double rate_per_s = 25.0 * speed_percent_ / 100.0;  // %/s
    double step = rate_per_s * dt;
    int delta = target_percent_ - position_percent_;
    if (std::abs(delta) <= step) position_percent_ = target_percent_;
    else position_percent_ += static_cast<int>(std::copysign(step, delta));
    last_update_ = now;
}

grpc::Status PulleyServiceImpl::DeployPulley(
    grpc::ServerContext*, const astro_mount::PulleyMoveRequest* req, google::protobuf::Empty*) {
    std::lock_guard<std::mutex> lock(mtx_);
    target_percent_ = std::clamp(req->position_percent(), 0, 100);
    if (req->speed_percent() > 0) speed_percent_ = std::clamp(req->speed_percent(), 1, 100);
    return grpc::Status::OK;
}

grpc::Status PulleyServiceImpl::RetractPulley(
    grpc::ServerContext*, const google::protobuf::Empty*, google::protobuf::Empty*) {
    std::lock_guard<std::mutex> lock(mtx_);
    target_percent_ = 0;
    return grpc::Status::OK;
}

grpc::Status PulleyServiceImpl::StopPulley(
    grpc::ServerContext*, const google::protobuf::Empty*, google::protobuf::Empty*) {
    std::lock_guard<std::mutex> lock(mtx_);
    target_percent_ = position_percent_;
    return grpc::Status::OK;
}

grpc::Status PulleyServiceImpl::SetPulleyPosition(
    grpc::ServerContext*, const astro_mount::PulleyPositionRequest* req, google::protobuf::Empty*) {
    std::lock_guard<std::mutex> lock(mtx_);
    target_percent_ = std::clamp(req->position_percent(), 0, 100);
    if (req->speed_percent() > 0) speed_percent_ = std::clamp(req->speed_percent(), 1, 100);
    return grpc::Status::OK;
}

grpc::Status PulleyServiceImpl::HomePulley(
    grpc::ServerContext* context, const google::protobuf::Empty*,
    grpc::ServerWriter<astro_mount::PulleyHomeProgress>* writer) {
    std::lock_guard<std::mutex> lock(mtx_);
    target_percent_ = 0;
    const int steps = 10;
    for (int i = 1; i <= steps; ++i) {
        if (context->IsCancelled()) return grpc::Status::CANCELLED;
        position_percent_ = static_cast<int>(100.0 * (steps - i) / steps);
        astro_mount::PulleyHomeProgress p;
        p.set_homing(true);
        p.set_phase(i < steps ? "retracting" : "complete");
        p.set_progress_percent(100.0 * i / steps);
        p.set_complete(i == steps);
        if (!writer->Write(p)) return grpc::Status::CANCELLED;
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    position_percent_ = 0;
    homed_ = true;
    return grpc::Status::OK;
}

grpc::Status PulleyServiceImpl::GetPulleyStatus(
    grpc::ServerContext*, const google::protobuf::Empty*, astro_mount::PulleyStatus* status) {
    std::lock_guard<std::mutex> lock(mtx_);
    integratePosition();
    status->set_position_percent(position_percent_);
    status->set_target_position_percent(target_percent_);
    status->set_moving(position_percent_ != target_percent_);
    status->set_deployed(position_percent_ > 5);
    status->set_homed(homed_);
    status->set_speed_percent(speed_percent_);
    status->set_at_upper_limit(position_percent_ >= 100);
    status->set_at_lower_limit(position_percent_ <= 0);
    status->set_overload(false);
    status->set_connected(true);
    status->set_model_name("Simulated Pulley");
    status->set_error(false);
    status->set_max_position_steps(10000);
    status->set_travel_time_s(4.0);
    status->set_deployment_type("generic");
    return grpc::Status::OK;
}

grpc::Status PulleyServiceImpl::SetPulleySpeed(
    grpc::ServerContext*, const astro_mount::PulleySpeedRequest* req, google::protobuf::Empty*) {
    std::lock_guard<std::mutex> lock(mtx_);
    speed_percent_ = std::clamp(req->speed_percent(), 1, 100);
    return grpc::Status::OK;
}

} // namespace astro_pulley
