#include "controllers/canopen_interface.h"
#include "logging/logger.h"
#include "canopen/canopen.h"

#include <thread>
#include <chrono>
#include <mutex>
#include <cstring>
#include <unistd.h>

namespace astro_mount {
namespace controllers {

class CanOpenInterface::Impl {
    friend class CanOpenInterface;
public:
    Impl() : ctx_(nullptr), connected_(false), running_(false),
             sync_thread_running_(false) {
        // Initialize axis data
        for (int i = 0; i < 2; ++i) {
            axis_status_[i] = DriveStatus{};
            axis_position_[i] = PositionData{};
            axis_encoder_[i] = EncoderData{};
            axis_enabled_[i] = false;
            axis_target_position_[i] = 0.0;
            axis_target_velocity_[i] = 0.0;
        }
    }

    ~Impl() {
        shutdown();
    }

    // --- Helper: compute CANopen node ID for a given axis ---
    uint8_t nodeIdForAxis(int axis_id) const {
        // axis 0 → config_.node_id (typically 1 = HA)
        // axis 1 → config_.node_id + 1 (typically 2 = Dec)
        return static_cast<uint8_t>(config_.node_id + axis_id);
    }

    bool initialize(const CanOpenConfig& config) {
        std::lock_guard<std::mutex> lock(mutex_);

        config_ = config;
        connected_ = false;

        logging::Logger::get("canopen")->info(
            "CANopen: Initializing interface {} at {} bps, node_id={}",
            config.interface_name, config.bitrate, config.node_id);

        // Create CANopen context
        ctx_ = canopen_create();
        if (!ctx_) {
            logging::Logger::get("canopen")->error(
                "CANopen: Failed to create context");
            return false;
        }

        // Initialize real SocketCAN socket
        if (!canopen_init(ctx_, config.interface_name.c_str(),
                          config.node_id, config.bitrate)) {
            logging::Logger::get("canopen")->error(
                "CANopen: Failed to init interface {}: {}",
                config.interface_name, strerror(errno));
            canopen_destroy(ctx_);
            ctx_ = nullptr;
            return false;
        }

        connected_ = true;
        running_ = true;

        logging::Logger::get("canopen")->info(
            "CANopen: Interface {} initialized successfully",
            config.interface_name);
        return true;
    }

    void shutdown() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            running_ = false;
            sync_thread_running_ = false;
        }

        if (sync_thread_.joinable()) {
            sync_thread_.join();
        }

        disconnect();
    }

    bool connect() {
        std::lock_guard<std::mutex> lock(mutex_);

        if (connected_) {
            return true;
        }

        // Re-initialize if context exists but not connected
        if (ctx_) {
            if (canopen_init(ctx_, config_.interface_name.c_str(),
                             config_.node_id, config_.bitrate)) {
                connected_ = true;
                running_ = true;
                logging::Logger::get("canopen")->info(
                    "CANopen: Reconnected to {}", config_.interface_name);
                return true;
            }
        }
        return false;
    }

    void disconnect() {
        std::lock_guard<std::mutex> lock(mutex_);

        if (!connected_) {
            return;
        }

        // Disable all axes
        for (int i = 0; i < 2; ++i) {
            axis_enabled_[i] = false;
            axis_target_velocity_[i] = 0.0;
            axis_status_[i].enabled = false;
            axis_status_[i].operational = false;
            axis_status_[i].timestamp = std::chrono::system_clock::now();
        }

        // Shutdown CANopen context (closes socket, stops reader thread)
        if (ctx_) {
            canopen_shutdown(ctx_);
            canopen_destroy(ctx_);
            ctx_ = nullptr;
        }

        connected_ = false;

        logging::Logger::get("canopen")->info(
            "CANopen: Disconnected from {}", config_.interface_name);
    }

