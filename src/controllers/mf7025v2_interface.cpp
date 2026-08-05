// MF7025v2 CAN interface — Linux SocketCAN implementation
// Protocol: LingKong V2.36 proprietary CAN (not CANopen)
// CAN ID = 0x140 + node_id (1..32), DLC=8, Standard frame (11-bit)

#ifndef __linux__
#error "MF7025v2 CAN interface requires Linux (SocketCAN)."
#endif

#include "controllers/imf7025v2_interface.h"

#include <linux/can.h>
#include <linux/can/raw.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>
#include <cstring>
#include <chrono>
#include <mutex>
#include <stdexcept>
#include <iostream>
#include <sstream>
#include <iomanip>

namespace astro_mount {
namespace controllers {

class Mf7025v2CanInterface : public IMf7025v2Interface {
public:
    Mf7025v2CanInterface() = default;
    ~Mf7025v2CanInterface() override { close(); }

    bool open(const std::string& can_iface, uint32_t bitrate) override {
        std::lock_guard<std::mutex> lock(mutex_);
        if (sock_fd_ >= 0) return true; // already open

        sock_fd_ = ::socket(PF_CAN, SOCK_RAW, CAN_RAW);
        if (sock_fd_ < 0) {
            std::cerr << "[MF7025v2] socket() failed: " << strerror(errno) << std::endl;
            return false;
        }

        struct ifreq ifr;
        std::strncpy(ifr.ifr_name, can_iface.c_str(), IFNAMSIZ - 1);
        if (::ioctl(sock_fd_, SIOCGIFINDEX, &ifr) < 0) {
            std::cerr << "[MF7025v2] ioctl SIOCGIFINDEX failed for " << can_iface
                      << ": " << strerror(errno) << std::endl;
            ::close(sock_fd_);
            sock_fd_ = -1;
            return false;
        }

        struct sockaddr_can addr;
        std::memset(&addr, 0, sizeof(addr));
        addr.can_family = AF_CAN;
        addr.can_ifindex = ifr.ifr_ifindex;

        if (::bind(sock_fd_, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
            std::cerr << "[MF7025v2] bind() failed: " << strerror(errno) << std::endl;
            ::close(sock_fd_);
            sock_fd_ = -1;
            return false;
        }

        // Set reception timeout
        struct timeval tv;
        tv.tv_sec = 0;
        tv.tv_usec = static_cast<__suseconds_t>(timeout_us_);
        ::setsockopt(sock_fd_, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

        iface_name_ = can_iface;
        std::cout << "[MF7025v2] SocketCAN opened on " << can_iface << std::endl;
        return true;
    }

    void close() override {
        std::lock_guard<std::mutex> lock(mutex_);
        if (sock_fd_ >= 0) {
            ::close(sock_fd_);
            sock_fd_ = -1;
        }
    }

    bool isOpen() const override { return sock_fd_ >= 0; }

    // ── Motor power ────────────────────────────────────────────────────────

    bool motorRun(uint8_t node_id) override {
        uint8_t cmd[] = { 0x88, 0, 0, 0, 0, 0, 0, 0 };
        std::vector<uint8_t> resp;
        return executeCommand(node_id, cmd, sizeof(cmd), resp);
    }

    bool motorOff(uint8_t node_id) override {
        uint8_t cmd[] = { 0x80, 0, 0, 0, 0, 0, 0, 0 };
        std::vector<uint8_t> resp;
        return executeCommand(node_id, cmd, sizeof(cmd), resp);
    }

    bool motorStop(uint8_t node_id) override {
        uint8_t cmd[] = { 0x81, 0, 0, 0, 0, 0, 0, 0 };
        std::vector<uint8_t> resp;
        return executeCommand(node_id, cmd, sizeof(cmd), resp);
    }

    // ── Status ─────────────────────────────────────────────────────────────

    bool readStatus1(uint8_t node_id, Mf7025v2Status& st) override {
        uint8_t cmd[] = { 0x9A, 0, 0, 0, 0, 0, 0, 0 };
        std::vector<uint8_t> resp;
        if (!executeCommand(node_id, cmd, sizeof(cmd), resp) || resp.size() < 8)
            return false;

        st.temperature = static_cast<int8_t>(resp[1]);
        st.voltage    = static_cast<int16_t>(resp[2] | (resp[3] << 8));
        st.current    = static_cast<int16_t>(resp[4] | (resp[5] << 8));
        st.motor_state = resp[6];
        st.error_state = resp[7];
        return true;
    }

    bool readStatus2(uint8_t node_id, Mf7025v2Status& st) override {
        uint8_t cmd[] = { 0x9C, 0, 0, 0, 0, 0, 0, 0 };
        std::vector<uint8_t> resp;
        if (!executeCommand(node_id, cmd, sizeof(cmd), resp) || resp.size() < 8)
            return false;

        st.temperature = static_cast<int8_t>(resp[1]);
        st.iq       = static_cast<int16_t>(resp[2] | (resp[3] << 8));
        st.speed    = static_cast<int16_t>(resp[4] | (resp[5] << 8));
        st.encoder  = static_cast<uint16_t>(resp[6] | (resp[7] << 8));
        return true;
    }

    bool clearErrors(uint8_t node_id) override {
        uint8_t cmd[] = { 0x9B, 0, 0, 0, 0, 0, 0, 0 };
        std::vector<uint8_t> resp;
        return executeCommand(node_id, cmd, sizeof(cmd), resp);
    }

    // ── Motion control ─────────────────────────────────────────────────────

    bool speedControl(uint8_t node_id, int16_t iq, int32_t speed_001dps) override {
        // Frame per V2.36: DATA[0]=0xA2, DATA[1]=0, DATA[2..3]=iqControl LE,
        // DATA[4..7]=speedControl int32 LE (0.01dps/LSB)
        uint8_t cmd[8] = {
            0xA2, 0,
            static_cast<uint8_t>(iq & 0xFF),
            static_cast<uint8_t>((iq >> 8) & 0xFF),
            static_cast<uint8_t>(speed_001dps & 0xFF),
            static_cast<uint8_t>((speed_001dps >> 8) & 0xFF),
            static_cast<uint8_t>((speed_001dps >> 16) & 0xFF),
            static_cast<uint8_t>((speed_001dps >> 24) & 0xFF)
        };
        std::vector<uint8_t> resp;
        return executeCommand(node_id, cmd, sizeof(cmd), resp);
    }

    bool positionControl2(uint8_t node_id, uint16_t max_speed_dps,
                          int32_t angle_001deg) override {
        uint8_t cmd[8];
        cmd[0] = 0xA4;
        cmd[1] = 0;  // NULL
        // maxSpeed: uint16 LE, 1 dps/LSB
        cmd[2] = static_cast<uint8_t>(max_speed_dps & 0xFF);
        cmd[3] = static_cast<uint8_t>((max_speed_dps >> 8) & 0xFF);
        // angleControl: int32 LE, 0.01°/LSB
        cmd[4] = static_cast<uint8_t>(angle_001deg & 0xFF);
        cmd[5] = static_cast<uint8_t>((angle_001deg >> 8) & 0xFF);
        cmd[6] = static_cast<uint8_t>((angle_001deg >> 16) & 0xFF);
        cmd[7] = static_cast<uint8_t>((angle_001deg >> 24) & 0xFF);
        std::vector<uint8_t> resp;
        return executeCommand(node_id, cmd, sizeof(cmd), resp);
    }

    bool positionControl1(uint8_t node_id, int32_t angle_001deg) override {
        uint8_t cmd[8];
        cmd[0] = 0xA3;
        cmd[1] = 0;
        cmd[2] = 0;
        cmd[3] = 0;
        cmd[4] = static_cast<uint8_t>(angle_001deg & 0xFF);
        cmd[5] = static_cast<uint8_t>((angle_001deg >> 8) & 0xFF);
        cmd[6] = static_cast<uint8_t>((angle_001deg >> 16) & 0xFF);
        cmd[7] = static_cast<uint8_t>((angle_001deg >> 24) & 0xFF);
        std::vector<uint8_t> resp;
        return executeCommand(node_id, cmd, sizeof(cmd), resp);
    }

    bool torqueControl(uint8_t node_id, int16_t iq) override {
        uint8_t cmd[8];
        cmd[0] = 0xA1;
        cmd[1] = 0;
        cmd[2] = 0;
        cmd[3] = 0;
        cmd[4] = static_cast<uint8_t>(iq & 0xFF);
        cmd[5] = static_cast<uint8_t>((iq >> 8) & 0xFF);
        cmd[6] = 0;
        cmd[7] = 0;
        std::vector<uint8_t> resp;
        return executeCommand(node_id, cmd, sizeof(cmd), resp);
    }

    // ── Encoder ─────────────────────────────────────────────────────────────

    bool readEncoder(uint8_t node_id,
                     uint16_t& encoder, uint16_t& raw, uint16_t& offset) override {
        uint8_t cmd[] = { 0x90, 0, 0, 0, 0, 0, 0, 0 };
        std::vector<uint8_t> resp;
        if (!executeCommand(node_id, cmd, sizeof(cmd), resp) || resp.size() < 8)
            return false;
        // Per V2.36: DATA[0]=0x90, DATA[1]=NULL, DATA[2..3]=encoder, DATA[4..5]=raw, DATA[6..7]=offset
        encoder = static_cast<uint16_t>(resp[2] | (resp[3] << 8));
        raw     = static_cast<uint16_t>(resp[4] | (resp[5] << 8));
        offset  = static_cast<uint16_t>(resp[6] | (resp[7] << 8));
        return true;
    }

    bool readMultiTurnAngle(uint8_t node_id, int64_t& angle_001deg) override {
        uint8_t cmd[] = { 0x92, 0, 0, 0, 0, 0, 0, 0 };
        std::vector<uint8_t> resp;
        if (!executeCommand(node_id, cmd, sizeof(cmd), resp) || resp.size() < 8)
            return false;
        // Protocol transmits 56 bits (DATA[1..7]) of the int64 motorAngle.
        // Sign-extend from bit 55 to bit 63 so negative angles decode
        // correctly (e.g. 0xFFFFFFFFFFE4 → -28, not 72057594037927908).
        uint64_t raw = static_cast<uint64_t>(resp[1]) |
                       (static_cast<uint64_t>(resp[2]) << 8) |
                       (static_cast<uint64_t>(resp[3]) << 16) |
                       (static_cast<uint64_t>(resp[4]) << 24) |
                       (static_cast<uint64_t>(resp[5]) << 32) |
                       (static_cast<uint64_t>(resp[6]) << 40) |
                       (static_cast<uint64_t>(resp[7]) << 48);
        if (raw & (1ULL << 55)) {
            raw |= 0xFF00000000000000ULL;  // sign-extend 56→64 bits
        }
        angle_001deg = static_cast<int64_t>(raw);
        return true;
    }

    bool readSingleTurnAngle(uint8_t node_id, uint32_t& angle_001deg) override {
        uint8_t cmd[] = { 0x94, 0, 0, 0, 0, 0, 0, 0 };
        std::vector<uint8_t> resp;
        if (!executeCommand(node_id, cmd, sizeof(cmd), resp) || resp.size() < 8)
            return false;
        // Per V2.36: DATA[0]=0x94, DATA[1..3]=NULL, DATA[4..7]=circleAngle uint32 LE (0.01°/LSB)
        angle_001deg = static_cast<uint32_t>(
            resp[4] | (resp[5] << 8) | (resp[6] << 16) | (resp[7] << 24));
        return true;
    }

    // ── Zero position ─────────────────────────────────────────────────────

    bool setZeroRAM(uint8_t node_id) override {
        // 0x95: Set current position as zero point (RAM, volatile).
        // Motor stops after receiving this command.
        uint8_t cmd[] = { 0x95, 0, 0, 0, 0, 0, 0, 0 };
        std::vector<uint8_t> resp;
        return executeCommand(node_id, cmd, sizeof(cmd), resp);
    }

    // ── Configuration ──────────────────────────────────────────────────────

    bool readParam(uint8_t node_id, uint8_t param_id,
                   std::vector<uint8_t>& data) override {
        uint8_t cmd[8] = { 0xC0, param_id, 0, 0, 0, 0, 0, 0 };
        if (!executeCommand(node_id, cmd, sizeof(cmd), data) || data.size() < 8)
            return false;
        data.resize(6); // bytes 2..7 are parameter data
        return true;
    }

    bool writeParam(uint8_t node_id, uint8_t param_id,
                    const std::vector<uint8_t>& data) override {
        uint8_t cmd[8] = { 0xC1, param_id, 0, 0, 0, 0, 0, 0 };
        for (size_t i = 0; i < data.size() && (i + 2) < 8; ++i)
            cmd[i + 2] = data[i];
        std::vector<uint8_t> resp;
        return executeCommand(node_id, cmd, sizeof(cmd), resp);
    }

public:
    void setCanTrace(bool enabled) override { can_trace_enabled_ = enabled; }
    void setCanTraceReadState(bool enabled) override { can_trace_read_state_enabled_ = enabled; }

private:
    int sock_fd_{-1};
    std::string iface_name_;
    std::mutex mutex_;
    std::mutex can_mutex_;  // Protects send/receive pairs (request-response atomicity)
    uint32_t timeout_us_{100000}; // 100ms CAN response timeout
    bool can_trace_enabled_{true};
    bool can_trace_read_state_enabled_{false};

    // Returns true if cmd_byte is a ReadState command that may be suppressed
    static bool isReadStateCommand(uint8_t cmd_byte) {
        switch (cmd_byte) {
            case 0x9A: // ReadStatus1
            case 0x9C: // ReadStatus2
            case 0x9D: // ReadStatus3
            case 0x90: // ReadEncoder
            case 0x92: // ReadMultiTurnAngle
            case 0x94: // ReadSingleTurnAngle
                return true;
            default:
                return false;
        }
    }

    // ── CAN command introspection for tracing ─────────────────────────────
    struct CmdDesc {
        const char* name;
        const char* params;
    };

    static CmdDesc describeCommand(uint8_t cmd_byte, const uint8_t* data) {
        switch (cmd_byte) {
            case 0x80: return {"MotorOff",           ""};
            case 0x81: return {"MotorStop",          ""};
            case 0x88: return {"MotorRun",           ""};
            case 0x8C: {
                if (data[1] == 0x00)      return {"BrakeControl",    "cmd=PowerOff(0x00)"};
                if (data[1] == 0x01)      return {"BrakeControl",    "cmd=PowerOn(0x01)"};
                if (data[1] == 0x10)      return {"BrakeControl",    "cmd=ReadStatus(0x10)"};
                return {"BrakeControl",    "cmd=?"};
            }
            case 0x9A: return {"ReadStatus1",        "temperature,voltage,current,motorState,errorState"};
            case 0x9B: return {"ClearErrors",         ""};
            case 0x9C: return {"ReadStatus2",         "temperature,iq/power,speed,encoder"};
            case 0x9D: return {"ReadStatus3",         "temperature,iA,iB,iC"};
            case 0x90: return {"ReadEncoder",         "encoder,raw,offset"};
            case 0x92: return {"ReadMultiTurnAngle",  "motorAngle(int64,0.01°/LSB)"};
            case 0x94: return {"ReadSingleTurnAngle", "circleAngle(uint32,0.01°/LSB)"};
            case 0x18: return {"CalibrateEncoder",    "→ROM, motor rotates ~few sec"};
            case 0x19: return {"SetZeroROM",          "encoderOffset→ROM"};
            case 0x95: return {"SetZeroRAM",          "→motor stop state"};
            case 0x07: return {"Reboot",              "no response"};
            case 0xA0: {
                int16_t pc = static_cast<int16_t>(data[4] | (data[5] << 8));
                std::ostringstream oss;
                oss << "powerControl=" << pc << "(0x" << std::hex << (pc & 0xFFFF) << std::dec << ")";
                static std::string buf; buf = oss.str();
                return {"OpenLoop", buf.c_str()};
            }
            case 0xA1: {
                int16_t iq = static_cast<int16_t>(data[4] | (data[5] << 8));
                std::ostringstream oss;
                oss << "iqControl=" << iq << "(0x" << std::hex << (iq & 0xFFFF) << std::dec << ")";
                static std::string buf; buf = oss.str();
                return {"TorqueControl", buf.c_str()};
            }
            case 0xA2: {
                int16_t iq = static_cast<int16_t>(data[2] | (data[3] << 8));
                int32_t sp = static_cast<int32_t>(data[4] | (data[5] << 8) | (data[6] << 16) | (data[7] << 24));
                std::ostringstream oss;
                oss << "iqLimit=" << iq << " speed=" << (sp*0.01) << "dps(0x" << std::hex << (sp & 0xFFFFFFFF) << std::dec << ")";
                static std::string buf; buf = oss.str();
                return {"SpeedControl", buf.c_str()};
            }
            case 0xA3: {
                int32_t ang = static_cast<int32_t>(data[4] | (data[5] << 8) | (data[6] << 16) | (data[7] << 24));
                std::ostringstream oss;
                oss << "angle=" << (ang*0.01) << "°(0x" << std::hex << (ang & 0xFFFFFFFF) << std::dec << ")";
                static std::string buf; buf = oss.str();
                return {"Position1", buf.c_str()};
            }
            case 0xA4: {
                uint16_t ms = static_cast<uint16_t>(data[2] | (data[3] << 8));
                int32_t ang = static_cast<int32_t>(data[4] | (data[5] << 8) | (data[6] << 16) | (data[7] << 24));
                std::ostringstream oss;
                oss << "maxSpeed=" << ms << "dps angle=" << (ang*0.01) << "°(0x" << std::hex << (ang & 0xFFFFFFFF) << std::dec << ")";
                static std::string buf; buf = oss.str();
                return {"Position2", buf.c_str()};
            }
            case 0xA5: {
                uint8_t dir = data[1];
                uint32_t ang = static_cast<uint32_t>(data[4] | (data[5] << 8) | (data[6] << 16) | (data[7] << 24));
                std::ostringstream oss;
                oss << "dir=" << (dir==0?"CW":"CCW") << " angle=" << (ang*0.01) << "°(0x" << std::hex << ang << std::dec << ")";
                static std::string buf; buf = oss.str();
                return {"SingleTurnPos1", buf.c_str()};
            }
            case 0xA6: {
                uint8_t dir = data[1];
                uint16_t ms = static_cast<uint16_t>(data[2] | (data[3] << 8));
                uint32_t ang = static_cast<uint32_t>(data[4] | (data[5] << 8) | (data[6] << 16) | (data[7] << 24));
                std::ostringstream oss;
                oss << "dir=" << (dir==0?"CW":"CCW") << " maxSpeed=" << ms << "dps angle=" << (ang*0.01) << "°(0x" << std::hex << ang << std::dec << ")";
                static std::string buf; buf = oss.str();
                return {"SingleTurnPos2", buf.c_str()};
            }
            case 0xA7: {
                int32_t inc = static_cast<int32_t>(data[4] | (data[5] << 8) | (data[6] << 16) | (data[7] << 24));
                std::ostringstream oss;
                oss << "increment=" << (inc*0.01) << "°(0x" << std::hex << (inc & 0xFFFFFFFF) << std::dec << ")";
                static std::string buf; buf = oss.str();
                return {"IncrementalPos1", buf.c_str()};
            }
            case 0xA8: {
                uint16_t ms = static_cast<uint16_t>(data[2] | (data[3] << 8));
                int32_t inc = static_cast<int32_t>(data[4] | (data[5] << 8) | (data[6] << 16) | (data[7] << 24));
                std::ostringstream oss;
                oss << "maxSpeed=" << ms << "dps increment=" << (inc*0.01) << "°(0x" << std::hex << (inc & 0xFFFFFFFF) << std::dec << ")";
                static std::string buf; buf = oss.str();
                return {"IncrementalPos2", buf.c_str()};
            }
            case 0xC0: return {"ReadControlParam",   "controlParamID"};
            case 0xC1: return {"WriteControlParam",  "→RAM"};
            case 0x40: return {"ReadSettingParam",   ""};
            case 0x42: return {"WriteSettingParam",  ""};
            case 0x44: return {"SaveSettings",       "→ROM"};
            default:   return {"Unknown",            ""};
        }
    }

    void traceCanFrame(uint32_t can_id, const uint8_t* data, size_t len) {
        if (!can_trace_enabled_) return;
        uint8_t node_id = static_cast<uint8_t>(can_id - 0x140);
        uint8_t cmd_byte = (len > 0) ? data[0] : 0;

        // Suppress ReadState commands unless explicitly enabled
        if (!can_trace_read_state_enabled_ && isReadStateCommand(cmd_byte)) return;

        auto desc = describeCommand(cmd_byte, data);

        // Build hex dump of the frame data
        char hex[32];
        int pos = 0;
        for (size_t i = 0; i < len && i < 8; ++i) {
            pos += snprintf(hex + pos, sizeof(hex) - pos, "%02X", data[i]);
        }

        std::cerr << "[MF7025v2] CAN→node" << static_cast<int>(node_id)
                  << " 0x" << std::hex << std::uppercase << static_cast<int>(cmd_byte) << std::nouppercase << std::dec
                  << " " << desc.name;
        if (desc.params[0] != '\0') {
            std::cerr << " | " << desc.params;
        }
        std::cerr << " | FRAME: " << std::hex << can_id << "#" << hex << std::dec << std::endl;
    }

    bool sendFrame(uint32_t can_id, const uint8_t* data, size_t len) {
        traceCanFrame(can_id, data, len);

        struct can_frame frame;
        std::memset(&frame, 0, sizeof(frame));
        frame.can_id = can_id;
        frame.can_dlc = static_cast<uint8_t>(len > 8 ? 8 : len);
        std::memcpy(frame.data, data, frame.can_dlc);

        ssize_t n = ::write(sock_fd_, &frame, sizeof(frame));
        if (n != sizeof(frame)) {
            std::cerr << "[MF7025v2] sendFrame failed for CAN ID 0x"
                      << std::hex << can_id << std::dec
                      << ": " << strerror(errno) << std::endl;
            return false;
        }
        return true;
    }

    bool receiveFrame(uint32_t expected_can_id, uint8_t expected_cmd_byte,
                      std::vector<uint8_t>& data, int timeout_ms = 100) {
        struct can_frame frame;
        auto start = std::chrono::steady_clock::now();
        while (true) {
            ssize_t n = ::read(sock_fd_, &frame, sizeof(frame));
            if (n == sizeof(frame)) {
                if (frame.can_id == expected_can_id) {
                    // Validate echo: command byte must match the request.
                    // Without this check, cross-talk between concurrent
                    // speedControl (0xA2) and readStatus2 (0x9C) on the
                    // same CAN ID would silently corrupt data.
                    if (frame.can_dlc > 0 && frame.data[0] != expected_cmd_byte) {
                        // Wrong command echo — another thread's response.
                        // Keep waiting for our actual response.
                        auto elapsed = std::chrono::steady_clock::now() - start;
                        if (std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count() > timeout_ms)
                            return false;
                        continue;
                    }
                    data.assign(frame.data, frame.data + frame.can_dlc);
                    return true;
                }
                // else: not our frame, keep waiting
            } else if (n < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    auto elapsed = std::chrono::steady_clock::now() - start;
                    if (std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count() > timeout_ms)
                        return false;
                    continue;
                }
                return false;
            }
            auto elapsed = std::chrono::steady_clock::now() - start;
            if (std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count() > timeout_ms)
                return false;
        }
    }

    bool executeCommand(uint8_t node_id, const uint8_t* cmd, size_t cmd_len,
                        std::vector<uint8_t>& response) {
        if (sock_fd_ < 0) return false;

        // Protect the send/receive pair against concurrent access from
        // the monitor thread (readStatus2) and gRPC handler threads
        // (speedControl, positionControl, etc.).
        std::lock_guard<std::mutex> lock(can_mutex_);

        // CAN ID = 0x140 + node_id
        uint32_t can_id = 0x140 + node_id;

        if (!sendFrame(can_id, cmd, cmd_len))
            return false;

        // Pass the command byte so receiveFrame can validate the echo
        uint8_t expected_cmd = (cmd_len > 0) ? cmd[0] : 0;
        return receiveFrame(can_id, expected_cmd, response, 100);
    }
};

// Factory function
std::unique_ptr<IMf7025v2Interface> createMf7025v2CanInterface() {
#ifdef __linux__
    return std::make_unique<Mf7025v2CanInterface>();
#else
    return nullptr;
#endif
}

} // namespace controllers
} // namespace astro_mount
