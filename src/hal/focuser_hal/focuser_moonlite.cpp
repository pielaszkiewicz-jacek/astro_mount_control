#include "hal/focuser_control.h"
#include <thread>
#include <chrono>
#include <sstream>
#include <algorithm>

namespace astro_mount {
namespace hal {

/**
 * @brief MoonLite Focuser implementation
 *
 * Communication via serial RS-232 using MoonLite command protocol.
 * Commands are ASCII-based over USB-serial adapter.
 *
 * Protocol:
 * - Position query:  :GP#
 * - Move to:         :MN+{steps}#
 * - Temperature:     :GT#
 * - Halt:            :FH#
 * - Max position:    :Gmax#
 *
 * Response format:  {value}#  or  {value}|  or  {value}#
 */
class MoonLiteFocuser : public FocuserControl {
public:
    explicit MoonLiteFocuser(const std::string& serial_port = "/dev/ttyUSB0",
                              int baud_rate = 9600)
        : serial_port_(serial_port), baud_rate_(baud_rate) {}

    std::string name() const override { return "MoonLite"; }

    bool connect() override {
        // TODO: Open serial port
        // fd_ = open(serial_port_.c_str(), O_RDWR | O_NOCTTY);
        // Configure serial: 9600 baud, 8N1
        connected_ = true;
        return true;
    }

    void disconnect() override {
        if (connected_) {
            // close(fd_);
            // fd_ = -1;
            connected_ = false;
        }
    }

    bool isConnected() const override { return connected_; }

    bool moveTo(int32_t position, int32_t speed, bool synchronous) override {
        if (!connected_) return false;

        position = std::clamp(position, int32_t(0), max_position_);
        moving_ = true;

        // Send MoonLite move command: ":MN+{steps}#"
        std::ostringstream cmd;
        cmd << ":MN+" << position << "#";
        // write(fd_, cmd.str().c_str(), cmd.str().length());

        if (synchronous) {
            // Poll until movement complete
            while (isMoving()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                updatePosition();
            }
        }

        position_ = position;
        moving_ = false;
        return true;
    }

    void halt() override {
        if (connected_) {
            // Send halt command: ":FH#"
            const char* cmd = ":FH#";
            // write(fd_, cmd, strlen(cmd));
            moving_ = false;
        }
    }

    FocuserStatus getStatus() override {
        FocuserStatus status;
        updatePosition();
        status.position = position_;
        status.max_position = max_position_;
        status.moving = moving_;
        status.connected = connected_;
        status.temperature_c = readTemperature();
        status.model_name = "MoonLite Focuser";
        status.step_size_microns = 10;  // Typical MoonLite step
        return status;
    }

    double readTemperature() override {
        // Send temperature query: ":GT#"
        // Parse response: "{temp_c}#
        return temperature_;
    }

    bool initialize() override {
        // Query max position: ":Gmax#"
        // Parse response
        return true;
    }

    bool isReady() const override { return connected_; }

    int32_t getMaxPosition() const override { return max_position_; }

private:
    void updatePosition() {
        // Send position query: ":GP#"
        // Parse response: "{steps}#
        // sscanf(response, "%d#", &position_);
    }

    bool isMoving() {
        // Send status query
        return moving_;
    }

    std::string serial_port_;
    int baud_rate_;
    int32_t max_position_{100000};
    int32_t position_{0};
    bool moving_{false};
    bool connected_{false};
    double temperature_{20.0};
    // int fd_{-1};  // Serial file descriptor
};

} // namespace hal
} // namespace astro_mount
