#include "weather/weather_monitor.h"
#include "weather/weather_rules.h"
#include "weather/sources/weather_api_source.h"
#include "weather/sources/openweathermap_source.h"
#include "weather/sources/weathergov_source.h"
#include "weather/sources/imgw_source.h"
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
