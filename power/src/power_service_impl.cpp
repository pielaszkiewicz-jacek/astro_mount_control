#include "power/include/power_service_impl.h"
#include "controllers/power_manager.h"
#include "hal/power_control.h"
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include <google/protobuf/util/time_util.h>

namespace astro_power {

using astro_mount::controllers::PowerManager;
using astro_mount::hal::PowerControl;
using astro_mount::hal::SimulatedPower;
using astro_mount::hal::PowerData;

PowerServiceImpl::PowerServiceImpl(const std::string& config_path)
    : config_path_(config_path) {
    try {
        if (!configureFromJson(config_path)) {
            std::cerr << "[PowerServiceImpl] Config load failed, using simulated\n";
            auto hal = std::make_unique<SimulatedPower>();
            hal->initialize();
            manager_ = std::make_unique<PowerManager>(std::move(hal));
        }
        if (manager_) {
            manager_->start();
            initialized_ = true;
        }
    } catch (const std::exception& e) {
        std::cerr << "[PowerServiceImpl] Init error: " << e.what() << "\n";
    }
}

PowerServiceImpl::~PowerServiceImpl() {
    if (manager_) manager_->stop();
}

bool PowerServiceImpl::configureFromJson(const std::string& config_path) {
    std::ifstream file(config_path);
    if (!file.is_open()) return false;

    nlohmann::json config;
    file >> config;

    std::string hal_type = config.value("hal_type", "simulated");
    std::unique_ptr<PowerControl> hal;

    if (hal_type == "simulated" || hal_type.empty()) {
        hal = std::make_unique<SimulatedPower>();
    } else {
        std::cerr << "[PowerServiceImpl] Unknown HAL type '" << hal_type << "', using simulated\n";
        hal = std::make_unique<SimulatedPower>();
    }

    if (!hal->initialize()) {
        std::cerr << "[PowerServiceImpl] Failed to initialise power HAL\n";
        return false;
    }

    manager_ = std::make_unique<PowerManager>(std::move(hal));

    if (config.contains("low_voltage_threshold")) {
        manager_->setLowVoltageThreshold(config["low_voltage_threshold"].get<double>());
    }

    return true;
}

void PowerServiceImpl::populatePowerStatus(astro_mount::PowerStatus* status) const {
    if (!initialized_ || !manager_) {
        status->set_voltage_v(0);
        status->set_charge_percent(0);
        return;
    }

    auto data = manager_->getStatus();

    status->set_voltage_v(data.voltage_v);
    status->set_current_a(data.current_a);
    status->set_power_w(data.power_w);
    status->set_capacity_ah(data.capacity_ah);
    status->set_charge_percent(data.charge_percent);
    status->set_charging(data.charging);
    status->set_on_battery(data.on_battery);
    status->set_temperature_c(data.temperature_c);
    status->set_estimated_runtime_min(data.estimated_runtime_min);
    status->set_input_voltage_v(data.input_voltage_v);
    status->set_output_voltage_v(data.output_voltage_v);

    for (const auto& out : data.outputs) {
        auto* po = status->add_outputs();
        po->set_id(out.id);
        po->set_name(out.name);
        po->set_enabled(out.enabled);
        po->set_voltage_v(out.voltage_v);
        po->set_current_a(out.current_a);
        po->set_overload(out.overload);
    }

    auto ts = google::protobuf::util::TimeUtil::SecondsToTimestamp(
        std::chrono::duration_cast<std::chrono::seconds>(
            data.timestamp.time_since_epoch()).count());
    *status->mutable_timestamp() = ts;
}

grpc::Status PowerServiceImpl::GetPowerStatus(
    grpc::ServerContext* context,
    const google::protobuf::Empty* request,
    astro_mount::PowerStatus* response) {
    std::lock_guard<std::mutex> lock(mutex_);
    populatePowerStatus(response);
    return grpc::Status::OK;
}

grpc::Status PowerServiceImpl::SetPowerOutput(
    grpc::ServerContext* context,
    const astro_mount::PowerOutputRequest* request,
    google::protobuf::Empty* response) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!initialized_ || !manager_)
        return grpc::Status(grpc::StatusCode::UNAVAILABLE, "Power manager not initialised");
    (void)request;
    return grpc::Status(grpc::StatusCode::UNIMPLEMENTED,
                        "Output switching not yet implemented");
}

grpc::Status PowerServiceImpl::GetPowerHistory(
    grpc::ServerContext* context,
    const astro_mount::PowerHistoryRequest* request,
    astro_mount::PowerHistoryResponse* response) {
    std::lock_guard<std::mutex> lock(mutex_);
    (void)request;
    response->set_total_points(0);
    return grpc::Status::OK;
}

} // namespace astro_power
