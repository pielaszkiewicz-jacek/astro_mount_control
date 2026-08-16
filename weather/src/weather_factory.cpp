#include "weather/weather_monitor.h"
#include "weather/weather_rules.h"
#include "weather/sources/weather_api_source.h"
#include "weather/sources/openweathermap_source.h"
#include "weather/sources/weathergov_source.h"
#include "weather/sources/imgw_source.h"
#include "weather/sensors/rain_sensor.h"
#include "weather/sensors/wind_sensor.h"
#include "weather/sensors/cloud_sensor.h"
#include "weather/sensors/gps_receiver.h"
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>

namespace astro_weather {

using astro_mount::weather::WeatherMonitor;
using astro_mount::weather::WeatherRulesConfig;
using astro_mount::weather::WeatherApiConfig;
using astro_mount::weather::WeatherApiSource;
using astro_mount::weather::OpenWeatherMapSource;
using astro_mount::weather::WeatherGovSource;
using astro_mount::weather::ImgwSource;
using astro_mount::weather::SimulatedGpsReceiver;

std::unique_ptr<WeatherMonitor>
createWeatherMonitorFromConfig(const std::string& config_path) {
    std::ifstream file(config_path);
    if (!file.is_open()) {
        std::cerr << "[WeatherFactory] Cannot open config: " << config_path << "\n";
        std::cerr << "[WeatherFactory] Creating monitor with simulated data\n";
        return std::make_unique<WeatherMonitor>();
    }

    nlohmann::json config;
    try {
        file >> config;
    } catch (const std::exception& e) {
        std::cerr << "[WeatherFactory] Failed to parse config JSON: " << e.what() << "\n";
        return std::make_unique<WeatherMonitor>();
    }

    auto monitor = std::make_unique<WeatherMonitor>();

    if (config.contains("rules")) {
        auto& r = config["rules"];
        WeatherRulesConfig rules;
        if (r.contains("min_temperature_c")) rules.min_temperature_c = r["min_temperature_c"].get<double>();
        if (r.contains("max_temperature_c")) rules.max_temperature_c = r["max_temperature_c"].get<double>();
        if (r.contains("max_wind_speed_ms")) rules.max_wind_speed_ms = r["max_wind_speed_ms"].get<double>();
        if (r.contains("max_wind_gust_ms")) rules.max_wind_gust_ms = r["max_wind_gust_ms"].get<double>();
        if (r.contains("auto_park_on_rain")) rules.auto_park_on_rain = r["auto_park_on_rain"].get<bool>();
        if (r.contains("max_rain_rate_mmh")) rules.max_rain_rate_mmh = r["max_rain_rate_mmh"].get<double>();
        if (r.contains("max_cloud_cover_percent")) rules.max_cloud_cover_percent = r["max_cloud_cover_percent"].get<double>();
        if (r.contains("max_humidity_percent")) rules.max_humidity_percent = r["max_humidity_percent"].get<double>();
        if (r.contains("dew_point_tolerance_c")) rules.dew_point_tolerance_c = r["dew_point_tolerance_c"].get<double>();
        if (r.contains("auto_park_enabled")) rules.auto_park_enabled = r["auto_park_enabled"].get<bool>();
        if (r.contains("check_interval_seconds")) rules.check_interval_seconds = r["check_interval_seconds"].get<int32_t>();
        if (r.contains("debounce_seconds")) rules.debounce_seconds = r["debounce_seconds"].get<int32_t>();
        if (r.contains("notify_on_alert")) rules.notify_on_alert = r["notify_on_alert"].get<bool>();
        if (r.contains("close_dome_on_danger")) rules.close_dome_on_danger = r["close_dome_on_danger"].get<bool>();
        monitor->setRules(rules);
    }

    // R7: wire physical/simulated sensors from the config. GPIO sensors are
    // only attached if the pin is valid AND initialize() succeeds — otherwise
    // the honest "no sensor" state is kept (isOperational() == false).
    if (config.contains("sensors")) {
        auto& s = config["sensors"];

        if (s.contains("rain") && s["rain"].value("enabled", false)) {
            int pin = s["rain"].value("gpio_pin", -1);
            if (pin >= 0) {
                auto rain = std::make_unique<astro_mount::weather::GpioRainSensor>(pin, false);
                if (rain->initialize()) monitor->setRainSensor(std::move(rain));
            } else {
                monitor->setRainSensor(
                    std::make_unique<astro_mount::weather::SimulatedRainSensor>(false, 0.0));
            }
        }

        if (s.contains("wind") && s["wind"].value("enabled", false)) {
            int pin = s["wind"].value("gpio_pin", -1);
            double cal = s["wind"].value("calibration_factor", 0.5);
            if (pin >= 0) {
                auto wind = std::make_unique<astro_mount::weather::GpioWindSensor>(pin, cal);
                if (wind->initialize()) monitor->setWindSensor(std::move(wind));
            } else {
                monitor->setWindSensor(
                    std::make_unique<astro_mount::weather::SimulatedWindSensor>(2.0));
            }
        }

        if (s.contains("cloud") && s["cloud"].value("enabled", false)) {
            // Cloud/sky sensor driver selection. Supported drivers:
            //   - "mlx90614"  — MLX90614 IR thermometer over I²C
            //   - "boltwood"  — Boltwood Cloud Sensor II over serial
            //   - "simulated" / unspecified — simulated cloud sensor
            std::string driver = s["cloud"].value("driver", "simulated");
            std::string device = s["cloud"].value("device", "/dev/i2c-1");
            int i2c_addr = s["cloud"].value("address", 0x5A);
            int baud = s["cloud"].value("baud_rate", 9600);
            double clear_delta = s["cloud"].value("clear_delta_c", 20.0);
            double cloudy_delta = s["cloud"].value("cloudy_delta_c", 5.0);

            std::unique_ptr<astro_mount::weather::CloudSensor> cloud;
            if (driver == "mlx90614") {
                cloud = std::make_unique<astro_mount::weather::Mlx90614CloudSensor>(
                    device, i2c_addr, clear_delta, cloudy_delta);
            } else if (driver == "boltwood") {
                cloud = std::make_unique<astro_mount::weather::BoltwoodCloudSensor>(
                    device, baud, clear_delta, cloudy_delta);
            } else {
                cloud = std::make_unique<astro_mount::weather::SimulatedCloudSensor>(20.0);
            }
            if (cloud->initialize()) {
                monitor->setCloudSensor(std::move(cloud));
            } else {
                std::cerr << "[WeatherFactory] Cloud driver '" << driver
                          << "' failed to initialise on '" << device
                          << "' — no cloud sensor attached\n";
            }
        }
    }

    // GPS receiver driver selection. Supported drivers:
    //   - "nmea"      — NMEA 0183 serial GPS (UART/USB)
    //   - "simulated" / unspecified — fixed test position
    if (config.contains("gps") && config["gps"].value("enabled", false)) {
        std::string driver = config["gps"].value("driver", "simulated");
        std::string device = config["gps"].value("device", "/dev/ttyUSB0");
        int baud = config["gps"].value("baud_rate", 9600);

        std::unique_ptr<astro_mount::weather::GpsReceiver> gps;
        if (driver == "nmea") {
            gps = std::make_unique<astro_mount::weather::NmeaGpsReceiver>(device, baud);
        } else {
            double lat = config["gps"].value("latitude", 52.0);
            double lon = config["gps"].value("longitude", 21.0);
            double alt = config["gps"].value("altitude_m", 100.0);
            gps = std::make_unique<SimulatedGpsReceiver>(lat, lon, alt);
        }
        if (gps->initialize()) {
            monitor->setGpsReceiver(std::move(gps));
        } else {
            std::cerr << "[WeatherFactory] GPS driver '" << driver
                      << "' failed to initialise on '" << device
                      << "' — no GPS receiver attached\n";
        }
    }

    if (config.contains("api_source")) {
        auto& ac = config["api_source"];
        WeatherApiConfig api_cfg;
        api_cfg.api_key = ac.value("api_key", "");
        api_cfg.latitude = ac.value("latitude", 52.0);
        api_cfg.longitude = ac.value("longitude", 21.0);
        api_cfg.location_query = ac.value("location_query", "");
        api_cfg.units = "metric";

        std::string provider = ac.value("provider", "");
        std::unique_ptr<WeatherApiSource> source;
        if (provider == "openweathermap") {
            source = std::make_unique<OpenWeatherMapSource>(api_cfg);
        } else if (provider == "weathergov") {
            source = std::make_unique<WeatherGovSource>(api_cfg);
        } else if (provider == "imgw") {
            source = std::make_unique<ImgwSource>(api_cfg);
        }
        if (source) monitor->setWeatherApiSource(std::move(source));
    }

    return monitor;
}

} // namespace astro_weather
