// mf7025v2_c1_test.cpp
//
// Standalone CLI to test the LingKong MF7025v2 proprietary CAN command 0xC1
// (WriteControlParam → RAM) over Linux SocketCAN.
//
// The 0xC1 command writes a control parameter (e.g. 0x0A = position PID,
// 0x0B = speed PID, 0x0C = current PID) to the drive RAM.  This tool sends
// the command and waits for the drive's echo frame; if no matching echo
// arrives within the timeout, it reports a failure — the same condition that
// produces the "[mf7025v2] ... PID write (0x..) failed" warnings in the
// controller log.
//
// Protocol reference (LingKong V2.36):
//   CAN ID  = 0x140 + node_id  (node_id 1..32, standard 11-bit frame)
//   DLC     = 8
//   cmd[0]  = command byte (0xC1 for write, 0xC0 for read)
//   cmd[1]  = control parameter id
//   cmd[2..7] = parameter data (LE)
//
// Build:
//   g++ -std=c++17 -O2 -Wall tools/mf7025v2_c1_test.cpp -o build/bin/mf7025v2_c1_test
//
// Examples:
//   sudo ./build/bin/mf7025v2_c1_test -i can0 -n 1 -p 0x0B --status
//   sudo ./build/bin/mf7025v2_c1_test -i can0 -n 2 -p 0x0A -d 0A0000000000 -r 5

#include <linux/can.h>
#include <linux/can/raw.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

#include <chrono>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

namespace {

struct Options {
    std::string iface = "can0";
    int node = 1;                 // node id 1..32
    int param = 0x0B;             // control parameter id (default: speed PID)
    std::vector<uint8_t> data;    // parameter data bytes (max 6)
    int timeout_ms = 100;
    int repeat = 1;
    bool do_status = false;       // sanity-check 0x9A first
    bool do_read = false;         // read back with 0xC0 after write
};

void printUsage(const char* argv0) {
    std::cerr << "Usage: " << argv0 << " [options]\n"
              << "  -i IFACE   SocketCAN interface        (default: can0)\n"
              << "  -n NODE    CAN node id 1..32          (default: 1)\n"
              << "  -p PARAM   control param id (hex)      (default: 0x0B speed PID)\n"
              << "  -d DATA    data bytes, hex, no spaces  (default: 14002D000000 = kp20,ki45)\n"
              << "  -t MS      response timeout ms         (default: 100)\n"
              << "  -r COUNT   repeat COUNT times          (default: 1)\n"
              << "  --status   send 0x9A ReadStatus1 sanity check first\n"
              << "  --read     read back the param with 0xC0 after each write\n"
              << "  -h         show this help\n";
}

bool parseArgs(int argc, char** argv, Options& opt) {
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto next = [&](const char* name) -> std::string {
            if (i + 1 >= argc) {
                std::cerr << "Missing value for " << name << "\n";
                return {};
            }
            return argv[++i];
        };
        if (a == "-i") opt.iface = next("-i");
        else if (a == "-n") opt.node = std::stoi(next("-n"));
        else if (a == "-p") opt.param = std::stoi(next("-p"), nullptr, 0);
        else if (a == "-d") {
            std::string hex = next("-d");
            if (hex.size() % 2 != 0) {
                std::cerr << "-d must contain an even number of hex digits\n";
                return false;
            }
            opt.data.clear();
            for (size_t j = 0; j + 1 < hex.size(); j += 2) {
                opt.data.push_back(static_cast<uint8_t>(std::stoi(hex.substr(j, 2), nullptr, 16)));
            }
        }
        else if (a == "-t") opt.timeout_ms = std::stoi(next("-t"));
        else if (a == "-r") opt.repeat = std::stoi(next("-r"));
        else if (a == "--status") opt.do_status = true;
        else if (a == "--read") opt.do_read = true;
        else if (a == "-h" || a == "--help") { printUsage(argv[0]); return false; }
        else {
            std::cerr << "Unknown option: " << a << "\n";
            printUsage(argv[0]);
            return false;
        }
    }
    if (opt.node < 1 || opt.node > 32) {
        std::cerr << "Node id must be 1..32\n";
        return false;
    }
    if (opt.data.size() > 6) {
        std::cerr << "Data must be at most 6 bytes\n";
        return false;
    }
    if (opt.data.empty()) {
        // Default speed-PID payload: kp=20, ki=45, kd=0 (LE uint16 triple).
        opt.data = {0x14, 0x00, 0x2D, 0x00, 0x00, 0x00};
    }
    return true;
}

std::string hexDump(const uint8_t* d, size_t n) {
    std::ostringstream oss;
    oss << std::hex << std::uppercase << std::setfill('0');
    for (size_t i = 0; i < n; ++i) oss << std::setw(2) << static_cast<int>(d[i]);
    return oss.str();
}

