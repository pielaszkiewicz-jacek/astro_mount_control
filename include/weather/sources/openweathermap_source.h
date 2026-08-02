#ifndef OPENWEATHERMAP_SOURCE_H
#define OPENWEATHERMAP_SOURCE_H

#include "weather/sources/weather_api_source.h"
#include <string>
#include <map>

namespace astro_mount {
namespace weather {

/**
 * @brief OpenWeatherMap API integration
 *
 * Fetches current weather data from OpenWeatherMap One Call API 3.0.
 * Requires an API key (free tier available at https://openweathermap.org/api).
 *
 * API: https://api.openweathermap.org/data/3.0/onecall
 * Free tier: 1,000 calls/day
 *
 * Maps OpenWeatherMap response fields to WeatherData:
 * - current.temp → temperature_c
 * - current.humidity → humidity_percent
 * - current.pressure → pressure_hpa
 * - current.wind_speed → wind_speed_ms
 * - current.wind_gust → wind_gust_ms
 * - current.wind_deg → wind_direction_deg
 * - current.rain.1h → rain_rate_mmh
 * - current.clouds → cloud_cover_percent
 * - current.dew_point → dew_point_c
 * - current.uvi → (UV index, stored in metadata)
 * - minutely precipitation → rain_rate_mmh (short-term forecast)
 */
class OpenWeatherMapSource : public WeatherApiSource {
public:
    explicit OpenWeatherMapSource(WeatherApiConfig config = {});
    ~OpenWeatherMapSource() override;

    std::string name() const override { return "openweathermap"; }
    bool initialize() override;
    bool fetchCurrent(WeatherData& data) override;
    bool isOperational() const override;
    WeatherApiConfig getConfig() const override;
    void setConfig(const WeatherApiConfig& config) override;
    void shutdown() override;

private:
    /// @brief Build the API request URL
    std::string buildUrl() const;

    /// @brief Parse JSON response into WeatherData
    bool parseResponse(const std::string& json, WeatherData& data);

    /// @brief Perform HTTP GET request (via libcurl or equivalent)
    bool httpGet(const std::string& url, std::string& response);

    WeatherApiConfig config_;
    bool initialized_{false};
    std::string base_url_{"https://api.openweathermap.org/data/3.0/onecall"};
};

} // namespace weather
} // namespace astro_mount

#endif // OPENWEATHERMAP_SOURCE_H
