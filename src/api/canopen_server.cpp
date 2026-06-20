#include "api/canopen_server.h"
#include "proto/canopen_service.grpc.pb.h"
#include "proto/canopen_service.pb.h"
#include "controllers/canopen_factory.h"
#include "logging/logger.h"
#include <google/protobuf/util/time_util.h>
#include <grpcpp/server_builder.h>
#include <memory>
#include <chrono>
#include <iostream>

using google::protobuf::util::TimeUtil;

namespace astro_mount {
namespace canopen {

// Helper function to convert system_clock::time_point to protobuf Timestamp
inline google::protobuf::Timestamp TimePointToTimestamp(
    const std::chrono::system_clock::time_point& tp) {
    auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(
        tp.time_since_epoch()).count();
    return TimeUtil::MillisecondsToTimestamp(milliseconds);
}

// Internal service implementation
class CanOpenServer::ServiceImpl final 
    : public astro_mount::canopen::CanOpenService::Service {
public:
    explicit ServiceImpl(std::unique_ptr<controllers::ICanOpenInterface> interface)
        : canopen_interface_(std::move(interface)) {
        // Defer initialization — the caller is responsible for calling
        // initialize() on the ICanOpenInterface before passing it here.
        // This avoids hardcoding "mock" and allows real CANopen hardware.
    }

    // Connection management
    grpc::Status Connect(grpc::ServerContext* context,
                        const ConnectionRequest* request,
                        ConnectionResponse* response) override {
        try {
            controllers::ICanOpenInterface::Config config;
            config.library = request->library();
            config.interface_name = request->interface_name();
            config.bitrate = request->bitrate();
            config.node_id = request->node_id();
            config.use_sync = request->use_sync();
            config.sync_period_ms = request->sync_period_ms();
            // Copy PDO mapping (max 4 entries)
            int i = 0;
            for (const auto& mapping : request->pdo_mapping()) {
                if (i >= 4) break;
                try {
                    config.pdo_mapping[i] = std::stoi(mapping, nullptr, 0);
                } catch (...) {
                    config.pdo_mapping[i] = 0;
                }
                ++i;
            }
            // Fill remaining slots with 0
            for (; i < 4; ++i) {
                config.pdo_mapping[i] = 0;
            }
            config.sdo_timeout_ms = request->sdo_timeout_ms();
            
            if (canopen_interface_->initialize(config)) {
                response->set_success(true);
                response->set_message("CANopen interface initialized");
            } else {
                response->set_success(false);
                response->set_message("Failed to initialize CANopen interface");
            }
        } catch (const std::exception& e) {
            response->set_success(false);
            response->set_message(std::string("Error: ") + e.what());
        }
        return grpc::Status::OK;
    }

    grpc::Status Disconnect(grpc::ServerContext* context,
                           const google::protobuf::Empty* request,
                           google::protobuf::Empty* response) override {
        canopen_interface_->disconnect();
        return grpc::Status::OK;
    }

    grpc::Status IsConnected(grpc::ServerContext* context,
                            const google::protobuf::Empty* request,
                            ConnectionStatus* response) override {
        response->set_connected(canopen_interface_->isConnected());
        *response->mutable_timestamp() = TimeUtil::GetCurrentTime();
        return grpc::Status::OK;
    }

    // Axis control
    grpc::Status ConfigureAxis(grpc::ServerContext* context,
                              const AxisConfigRequest* request,
                              OperationResult* response) override {
        bool success = canopen_interface_->configureDrive(
            request->axis_id(), request->config_string());
        response->set_success(success);
        response->set_message(success ? "Axis configured" : "Failed to configure axis");
        *response->mutable_timestamp() = TimeUtil::GetCurrentTime();
        return grpc::Status::OK;
    }

    grpc::Status EnableAxis(grpc::ServerContext* context,
                           const AxisControlRequest* request,
                           OperationResult* response) override {
        bool success = canopen_interface_->enableDrive(request->axis_id());
        response->set_success(success);
        response->set_message(success ? "Axis enabled" : "Failed to enable axis");
        *response->mutable_timestamp() = TimeUtil::GetCurrentTime();
        return grpc::Status::OK;
    }

    grpc::Status DisableAxis(grpc::ServerContext* context,
                            const AxisControlRequest* request,
                            OperationResult* response) override {
        canopen_interface_->disableDrive(request->axis_id());
        response->set_success(true);
        response->set_message("Axis disabled");
        *response->mutable_timestamp() = TimeUtil::GetCurrentTime();
        return grpc::Status::OK;
    }

    grpc::Status SetPositionTarget(grpc::ServerContext* context,
                                  const PositionTargetRequest* request,
                                  OperationResult* response) override {
        bool success = canopen_interface_->setPositionTarget(
            request->axis_id(), request->position(), 
            request->velocity(), request->acceleration());
        response->set_success(success);
        response->set_message(success ? "Position target set" : "Failed to set position target");
        *response->mutable_timestamp() = TimeUtil::GetCurrentTime();
        return grpc::Status::OK;
    }

    grpc::Status SetVelocityTarget(grpc::ServerContext* context,
                                  const VelocityTargetRequest* request,
                                  OperationResult* response) override {
        bool success = canopen_interface_->setVelocityTarget(
            request->axis_id(), request->velocity(), request->acceleration());
        response->set_success(success);
        response->set_message(success ? "Velocity target set" : "Failed to set velocity target");
        *response->mutable_timestamp() = TimeUtil::GetCurrentTime();
        return grpc::Status::OK;
    }

    grpc::Status StopAxis(grpc::ServerContext* context,
                         const AxisControlRequest* request,
                         OperationResult* response) override {
        canopen_interface_->stopAxis(request->axis_id());
        response->set_success(true);
        response->set_message("Axis stopped");
        *response->mutable_timestamp() = TimeUtil::GetCurrentTime();
        return grpc::Status::OK;
    }

    grpc::Status EmergencyStop(grpc::ServerContext* context,
                              const AxisControlRequest* request,
                              OperationResult* response) override {
        canopen_interface_->emergencyStop(request->axis_id());
        response->set_success(true);
        response->set_message("Emergency stop executed");
        *response->mutable_timestamp() = TimeUtil::GetCurrentTime();
        return grpc::Status::OK;
    }

    // Status queries
    grpc::Status GetAxisStatus(grpc::ServerContext* context,
                              const AxisControlRequest* request,
                              AxisStatus* response) override {
        auto status = canopen_interface_->getDriveStatus(request->axis_id());
        response->set_axis_id(request->axis_id());
        response->set_operational(status.operational);
        response->set_enabled(status.enabled);
        response->set_moving(status.moving);
        response->set_target_reached(status.target_reached);
        response->set_status_word(status.status_word);
        response->set_error_code(status.error_code);
        *response->mutable_timestamp() = TimePointToTimestamp(status.timestamp);
        return grpc::Status::OK;
    }

    grpc::Status GetPositionData(grpc::ServerContext* context,
                                const AxisControlRequest* request,
                                PositionData* response) override {
        auto data = canopen_interface_->getPositionData(request->axis_id());
        response->set_axis_id(request->axis_id());
        response->set_actual_position(data.actual_position);
        response->set_actual_velocity(data.actual_velocity);
        response->set_actual_torque(data.actual_torque);
        response->set_target_position(data.target_position);
        response->set_following_error(data.following_error);
        *response->mutable_timestamp() = TimePointToTimestamp(data.timestamp);
        return grpc::Status::OK;
    }

    grpc::Status GetEncoderData(grpc::ServerContext* context,
                               const AxisControlRequest* request,
                               EncoderData* response) override {
        auto data = canopen_interface_->getEncoderData(request->axis_id());
        response->set_axis_id(request->axis_id());
        response->set_raw_position(data.raw_position);
        response->set_raw_velocity(data.raw_velocity);
        response->set_index_pulse(data.index_pulse);
        response->set_direction(data.direction);
        response->set_error_count(data.error_count);
        *response->mutable_timestamp() = TimePointToTimestamp(data.timestamp);
        return grpc::Status::OK;
    }

    // CANopen protocol operations
    grpc::Status SendSDO(grpc::ServerContext* context,
                        const SDORequest* request,
                        OperationResult* response) override {
        bool success = canopen_interface_->sendSDO(
            request->axis_id(), request->index(), request->subindex(),
            request->data().data(), request->data().size());
        response->set_success(success);
        response->set_message(success ? "SDO sent" : "Failed to send SDO");
        *response->mutable_timestamp() = TimeUtil::GetCurrentTime();
        return grpc::Status::OK;
    }

    grpc::Status ReceiveSDO(grpc::ServerContext* context,
                           const SDORequest* request,
                           SDOResponse* response) override {
        // Create buffer for received data
        const size_t buffer_size = 256;
        uint8_t buffer[buffer_size];
        
        int received = canopen_interface_->receiveSDO(
            request->axis_id(), request->index(), request->subindex(),
            buffer, buffer_size);
        
        response->set_success(received >= 0);
        response->set_data_size(received);
        if (received > 0) {
            response->set_data(buffer, received);
        }
        return grpc::Status::OK;
    }

    grpc::Status ConfigurePDO(grpc::ServerContext* context,
                             const PDOConfigRequest* request,
                             OperationResult* response) override {
        bool success = canopen_interface_->configurePDO(
            request->axis_id(), request->pdo_number(),
            {request->mapping().begin(), request->mapping().end()});
        response->set_success(success);
        response->set_message(success ? "PDO configured" : "Failed to configure PDO");
        *response->mutable_timestamp() = TimeUtil::GetCurrentTime();
        return grpc::Status::OK;
    }

    grpc::Status EnablePDO(grpc::ServerContext* context,
                          const PDOControlRequest* request,
                          OperationResult* response) override {
        canopen_interface_->enablePDO(
            request->axis_id(), request->pdo_number(), request->enable());
        response->set_success(true);
        response->set_message("PDO control applied");
        *response->mutable_timestamp() = TimeUtil::GetCurrentTime();
        return grpc::Status::OK;
    }

    grpc::Status SendSync(grpc::ServerContext* context,
                         const google::protobuf::Empty* request,
                         OperationResult* response) override {
        canopen_interface_->sendSync();
        response->set_success(true);
        response->set_message("SYNC message sent");
        *response->mutable_timestamp() = TimeUtil::GetCurrentTime();
        return grpc::Status::OK;
    }

    // Statistics and configuration
    grpc::Status GetStatistics(grpc::ServerContext* context,
                              const google::protobuf::Empty* request,
                              Statistics* response) override {
        response->set_implementation_type(canopen_interface_->getImplementationType());
        response->set_supports_simulation(canopen_interface_->supportsSimulation());
        response->set_axis_count(3); // Mock implementation has 3 axes
        *response->mutable_timestamp() = TimeUtil::GetCurrentTime();
        return grpc::Status::OK;
    }

    grpc::Status SaveConfiguration(grpc::ServerContext* context,
                                  const ConfigurationRequest* request,
                                  OperationResult* response) override {
        bool success = canopen_interface_->saveConfiguration(request->filename());
        response->set_success(success);
        response->set_message(success ? "Configuration saved" : "Failed to save configuration");
        *response->mutable_timestamp() = TimeUtil::GetCurrentTime();
        return grpc::Status::OK;
    }

    grpc::Status LoadConfiguration(grpc::ServerContext* context,
                                  const ConfigurationRequest* request,
                                  OperationResult* response) override {
        bool success = canopen_interface_->loadConfiguration(request->filename());
        response->set_success(success);
        response->set_message(success ? "Configuration loaded" : "Failed to load configuration");
        *response->mutable_timestamp() = TimeUtil::GetCurrentTime();
        return grpc::Status::OK;
    }

    // Trajectory operations (mock implementation)
    grpc::Status GenerateTrajectory(grpc::ServerContext* context,
                                   const TrajectoryParams* request,
                                   Trajectory* response) override {
        // Mock implementation for trajectory generation
        controllers::ICanOpenInterface::TrajectoryParams params;
        params.type = static_cast<controllers::ICanOpenInterface::TrajectoryType>(request->type());
        params.max_velocity = request->max_velocity();
        params.max_acceleration = request->max_acceleration();
        params.max_jerk = request->max_jerk();
        params.start_position = request->start_position();
        params.target_position = request->target_position();
        params.update_rate = request->update_rate();
        
        auto trajectory = canopen_interface_->generateTrajectory(params);
        
        response->set_axis_id(0); // Default axis
        for (const auto& point : trajectory) {
            auto* trajectory_point = response->add_points();
            trajectory_point->set_position(point.position);
            trajectory_point->set_velocity(point.velocity);
            trajectory_point->set_acceleration(point.acceleration);
            trajectory_point->set_jerk(point.jerk);
            trajectory_point->set_time(point.time);
        }
        
        return grpc::Status::OK;
    }

    grpc::Status ExecuteTrajectory(grpc::ServerContext* context,
                                  const TrajectoryExecutionRequest* request,
                                  OperationResult* response) override {
        // Convert trajectory points
        std::vector<controllers::ICanOpenInterface::TrajectoryPoint> trajectory;
        for (const auto& point : request->trajectory()) {
            controllers::ICanOpenInterface::TrajectoryPoint tp;
            tp.position = point.position();
            tp.velocity = point.velocity();
            tp.acceleration = point.acceleration();
            tp.jerk = point.jerk();
            tp.time = point.time();
            trajectory.push_back(tp);
        }
        
        bool success = canopen_interface_->executeTrajectory(
            request->axis_id(), trajectory);
        
        response->set_success(success);
        response->set_message(success ? "Trajectory execution started" : "Failed to execute trajectory");
        *response->mutable_timestamp() = TimeUtil::GetCurrentTime();
        return grpc::Status::OK;
    }

private:
    std::unique_ptr<controllers::ICanOpenInterface> canopen_interface_;
};

// CanOpenServer implementation
CanOpenServer::CanOpenServer(const std::string& address, int port,
                           std::unique_ptr<controllers::ICanOpenInterface> canopen_interface)
    : address_(address)
    , port_(port)
    , running_(false) {
    service_impl_ = std::make_unique<ServiceImpl>(std::move(canopen_interface));
}

CanOpenServer::~CanOpenServer() {
    stop();
}

bool CanOpenServer::start() {
    if (running_) {
        API_LOG_WARN("CANopen server already running");
        return true;
    }

    try {
        std::string server_address = address_ + ":" + std::to_string(port_);
        grpc::ServerBuilder builder;
        builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
        builder.RegisterService(service_impl_.get());
        
        server_ = builder.BuildAndStart();
        if (!server_) {
            API_LOG_ERROR("Failed to start CANopen server on {}", server_address);
            return false;
        }
        
        running_ = true;
        API_LOG_INFO("CANopen server started on {}", server_address);
        return true;
        
    } catch (const std::exception& e) {
        API_LOG_ERROR("Failed to start CANopen server: {}", e.what());
        return false;
    }
}

void CanOpenServer::stop() {
    if (running_ && server_) {
        server_->Shutdown();
        server_->Wait();
        running_ = false;
        API_LOG_INFO("CANopen server stopped");
    }
}

std::string CanOpenServer::getAddress() const {
    return address_ + ":" + std::to_string(port_);
}

bool CanOpenServer::isRunning() const {
    return running_;
}

} // namespace canopen
} // namespace astro_mount