    bool isConnected() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return connected_;
    }

    bool configureDrive(int axis_id, const std::string& config_string) {
        if (axis_id < 0 || axis_id >= 2) {
            return false;
        }

        std::lock_guard<std::mutex> lock(mutex_);

        if (!connected_ || !ctx_) {
            return false;
        }

        uint8_t node_id = nodeIdForAxis(axis_id);

        logging::Logger::get("canopen")->info(
            "CANopen: Configuring axis {} (node {})", axis_id, node_id);

        // Parse JSON config string and apply via SDO writes
        // In a full implementation, this would parse the JSON and set
        // individual object dictionary entries via SDO.
        // For now, we rely on the drives having sensible defaults.

        return true;
    }

    bool enableDrive(int axis_id) {
        if (axis_id < 0 || axis_id >= 2) {
            return false;
        }

        std::lock_guard<std::mutex> lock(mutex_);

        if (!connected_ || !ctx_) {
            return false;
        }

        uint8_t node_id = nodeIdForAxis(axis_id);

        logging::Logger::get("canopen")->info(
            "CANopen: Enabling axis {} (node {})", axis_id, node_id);

        // Send CiA 402 enable sequence via SDO
        if (!canopen_402_enable_drive(ctx_, node_id)) {
            logging::Logger::get("canopen")->error(
                "CANopen: Failed to enable axis {} (node {})",
                axis_id, node_id);
            return false;
        }

        axis_enabled_[axis_id] = true;
        axis_status_[axis_id].enabled = true;
        axis_status_[axis_id].operational = true;
        axis_status_[axis_id].timestamp = std::chrono::system_clock::now();

        logging::Logger::get("canopen")->info(
            "CANopen: Axis {} (node {}) enabled successfully",
            axis_id, node_id);

        // Notify status callback
        if (status_callback_) {
            status_callback_(axis_id, axis_status_[axis_id]);
        }

        return true;
    }

    void disableDrive(int axis_id) {
        if (axis_id < 0 || axis_id >= 2) {
            return;
        }

        std::lock_guard<std::mutex> lock(mutex_);

        if (!connected_ || !ctx_) {
            return;
        }

        uint8_t node_id = nodeIdForAxis(axis_id);

        logging::Logger::get("canopen")->info(
            "CANopen: Disabling axis {} (node {})", axis_id, node_id);

        canopen_402_disable_drive(ctx_, node_id);

        axis_enabled_[axis_id] = false;
        axis_status_[axis_id].enabled = false;
        axis_status_[axis_id].operational = false;
        axis_status_[axis_id].timestamp = std::chrono::system_clock::now();

        // Notify status callback
        if (status_callback_) {
            status_callback_(axis_id, axis_status_[axis_id]);
        }
    }

    bool setPositionTarget(int axis_id, double position, double velocity, double acceleration) {
        if (axis_id < 0 || axis_id >= 2) {
            return false;
        }

        std::lock_guard<std::mutex> lock(mutex_);

        if (!connected_ || !ctx_) {
            return false;
        }

        uint8_t node_id = nodeIdForAxis(axis_id);

        // Ensure drive is in Operation Enabled state before sending position command.
        // This handles recovery from Quick Stop Active or fault states.
        uint16_t status = canopen_402_get_status_word(ctx_, node_id);
        if ((status & CIA402_STATUS_OPERATION_ENABLED) == 0) {
            logging::Logger::get("canopen")->warn(
                "CANopen: Axis {} (node {}) not in Operation Enabled (status=0x{:04X}), re-enabling",
                axis_id, node_id, status);
            if (!canopen_402_enable_drive(ctx_, node_id)) {
                logging::Logger::get("canopen")->error(
                    "CANopen: Failed to re-enable axis {} (node {}) before setPositionTarget",
                    axis_id, node_id);
                return false;
            }
            axis_enabled_[axis_id] = true;
            axis_status_[axis_id].enabled = true;
            axis_status_[axis_id].operational = true;
        }

        axis_target_position_[axis_id] = position;

        // Convert degrees to motor counts (simple linear mapping)
        // In a real system, this would use the gear ratio and encoder resolution
        int32_t target_counts = static_cast<int32_t>(position * 10000.0);

        // Set profile position mode
        int8_t mode = CIA402_OPMODE_PROFILE_POS;
        canopen_402_set_mode(ctx_, node_id, mode);

        // Set profile velocity and acceleration via SDO
        int32_t profile_vel = static_cast<int32_t>(velocity * 1000.0);
        canopen_sdo_write_expedited(ctx_, node_id,
                                     OD_INDEX_PROFILE_VELOCITY, 0,
                                     &profile_vel, 4);

        int32_t profile_acc = static_cast<int32_t>(acceleration * 1000.0);
        canopen_sdo_write_expedited(ctx_, node_id,
                                     OD_INDEX_PROFILE_ACCEL, 0,
                                     &profile_acc, 4);

        // Set target position (this also toggles the control word new-set-point bit)
        canopen_402_set_target_position(ctx_, node_id, target_counts);

        axis_position_[axis_id].target_position = position;
        axis_position_[axis_id].timestamp = std::chrono::system_clock::now();

        axis_status_[axis_id].moving = true;
        axis_status_[axis_id].target_reached = false;
        axis_status_[axis_id].timestamp = std::chrono::system_clock::now();

        logging::Logger::get("canopen")->info(
            "CANopen: Axis {} (node {}): setPosition target={:.4f}° vel={:.4f} acc={:.4f}",
            axis_id, node_id, position, velocity, acceleration);

        // Notify callbacks
        if (position_callback_) {
            position_callback_(axis_id, axis_position_[axis_id]);
        }
        if (status_callback_) {
            status_callback_(axis_id, axis_status_[axis_id]);
        }

        return true;
    }

    bool setVelocityTarget(int axis_id, double velocity, double acceleration) {
        if (axis_id < 0 || axis_id >= 2) {
            return false;
        }

        std::lock_guard<std::mutex> lock(mutex_);

        if (!connected_ || !ctx_) {
            return false;
        }

        uint8_t node_id = nodeIdForAxis(axis_id);

        // Ensure drive is in Operation Enabled state
        // This handles the case where stopAxis() previously used quick stop,
        // or the drive was otherwise put into a non-operational state.
        uint16_t status = canopen_402_get_status_word(ctx_, node_id);
        if ((status & CIA402_STATUS_OPERATION_ENABLED) == 0) {
            logging::Logger::get("canopen")->warn(
                "CANopen: Axis {} (node {}) not in Operation Enabled (status=0x{:04X}), re-enabling",
                axis_id, node_id, status);
            if (!canopen_402_enable_drive(ctx_, node_id)) {
                logging::Logger::get("canopen")->error(
                    "CANopen: Failed to re-enable axis {} (node {}) before setVelocityTarget",
                    axis_id, node_id);
                return false;
            }
            axis_enabled_[axis_id] = true;
            axis_status_[axis_id].enabled = true;
            axis_status_[axis_id].operational = true;
        }

        axis_target_velocity_[axis_id] = velocity;

        // Set Profile Velocity mode (mode 3)
        int8_t mode = CIA402_OPMODE_PROFILE_VEL;
        canopen_402_set_mode(ctx_, node_id, mode);

        // Set target velocity via SDO to OD index 0x60FF (Target Velocity for Profile Velocity mode)
        int32_t target_vel = static_cast<int32_t>(velocity * 1000.0);
        canopen_sdo_write_expedited(ctx_, node_id,
                                     OD_INDEX_TARGET_VELOCITY_PROFILE, 0,
                                     &target_vel, 4);

        // Set profile acceleration/deceleration
        int32_t profile_acc = static_cast<int32_t>(acceleration * 1000.0);
        canopen_sdo_write_expedited(ctx_, node_id,
                                     OD_INDEX_PROFILE_ACCEL, 0,
                                     &profile_acc, 4);

        // When velocity target is 0 (stop command), use maximum deceleration
        // to ensure the axis stops promptly. Using the normal profile deceleration
        // value would result in slow coasting to a stop.
        if (velocity == 0.0) {
            // Use Quick Stop deceleration (OD 0x6085) value for prompt stop
            // Setting profile deceleration to maximum ensures immediate deceleration
            int32_t quick_decel = 500000; // very fast deceleration
            canopen_sdo_write_expedited(ctx_, node_id,
                                         OD_INDEX_PROFILE_DECEL, 0,
                                         &quick_decel, 4);
            logging::Logger::get("canopen")->debug(
                "CANopen: Axis {} (node {}): stop with fast decel={}",
                axis_id, node_id, quick_decel);
        } else {
            canopen_sdo_write_expedited(ctx_, node_id,
                                         OD_INDEX_PROFILE_DECEL, 0,
                                         &profile_acc, 4);
        }

        axis_position_[axis_id].actual_velocity = velocity;
        axis_position_[axis_id].timestamp = std::chrono::system_clock::now();

        axis_status_[axis_id].moving = (velocity != 0.0);
        axis_status_[axis_id].timestamp = std::chrono::system_clock::now();

        logging::Logger::get("canopen")->info(
            "CANopen: Axis {} (node {}): setVelocity vel={:.6f}°/{:.4f} acc={:.4f}",
            axis_id, node_id, velocity, acceleration);

        // Notify callbacks
        if (position_callback_) {
            position_callback_(axis_id, axis_position_[axis_id]);
        }
        if (status_callback_) {
            status_callback_(axis_id, axis_status_[axis_id]);
        }

        return true;
    }

    void stopAxis(int axis_id) {
        if (axis_id < 0 || axis_id >= 2) {
            return;
        }

        std::lock_guard<std::mutex> lock(mutex_);

        if (!connected_ || !ctx_) {
            return;
        }

        uint8_t node_id = nodeIdForAxis(axis_id);

        logging::Logger::get("canopen")->info(
            "CANopen: Stopping axis {} (node {})", axis_id, node_id);

        // Graceful stop: write velocity=0 to target velocity OD (0x60FF).
        // This keeps the drive in Operation Enabled state so subsequent
        // setVelocityTarget() / setPositionTarget() calls work immediately.
        // NOTE: We do NOT use canopen_402_quick_stop() here because that
        // transitions the drive into "Quick Stop Active" state (CiA 402),
        // from which all new motion commands are rejected. Quick stop is
        // reserved for emergencyStop() where immediate halt is required.
        int32_t zero_vel = 0;
        canopen_sdo_write_expedited(ctx_, node_id,
                                     OD_INDEX_TARGET_VELOCITY_PROFILE, 0,
                                     &zero_vel, 4);

        // Also set a fast deceleration to ensure prompt stop
        int32_t fast_decel = 500000;
        canopen_sdo_write_expedited(ctx_, node_id,
                                     OD_INDEX_PROFILE_DECEL, 0,
                                     &fast_decel, 4);

        axis_target_velocity_[axis_id] = 0.0;
        axis_position_[axis_id].actual_velocity = 0.0;
        axis_position_[axis_id].timestamp = std::chrono::system_clock::now();

        axis_status_[axis_id].moving = false;
        axis_status_[axis_id].timestamp = std::chrono::system_clock::now();

        // axis_enabled_ stays true — drive remains in Operation Enabled state

        logging::Logger::get("canopen")->info(
            "CANopen: Axis {} (node {}) stopped (velocity=0 via SDO, drive stays enabled)",
            axis_id, node_id);

        // Notify callbacks
        if (position_callback_) {
            position_callback_(axis_id, axis_position_[axis_id]);
        }
        if (status_callback_) {
            status_callback_(axis_id, axis_status_[axis_id]);
        }
    }

    void emergencyStop(int axis_id) {
        std::lock_guard<std::mutex> lock(mutex_);

        if (!connected_ || !ctx_) {
            return;
        }

        // Emergency stop all axes via NMT Stop Remote Node
        for (int i = 0; i < 2; ++i) {
            uint8_t node_id = nodeIdForAxis(i);
            canopen_nmt_send_command(ctx_, node_id, CANOPEN_NMT_STOP_REMOTE_NODE);

            axis_enabled_[i] = false;
            axis_target_velocity_[i] = 0.0;
            axis_status_[i].enabled = false;
            axis_status_[i].operational = false;
            axis_status_[i].moving = false;
            axis_status_[i].timestamp = std::chrono::system_clock::now();

            if (status_callback_) {
                status_callback_(i, axis_status_[i]);
            }
        }

        logging::Logger::get("canopen")->warn(
            "CANopen: Emergency stop - all axes stopped");
    }

    bool clearErrors(int axis_id) {
        if (axis_id < 0 || axis_id >= 2) {
            return false;
        }

        std::lock_guard<std::mutex> lock(mutex_);

        if (!connected_ || !ctx_) {
            return false;
        }

        uint8_t node_id = nodeIdForAxis(axis_id);

        logging::Logger::get("canopen")->info(
            "CANopen: Clearing errors on axis {} (node {})",
            axis_id, node_id);

        if (!canopen_402_fault_reset(ctx_, node_id)) {
            return false;
        }

        axis_status_[axis_id].error = false;
        axis_status_[axis_id].error_code = 0;
        axis_status_[axis_id].timestamp = std::chrono::system_clock::now();

        if (status_callback_) {
            status_callback_(axis_id, axis_status_[axis_id]);
        }

        return true;
    }

    DriveStatus getDriveStatus(int axis_id) const {
        std::lock_guard<std::mutex> lock(mutex_);

        if (axis_id < 0 || axis_id >= 2) {
            return DriveStatus{};
        }

        // Try to read actual status word from the drive via SDO
        if (connected_ && ctx_) {
            uint8_t node_id = nodeIdForAxis(axis_id);
            uint16_t sw = canopen_402_get_status_word(ctx_, node_id);

            // Update internal state from status word
            axis_status_[axis_id].enabled = (sw & CIA402_STATUS_OPERATION_ENABLED) != 0;
            axis_status_[axis_id].target_reached = (sw & CIA402_STATUS_TARGET_REACHED) != 0;
            axis_status_[axis_id].warning = (sw & CIA402_STATUS_WARNING) != 0;
            axis_status_[axis_id].error = (sw & CIA402_STATUS_FAULT) != 0;
            axis_status_[axis_id].status_word = sw;
            axis_status_[axis_id].timestamp = std::chrono::system_clock::now();
        }

        return axis_status_[axis_id];
    }

    PositionData getPositionData(int axis_id) const {
        std::lock_guard<std::mutex> lock(mutex_);

        if (axis_id < 0 || axis_id >= 2) {
            return PositionData{};
        }

        // Try to read actual position from the drive via SDO
        if (connected_ && ctx_) {
            uint8_t node_id = nodeIdForAxis(axis_id);

            // Read actual position (OD 0x6064)
            int32_t actual_pos = 0;
            size_t len = 4;
            if (canopen_sdo_read_expedited(ctx_, node_id,
                                           0x6064, 0, &actual_pos, &len)) {
                axis_position_[axis_id].actual_position =
                    static_cast<double>(actual_pos) / 10000.0;
            }

            // Read actual velocity (OD 0x606C)
            int32_t actual_vel = 0;
            len = 4;
            if (canopen_sdo_read_expedited(ctx_, node_id,
                                           0x606C, 0, &actual_vel, &len)) {
                axis_position_[axis_id].actual_velocity =
                    static_cast<double>(actual_vel) / 1000.0;
            }

            axis_position_[axis_id].timestamp =
                std::chrono::system_clock::now();
        }

        return axis_position_[axis_id];
    }

    EncoderData getEncoderData(int axis_id) const {
        std::lock_guard<std::mutex> lock(mutex_);

        if (axis_id < 0 || axis_id >= 2) {
            return EncoderData{};
        }

        // Try to read encoder position from the drive via SDO
        if (connected_ && ctx_) {
            uint8_t node_id = nodeIdForAxis(axis_id);

            // Read encoder position (OD 0x6381 or similar)
            uint32_t enc_pos = 0;
            size_t len = 4;
            if (canopen_sdo_read_expedited(ctx_, node_id,
                                           OD_INDEX_POSITION_ENCODER, 0,
                                           &enc_pos, &len)) {
                axis_encoder_[axis_id].raw_position = enc_pos;
            }

            axis_encoder_[axis_id].timestamp =
                std::chrono::system_clock::now();
        }

        return axis_encoder_[axis_id];
    }

    void setStatusCallback(StatusCallback callback) {
        std::lock_guard<std::mutex> lock(mutex_);
        status_callback_ = callback;
    }

    void setPositionCallback(PositionCallback callback) {
        std::lock_guard<std::mutex> lock(mutex_);
        position_callback_ = callback;
    }

    void setEncoderCallback(EncoderCallback callback) {
        std::lock_guard<std::mutex> lock(mutex_);
        encoder_callback_ = callback;
    }

    void setErrorCallback(ErrorCallback callback) {
        std::lock_guard<std::mutex> lock(mutex_);
        error_callback_ = callback;
    }

    bool sendSDO(int axis_id, uint16_t index, uint8_t subindex,
                 const void* data, size_t data_size) {
        std::lock_guard<std::mutex> lock(mutex_);

        if (!connected_ || !ctx_) {
            return false;
        }

        uint8_t node_id = nodeIdForAxis(axis_id);

        return canopen_sdo_write_expedited(ctx_, node_id,
                                            index, subindex,
                                            data, data_size);
    }

    int receiveSDO(int axis_id, uint16_t index, uint8_t subindex,
                   void* data, size_t data_size) {
        std::lock_guard<std::mutex> lock(mutex_);

        if (!connected_ || !ctx_) {
            return -1;
        }

        uint8_t node_id = nodeIdForAxis(axis_id);
        size_t len = data_size;

        if (!canopen_sdo_read_expedited(ctx_, node_id,
                                         index, subindex,
                                         data, &len)) {
            return -1;
        }

        return static_cast<int>(len);
    }

    bool configurePDO(int axis_id, int pdo_number,
                      const std::vector<uint32_t>& mapping) {
        std::lock_guard<std::mutex> lock(mutex_);

        if (!connected_ || !ctx_) {
            return false;
        }

        logging::Logger::get("canopen")->info(
            "CANopen: Configuring PDO {} for axis {}", pdo_number, axis_id);

        // PDO configuration via SDO would go here
        // For now, we assume the drives have pre-configured PDOs

        return true;
    }

    void enablePDO(int axis_id, int pdo_number, bool enable) {
        std::lock_guard<std::mutex> lock(mutex_);

        if (!connected_ || !ctx_) {
            return;
        }

        logging::Logger::get("canopen")->info(
            "CANopen: {} PDO {} for axis {}",
            (enable ? "Enabling" : "Disabling"), pdo_number, axis_id);
    }

    bool sendNMT(uint8_t node_id, uint8_t command) {
        std::lock_guard<std::mutex> lock(mutex_);

        if (!connected_ || !ctx_) {
            return false;
        }

        logging::Logger::get("canopen")->info(
            "CANopen: Sending NMT command 0x{:02X} to node {}",
            command, node_id);

        return canopen_nmt_send_command(ctx_, node_id, command);
    }

    void sendSync() {
        std::lock_guard<std::mutex> lock(mutex_);

        if (!connected_ || !ctx_) {
            return;
        }

        // Send SYNC message (COB-ID 0x80, no data)
        canopen_frame_t frame;
        memset(&frame, 0, sizeof(frame));
        frame.can_id = CANOPEN_COBID_SYNC;
        frame.can_dlc = 0;

        canopen_send_frame(ctx_, &frame);
    }

    std::string getStatistics() const {
        std::lock_guard<std::mutex> lock(mutex_);

        // In a real implementation, read CAN bus statistics
        return "CANopen interface: " + config_.interface_name +
               " connected=" + (connected_ ? "true" : "false") +
               " node_id=" + std::to_string(config_.node_id);
    }

    bool saveConfiguration(const std::string& filename) const {
        // Configuration saving is not critical for real CANopen operation
        (void)filename;
        return true;
    }

    bool loadConfiguration(const std::string& filename) {
        // Configuration loading is not critical for real CANopen operation
        (void)filename;
        return true;
    }

    std::vector<TrajectoryPoint> generateTrajectory(
        const TrajectoryParams& params) {
        std::vector<TrajectoryPoint> points;
        double t = 0.0;
        double dt = 1.0 / params.update_rate;
        double total_time = std::abs(params.target_position -
                                      params.start_position) /
                            params.max_velocity;

        while (t < total_time) {
            TrajectoryPoint pt;
            double frac = t / total_time;

            // Simple trapezoidal profile
            if (frac < 0.25) {
                // Acceleration phase
                pt.acceleration = params.max_acceleration;
                pt.velocity = params.max_velocity * (frac / 0.25);
            } else if (frac > 0.75) {
                // Deceleration phase
                pt.acceleration = -params.max_acceleration;
                pt.velocity = params.max_velocity * ((1.0 - frac) / 0.25);
            } else {
                // Cruise phase
                pt.acceleration = 0.0;
                pt.velocity = params.max_velocity;
            }

            pt.position = params.start_position +
                          (params.target_position - params.start_position) *
                          frac;
            pt.time = t;
            pt.jerk = 0.0;

            points.push_back(pt);
            t += dt;
        }

        // Add final point
        TrajectoryPoint final_pt;
        final_pt.position = params.target_position;
        final_pt.velocity = 0.0;
        final_pt.acceleration = 0.0;
        final_pt.jerk = 0.0;
        final_pt.time = total_time;
        points.push_back(final_pt);

        return points;
    }

    bool executeTrajectory(int axis_id,
                          const std::vector<TrajectoryPoint>& trajectory,
                          std::function<void(const TrajectoryPoint&)> callback) {
        if (axis_id < 0 || axis_id >= 2 || trajectory.empty()) {
            return false;
        }

        // Execute trajectory in a background thread
        std::thread([this, axis_id, trajectory, callback]() {
            for (const auto& pt : trajectory) {
                if (!running_) break;

                setPositionTarget(axis_id, pt.position,
                                  pt.velocity, pt.acceleration);

                if (callback) {
                    callback(pt);
                }

                std::this_thread::sleep_for(
                    std::chrono::milliseconds(10));
            }
        }).detach();

        return true;
    }

