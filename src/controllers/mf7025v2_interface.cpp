// MF7025v2 CAN interface — Linux only (SocketCAN)
// ================================================
// This implementation uses Linux SocketCAN (AF_CAN, SOCK_RAW) which is
// only available on Linux. Building on other platforms will fail.
#include "controllers/imf7025v2_interface.h"
#include "logging/logger.h"

#ifndef __linux__
#error "MF7025v2 CAN interface requires Linux (SocketCAN). Use CANopen HAL on other platforms."
#endif

#include <cstring>
#include <cerrno>
#include <thread>
#include <chrono>
#include <algorithm>

#include <sys/socket.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <linux/can.h>
#include <linux/can/raw.h>
#include <unistd.h>
#include <fcntl.h>

namespace astro_mount {
namespace controllers {

namespace {
    constexpr const char* LOG_TAG = "mf7025v2";

    /// Build CAN ID for a command: 0x140 + node_id
    inline canid_t makeCanId(uint8_t node_id) {
        return static_cast<canid_t>(0x140 + node_id);
    }

}

class Mf7025v2CanInterface : public IMf7025v2Interface {
public:
    Mf7025v2CanInterface() = default;
    ~Mf7025v2CanInterface() override { shutdown(); }

    // ─── Lifecycle ───────────────────────────────────────────────

