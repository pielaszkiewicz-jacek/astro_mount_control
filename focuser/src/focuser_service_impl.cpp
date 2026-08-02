#include "focuser/include/focuser_service_impl.h"
#include "hal/focuser_control.h"
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include <chrono>
#include <thread>
#include <cmath>
#include <random>

namespace astro_focuser {

using astro_mount::hal::FocuserControl;
using astro_mount::hal::FocuserStatus;

// ── Simulated focuser (inline, used when no real HAL is configured) ────

class SimulatedFocuser : public FocuserControl {
public:
    explicit SimulatedFocuser(int32_t max_pos = 100000) : max_position_(max_pos) {}
    std::string name() const override { return "simulated"; }
    bool connect() override { connected_ = true; position_ = max_position_ / 2; return true; }
    void disconnect() override { connected_ = false; }
    bool isConnected() const override { return connected_; }
    bool moveTo(int32_t position, int32_t speed, bool synchronous) override {
        if (!connected_ || position < 0 || position > max_position_) return false;
        moving_ = true;
        if (synchronous) {
            int32_t dist = std::abs(position - position_);
            std::this_thread::sleep_for(std::chrono::milliseconds(dist * 10 / std::max(speed, 1)));
        }
        position_ = position; moving_ = false;
        return true;
    }
    void halt() override { moving_ = false; }
    FocuserStatus getStatus() override {
        FocuserStatus s;
        s.position = position_; s.max_position = max_position_;
        s.moving = moving_; s.connected = connected_;
        s.temperature_c = temperature_; s.model_name = "Simulated Focuser";
        s.step_size_microns = 10;
        return s;
    }
    double readTemperature() override {
        static std::default_random_engine gen(std::random_device{}());
        std::normal_distribution<double> noise(0.0, 0.1);
        temperature_ += noise(gen) * 0.1;
        return temperature_;
    }
    bool initialize() override { connected_ = true; position_ = max_position_ / 2; return true; }
    bool isReady() const override { return connected_; }
    int32_t getMaxPosition() const override { return max_position_; }
private:
    int32_t max_position_, position_{0}, target_position_{0};
    bool moving_{false}, connected_{false};
    double temperature_{20.0};
};

// ── Service Implementation ───────────────────────────────────────────

FocuserServiceImpl::FocuserServiceImpl(const std::string& config_path) {
    try {
        if (!configureFromJson(config_path)) {
            std::cerr << "[FocuserServiceImpl] Using simulated focuser\n";
            focuser_ = std::make_unique<SimulatedFocuser>();
        }
        if (focuser_) { focuser_->connect(); focuser_->initialize(); initialized_ = true; }
    } catch (const std::exception& e) {
        std::cerr << "[FocuserServiceImpl] Error: " << e.what() << "\n";
    }
}

FocuserServiceImpl::~FocuserServiceImpl() { if (focuser_) focuser_->disconnect(); }

bool FocuserServiceImpl::configureFromJson(const std::string& config_path) {
    std::ifstream file(config_path);
    if (!file.is_open()) return false;
    nlohmann::json config; file >> config;
    simulated_ = config.value("simulated", true);
    std::string hal_type = config.value("hal_type", "simulated");

    if (hal_type == "zwo" || hal_type == "moonlite" || hal_type == "pegasus") {
        std::cerr << "[Focuser] " << hal_type << " HAL not yet implemented, using simulated\n";
    }
    focuser_ = std::make_unique<SimulatedFocuser>(
        config.value("max_position", 100000));

    if (config.contains("temp_compensation")) {
        auto& tc = config["temp_compensation"];
        temp_comp_.enabled = tc.value("enabled", false);
        temp_comp_.coefficient = tc.value("coefficient", 0.0);
        temp_comp_.max_adjustment = tc.value("max_adjustment", 500);
    }
    return true;
}

void FocuserServiceImpl::populateStatus(astro_mount::FocuserStatus* status) const {
    if (!initialized_ || !focuser_) { status->set_connected(false); return; }
    auto s = focuser_->getStatus();
    status->set_position(s.position);
    status->set_max_position(s.max_position);
    status->set_moving(s.moving);
    status->set_connected(s.connected);
    status->set_temperature_c(s.temperature_c);
    status->set_hfd(s.hfd);
    status->set_model_name(s.model_name);
    status->set_error_message(s.error_message);
    status->set_step_size_microns(s.step_size_microns);
}

grpc::Status FocuserServiceImpl::MoveFocuser(
    grpc::ServerContext*, const astro_mount::FocuserMoveRequest* req, google::protobuf::Empty*) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!initialized_ || !focuser_)
        return grpc::Status(grpc::StatusCode::UNAVAILABLE, "Focuser not initialised");
    if (!focuser_->moveTo(req->position(), req->speed(), req->synchronous()))
        return grpc::Status(grpc::StatusCode::INTERNAL, "Move failed");
    return grpc::Status::OK;
}

