#pragma once
// Abstract interface for MF7025v2 (LingKong BLDC) CAN bus communication.
// Provides raw access to the proprietary CAN V2.36 protocol.
// Concrete implementation: Mf7025v2CanInterface (SocketCAN, Linux only).

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace astro_mount {
namespace controllers {

struct Mf7025v2Status {
    int8_t   temperature{0};       // °C
    int16_t  voltage{0};           // 0.01V
    int16_t  current{0};           // 0.01A
    int16_t  iq{0};                // torque current (33/4096 A/LSB for MF)
    int16_t  speed{0};             // 1 dps/LSB
    uint16_t encoder{0};           // raw encoder counts
    uint8_t  motor_state{0};       // 0=stopped, 1=running
    uint8_t  error_state{0};       // bitfield
};

class IMf7025v2Interface {
public:
    virtual ~IMf7025v2Interface() = default;

    // Lifecycle
    virtual bool open(const std::string& can_iface, uint32_t bitrate) = 0;
    virtual void close() = 0;
    virtual bool isOpen() const = 0;

    // Diagnostics
    virtual void setCanTrace(bool enabled) = 0;
    virtual void setCanTraceReadState(bool enabled) = 0;

    // Motor power
    virtual bool motorRun(uint8_t node_id) = 0;       // 0x88
    virtual bool motorOff(uint8_t node_id) = 0;       // 0x80
    virtual bool motorStop(uint8_t node_id) = 0;      // 0x81

    // Status
    virtual bool readStatus1(uint8_t node_id, Mf7025v2Status& status) = 0;  // 0x9A
    virtual bool readStatus2(uint8_t node_id, Mf7025v2Status& status) = 0;  // 0x9C
    virtual bool clearErrors(uint8_t node_id) = 0;    // 0x9B

    // Motion control
    // Speed control (0xA2): iqControl(int16) + speedControl(int32, 0.01dps)
    virtual bool speedControl(uint8_t node_id, int16_t iq, int32_t speed_001dps) = 0;

    // Multi-turn position (0xA4): maxSpeed(uint16, 1dps) + angleControl(int32, 0.01°)
    virtual bool positionControl2(uint8_t node_id, uint16_t max_speed_dps,
                                  int32_t angle_001deg) = 0;

    // Multi-turn position without speed limit (0xA3)
    virtual bool positionControl1(uint8_t node_id, int32_t angle_001deg) = 0;

    // Torque control (0xA1): iqControl(int16)
    virtual bool torqueControl(uint8_t node_id, int16_t iq) = 0;

    // Encoder
    virtual bool readEncoder(uint8_t node_id,
                             uint16_t& encoder, uint16_t& raw,
                             uint16_t& offset) = 0;        // 0x90
    virtual bool readMultiTurnAngle(uint8_t node_id, int64_t& angle_001deg) = 0; // 0x92
    virtual bool readSingleTurnAngle(uint8_t node_id, uint32_t& angle_001deg) = 0; // 0x94

    // Zero position
    virtual bool setZeroRAM(uint8_t node_id) = 0;              // 0x95 — set current pos as zero (RAM)

    // Configuration
    virtual bool readParam(uint8_t node_id, uint8_t param_id,
                           std::vector<uint8_t>& data) = 0;   // 0xC0
    virtual bool writeParam(uint8_t node_id, uint8_t param_id,
                            const std::vector<uint8_t>& data) = 0; // 0xC1

    // Combined PID write to RAM (0x31).  One frame overwrites all three loops
    // (current, speed, position) at once:
    //   DATA[1] = 0x00 (reserved)
    //   DATA[2..3] = current Kp/Ki
    //   DATA[4..5] = speed Kp/Ki
    //   DATA[6..7] = position Kp/Ki
    // All values are single bytes (0..255).  Kd is not part of this command.
    virtual bool writePidRam(uint8_t node_id,
                             uint8_t cur_kp, uint8_t cur_ki,
                             uint8_t spd_kp, uint8_t spd_ki,
                             uint8_t pos_kp, uint8_t pos_ki) = 0; // 0x31
};

// Factory function — creates the Linux SocketCAN implementation.
// Returns nullptr on non-Linux platforms.
std::unique_ptr<IMf7025v2Interface> createMf7025v2CanInterface();

} // namespace controllers
} // namespace astro_mount
