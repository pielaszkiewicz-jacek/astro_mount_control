#ifndef DEROTATOR_TMC5160_H
#define DEROTATOR_TMC5160_H

#include "hal/derotator_control.h"
#include <nlohmann/json.hpp>
#include <cmath>
#include <cstring>
#include <mutex>
#include <thread>
#include <chrono>
#include <fstream>
#include <string>
#include <algorithm>

#ifdef __linux__
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/spi/spidev.h>
#endif

namespace astro_mount { namespace hal {

using json = nlohmann::json;

/**
 * @brief Derotator HAL for a TMC5160 stepper driver controlled over SPI.
 *
 * TMC5160 (Trinamic) is a high-performance stepper driver with an SPI register
 * interface, stealthChop2 and stallGuard2. This implementation:
 *   - opens a Linux SPI device (/dev/spidev*) with a configured speed/CS,
 *   - writes TMC5160 registers (GCONF, IHOLD_IRUN, CHOPCONF, VACTUAL, XTARGET),
 *   - drives the motor in position (XTARGET) or velocity (VACTUAL) mode,
 *   - tracks angle/rate and implements a simple homing sequence,
 *   - degrades gracefully: without an SPI device it operates in SIMULATION
 *     mode (connect()/initialize() succeed) so the rest of the system keeps
 *     working and the HAL is unit-testable.
 *
 * P6 FIX: `position_` is now integrated over time — in velocity mode from
 * `VACTUAL` (rate · dt) and in position mode by slewing toward `XTARGET` at
 * `max_speed` — and updated on every getStatus()/isMoving() call, so
 * `current_position_deg` no longer stays at 0.0 after setAngle()/setRate().
 *
 * Config JSON keys:
 *   { "type": "TMC5160", "device": "/dev/spidev0.0", "speed_hz": 4000000,
 *     "gear_ratio": 10.0, "max_speed": 5.0, "microsteps": 256,
 *     "irun": 16, "ihold": 8, "homing_offset": 0.0, "steps_per_rev": 200,
 *     "simulate": false }
 *
 * With `"simulate": true` (or an empty config path) the HAL runs fully in
 * memory: connect()/initialize() succeed and register writes are no-ops.
 */
class Tmc5160Derotator : public DerotatorControl {
public:
    explicit Tmc5160Derotator(const std::string& config_path = "")
        : simulated_(config_path.empty()) {  // no config path → simulation mode
        loadConfig(config_path);
    }

    ~Tmc5160Derotator() override {
        disconnect();
    }

    std::string name() const override { return "tmc5160_spi"; }

    bool connect() override {
        std::lock_guard<std::mutex> lock(mtx_);
        if (simulated_) { connected_ = true; return true; }
#ifdef __linux__
        if (fd_ >= 0) return true;
        fd_ = ::open(device_.c_str(), O_RDWR);
        if (fd_ < 0) return false;

        uint8_t mode = SPI_MODE_3;   // TMC5160 datasheet: SPI mode 3
        uint8_t bits = 8;
        uint32_t speed = speed_hz_;
        if (::ioctl(fd_, SPI_IOC_WR_MODE, &mode) < 0 ||
            ::ioctl(fd_, SPI_IOC_WR_BITS_PER_WORD, &bits) < 0 ||
            ::ioctl(fd_, SPI_IOC_WR_MAX_SPEED_HZ, &speed) < 0) {
            ::close(fd_);
            fd_ = -1;
            return false;
        }
        connected_ = true;
        return true;
#else
        (void)0;
        return false;
#endif
    }

    void disconnect() override {
        std::lock_guard<std::mutex> lock(mtx_);
        stopMotion();
#ifdef __linux__
        if (fd_ >= 0) { ::close(fd_); fd_ = -1; }
#endif
        connected_ = false;
    }