grpc::Status FocuserServiceImpl::HaltFocuser(
    grpc::ServerContext*, const google::protobuf::Empty*, google::protobuf::Empty*) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (focuser_) focuser_->halt();
    return grpc::Status::OK;
}

grpc::Status FocuserServiceImpl::GetFocuserPosition(
    grpc::ServerContext*, const google::protobuf::Empty*, astro_mount::FocuserStatus* r) {
    std::lock_guard<std::mutex> lock(mutex_); populateStatus(r);
    return grpc::Status::OK;
}

grpc::Status FocuserServiceImpl::RunAutoFocus(
    grpc::ServerContext*, const astro_mount::AutoFocusRequest* req,
    grpc::ServerWriter<astro_mount::AutoFocusProgress>* writer) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!initialized_ || !focuser_)
        return grpc::Status(grpc::StatusCode::UNAVAILABLE, "Focuser not initialised");

    int start = req->start_position() > 0 ? req->start_position() : 0;
    int end = req->end_position() > 0 ? req->end_position() : focuser_->getMaxPosition();
    int step = req->step_size() > 0 ? req->step_size() : 100;
    int total = (end - start) / step;
    int best_pos = start;
    double best_hfd = 999.0;

    for (int i = 0; i <= total; ++i) {
        int pos = start + i * step;
        focuser_->moveTo(pos, 50, true);
        std::this_thread::sleep_for(std::chrono::milliseconds(200));

        astro_mount::AutoFocusProgress prog;
        prog.set_current_step(i + 1);
        prog.set_total_steps(total + 1);
        prog.set_position(pos);
        // Simulated V-curve
        int p0 = focuser_->getMaxPosition() / 2;
        prog.set_hfd(2.0 + 0.001 * std::pow(pos - p0, 2));
        prog.set_temperature_c(focuser_->readTemperature());
        prog.set_status("scanning");
        if (prog.hfd() < best_hfd) { best_hfd = prog.hfd(); best_pos = pos; }
        prog.set_best_position(best_pos);
        prog.set_best_hfd(best_hfd);
        if (!writer->Write(prog)) break;
    }
    astro_mount::AutoFocusProgress done;
    done.set_status("complete"); done.set_best_position(best_pos); done.set_best_hfd(best_hfd);
    writer->Write(done);
    focuser_->moveTo(best_pos, 50, true);
    return grpc::Status::OK;
}

grpc::Status FocuserServiceImpl::SetTemperatureCompensation(
    grpc::ServerContext*, const astro_mount::TempCompConfig* req, google::protobuf::Empty*) {
    std::lock_guard<std::mutex> lock(mutex_);
    temp_comp_.enabled = req->enabled();
    temp_comp_.coefficient = req->coefficient();
    temp_comp_.max_adjustment = req->max_adjustment();
    if (focuser_) temp_comp_.reference_temp = focuser_->readTemperature();
    return grpc::Status::OK;
}

grpc::Status FocuserServiceImpl::GetTempCompensationStatus(
    grpc::ServerContext*, const google::protobuf::Empty*, astro_mount::TempCompStatus* r) {
    std::lock_guard<std::mutex> lock(mutex_);
    r->set_enabled(temp_comp_.enabled);
    r->set_coefficient(temp_comp_.coefficient);
    r->set_current_temp_c(focuser_ ? focuser_->readTemperature() : 20.0);
    r->set_reference_temp_c(temp_comp_.reference_temp);
    r->set_total_adjustment(temp_comp_.total_adjustment);
    r->set_max_adjustment(temp_comp_.max_adjustment);
    return grpc::Status::OK;
}

} // namespace astro_focuser
