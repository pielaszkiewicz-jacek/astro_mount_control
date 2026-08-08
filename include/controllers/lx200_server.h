#ifndef LX200_SERVER_H
#define LX200_SERVER_H

#include <string>
#include <memory>
#include <thread>
#include <atomic>
#include <mutex>

namespace astro_mount {
namespace controllers {

// Forward declaration
class MountController;

/**
 * @brief LX200 serial protocol server
 *
 * Listens on a serial port for LX200 commands from planetarium software
 * (Stellarium, Cartes du Ciel, SkySafari, etc.) and translates them
 * into MountController calls.
 *
 * Supported LX200 commands:
 *   :GD#  - Get Declination
 *   :GR#  - Get Right Ascension
 *   :GA#  - Get Altitude
 *   :GZ#  - Get Azimuth
 *   :GVD# - Get firmware date
 *   :GVN# - Get firmware version number
 *   :GVP# - Get product name
 *   :GVT# - Get telescope focal length
 *   :Sd+DD*MM:SS# - Set target Declination
 *   :SrHH:MM:SS#  - Set target Right Ascension
 *   :MS#  - Slew to target
 *   :Q#   - Stop all movement
 *   :RS#  - Get slew status
 *
 * Thread-safe: all serial I/O is serialized through a mutex.
 */
class LX200Server {
public:
    struct Config {
        bool enabled{false};
        std::string port{"/dev/ttyUSB0"};
        int baud_rate{9600};
    };

    /**
     * @brief Construct an LX200 server.
     * @param controller Reference to the mount controller for command dispatch
     * @param config Serial port and protocol configuration
     */
    LX200Server(MountController& controller, const Config& config);

    ~LX200Server();

    // Non-copyable, non-movable
    LX200Server(const LX200Server&) = delete;
    LX200Server& operator=(const LX200Server&) = delete;

    /**
     * @brief Start the LX200 serial server thread.
     * @return true if the serial port was opened and the thread started
     */
    bool start();

    /**
     * @brief Stop the server thread and close the serial port.
     */
    void stop();

    /**
     * @brief Check if the server is currently running.
     */
    bool isRunning() const { return running_.load(); }

    /**
     * @brief Update the server configuration (restarts if running).
     */
    void reconfigure(const Config& config);

private:
    /**
     * @brief Main server loop — runs on a dedicated thread.
     */
    void serverLoop();

    /**
     * @brief Open the serial port with the configured parameters.
     * @return true on success
     */
    bool openPort();

    /**
     * @brief Close the serial port.
     */
    void closePort();

    /**
     * @brief Read a single line from the serial port (terminated by '#').
     * @param timeout_ms Timeout in milliseconds
     * @return The command string (without '#') or empty on timeout/error
     */
    std::string readCommand(int timeout_ms);

    /**
     * @brief Write a response to the serial port.
     * @param response The response string (without terminator)
     */
    void writeResponse(const std::string& response);

    /**
     * @brief Process a single LX200 command and return the response.
     * @param cmd The command string (without leading ':' and trailing '#')
     * @return The response string to send back
     */
    std::string processCommand(const std::string& cmd);

    // ── LX200 command handlers ──
    std::string handleGetRA();
    std::string handleGetDec();
    std::string handleGetAlt();
    std::string handleGetAz();
    std::string handleSetRA(const std::string& value);
    std::string handleSetDec(const std::string& value);
    std::string handleSlew();
    std::string handleStop();
    std::string handleGetSlewStatus();

    // Parsing helpers
    static double parseRA(const std::string& str);   // HH:MM:SS → hours
    static double parseDec(const std::string& str);  // sDD*MM:SS → degrees
    static std::string formatRA(double ra_hours);     // hours → HH:MM:SS#
    static std::string formatDec(double dec_deg);     // degrees → sDD*MM:SS#

    MountController& controller_;
    Config config_;

    int serial_fd_{-1};
    std::atomic<bool> running_{false};
    std::unique_ptr<std::thread> server_thread_;
    mutable std::mutex mutex_;

    // Target coordinates for slew
    double target_ra_{0.0};
    double target_dec_{0.0};
};

} // namespace controllers
} // namespace astro_mount

#endif // LX200_SERVER_H
