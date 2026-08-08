#include "controllers/lx200_server.h"
#include "controllers/mount_controller.h"

#include <fcntl.h>
#include <unistd.h>
#include <termios.h>
#include <sys/select.h>
#include <errno.h>
#include <cstring>
#include <cmath>
#include <sstream>
#include <iomanip>
#include <chrono>

namespace astro_mount {
namespace controllers {

// ==========================================================================
// Construction / Destruction
// ==========================================================================

LX200Server::LX200Server(MountController& controller, const Config& config)
    : controller_(controller), config_(config) {}

LX200Server::~LX200Server() {
    stop();
}

// ==========================================================================
// Start / Stop
// ==========================================================================

bool LX200Server::start() {
    if (running_.load()) return true;
    if (!config_.enabled) return false;

    std::lock_guard<std::mutex> lock(mutex_);
    if (serial_fd_ >= 0) {
        closePort();
    }

    if (!openPort()) {
        return false;
    }

    running_.store(true);
    server_thread_ = std::make_unique<std::thread>(&LX200Server::serverLoop, this);
    return true;
}

void LX200Server::stop() {
    running_.store(false);
    if (server_thread_ && server_thread_->joinable()) {
        server_thread_->join();
    }
    server_thread_.reset();
    std::lock_guard<std::mutex> lock(mutex_);
    closePort();
}

void LX200Server::reconfigure(const Config& config) {
    bool was_running = running_.load();
    if (was_running) stop();
    config_ = config;
    if (was_running) start();
}

// ==========================================================================
// Serial Port
// ==========================================================================

bool LX200Server::openPort() {
    serial_fd_ = ::open(config_.port.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (serial_fd_ < 0) {
        return false;
    }

    struct termios tty;
    std::memset(&tty, 0, sizeof(tty));

    if (tcgetattr(serial_fd_, &tty) != 0) {
        ::close(serial_fd_);
        serial_fd_ = -1;
        return false;
    }

    // Map baud rate
    speed_t baud = B9600;
    switch (config_.baud_rate) {
        case 4800:   baud = B4800;   break;
        case 9600:   baud = B9600;   break;
        case 19200:  baud = B19200;  break;
        case 38400:  baud = B38400;  break;
        case 57600:  baud = B57600;  break;
        case 115200: baud = B115200; break;
        default:     baud = B9600;   break;
    }

    cfsetospeed(&tty, baud);
    cfsetispeed(&tty, baud);

    // 8N1
    tty.c_cflag &= ~PARENB;
    tty.c_cflag &= ~CSTOPB;
    tty.c_cflag &= ~CSIZE;
    tty.c_cflag |= CS8;
    tty.c_cflag &= ~CRTSCTS;    // No hardware flow control
    tty.c_cflag |= CREAD | CLOCAL;

    tty.c_lflag &= ~(ICANON | ECHO | ECHOE | ECHONL | ISIG);
    tty.c_iflag &= ~(IXON | IXOFF | IXANY | ICRNL | INLCR);
    tty.c_oflag &= ~OPOST;

    // Read timeout: 0.1 s
    tty.c_cc[VMIN] = 0;
    tty.c_cc[VTIME] = 1;

    tcflush(serial_fd_, TCIFLUSH);
    if (tcsetattr(serial_fd_, TCSANOW, &tty) != 0) {
        ::close(serial_fd_);
        serial_fd_ = -1;
        return false;
    }

    return true;
}

void LX200Server::closePort() {
    if (serial_fd_ >= 0) {
        ::close(serial_fd_);
        serial_fd_ = -1;
    }
}

// ==========================================================================
// I/O Helpers
// ==========================================================================

std::string LX200Server::readCommand(int timeout_ms) {
    std::string cmd;
    auto start = std::chrono::steady_clock::now();

    while (running_.load()) {
        char ch;
        int n = ::read(serial_fd_, &ch, 1);
        if (n > 0) {
            if (ch == '#') {
                return cmd;  // End of command
            }
            if (ch == ':') {
                cmd.clear();  // Start of new command
                continue;
            }
            cmd += ch;
        } else if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
            return "";  // Error
        }

        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start).count();
        if (elapsed >= timeout_ms) {
            return "";  // Timeout
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return "";
}

void LX200Server::writeResponse(const std::string& response) {
    if (serial_fd_ < 0) return;
    std::string data = response + "#";
    ::write(serial_fd_, data.c_str(), data.size());
}

// ==========================================================================
// Server Loop
// ==========================================================================

void LX200Server::serverLoop() {
    while (running_.load()) {
        std::string cmd = readCommand(1000);
        if (cmd.empty()) continue;

        std::string response;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            response = processCommand(cmd);
        }
        if (!response.empty()) {
            writeResponse(response);
        }
    }
}

// ==========================================================================
// Command Processing
// ==========================================================================

std::string LX200Server::processCommand(const std::string& cmd) {
    if (cmd.empty()) return "";

    // Get commands
    if (cmd == "GD") return handleGetDec();
    if (cmd == "GR") return handleGetRA();
    if (cmd == "GA") return handleGetAlt();
    if (cmd == "GZ") return handleGetAz();
    if (cmd == "GVD") return "Aug  7 2026";  // Firmware date
    if (cmd == "GVN") return "1.0";           // Version number
    if (cmd == "GVP") return "Astro Mount Control";  // Product name
    if (cmd == "GVT") return "2000";          // Focal length (mm)

    // Set target coordinates
    if (cmd.rfind("Sr", 0) == 0) return handleSetRA(cmd.substr(2));
    if (cmd.rfind("Sd", 0) == 0) return handleSetDec(cmd.substr(2));

    // Action commands
    if (cmd == "MS") return handleSlew();
    if (cmd == "Q")  return handleStop();
    if (cmd == "RS") return handleGetSlewStatus();

    // Unknown command — LX200 convention: return nothing for unsupported commands
    return "";
}

// ==========================================================================
// Get Commands
// ==========================================================================

std::string LX200Server::handleGetRA() {
    auto status = controller_.getStatus();
    // For equatorial: telescope_axis1_position is HA
    // We don't have RA directly — return 00:00:00 as fallback
    return formatRA(0.0);
}

std::string LX200Server::handleGetDec() {
    auto status = controller_.getStatus();
    // telescope_axis2_position for equatorial is declination
    return formatDec(status.telescope_axis2_position);
}

std::string LX200Server::handleGetAlt() {
    return "+00*00:00";
}

std::string LX200Server::handleGetAz() {
    return "000*00:00";
}

// ==========================================================================
// Set Commands
// ==========================================================================

std::string LX200Server::handleSetRA(const std::string& value) {
    target_ra_ = parseRA(value);
    return "1";
}

std::string LX200Server::handleSetDec(const std::string& value) {
    target_dec_ = parseDec(value);
    return "1";
}

// ==========================================================================
// Action Commands
// ==========================================================================

std::string LX200Server::handleSlew() {
    try {
        controller_.slewToEquatorial(target_ra_, target_dec_);
        return "0";  // Slewing
    } catch (...) {
        return "0";
    }
}

std::string LX200Server::handleStop() {
    controller_.stop();
    return "";
}

std::string LX200Server::handleGetSlewStatus() {
    auto status = controller_.getStatus();
    switch (status.state) {
        case MountController::MountStatus::State::SLEWING:
            return "0";  // Slewing
        case MountController::MountStatus::State::TRACKING:
            return "1";  // Tracking (target reached)
        case MountController::MountStatus::State::IDLE:
            return "1";  // Idle
        default:
            return "0";
    }
}

// ==========================================================================
// Parsing / Formatting Helpers
// ==========================================================================

double LX200Server::parseRA(const std::string& str) {
    // Format: HH:MM:SS or HH:MM.S
    double ra = 0.0;
    int hh = 0, mm = 0;
    double ss = 0.0;

    if (sscanf(str.c_str(), "%d:%d:%lf", &hh, &mm, &ss) >= 2) {
        ra = hh + mm / 60.0 + ss / 3600.0;
    } else if (sscanf(str.c_str(), "%d:%lf", &hh, &ss) >= 2) {
        // HH:MM.S format
        ra = hh + ss / 60.0;
    }
    return ra;
}

double LX200Server::parseDec(const std::string& str) {
    // Format: sDD*MM:SS or sDD*MM
    double dec = 0.0;
    int sign = 1;
    int dd = 0, mm = 0;
    double ss = 0.0;

    std::string s = str;
    if (!s.empty() && s[0] == '-') { sign = -1; s = s.substr(1); }
    else if (!s.empty() && s[0] == '+') { s = s.substr(1); }

    size_t star = s.find('*');
    if (star != std::string::npos) {
        dd = std::stoi(s.substr(0, star));
        std::string rest = s.substr(star + 1);
        if (sscanf(rest.c_str(), "%d:%lf", &mm, &ss) >= 1) {
            dec = dd + mm / 60.0 + ss / 3600.0;
        }
    }
    return sign * dec;
}

std::string LX200Server::formatRA(double ra_hours) {
    if (ra_hours < 0) ra_hours += 24.0;
    if (ra_hours >= 24.0) ra_hours -= 24.0;

    int hh = static_cast<int>(ra_hours);
    double mins = (ra_hours - hh) * 60.0;
    int mm = static_cast<int>(mins);
    int ss = static_cast<int>((mins - mm) * 60.0);

    std::ostringstream oss;
    oss << std::setfill('0') << std::setw(2) << hh << ":"
        << std::setw(2) << mm << ":"
        << std::setw(2) << ss;
    return oss.str();
}

std::string LX200Server::formatDec(double dec_deg) {
    char sign = (dec_deg >= 0) ? '+' : '-';
    double abs_dec = std::abs(dec_deg);
    int dd = static_cast<int>(abs_dec);
    double mins = (abs_dec - dd) * 60.0;
    int mm = static_cast<int>(mins);
    int ss = static_cast<int>((mins - mm) * 60.0);

    std::ostringstream oss;
    oss << sign << std::setfill('0') << std::setw(2) << dd << "*"
        << std::setw(2) << mm << ":"
        << std::setw(2) << ss;
    return oss.str();
}

} // namespace controllers
} // namespace astro_mount
