#include "weather/sensors/gps_receiver.h"
#include <chrono>
#include <cmath>
#include <cstring>
#include <sstream>
#include <cstdio>

#ifdef __linux__
#include <fcntl.h>
#include <unistd.h>
#include <termios.h>
#include <sys/select.h>
#include <errno.h>
#endif

namespace astro_mount {
namespace weather {

// ─── NmeaGpsReceiver ───────────────────────────────────────────────────────

NmeaGpsReceiver::NmeaGpsReceiver(std::string device_path, int baud_rate, int timeout_ms)
    : device_path_(std::move(device_path))
    , baud_rate_(baud_rate)
    , timeout_ms_(timeout_ms) {
}

double NmeaGpsReceiver::parseCoordinate(const std::string& field, char hemi) {
    if (field.empty()) return 0.0;
    double value = 0.0;
    try { value = std::stod(field); } catch (...) { return 0.0; }
    // Format ddmm.mmmm (or dddmm.mmmm for longitude).
    int deg = static_cast<int>(value / 100.0);
    double minutes = value - deg * 100.0;
    double result = deg + minutes / 60.0;
    if (hemi == 'S' || hemi == 'W') result = -result;
    return result;
}

bool NmeaGpsReceiver::parseSentence(const std::string& sentence, GpsData& data) {
    // Strip optional NMEA checksum: *hh at the end.
    std::string body = sentence;
    auto star = body.find('*');
    if (star != std::string::npos) body = body.substr(0, star);

    if (body.rfind("$G", 0) != 0 && body.rfind("$B", 0) != 0) return false;

    // Split on commas.
    std::vector<std::string> f;
    std::string token;
    std::istringstream ss(body);
    while (std::getline(ss, token, ',')) f.push_back(token);
    if (f.empty()) return false;

    std::string type = f[0].size() >= 6 ? f[0].substr(3, 3) : "";
    // "GPGGA" → f[0]="$GPGGA", type="GGA". Same for "GNGGA".

    if (type == "GGA") {
        // $--GGA,time,lat,NS,lon,EW,fix,satellites,hdop,alt,M,...
        if (f.size() < 10) return false;
        int fix = 0;
        try { fix = std::stoi(f[6]); } catch (...) {}
        data.fix_valid = (fix > 0);
        if (fix > 0 && f[2].size() >= 4 && f[4].size() >= 5) {
            data.latitude = parseCoordinate(f[2], f[3].empty() ? 'N' : f[3][0]);
            data.longitude = parseCoordinate(f[4], f[5].empty() ? 'E' : f[5][0]);
            try { data.satellites_visible = std::stoi(f[7]); } catch (...) {}
            try { data.hdop = std::stod(f[8]); } catch (...) {}
            if (f.size() > 9 && !f[9].empty()) {
                try { data.altitude_m = std::stod(f[9]); } catch (...) {}
            }
        }
        data.timestamp = std::chrono::system_clock::now();
        return true;
    }

    if (type == "RMC") {
        // $--RMC,time,status,lat,NS,lon,EW,speed,course,date,...
        if (f.size() < 5) return false;
        char status = f[2].empty() ? 'V' : f[2][0];
        data.fix_valid = (status == 'A');
        if (status == 'A' && f[3].size() >= 4 && f[5].size() >= 5) {
            data.latitude = parseCoordinate(f[3], f[4].empty() ? 'N' : f[4][0]);
            data.longitude = parseCoordinate(f[5], f[6].empty() ? 'E' : f[6][0]);
        }
        data.timestamp = std::chrono::system_clock::now();
        return true;
    }

    return false;
}

bool NmeaGpsReceiver::initialize() {
#ifdef __linux__
    fd_ = ::open(device_path_.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd_ < 0) {
        std::perror("[NmeaGpsReceiver] open serial");
        initialized_ = false;
        return false;
    }
    struct termios tio;
    if (::tcgetattr(fd_, &tio) != 0) {
        ::close(fd_); fd_ = -1;
        initialized_ = false;
        return false;
    }
    ::cfmakeraw(&tio);
    ::cfsetspeed(&tio, static_cast<speed_t>(baud_rate_));
    tio.c_cc[VMIN] = 0;
    tio.c_cc[VTIME] = 1;
    if (::tcsetattr(fd_, TCSANOW, &tio) != 0) {
        ::close(fd_); fd_ = -1;
        initialized_ = false;
        return false;
    }
    ::tcflush(fd_, TCIOFLUSH);
    initialized_ = true;
    return true;
#else
    initialized_ = false;
    return false;
#endif
}

bool NmeaGpsReceiver::readLine(std::string& line) {
#ifdef __linux__
    if (fd_ < 0) return false;
    line.clear();
    char c;
    auto deadline = std::chrono::steady_clock::now() +
                    std::chrono::milliseconds(timeout_ms_);
    while (std::chrono::steady_clock::now() < deadline) {
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(fd_, &fds);
        struct timeval tv{};
        tv.tv_sec = 0;
        tv.tv_usec = 100000;  // 100 ms
        int sel = ::select(fd_ + 1, &fds, nullptr, nullptr, &tv);
        if (sel < 0) return false;
        if (sel == 0) continue;
        ssize_t n = ::read(fd_, &c, 1);
        if (n == 1) {
            if (c == '\n') return !line.empty();
            if (c != '\r') line.push_back(c);
        } else if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
            return false;
        }
    }
    return !line.empty();
#else
    (void)line;
    return false;
#endif
}

void NmeaGpsReceiver::applySentence(const std::string& line) {
    GpsData tmp = position_;
    if (parseSentence(line, tmp)) {
        std::lock_guard<std::mutex> lock(mutex_);
        position_ = tmp;
    }
}

GpsData NmeaGpsReceiver::readPosition() {
#ifdef __linux__
    // Drain whatever sentences are available (up to a small budget) and apply
    // them, then return the latest accumulated fix.
    for (int i = 0; i < 16; ++i) {
        std::string line;
        if (!readLine(line)) break;
        if (!line.empty() && line[0] == '$') applySentence(line);
    }
#endif
    std::lock_guard<std::mutex> lock(mutex_);
    return position_;
}

bool NmeaGpsReceiver::hasFix() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return position_.fix_valid;
}

