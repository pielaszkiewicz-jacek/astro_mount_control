#ifndef WIND_SENSOR_H
#define WIND_SENSOR_H

#include <string>

namespace astro_mount {
namespace weather {

/**
 * @brief Abstract wind sensor interface
 *
 * Supports:
 * - Anemometer with pulse output (GPIO)
 * - Modbus RTU wind sensor
 * - I²C digital wind sensor
 * - Simulated for testing
 */
class WindSensor {
public:
    virtual ~WindSensor() = default;

    virtual std::string name() const = 0;
    virtual bool initialize() = 0;

    /// @brief Read current wind speed [m/s]
    virtual double readWindSpeed() = 0;

    /// @brief Read wind gust speed [m/s]
    virtual double readWindGust() = 0;

    /// @brief Read wind direction [0-360°]
    virtual double readWindDirection() = 0;

    virtual bool isOperational() const = 0;
    virtual void shutdown() = 0;
};

/**
 * @brief Simulated wind sensor
 */
class SimulatedWindSensor : public WindSensor {
public:
    explicit SimulatedWindSensor(double base_speed = 2.0);
    std::string name() const override { return "simulated_wind"; }
    bool initialize() override;
    double readWindSpeed() override;
    double readWindGust() override;
    double readWindDirection() override;
    bool isOperational() const override;
    void shutdown() override;

    void setSimulatedWind(double speed, double gust, double direction);

private:
    double base_speed_;
    double current_speed_{0.0};
    double current_gust_{0.0};
    double current_direction_{0.0};
    bool initialized_{false};
};

/**
 * @brief GPIO pulse-counting anemometer
 */
class GpioWindSensor : public WindSensor {
public:
    explicit GpioWindSensor(int gpio_pin, double pulses_per_ms = 0.5);
    std::string name() const override { return "gpio_wind"; }
    bool initialize() override;
    double readWindSpeed() override;
    double readWindGust() override;
    double readWindDirection() override;
    bool isOperational() const override;
    void shutdown() override;

private:
    int gpio_pin_;
    double pulses_per_ms_;
    int fd_{-1};
};

} // namespace weather
} // namespace astro_mount

#endif // WIND_SENSOR_H
