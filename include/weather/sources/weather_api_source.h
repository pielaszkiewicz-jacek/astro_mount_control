#ifndef WEATHER_API_SOURCE_H
#define WEATHER_API_SOURCE_H

#include <string>
#include <chrono>
#include <functional>
#include "weather/weather_monitor.h"

namespace astro_mount {
namespace weather {

/**
 * @brief Configuration for an external weather API source
 */
struct WeatherApiConfig {
    std::string api_key;             // API key (if required)
    std::string location_query;      // City name, coordinates "lat,lon", or postal code
    double latitude{52.0};           // Fallback latitude
    double longitude{21.0};          // Fallback longitude
    std::string units{"metric"};     // "metric" or "imperial"
    std::string language{"en"};      // Response language
    int timeout_seconds{10};         // HTTP request timeout
    int retry_count{3};              // Retry attempts on failure
    int update_interval_minutes{10}; // How often to fetch new data
};

/**
 * @brief Generic interface for external weather API sources
 *
 * Allows plugging any weather API (OpenWeatherMap, Weather.gov, IMGW, etc.)
 * into the WeatherMonitor system. All sources return normalized WeatherData.
 */
class WeatherApiSource {
public:
    virtual ~WeatherApiSource() = default;

    /// @brief Source name identifier (e.g. "openweathermap", "weathergov", "imgw")
    virtual std::string name() const = 0;

    /// @brief Initialize the API client (validate config, test connection)
    virtual bool initialize() = 0;

    /// @brief Fetch current weather data from the API
    /// @return Normalized WeatherData, or std::nullopt on failure
    virtual bool fetchCurrent(WeatherData& data) = 0;

    /// @brief Check if the API source is operational
    virtual bool isOperational() const = 0;

    /// @brief Get the API configuration
    virtual WeatherApiConfig getConfig() const = 0;

    /// @brief Update configuration
    virtual void setConfig(const WeatherApiConfig& config) = 0;

    /// @brief Shutdown the API client
    virtual void shutdown() = 0;
};

} // namespace weather
} // namespace astro_mount

#endif // WEATHER_API_SOURCE_H
