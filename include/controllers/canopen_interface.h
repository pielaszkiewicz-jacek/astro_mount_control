#pragma once
// CANopen/CiA 402 concrete interface for NiMotion STMP42SXI over SocketCAN.

#include "controllers/icanopen_interface.h"
#include "canopen/canopen.h"

#include <atomic>
#include <mutex>
#include <string>

namespace astro_mount {
namespace controllers {

class CanOpenInterface : public ICanOpenInterface {
public:
    CanOpenInterface();
    ~CanOpenInterface() override;

    bool initialize(const Config& config) override;
    void shutdown() override;
    bool isInitialized() const override;

    bool enableDrive(uint8_t axis_id) override;
    bool disableDrive(uint8_t axis_id) override;

    bool setPositionTarget(uint8_t axis_id, int32_t position,
                           uint32_t velocity, uint32_t acceleration) override;
    bool setPositionTargetRelative(uint8_t axis_id, int32_t position,
                                   uint32_t velocity, uint32_t acceleration) override;
    bool setVelocityTarget(uint8_t axis_id, int32_t velocity,
                           uint32_t acceleration) override;
    bool stopAxis(uint8_t axis_id) override;
    bool emergencyStop(uint8_t axis_id) override;

    bool getDriveStatus(uint8_t axis_id, CanOpenStatus& status) override;
    bool getPositionData(uint8_t axis_id, CanOpenPositionData& data) override;
    bool clearErrors(uint8_t axis_id) override;

    bool sendSDO(uint8_t axis_id, uint16_t index, uint8_t subindex,
                 uint32_t value, uint8_t size) override;
    bool readSDO(uint8_t axis_id, uint16_t index, uint8_t subindex,
                 uint32_t& value, uint8_t& size) override;

    bool sendNMT(uint8_t axis_id, uint8_t command) override;
    bool setNodeId(uint8_t axis_id, uint8_t new_node_id) override;
    bool getNodeId(uint8_t axis_id, uint8_t& node_id) override;
    bool setBaudRate(uint8_t axis_id, uint8_t baud_code) override;
    bool saveParameters(uint8_t axis_id) override;

    bool setHeartbeatPeriod(uint8_t axis_id, uint16_t ms) override;
    bool setHomingParameters(uint8_t axis_id, uint8_t method,
                             uint32_t speed_switch, uint32_t speed_zero,
                             uint32_t acceleration) override;
    bool startHoming(uint8_t axis_id) override;
    bool configurePdo(uint8_t axis_id, bool enable) override;
    bool pollEvents() override;

    // Direct access to the underlying C context (for CanOpenHAL NMT monitor).
    canopen_ctx_t* rawContext() { return &ctx_; }

private:
    uint8_t nodeIdForAxis(uint8_t axis_id) const;
    bool writeControlWord(uint8_t node_id, uint16_t value);
    bool readStatusWord(uint8_t node_id, uint16_t& value);
    bool writeMode(uint8_t node_id, uint8_t mode);
    bool switchMode(uint8_t node_id, uint8_t mode);
    bool writeSDO2(uint8_t node_id, uint16_t index, uint8_t subindex, uint16_t value);
    bool writeSDO4(uint8_t node_id, uint16_t index, uint8_t subindex, uint32_t value);

    static void pdoCallback(uint32_t cob_id, const uint8_t* data, uint8_t dlc,
                            void* userdata);
    struct PdoCache {
        uint16_t status_word{0};
        int32_t position{0};
        bool valid{false};
    };
    PdoCache pdo_cache_[128];

    Config config_;
    canopen_ctx_t ctx_;
    std::atomic<bool> initialized_{false};
    mutable std::mutex mutex_;
    uint8_t mode_cache_[128];
};

} // namespace controllers
} // namespace astro_mount
