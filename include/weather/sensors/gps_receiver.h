#ifndef GPS_RECEIVER_H
#define GPS_RECEIVER_H

#include <string>
#include <chrono>

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
