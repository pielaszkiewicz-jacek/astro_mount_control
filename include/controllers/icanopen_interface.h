#ifndef ICANOPEN_INTERFACE_H
#define ICANOPEN_INTERFACE_H

#include <string>
#include <memory>
#include <functional>
#include <vector>
#include <chrono>
#include <cstdint>

namespace astro_mount {
namespace controllers {

/**
 * @brief Abstract CANopen interface for astronomical mount control
 * 
 * Defines common interface for different CANopen implementations:
 * - Mock implementation for simulation/testing
 * - Real implementation using CANopen libraries (CANopenSocket, libedssharp, etc.)
 * 
 * Implements CANopen protocol (CiA 301, CiA 402) for controlling
 * servo drives and encoders in astronomical mounts.
 */
class ICanOpenInterface {
public:
    struct Config {
        std::string interface_name;      // e.g., "can0", "vcan0", "mock"
        int bitrate;                     // CAN bitrate (125000, 250000, 500000, 1000000)
        int node_id;                     // CANopen node ID (1-127)
        bool use_sync;                   // Use SYNC messages
        int sync_period_ms;              // SYNC period in milliseconds
        int pdo_mapping[4];              // PDO mapping configuration
        int sdo_timeout_ms;              // SDO timeout in milliseconds
        std::string library;             // Library to use: "mock", "canopensocket", "libedssharp", "canfestival"
        double position_counts_per_degree = 1000.0 / 360.0;
        double velocity_counts_per_deg_s = 1000.0 / 360.0;
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
        bool limit_switch_active;        // Limit switch active
        double temperature;              // Drive temperature in °C
        double current;                  // Drive current in A
        bool communication_ok;           // Communication status
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

    virtual ~ICanOpenInterface() = default;

    /**
     * @brief Initialize CANopen interface
     * @param config Configuration
     * @return True if initialization successful
     */
    virtual bool initialize(const Config& config) = 0;

    /**
     * @brief Shutdown CANopen interface
     */
    virtual void shutdown() = 0;

    /**
     * @brief Connect to CAN network
     * @return True if connection successful
     */
    virtual bool connect() = 0;

    /**
     * @brief Disconnect from CAN network
     */
    virtual void disconnect() = 0;

    /**
     * @brief Check if connected
     * @return True if connected
     */
    virtual bool isConnected() const = 0;

    /**
     * @brief Configure drive for axis
     * @param axis_id Axis identifier (0=RA/Azimuth, 1=Dec/Altitude)
     * @param config_string Configuration string
     * @return True if configuration successful
     */
    virtual bool configureDrive(int axis_id, const std::string& config_string) = 0;

    /**
     * @brief Enable drive
     * @param axis_id Axis identifier
     * @return True if enable successful
     */
    virtual bool enableDrive(int axis_id) = 0;

    /**
     * @brief Disable drive
     * @param axis_id Axis identifier
     */
    virtual void disableDrive(int axis_id) = 0;

    /**
     * @brief Set position target
     * @param axis_id Axis identifier
     * @param position Target position in degrees
     * @param velocity Max velocity in deg/s
     * @param acceleration Acceleration in deg/s²
     * @return True if command accepted
     */
    virtual bool setPositionTarget(int axis_id, double position, double velocity, double acceleration) = 0;

    /**
     * @brief Set velocity target
     * @param axis_id Axis identifier
     * @param velocity Target velocity in deg/s
     * @param acceleration Acceleration in deg/s²
     * @return True if command accepted
     */
    virtual bool setVelocityTarget(int axis_id, double velocity, double acceleration) = 0;

    /**
     * @brief Stop axis (quick stop)
     * @param axis_id Axis identifier
     */
    virtual void stopAxis(int axis_id) = 0;

    /**
     * @brief Emergency stop
     * @param axis_id Axis identifier
     */
    virtual void emergencyStop(int axis_id) = 0;

    /**
     * @brief Clear errors
     * @param axis_id Axis identifier
     * @return True if errors cleared
     */
    virtual bool clearErrors(int axis_id) = 0;

    /**
     * @brief Get drive status
     * @param axis_id Axis identifier
     * @return Drive status
     */
    virtual DriveStatus getDriveStatus(int axis_id) const = 0;

    /**
     * @brief Get position data
     * @param axis_id Axis identifier
     * @return Position data
     */
    virtual PositionData getPositionData(int axis_id) const = 0;

    /**
     * @brief Get encoder data
     * @param axis_id Axis identifier
     * @return Encoder data
     */
    virtual EncoderData getEncoderData(int axis_id) const = 0;

    /**
     * @brief Set status callback
     * @param callback Callback function
     */
    virtual void setStatusCallback(StatusCallback callback) = 0;

