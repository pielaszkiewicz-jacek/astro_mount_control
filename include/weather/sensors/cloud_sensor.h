#ifndef CLOUD_SENSOR_H
#define CLOUD_SENSOR_H

#include <string>

namespace astro_mount {
namespace weather {

/**
 * @brief Abstract cloud/sky sensor interface
 *
 * Supports:
 * - MLX90614 IR temperature sensor (sky temperature → cloud detection)
 * - Boltwood cloud sensor (analog)
 * - SQM (Sky Quality Meter) for sky brightness
 * - Simulated for testing
 */
class CloudSensor {
public:
    virtual ~CloudSensor() = default;

    virtual std::string name() const = 0;
    virtual bool initialize() = 0;

    /// @brief Read cloud cover percentage [0-100%]
    virtual double readCloudCover() = 0;

    /// @brief Read sky brightness [mag/arcsec²]
    virtual double readSkyBrightness() = 0;

    /// @brief Read ambient light level [lux]
    virtual double readAmbientLight() = 0;

    /// @brief Read sky temperature [°C] (IR sensor)
    virtual double readSkyTemperature() = 0;

    /// @brief Read ambient temperature [°C]
    virtual double readAmbientTemperature() = 0;

    virtual bool isOperational() const = 0;
    virtual void shutdown() = 0;
};

/**
 * @brief Simulated cloud/sky sensor
 */
class SimulatedCloudSensor : public CloudSensor {
public:
    explicit SimulatedCloudSensor(double cover = 20.0);
    std::string name() const override { return "simulated_cloud"; }
    bool initialize() override;
    double readCloudCover() override;
    double readSkyBrightness() override;
    double readAmbientLight() override;
    double readSkyTemperature() override;
    double readAmbientTemperature() override;
    bool isOperational() const override;
    void shutdown() override;

    void setSimulatedConditions(double cover_percent, double sky_temp_c = -10.0);

private:
    double simulated_cover_;
    double simulated_sky_temp_;
    double ambient_temp_{20.0};
    bool initialized_{false};
};

} // namespace weather
} // namespace astro_mount

#endif // CLOUD_SENSOR_H
