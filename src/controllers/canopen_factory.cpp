#include "controllers/canopen_factory.h"
#include "controllers/canopen_interface.h"
#include "logging/logger.h"
#include <stdexcept>
#include <algorithm>
#include <thread>
#include <cmath>
#include <chrono>
#include <string>
#include <functional>

namespace astro_mount {
namespace controllers {

// Forward declaration of adapter class
class CanOpenInterfaceAdapter;

std::unique_ptr<ICanOpenInterface> CanOpenFactory::create(const std::string& library_name) {
    ICanOpenInterface::Config config;
    config.library = library_name;
    return create(config);
}

std::unique_ptr<ICanOpenInterface> CanOpenFactory::create(const ICanOpenInterface::Config& config) {
    std::string lib = config.library;
    if (lib.empty()) {
        lib = getDefaultLibrary();
    }

    if (lib == "mock") {
        // Simple test CANopen service with 3 axes
        class TestCanOpenService : public ICanOpenInterface {
        public:
            TestCanOpenService() : connected_(false) {
                // Initialize axes
                for (int i = 0; i < 3; ++i) {
                    axis_position_[i] = 0.0;
                    axis_velocity_[i] = 0.0;
                    axis_enabled_[i] = false;
                    axis_moving_[i] = false;
                }
                axis_names_[0] = "HA Axis";
                axis_names_[1] = "Dec Axis";
                axis_names_[2] = "Field Rotator";
            }
            
            bool initialize(const Config& config) override {
                config_ = config;
                return true;
            }
            
            void shutdown() override {
                disconnect();
            }
            
            bool connect() override { 
                connected_ = true;
                return true; 
            }
            
            void disconnect() override {
                connected_ = false;
                for (int i = 0; i < 3; ++i) {
                    axis_enabled_[i] = false;
                    axis_moving_[i] = false;
                    axis_velocity_[i] = 0.0;
                }
            }
            
            bool isConnected() const override { 
                return connected_; 
            }
            
            bool configureDrive(int axis_id, const std::string& config_string) override {
                if (axis_id < 0 || axis_id >= 3) return false;
                return true;
            }
            
            bool enableDrive(int axis_id) override {
                if (axis_id < 0 || axis_id >= 3) return false;
                axis_enabled_[axis_id] = true;
                return true;
            }
            
            void disableDrive(int axis_id) override {
                if (axis_id < 0 || axis_id >= 3) return;
                axis_enabled_[axis_id] = false;
                axis_moving_[axis_id] = false;
            }
            
            bool setPositionTarget(int axis_id, double position, double velocity, double acceleration) override {
                if (axis_id < 0 || axis_id >= 3) return false;
                if (!axis_enabled_[axis_id]) return false;
                
                // Simple immediate positioning for test
                axis_position_[axis_id] = position;
                axis_velocity_[axis_id] = 0.0;
                axis_moving_[axis_id] = false;
                return true;
            }
            
            bool setVelocityTarget(int axis_id, double velocity, double acceleration) override {
                if (axis_id < 0 || axis_id >= 3) return false;
                if (!axis_enabled_[axis_id]) return false;
                
                axis_velocity_[axis_id] = velocity;
                axis_moving_[axis_id] = (velocity != 0.0);
                return true;
            }
            
            void stopAxis(int axis_id) override {
                if (axis_id < 0 || axis_id >= 3) return;
                axis_moving_[axis_id] = false;
                axis_velocity_[axis_id] = 0.0;
            }
            
            void emergencyStop(int axis_id) override {
                if (axis_id < 0 || axis_id >= 3) return;
                axis_enabled_[axis_id] = false;
                axis_moving_[axis_id] = false;
                axis_velocity_[axis_id] = 0.0;
            }
            
            bool clearErrors(int axis_id) override {
                return true;
            }
            
            DriveStatus getDriveStatus(int axis_id) const override {
                DriveStatus status = {};
                if (axis_id >= 0 && axis_id < 3) {
                    status.operational = connected_;
                    status.enabled = axis_enabled_[axis_id];
                    status.moving = axis_moving_[axis_id];
                    status.target_reached = true;
                    status.timestamp = std::chrono::system_clock::now();
                }
                return status;
            }
            
            PositionData getPositionData(int axis_id) const override {
                PositionData data;
                if (axis_id >= 0 && axis_id < 3) {
                    data.actual_position = axis_position_[axis_id];
                    data.actual_velocity = axis_velocity_[axis_id];
                    data.target_position = axis_position_[axis_id];
                    data.timestamp = std::chrono::system_clock::now();
                }
                return data;
            }
            
            EncoderData getEncoderData(int axis_id) const override {
                EncoderData data;
                if (axis_id >= 0 && axis_id < 3) {
                    data.raw_position = static_cast<uint32_t>(axis_position_[axis_id] * 1000.0);
                    data.raw_velocity = static_cast<int32_t>(axis_velocity_[axis_id] * 1000.0);
                    data.timestamp = std::chrono::system_clock::now();
                }
                return data;
            }
            
            void setStatusCallback(StatusCallback callback) override {
                // Store for simple implementation
            }
            
            void setPositionCallback(PositionCallback callback) override {
                // Store for simple implementation
            }
            
            void setEncoderCallback(EncoderCallback callback) override {
                // Store for simple implementation
            }
            
            void setErrorCallback(ErrorCallback callback) override {
                // Store for simple implementation
            }
            
            bool sendSDO(int axis_id, uint16_t index, uint8_t subindex, 
                         const void* data, size_t data_size) override {
                return true;
            }
            
            int receiveSDO(int axis_id, uint16_t index, uint8_t subindex,
                           void* data, size_t data_size) override {
                return 0;
            }
            
            bool configurePDO(int axis_id, int pdo_number, const std::vector<uint32_t>& mapping) override {
                return true;
            }
            
            void enablePDO(int axis_id, int pdo_number, bool enable) override {
            }
            
            bool sendNMT(uint8_t node_id, uint8_t command) override {
                // Simulate NMT command
                logging::Logger::get("canopen")->info("NMT: Sending command 0x{:02X} to node {}", command, node_id);
                
                // For Reset Node (0x81) - simulate re-initialization
                if (command == 0x81 || command == 0x82) {
                    for (int i = 0; i < 3; ++i) {
                        axis_enabled_[i] = false;
                        axis_moving_[i] = false;
                        axis_velocity_[i] = 0.0;
                        axis_position_[i] = 0.0;
                    }
                }
                
                return true;
            }
            
            void sendSync() override {
            }
            
            std::string getStatistics() const override {
                std::string stats = "Test CANopen Service (3 axes)\n";
                stats += "Connected: " + std::string(connected_ ? "Yes" : "No") + "\n";
                for (int i = 0; i < 3; ++i) {
                    stats += "Axis " + std::to_string(i) + " (" + axis_names_[i] + "): ";
                    stats += axis_enabled_[i] ? "Enabled" : "Disabled";
                    stats += ", Pos: " + std::to_string(axis_position_[i]) + " deg\n";
                }
                return stats;
            }
            
            bool saveConfiguration(const std::string& filename) const override {
                return true;
            }
            
            bool loadConfiguration(const std::string& filename) override {
                return true;
            }
            
            std::string getImplementationType() const override {
                return "test-service";
            }
            
            bool supportsSimulation() const override {
                return true;
            }

            // Trajectory methods
            std::vector<TrajectoryPoint> generateTrajectory(const TrajectoryParams& params) override {
                std::vector<TrajectoryPoint> trajectory;
                double dt = 1.0 / params.update_rate;
                double distance = std::abs(params.target_position - params.start_position);
                double total_time = distance / std::max(0.1, params.max_velocity);
                int steps = static_cast<int>(total_time / dt) + 1;
                
                for (int i = 0; i < steps; ++i) {
                    double t = i * dt;
                    TrajectoryPoint point;
                    point.time = t;
                    double progress = std::min(t / total_time, 1.0);
                    point.position = params.start_position + (params.target_position - params.start_position) * progress;
                    point.velocity = params.max_velocity;
                    point.acceleration = params.max_acceleration;
                    point.jerk = params.max_jerk;
                    trajectory.push_back(point);
                }
                return trajectory;
            }

            bool executeTrajectory(int axis_id, const std::vector<TrajectoryPoint>& trajectory,
                                  std::function<void(const TrajectoryPoint&)> callback = nullptr) override {
                if (axis_id < 0 || axis_id >= 3) return false;
                if (!axis_enabled_[axis_id]) return false;
                
                // Simple execution - just set final position
                if (!trajectory.empty()) {
                    axis_position_[axis_id] = trajectory.back().position;
                }
                return true;
            }

        private:
            Config config_;
            bool connected_;
            double axis_position_[3];
            double axis_velocity_[3];
            bool axis_enabled_[3];
            bool axis_moving_[3];
            std::string axis_names_[3];
        };
        
        return std::make_unique<TestCanOpenService>();
    }
#ifdef HAVE_CANOPEN
    else if (lib == "canopensocket" || lib == "libedssharp" || lib == "canfestival") {
        // Create adapter for existing CanOpenInterface
        class CanOpenInterfaceAdapter : public ICanOpenInterface {
        public:
            CanOpenInterfaceAdapter() : impl_(std::make_unique<CanOpenInterface>()) {}
            
            bool initialize(const Config& config) override {
                // Convert Config to CanOpenInterface::CanOpenConfig
                CanOpenInterface::CanOpenConfig canopen_config;
                canopen_config.interface_name = config.interface_name;
                canopen_config.bitrate = config.bitrate;
                canopen_config.node_id = config.node_id;
                canopen_config.use_sync = config.use_sync;
                canopen_config.sync_period_ms = config.sync_period_ms;
                for (int i = 0; i < 4; ++i) {
                    canopen_config.pdo_mapping[i] = config.pdo_mapping[i];
                }
                canopen_config.sdo_timeout_ms = config.sdo_timeout_ms;
                canopen_config.position_counts_per_degree = config.position_counts_per_degree;
                canopen_config.velocity_counts_per_deg_s = config.velocity_counts_per_deg_s;
                
                config_ = config;
                return impl_->initialize(canopen_config);
            }
            
            void shutdown() override {
                impl_->shutdown();
            }
            
            bool connect() override {
                return impl_->connect();
            }
            
            void disconnect() override {
                impl_->disconnect();
            }
            
            bool isConnected() const override {
                return impl_->isConnected();
            }
            
            bool configureDrive(int axis_id, const std::string& config_string) override {
                return impl_->configureDrive(axis_id, config_string);
            }
            
            bool enableDrive(int axis_id) override {
                return impl_->enableDrive(axis_id);
            }
            
            void disableDrive(int axis_id) override {
                impl_->disableDrive(axis_id);
            }
            
            bool setPositionTarget(int axis_id, double position, double velocity, double acceleration) override {
                return impl_->setPositionTarget(axis_id, position, velocity, acceleration);
            }
            
            bool setVelocityTarget(int axis_id, double velocity, double acceleration) override {
                return impl_->setVelocityTarget(axis_id, velocity, acceleration);
            }
            
            void stopAxis(int axis_id) override {
                impl_->stopAxis(axis_id);
            }
            
            void emergencyStop(int axis_id) override {
                impl_->emergencyStop(axis_id);
            }
            
            bool clearErrors(int axis_id) override {
                return impl_->clearErrors(axis_id);
            }
            
            DriveStatus getDriveStatus(int axis_id) const override {
                auto status = impl_->getDriveStatus(axis_id);
                DriveStatus result;
                result.operational = status.operational;
                result.enabled = status.enabled;
                result.warning = status.warning;
                result.error = status.error;
                result.homed = status.homed;
                result.moving = status.moving;
                result.target_reached = status.target_reached;
                result.status_word = status.status_word;
                result.error_code = status.error_code;
                result.timestamp = status.timestamp;
                return result;
            }
            
            PositionData getPositionData(int axis_id) const override {
                auto pos = impl_->getPositionData(axis_id);
                PositionData result;
                result.actual_position = pos.actual_position;
                result.actual_velocity = pos.actual_velocity;
                result.actual_torque = pos.actual_torque;
                result.target_position = pos.target_position;
                result.following_error = pos.following_error;
                result.timestamp = pos.timestamp;
                return result;
            }
            
            EncoderData getEncoderData(int axis_id) const override {
                auto enc = impl_->getEncoderData(axis_id);
                EncoderData result;
                result.raw_position = enc.raw_position;
                result.raw_velocity = enc.raw_velocity;
                result.index_pulse = enc.index_pulse;
                result.direction = enc.direction;
                result.error_count = enc.error_count;
                result.timestamp = enc.timestamp;
                return result;
            }
            
            void setStatusCallback(StatusCallback callback) override {
                // Convert callback
                CanOpenInterface::StatusCallback canopen_callback = 
                    [callback](int axis_id, const CanOpenInterface::DriveStatus& status) {
                        DriveStatus result;
                        result.operational = status.operational;
                        result.enabled = status.enabled;
                        result.warning = status.warning;
                        result.error = status.error;
                        result.homed = status.homed;
                        result.moving = status.moving;
                        result.target_reached = status.target_reached;
                        result.status_word = status.status_word;
                        result.error_code = status.error_code;
                        result.timestamp = status.timestamp;
                        callback(axis_id, result);
                    };
                impl_->setStatusCallback(canopen_callback);
            }
            
            void setPositionCallback(PositionCallback callback) override {
                // Convert callback
                CanOpenInterface::PositionCallback canopen_callback = 
                    [callback](int axis_id, const CanOpenInterface::PositionData& pos) {
                        PositionData result;
                        result.actual_position = pos.actual_position;
                        result.actual_velocity = pos.actual_velocity;
                        result.actual_torque = pos.actual_torque;
                        result.target_position = pos.target_position;
                        result.following_error = pos.following_error;
                        result.timestamp = pos.timestamp;
                        callback(axis_id, result);
                    };
                impl_->setPositionCallback(canopen_callback);
            }
            
            void setEncoderCallback(EncoderCallback callback) override {
                // Convert callback
                CanOpenInterface::EncoderCallback canopen_callback = 
                    [callback](int axis_id, const CanOpenInterface::EncoderData& enc) {
                        EncoderData result;
                        result.raw_position = enc.raw_position;
                        result.raw_velocity = enc.raw_velocity;
                        result.index_pulse = enc.index_pulse;
                        result.direction = enc.direction;
                        result.error_count = enc.error_count;
                        result.timestamp = enc.timestamp;
                        callback(axis_id, result);
                    };
                impl_->setEncoderCallback(canopen_callback);
            }
            
            void setErrorCallback(ErrorCallback callback) override {
                impl_->setErrorCallback(callback);
            }
            
            bool sendSDO(int axis_id, uint16_t index, uint8_t subindex, 
                         const void* data, size_t data_size) override {
                return impl_->sendSDO(axis_id, index, subindex, data, data_size);
            }
            
            int receiveSDO(int axis_id, uint16_t index, uint8_t subindex,
                           void* data, size_t data_size) override {
                return impl_->receiveSDO(axis_id, index, subindex, data, data_size);
            }
            
            bool configurePDO(int axis_id, int pdo_number, const std::vector<uint32_t>& mapping) override {
                return impl_->configurePDO(axis_id, pdo_number, mapping);
            }
            
            void enablePDO(int axis_id, int pdo_number, bool enable) override {
                impl_->enablePDO(axis_id, pdo_number, enable);
            }
            
            bool sendNMT(uint8_t node_id, uint8_t command) override {
                // In real implementation, send NMT CAN frame with COB-ID 0x000 + node_id
                // (NMT uses CAN ID 0x000 for all nodes, data byte 0 = command, byte 1 = node_id)
                // Delegate to underlying implementation
                return impl_->sendNMT(node_id, command);
            }
            
            void sendSync() override {
                impl_->sendSync();
            }
            
            std::string getStatistics() const override {
                return impl_->getStatistics();
            }
            
            bool saveConfiguration(const std::string& filename) const override {
                return impl_->saveConfiguration(filename);
            }
            
            bool loadConfiguration(const std::string& filename) override {
                return impl_->loadConfiguration(filename);
            }
            
            std::string getImplementationType() const override {
                return config_.library;
            }
            
            bool supportsSimulation() const override {
                return config_.library == "mock";
            }

            // Trajectory methods for adapter
            std::vector<TrajectoryPoint> generateTrajectory(const TrajectoryParams& params) override {
                // Delegate to CanOpenInterface
                CanOpenInterface::TrajectoryParams canopen_params;
                canopen_params.type = static_cast<CanOpenInterface::TrajectoryType>(params.type);
                canopen_params.max_velocity = params.max_velocity;
                canopen_params.max_acceleration = params.max_acceleration;
                canopen_params.max_jerk = params.max_jerk;
                canopen_params.start_position = params.start_position;
                canopen_params.target_position = params.target_position;
                canopen_params.update_rate = params.update_rate;
                
                auto canopen_trajectory = impl_->generateTrajectory(canopen_params);
                
                // Convert back to ICanOpenInterface::TrajectoryPoint
                std::vector<TrajectoryPoint> trajectory;
                for (const auto& canopen_point : canopen_trajectory) {
                    TrajectoryPoint point;
                    point.position = canopen_point.position;
                    point.velocity = canopen_point.velocity;
                    point.acceleration = canopen_point.acceleration;
                    point.jerk = canopen_point.jerk;
                    point.time = canopen_point.time;
                    trajectory.push_back(point);
                }
                return trajectory;
            }

            bool executeTrajectory(int axis_id, const std::vector<TrajectoryPoint>& trajectory,
                                  std::function<void(const TrajectoryPoint&)> callback = nullptr) override {
                // Convert trajectory to CanOpenInterface format
                std::vector<CanOpenInterface::TrajectoryPoint> canopen_trajectory;
                for (const auto& point : trajectory) {
                    CanOpenInterface::TrajectoryPoint canopen_point;
                    canopen_point.position = point.position;
                    canopen_point.velocity = point.velocity;
                    canopen_point.acceleration = point.acceleration;
                    canopen_point.jerk = point.jerk;
                    canopen_point.time = point.time;
                    canopen_trajectory.push_back(canopen_point);
                }
                
                // Convert callback if provided
                std::function<void(const CanOpenInterface::TrajectoryPoint&)> canopen_callback;
                if (callback) {
                    canopen_callback = [callback](const CanOpenInterface::TrajectoryPoint& canopen_point) {
                        TrajectoryPoint point;
                        point.position = canopen_point.position;
                        point.velocity = canopen_point.velocity;
                        point.acceleration = canopen_point.acceleration;
                        point.jerk = canopen_point.jerk;
                        point.time = canopen_point.time;
                        callback(point);
                    };
                }
                
                return impl_->executeTrajectory(axis_id, canopen_trajectory, canopen_callback);
            }

            uint8_t getNodeNMTState(int axis_id) const override {
                return impl_->getNodeNMTState(axis_id);
            }

            bool isHeartbeatRecent(int axis_id, int max_age_ms) const override {
                return impl_->isHeartbeatRecent(axis_id, max_age_ms);
            }

            bool isDriveEnabled(int axis_id) const override {
                return impl_->isDriveEnabled(axis_id);
            }
            
        private:
            std::unique_ptr<CanOpenInterface> impl_;
            Config config_;
        };
        
        return std::make_unique<CanOpenInterfaceAdapter>();
    }
#endif
    else {
        // Unsupported library
        return nullptr;
    }
}

std::vector<std::string> CanOpenFactory::getSupportedLibraries() {
    std::vector<std::string> libraries = {"mock"};
    
#ifdef HAVE_CANOPEN
    libraries.push_back("canopensocket");
    libraries.push_back("libedssharp");
    libraries.push_back("canfestival");
#endif
    
    return libraries;
}

bool CanOpenFactory::isLibrarySupported(const std::string& library_name) {
    auto libraries = getSupportedLibraries();
    return std::find(libraries.begin(), libraries.end(), library_name) != libraries.end();
}

std::string CanOpenFactory::getDefaultLibrary() {
#ifdef HAVE_CANOPEN
    return "canopensocket";
#else
    return "mock";
#endif
}

} // namespace controllers
} // namespace astro_mount