    bool initialize(const std::string& can_interface, uint32_t baud_rate) override {
        if (initialized_) return true;

        // Create CAN socket
        socket_fd_ = socket(PF_CAN, SOCK_RAW, CAN_RAW);
        if (socket_fd_ < 0) {
            MF_LOG_ERROR("Cannot create CAN socket: {}", strerror(errno));
            return false;
        }

        // Look up interface index
        struct ifreq ifr;
        std::strncpy(ifr.ifr_name, can_interface.c_str(), IFNAMSIZ - 1);
        ifr.ifr_name[IFNAMSIZ - 1] = '\0';
        if (ioctl(socket_fd_, SIOCGIFINDEX, &ifr) < 0) {
            MF_LOG_ERROR("Cannot find CAN interface {}: {}", can_interface, strerror(errno));
            close(socket_fd_);
            socket_fd_ = -1;
            return false;
        }

        // Bind socket
        struct sockaddr_can addr;
        std::memset(&addr, 0, sizeof(addr));
        addr.can_family = AF_CAN;
        addr.can_ifindex = ifr.ifr_ifindex;
        if (bind(socket_fd_, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
            MF_LOG_ERROR("Cannot bind CAN socket: {}", strerror(errno));
            close(socket_fd_);
            socket_fd_ = -1;
            return false;
        }

        // Set receive timeout
        struct timeval tv;
        tv.tv_sec = 0;
        tv.tv_usec = 100000; // 100ms default timeout
        setsockopt(socket_fd_, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

        interface_name_ = can_interface;
        initialized_ = true;
        MF_LOG_INFO("MF7025v2 CAN interface initialized on {}", can_interface);
        return true;
    }

    void shutdown() override {
        if (socket_fd_ >= 0) {
            close(socket_fd_);
            socket_fd_ = -1;
        }
        initialized_ = false;
    }

    bool isInitialized() const override {
        return initialized_;
    }

    // ─── Raw CAN frame I/O ───────────────────────────────────────
    // Public methods are thread-safe wrappers around the locked internal versions.
    // The locked versions (sendFrameLocked/receiveFrameLocked) are used by executeCommand
    // which already holds the mutex to avoid deadlock.

    bool sendFrame(uint8_t node_id, const uint8_t data[8]) override {
        std::lock_guard<std::mutex> lock(mutex_);
        return sendFrameLocked(node_id, data);
    }

    bool receiveFrame(uint8_t& node_id, uint8_t data[8], int timeout_ms) override {
        std::lock_guard<std::mutex> lock(mutex_);
        return receiveFrameLocked(node_id, data, timeout_ms);
    }

    bool sendFrameLocked(uint8_t node_id, const uint8_t data[8]) {
        // Internal: caller must hold mutex_
        if (socket_fd_ < 0) return false;

        struct can_frame frame;
        frame.can_id = makeCanId(node_id);
        frame.can_dlc = 8;
        std::memcpy(frame.data, data, 8);

        int nbytes = write(socket_fd_, &frame, sizeof(frame));
        if (nbytes != sizeof(frame)) {
            MF_LOG_ERROR("CAN write failed: {}", strerror(errno));
            return false;
        }
        return true;
    }

    bool receiveFrameLocked(uint8_t& node_id, uint8_t data[8], int timeout_ms) {
        // Internal: caller must hold mutex_
        if (socket_fd_ < 0) return false;

        struct can_frame frame;
        // Update timeout if needed
        if (timeout_ms > 0 && timeout_ms != 100) {
            struct timeval tv;
            tv.tv_sec = timeout_ms / 1000;
            tv.tv_usec = (timeout_ms % 1000) * 1000;
            setsockopt(socket_fd_, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        }

        int nbytes = read(socket_fd_, &frame, sizeof(frame));
        if (nbytes < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                return false; // timeout
            }
            MF_LOG_ERROR("CAN read failed: {}", strerror(errno));
            return false;
        }
        if (nbytes < static_cast<int>(sizeof(struct can_frame))) {
            return false;
        }

        // Extract node_id from CAN ID: response ID = 0x140 + node_id
        node_id = static_cast<uint8_t>(frame.can_id - 0x140);
        std::memcpy(data, frame.data, 8);
        return true;
    }

    bool executeCommand(uint8_t node_id, const uint8_t cmd[8],
                        uint8_t response[8], int timeout_ms) override {
        std::lock_guard<std::mutex> lock(mutex_);

        if (!sendFrameLocked(node_id, cmd)) return false;

        // Wait for response with matching node_id and echoed command byte
        auto deadline = std::chrono::steady_clock::now() +
                        std::chrono::milliseconds(timeout_ms > 0 ? timeout_ms : 100);

        uint8_t rx_node = 0;
        uint8_t rx_data[8] = {0};

        while (std::chrono::steady_clock::now() < deadline) {
            if (receiveFrameLocked(rx_node, rx_data, 50)) {
                // Verify: response should match node_id AND echo the command byte
                if (rx_node == node_id && rx_data[0] == cmd[0]) {
                    std::memcpy(response, rx_data, 8);
                    return true;
                }
            }
        }

        MF_LOG_WARN("MF7025v2 executeCommand timeout for node {}", node_id);
        return false;
    }

    // ─── Motor control ──────────────────────────────────────────

    bool motorEnable(uint8_t node_id) override {
        uint8_t cmd[8] = {0x88, 0, 0, 0, 0, 0, 0, 0};
        uint8_t resp[8] = {0};
        return executeCommand(node_id, cmd, resp, 100);
    }

    bool motorDisable(uint8_t node_id) override {
        uint8_t cmd[8] = {0x80, 0, 0, 0, 0, 0, 0, 0};
        uint8_t resp[8] = {0};
        return executeCommand(node_id, cmd, resp, 100);
    }

    bool motorStop(uint8_t node_id) override {
        uint8_t cmd[8] = {0x81, 0, 0, 0, 0, 0, 0, 0};
        uint8_t resp[8] = {0};
        return executeCommand(node_id, cmd, resp, 100);
    }

    bool clearErrors(uint8_t node_id) override {
        uint8_t cmd[8] = {0x9B, 0, 0, 0, 0, 0, 0, 0};
        uint8_t resp[8] = {0};
        return executeCommand(node_id, cmd, resp, 100);
    }

    bool motorRestart(uint8_t node_id) override {
        uint8_t cmd[8] = {0x07, 0, 0, 0, 0, 0, 0, 0};
        // No response expected for restart
        return sendFrame(node_id, cmd);
    }

    // ─── Status ─────────────────────────────────────────────────

    Mf7025v2Status1 readStatus1(uint8_t node_id) override {
        Mf7025v2Status1 status{};
        status.timestamp = std::chrono::steady_clock::now();
        status.motor_state = 0x10; // default OFF
        status.error_state = 0xFF; // error

        uint8_t cmd[8] = {0x9A, 0, 0, 0, 0, 0, 0, 0};
        uint8_t resp[8] = {0};

        if (executeCommand(node_id, cmd, resp, 100)) {
            // Response: temperature(int8), voltage(int16 LE), current(int16 LE),
            //           motorState(uint8), errorState(uint8), padding(2)
            status.temperature = static_cast<int8_t>(resp[1]);
            status.voltage     = static_cast<int16_t>(resp[2] | (resp[3] << 8));
            status.current     = static_cast<int16_t>(resp[4] | (resp[5] << 8));
            status.motor_state = resp[6];
            status.error_state = resp[7];
        }

        return status;
    }

    Mf7025v2Status2 readStatus2(uint8_t node_id) override {
        Mf7025v2Status2 status{};
        status.timestamp = std::chrono::steady_clock::now();

        uint8_t cmd[8] = {0x9C, 0, 0, 0, 0, 0, 0, 0};
        uint8_t resp[8] = {0};

        if (executeCommand(node_id, cmd, resp, 100)) {
            // Response: temperature(int8), iq(int16 LE), speed(int16 LE), encoder(uint16 LE)
            status.temperature = static_cast<int8_t>(resp[1]);
            status.iq          = static_cast<int16_t>(resp[2] | (resp[3] << 8));
            status.speed       = static_cast<int16_t>(resp[4] | (resp[5] << 8));
            status.encoder     = static_cast<uint16_t>(resp[6] | (resp[7] << 8));
        }

        return status;
    }

    // ─── Motion control ─────────────────────────────────────────

    bool setTorque(uint8_t node_id, int16_t iq_control) override {
        uint8_t cmd[8];
        cmd[0] = 0xA1;
        cmd[1] = 0;
        cmd[2] = static_cast<uint8_t>(iq_control & 0xFF);
        cmd[3] = static_cast<uint8_t>((iq_control >> 8) & 0xFF);
        cmd[4] = cmd[5] = cmd[6] = cmd[7] = 0;
        uint8_t resp[8] = {0};
        return executeCommand(node_id, cmd, resp, 100);
    }

    bool setVelocity(uint8_t node_id, int32_t speed_control, int16_t torque_limit) override {
        uint8_t cmd[8];
        cmd[0] = 0xA2;
        cmd[1] = static_cast<uint8_t>(torque_limit & 0xFF);
        cmd[2] = static_cast<uint8_t>((torque_limit >> 8) & 0xFF);
        cmd[3] = static_cast<uint8_t>(speed_control & 0xFF);
        cmd[4] = static_cast<uint8_t>((speed_control >> 8) & 0xFF);
        cmd[5] = static_cast<uint8_t>((speed_control >> 16) & 0xFF);
        cmd[6] = static_cast<uint8_t>((speed_control >> 24) & 0xFF);
        cmd[7] = 0;
        uint8_t resp[8] = {0};
        return executeCommand(node_id, cmd, resp, 100);
    }

    bool setMultiTurnPosition(uint8_t node_id, int32_t angle_control) override {
        uint8_t cmd[8];
        cmd[0] = 0xA3;
        cmd[1] = cmd[2] = cmd[3] = 0;
        cmd[4] = static_cast<uint8_t>(angle_control & 0xFF);
        cmd[5] = static_cast<uint8_t>((angle_control >> 8) & 0xFF);
        cmd[6] = static_cast<uint8_t>((angle_control >> 16) & 0xFF);
        cmd[7] = static_cast<uint8_t>((angle_control >> 24) & 0xFF);
        uint8_t resp[8] = {0};
        return executeCommand(node_id, cmd, resp, 100);
    }

    bool setMultiTurnPositionWithSpeed(uint8_t node_id, int32_t angle_control,
                                       uint16_t max_speed) override {
        uint8_t cmd[8];
        cmd[0] = 0xA4;
        cmd[1] = static_cast<uint8_t>(max_speed & 0xFF);
        cmd[2] = static_cast<uint8_t>((max_speed >> 8) & 0xFF);
        cmd[3] = 0;
        cmd[4] = static_cast<uint8_t>(angle_control & 0xFF);
        cmd[5] = static_cast<uint8_t>((angle_control >> 8) & 0xFF);
        cmd[6] = static_cast<uint8_t>((angle_control >> 16) & 0xFF);
        cmd[7] = static_cast<uint8_t>((angle_control >> 24) & 0xFF);
        uint8_t resp[8] = {0};
        return executeCommand(node_id, cmd, resp, 100);
    }

    bool setIncrementalPosition(uint8_t node_id, int32_t increment) override {
        uint8_t cmd[8];
        cmd[0] = 0xA7;
        cmd[1] = cmd[2] = cmd[3] = 0;
        cmd[4] = static_cast<uint8_t>(increment & 0xFF);
        cmd[5] = static_cast<uint8_t>((increment >> 8) & 0xFF);
        cmd[6] = static_cast<uint8_t>((increment >> 16) & 0xFF);
        cmd[7] = static_cast<uint8_t>((increment >> 24) & 0xFF);
        uint8_t resp[8] = {0};
        return executeCommand(node_id, cmd, resp, 100);
    }

    bool setIncrementalPositionWithSpeed(uint8_t node_id, int32_t increment,
                                         uint16_t max_speed) override {
        uint8_t cmd[8];
        cmd[0] = 0xA8;
        cmd[1] = static_cast<uint8_t>(max_speed & 0xFF);
        cmd[2] = static_cast<uint8_t>((max_speed >> 8) & 0xFF);
        cmd[3] = 0;
        cmd[4] = static_cast<uint8_t>(increment & 0xFF);
        cmd[5] = static_cast<uint8_t>((increment >> 8) & 0xFF);
        cmd[6] = static_cast<uint8_t>((increment >> 16) & 0xFF);
        cmd[7] = static_cast<uint8_t>((increment >> 24) & 0xFF);
        uint8_t resp[8] = {0};
        return executeCommand(node_id, cmd, resp, 100);
    }

    // ─── Encoder ────────────────────────────────────────────────

    int64_t readMultiTurnAngle(uint8_t node_id) override {
        uint8_t cmd[8] = {0x92, 0, 0, 0, 0, 0, 0, 0};
        uint8_t resp[8] = {0};

        if (executeCommand(node_id, cmd, resp, 100)) {
            // Response: 8 bytes motorAngle (int64, LE, 0.01°/LSB)
            int64_t angle = 0;
            for (int i = 0; i < 8; i++) {
                angle |= static_cast<int64_t>(resp[i]) << (i * 8);
            }
            return angle;
        }
        return 0;
    }

    uint32_t readSingleTurnAngle(uint8_t node_id) override {
        uint8_t cmd[8] = {0x94, 0, 0, 0, 0, 0, 0, 0};
        uint8_t resp[8] = {0};

        if (executeCommand(node_id, cmd, resp, 100)) {
            // Response: 4 bytes circleAngle (uint32, LE, 0.01°/LSB), followed by 4 padding
            uint32_t angle = 0;
            for (int i = 0; i < 4; i++) {
                angle |= static_cast<uint32_t>(resp[i]) << (i * 8);
            }
            return angle;
        }
        return 0;
    }

    Mf7025v2EncoderData readEncoderData(uint8_t node_id) override {
        Mf7025v2EncoderData data{};
        uint8_t cmd[8] = {0x90, 0, 0, 0, 0, 0, 0, 0};
        uint8_t resp[8] = {0};

        if (executeCommand(node_id, cmd, resp, 100)) {
            data.encoder       = static_cast<uint16_t>(resp[1] | (resp[2] << 8));
            data.encoder_raw   = static_cast<uint16_t>(resp[3] | (resp[4] << 8));
            data.encoder_offset = static_cast<uint16_t>(resp[5] | (resp[6] << 8));
        }

        return data;
    }

    bool calibrateEncoder(uint8_t node_id) override {
        uint8_t cmd[8] = {0x18, 0, 0, 0, 0, 0, 0, 0};
        uint8_t resp[8] = {0};
        return executeCommand(node_id, cmd, resp, 1000); // longer timeout for calibration
    }

    bool setZeroROM(uint8_t node_id, int32_t encoder_offset) override {
        uint8_t cmd[8];
        cmd[0] = 0x19;
        cmd[1] = static_cast<uint8_t>(encoder_offset & 0xFF);
        cmd[2] = static_cast<uint8_t>((encoder_offset >> 8) & 0xFF);
        cmd[3] = static_cast<uint8_t>((encoder_offset >> 16) & 0xFF);
        cmd[4] = static_cast<uint8_t>((encoder_offset >> 24) & 0xFF);
        cmd[5] = cmd[6] = cmd[7] = 0;
        uint8_t resp[8] = {0};
        return executeCommand(node_id, cmd, resp, 100);
    }

    bool setZeroRAM(uint8_t node_id) override {
        uint8_t cmd[8] = {0x95, 0, 0, 0, 0, 0, 0, 0};
        uint8_t resp[8] = {0};
        return executeCommand(node_id, cmd, resp, 100);
    }

    // ─── Brake ──────────────────────────────────────────────────

    bool brakeRelease(uint8_t node_id) override {
        uint8_t cmd[8] = {0x8C, 0x01, 0, 0, 0, 0, 0, 0};
        uint8_t resp[8] = {0};
        return executeCommand(node_id, cmd, resp, 100);
    }

    bool brakeEngage(uint8_t node_id) override {
        uint8_t cmd[8] = {0x8C, 0x00, 0, 0, 0, 0, 0, 0};
        uint8_t resp[8] = {0};
        return executeCommand(node_id, cmd, resp, 100);
    }

    bool readBrakeStatus(uint8_t node_id) override {
        uint8_t cmd[8] = {0x8C, 0x10, 0, 0, 0, 0, 0, 0};
        uint8_t resp[8] = {0};
        if (executeCommand(node_id, cmd, resp, 100)) {
            return resp[1] != 0; // data[1] = brake state (0=engaged, 1=released)
        }
        return false;
    }

    // ─── Parameters ─────────────────────────────────────────────

    bool readControlParam(uint8_t node_id, uint8_t param_id, uint8_t data_out[6]) override {
        uint8_t cmd[8];
        cmd[0] = 0xC0;
        cmd[1] = param_id;
        cmd[2] = cmd[3] = cmd[4] = cmd[5] = cmd[6] = cmd[7] = 0;
        uint8_t resp[8] = {0};

        if (executeCommand(node_id, cmd, resp, 100)) {
            // Response: param_id echo + 6 bytes data
            std::memcpy(data_out, resp + 2, 6);
            return true;
        }
        return false;
    }

    bool writeControlParam(uint8_t node_id, uint8_t param_id, const uint8_t data[6]) override {
        uint8_t cmd[8];
        cmd[0] = 0xC1;
        cmd[1] = param_id;
        std::memcpy(cmd + 2, data, 6);
        uint8_t resp[8] = {0};
        return executeCommand(node_id, cmd, resp, 100);
    }

    bool saveSettings(uint8_t node_id) override {
        uint8_t cmd[8] = {0x44, 0, 0, 0, 0, 0, 0, 0};
        uint8_t resp[8] = {0};
        return executeCommand(node_id, cmd, resp, 500); // longer timeout for flash write
    }

private:
    int socket_fd_{-1};
    std::string interface_name_;
    std::mutex mutex_;
    std::atomic<bool> initialized_{false};

    // Logging helpers
    #define MF_LOG_ERROR(...)  logging::Logger::get(LOG_TAG)->error(__VA_ARGS__)
    #define MF_LOG_WARN(...)   logging::Logger::get(LOG_TAG)->warn(__VA_ARGS__)
    #define MF_LOG_INFO(...)   logging::Logger::get(LOG_TAG)->info(__VA_ARGS__)
};

// ─── Factory function ──────────────────────────────────────────────

std::unique_ptr<IMf7025v2Interface> createMf7025v2CanInterface() {
    return std::make_unique<Mf7025v2CanInterface>();
}

} // namespace controllers
} // namespace astro_mount