    /**
     * @brief Set position callback
     * @param callback Callback function
     */
    virtual void setPositionCallback(PositionCallback callback) = 0;

    /**
     * @brief Set encoder callback
     * @param callback Callback function
     */
    virtual void setEncoderCallback(EncoderCallback callback) = 0;

    /**
     * @brief Set error callback
     * @param callback Callback function
     */
    virtual void setErrorCallback(ErrorCallback callback) = 0;

    /**
     * @brief Send SDO (Service Data Object) message
     * @param axis_id Axis identifier
     * @param index Object dictionary index
     * @param subindex Object dictionary subindex
     * @param data Data to send
     * @param data_size Size of data
     * @return True if SDO successful
     */
    virtual bool sendSDO(int axis_id, uint16_t index, uint8_t subindex, 
                         const void* data, size_t data_size) = 0;

    /**
     * @brief Receive SDO (Service Data Object) message
     * @param axis_id Axis identifier
     * @param index Object dictionary index
     * @param subindex Object dictionary subindex
     * @param data Buffer for received data
     * @param data_size Size of buffer
     * @return Size of received data, or -1 on error
     */
    virtual int receiveSDO(int axis_id, uint16_t index, uint8_t subindex,
                           void* data, size_t data_size) = 0;

    /**
     * @brief Configure PDO (Process Data Object)
     * @param axis_id Axis identifier
     * @param pdo_number PDO number (1-4)
     * @param mapping PDO mapping configuration
     * @return True if configuration successful
     */
    virtual bool configurePDO(int axis_id, int pdo_number, const std::vector<uint32_t>& mapping) = 0;

    /**
     * @brief Enable PDO transmission
     * @param axis_id Axis identifier
     * @param pdo_number PDO number
     * @param enable True to enable
     */
    virtual void enablePDO(int axis_id, int pdo_number, bool enable) = 0;

    /**
     * @brief Send NMT (Network Management) command to a CANopen node
     * @param node_id CANopen node ID (1-127), 0 = all nodes
     * @param command NMT command (CiA 301 §9.2.1):
     *        0x01 = Start Remote Node
     *        0x02 = Stop Remote Node
     *        0x80 = Enter Pre-Operational
     *        0x81 = Reset Node
     *        0x82 = Reset Communication
     * @return True if command sent successfully
     */
    virtual bool sendNMT(uint8_t node_id, uint8_t command) = 0;

    /**
     * @brief Send SYNC message
     */
    virtual void sendSync() = 0;

    /**
     * @brief Get CAN bus statistics
     * @return Statistics string
     */
    virtual std::string getStatistics() const = 0;

    /**
     * @brief Save configuration to file
     * @param filename File to save to
     * @return True if save successful
     */
    virtual bool saveConfiguration(const std::string& filename) const = 0;

    /**
     * @brief Load configuration from file
     * @param filename File to load from
     * @return True if load successful
     */
    virtual bool loadConfiguration(const std::string& filename) = 0;

    /**
     * @brief Get implementation type
     * @return Implementation type string
     */
    virtual std::string getImplementationType() const = 0;

    /**
     * @brief Check if implementation supports simulation
     * @return True if simulation is supported
     */
    virtual bool supportsSimulation() const = 0;

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
    virtual std::vector<TrajectoryPoint> generateTrajectory(const TrajectoryParams& params) = 0;

    /**
     * @brief Execute trajectory
     * @param axis_id Axis identifier
     * @param trajectory Trajectory to execute
     * @param callback Callback for each point (optional)
     * @return True if execution started
     */
    virtual bool executeTrajectory(int axis_id, const std::vector<TrajectoryPoint>& trajectory,
                                  std::function<void(const TrajectoryPoint&)> callback = nullptr) = 0;

    /**
     * @brief Get cached NMT state from real heartbeat reception (no SDO traffic).
     * @param axis_id Axis identifier (0=RA/Azimuth, 1=Dec/Altitude)
     * @return CiA 301 NMT state: 0x00=Bootup, 0x04=Stopped, 0x05=Operational, 0x7F=Pre-Op
     */
    virtual uint8_t getNodeNMTState(int axis_id) const { return 0x00; }

    /**
     * @brief Check if the last heartbeat for this axis is recent (received via real
     *        heartbeat callback, not SDO polling).
     * @param axis_id Axis identifier
     * @param max_age_ms Maximum acceptable age of the last heartbeat in ms
     * @return True if a heartbeat was received within max_age_ms
     */
    virtual bool isHeartbeatRecent(int axis_id, int max_age_ms) const { return false; }

    /**
     * @brief Check if a drive is currently enabled (cached, no SDO traffic).
     */
    virtual bool isDriveEnabled(int axis_id) const { return false; }
};

} // namespace controllers
} // namespace astro_mount

#endif // ICANOPEN_INTERFACE_H