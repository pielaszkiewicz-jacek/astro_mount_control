#ifndef RAIN_SENSOR_H
#define RAIN_SENSOR_H

#include <string>

namespace astro_mount {
namespace weather {

/**
 * @brief Abstract rain sensor interface
 *
 * Supports multiple hardware implementations:
 * - GPIO binary sensor (wet/dry)
 * - 1-Wire rain gauge with tipping bucket
 * - I²C capacitive rain sensor
 * - Simulated for testing
 */
class RainSensor {
public:
    virtual ~RainSensor() = default;

    /// @brief Sensor name/identifier
    virtual std::string name() const = 0;

    /// @brief Initialize the sensor
    virtual bool initialize() = 0;

    /// @brief Read rain detection status
    virtual bool readRainDetected() = 0;

    /// @brief Read current rain rate [mm/hour]
    virtual double readRainRate() = 0;

    /// @brief Read total rainfall today [mm]
    virtual double readTotalRainfall() = 0;

    /// @brief Check if sensor is operational
    virtual bool isOperational() const = 0;

    /// @brief Shutdown the sensor
    virtual void shutdown() = 0;
};

/**
 * @brief Simulated rain sensor for testing
 */
class SimulatedRainSensor : public RainSensor {
public:
    explicit SimulatedRainSensor(bool simulate_rain = false, double rate = 0.0);
    std::string name() const override { return "simulated_rain"; }
    bool initialize() override;
    bool readRainDetected() override;
    double readRainRate() override;
    double readTotalRainfall() override;
    bool isOperational() const override;
    void shutdown() override;

    void setSimulatedRain(bool detected, double rate_mmh = 0.0);

private:
    bool simulated_rain_;
    double simulated_rate_;
    double total_rainfall_{0.0};
    bool initialized_{false};
};

/**
 * @brief GPIO-based rain sensor (binary wet/dry)
 */
class GpioRainSensor : public RainSensor {
public:
    explicit GpioRainSensor(int gpio_pin, bool invert = false);
    std::string name() const override { return "gpio_rain"; }
    bool initialize() override;
    bool readRainDetected() override;
    double readRainRate() override;
    double readTotalRainfall() override;
    bool isOperational() const override;
    void shutdown() override;

private:
    int gpio_pin_;
    bool invert_;
    int fd_{-1};
};

} // namespace weather
} // namespace astro_mount

#endif // RAIN_SENSOR_H