    bool initialize() override {
        std::lock_guard<std::mutex> lock(mtx_);
        if (!connected_) return false;
        if (simulated_) { initialized_ = true; resetMoveClock(); return true; }

        writeRegister(0x00, 0x00000001);          // GCONF: enable driver, stealthChop off for now

        uint32_t irun = static_cast<uint32_t>(std::clamp(irun_, 0, 31)) & 0x1F;
        uint32_t ihold = static_cast<uint32_t>(std::clamp(ihold_, 0, 31)) & 0x1F;
        uint32_t iholddelay = 3;                  // ~0.5 s
        writeRegister(0x10, (irun << 8) | (ihold << 0) | (iholddelay << 16));

        // CHOPCONF: MRES field (bits 24..27). TMC5160 register 0x6C.
        uint32_t mres = 4;                        // 16 µsteps default
        switch (microsteps_) {
            case 1:   mres = 8; break;  // 256
            case 2:   mres = 7; break;  // 128
            case 4:   mres = 6; break;  //  64
            case 8:   mres = 5; break;  //  32
            case 16:  mres = 4; break;  //  16
            case 32:  mres = 3; break;  //   8
            case 64:  mres = 2; break;  //   4
            case 128: mres = 1; break;  //   2
            default:  mres = 4; break;  //  16
        }
        // toff=5 (bits 0..3) enables stealthChop2; MRES in bits 24..27.
        writeRegister(0x6C, 0x00000005u | (mres << 24));
        writeRegister(0x6D, 0x00000000);          // PWMCON default (stealthChop2)

        stopMotion();
        initialized_ = true;
        return true;
    }

