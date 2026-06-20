#ifndef CANOPEN_INTERFACE_H
#define CANOPEN_INTERFACE_H

#include <string>
#include <memory>
#include <functional>
#include <vector>
#include <chrono>
#include "controllers/icanopen_interface.h"

namespace astro_mount {
namespace controllers {

/**
 * @brief CANopen interface for astronomical mount control
 * 
 * Implements CANopen protocol (CiA 301, CiA 402) for controlling
 * servo drives and encoders in astronomical mounts.
 */
class CanOpenInterface {
public:
    struct CanOpenConfig {
        std::string interface_name;      // e.g., "can0", "vcan0"
        int bitrate;                     // CAN bitrate (125000, 250000, 500000, 1000000)
        int node_id;                     // CANopen node ID (1-127)
        bool use_sync;                   // Use SYNC messages
        int sync_period_ms;              // SYNC period in milliseconds
        int pdo_mapping[4];              // PDO mapping configuration
        int sdo_timeout_ms;              // SDO timeout in milliseconds
        double axis_position_counts_per_degree[2] = {4000.0 / 360.0, 4000.0 / 360.0};  // per-axis counts per degree
        double axis_velocity_counts_per_deg_s[2] = {4000.0 / 360.0, 4000.0 / 360.0};  // per-axis counts per °/s
        std::string accel_mode = "time";  // "time" or "rate"
        bool pdo_config_enabled = false;  // Write PDO mappings to drive (may overwrite mfgr params)

        /** Custom SDO sequence sent to each axis during initialize() */
        bool servo_init_enabled = false;
        std::vector<ICanOpenInterface::ServoInitEntry> servo_init_sequence;
    };

    struct DriveStatus {
        bool operational;                // Drive is operational
        bool enabled;                    // Drive enabled
        bool warning;                    // Warning present
        bool error;                      // Error present
        bool homed;                      // Axis is homed
        bool moving;                     // Axis is moving
        bool target_reached;             // Target position reached
        uint16_t status_word;            // CANopen status word
        uint32_t error_code;             // Drive error code
        std::chrono::system_clock::time_point timestamp;
    };

    struct PositionData {
        double actual_position;          // Actual position in degrees
        double actual_velocity;          // Actual velocity in deg/s
        double actual_torque;            // Actual torque percentage
        double target_position;          // Target position in degrees
        double following_error;          // Following error in degrees
        std::chrono::system_clock::time_point timestamp;
    };

    struct EncoderData {
        uint32_t raw_position;           // Raw encoder counts
        uint32_t raw_velocity;           // Raw velocity counts
        bool index_pulse;                // Index pulse detected
        bool direction;                  // Rotation direction
        uint32_t error_count;            // Encoder error count
        std::chrono::system_clock::time_point timestamp;
    };

    /**
     * @brief Callback for drive status updates
     */
    using StatusCallback = std::function<void(int axis_id, const DriveStatus& status)>;

    /**
     * @brief Callback for position updates
     */
    using PositionCallback = std::function<void(int axis_id, const PositionData& position)>;

    /**
     * @brief Callback for encoder updates
     */
    using EncoderCallback = std::function<void(int axis_id, const EncoderData& encoder)>;

    /**
     * @brief Callback for error events
     */
    using ErrorCallback = std::function<void(int axis_id, const std::string& error)>;

    CanOpenInterface();
    ~CanOpenInterface();

    /**
     * @brief Initialize CANopen interface
     * @param config Configuration
     * @return True if initialization successful
     */
    bool initialize(const CanOpenConfig& config);

    /**
     * @brief Shutdown CANopen interface
     */
    void shutdown();

    /**
     * @brief Connect to CAN network
     * @return True if connection successful
     */
    bool connect();

    /**
     * @brief Disconnect from CAN network
     */
    void disconnect();

    /**
     * @brief Check if connected
     * @return True if connected
     */
    bool isConnected() const;

    /**
     * @brief Configure drive for axis
     * @param axis_id Axis identifier (0=RA/Azimuth, 1=Dec/Altitude)
     * @param config_string Configuration string
     * @return True if configuration successful
     */
    bool configureDrive(int axis_id, const std::string& config_string);

    /**
     * @brief Enable drive
     * @param axis_id Axis identifier
     * @return True if enable successful
     */
    bool enableDrive(int axis_id);

    /**
     * @brief Disable drive
     * @param axis_id Axis identifier
     */
    void disableDrive(int axis_id);

    /**
     * @brief Set position target
     * @param axis_id Axis identifier
     * @param position Target position in degrees
     * @param velocity Max velocity in deg/s
     * @param acceleration Acceleration in deg/s²
     * @return True if command accepted
     */
    bool setPositionTarget(int axis_id, double position, double velocity, double acceleration);

    /**
     * @brief Set velocity target
     * @param axis_id Axis identifier
     * @param velocity Target velocity in deg/s
     * @param acceleration Acceleration in deg/s²
     * @return True if command accepted
     */
    bool setVelocityTarget(int axis_id, double velocity, double acceleration);

    /**
     * @brief Stop axis (quick stop)
     * @param axis_id Axis identifier
     */
    void stopAxis(int axis_id);

    /**
     * @brief Emergency stop
     * @param axis_id Axis identifier
     */
    void emergencyStop(int axis_id);

    /**
     * @brief Clear errors
     * @param axis_id Axis identifier
     * @return True if errors cleared
     */
    bool clearErrors(int axis_id);

    /**
     * @brief Get drive status
     * @param axis_id Axis identifier
     * @return Drive status
     */
    DriveStatus getDriveStatus(int axis_id) const;

    /**
     * @brief Get position data
     * @param axis_id Axis identifier
     * @return Position data
     */
    PositionData getPositionData(int axis_id) const;