int openCanSocket(const std::string& iface) {
    int fd = ::socket(PF_CAN, SOCK_RAW, CAN_RAW);
    if (fd < 0) {
        std::cerr << "socket(PF_CAN) failed: " << strerror(errno) << "\n";
        return -1;
    }
    struct ifreq ifr;
    std::strncpy(ifr.ifr_name, iface.c_str(), IFNAMSIZ - 1);
    if (::ioctl(fd, SIOCGIFINDEX, &ifr) < 0) {
        std::cerr << "ioctl SIOCGIFINDEX failed for '" << iface << "': " << strerror(errno) << "\n"
                  << "HINT: bring the interface up first, e.g.:\n"
                  << "  sudo ip link set up " << iface << "\n";
        ::close(fd);
        return -1;
    }
    struct sockaddr_can addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.can_family = AF_CAN;
    addr.can_ifindex = ifr.ifr_ifindex;
    if (::bind(fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
        std::cerr << "bind() failed: " << strerror(errno) << "\n";
        ::close(fd);
        return -1;
    }
    return fd;
}

// Send one 8-byte command and wait for the matching echo. Returns true and
// fills resp on success, false on timeout/mismatch.
bool transact(int fd, uint32_t can_id, const uint8_t cmd[8], int timeout_ms,
              std::vector<uint8_t>& resp) {
    struct can_frame frame;
    std::memset(&frame, 0, sizeof(frame));
    frame.can_id = can_id;
    frame.can_dlc = 8;
    std::memcpy(frame.data, cmd, 8);

    if (::write(fd, &frame, sizeof(frame)) != sizeof(frame)) {
        std::cerr << "  write() failed: " << strerror(errno) << "\n";
        return false;
    }

    struct timeval tv;
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;
    ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    auto start = std::chrono::steady_clock::now();
    while (true) {
        ssize_t n = ::read(fd, &frame, sizeof(frame));
        if (n == sizeof(frame)) {
            if (frame.can_id == can_id) {
                // Validate echo: first data byte must match the request cmd.
                if (frame.can_dlc > 0 && frame.data[0] != cmd[0]) {
                    // Another command's response on the same CAN ID — keep waiting.
                    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now() - start).count();
                    if (elapsed > timeout_ms) return false;
                    continue;
                }
                resp.assign(frame.data, frame.data + frame.can_dlc);
                return true;
            }
            // not our frame — keep waiting
        } else if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) return false;  // timeout
            std::cerr << "  read() error: " << strerror(errno) << "\n";
            return false;
        }
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start).count();
        if (elapsed > timeout_ms) return false;
    }
}

}  // namespace

int main(int argc, char** argv) {
    Options opt;
    if (!parseArgs(argc, argv, opt)) return 2;

    int fd = openCanSocket(opt.iface);
    if (fd < 0) return 1;

    uint32_t can_id = 0x140 + static_cast<uint32_t>(opt.node);
    std::cout << "CAN interface: " << opt.iface << " | node=" << opt.node
              << " (CAN ID 0x" << std::hex << can_id << std::dec << ")"
              << " | param=0x" << std::hex << std::uppercase << opt.param << std::nouppercase << std::dec
              << " | data=" << hexDump(opt.data.data(), opt.data.size()) << "\n\n";

    // 1) Optional sanity check: 0x9A ReadStatus1.
    if (opt.do_status) {
        uint8_t cmd[8] = {0x9A, 0, 0, 0, 0, 0, 0, 0};
        std::vector<uint8_t> resp;
        std::cout << "[sanity] 0x9A ReadStatus1 → ";
        if (transact(fd, can_id, cmd, opt.timeout_ms, resp)) {
            std::cout << "OK (" << resp.size() << " bytes: " << hexDump(resp.data(), resp.size()) << ")\n";
        } else {
            std::cout << "NO RESPONSE\n";
            std::cout << "  → the node is not answering at all. Check wiring/termination/power and that\n"
                      << "    the controller is not already using the bus.\n";
        }
        std::cout << "\n";
    }

    // 2) The 0xC1 write-under-test.
    int ok = 0, fail = 0;
    for (int r = 1; r <= opt.repeat; ++r) {
        uint8_t cmd[8] = {0xC1, static_cast<uint8_t>(opt.param), 0, 0, 0, 0, 0, 0};
        for (size_t i = 0; i < opt.data.size(); ++i) cmd[2 + i] = opt.data[i];

        std::vector<uint8_t> resp;
        std::cout << "[write  " << r << "/" << opt.repeat << "] 0xC1 param=0x"
                  << std::hex << std::uppercase << opt.param << std::nouppercase << std::dec
                  << " data=" << hexDump(opt.data.data(), opt.data.size()) << " → " << std::flush;
        if (transact(fd, can_id, cmd, opt.timeout_ms, resp)) {
            std::cout << "OK (echo: " << hexDump(resp.data(), resp.size()) << ")\n";
            ++ok;
        } else {
            std::cout << "FAILED (no matching echo within " << opt.timeout_ms << " ms)\n";
            ++fail;
        }

        if (opt.do_read) {
            uint8_t rcmd[8] = {0xC0, static_cast<uint8_t>(opt.param), 0, 0, 0, 0, 0, 0};
            std::vector<uint8_t> rresp;
            std::cout << "[read ] 0xC0 param=0x" << std::hex << std::uppercase << opt.param
                      << std::nouppercase << std::dec << " → " << std::flush;
            if (transact(fd, can_id, rcmd, opt.timeout_ms, rresp)) {
                // Response data for 0xC0 is bytes 2..7.
                std::string hex = (rresp.size() > 2) ? hexDump(rresp.data() + 2, rresp.size() - 2) : "-";
                std::cout << "OK (param data: " << hex << ")\n";
            } else {
                std::cout << "FAILED\n";
            }
        }
    }

    std::cout << "\nResult: " << ok << " OK, " << fail << " FAILED\n";
    ::close(fd);

    if (fail > 0) {
        std::cout << "\nDiagnosis:\n"
                  << "  * If 0x9A passed but 0xC1 failed → the drive answers on the bus but does NOT\n"
                  << "    acknowledge 0xC1 writes. Typical causes:\n"
                  << "      - firmware does not support runtime RAM writes of this param (0xC1)\n"
                  << "      - motor must be in a specific state (e.g. stopped / disabled) to accept 0xC1\n"
                  << "      - param id not writable on this drive revision\n"
                  << "    This matches the repeated 'PID write failed' warnings in the controller log.\n"
                  << "  * If 0x9A also failed → general CAN communication problem (bus, bitrate,\n"
                  << "    termination, node id).\n";
        return 1;
    }
    return 0;
}