bool NmeaGpsReceiver::isOperational() const {
    return initialized_ && fd_ >= 0;
}

void NmeaGpsReceiver::shutdown() {
#ifdef __linux__
    if (fd_ >= 0) { ::close(fd_); fd_ = -1; }
#endif
    initialized_ = false;
}

// ─── SimulatedGpsReceiver ──────────────────────────────────────────────────

SimulatedGpsReceiver::SimulatedGpsReceiver(double lat, double lon, double alt) {
    position_.latitude = lat;
    position_.longitude = lon;
    position_.altitude_m = alt;
    position_.fix_valid = true;
    position_.satellites_visible = 8;
    position_.hdop = 1.2;
}

bool SimulatedGpsReceiver::initialize() {
    initialized_ = true;
    return true;
}

GpsData SimulatedGpsReceiver::readPosition() {
    position_.timestamp = std::chrono::system_clock::now();
    return position_;
}

bool SimulatedGpsReceiver::hasFix() const {
    return position_.fix_valid;
}

bool SimulatedGpsReceiver::isOperational() const {
    return initialized_;
}

void SimulatedGpsReceiver::shutdown() {
    initialized_ = false;
}

void SimulatedGpsReceiver::setSimulatedPosition(double lat, double lon, double alt) {
    position_.latitude = lat;
    position_.longitude = lon;
    position_.altitude_m = alt;
    position_.timestamp = std::chrono::system_clock::now();
}

} // namespace weather
} // namespace astro_mount
