#pragma once
// Abstract CANopen/CiA 402 interface (NiMotion STMP42SXI).
// Concrete implementation: canopen_interface.cpp (SocketCAN, Linux only).

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace astro_mount {
namespace controllers {

struct CanOpenStatus {
    uint16_t status_word{0};
    uint8_t  mode_display{0};
    int32_t  actual_position{0};
    int32_t  actual_velocity{0};
    int16_t  actual_current{0};
    bool     position_valid{false};
    bool     enabled{false};
    bool     moving{false};
    bool     target_reached{false};
    bool     fault{false};
};

struct CanOpenPositionData {
    int32_t actual_position{0};
    int32_t demand_position{0};
    int32_t actual_velocity{0};
};

class ICanOpenInterface {
public:
    struct Config {
        std::string library{"canopensocket"};
        std::string interface_name{"can0"};
        uint32_t    bitrate{1000000};
        uint8_t     node_id{1};
        uint32_t    sdo_timeout_ms{1000};
        uint32_t    pdo_update_rate{100};
        std::string accel_mode{"rate"};        // "rate" (6083=acceleration) | "time" (ramp time)
        bool        pdo_config_enabled{false};
        bool        position_rewind_enabled{true};
        double      position_rewind_interval_seconds{3600.0};
        double      position_rewind_threshold_percent{80.0};
        // axis_id -> CANopen node_id mapping (default: axis 0 = node 1, axis 1 = node 2)
        std::vector<uint8_t> axis_node_ids{1, 2};
    };

    virtual ~ICanOpenInterface() = default;

    virtual bool initialize(const Config& config) = 0;
    virtual void shutdown() = 0;
    virtual bool isInitialized() const = 0;

    // CiA 402 state machine
    virtual bool enableDrive(uint8_t axis_id) = 0;
    virtual bool disableDrive(uint8_t axis_id) = 0;

    // Motion
    virtual bool setPositionTarget(uint8_t axis_id, int32_t position,
                                   uint32_t velocity, uint32_t acceleration) = 0;

    // Relative profile position move (CiA 402 control-word bit6 = 1). The
    // position argument is a delta from the current position, so it does not
    // depend on a freshly-read absolute position. Default: falls back to an
    // absolute move for backends that do not support true relative positioning.
    virtual bool setPositionTargetRelative(uint8_t axis_id, int32_t position,
                                           uint32_t velocity, uint32_t acceleration) {
        return setPositionTarget(axis_id, position, velocity, acceleration);
    }
    virtual bool setVelocityTarget(uint8_t axis_id, int32_t velocity,
                                   uint32_t acceleration) = 0;
    virtual bool stopAxis(uint8_t axis_id) = 0;
    virtual bool emergencyStop(uint8_t axis_id) = 0;

    // Status
    virtual bool getDriveStatus(uint8_t axis_id, CanOpenStatus& status) = 0;
    virtual bool getPositionData(uint8_t axis_id, CanOpenPositionData& data) = 0;
    virtual bool clearErrors(uint8_t axis_id) = 0;

    // Raw SDO access
    virtual bool sendSDO(uint8_t axis_id, uint16_t index, uint8_t subindex,
                         uint32_t value, uint8_t size) = 0;
    virtual bool readSDO(uint8_t axis_id, uint16_t index, uint8_t subindex,
                         uint32_t& value, uint8_t& size) = 0;

    // NMT + system configuration
    virtual bool sendNMT(uint8_t axis_id, uint8_t command) = 0;
    virtual bool setNodeId(uint8_t axis_id, uint8_t new_node_id) = 0;
    virtual bool getNodeId(uint8_t axis_id, uint8_t& node_id) = 0;
    virtual bool setBaudRate(uint8_t axis_id, uint8_t baud_code) = 0;
    virtual bool saveParameters(uint8_t axis_id) = 0;

    // Heartbeat producer (1017h) — enables NMT monitoring.
    virtual bool setHeartbeatPeriod(uint8_t axis_id, uint16_t ms) = 0;

    // Homing mode (6098h/6099h/609Ah).
    virtual bool setHomingParameters(uint8_t axis_id, uint8_t method,
                                     uint32_t speed_switch, uint32_t speed_zero,
                                     uint32_t acceleration) = 0;
    virtual bool startHoming(uint8_t axis_id) = 0;

    // PDO configuration (TPDO1: 6041h+6064h, RPDO2: 6040h+607Ah).
    virtual bool configurePdo(uint8_t axis_id, bool enable) = 0;

    // Drain pending heartbeat/EMCY/PDO frames into the callbacks (non-blocking).
    // Returns true if at least one frame was processed.
    virtual bool pollEvents() { return false; }
};

} // namespace controllers
} // namespace astro_mount
