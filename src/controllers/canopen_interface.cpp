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
            nmt_cached_state_[i] = 0x00;  // NMT bootup / unknown
            nmt_heartbeat_count_[i] = 0;
            nmt_last_heartbeat_[i] = std::chrono::steady_clock::time_point{};
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

        // Register NMT heartbeat callback so we receive real heartbeat
        // state updates from the C reader thread instead of polling via SDO.
        canopen_set_nmt_callback(ctx_, nmtCallbackTrampoline, this);

        // ── Execute custom servo initialization SDO sequence ──────────
        // Sends manufacturer-specific configuration (microstep resolution,
        // electronic gearing, encoder settings) to each drive before
        // normal operation. The sequence is defined in config JSON under
        // "servo_init" and loaded through ControllerConfig → CanOpenConfig.
        if (config_.servo_init_enabled && !config_.servo_init_sequence.empty()) {
            auto logger = logging::Logger::get("canopen");
            logger->info("CANopen: Applying servo init sequence ({} entries)...",
                        config_.servo_init_sequence.size());

            for (const auto& entry : config_.servo_init_sequence) {
                uint8_t node = nodeIdForAxis(entry.axis);
                logger->info("  Axis {} (node {}): SDO write 0x{:04X}:{} = {} ({})",
                            entry.axis, node, entry.index, entry.subindex,
                            entry.value,
                            entry.description);

                size_t sz = (entry.data_size >= 1 && entry.data_size <= 4) ? entry.data_size : 4;
                bool ok = canopen_sdo_write_expedited(
                    ctx_, node,
                    entry.index, entry.subindex,
                    &entry.value, sz);

                if (!ok) {
                    logger->warn("  Axis {}: SDO write 0x{:04X}:{} FAILED (timeout?)",
                                entry.axis, entry.index, entry.subindex);
                } else {
                    // Read-back verification: confirm the written value is
                    // actually applied on the drive.  Some objects are
                    // scaled / capped by the drive firmware, so the stored
                    // value may differ from the one written.
                    int32_t readback = 0;
                    size_t rb_len = sizeof(readback);
                    bool rb_ok = canopen_sdo_read_expedited(
                        ctx_, node,
                        entry.index, entry.subindex,
                        &readback, &rb_len);
                    if (rb_ok) {
                        // Mask readback to the expected size so we don't
                        // print garbage high bytes for 8/16-bit objects.
                        if (sz == 1) readback &= 0xFF;
                        else if (sz == 2) readback &= 0xFFFF;
                        logger->info("  Axis {}: SDO verify 0x{:04X}:{} → wrote={}, read={} ({})",
                                    entry.axis, entry.index, entry.subindex,
                                    entry.value, readback, entry.description);
                    } else {
                        logger->warn("  Axis {}: SDO verify 0x{:04X}:{} readback FAILED",
                                    entry.axis, entry.index, entry.subindex);
                    }
                }
            }
        }

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

        // Join trajectory thread before destroying context
        if (traj_thread_.joinable()) {
            traj_thread_.join();
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

        // If context was destroyed by a previous disconnect(), create a new one.
        if (!ctx_) {
            ctx_ = canopen_create();
            if (!ctx_) {
                logging::Logger::get("canopen")->error(
                    "CANopen: Failed to create context for reconnection");
                return false;
            }
        }

        // Re-initialize the CANopen socket and reader thread
        if (canopen_init(ctx_, config_.interface_name.c_str(),
                         config_.node_id, config_.bitrate)) {
            connected_ = true;
            running_ = true;
            logging::Logger::get("canopen")->info(
                "CANopen: Reconnected to {}", config_.interface_name);
            return true;
        }

        // Initialization failed — clean up the context we just created
        if (ctx_) {
            canopen_destroy(ctx_);
            ctx_ = nullptr;
        }
        return false;
    }

    void disconnect() {
        std::lock_guard<std::mutex> lock(mutex_);

        if (!connected_) {
            return;
        }

        // Disable all axes and clear state.  The DriveStatus objects
        // are mutable (updated by getDriveStatus), so we reset them
        // under the lock to keep the internal view consistent.
        for (int i = 0; i < 2; ++i) {
            axis_enabled_[i] = false;
            axis_target_velocity_[i] = 0.0;
            axis_status_[i] = DriveStatus{};
            axis_status_[i].timestamp = std::chrono::system_clock::now();
            axis_position_[i] = PositionData{};
            axis_encoder_[i] = EncoderData{};
        }

        // Shutdown CANopen context (stops reader thread, closes socket)
        // and then destroy it so that a subsequent connect() can create
        // a fresh context.
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

        // --- Zero target velocity BEFORE enabling the drive ---
        // After the enable sequence, the drive enters Operation Enabled and
        // immediately starts executing whichever motion profile was last
        // configured.  If target velocity (0x60FF) still holds a non-zero
        // value from a previous Profile Velocity command, the axis will
        // lurch as soon as it is enabled.  Zero it first.
        int32_t zero_vel = 0;
        canopen_sdo_write_expedited(ctx_, node_id,
                                    0x60FF, 0, &zero_vel, 4);
        logging::Logger::get("canopen")->info(
            "CANopen: Axis {} pre-enable: target velocity zeroed", axis_id);

        // --- Read actual position BEFORE enabling the drive ---
        // Also prevent a stale target position (0x607A) from causing a
        // move: read the current position and write it back as the target.
        int32_t actual_pos = 0;
        size_t len = 4;
        bool have_pos = canopen_sdo_read_expedited(ctx_, node_id,
                                                   0x6064, 0, &actual_pos, &len);
        if (have_pos) {
            logging::Logger::get("canopen")->info(
                "CANopen: Axis {} pre-enable position = {} counts, locking target",
                axis_id, actual_pos);
            canopen_sdo_write_expedited(ctx_, node_id,
                                        0x607A, 0, &actual_pos, 4);
        }

        // Send CiA 402 enable sequence via SDO
        if (!canopen_402_enable_drive(ctx_, node_id)) {
            logging::Logger::get("canopen")->error(
                "CANopen: Failed to enable axis {} (node {})",
                axis_id, node_id);
            return false;
        }

        // --- After enable, always set mode to Profile Position ---
        // This must be done regardless of whether the position read
        // succeeded; otherwise the drive may remain in Velocity mode
        // and ignore all subsequent target position commands.
        canopen_402_set_mode(ctx_, node_id, CIA402_OPMODE_PROFILE_POS);
        logging::Logger::get("canopen")->info(
            "CANopen: Axis {} mode set to Profile Position (1)", axis_id);

        if (have_pos) {
            // Write target position again with new-set-point toggle
            canopen_402_set_target_position(ctx_, node_id, actual_pos);
            logging::Logger::get("canopen")->info(
                "CANopen: Axis {} post-enable target locked at {} counts",
                axis_id, actual_pos);
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

        // Convert degrees to drive position units using the configured
        // counts-per-degree factor (default 4000.0/360.0 ≈ 11.111 counts/°,
        // matching a 4000-count encoder → 360° at the motor shaft).
        const double cpd = config_.axis_position_counts_per_degree[axis_id];
        int32_t target_counts = static_cast<int32_t>(position * cpd);

        // Ensure the drive is in Profile Position mode.  We read the
        // current mode first to avoid unnecessary state transitions that
        // would cancel an in-progress motion profile.
        int8_t current_mode = 0;
        size_t mode_len = 1;
        canopen_sdo_read_expedited(ctx_, node_id,
                                    OD_INDEX_MODES_OF_OP_DISPLAY, 0,
                                    &current_mode, &mode_len);
        if (current_mode != CIA402_OPMODE_PROFILE_POS) {
            logging::Logger::get("canopen")->info(
                "CANopen: Axis {} mode={}, switching to Profile Position (1)",
                axis_id, current_mode);
            int8_t mode = CIA402_OPMODE_PROFILE_POS;
            canopen_402_set_mode(ctx_, node_id, mode);
        }

        // Set profile velocity and acceleration via SDO.
        // Convert using configured scaling factor (default 4000.0/360.0 ≈ 11.111 counts per °/s,
        // matching a 4000-count encoder → 360°/s at the motor shaft).
        const double vpd = config_.axis_velocity_counts_per_deg_s[axis_id];
        int32_t profile_vel = static_cast<int32_t>(velocity * vpd);
        canopen_sdo_write_expedited(ctx_, node_id,
                                     OD_INDEX_PROFILE_VELOCITY, 0,
                                     &profile_vel, 4);

        // Convert acceleration to drive units.
        // Mode "time": drive interprets 0x6083/0x6084 as ramp time → use inverse mapping.
        // Mode "rate": drive interprets as acceleration rate → use linear mapping.
        int32_t profile_acc;
        if (config_.accel_mode == "rate") {
            profile_acc = static_cast<int32_t>(acceleration * vpd);
        } else {
            // Default "time" mode: larger °/s² → smaller time → faster ramp
            // K = 27750 chosen so default 50 °/s² maps to ~555 drive units
            const double accel_K = 27750.0;
            profile_acc = static_cast<int32_t>(
                accel_K / std::max(acceleration, 0.001));
        }
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
            "CANopen: Axis {} (node {}): setPosition pos={:.4f}° vel={:.4f} acc={:.4f} cpd={:.6f} -> target_counts={} (raw)",
            axis_id, node_id, position, velocity, acceleration, cpd, target_counts);

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

        // Ensure drive is in Operation Enabled state before sending velocity command.
        // This handles recovery from Quick Stop Active or fault states, same as
        // setPositionTarget() — without this, a drive that faulted during init
        // would permanently fail all velocity commands.
        uint16_t status = canopen_402_get_status_word(ctx_, node_id);
        if ((status & CIA402_STATUS_OPERATION_ENABLED) == 0) {
            logging::Logger::get("canopen")->warn(
                "CANopen: Axis {} (node {}) not in Operation Enabled (status=0x{:04X}), re-enabling",
                axis_id, node_id, status);
            if (!canopen_402_enable_drive(ctx_, node_id)) {
                logging::Logger::get("canopen")->error(
                    "CANopen: Failed to re-enable axis {} (node {}) before setVelocityTarget",
                    axis_id, node_id);
                axis_enabled_[axis_id] = false;
                axis_status_[axis_id].enabled = false;
                return false;
            }
            axis_enabled_[axis_id] = true;
            axis_status_[axis_id].enabled = true;
            axis_status_[axis_id].operational = true;
        }

        axis_target_velocity_[axis_id] = velocity;

        // Set Profile Velocity mode (mode 3)
        int8_t mode = CIA402_OPMODE_PROFILE_VEL;
        if (!canopen_402_set_mode(ctx_, node_id, mode)) {
            axis_enabled_[axis_id] = false;
            axis_status_[axis_id].enabled = false;
            logging::Logger::get("canopen")->debug(
                "CANopen: Axis {} (node {}): set_mode failed, marking disabled",
                axis_id, node_id);
            return false;
        }

        const double vpd = config_.axis_velocity_counts_per_deg_s[axis_id];

        // Write acceleration/deceleration BEFORE target velocity.
        // In Profile Velocity mode, the drive starts executing the target
        // velocity as soon as 0x60FF is written.  If we write 0x60FF first,
        // the drive uses whatever acceleration values were left over from
        // the previous motion — causing asymmetric behaviour on direction
        // changes.  The manual CAN test worked because it wrote accel/decel
        // first, then target velocity.
        //
        // Convert acceleration to drive units.
        // Mode "time": drive interprets 0x6083/0x6084 as ramp time → use inverse mapping.
        // Mode "rate": drive interprets as acceleration rate → use linear mapping.
        int32_t profile_acc;
        if (config_.accel_mode == "rate") {
            profile_acc = static_cast<int32_t>(acceleration * vpd);
        } else {
            // Default "time" mode: larger °/s² → smaller time → faster ramp
            const double accel_K = 27750.0;
            profile_acc = static_cast<int32_t>(
                accel_K / std::max(acceleration, 0.001));
        }
        canopen_sdo_write_expedited(ctx_, node_id,
                                     OD_INDEX_PROFILE_ACCEL, 0,
                                     &profile_acc, 4);
        canopen_sdo_write_expedited(ctx_, node_id,
                                     OD_INDEX_PROFILE_DECEL, 0,
                                     &profile_acc, 4);

        // Set target velocity via SDO to OD index 0x60FF
        int32_t target_vel = static_cast<int32_t>(velocity * vpd);
        if (!canopen_sdo_write_expedited(ctx_, node_id,
                                          OD_INDEX_TARGET_VELOCITY_PROFILE, 0,
                                          &target_vel, 4)) {
            axis_enabled_[axis_id] = false;
            axis_status_[axis_id].enabled = false;
            logging::Logger::get("canopen")->debug(
                "CANopen: Axis {} (node {}): target_vel write failed, marking disabled",
                axis_id, node_id);
            return false;
        }

        axis_position_[axis_id].actual_velocity = velocity;
        axis_position_[axis_id].timestamp = std::chrono::system_clock::now();

        axis_status_[axis_id].moving = (velocity != 0.0);
        axis_status_[axis_id].timestamp = std::chrono::system_clock::now();

        logging::Logger::get("canopen")->info(
            "CANopen: Axis {} (node {}): setVelocity vel={:.4f}°/s acc={:.4f}",
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
            "CANopen: Stopping axis {} (node {}) via Halt bit", axis_id, node_id);

        // CiA 402 §6.4.1.5: Halt bit (control word bit 8 = 0x0100) works in
        // BOTH Profile Position and Profile Velocity modes. It stops ongoing
        // motion using the currently configured profile deceleration (0x6084)
        // WITHOUT permanently changing any OD parameters.
        //
        // Previously, for velocity mode, we wrote velocity=0 to 0x60FF and
        // overwrote 0x6084 with 500000. This left a persistent asymmetry
        // between 0x6083 (accel) and 0x6084 (decel), causing visibly different
        // speeds when starting motion in opposite directions.

        // Set Halt bit: 0x000F (Op Enabled) | 0x0100 = 0x010F
        canopen_402_set_control_word(ctx_, node_id, 0x010F);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));

        // Clear halt bit to allow future motion commands.
        // Per CiA 402, clearing halt while stationary keeps the drive
        // in Operation Enabled — no re-enable needed.
        canopen_402_set_control_word(ctx_, node_id, 0x000F);

        axis_target_velocity_[axis_id] = 0.0;
        axis_position_[axis_id].actual_velocity = 0.0;
        axis_position_[axis_id].timestamp = std::chrono::system_clock::now();

        axis_status_[axis_id].moving = false;
        axis_status_[axis_id].timestamp = std::chrono::system_clock::now();

        // axis_enabled_ stays true — drive remains in Operation Enabled state

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

        // CiA 402 §6.4: Quick Stop (control word = 0x0002) commands the
        // drive to decelerate using the quick stop deceleration (OD 0x6085)
        // and transition to "Switch On Disabled".  The drive remains in
        // NMT Operational state and continues to respond to SDO — so
        // re-enabling after emergency stop is fast (no SDO timeouts).
        //
        // We deliberately do NOT send NMT Stop (0x02) because it would
        // put the drive in NMT Stopped state where it ignores SDO requests.
        // The subsequent re-enable would then suffer ~1 s SDO timeouts.
        for (int i = 0; i < 2; ++i) {
            uint8_t node_id = nodeIdForAxis(i);
            canopen_402_quick_stop(ctx_, node_id);

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
            "CANopen: Emergency stop — Quick Stop sent to all axes");
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

        // Use configured counts_per_degree for conversion.
        // Default 10000.0 means the drive reports position in units of
        // 0.0001° (CiA 402 standard).  For raw encoder counts, set
        // this to (encoder_resolution × quadrature / 360).
        const double cpd = config_.axis_position_counts_per_degree[axis_id];
        const double vpd = config_.axis_velocity_counts_per_deg_s[axis_id];

        // Try to read actual position from the drive via SDO
        if (connected_ && ctx_) {
            uint8_t node_id = nodeIdForAxis(axis_id);

            // Read actual position (OD 0x6064)
            int32_t actual_pos = 0;
            size_t len = 4;
            if (canopen_sdo_read_expedited(ctx_, node_id,
                                           0x6064, 0, &actual_pos, &len)) {
                axis_position_[axis_id].actual_position =
                    static_cast<double>(actual_pos) / cpd;
            }

            // Read actual velocity (OD 0x606C)
            int32_t actual_vel = 0;
            len = 4;
            if (canopen_sdo_read_expedited(ctx_, node_id,
                                           0x606C, 0, &actual_vel, &len)) {
                axis_position_[axis_id].actual_velocity =
                    static_cast<double>(actual_vel) / vpd;
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

        // Join any previous trajectory thread first
        if (traj_thread_.joinable()) {
            traj_thread_.join();
        }

        // Execute trajectory in a background thread, stored so shutdown()
        // can join it and prevent use-after-free.
        traj_thread_ = std::thread([this, axis_id, trajectory, callback]() {
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
        });

        return true;
    }

    // --- Drive enabled state (cached, no SDO traffic) ---
    // Returns the last known enabled state for the given axis.
    // Updated by enableDrive(), disableDrive(), emergencyStop(),
    // and getDriveStatus() when it reads the CiA 402 status word.
    bool isDriveEnabled(int axis_id) const {
        if (axis_id < 0 || axis_id >= 2) return false;
        return axis_enabled_[axis_id];
    }

    // --- Real NMT state from heartbeat callbacks (no SDO traffic) ---
    // Returns the NMT state cached from the last heartbeat received for
    // the given axis.  Values are CiA 301 NMT states:
    //   0x00 = Bootup / Initialising
    //   0x04 = Stopped
    //   0x05 = Operational
    //   0x7F = Pre-Operational
    uint8_t getNodeNMTState(int axis_id) const {
        return getCachedNMTState(axis_id);
    }

    bool isHeartbeatRecent(int axis_id, int max_age_ms) const {
        if (axis_id < 0 || axis_id >= 2) return false;
        auto last = getLastHeartbeatTime(axis_id);
        if (last.time_since_epoch().count() == 0) return false;
        auto age = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - last).count();
        return age <= max_age_ms;
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

    // Real NMT state from heartbeat callbacks (updated by reader thread)
    uint8_t nmt_cached_state_[2];
    uint32_t nmt_heartbeat_count_[2];
    std::chrono::steady_clock::time_point nmt_last_heartbeat_[2];

    // SYNC thread
    bool sync_thread_running_;
    std::thread sync_thread_;

    // Trajectory execution thread (stored to prevent use-after-free)
    std::thread traj_thread_;

    // Callbacks
    StatusCallback status_callback_;
    PositionCallback position_callback_;
    EncoderCallback encoder_callback_;
    ErrorCallback error_callback_;

    // --- NMT heartbeat callback (called from C reader thread) ---
    // Updates the cached NMT state for the given node.
    // The mapping from node_id → axis index is: axis = node_id - config_.node_id.
    void onNMTStateChange(uint8_t node_id, uint8_t state) {
        int axis = static_cast<int>(node_id) - config_.node_id;
        if (axis < 0 || axis >= 2) return;

        nmt_cached_state_[axis] = state;
        nmt_heartbeat_count_[axis]++;
        nmt_last_heartbeat_[axis] = std::chrono::steady_clock::now();
    }

    // Static trampoline for the C callback
    static void nmtCallbackTrampoline(void* userdata, uint8_t node_id,
                                      uint8_t state) {
        auto* impl = static_cast<Impl*>(userdata);
        if (impl) impl->onNMTStateChange(node_id, state);
    }

    // --- NMT state query (from real heartbeats, not SDO) ---
    uint8_t getCachedNMTState(int axis_id) const {
        if (axis_id < 0 || axis_id >= 2) return 0x00;
        return nmt_cached_state_[axis_id];
    }

    uint32_t getHeartbeatCount(int axis_id) const {
        if (axis_id < 0 || axis_id >= 2) return 0;
        return nmt_heartbeat_count_[axis_id];
    }

    std::chrono::steady_clock::time_point getLastHeartbeatTime(int axis_id) const {
        if (axis_id < 0 || axis_id >= 2) return {};
        return nmt_last_heartbeat_[axis_id];
    }

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

uint8_t CanOpenInterface::getNodeNMTState(int axis_id) const {
    return pimpl->getNodeNMTState(axis_id);
}

bool CanOpenInterface::isDriveEnabled(int axis_id) const {
    return pimpl->isDriveEnabled(axis_id);
}

bool CanOpenInterface::isHeartbeatRecent(int axis_id, int max_age_ms) const {
    return pimpl->isHeartbeatRecent(axis_id, max_age_ms);
}

} // namespace controllers
} // namespace astro_mount

