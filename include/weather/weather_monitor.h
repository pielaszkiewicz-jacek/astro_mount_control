#ifndef WEATHER_MONITOR_H
#define WEATHER_MONITOR_H

#include <memory>
#include <string>
#include <vector>
#include <functional>
#include <chrono>
#include <mutex>
#include <atomic>
#include <thread>
#include "proto/weather.pb.h"
#include "weather/weather_rules.h"

namespace astro_mount {
namespace weather {

// Forward declarations
class RainSensor;
class WindSensor;
class CloudSensor;
class GpsReceiver;
class WeatherApiSource;

/**
 * @brief Complete weather status snapshot (internal representation)
 */
struct WeatherData {
    // Temperature
    double temperature_c{0.0};
    double temperature_trend{0.0};
    double dew_point_c{0.0};
    double wind_chill_c{0.0};

    // Humidity & pressure
    double humidity_percent{0.0};
    double pressure_hpa{1013.25};
    double pressure_trend{0.0};

    // Wind
    double wind_speed_ms{0.0};
    double wind_gust_ms{0.0};
    double wind_direction_deg{0.0};

    // Rain
    bool rain_detected{false};
    double rain_rate_mmh{0.0};
    double rain_total_mm{0.0};

    // Sky
    double cloud_cover_percent{0.0};
    double sky_brightness_mpsas{0.0};
    double ambient_light_lux{0.0};

    // Derived
    WeatherCondition condition{WeatherCondition::CONDITION_UNKNOWN};
    WeatherAlertSeverity alert_level{WeatherAlertSeverity::WEATHER_CLEAR};
    bool safe_to_observe{true};

    std::chrono::system_clock::time_point timestamp;
};

/**
 * @brief Weather alert event
 */
struct WeatherAlertEvent {
    std::string id;
    std::chrono::system_clock::time_point timestamp;
    WeatherAlertSeverity severity;
    std::string condition;
    std::string message;
    double current_value{0.0};
    double threshold_value{0.0};
    std::string suggested_action;
    bool auto_action_taken{false};
};

/**
 * @brief Core weather monitoring system
 *
 * Reads sensors, applies median filtering and averaging,
 * evaluates safety rules, and triggers auto-park/notifications
 * when dangerous conditions are detected.
 *
 * Thread-safe — runs sensor reads on a background timer thread.
 */
class WeatherMonitor {
public:
    WeatherMonitor();
    ~WeatherMonitor();

    // Non-copyable
    WeatherMonitor(const WeatherMonitor&) = delete;
    WeatherMonitor& operator=(const WeatherMonitor&) = delete;

    /**
     * @brief Register sensor interfaces
     */
    void setRainSensor(std::unique_ptr<RainSensor> sensor);
    void setWindSensor(std::unique_ptr<WindSensor> sensor);
    void setCloudSensor(std::unique_ptr<CloudSensor> sensor);
    void setGpsReceiver(std::unique_ptr<GpsReceiver> gps);

    /**
     * @brief Register an external weather API source
     *
     * API sources supplement or replace physical sensors.
     * Supported: OpenWeatherMap, Weather.gov, IMGW.
     * The source is polled on each monitoring cycle.
     */
    void setWeatherApiSource(std::unique_ptr<WeatherApiSource> api_source);

    /**
     * @brief Start the weather monitoring loop
     * @param interval_seconds Polling interval
     */
    bool start(int interval_seconds = 10);

    /**
     * @brief Stop the monitoring loop
     */
    void stop();

    /**
     * @brief Get current weather status
     */
    WeatherData getCurrentWeather() const;

    /**
     * @brief Get current status as protobuf
     */
    astro_mount::WeatherStatus getStatusProto() const;

    /**
     * @brief Configure safety rules
     */
    void setRules(const WeatherRulesConfig& rules);
    WeatherRulesConfig getRules() const;

    /**
     * @brief Get recent weather history
     */
    std::vector<WeatherData> getHistory(
        std::chrono::system_clock::time_point since,
        int max_points = 1000) const;

    /**
     * @brief Set callback for weather alerts
     */
    void setAlertCallback(std::function<void(const WeatherAlertEvent&)> callback);

    /**
     * @brief Force a sensor read cycle now
     */
    void forceRead();

    /**
     * @brief Get GPS position
     */
    astro_mount::GpsPosition getGpsPosition() const;

private:
    // Main monitoring loop
    void monitoringLoop();

    // Read all sensors
    void readAllSensors();

    // Apply rules and evaluate safety
    WeatherAlertSeverity evaluateRules();

    // Calculate derived values (dew point, wind chill, trends)
    void calculateDerived();

    // Add reading to history ring buffer
    void addToHistory(const WeatherData& data);

    // Sensors
    std::unique_ptr<RainSensor> rain_sensor_;
    std::unique_ptr<WindSensor> wind_sensor_;
    std::unique_ptr<CloudSensor> cloud_sensor_;
    std::unique_ptr<GpsReceiver> gps_receiver_;

    // External weather API source (e.g. OpenWeatherMap, Weather.gov, IMGW)
    std::unique_ptr<WeatherApiSource> api_source_;

    // Current state
    WeatherData current_;
    WeatherRulesConfig rules_;

    // History ring buffer
    std::vector<WeatherData> history_;
    size_t history_max_size_{3600};  // 1 hour at 1s intervals
    mutable std::mutex history_mutex_;

    // Alert callback
    std::function<void(const WeatherAlertEvent&)> alert_callback_;

    // Threading
    std::unique_ptr<std::thread> monitor_thread_;
    std::atomic<bool> running_{false};
    int poll_interval_{10};
    mutable std::mutex mutex_;
};

} // namespace weather
} // namespace astro_mount

#endif // WEATHER_MONITOR_H
