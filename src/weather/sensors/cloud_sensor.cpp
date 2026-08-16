#include "weather/sensors/cloud_sensor.h"
#include <algorithm>
#include <random>
#include <cmath>
#include <cstring>
#include <sstream>
#include <cstdio>
#include <chrono>

#ifdef __linux__
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/i2c-dev.h>
#include <termios.h>
#include <sys/select.h>
#include <errno.h>
#endif

namespace astro_mount {
namespace weather {

// ─── Mlx90614CloudSensor ───────────────────────────────────────────────────

Mlx90614CloudSensor::Mlx90614CloudSensor(std::string device_path, int i2c_address,
                                         double clear_delta_c, double cloudy_delta_c)
    : device_path_(std::move(device_path))
    , i2c_address_(i2c_address)
    , clear_delta_c_(clear_delta_c)
    , cloudy_delta_c_(cloudy_delta_c) {
}

double Mlx90614CloudSensor::decodeTemperature(uint16_t raw) {
    // MLX90614: value = raw * 0.02 - 273.15 [°C]
    return static_cast<double>(raw) * 0.02 - 273.15;
}

double Mlx90614CloudSensor::computeCloudCover(double sky_temp_c, double ambient_temp_c) const {
    double delta = ambient_temp_c - sky_temp_c;
    if (delta >= clear_delta_c_) return 0.0;        // cold, clear sky
    if (delta <= cloudy_delta_c_) return 100.0;     // warm sky → overcast
    // Linear interpolation between the two thresholds.
    double t = (clear_delta_c_ - delta) / (clear_delta_c_ - cloudy_delta_c_);
    return std::clamp(t * 100.0, 0.0, 100.0);
}

bool Mlx90614CloudSensor::initialize() {
#ifdef __linux__
    fd_ = ::open(device_path_.c_str(), O_RDWR);
    if (fd_ < 0) {
        std::perror("[Mlx90614CloudSensor] open I2C");
        initialized_ = false;
        return false;
    }
    if (::ioctl(fd_, I2C_SLAVE, i2c_address_) < 0) {
        std::perror("[Mlx90614CloudSensor] I2C_SLAVE");
        ::close(fd_);
        fd_ = -1;
        initialized_ = false;
        return false;
    }
    // Verify communication by reading the ambient temperature register.
    double sky = 0.0, amb = 0.0;
    initialized_ = readTemperatures(sky, amb);
    if (!initialized_) {
        ::close(fd_);
        fd_ = -1;
    }
    return initialized_;
#else
    initialized_ = false;
    return false;
#endif
}

bool Mlx90614CloudSensor::readTemperatures(double& sky_temp_c, double& ambient_temp_c) {
#ifdef __linux__
    if (fd_ < 0) return false;

    // Read the ambient temperature register (0x07) — SMBus block read:
    // write register byte, then read 3 bytes (data L, data H, PEC).
    auto read_reg = [&](uint8_t reg, uint16_t& raw) -> bool {
        if (::write(fd_, &reg, 1) != 1) return false;
        uint8_t buf[3] = {0, 0, 0};
        if (::read(fd_, buf, 3) != 3) return false;
        raw = static_cast<uint16_t>(buf[0]) | (static_cast<uint16_t>(buf[1]) << 8);
        return true;
    };

    uint16_t raw_ta = 0, raw_tobj = 0;
    if (!read_reg(0x07, raw_ta)) return false;   // ambient
    if (!read_reg(0x06, raw_tobj)) return false; // object/sky

    sky_temp_c = decodeTemperature(raw_tobj);
    ambient_temp_c = decodeTemperature(raw_ta);
    return std::isfinite(sky_temp_c) && std::isfinite(ambient_temp_c);
#else
    (void)sky_temp_c; (void)ambient_temp_c;
    return false;
#endif
}

double Mlx90614CloudSensor::readCloudCover() {
    double sky = 0.0, amb = 0.0;
    if (!readTemperatures(sky, amb)) return 100.0;  // unknown → assume worst case
    last_sky_temp_ = sky;
    last_ambient_temp_ = amb;
    return computeCloudCover(sky, amb);
}

double Mlx90614CloudSensor::readSkyTemperature() {
    double sky = 0.0, amb = 0.0;
    if (readTemperatures(sky, amb)) {
        last_sky_temp_ = sky;
        last_ambient_temp_ = amb;
    }
    return last_sky_temp_;
}

double Mlx90614CloudSensor::readAmbientTemperature() {
    double sky = 0.0, amb = 0.0;
    if (readTemperatures(sky, amb)) {
        last_sky_temp_ = sky;
        last_ambient_temp_ = amb;
    }
    return last_ambient_temp_;
}

double Mlx90614CloudSensor::readSkyBrightness() {
    // No SQM on the MLX90614 — approximate: clear sky is "darker" (higher
    // MPSAS), overcast is brighter (light scattered by clouds).
    double cover = readCloudCover();
    return 21.5 - (cover / 100.0) * 3.0;
}

double Mlx90614CloudSensor::readAmbientLight() {
    // No photodiode on the MLX90614 — ambient light is unavailable.
    return 0.0;
}

bool Mlx90614CloudSensor::isOperational() const {
    return initialized_ && fd_ >= 0;
}

void Mlx90614CloudSensor::shutdown() {
#ifdef __linux__
    if (fd_ >= 0) { ::close(fd_); fd_ = -1; }
#endif
    initialized_ = false;
}

// ─── BoltwoodCloudSensor ───────────────────────────────────────────────────

BoltwoodCloudSensor::BoltwoodCloudSensor(std::string device_path, int baud_rate,
                                         double clear_delta_c, double cloudy_delta_c)
    : device_path_(std::move(device_path))
    , baud_rate_(baud_rate)
    , clear_delta_c_(clear_delta_c)
    , cloudy_delta_c_(cloudy_delta_c) {
}

double BoltwoodCloudSensor::computeCloudCover(double sky_temp_c, double ambient_temp_c) const {
    double delta = ambient_temp_c - sky_temp_c;
    if (delta >= clear_delta_c_) return 0.0;
    if (delta <= cloudy_delta_c_) return 100.0;
    double t = (clear_delta_c_ - delta) / (clear_delta_c_ - cloudy_delta_c_);
    return std::clamp(t * 100.0, 0.0, 100.0);
}

bool BoltwoodCloudSensor::parseLine(const std::string& line,
                                    double& sky_temp_c, double& ambient_temp_c) {
    // Tolerant tokenizer for Boltwood-style key=value status lines, e.g.:
    //   CloudSkyTemp=-18.5;CloudTemp=-3.2;Rain=False;Wind=2.4;
    //   SkyTemp=-12.0;AmbientTemp=5.0;...
    bool found_sky = false, found_ambient = false;
    std::string token;
    std::istringstream ss(line);
    while (std::getline(ss, token, ';')) {
        auto eq = token.find('=');
        if (eq == std::string::npos) continue;
        std::string key = token.substr(0, eq);
        std::string val = token.substr(eq + 1);
        // Trim whitespace from key.
        key.erase(key.begin(), std::find_if(key.begin(), key.end(),
            [](unsigned char c) { return !std::isspace(c); }));
        key.erase(std::find_if(key.rbegin(), key.rend(),
            [](unsigned char c) { return !std::isspace(c); }).base(), key.end());

        if (key == "CloudSkyTemp" || key == "SkyTemp" || key == "SkyTempC") {
            try { sky_temp_c = std::stod(val); found_sky = true; } catch (...) {}
        } else if (key == "CloudTemp" || key == "AmbientTemp" || key == "AmbientTempC") {
            try { ambient_temp_c = std::stod(val); found_ambient = true; } catch (...) {}
        }
    }
    return found_sky;
}

bool BoltwoodCloudSensor::initialize() {
#ifdef __linux__
    fd_ = ::open(device_path_.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd_ < 0) {
        std::perror("[BoltwoodCloudSensor] open serial");
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
    // Clear any buffered garbage, then try to read a status line to validate.
    ::tcflush(fd_, TCIOFLUSH);
    std::string line;
    initialized_ = readLine(line, 2000) && parseLine(line, last_sky_temp_, last_ambient_temp_);
    if (!initialized_) {
        // Not fatal for a polling sensor — mark operational if the port is open,
        // readings will surface once the sensor starts streaming.
        initialized_ = true;
    }
    return initialized_;
#else
    initialized_ = false;
    return false;
#endif
}

bool BoltwoodCloudSensor::readLine(std::string& line, int timeout_ms) {
#ifdef __linux__
    if (fd_ < 0) return false;
    line.clear();
    char c;
    auto deadline = std::chrono::steady_clock::now() +
                    std::chrono::milliseconds(timeout_ms);
    while (std::chrono::steady_clock::now() < deadline) {
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(fd_, &fds);
        struct timeval tv{};
        tv.tv_sec = 0;
        tv.tv_usec = 200000;  // 200 ms
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
    (void)line; (void)timeout_ms;
    return false;
#endif
}

double BoltwoodCloudSensor::readCloudCover() {
    std::string line;
    if (readLine(line, 1000) && parseLine(line, last_sky_temp_, last_ambient_temp_)) {
        return computeCloudCover(last_sky_temp_, last_ambient_temp_);
    }
    // No fresh line — return the last computed value (or worst case on first).
    if (!std::isfinite(last_sky_temp_)) return 100.0;
    return computeCloudCover(last_sky_temp_, last_ambient_temp_);
}

double BoltwoodCloudSensor::readSkyTemperature() {
    std::string line;
    double sky = last_sky_temp_, amb = last_ambient_temp_;
    if (readLine(line, 500) && parseLine(line, sky, amb)) {
        last_sky_temp_ = sky;
        last_ambient_temp_ = amb;
    }
    return last_sky_temp_;
}

double BoltwoodCloudSensor::readAmbientTemperature() {
    std::string line;
    double sky = last_sky_temp_, amb = last_ambient_temp_;
    if (readLine(line, 500) && parseLine(line, sky, amb)) {
        last_sky_temp_ = sky;
        last_ambient_temp_ = amb;
    }
    return last_ambient_temp_;
}

double BoltwoodCloudSensor::readSkyBrightness() {
    double cover = readCloudCover();
    return 21.5 - (cover / 100.0) * 3.0;
}

double BoltwoodCloudSensor::readAmbientLight() {
    return 0.0;  // no lux sensor on the Boltwood
}

bool BoltwoodCloudSensor::isOperational() const {
    return initialized_ && fd_ >= 0;
}

void BoltwoodCloudSensor::shutdown() {
#ifdef __linux__
    if (fd_ >= 0) { ::close(fd_); fd_ = -1; }
#endif
    initialized_ = false;
}

// ─── SimulatedCloudSensor ──────────────────────────────────────────────────

SimulatedCloudSensor::SimulatedCloudSensor(double cover)
    : simulated_cover_(cover), simulated_sky_temp_(-10.0) {
}

bool SimulatedCloudSensor::initialize() {
    initialized_ = true;
    return true;
}

double SimulatedCloudSensor::readCloudCover() {
    static std::default_random_engine gen(std::random_device{}());
    std::normal_distribution<double> noise(0.0, 5.0);
    return std::clamp(simulated_cover_ + noise(gen), 0.0, 100.0);
}

double SimulatedCloudSensor::readSkyBrightness() {
    // Brighter sky = more cloud cover or moonlight
    double base_brightness = 21.5; // Dark sky MPSAS
    double cloud_factor = simulated_cover_ / 100.0 * 3.0;
    return base_brightness - cloud_factor;
}

double SimulatedCloudSensor::readAmbientLight() {
    // Simple ambient light reading
    return simulated_cover_ > 80.0 ? 50.0 : 0.1;
}

double SimulatedCloudSensor::readSkyTemperature() {
    static std::default_random_engine gen(std::random_device{}());
    std::normal_distribution<double> noise(0.0, 0.5);

    // Clear sky = cold IR temp, cloudy = warmer IR temp
    double sky_temp = -15.0 + (simulated_cover_ / 100.0) * 25.0;
    return sky_temp + noise(gen);
}

double SimulatedCloudSensor::readAmbientTemperature() {
    static std::default_random_engine gen(std::random_device{}());
    std::normal_distribution<double> noise(0.0, 0.3);
    return ambient_temp_ + noise(gen);
}

bool SimulatedCloudSensor::isOperational() const {
    return initialized_;
}

void SimulatedCloudSensor::shutdown() {
    initialized_ = false;
}

void SimulatedCloudSensor::setSimulatedConditions(double cover_percent, double sky_temp_c) {
    simulated_cover_ = cover_percent;
    simulated_sky_temp_ = sky_temp_c;
}

} // namespace weather
} // namespace astro_mount