    /**
     * @brief Get encoder data
     * @param axis_id Axis identifier
     * @return Encoder data
     */
    EncoderData getEncoderData(int axis_id) const;

    /**
     * @brief Set status callback
     * @param callback Callback function
     */
    void setStatusCallback(StatusCallback callback);

    /**
     * @brief Set position callback
     * @param callback Callback function
     */
    void setPositionCallback(PositionCallback callback);

    /**
     * @brief Set encoder callback
     * @param callback Callback function
     */
    void setEncoderCallback(EncoderCallback callback);

    /**
     * @brief Set error callback
     * @param callback Callback function
     */
    void setErrorCallback(ErrorCallback callback);

    /**
     * @brief Send SDO (Service Data Object) message
     * @param axis_id Axis identifier
     * @param index Object dictionary index
     * @param subindex Object dictionary subindex
     * @param data Data to send
     * @param data_size Size of data
     * @return True if SDO successful
     */
    bool sendSDO(int axis_id, uint16_t index, uint8_t subindex, 
                 const void* data, size_t data_size);

    /**
     * @brief Receive SDO (Service Data Object) message
     * @param axis_id Axis identifier
     * @param index Object dictionary index
     * @param subindex Object dictionary subindex
     * @param data Buffer for received data
     * @param data_size Size of buffer
     * @return Size of received data, or -1 on error
     */
    int receiveSDO(int axis_id, uint16_t index, uint8_t subindex,
                   void* data, size_t data_size);

    /**
     * @brief Configure PDO (Process Data Object)
     * @param axis_id Axis identifier
     * @param pdo_number PDO number (1-4)
     * @param mapping PDO mapping configuration
     * @return True if configuration successful
     */
    bool configurePDO(int axis_id, int pdo_number, const std::vector<uint32_t>& mapping);

    /**
     * @brief Enable PDO transmission
     * @param axis_id Axis identifier
     * @param pdo_number PDO number
     * @param enable True to enable
     */
    void enablePDO(int axis_id, int pdo_number, bool enable);

    /**
     * @brief Send NMT (Network Management) command to a CANopen node
     * @param node_id CANopen node ID (1-127), 0 = all nodes
     * @param command NMT command
     * @return True if command sent successfully
     */
    bool sendNMT(uint8_t node_id, uint8_t command);

    /**
     * @brief Send SYNC message
     */
    void sendSync();

    /**
     * @brief Get CAN bus statistics
     * @return Statistics string
     */
    std::string getStatistics() const;

    /**
     * @brief Save configuration to file
     * @param filename File to save to
     * @return True if save successful
     */
    bool saveConfiguration(const std::string& filename) const;

    /**
     * @brief Load configuration from file
     * @param filename File to load from
     * @return True if load successful
     */
    bool loadConfiguration(const std::string& filename);

    /**
     * @brief Trajectory generator types
     */
    enum TrajectoryType {
        TRAPEZOIDAL,    // Trapezoidal velocity profile
        S_SHAPE,        // S-curve (jerk-limited) profile
        SINE,           // Sine-based smooth profile
        POLYNOMIAL      // Polynomial profile
    };

    /**
     * @brief Trajectory parameters
     */
    struct TrajectoryParams {
        TrajectoryType type;
        double max_velocity;      // Maximum velocity in deg/s
        double max_acceleration;  // Maximum acceleration in deg/s²
        double max_jerk;          // Maximum jerk in deg/s³ (for S-shape)
        double start_position;    // Start position in degrees
        double target_position;   // Target position in degrees
        double update_rate;       // Update rate in Hz
    };

    /**
     * @brief Trajectory point
     */
    struct TrajectoryPoint {
        double position;          // Position in degrees
        double velocity;          // Velocity in deg/s
        double acceleration;      // Acceleration in deg/s²
        double jerk;              // Jerk in deg/s³
        double time;              // Time from start in seconds
    };

    /**
     * @brief Generate trajectory
     * @param params Trajectory parameters
     * @return Vector of trajectory points
     */
    std::vector<TrajectoryPoint> generateTrajectory(const TrajectoryParams& params);

    /**
     * @brief Execute trajectory
     * @param axis_id Axis identifier
     * @param trajectory Trajectory to execute
     * @param callback Callback for each point (optional)
     * @return True if execution started
     */
    bool executeTrajectory(int axis_id, const std::vector<TrajectoryPoint>& trajectory,
                          std::function<void(const TrajectoryPoint&)> callback = nullptr);

    /**
     * @brief Get cached NMT state from real heartbeat (no SDO traffic).
     * @param axis_id Axis identifier (0=RA/Azimuth, 1=Dec/Altitude)
     * @return CiA 301 NMT state: 0x00=Bootup, 0x04=Stopped, 0x05=Operational, 0x7F=Pre-Op
     */
    uint8_t getNodeNMTState(int axis_id) const;

    /**
     * @brief Check if a drive is currently enabled (cached, no SDO traffic).
     *
     * Returns the last known enabled state set by enableDrive() /
     * disableDrive() / emergencyStop().  Use this for fast checks
     * before attempting SDO operations that would time out if the
     * drive is stopped.
     */
    bool isDriveEnabled(int axis_id) const;

    /**
     * @brief Check if the last heartbeat for this axis is recent.
     * @param axis_id Axis identifier
     * @param max_age_ms Maximum acceptable age of the last heartbeat in ms
     * @return True if a heartbeat was received within max_age_ms
     */
    bool isHeartbeatRecent(int axis_id, int max_age_ms) const;

private:
    class Impl;
    std::shared_ptr<Impl> pimpl;
};

} // namespace controllers
} // namespace astro_mount

#endif // CANOPEN_INTERFACE_H