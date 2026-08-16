#ifndef GPS_RECEIVER_H
#define GPS_RECEIVER_H

#include <string>
#include <chrono>
#include <mutex>
#include <vector>

namespace astro_mount {
namespace weather {

/**
 * @brief GPS position data
 */
struct GpsData {
    double latitude{0.0};
    double longitude{0.0};
    double altitude_m{0.0};
    std::chrono::system_clock::time_point timestamp;
    int satellites_visible{0};
    double hdop{99.9};
    bool fix_valid{false};
};

/**
 * @brief Abstract GPS receiver interface
 *
 * Supports:
 * - NMEA serial GPS (via UART/USB)
 * - I²C GPS modules
 * - Simulated for testing
 */
class GpsReceiver {
public:
    virtual ~GpsReceiver() = default;

    virtual std::string name() const = 0;
    virtual bool initialize() = 0;

    /// @brief Read current GPS position
    virtual GpsData readPosition() = 0;

    /// @brief Check if GPS has a valid fix
    virtual bool hasFix() const = 0;

    virtual bool isOperational() const = 0;
    virtual void shutdown() = 0;
};

/**
 * @brief Real GPS receiver driver — NMEA 0183 over serial (UART/USB).
 *
 * Reads NMEA sentences ($GPGGA/$GNGGA for position+fix, $GPRMC/$GNRMC for
 * speed/course) from a serial device such as /dev/ttyUSB0 or /dev/ttyAMA0.
 * Standard baud rates for GPS modules: 4800 (default), 9600, 115200.
 *
 * The sentence parser is exposed statically so it can be unit-tested without
 * hardware.
 */
class NmeaGpsReceiver : public GpsReceiver {
public:
    NmeaGpsReceiver(std::string device_path = "/dev/ttyUSB0",
                    int baud_rate = 9600,
                    int timeout_ms = 3000);
    std::string name() const override { return "nmea_gps"; }
    bool initialize() override;
    GpsData readPosition() override;
    bool hasFix() const override;
    bool isOperational() const override;
    void shutdown() override;

    /// Parse one NMEA sentence into `data`. Returns true if the sentence
    /// updated any field. Accepts GGA and RMC (with or without checksum).
    static bool parseSentence(const std::string& sentence, GpsData& data);

    /// Parse a NMEA coordinate field (ddmm.mmmm) to decimal degrees.
    static double parseCoordinate(const std::string& field, char hemi);

private:
    bool readLine(std::string& line);
    void applySentence(const std::string& line);

    std::string device_path_;
    int baud_rate_;
    int timeout_ms_;
    int fd_{-1};
    bool initialized_{false};
    mutable std::mutex mutex_;  // guards position_
    GpsData position_;
};

/**
 * @brief Simulated GPS receiver (returns fixed test position)
 */
class SimulatedGpsReceiver : public GpsReceiver {
public:
    explicit SimulatedGpsReceiver(double lat = 52.0, double lon = 21.0, double alt = 100.0);
    std::string name() const override { return "simulated_gps"; }
    bool initialize() override;
    GpsData readPosition() override;
    bool hasFix() const override;
    bool isOperational() const override;
    void shutdown() override;

    void setSimulatedPosition(double lat, double lon, double alt);

private:
    GpsData position_;
    bool initialized_{false};
};

} // namespace weather
} // namespace astro_mount

#endif // GPS_RECEIVER_H
