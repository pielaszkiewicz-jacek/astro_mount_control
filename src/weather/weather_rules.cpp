#include "weather/weather_rules.h"
#include <cmath>
#include <algorithm>

namespace astro_mount {
namespace weather {

WeatherAlertSeverity WeatherRules::evaluate(
    double temperature_c, double humidity_percent,
    double wind_speed_ms, double wind_gust_ms,
    bool rain_detected, double cloud_cover_percent,
    double dew_point_c, const WeatherRulesConfig& rules) {

    int danger_count = 0;
    int warning_count = 0;

    // Check all conditions
    if (!isTemperatureSafe(temperature_c, rules)) {
        if (temperature_c < rules.min_temperature_c || temperature_c > rules.max_temperature_c)
            danger_count++;
        else
            warning_count++;
    }

    if (!isWindSafe(wind_speed_ms, wind_gust_ms, rules)) {
        if (wind_speed_ms > rules.max_wind_speed_ms * 1.5 || wind_gust_ms > rules.max_wind_gust_ms * 1.5)
            danger_count++;
        else
            warning_count++;
    }

    if (!isRainSafe(rain_detected, 0, rules)) {
        danger_count++;  // Rain is always a danger
    }

    if (!isCloudSafe(cloud_cover_percent, rules)) {
        warning_count++;
    }

    if (!isHumiditySafe(humidity_percent, temperature_c, dew_point_c, rules)) {
        warning_count++;
    }

    // Determine overall severity
    if (danger_count > 0) {
        return WeatherAlertSeverity::WEATHER_DANGER;
    }
    if (warning_count >= 2) {
        return WeatherAlertSeverity::WEATHER_WARNING;
    }
    if (warning_count == 1) {
        return WeatherAlertSeverity::WEATHER_CAUTION;
    }
    return WeatherAlertSeverity::WEATHER_CLEAR;
}

bool WeatherRules::isTemperatureSafe(double temp_c, const WeatherRulesConfig& rules) {
    return temp_c >= rules.min_temperature_c && temp_c <= rules.max_temperature_c;
}

bool WeatherRules::isWindSafe(double wind_ms, double gust_ms, const WeatherRulesConfig& rules) {
    return wind_ms <= rules.max_wind_speed_ms && gust_ms <= rules.max_wind_gust_ms;
}

bool WeatherRules::isRainSafe(bool detected, double rate_mmh, const WeatherRulesConfig& rules) {
    if (!detected) return true;
    return rate_mmh <= rules.max_rain_rate_mmh;
}

bool WeatherRules::isCloudSafe(double cover_percent, const WeatherRulesConfig& rules) {
    return cover_percent <= rules.max_cloud_cover_percent;
}

bool WeatherRules::isHumiditySafe(double humidity, double temp_c, double dew_point_c,
                                   const WeatherRulesConfig& rules) {
    if (humidity > rules.max_humidity_percent) return false;
    return (temp_c - dew_point_c) >= rules.dew_point_tolerance_c;
}

bool WeatherRules::shouldAutoPark(WeatherAlertSeverity severity, const WeatherRulesConfig& rules) {
    return rules.auto_park_enabled && severity >= WeatherAlertSeverity::WEATHER_WARNING;
}

std::string WeatherRules::getSuggestedAction(WeatherAlertSeverity severity, const WeatherRulesConfig& rules) {
    switch (severity) {
        case WeatherAlertSeverity::WEATHER_DANGER:
            return rules.close_dome_on_danger ? "close_dome_and_park" : "park_mount";
        case WeatherAlertSeverity::WEATHER_WARNING:
            return "prepare_to_park";
        case WeatherAlertSeverity::WEATHER_CAUTION:
            return "monitor";
        default:
            return "none";
    }
}

double WeatherRules::calculateDewPoint(double temp_c, double humidity_pct) {
    double a = 17.27, b = 237.7;
    double alpha = (a * temp_c) / (b + temp_c) + std::log(std::max(humidity_pct / 100.0, 0.01));
    return (b * alpha) / (a - alpha);
}

double WeatherRules::calculateWindChill(double temp_c, double wind_ms) {
    if (wind_ms <= 0.5 || temp_c >= 10.0) return temp_c;
    double v = wind_ms * 3.6; // m/s → km/h
    return 13.12 + 0.6215 * temp_c - 11.37 * std::pow(v, 0.16) +
           0.3965 * temp_c * std::pow(v, 0.16);
}

} // namespace weather
} // namespace astro_mount
