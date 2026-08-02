#ifndef WEATHER_RULES_H
#define WEATHER_RULES_H

#include <string>
#include "proto/weather.pb.h"

namespace astro_mount {
namespace weather {

/**
 * @brief Weather safety rules configuration
 *
 * Defines thresholds and actions for weather-related safety decisions.
 * Used by WeatherMonitor to determine when to issue alerts and
 * trigger automatic safety actions (park mount, close dome).
 */
struct WeatherRulesConfig {
    // Temperature limits
    double min_temperature_c{-20.0};
    double max_temperature_c{50.0};

    // Wind limits
    double max_wind_speed_ms{15.0};     // ~54 km/h
    double max_wind_gust_ms{20.0};      // ~72 km/h

    // Rain
    bool auto_park_on_rain{true};
    double max_rain_rate_mmh{5.0};

    // Cloud cover
    double max_cloud_cover_percent{90.0};

    // Humidity/dew
    double max_humidity_percent{95.0};
    double dew_point_tolerance_c{3.0};  // Must be at least 3°C above dew point

    // General
    bool auto_park_enabled{true};
    int32_t check_interval_seconds{10};
    int32_t debounce_seconds{30};       // Wait 30s before triggering
    bool notify_on_alert{true};
    bool close_dome_on_danger{false};   // Requires DomeHAL (F5)
};

/**
 * @brief Evaluate weather data against safety rules
 *
 * Static utility functions for checking weather conditions
 * against configured thresholds and determining alert levels.
 */
class WeatherRules {
public:
    /**
     * @brief Evaluate overall weather safety
     * @return Alert severity level
     */
    static WeatherAlertSeverity evaluate(
        double temperature_c,
        double humidity_percent,
        double wind_speed_ms,
        double wind_gust_ms,
        bool rain_detected,
        double cloud_cover_percent,
        double dew_point_c,
        const WeatherRulesConfig& rules);

    /**
     * @brief Check specific conditions
     */
    static bool isTemperatureSafe(double temp_c, const WeatherRulesConfig& rules);
    static bool isWindSafe(double wind_ms, double gust_ms, const WeatherRulesConfig& rules);
    static bool isRainSafe(bool detected, double rate_mmh, const WeatherRulesConfig& rules);
    static bool isCloudSafe(double cover_percent, const WeatherRulesConfig& rules);
    static bool isHumiditySafe(double humidity, double temp_c, double dew_point_c,
                               const WeatherRulesConfig& rules);

    /**
     * @brief Check if mount should be parked based on conditions
     */
    static bool shouldAutoPark(WeatherAlertSeverity severity, const WeatherRulesConfig& rules);

    /**
     * @brief Get suggested action string for alert severity
     */
    static std::string getSuggestedAction(WeatherAlertSeverity severity, const WeatherRulesConfig& rules);

private:
    /**
     * @brief Calculate dew point using Magnus formula
     * @param temp_c Temperature [°C]
     * @param humidity_pct Relative humidity [0-100]
     * @return Dew point [°C]
     */
    static double calculateDewPoint(double temp_c, double humidity_pct);

    /**
     * @brief Calculate wind chill
     */
    static double calculateWindChill(double temp_c, double wind_ms);
};

} // namespace weather
} // namespace astro_mount

#endif // WEATHER_RULES_H