private:
    canopen_ctx_t* ctx_;
    CanOpenConfig config_;
    bool connected_;
    bool running_;

    // Axis state — mutable because getters may update from live SDO data
    bool axis_enabled_[2];
    double axis_target_position_[2];
    double axis_target_velocity_[2];
    mutable DriveStatus axis_status_[2];
    mutable PositionData axis_position_[2];
    mutable EncoderData axis_encoder_[2];

    // SYNC thread
    bool sync_thread_running_;
    std::thread sync_thread_;

    // Callbacks
    StatusCallback status_callback_;
    PositionCallback position_callback_;
    EncoderCallback encoder_callback_;
    ErrorCallback error_callback_;

    mutable std::mutex mutex_;
};

// ======================================================================
// CanOpenInterface public API — delegates to Impl
// ======================================================================

CanOpenInterface::CanOpenInterface()
    : pimpl(std::make_unique<Impl>()) {}

CanOpenInterface::~CanOpenInterface() = default;

bool CanOpenInterface::initialize(const CanOpenConfig& config) {
    return pimpl->initialize(config);
}

void CanOpenInterface::shutdown() {
    pimpl->shutdown();
}

bool CanOpenInterface::connect() {
    return pimpl->connect();
}

void CanOpenInterface::disconnect() {
    pimpl->disconnect();
}

