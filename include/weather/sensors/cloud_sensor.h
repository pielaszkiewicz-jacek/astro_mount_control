#ifndef CLOUD_SENSOR_H
#define CLOUD_SENSOR_H

#include <string>
#include <cstdint>

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
 * @brief Real cloud sensor driver — MLX90614 IR thermometer over I²C.
 *
 * Reads the object (sky) and ambient temperatures from an MLX90614 connected
 * to a Linux I²C bus (/dev/i2c-N). Cloud cover is derived from the
 * sky-minus-ambient temperature delta: a large delta means a cold, clear sky;
 * a small delta means clouds (which radiate back, warming the IR reading).
 *
 * Register map (MLX90614):
 *   - RAM 0x06 = TOBJ1 (object/sky temperature)
 *   - RAM 0x07 = TA    (ambient temperature)
 *   - Value (°C) = raw * 0.02 - 273.15
 */
class Mlx90614CloudSensor : public CloudSensor {
public:
    /// @param device_path Linux I²C device, e.g. "/dev/i2c-1"
    /// @param i2c_address 7-bit slave address in decimal (default 0x5A = 90)
    /// @param clear_delta_c Sky-ambient delta at which sky is clear (0% cover)
    /// @param cloudy_delta_c Delta at which sky is fully overcast (100% cover)
    Mlx90614CloudSensor(std::string device_path = "/dev/i2c-1",
                        int i2c_address = 0x5A,
                        double clear_delta_c = 20.0,
                        double cloudy_delta_c = 5.0);
    std::string name() const override { return "mlx90614_cloud"; }
    bool initialize() override;
    double readCloudCover() override;
    double readSkyBrightness() override;
    double readAmbientLight() override;
    double readSkyTemperature() override;
    double readAmbientTemperature() override;
    bool isOperational() const override;
    void shutdown() override;

    /// Decode a raw MLX90614 register value (0.02 °C/LSB) to °C.
    static double decodeTemperature(uint16_t raw);

    /// Derive cloud cover [0-100%] from the sky/ambient delta.
    double computeCloudCover(double sky_temp_c, double ambient_temp_c) const;

private:
    /// Read both temperatures from the I²C device (Linux SMBus-style block).
    bool readTemperatures(double& sky_temp_c, double& ambient_temp_c);

    std::string device_path_;
    int i2c_address_;
    double clear_delta_c_;
    double cloudy_delta_c_;
    int fd_{-1};
    bool initialized_{false};
    double last_sky_temp_{-20.0};
    double last_ambient_temp_{20.0};
};

/**
 * @brief Real cloud sensor driver — Boltwood Cloud Sensor II over serial.
 *
 * Reads the Boltwood cloud sensor's serial status stream (e.g. /dev/ttyUSB0,
 * 9600 baud) and parses key=value tokens such as CloudSkyTemp/SkyTemp,
 * CloudTemp/AmbientTemp, Rain, Wind. Cloud cover is derived from the
 * sky-minus-ambient temperature delta.
 */
class BoltwoodCloudSensor : public CloudSensor {
public:
    /// @param device_path Serial device, e.g. "/dev/ttyUSB0"
    /// @param baud_rate   Serial baud rate (Boltwood II defaults to 9600)
    /// @param clear_delta_c / cloudy_delta_c — cloud-cover mapping thresholds
    BoltwoodCloudSensor(std::string device_path = "/dev/ttyUSB0",
                        int baud_rate = 9600,
                        double clear_delta_c = 20.0,
                        double cloudy_delta_c = 5.0);
    std::string name() const override { return "boltwood_cloud"; }
    bool initialize() override;
    double readCloudCover() override;
    double readSkyBrightness() override;
    double readAmbientLight() override;
    double readSkyTemperature() override;
    double readAmbientTemperature() override;
    bool isOperational() const override;
    void shutdown() override;

    /// Parse a Boltwood status line into temperatures (returns false if the
    /// line carries no usable sky temperature).
    static bool parseLine(const std::string& line,
                          double& sky_temp_c, double& ambient_temp_c);

    /// Derive cloud cover [0-100%] from the sky/ambient delta.
    double computeCloudCover(double sky_temp_c, double ambient_temp_c) const;

private:
    bool readLine(std::string& line, int timeout_ms = 2000);

    std::string device_path_;
    int baud_rate_;
    double clear_delta_c_;
    double cloudy_delta_c_;
    int fd_{-1};
    bool initialized_{false};
    double last_sky_temp_{-20.0};
    double last_ambient_temp_{20.0};
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