    bool home() override {
        std::lock_guard<std::mutex> lock(mtx_);
        if (!connected_ || !initialized_) return false;

        stopMotion();
        // Bounded search drive: step slowly towards the negative stop. On real
        // hardware this would watch stallGuard2 (TMC5160 SG_VALUE) or a home
        // switch. The bounded loop keeps the HAL self-contained and safe.
        const int STEPS = 500;
        for (int i = 0; i < STEPS; ++i) {
            writeRegister(0x6B, 0x00000040u);     // small negative VACTUAL
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        writeRegister(0x6B, 0x00000000u);         // stop
        position_ = 0.0;
        target_position_deg_ = 0.0;
        position_mode_ = false;
        homed_ = true;
        resetMoveClock();
        return true;
    }

    bool setMode(DerotatorMode m) override {
        std::lock_guard<std::mutex> lock(mtx_);
        mode_ = m;
        if (mode_ == DerotatorMode::DISABLED) {
            stopMotion();
        }
        return true;
    }

    bool setAngle(double angle_deg) override {
        std::lock_guard<std::mutex> lock(mtx_);
        if (!connected_ || !initialized_) return false;

        // Angle -> microstep position, including gear ratio.
        double steps = angle_deg * microsteps_ * steps_per_rev_ / 360.0 * gear_ratio_;
        int32_t target_steps = static_cast<int32_t>(
            std::clamp(steps, -2147483647.0, 2147483647.0));
        writeRegister(0x2D, static_cast<uint32_t>(target_steps));  // XTARGET
        target_position_deg_ = angle_deg;
        rate_ = 0.0;                             // position mode
        position_mode_ = true;
        resetMoveClock();
        return true;
    }

    bool setRate(double rate_deg_s) override {
        std::lock_guard<std::mutex> lock(mtx_);
        if (!connected_ || !initialized_) return false;

        double steps_per_s = rate_deg_s * microsteps_ * steps_per_rev_ / 360.0 * gear_ratio_;
        int32_t vactual = static_cast<int32_t>(
            std::clamp(steps_per_s, -10000000.0, 10000000.0));
        writeRegister(0x6B, static_cast<uint32_t>(vactual));      // VACTUAL
        rate_ = rate_deg_s;
        position_mode_ = false;                  // velocity mode
        resetMoveClock();
        return true;
    }

    DerotatorStatus getStatus() override {
        std::lock_guard<std::mutex> lock(mtx_);
        integratePosition();
        DerotatorStatus s;
        s.mode = mode_;
        s.homed = homed_;
        s.current_position_deg = position_;
        s.target_position_deg = target_position_deg_;
        s.current_rate_deg_s = rate_;
        // Moving: in position mode until the target is reached; in velocity
        // mode while a non-zero VACTUAL is commanded.
        s.moving = position_mode_
            ? (std::abs(target_position_deg_ - position_) > 1e-6)
            : (std::abs(rate_) > 1e-9);
        s.error = !connected_ || !initialized_;
        if (s.error) s.error_message = "TMC5160 not connected/initialised";
        return s;
    }

    bool isMoving() const override {
        std::lock_guard<std::mutex> lock(mtx_);
        integratePosition();
        return position_mode_
            ? (std::abs(target_position_deg_ - position_) > 1e-6)
            : (std::abs(rate_) > 1e-9);
    }

private:
    void loadConfig(const std::string& config_path) {
        if (config_path.empty()) return;
        std::ifstream file(config_path);
        if (!file.is_open()) return;
        try {
            json cfg = json::parse(file);
            device_         = cfg.value("device", device_);
            speed_hz_       = cfg.value("speed_hz", speed_hz_);
            gear_ratio_     = cfg.value("gear_ratio", gear_ratio_);
            max_speed_      = cfg.value("max_speed", max_speed_);
            microsteps_     = cfg.value("microsteps", microsteps_);
            irun_           = cfg.value("irun", irun_);
            ihold_          = cfg.value("ihold", ihold_);
            homing_offset_  = cfg.value("homing_offset", homing_offset_);
            steps_per_rev_  = cfg.value("steps_per_rev", steps_per_rev_);
            simulated_      = cfg.value("simulate", simulated_);
        } catch (...) {
            // Keep defaults on parse error.
        }
    }

    void stopMotion() {
        writeRegister(0x6B, 0x00000000u);  // VACTUAL = 0
        rate_ = 0.0;
        resetMoveClock();
    }

    void resetMoveClock() const {
        last_move_update_ = std::chrono::steady_clock::now();
    }

    /**
     * @brief Advance the tracked position by the elapsed wall time (P6).
     *
     * Velocity mode: position += VACTUAL·dt. Position mode: slew toward the
     * XTARGET at max_speed_ until reached. Called on every getStatus() /
     * isMoving(), so `current_position_deg` tracks the commanded motion even
     * though the SPI XACTUAL register is not read back.
     */
    void integratePosition() const {
        auto now = std::chrono::steady_clock::now();
        double dt = std::chrono::duration<double>(now - last_move_update_).count();
        // Guard large gaps (> 2 s, e.g. first call or idle) — avoid jumps.
        if (dt <= 0.0 || dt > 2.0) { last_move_update_ = now; return; }

        if (position_mode_) {
            // Position mode: move toward XTARGET at max_speed_.
            double step = max_speed_ * dt;
            double delta = target_position_deg_ - position_;
            if (std::abs(delta) <= step) position_ = target_position_deg_;
            else position_ += std::copysign(step, delta);
        } else {
            // Velocity mode: integrate VACTUAL.
            position_ += rate_ * dt;
        }
        last_move_update_ = now;
    }

    bool writeRegister(uint8_t addr, uint32_t value) {
        if (simulated_) return true;  // memory-only mode
#ifdef __linux__
        if (fd_ < 0) return false;
        // TMC5160 datagram: 1 address byte (bit 7 = write), 4 data bytes.
        uint8_t buf[5];
        buf[0] = static_cast<uint8_t>((addr & 0x3F) | 0x80);
        buf[1] = static_cast<uint8_t>((value >> 24) & 0xFF);
        buf[2] = static_cast<uint8_t>((value >> 16) & 0xFF);
        buf[3] = static_cast<uint8_t>((value >> 8) & 0xFF);
        buf[4] = static_cast<uint8_t>(value & 0xFF);
        struct spi_ioc_transfer tr;
        std::memset(&tr, 0, sizeof(tr));
        tr.tx_buf = reinterpret_cast<unsigned long>(buf);
        tr.len = sizeof(buf);
        tr.speed_hz = speed_hz_;
        tr.bits_per_word = 8;
        return ::ioctl(fd_, SPI_IOC_MESSAGE(1), &tr) >= 0;
#else
        (void)addr; (void)value;
        return false;
#endif
    }

    mutable std::mutex mtx_;
    std::string device_{"/dev/spidev0.0"};
    uint32_t speed_hz_{4000000};
    double gear_ratio_{10.0};
    double max_speed_{5.0};
    int microsteps_{256};
    int irun_{16};
    int ihold_{8};
    double homing_offset_{0.0};
    int steps_per_rev_{200};

    bool connected_{false};
    bool initialized_{false};
    bool homed_{false};
    bool simulated_{false};
    bool position_mode_{false};
    DerotatorMode mode_{DerotatorMode::DISABLED};

    // P6: position is integrated over time from the commanded rate/target.
    mutable double position_{0.0};
    double target_position_deg_{0.0};
    double rate_{0.0};
    mutable std::chrono::steady_clock::time_point last_move_update_{
        std::chrono::steady_clock::now()};

#ifdef __linux__
    int fd_{-1};
#endif
};

}} // namespace astro_mount::hal
#endif // DEROTATOR_TMC5160_H