bool CanOpenInterface::isConnected() const {
    return pimpl->isConnected();
}

bool CanOpenInterface::configureDrive(int axis_id,
                                       const std::string& config_string) {
    return pimpl->configureDrive(axis_id, config_string);
}

bool CanOpenInterface::enableDrive(int axis_id) {
    return pimpl->enableDrive(axis_id);
}

void CanOpenInterface::disableDrive(int axis_id) {
    pimpl->disableDrive(axis_id);
}

bool CanOpenInterface::setPositionTarget(int axis_id, double position,
                                          double velocity,
                                          double acceleration) {
    return pimpl->setPositionTarget(axis_id, position, velocity, acceleration);
}

bool CanOpenInterface::setVelocityTarget(int axis_id, double velocity,
                                          double acceleration) {
    return pimpl->setVelocityTarget(axis_id, velocity, acceleration);
}

void CanOpenInterface::stopAxis(int axis_id) {
    pimpl->stopAxis(axis_id);
}

void CanOpenInterface::emergencyStop(int axis_id) {
    pimpl->emergencyStop(axis_id);
}

bool CanOpenInterface::clearErrors(int axis_id) {
    return pimpl->clearErrors(axis_id);
}

CanOpenInterface::DriveStatus CanOpenInterface::getDriveStatus(int axis_id) const {
    return pimpl->getDriveStatus(axis_id);
}

