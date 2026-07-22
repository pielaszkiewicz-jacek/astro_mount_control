#pragma once
#include <cstdint>
#include <string>
#include <functional>
#include <chrono>

namespace astro_mount {
namespace controllers {

/// Status 1: temperature, voltage, current, motor state, error flags
struct Mf7025v2Status1 {
    int8_t  temperature;      // °C
    int16_t voltage;          // 0.01V/LSB
    int16_t current;          // 0.01A/LSB
    uint8_t motor_state;      // 0x00=ON, 0x10=OFF
    uint8_t error_state;      // bit flags
    std::chrono::steady_clock::time_point timestamp;
};

/// Status 2: temperature, iq, speed, encoder
struct Mf7025v2Status2 {
    int8_t  temperature;      // °C
    int16_t iq;               // torque current (MF: 33/4096 A/LSB)
    int16_t speed;            // 1 dps/LSB
    uint16_t encoder;         // encoder position
    std::chrono::steady_clock::time_point timestamp;
};

/// Encoder data: position, raw, offset
struct Mf7025v2EncoderData {
    uint16_t encoder;         // position with offset
    uint16_t encoder_raw;     // raw position
    uint16_t encoder_offset;  // zero offset
};

/**
 * @brief Abstract interface for MF7025v2 (LingKong BLDC Servo) CAN communication.
 *
 * Implements the LingKong CAN protocol V2.36:
 * - CAN ID = 0x140 + node_id (1..32)
 * - Standard frame (11-bit), DLC=8
 * - Request-response, response time < 0.25ms
 *
 * Provides high-level commands mapped from raw protocol frames:
 *   Motor control: 0x80 (Off), 0x81 (Stop), 0x88 (Run)
 *   Status: 0x9A (Status1), 0x9C (Status2), 0x9B (Clear Errors)
 *   Motion: 0xA1 (Torque), 0xA2 (Speed), 0xA3/A4 (Multi-turn pos), 0xA7/A8 (Incremental)
 *   Encoder: 0x90 (Read), 0x92 (Multi-turn angle), 0x18 (Calibrate)
 *   Config: 0xC0/C1 (Control params), 0x40/0x42 (Setting params), 0x44 (Save)
 */
class IMf7025v2Interface {
public:
    virtual ~IMf7025v2Interface() = default;

    // ─── Lifecycle ───────────────────────────────────────────────
    virtual bool initialize(const std::string& can_interface, uint32_t baud_rate) = 0;
    virtual void shutdown() = 0;
    virtual bool isInitialized() const = 0;

    // ─── Raw CAN frame I/O ───────────────────────────────────────
    virtual bool sendFrame(uint8_t node_id, const uint8_t data[8]) = 0;
    virtual bool receiveFrame(uint8_t& node_id, uint8_t data[8], int timeout_ms) = 0;

    /// Send command and wait for matching response
    virtual bool executeCommand(uint8_t node_id, const uint8_t cmd[8],
                                uint8_t response[8], int timeout_ms) = 0;

    // ─── Motor control ──────────────────────────────────────────
    virtual bool motorEnable(uint8_t node_id) = 0;       // 0x88
    virtual bool motorDisable(uint8_t node_id) = 0;      // 0x80
    virtual bool motorStop(uint8_t node_id) = 0;          // 0x81
    virtual bool clearErrors(uint8_t node_id) = 0;        // 0x9B
    virtual bool motorRestart(uint8_t node_id) = 0;       // 0x07 (no response)

    // ─── Status ─────────────────────────────────────────────────
    virtual Mf7025v2Status1 readStatus1(uint8_t node_id) = 0;   // 0x9A
    virtual Mf7025v2Status2 readStatus2(uint8_t node_id) = 0;   // 0x9C

    // ─── Motion control ─────────────────────────────────────────
    virtual bool setTorque(uint8_t node_id, int16_t iq_control) = 0;                             // 0xA1
    virtual bool setVelocity(uint8_t node_id, int32_t speed_control, int16_t torque_limit) = 0;   // 0xA2
    virtual bool setMultiTurnPosition(uint8_t node_id, int32_t angle_control) = 0;                // 0xA3
    virtual bool setMultiTurnPositionWithSpeed(uint8_t node_id, int32_t angle_control,
                                               uint16_t max_speed) = 0;                           // 0xA4
    virtual bool setIncrementalPosition(uint8_t node_id, int32_t increment) = 0;                  // 0xA7
    virtual bool setIncrementalPositionWithSpeed(uint8_t node_id, int32_t increment,
                                                 uint16_t max_speed) = 0;                         // 0xA8

    // ─── Encoder ────────────────────────────────────────────────
    virtual int64_t  readMultiTurnAngle(uint8_t node_id) = 0;      // 0x92, 0.01°/LSB
    virtual uint32_t readSingleTurnAngle(uint8_t node_id) = 0;     // 0x94, 0.01°/LSB
    virtual Mf7025v2EncoderData readEncoderData(uint8_t node_id) = 0;  // 0x90
    virtual bool calibrateEncoder(uint8_t node_id) = 0;             // 0x18
    virtual bool setZeroROM(uint8_t node_id, int32_t encoder_offset) = 0;  // 0x19
    virtual bool setZeroRAM(uint8_t node_id) = 0;                   // 0x95

    // ─── Brake ──────────────────────────────────────────────────
    virtual bool brakeRelease(uint8_t node_id) = 0;       // 0x8C, data[0]=0x01
    virtual bool brakeEngage(uint8_t node_id) = 0;        // 0x8C, data[0]=0x00
    virtual bool readBrakeStatus(uint8_t node_id) = 0;    // 0x8C, data[0]=0x10

    // ─── Parameters ─────────────────────────────────────────────
    virtual bool readControlParam(uint8_t node_id, uint8_t param_id, uint8_t data_out[6]) = 0;
    virtual bool writeControlParam(uint8_t node_id, uint8_t param_id, const uint8_t data[6]) = 0;
    virtual bool saveSettings(uint8_t node_id) = 0;               // 0x44
};

/**
 * @brief Factory function to create a SocketCAN-based MF7025v2 interface.
 *
 * Creates a concrete implementation of IMf7025v2Interface using Linux SocketCAN.
 * On non-Linux platforms, the returned interface will fail to initialize.
 *
 * @return std::unique_ptr<IMf7025v2Interface>  The CAN interface instance
 */
std::unique_ptr<IMf7025v2Interface> createMf7025v2CanInterface();

} // namespace controllers
} // namespace astro_mount