CanOpenInterface::PositionData CanOpenInterface::getPositionData(int axis_id) const {
    return pimpl->getPositionData(axis_id);
}

CanOpenInterface::EncoderData CanOpenInterface::getEncoderData(int axis_id) const {
    return pimpl->getEncoderData(axis_id);
}

void CanOpenInterface::setStatusCallback(StatusCallback callback) {
    pimpl->setStatusCallback(callback);
}

void CanOpenInterface::setPositionCallback(PositionCallback callback) {
    pimpl->setPositionCallback(callback);
}

void CanOpenInterface::setEncoderCallback(EncoderCallback callback) {
    pimpl->setEncoderCallback(callback);
}

void CanOpenInterface::setErrorCallback(ErrorCallback callback) {
    pimpl->setErrorCallback(callback);
}

bool CanOpenInterface::sendSDO(int axis_id, uint16_t index,
                                uint8_t subindex, const void* data,
                                size_t data_size) {
    return pimpl->sendSDO(axis_id, index, subindex, data, data_size);
}

int CanOpenInterface::receiveSDO(int axis_id, uint16_t index,
                                  uint8_t subindex, void* data,
                                  size_t data_size) {
    return pimpl->receiveSDO(axis_id, index, subindex, data, data_size);
}

bool CanOpenInterface::configurePDO(int axis_id, int pdo_number,
                                     const std::vector<uint32_t>& mapping) {
    return pimpl->configurePDO(axis_id, pdo_number, mapping);
}

void CanOpenInterface::enablePDO(int axis_id, int pdo_number, bool enable) {
    pimpl->enablePDO(axis_id, pdo_number, enable);
}

bool CanOpenInterface::sendNMT(uint8_t node_id, uint8_t command) {
    return pimpl->sendNMT(node_id, command);
}

void CanOpenInterface::sendSync() {
    pimpl->sendSync();
}

std::string CanOpenInterface::getStatistics() const {
    return pimpl->getStatistics();
}

bool CanOpenInterface::saveConfiguration(const std::string& filename) const {
    return pimpl->saveConfiguration(filename);
}

bool CanOpenInterface::loadConfiguration(const std::string& filename) {
    return pimpl->loadConfiguration(filename);
}

std::vector<CanOpenInterface::TrajectoryPoint>
CanOpenInterface::generateTrajectory(const TrajectoryParams& params) {
    return pimpl->generateTrajectory(params);
}

bool CanOpenInterface::executeTrajectory(
    int axis_id,
    const std::vector<TrajectoryPoint>& trajectory,
    std::function<void(const TrajectoryPoint&)> callback) {
    return pimpl->executeTrajectory(axis_id, trajectory, callback);
}

} // namespace controllers
} // namespace astro_mount
